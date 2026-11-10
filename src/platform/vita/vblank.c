/* Vita time (decisions D28). By default the game's vblanks follow the
 * display's own counter (sceDisplayGetVcount, widened from its 16 bits by
 * vita-port-kit's vpk_vcount.h, D90): VSync(0) waits for the next
 * real vblank if the game is early, and if it is late runs every vblank it
 * missed (pad, CD, SPU, the game's VSync callback), as the PS1's interrupt
 * would have, so the game's delta time keeps its speed and the audio stays
 * continuous; the picture is shown once per VSync call. realtime=0 keeps the
 * linux32 host's virtual time (D17: VSync(0) is exactly one vblank), so
 * scripted test runs replay exactly. The clock, its catch-up arithmetic and
 * VSync itself are shared with linux32 (common/clock.c, libetc.c, D104).
 * Also PsyX's scene hooks (isbg clears), the same as linux32's. */
#include "../../game/tick60.h"
#include "vita.h"
#include "vpk_vcount.h"
#include "watchdog.h"
#include "psx_arena.h"
#include "psx_cdimage.h"
#include "psx_cdnative.h"
#include "psx_clock.h"
#include "psx_hooks.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>

#include "psx/libetc.h"
#include "psx_gpu.h"

int host_shot_display(const char *path); /* linux32/png.c */

static SceUInt64 s_stat_t0;
static int s_stat_v0, s_stat_shown, s_stat_late;
static VpkVcount s_vc;
static int s_hold; /* shot_hold: a shot's vblank, held at the next present */

int vita_vblank_count(void) { return psx_clock_count(); }

/* A save state was loaded (D51): its vblank count from now on, counted
 * against the display afresh, and a new stats window. */
void vita_set_vblank(int vblank) {
  psx_clock_set(vblank);
  s_stat_v0 = vblank;
  s_stat_shown = s_stat_late = 0;
  s_stat_t0 = 0;
}
int host_vblank_count(void) { return psx_clock_count(); } /* linux32/gpu_soft.c */

static int shot_due(int v) {
  int i;
  if (g_vita_opts.shot_every > 0 && v % g_vita_opts.shot_every == 0)
    return 1;
  for (i = 0; i < g_vita_opts.nshots; i++)
    if (g_vita_opts.shots[i] == v)
      return 1;
  return 0;
}

/* Disc reads since the start (both disc layouts, D60): file-system calls,
 * data and the time they took. Only when there were new ones. */
static void cd_report(void) {
  static unsigned last;
  PsxCdIo io;
  psx_cd_io(&io);
  if (io.reads == last)
    return;
  last = io.reads;
  vita_log("cd: %u reads, %u KB, %u ms in the file system since the start; %u CdReads, the "
           "game thread waited %u ms for them",
           io.reads, io.kbytes, (unsigned)(io.us / 1000), io.loads, (unsigned)(io.wait_us / 1000));
  if (g_psx_cd_native) {
    PsxCdnStats st;
    psx_cdn_stats(&st);
    vita_log("cd: cd_native's worker: %u.%02u ms on the CdReads; %u of them after a stream chunk",
             (unsigned)(st.job_us / 1000), (unsigned)(st.job_us % 1000 / 10), st.behind_chunk);
  }
}

static void vblank(void) {
  int v = psx_clock_tick(), i;
  SceUInt64 now;

  vita_watchdog_vblank(v); /* "where" lines, hang_test= (D73) */
  vita_pad_poll();
  vita_pad_vblank();
  prof_enter(PROF_SPU);
  psx_cd_vblank();
  vita_mixer_vblank(); /* psx_spu_vblank on the mixer thread (mixer.c) */
  prof_leave();
  psx_clock_callback();
  prof_vblank();

  for (i = 0; i < g_vita_opts.npokes; i++)
    if (g_vita_opts.pokes[i].vblank == v) {
      *(uint32_t *)PSX_ARENA(g_vita_opts.pokes[i].addr) = g_vita_opts.pokes[i].value;
      vita_log("poke %08x = %08x", (unsigned)g_vita_opts.pokes[i].addr,
               (unsigned)g_vita_opts.pokes[i].value);
    }
  vita_bench_vblank(v);
  if (shot_due(v)) {
    char path[256];
    s_hold = v;
    if (g_vita_gpu) { /* the shot reads VRAM: bring in what the GPU drew */
      vita_gpu_sync_display();
      if (g_vita_opts.shot_hd)
        vita_gpu_shot_hd(v);
    }
    snprintf(path, sizeof path, VITA_DATA_DIR "/shots/shot-%d.png", v);
    if (host_shot_display(path) == 0)
      vita_log("shot %s", path);
  }
  now = sceKernelGetProcessTimeWide();
  if (!s_stat_t0)
    s_stat_t0 = now;
  if (g_vita_opts.stats && v - s_stat_v0 >= 300) {
    unsigned us = (unsigned)(now - s_stat_t0);
    int window = v - s_stat_v0;
    vita_log("vblank %d: %d vblanks in %u ms (%u.%u per second), %d pictures, %d vblanks late",
             v, v - s_stat_v0, us / 1000, (v - s_stat_v0) * 1000000u / us,
             (v - s_stat_v0) * 10000000u / us % 10, s_stat_shown, s_stat_late);
    s_stat_t0 = now;
    s_stat_v0 = v;
    s_stat_shown = s_stat_late = 0;
    prof_report(v);
    prof_draw_report(v, window);
    vita_mixer_report(v);
    if (g_vita_gpu)
      vita_gpu_report(v);
    vita_parse_report(v);
    vita_pres_report(v);
    nd_report(v);
    vita_present_report(v);
    vita_picture_report(v);
    cd_report();
    vita_xa_ahead_report(v);
  }
  /* nothing presses a button in a scripted run: keep the Vita from
   * dimming and sleeping, which drops the Wi-Fi hw_run.sh polls over */
  if (g_vita_opts.keep_awake && v % 60 == 0) {
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING);
  }
  if (g_vita_opts.frames > 0 && v >= g_vita_opts.frames) {
    vita_audio_close();
    vita_log("frames=%d reached, exiting", g_vita_opts.frames);
    tick60_report(vita_log); /* 60 Hz logic's counts (D130) */
    vita_exit(0); /* the render thread first (D120) */
  }
}

/* shot_hold: after the shot's picture is shown, hold.req (its vblank) asks
 * for a window capture and the game waits for hold.ack. Files, not the log:
 * Vita3K writes the log to the host in 4 KB blocks. */
static void hold(void);

static void shown_pictures(int shown) {
  if (shown)
    vita_boot_close(); /* the disc screen's framebuffer isn't shown any more (D60) */
  s_stat_shown += shown;
  vita_bench_present(shown);
}

static void present(void) {
  shown_pictures(vita_video_present());
  hold();
}

static void hold(void) {
  if (s_hold && g_vita_opts.shot_hold > 0) {
    char buf[16];
    SceIoStat st;
    SceUID fd;
    int ms, n = snprintf(buf, sizeof buf, "%d\n", s_hold);
    for (ms = 0; ms < 3 && g_vita_gl; ms++) /* Vita3K can show an older swap buffer */
      vita_gpu_repeat();
    sceKernelDelayThread(100 * 1000); /* the flip */
    fd = sceIoOpen(VITA_DATA_DIR "/hold.req", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
      sceIoWrite(fd, buf, n);
      sceIoClose(fd);
    }
    for (ms = 0; ms < g_vita_opts.shot_hold && sceIoGetstat(VITA_DATA_DIR "/hold.ack", &st) < 0;
         ms += 20)
      sceKernelDelayThread(20 * 1000);
    sceIoRemove(VITA_DATA_DIR "/hold.ack");
  }
  s_hold = 0;
}

/* The display's vcount, widened (D90). vcount_test=N (tests) moves its 16
 * bits so they wrap N vblanks after the first read, not up to 18 minutes on. */
static volatile unsigned s_vc_offset;

static int vcount(void) {
  unsigned raw = (unsigned)sceDisplayGetVcount();
  if (!s_vc.valid && g_vita_opts.vcount_test > 0) {
    s_vc_offset = 0x10000u - (unsigned)g_vita_opts.vcount_test - raw;
    vita_log("vcount_test: the display's 16-bit vcount wraps in %d vblanks (D90)",
             g_vita_opts.vcount_test);
  }
  return (int)vpk_vcount_widen(&s_vc, (raw + s_vc_offset) & 0xffffu); /* 16 bits, as on hardware */
}

/* Stage 7c (D112), from any thread: the display against `due`, in our
 * vblanks. The widened vcount less the clock's base is our vblank the
 * display is at (psx_clock_due); the 16 bits the display gives are enough
 * for a difference of a few vblanks, so no widening here (it's the game
 * thread's). */
unsigned vita_display_vcount(void) {
  return ((unsigned)sceDisplayGetVcount() + s_vc_offset) & 0xffffu;
}

int vita_vblank_past_due(int due) {
  int base;
  unsigned raw;
  if (!psx_clock_display_base(&base))
    return VITA_DUE_UNCOUNTED;
  raw = ((unsigned)sceDisplayGetVcount() + s_vc_offset) & 0xffffu;
  /* at 50 Hz (PAL, D138) the display vblank `due` starts at, and ours past it */
  return psx_clock_display_to_ours(
      (int16_t)(uint16_t)(raw - (unsigned)psx_clock_display_due(base, due)));
}

static void wait_display(void) { sceDisplayWaitVblankStart(); }

/* VSync(0) (libetc.c): one vblank with realtime=0; otherwise up to the
 * display's, every one missed after a late tick (psx_clock_due). */
void psx_platform_vsync(void) {
  int n, late;
  if (vita_menu_service())
    psx_clock_resync(); /* the game was paused: start counting again, no catch-up */
  if (!g_vita_opts.realtime) {
    vblank();
    present();
    return;
  }
  prof_enter(PROF_WAIT);
  n = psx_clock_due(1, vcount, wait_display, &late);
  prof_leave();
  s_stat_late += late;
  if (vita_gpu_due_mode()) {
    /* 7c (D112): each vblank takes the frame due at it; the render thread
     * shows it at the display's vblank. Only the PS1 display is posted. */
    while (n-- > 0) {
      vblank();
      vita_gpu_due_vblank(psx_clock_count());
    }
    shown_pictures(vita_gpu_present_due());
    hold();
    return;
  }
  while (n-- > 0)
    vblank();
  present();
}

void PsyX_UpdateInput(void) {}

/* DrawOTag/DrawPrim call this first (linux32/vblank.c has the reasons). */
char PsyX_BeginScene(void) {
  if (psx_gpu_split_count() > 0)
    psx_gpu_draw_splits();
  if (activeDrawEnv.isbg) {
    const RECT16 *c = &activeDrawEnv.clip;
    GR_ClearVRAM(c->x, c->y, c->w, c->h, activeDrawEnv.r0, activeDrawEnv.g0,
                 activeDrawEnv.b0);
    activeDrawEnv.isbg = 0;
  }
  return 0;
}

void PsyX_EndScene(void) {}

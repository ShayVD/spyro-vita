/* Vita time (decisions D28). By default the game's vblanks follow the
 * display's own counter (sceDisplayGetVcount): VSync(0) waits for the next
 * real vblank if the game is early, and if it is late runs every vblank it
 * missed (pad, CD, SPU, the game's VSync callback), as the PS1's interrupt
 * would have, so the game's delta time keeps its speed and the audio stays
 * continuous; the picture is shown once per VSync call. realtime=0 keeps the
 * linux32 host's virtual time (D17: VSync(0) is exactly one vblank), so
 * scripted test runs replay exactly. Also PsyX's scene hooks (isbg clears),
 * the same as linux32's. */
#include "vita.h"
#include "psx_arena.h"
#include "psx_cdimage.h"
#include "psx_hooks.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>

#include "psx/libetc.h"
#include "PsyX/PsyX_render.h"
#include "PsyX/PsyX_public.h"
#include "gpu/PsyX_GPU.h"

extern void (*vsync_callback)(void); /* LIBETC.C */
int host_shot_display(const char *path); /* linux32/png.c */

int g_vmode = MODE_NTSC;
static int s_vblank;
static SceUInt64 s_stat_t0;
static int s_stat_v0, s_stat_shown, s_stat_late;
static int s_vcount_base = -1; /* display vcount at our vblank 0 */
static int s_hold; /* shot_hold: a shot's vblank, held at the next present */

#define MAX_CATCH_UP 8 /* vblanks run at once after a slow frame; more are dropped */

int vita_vblank_count(void) { return s_vblank; }

/* A save state was loaded (D51): its vblank count from now on, counted
 * against the display afresh, and a new stats window. */
void vita_set_vblank(int vblank) {
  s_vblank = s_stat_v0 = vblank;
  s_vcount_base = -1;
  s_stat_shown = s_stat_late = 0;
  s_stat_t0 = 0;
}
int host_vblank_count(void) { return s_vblank; } /* linux32/gpu_soft.c */

int PsyX_Sys_GetVBlankCount(void) { return s_vblank; }

int PsyX_Sys_SetVMode(int mode) {
  int old = g_vmode;
  g_vmode = mode;
  return old;
}

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
  vita_log("cd: %u reads, %u KB, %u ms in the file system since the start", io.reads, io.kbytes,
           (unsigned)(io.us / 1000));
}

static void vblank(void) {
  int v = ++s_vblank, i;
  SceUInt64 now;

  vita_pad_poll();
  vita_pad_vblank();
  prof_enter(PROF_SPU);
  psx_cd_vblank();
  vita_mixer_vblank(); /* psx_spu_vblank on the mixer thread (mixer.c) */
  prof_leave();
  if (vsync_callback)
    vsync_callback();
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
    vita_present_report(v);
    vita_picture_report(v);
    cd_report();
  }
  if (g_vita_opts.frames > 0 && v >= g_vita_opts.frames) {
    vita_audio_close();
    vita_log("frames=%d reached, exiting", g_vita_opts.frames);
    sceKernelExitProcess(0);
  }
}

/* shot_hold: after the shot's picture is shown, hold.req (its vblank) asks
 * for a window capture and the game waits for hold.ack. Files, not the log:
 * Vita3K writes the log to the host in 4 KB blocks. */
static void present(void) {
  int shown = vita_video_present();
  if (shown)
    vita_boot_close(); /* the disc screen's framebuffer isn't shown any more (D60) */
  s_stat_shown += shown;
  vita_bench_present(shown);
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

static int real_vblanks(void) {
  return sceDisplayGetVcount() - s_vcount_base;
}

void PsyX_WaitForTimestep(int count) {
  int target, n;
  if (vita_menu_service())
    s_vcount_base = -1; /* the game was paused: start counting again, no catch-up */
  if (!g_vita_opts.realtime) {
    while (count-- > 0)
      vblank();
    present();
    return;
  }
  if (s_vcount_base < 0)
    s_vcount_base = sceDisplayGetVcount() - s_vblank;
  target = s_vblank + count;
  prof_enter(PROF_WAIT);
  while (real_vblanks() < target)
    sceDisplayWaitVblankStart();
  prof_leave();
  n = real_vblanks() - s_vblank;
  if (n > count)
    s_stat_late += n - count;
  if (n > MAX_CATCH_UP) { /* a load or a stall: don't replay all of it */
    s_vcount_base += n - MAX_CATCH_UP;
    n = MAX_CATCH_UP;
  }
  while (n-- > 0)
    vblank();
  present();
}

void PsyX_UpdateInput(void) {}

/* DrawOTag/DrawPrim call this first (linux32/vblank.c has the reasons). */
char PsyX_BeginScene(void) {
  if (g_splitIndex > 0)
    DrawAllSplits();
  if (activeDrawEnv.isbg) {
    const RECT16 *c = &activeDrawEnv.clip;
    GR_ClearVRAM(c->x, c->y, c->w, c->h, activeDrawEnv.r0, activeDrawEnv.g0,
                 activeDrawEnv.b0);
    activeDrawEnv.isbg = 0;
  }
  return 0;
}

void PsyX_EndScene(void) {}

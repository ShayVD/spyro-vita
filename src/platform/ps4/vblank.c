/* PS4 time (docs/ps4-plan.md P3-P4, D193, D194), the Vita's model (vita/vblank.c,
 * D28, D90): the game's vblanks follow the display's own count
 * (sceVideoOutGetVblankStatus). VSync(0) waits for the next real vblank if
 * the game is early, and if it is late runs every vblank it missed (pad, CD,
 * SPU, the game's callback), so the game keeps its speed and the audio stays
 * continuous; the picture is shown once per VSync call. realtime=0: the
 * linux32 host's virtual time (D17). The clock and its catch-up are shared
 * (common/clock.c, libetc.c, D104). Also PsyX's scene hooks (isbg clears),
 * the same as linux32's. The options menu runs inside VSync (menu.c): the
 * game is paused while it's open, and the clock starts counting afresh
 * after it (no catch-up). */
#include "ps4.h"
#include "host.h"
#include "psx_arena.h"
#include "psx_clock.h"
#include "psx_hooks.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdio.h>
#include <stdlib.h>

#include "psx/libetc.h"
#include "psx_gpu.h"

HostOpts g_host_opts = {.shot_dir = PS4_DATA_DIR "/shots"}; /* linux32's gpu_soft.c, png.c */

int host_vblank_count(void) { return psx_clock_count(); }
int ps4_vblank_count(void) { return psx_clock_count(); }
void host_set_vblank(int vblank) { psx_clock_set(vblank); }

static unsigned long long s_stat_t0;
static int s_stat_v0, s_stat_shown, s_stat_late;

static void time_report(int v);

static void vblank(void) {
  int v = psx_clock_tick();
  unsigned long long now;

  ps4_pad_vblank();
  psx_cd_vblank();
  psx_spu_vblank();
  psx_clock_callback();

  if ((g_ps4_opts.shot_at && v == g_ps4_opts.shot_at) ||
      (g_ps4_opts.shot_every > 0 && v % g_ps4_opts.shot_every == 0)) {
    char path[256];
    snprintf(path, sizeof path, PS4_DATA_DIR "/shots/shot-%d.png", v);
    if (host_shot_display(path) == 0)
      host_log("shot %s", path);
  }
  now = ps4_now_us();
  if (!s_stat_t0)
    s_stat_t0 = now;
  if (g_ps4_opts.stats && v - s_stat_v0 >= 300) {
    unsigned us = (unsigned)(now - s_stat_t0);
    if (!us)
      us = 1;
    host_log("vblank %d: %d vblanks in %u ms (%u.%u per second), %d pictures, %d vblanks late", v,
             v - s_stat_v0, us / 1000, (v - s_stat_v0) * 1000000u / us,
             (v - s_stat_v0) * 10000000u / us % 10, s_stat_shown, s_stat_late);
    time_report(v);
    ps4_video_report(v);
    ps4_audio_report(v);
    ps4_present_report(v);
    ps4_raster_report(v);
    host_log_flush();
    s_stat_t0 = now;
    s_stat_v0 = v;
    s_stat_shown = s_stat_late = 0;
  }
  if (g_ps4_opts.frames > 0 && v >= g_ps4_opts.frames) {
    host_log("frames=%d reached, exiting", g_ps4_opts.frames);
    ps4_exit(0);
  }
}

static int vcount(void) { return (int)ps4_video_vcount(); }

/* where a frame's time goes: the game's own work between VSync calls (its
 * update and draws, the software rasterizer), the vblanks VSync runs, the
 * picture's scaling and flip, the wait for the display */
static unsigned long long s_t_game, s_t_vblanks, s_t_present, s_t_wait, s_t_last;
static unsigned s_t_calls;

void psx_platform_vsync(void) {
  int n, late;
  unsigned long long t0, t1, t2;
  g_ps4_beat++;
  g_ps4_where = "VSync";
  if (ps4_menu_service()) { /* the game was paused: count again, no catch-up */
    psx_clock_resync();
    s_t_last = 0;
  }
  t0 = ps4_now_us();
  if (s_t_last)
    s_t_game += t0 - s_t_last;
  s_t_calls++;
  if (!g_ps4_opts.realtime) {
    n = 1;
    late = 0;
  } else {
    n = psx_clock_due(1, vcount, ps4_video_wait_vblank, &late);
  }
  t1 = ps4_now_us();
  s_t_wait += t1 - t0;
  s_stat_late += late;
  while (n-- > 0)
    vblank();
  t2 = ps4_now_us();
  s_t_vblanks += t2 - t1;
  g_ps4_where = "the present";
  s_stat_shown += ps4_video_present();
  g_ps4_where = "the game";
  s_t_last = ps4_now_us();
  s_t_present += s_t_last - t2;
}

static void time_report(int v) {
  unsigned n = s_t_calls ? s_t_calls : 1;
  host_log("time %d: per VSync (%u calls) game %llu us, vblanks %llu us, present %llu us, "
           "waiting %llu us",
           v, s_t_calls, s_t_game / n, s_t_vblanks / n, s_t_present / n, s_t_wait / n);
  s_t_game = s_t_vblanks = s_t_present = s_t_wait = 0;
  s_t_calls = 0;
}

void PsyX_UpdateInput(void) {}

/* DrawOTag/DrawPrim call this first (linux32/vblank.c has the reasons). */
char PsyX_BeginScene(void) {
  if (psx_gpu_split_count() > 0)
    psx_gpu_draw_splits();
  if (activeDrawEnv.isbg) {
    const RECT16 *c = &activeDrawEnv.clip;
    GR_ClearVRAM(c->x, c->y, c->w, c->h, activeDrawEnv.r0, activeDrawEnv.g0, activeDrawEnv.b0);
    activeDrawEnv.isbg = 0;
  }
  return 0;
}

void PsyX_EndScene(void) {}

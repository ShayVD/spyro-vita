/* The game's time on PC (docs/pc-build.md): the Vita's and the PS4's model
 * (vita/vblank.c, ps4/vblank.c; D28, D90), counted against a 60 Hz clock of
 * real time instead of a display's vcount, since a PC's display may run at
 * any rate (60, 75, 144 Hz, variable). VSync(0) waits for the clock if the
 * game is early and, if it is late, runs every vblank it missed (pad, CD,
 * SPU, the game's callback), so the game keeps its speed and the audio
 * stays continuous (common/clock.c). At each vblank the picture due then is
 * filed for the main thread (frames.c), which shows it at the display's
 * next refresh: the game thread never waits for the display. */
#include "pc.h"
#include "host.h"
#include "../../game/present.h"
#include "psx_arena.h"
#include "psx_clock.h"
#include "psx_hooks.h"

#include <SDL3/SDL.h>

#include <assert.h> /* PsyCross headers use static_assert */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "psx/libetc.h"
#include "psx_gpu.h"

HostOpts g_host_opts; /* linux32's gpu_soft.c, png.c */

int host_vblank_count(void) { return psx_clock_count(); }
void host_set_vblank(int vblank) { psx_clock_set(vblank); }

#define HZ 60
static Uint64 s_t0_ns; /* the clock's vblank 0 */

/* virtual=1 (tests): the clock counts the game's own vblanks, one per
 * vblank it waits for, so a run is never late and sees the same frames at
 * the same vblanks however fast the machine draws (D242: the renderers'
 * comparison under llvmpipe, where gpu=1 is slower than real time). It
 * still waits for real time when early. */
static volatile int s_virt;

/* the clock's vblanks (the "display" psx_clock_due counts against) */
static int vcount(void) {
  Uint64 now = SDL_GetTicksNS();
  if (g_pc_opts.virt) {
    if (!s_t0_ns)
      s_t0_ns = now;
    return s_virt;
  }
  if (!s_t0_ns)
    s_t0_ns = now;
  return (int)((now - s_t0_ns) * HZ / 1000000000ull);
}

/* until the clock's next vblank */
static void wait_vblank(void) {
  if (g_pc_opts.virt) {
    Uint64 due = ((Uint64)s_virt + 1) * 1000000000ull / HZ + s_t0_ns, now = SDL_GetTicksNS();
    if (due > now)
      SDL_DelayPrecise(due - now);
    s_virt++;
    return;
  }
  Uint64 next = ((Uint64)vcount() + 1) * 1000000000ull / HZ + s_t0_ns, now = SDL_GetTicksNS();
  if (next > now)
    SDL_DelayPrecise(next - now);
}

int pc_vblank_now(void) {
  int base;
  if (!s_t0_ns || !psx_clock_display_base(&base))
    return INT_MAX; /* not counted yet: show the newest */
  return vcount() - base;
}

static unsigned long long s_stat_t0;
static int s_stat_v0, s_stat_late;
static unsigned long long s_t_game, s_t_vblanks, s_t_wait, s_t_last;
static unsigned s_t_calls;

static void park(void) {
  host_log("the game thread stops (the window closed)");
  host_log_flush();
  for (;;)
    SDL_Delay(1000);
}

static void shots(int v) {
  for (int i = 0; i < g_pc_opts.nshots; i++)
    if (g_pc_opts.shots[i] == v) {
      char path[1200];
      snprintf(path, sizeof path, "%sshots/shot-%d.png", g_pc_user_dir, v);
      pc_video_shot(path, psx_clock_due_at(v));
    }
}

static void vblank(void) {
  int v = psx_clock_tick(), u;

  if (g_pc_quit)
    park();
  pc_pad_vblank();
  psx_cd_vblank();
  psx_spu_vblank();
  psx_clock_callback();

  /* the picture due at this vblank's display vblanks (one, or at 50 Hz one
   * or two, D148): present.c's (filed already), or the PS1 display now */
  for (u = psx_clock_due_at(v - 1) + 1; u <= psx_clock_due_at(v); u++)
    if (port_present_frame_due(u) < 0)
      pc_frames_post_display(u);
  shots(v);

  {
    unsigned long long now = pc_now_us();
    if (!s_stat_t0)
      s_stat_t0 = now;
    if (g_pc_opts.stats && v - s_stat_v0 >= 300) {
      unsigned us = (unsigned)(now - s_stat_t0), n = s_t_calls ? s_t_calls : 1;
      if (!us)
        us = 1;
      host_log("vblank %d: %d vblanks in %u ms (%u.%u per second), %d late", v, v - s_stat_v0,
               us / 1000, (v - s_stat_v0) * 1000000u / us, (v - s_stat_v0) * 10000000u / us % 10,
               s_stat_late);
      host_log("time %d: per VSync (%u calls) game %llu us, vblanks %llu us, waiting %llu us", v,
               s_t_calls, s_t_game / n, s_t_vblanks / n, s_t_wait / n);
      pc_frames_report(v);
      pc_video_report(v);
      if (g_pc_gpu)
        pc_gpu_report(v);
      pc_audio_report(v);
      host_log_flush();
      s_t_game = s_t_vblanks = s_t_wait = 0;
      s_t_calls = 0;
      s_stat_t0 = now;
      s_stat_v0 = v;
      s_stat_late = 0;
    }
  }
  if (g_pc_opts.frames > 0 && v >= g_pc_opts.frames) {
    host_log("frames=%d reached, exiting", g_pc_opts.frames);
    SDL_Delay(100); /* the last shot's picture */
    host_log_flush();
    g_pc_quit = 1;
    SDL_Event q;
    SDL_zero(q);
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
    park();
  }
}

void psx_platform_vsync(void) {
  int n, late;
  unsigned long long t0 = pc_now_us(), t1;
  if (s_t_last)
    s_t_game += t0 - s_t_last;
  s_t_calls++;
  n = psx_clock_due(1, vcount, wait_vblank, &late);
  t1 = pc_now_us();
  s_t_wait += t1 - t0;
  s_stat_late += late;
  while (n-- > 0)
    vblank();
  s_t_last = pc_now_us();
  s_t_vblanks += s_t_last - t1;
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

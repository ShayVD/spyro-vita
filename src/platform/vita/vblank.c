/* Vita time: VSync(0) is one vblank, as on the linux32 host (decisions D17),
 * but paced by the Vita's own 60 Hz vblank: each one runs the pad, CD, SPU
 * and the game's VSync callback, then shows the picture (SDL_RenderPresent
 * waits for the display's vblank). A game frame that takes longer than its
 * vblanks slows the game down rather than skipping time. Also PsyX's scene
 * hooks (isbg clears), the same as linux32's. */
#include "vita.h"
#include "psx_arena.h"
#include "psx_hooks.h"

#include <assert.h> /* PsyCross headers use static_assert */
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
static int s_stat_v0;

int vita_vblank_count(void) { return s_vblank; }
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

static void vblank(void) {
  int v = ++s_vblank;
  SceUInt64 now;

  vita_pad_poll();
  vita_pad_vblank();
  psx_cd_vblank();
  psx_spu_vblank();
  if (vsync_callback)
    vsync_callback();
  vita_video_present();

  if (shot_due(v)) {
    char path[256];
    snprintf(path, sizeof path, VITA_DATA_DIR "/shots/shot-%d.png", v);
    if (host_shot_display(path) == 0)
      vita_log("shot %s", path);
  }
  now = sceKernelGetProcessTimeWide();
  if (!s_stat_t0)
    s_stat_t0 = now;
  if (g_vita_opts.stats && v - s_stat_v0 >= 300) {
    unsigned us = (unsigned)(now - s_stat_t0);
    vita_log("vblank %d: %d vblanks in %u ms (%u.%u per second)", v, v - s_stat_v0,
             us / 1000, (v - s_stat_v0) * 1000000u / us,
             (v - s_stat_v0) * 10000000u / us % 10);
    s_stat_t0 = now;
    s_stat_v0 = v;
  }
  if (g_vita_opts.frames > 0 && v >= g_vita_opts.frames) {
    vita_log("frames=%d reached, exiting", g_vita_opts.frames);
    sceKernelExitProcess(0);
  }
}

void PsyX_WaitForTimestep(int count) {
  while (count-- > 0)
    vblank();
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

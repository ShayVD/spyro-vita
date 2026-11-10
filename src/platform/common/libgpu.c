/* PsyQ's libgpu for the port (stage 8b, D115), in place of PsyCross's
 * LIBGPU.C (cmake/psycross.cmake leaves it out, as it does LIBETC.C).
 *
 * The entry points the game calls, with LIBGPU.C's semantics as our patches
 * left them: the environment calls (PutDrawEnv, PutDispEnv, SetDefDrawEnv,
 * SetDefDispEnv, SetDispMask), the VRAM calls (LoadImage, StoreImage,
 * MoveImage, ClearImage), DrawSync, DrawOTag, ResetGraph, SetGraphDebug and
 * SetDrawMode. Ported from PsyCross (MIT, THIRD_PARTY_NOTICES.md).
 *
 * What were the patches' hooks are direct calls now:
 *  - the gate (0009 g_psyxGpuGate): psx_walk_join (walk.c), first in every
 *    call that reads or writes the GPU state a walk uses, so a walk still
 *    running elsewhere finishes first; a no-op with no walk handed over;
 *  - the VRAM freeze (0008 g_psyxVramFrozen, later our g_psx_vram_frozen):
 *    gone with D30's pass (stage 7g, D129), the one caller; the presenter
 *    never makes these calls (src/game/pres/pres_names.h);
 *  - DrawOTag's pre and post (0005): present.c's port_present_ot_pre and
 *    port_present_ot_post; its walk (0009 g_psyxDrawOTagWalk):
 *    psx_walk_hand_over; its done (0007 g_psyxDrawOTagDone): the vertex
 *    cache's psx_vcache_ot_done (vcache.c).
 *
 * The walk itself, its splits and vertex buffer and the GPU state
 * (activeDrawEnv, activeDispEnv, currentDispEnv, g_GPUDisabledState) are
 * gpu_walk.c's (stage 8d, D117; PsyCross's PsyX_GPU.cpp before). */
#include "psx_gpu.h"
#include "psx_libgpu.h"
#include "psx_vcache.h"
#include "psx_walk.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "psx/libetc.h"

void (*drawsync_callback)(void) = NULL;

int ClearImage(RECT16 *rect, u_char r, u_char g, u_char b) {
  psx_walk_join();
  GR_ClearVRAM(rect->x, rect->y, rect->w, rect->h, r, g, b);
  GR_Clear(rect->x, rect->y, rect->w, rect->h, r, g, b);
  return 0;
}

int LoadImage(RECT16 *rect, u_long *p) {
  psx_walk_join();
  GR_CopyVRAM((unsigned short *)p, 0, 0, rect->w, rect->h, rect->x, rect->y);
  return 0;
}

int StoreImage(RECT16 *rect, u_long *p) {
  psx_walk_join();
  GR_ReadVRAM((unsigned short *)p, rect->x, rect->y, rect->w, rect->h);
  return 0;
}

int MoveImage(RECT16 *rect, int x, int y) {
  psx_walk_join();
  GR_CopyVRAM(NULL, rect->x, rect->y, rect->w, rect->h, x, y);
  return 0;
}

int DrawSync(int mode) {
  (void)mode;
  psx_walk_join();
  GR_UpdateVRAM();
  GR_ReadFramebufferDataToVRAM();
  if (psx_gpu_split_count() > 0)
    psx_gpu_draw_splits();
  if (drawsync_callback)
    drawsync_callback();
  return 0;
}

u_int DrawSyncCallback(void (*func)(void)) {
  drawsync_callback = func;
  return 0;
}

int ResetGraph(int mode) {
  psx_walk_join();
  if (mode == 0) { /* reset the GPU state */
    g_GPUDisabledState = 0;
    ClearImage(&activeDrawEnv.clip, 0, 0, 0);
    psx_gpu_clear_splits();
    PsyX_EndScene();
  } else if (mode == 1) { /* cancel all drawing */
    psx_gpu_clear_splits();
    PsyX_EndScene();
  }
  return 0;
}

int SetGraphDebug(int level) {
  (void)level;
  return 0;
}

void SetDispMask(int mask) {
  psx_walk_join();
  g_GPUDisabledState = (mask == 0);
}

DISPENV *PutDispEnv(DISPENV *env) {
  psx_walk_join();
  /* LIBGPU.C: the environment shown is the one put before this, unless the
   * interlace changes or the display stays at the same y */
  if (env->isinter != currentDispEnv.isinter || env->disp.y == currentDispEnv.disp.y) {
    activeDispEnv = currentDispEnv = *env;
  } else {
    activeDispEnv = currentDispEnv;
    currentDispEnv = *env;
  }
  return 0;
}

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h) {
  env->disp.x = x;
  env->disp.y = y;
  env->disp.w = w;
  env->disp.h = h;
  env->screen.x = 0;
  env->screen.y = 0;
  env->screen.w = 0;
  env->screen.h = 0;
  env->isrgb24 = 0;
  env->isinter = 0;
  env->pad1 = 0;
  env->pad0 = 0;
  return 0;
}

DRAWENV *PutDrawEnv(DRAWENV *env) {
  psx_walk_join();
  memcpy(&activeDrawEnv, env, sizeof(DRAWENV));
  return 0;
}

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h) {
  env->clip.x = x;
  env->clip.y = y;
  env->clip.w = w;
  env->clip.h = h;
  env->tw.x = 0;
  env->tw.y = 0;
  env->tw.w = 0;
  env->tw.h = 0;
  env->r0 = 0;
  env->g0 = 0;
  env->b0 = 0;
  env->dtd = 1;
  if (GetVideoMode() == MODE_NTSC)
    env->dfe = h < 289 ? 1 : 0;
  else
    env->dfe = h < 257 ? 1 : 0;
  env->ofs[0] = x;
  env->ofs[1] = y;
  env->tpage = 10;
  env->isbg = 0;
  return env;
}

void SetDrawMode(DR_MODE *p, int dfe, int dtd, int tpage, RECT16 *tw) {
  setDrawMode(p, dfe, dtd, tpage, tw);
}

void DrawOTag(u_long *p) {
  psx_walk_join();
  if (port_present_ot_pre(p)) {
    psx_vcache_ot_done();
    return;
  }
  if (g_GPUDisabledState) {
    psx_gpu_clear_splits();
    return; /* LIBGPU.C: no post or done call here */
  }
  if (!psx_walk_hand_over(p)) { /* the scene begins here unless the walk took the list */
    psx_gpu_walk_scene(p, 0, NULL);
    psx_gpu_draw_splits();
  }
  port_present_ot_post(p);
  psx_vcache_ot_done();
}

/* No caller in the game; the Vita's prof.c wraps it (--wrap=DrawPrim). */
void DrawPrim(void *p) {
  psx_walk_join();
  if (g_GPUDisabledState) {
    psx_gpu_clear_splits();
    return;
  }
  psx_gpu_walk_scene((u_long *)p, 1, NULL);
}

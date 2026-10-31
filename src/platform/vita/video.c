/* Vita picture with gpu=0 (decisions D26, D35): the PS1 VRAM is drawn by the same software
 * rasterizer as the linux32 host (src/platform/linux32/gpu_soft.c, behind
 * PsyCross's GR_* layer); each vblank the displayed VRAM rect (15-bit, or
 * 24-bit for the boot logos; png.c's host_display_rgb) goes to an SDL2
 * streaming texture and SDL2's own GXM renderer scales it 4:3 into the
 * 960x544 screen. SDL2's renderer uses precompiled shaders, so nothing needs
 * libshacccg.suprx. With gpu=1 the GPU renderer (gpu_gl.c) owns the screen
 * through vitaGL instead, and no SDL window exists. */
#include "vita.h"

#include <SDL2/SDL.h>
#include <assert.h> /* PsyCross headers use static_assert */
#include <stdlib.h>
#include <string.h>

#include "gpu/PsyX_GPU.h" /* currentDispEnv, g_GPUDisabledState */
#include "../../game/present.h"

uint8_t *host_display_rgb(int *pw, int *ph); /* linux32/png.c */

#define SCREEN_W 960
#define SCREEN_H 544
#define TEX_W 1024
#define TEX_H 512

static SDL_Window *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture *s_tex;

int vita_video_init(void) {
  if (g_vita_gpu) {
    int r = vita_gpu_init();
    if (r < 0)
      return -1;
    g_vita_gl = 1;
    if (r == 1)
      g_vita_gpu = 0; /* the software rasterizer, presented by gpu_gl.c */
    return 0;
  }
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "VITA gxm");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, g_vita_opts.filter ? "1" : "0");
  s_win = SDL_CreateWindow("Spyro Vita", 0, 0, SCREEN_W, SCREEN_H, 0);
  if (!s_win) {
    vita_log("video: SDL_CreateWindow failed: %s", SDL_GetError());
    return -1;
  }
  s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!s_ren) {
    vita_log("video: SDL_CreateRenderer failed: %s", SDL_GetError());
    return -1;
  }
  s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, TEX_W, TEX_H);
  if (!s_tex) {
    vita_log("video: SDL_CreateTexture failed: %s", SDL_GetError());
    return -1;
  }
  {
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(s_ren, &info) == 0)
      vita_log("video: renderer %s", info.name);
  }
  SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
  SDL_RenderClear(s_ren);
  SDL_RenderPresent(s_ren);
  return 0;
}

/* What the last upload showed: nothing to convert again while the
 * rasterizer hasn't written VRAM and the display is the same (the game draws
 * at 30 Hz, the screen runs at 60). */
static struct {
  unsigned gen;
  DISPENV disp;
  int disabled, w, h, valid;
} s_last;

/* The options panel (menu.c), drawn 1:1 in the middle over the picture. */
static const uint8_t *s_ovl;
static int s_ovl_w, s_ovl_h, s_ovl_dirty, s_ovl_tw, s_ovl_th;
static SDL_Texture *s_ovl_tex;

void vita_video_overlay(const uint8_t *rgb, int w, int h) {
  if (g_vita_gl) {
    vita_gpu_overlay(rgb, w, h);
    return;
  }
  s_ovl = rgb;
  s_ovl_w = w;
  s_ovl_h = h;
  s_ovl_dirty = 1;
}

void vita_video_force_redraw(void) {
  if (g_vita_gl)
    vita_gpu_force_redraw(); /* SDL's path draws every time anyway */
}

static void draw_overlay(void) {
  SDL_Rect dst;
  if (!s_ovl)
    return;
  if (!s_ovl_tex || s_ovl_tw != s_ovl_w || s_ovl_th != s_ovl_h) {
    if (s_ovl_tex)
      SDL_DestroyTexture(s_ovl_tex);
    s_ovl_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                  s_ovl_w, s_ovl_h);
    s_ovl_tw = s_ovl_w;
    s_ovl_th = s_ovl_h;
    s_ovl_dirty = 1;
  }
  if (!s_ovl_tex)
    return;
  if (s_ovl_dirty) {
    SDL_UpdateTexture(s_ovl_tex, NULL, s_ovl, s_ovl_w * 3);
    s_ovl_dirty = 0;
  }
  dst.x = (SCREEN_W - s_ovl_w) / 2;
  dst.y = (SCREEN_H - s_ovl_h) / 2;
  dst.w = s_ovl_w;
  dst.h = s_ovl_h;
  SDL_RenderCopy(s_ren, s_ovl_tex, NULL, &dst);
}

/* A presentation frame (src/game/present.c: widescreen, 60 fps): 512x240
 * PS1 15-bit, full screen when widescreen, else 4:3 in the middle. */
static int s_tex_slot = -1; /* the presentation slot in s_tex, -1: none */

static int present_frame(int slot) {
  const uint16_t *px = port_present_slot_pixels(slot);
  int fresh = port_present_frame_new() || s_tex_slot != slot, x, y, pitch;
  void *pixels;
  SDL_Rect src = {0, 0, PRESENT_W, PRESENT_H}, dst;
  prof_enter(PROF_PRESENT);
  if (fresh && px && SDL_LockTexture(s_tex, &src, &pixels, &pitch) == 0) {
    for (y = 0; y < PRESENT_H; y++) {
      const uint16_t *s = px + y * PRESENT_W;
      uint8_t *d = (uint8_t *)pixels + (size_t)y * pitch;
      for (x = 0; x < PRESENT_W; x++, d += 4) {
        int r = s[x] & 31, g = (s[x] >> 5) & 31, b = (s[x] >> 10) & 31;
        d[0] = (uint8_t)(r << 3 | r >> 2);
        d[1] = (uint8_t)(g << 3 | g >> 2);
        d[2] = (uint8_t)(b << 3 | b >> 2);
        d[3] = 255;
      }
    }
    SDL_UnlockTexture(s_tex);
    s_tex_slot = slot;
  }
  s_last.valid = 0; /* the texture no longer holds the display rect */
  dst.y = 0;
  dst.h = SCREEN_H;
  dst.w = port_present_frame_is_wide() ? SCREEN_W : SCREEN_H * 4 / 3;
  dst.x = (SCREEN_W - dst.w) / 2;
  SDL_RenderClear(s_ren);
  SDL_RenderCopy(s_ren, s_tex, &src, &dst);
  draw_overlay();
  prof_leave();
  prof_enter(PROF_WAIT);
  SDL_RenderPresent(s_ren);
  prof_leave();
  return fresh;
}

/* One frame: the display rect as the TV would show it, 4:3 in the middle.
 * Returns 1 if it was a new picture. */
int vita_video_present(void) {
  int w, h, x, y, pitch;
  void *pixels;
  uint8_t *rgb;
  SDL_Rect src, dst;
  int fresh = 1;
  if (g_vita_gl)
    return vita_gpu_present();
  if (!s_ren)
    return 0;
  {
    int slot = port_present_frame_slot();
    if (slot >= 0)
      return present_frame(slot);
  }
  prof_enter(PROF_PRESENT);
  if (s_last.valid && s_last.gen == prof_vram_gen() && s_last.disabled == g_GPUDisabledState &&
      !memcmp(&s_last.disp, &currentDispEnv, sizeof s_last.disp)) {
    w = s_last.w;
    h = s_last.h;
    fresh = 0;
    goto show;
  }
  s_last.valid = 1;
  s_tex_slot = -1;
  s_last.gen = prof_vram_gen();
  s_last.disabled = g_GPUDisabledState;
  s_last.disp = currentDispEnv;
  rgb = host_display_rgb(&w, &h);
  if (w > TEX_W)
    w = TEX_W;
  if (h > TEX_H)
    h = TEX_H;
  src.x = src.y = 0;
  src.w = w;
  src.h = h;
  if (SDL_LockTexture(s_tex, &src, &pixels, &pitch) == 0) {
    for (y = 0; y < h; y++) {
      const uint8_t *s = rgb + (size_t)y * w * 3;
      uint8_t *d = (uint8_t *)pixels + (size_t)y * pitch;
      for (x = 0; x < w; x++, s += 3, d += 4) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = 255;
      }
    }
    SDL_UnlockTexture(s_tex);
  }
  free(rgb);
  s_last.w = w;
  s_last.h = h;
show:
  src.x = src.y = 0;
  src.w = w;
  src.h = h;
  dst.h = SCREEN_H;
  dst.w = SCREEN_H * 4 / 3;
  dst.x = (SCREEN_W - dst.w) / 2;
  dst.y = 0;
  SDL_RenderClear(s_ren);
  SDL_RenderCopy(s_ren, s_tex, &src, &dst);
  draw_overlay();
  prof_leave();
  prof_enter(PROF_WAIT);
  SDL_RenderPresent(s_ren); /* flips at the Vita's vblank (PRESENTVSYNC) */
  prof_leave();
  return fresh;
}

/* Vita picture (decisions D25): the PS1 VRAM is drawn by the same software
 * rasterizer as the linux32 host (src/platform/linux32/gpu_soft.c, behind
 * PsyCross's GR_* layer); each vblank the displayed VRAM rect (15-bit, or
 * 24-bit for the boot logos; png.c's host_display_rgb) goes to an SDL2
 * streaming texture and SDL2's own GXM renderer scales it 4:3 into the
 * 960x544 screen. SDL2's renderer uses precompiled shaders, so nothing needs
 * libshacccg.suprx. */
#include "vita.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

uint8_t *host_display_rgb(int *pw, int *ph); /* linux32/png.c */

#define SCREEN_W 960
#define SCREEN_H 544
#define TEX_W 1024
#define TEX_H 512

static SDL_Window *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture *s_tex;

int vita_video_init(void) {
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

/* One frame: the display rect as the TV would show it, 4:3 in the middle. */
void vita_video_present(void) {
  int w, h, x, y, pitch;
  void *pixels;
  uint8_t *rgb;
  SDL_Rect src, dst;
  if (!s_ren)
    return;
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
  dst.h = SCREEN_H;
  dst.w = SCREEN_H * 4 / 3;
  dst.x = (SCREEN_W - dst.w) / 2;
  dst.y = 0;
  SDL_RenderClear(s_ren);
  SDL_RenderCopy(s_ren, s_tex, &src, &dst);
  SDL_RenderPresent(s_ren); /* waits for the Vita's vblank (PRESENTVSYNC) */
}

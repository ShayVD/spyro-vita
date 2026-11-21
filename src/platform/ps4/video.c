/* The PS4's display through sceVideoOut (docs/ps4-plan.md P3, D193): the
 * software rasterizer's VRAM (linux32's gpu_soft.c) scaled into a 1920x1080
 * linear A8R8G8B8 framebuffer on the CPU, as OpenOrbis's own graphics sample
 * draws (samples/_common/graphics.cpp: direct memory, two buffers, a flip
 * event queue). No GPU, no Piglet, no shader: it runs on every PS4 without
 * Sony modules. The GLES2 renderer replaces it when the player has given us
 * Piglet's modules (D193).
 *
 * The PS1 display rect fills a 4:3 1440x1080 box in the middle (the 224 lines
 * the game draws of an NTSC 240, as png.c's 960x544 shots), black bars at the
 * sides. Nearest-neighbour: each source row is built once in cached memory
 * and written to every framebuffer row it covers. The framebuffer is
 * write-combined: never read it (hardware run 2: copying repeated rows
 * from the row above cost most of a frame). */
#include "ps4.h"
#include "host.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>

#include "psx/libgte.h"
#include "psx/libgpu.h"

#define FB_W 1920
#define FB_H 1080
#define BOX_W 1440 /* 4:3 at 1080 lines */
#define BOX_X ((FB_W - BOX_W) / 2)
#define NBUF 2
#define FB_BYTES ((size_t)FB_W * FB_H * 4)
#define DMEM_ALIGN 0x200000u

extern DISPENV currentDispEnv; /* libgpu.c: what the PS1 scans out */
extern int g_GPUDisabledState;

/* OpenOrbis's VideoOut.h declares these without arguments: our own
 * prototypes under other names (the status layout shadPS4 and fpPS4 use,
 * with room to spare at the end) */
typedef struct {
  uint64_t count;
  uint64_t process_time;
  uint64_t tsc;
  uint64_t reserved[1];
  uint8_t flags;
  uint8_t pad[7];
  uint64_t spare[4];
} Ps4VblankStatus;
int32_t ps4_vo_vblank_status(int32_t handle, Ps4VblankStatus *status)
    __asm__("sceVideoOutGetVblankStatus");
int32_t ps4_vo_wait_vblank(int32_t handle) __asm__("sceVideoOutWaitVblank");

static int s_handle = -1;
static OrbisKernelEqueue s_flip_q;
static uint32_t *s_fb[NBUF];
static int s_cur; /* the buffer drawn next */
static int s_xmap[BOX_W], s_xmap_w = -1;
static uint32_t s_row[BOX_W]; /* one row of the box, in cached memory */
static unsigned long long s_draw_us, s_wait_us;
static unsigned s_shown, s_flip_err;

int ps4_video_handle(void) { return s_handle; }

int ps4_video_init(void) {
  OrbisVideoOutBufferAttribute attr;
  off_t phys = 0;
  void *mem = NULL;
  size_t size = (FB_BYTES * NBUF + DMEM_ALIGN - 1) & ~(size_t)(DMEM_ALIGN - 1);
  int rc, i;
  s_handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
  if (s_handle < 0) {
    host_log("video: sceVideoOutOpen failed: 0x%x", (unsigned)s_handle);
    return -1;
  }
  if ((rc = sceKernelCreateEqueue(&s_flip_q, "spyro flip")) < 0 ||
      (rc = sceVideoOutAddFlipEvent(s_flip_q, s_handle, NULL)) < 0) {
    host_log("video: flip event queue: 0x%x", (unsigned)rc);
    return -1;
  }
  /* write-combined memory the display reads (protection 0x33: CPU and GPU read/write) */
  if ((rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, DMEM_ALIGN, 3,
                                          &phys)) < 0 ||
      (rc = sceKernelMapDirectMemory(&mem, size, 0x33, 0, phys, DMEM_ALIGN)) < 0) {
    host_log("video: %zu bytes of direct memory: 0x%x", size, (unsigned)rc);
    return -1;
  }
  for (i = 0; i < NBUF; i++) {
    s_fb[i] = (uint32_t *)((char *)mem + FB_BYTES * i);
    memset(s_fb[i], 0, FB_BYTES); /* opaque black, alpha 0 is shown as black too */
  }
  /* 0x80000000: A8R8G8B8 sRGB; tiling 1: linear; aspect 0: 16:9 */
  sceVideoOutSetBufferAttribute(&attr, 0x80000000u, 1, 0, FB_W, FB_H, FB_W);
  if ((rc = sceVideoOutRegisterBuffers(s_handle, 0, (void *const *)s_fb, NBUF, &attr)) != 0) {
    host_log("video: sceVideoOutRegisterBuffers failed: 0x%x", (unsigned)rc);
    return -1;
  }
  sceVideoOutSetFlipRate(s_handle, 0); /* 60 Hz */
  host_log("video: sceVideoOut %dx%d, %d buffers at %p (software renderer)", FB_W, FB_H, NBUF, mem);
  return 0;
}

uint64_t ps4_video_vcount(void) {
  Ps4VblankStatus st;
  memset(&st, 0, sizeof st);
  if (s_handle < 0 || ps4_vo_vblank_status(s_handle, &st) < 0)
    return ps4_now_us() * 60 / 1000000; /* no display (yet): a 60 Hz clock */
  return st.count;
}

void ps4_video_wait_vblank(void) {
  if (s_handle < 0 || ps4_vo_wait_vblank(s_handle) < 0)
    sceKernelUsleep(1000);
}

static inline uint32_t argb555(uint16_t p) {
  uint32_t r = p & 31, g = (p >> 5) & 31, b = (p >> 10) & 31;
  r = r << 3 | r >> 2;
  g = g << 3 | g >> 2;
  b = b << 3 | b >> 2;
  return 0xFF000000u | r << 16 | g << 8 | b;
}

/* the PS1 display rect into buffer fb's box */
static void draw(uint32_t *fb) {
  const DISPENV *d = &currentDispEnv;
  const uint16_t *vram = host_vram();
  int w = d->disp.w > 0 ? d->disp.w : 320, h = d->disp.h > 0 ? d->disp.h : 240;
  int top = h == 240 ? 8 : 0, lines = h - 2 * top, y, x, prev = -1;
  if (d->disp.w <= 0 || g_GPUDisabledState) {
    for (y = 0; y < FB_H; y++)
      memset(fb + (size_t)y * FB_W + BOX_X, 0, BOX_W * 4);
    return;
  }
  if (s_xmap_w != w * 2 + d->isrgb24) {
    for (x = 0; x < BOX_W; x++)
      s_xmap[x] = x * w / BOX_W;
    s_xmap_w = w * 2 + d->isrgb24;
  }
  for (y = 0; y < FB_H; y++) {
    int sy = top + y * lines / FB_H;
    uint32_t *o = s_row;
    if (sy == prev) {
      memcpy(fb + (size_t)y * FB_W + BOX_X, s_row, BOX_W * 4);
      continue;
    }
    prev = sy;
    {
      int vy = (d->disp.y + sy) & 511;
      const uint16_t *row = vram + (size_t)vy * 1024;
      if (d->isrgb24) { /* the movies' 24-bit mode: 3 bytes a pixel */
        const uint8_t *b = (const uint8_t *)row;
        for (x = 0; x < BOX_W; x++) {
          int off = d->disp.x * 2 + s_xmap[x] * 3;
          o[x] = 0xFF000000u | (uint32_t)b[off & 2047] << 16 | (uint32_t)b[(off + 1) & 2047] << 8 |
                 b[(off + 2) & 2047];
        }
      } else {
        for (x = 0; x < BOX_W; x++)
          o[x] = argb555(row[(d->disp.x + s_xmap[x]) & 1023]);
      }
    }
    memcpy(fb + (size_t)y * FB_W + BOX_X, s_row, BOX_W * 4);
  }
}

/* the buffer we draw next must not be on screen or waiting to be */
static void wait_flip_done(void) {
  int n;
  OrbisKernelEvent ev;
  OrbisKernelUseconds timeout = 100000;
  while (sceVideoOutIsFlipPending(s_handle) > 0)
    if (sceKernelWaitEqueue(s_flip_q, &ev, 1, &n, &timeout) < 0)
      break;
}

int ps4_video_present(void) {
  int rc;
  if (s_handle < 0)
    return 0;
  {
    unsigned long long t0 = ps4_now_us(), t1;
    wait_flip_done();
    t1 = ps4_now_us();
    draw(s_fb[s_cur]);
    s_wait_us += t1 - t0;
    s_draw_us += ps4_now_us() - t1;
  }
  rc = sceVideoOutSubmitFlip(s_handle, s_cur, ORBIS_VIDEO_OUT_FLIP_VSYNC, (int64_t)s_shown);
  if (rc < 0) {
    s_flip_err++;
    return 0;
  }
  s_cur = (s_cur + 1) % NBUF;
  s_shown++;
  return 1;
}

void ps4_video_report(int vblank) {
  static unsigned last;
  unsigned n = s_shown - last ? s_shown - last : 1;
  host_log("video %d: %u pictures flipped, %u flip errors, display vcount %llu; per picture %llu us "
           "scaling, %llu us waiting for the flip", vblank, s_shown, s_flip_err,
           (unsigned long long)ps4_video_vcount(), s_draw_us / n, s_wait_us / n);
  last = s_shown;
  s_draw_us = s_wait_us = 0;
}

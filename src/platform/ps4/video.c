/* The PS4's display through sceVideoOut (docs/ps4-plan.md P3-P4, D193,
 * D199): the software path's picture scaled into a 1920x1080 linear
 * A8R8G8B8 framebuffer on the CPU, as OpenOrbis's own graphics sample draws
 * (samples/_common/graphics.cpp: direct memory, two buffers, a flip event
 * queue). No GPU, no Piglet, no shader: it runs on every PS4 without Sony
 * modules. With the GLES2 renderer (gpu_gles.c, the player's Piglet) Piglet
 * owns the display and this file only gives the vblank count.
 *
 * The picture is a presenter frame when there is one (present_ps4.c:
 * widescreen, 60 fps; 512 x 240 PS1 15-bit, the Vita's gpu=0 path,
 * vita/video.c), else the PS1 display rect. Its drawn lines (224 of NTSC's
 * 240, all of PAL's 256) fill the height: 16:9 frames the whole width, 4:3
 * ones a box in the middle (240 lines: 1080 * 4/3 * 240/224 = 1543, as the
 * Vita's 778 of 960; PAL's 256: 1440), black bars at the sides.
 * Nearest-neighbour: each source row is built once in cached memory and
 * written to every framebuffer row it covers. The framebuffer is
 * write-combined: never read it (hardware run 2: copying repeated rows
 * from the row above cost most of a frame).
 *
 * The overlay (the options menu, a save state's notice) is an RGB panel
 * drawn at twice its size in the middle, over the picture. */
#include "ps4.h"
#include "host.h"
#include "../../game/present.h"

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
#define NBUF 2
#define FB_BYTES ((size_t)FB_W * FB_H * 4)
#define DMEM_ALIGN 0x200000u
#define OVL_SCALE 2
#define OVL_MAX_W (FB_W / OVL_SCALE)
#define OVL_MAX_H (FB_H / OVL_SCALE)

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
static int s_fb_box[NBUF][2]; /* the box each buffer's bars were cleared for: x, w */
static int s_cur;             /* the buffer drawn next */
static int s_xmap[FB_W], s_xmap_key = -1;
static uint32_t s_row[FB_W]; /* one row of the box, in cached memory */
static unsigned long long s_draw_us, s_wait_us;
static unsigned s_shown, s_flip_err, s_present_frames;

/* the overlay: its RGB (OVL_MAX_W x OVL_MAX_H at most), 0 x 0 when none */
static uint8_t s_ovl[OVL_MAX_W * OVL_MAX_H * 3];
static int s_ovl_w, s_ovl_h;

int ps4_video_handle(void) { return s_handle; }

/* The display, for the vblank count and the flips (the software path only:
 * with the GPU's, Piglet holds it). */
static int open_display(void) {
  s_handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
  if (s_handle < 0) {
    host_log("video: sceVideoOutOpen failed: 0x%x", (unsigned)s_handle);
    return -1;
  }
  return 0;
}

int ps4_video_init(void) {
  OrbisVideoOutBufferAttribute attr;
  off_t phys = 0;
  void *mem = NULL;
  size_t size = (FB_BYTES * NBUF + DMEM_ALIGN - 1) & ~(size_t)(DMEM_ALIGN - 1);
  int rc, i;
  if (g_ps4_gpu) {
    /* Piglet shows the pictures and opens the display itself, at its first
     * eglSwapBuffers: a handle of ours on the main bus (opened after the
     * surface, which doesn't hold one yet) left that swap waiting for good
     * (GPU runs 3 and 4). So none here: ps4_video_vcount is a 60 Hz clock
     * and the swap's VSync keeps the pictures on the display's vblanks. */
    host_log("video: the vblanks follow a 60 Hz clock (Piglet holds the display)");
    return 0;
  }
  if (open_display() != 0)
    return -1;
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
    s_fb_box[i][0] = 0, s_fb_box[i][1] = FB_W;
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
  g_ps4_where = "waiting for a vblank";
  if (s_handle < 0 || ps4_vo_wait_vblank(s_handle) < 0) {
    /* no display handle (the GPU path: Piglet holds it): until the 60 Hz
     * clock's next tick, which ps4_video_vcount counts (1 ms polls had the
     * open menu presenting a thousand pictures a second, GPU run 7) */
    unsigned long long now = ps4_now_us(), next = (now * 60 / 1000000 + 1) * 1000000 / 60 + 1;
    sceKernelUsleep((unsigned)(next - now));
  }
}

void ps4_video_overlay(const uint8_t *rgb, int w, int h) {
  if (g_ps4_gpu) {
    ps4_gpu_overlay(rgb, w, h);
    return;
  }
  if (!rgb || w <= 0 || h <= 0 || w > OVL_MAX_W || h > OVL_MAX_H) {
    s_ovl_w = s_ovl_h = 0;
    return;
  }
  memcpy(s_ovl, rgb, (size_t)w * h * 3);
  s_ovl_w = w;
  s_ovl_h = h;
}

static inline uint32_t argb555(uint16_t p) {
  uint32_t r = p & 31, g = (p >> 5) & 31, b = (p >> 10) & 31;
  r = r << 3 | r >> 2;
  g = g << 3 | g >> 2;
  b = b << 3 | b >> 2;
  return 0xFF000000u | r << 16 | g << 8 | b;
}

/* What a picture's rows come from: the PS1 display rect, or a presenter
 * frame. row() builds source row sy (of the drawn lines) box_w wide. */
typedef struct {
  const uint16_t *px; /* a presenter frame: PRESENT_W wide; NULL: the display */
  int src_w, top, lines, box_w;
  const DISPENV *d;
} Pic;

static void xmap(int box_w, int src_w) {
  int key = box_w * 2048 + src_w, x;
  if (key == s_xmap_key)
    return;
  for (x = 0; x < box_w; x++)
    s_xmap[x] = x * src_w / box_w;
  s_xmap_key = key;
}

static void build_row(const Pic *p, int sy, uint32_t *o) {
  int x;
  if (p->px) {
    const uint16_t *row = p->px + (size_t)(p->top + sy) * PRESENT_W;
    for (x = 0; x < p->box_w; x++)
      o[x] = argb555(row[s_xmap[x]]);
    return;
  }
  {
    const DISPENV *d = p->d;
    const uint16_t *vram = host_vram();
    int vy = (d->disp.y + p->top + sy) & 511;
    const uint16_t *row = vram + (size_t)vy * 1024;
    if (d->isrgb24) { /* the movies' 24-bit mode: 3 bytes a pixel */
      const uint8_t *b = (const uint8_t *)row;
      for (x = 0; x < p->box_w; x++) {
        int off = d->disp.x * 2 + s_xmap[x] * 3;
        o[x] = 0xFF000000u | (uint32_t)b[off & 2047] << 16 | (uint32_t)b[(off + 1) & 2047] << 8 |
               b[(off + 2) & 2047];
      }
    } else {
      for (x = 0; x < p->box_w; x++)
        o[x] = argb555(row[(d->disp.x + s_xmap[x]) & 1023]);
    }
  }
}

/* the 4:3 box for a picture of `lines` drawn lines of `h` */
static int box43(int h) { return h == 256 ? 1440 : 1542; }

/* The overlay's part of framebuffer row y, if any, over s_row (whose x 0 is
 * the box's left edge, box_x in the framebuffer): written into the
 * framebuffer after the row (so only writes reach it). */
static void overlay_row(uint32_t *fbrow, int y) {
  int oy, ox0, x;
  uint32_t line[OVL_MAX_W * OVL_SCALE];
  const uint8_t *s;
  if (!s_ovl_w)
    return;
  oy = y - (FB_H - s_ovl_h * OVL_SCALE) / 2;
  if (oy < 0 || oy >= s_ovl_h * OVL_SCALE)
    return;
  s = s_ovl + (size_t)(oy / OVL_SCALE) * s_ovl_w * 3;
  ox0 = (FB_W - s_ovl_w * OVL_SCALE) / 2;
  for (x = 0; x < s_ovl_w; x++, s += 3) {
    uint32_t c = 0xFF000000u | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
    line[x * 2] = line[x * 2 + 1] = c;
  }
  memcpy(fbrow + ox0, line, (size_t)s_ovl_w * OVL_SCALE * 4);
}

static void draw(uint32_t *fb, int buf, const Pic *p) {
  int box_x = (FB_W - p->box_w) / 2, y, prev = -1;
  if (s_fb_box[buf][0] != box_x || s_fb_box[buf][1] != p->box_w) { /* the bars, black */
    for (y = 0; y < FB_H; y++) {
      memset(fb + (size_t)y * FB_W, 0, (size_t)box_x * 4);
      memset(fb + (size_t)y * FB_W + box_x + p->box_w, 0, (size_t)(FB_W - box_x - p->box_w) * 4);
    }
    s_fb_box[buf][0] = box_x;
    s_fb_box[buf][1] = p->box_w;
  }
  for (y = 0; y < FB_H; y++) {
    int sy = y * p->lines / FB_H;
    uint32_t *fbrow = fb + (size_t)y * FB_W;
    if (sy != prev) {
      build_row(p, sy, s_row);
      prev = sy;
    }
    memcpy(fbrow + box_x, s_row, (size_t)p->box_w * 4);
    overlay_row(fbrow, y);
  }
}

static void draw_black(uint32_t *fb, int buf) {
  int y;
  for (y = 0; y < FB_H; y++) {
    memset(fb + (size_t)y * FB_W, 0, FB_W * 4);
    overlay_row(fb + (size_t)y * FB_W, y);
  }
  s_fb_box[buf][0] = 0, s_fb_box[buf][1] = FB_W;
}

/* the picture this present shows */
static int pick(Pic *p) {
  int slot = port_present_frame_slot();
  memset(p, 0, sizeof *p);
  if (slot >= 0 && (p->px = port_present_slot_pixels(slot)) != NULL) {
    int h = port_present_slot_h(slot); /* 256 for PAL's display (D148) */
    p->src_w = PRESENT_W;
    p->top = h == 240 ? 8 : 0;
    p->lines = h - 2 * p->top;
    p->box_w = port_present_frame_is_wide() ? FB_W : box43(h);
    s_present_frames++;
    return 1;
  }
  p->d = &currentDispEnv;
  if (p->d->disp.w <= 0 || g_GPUDisabledState)
    return 0;
  {
    int h = p->d->disp.h > 0 ? p->d->disp.h : 240;
    p->src_w = p->d->disp.w;
    p->top = h == 240 ? 8 : 0;
    p->lines = h - 2 * p->top;
    p->box_w = box43(h);
  }
  return 1;
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
  Pic p;
  if (g_ps4_gpu)
    return ps4_gpu_present();
  if (s_handle < 0)
    return 0;
  {
    unsigned long long t0 = ps4_now_us(), t1;
    int have = pick(&p);
    wait_flip_done();
    t1 = ps4_now_us();
    if (have) {
      xmap(p.box_w, p.px ? PRESENT_W : p.src_w);
      draw(s_fb[s_cur], s_cur, &p);
    } else
      draw_black(s_fb[s_cur], s_cur);
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
  unsigned n;
  if (g_ps4_gpu) {
    ps4_gpu_report(vblank);
    return;
  }
  n = s_shown - last ? s_shown - last : 1;
  host_log("video %d: %u pictures flipped (%u presenter frames), %u flip errors, display vcount "
           "%llu; per picture %llu us scaling, %llu us waiting for the flip",
           vblank, s_shown - last, s_present_frames, s_flip_err,
           (unsigned long long)ps4_video_vcount(), s_draw_us / n, s_wait_us / n);
  last = s_shown;
  s_present_frames = 0;
  s_draw_us = s_wait_us = 0;
}

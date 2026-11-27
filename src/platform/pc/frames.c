/* Pictures from the game thread to the main thread (docs/pc-build.md).
 *
 * The game thread files each picture with the vblank it is due at, as the
 * Vita's render thread takes them with present_due=1 (stage 7c, D112):
 *  - with the presenter on (widescreen, 60 fps), present.c's frames, through
 *    a platform capture (PresentCapture): capture() copies the VRAM rect the
 *    software rasterizer just drew (gpu=1: the GPU renderer draws it into
 *    this slot's picture texture, gpu_gl.c), commit() files it with its due vblank,
 *    before that vblank comes (the in-between frame a vblank ahead of the
 *    real one);
 *  - at a vblank where present.c has no frame (boot logos, loading, the
 *    presenter off), vblank.c files the PS1 display as it is then.
 * The main thread shows, at each display refresh, the newest picture whose
 * due vblank the clock has reached (pc_vblank_now). A ring of RING buffers
 * under a mutex; the writer never reuses the one the main thread holds.
 *
 * The presenter's frames waiting here follow present.c's queue as the
 * Vita's render thread does (gpu_thread.c rt_file): when its queue was full
 * and it dropped its oldest frame for a new one, that frame goes here too
 * and the rest are due a vblank sooner (#280, D268). Without that, a run of
 * ticks a vblank long (the level-loading screen: a real and an in-between
 * frame each vblank) kept every frame due four vblanks ahead, two per
 * vblank, and the ring's eight slots were reused before most came due. */
#include "pc.h"
#include "host.h"
#include "../../game/present.h"

#include "encore/platform.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdlib.h>
#include <string.h>

#include "psx/libgte.h"
#include "psx/libgpu.h"

#define RING 8
#define MAX_W 640
#define MAX_H 512

extern DISPENV currentDispEnv; /* libgpu.c: what the PS1 scans out */
extern int g_GPUDisabledState;

typedef struct {
  uint32_t *rgba;
  int w, h, wide, due;
  int gpu;      /* a GPU picture (gpu_gl.c's texture for this slot), not rgba */
  int pslot;    /* the presenter's slot (PresentFrame.slot), -1: the PS1 display */
  unsigned seq; /* 0: empty */
} Slot;

static Slot s_ring[RING];
static EncMutex *s_mu;
static unsigned s_seq;          /* the last filed */
static int s_held = -1;         /* the slot the main thread holds */
static unsigned s_taken_seq;    /* the newest the main thread took */
static int s_last_due = -1000000; /* the newest due filed */
/* the frame being captured (game thread) */
static uint32_t *s_pending;
static int s_pend_w, s_pend_h;
static unsigned s_posted, s_superseded, s_shown, s_display_posts;

static inline uint32_t rgba555(uint16_t p) {
  uint32_t r = p & 31, g = (p >> 5) & 31, b = (p >> 10) & 31;
  r = r << 3 | r >> 2;
  g = g << 3 | g >> 2;
  b = b << 3 | b >> 2;
  return 0xFF000000u | b << 16 | g << 8 | r; /* bytes R, G, B, A */
}

void pc_frames_init(void) {
  enc_mutex_create(&s_mu);
  for (int i = 0; i < RING; i++)
    s_ring[i].rgba = calloc((size_t)MAX_W * MAX_H, 4);
  s_pending = calloc((size_t)MAX_W * MAX_H, 4);
}

/* the 15-bit VRAM rect at (x, y), w x h, into dst; NTSC's 240 lines as the 224
 * the game draws (png.c's shots, the Vita's shown_top) */
static void copy_rect(uint32_t *dst, int x, int y, int w, int h, int *ow, int *oh) {
  const uint16_t *vram = host_vram();
  int top = h == 240 ? 8 : 0, lines = h - 2 * top;
  if (w > MAX_W)
    w = MAX_W;
  if (lines > MAX_H)
    lines = MAX_H;
  for (int r = 0; r < lines; r++) {
    const uint16_t *row = vram + (size_t)((y + top + r) & 511) * 1024;
    uint32_t *o = dst + (size_t)r * w;
    for (int c = 0; c < w; c++)
      o[c] = rgba555(row[(x + c) & 1023]);
  }
  *ow = w;
  *oh = lines;
}

/* the PS1 display (currentDispEnv) as it is now, 24-bit movies too */
static void copy_display(uint32_t *dst, int *ow, int *oh) {
  const DISPENV *d = &currentDispEnv;
  int w = d->disp.w > 0 ? d->disp.w : 320, h = d->disp.h > 0 ? d->disp.h : 240;
  if (d->disp.w <= 0 || g_GPUDisabledState) {
    memset(dst, 0, (size_t)320 * 224 * 4);
    *ow = 320;
    *oh = 224;
    return;
  }
  if (!d->isrgb24) {
    copy_rect(dst, d->disp.x, d->disp.y, w, h, ow, oh);
    return;
  }
  {
    const uint16_t *vram = host_vram();
    int top = h == 240 ? 8 : 0, lines = h - 2 * top;
    if (w > MAX_W)
      w = MAX_W;
    if (lines > MAX_H)
      lines = MAX_H;
    for (int r = 0; r < lines; r++) {
      const uint8_t *b = (const uint8_t *)(vram + (size_t)((d->disp.y + top + r) & 511) * 1024);
      uint32_t *o = dst + (size_t)r * w;
      for (int c = 0; c < w; c++) {
        int off = d->disp.x * 2 + c * 3;
        o[c] = 0xFF000000u | (uint32_t)b[(off + 2) & 2047] << 16 | (uint32_t)b[(off + 1) & 2047] << 8 |
               b[off & 2047];
      }
    }
    *ow = w;
    *oh = lines;
  }
}

static int s_pend_slot = -1; /* gpu=1: the ring slot a capture drew, not yet filed */

/* game thread: a slot to write (the oldest the main thread doesn't hold,
 * and not a capture's waiting for its commit), empty while it's written:
 * the main thread skips it */
static int acquire(void) {
  int i, best = -1;
  enc_mutex_lock(s_mu);
  for (i = 0; i < RING; i++) {
    if (i == s_held || i == s_pend_slot)
      continue;
    if (best < 0 || s_ring[i].seq < s_ring[best].seq)
      best = i;
  }
  s_ring[best].seq = 0;
  enc_mutex_unlock(s_mu);
  return best;
}

/* under s_mu: the presenter's frame f, about to be filed, in present.c's
 * queue: the frame it dropped for f goes, never shown, and (unless by time,
 * D148) the others waiting move up a vblank; a frame waiting in f's slot
 * is older than f and gone from that queue too (gpu_thread.c rt_file).
 * Waiting: filed after the newest the main thread took. */
static void follow_queue(const PresentFrame *f) {
  int i;
  for (i = 0; i < RING; i++) {
    Slot *s = &s_ring[i];
    if (!s->seq || s->seq <= s_taken_seq || s->pslot < 0 || i == s_held)
      continue;
    if (s->pslot == f->dropped || s->pslot == f->slot)
      s->seq = 0;
    else if (f->dropped >= 0 && !f->fixed)
      s->due--;
  }
}

/* game thread: slot i (written) filed as due; f: the presenter's frame, or
 * NULL (the PS1 display) */
static void publish(int i, int w, int h, int wide, int gpu, int due, const PresentFrame *f) {
  enc_mutex_lock(s_mu);
  if (f)
    follow_queue(f);
  s_ring[i].pslot = f ? f->slot : -1;
  s_ring[i].w = w;
  s_ring[i].h = h;
  s_ring[i].wide = wide;
  s_ring[i].gpu = gpu;
  s_ring[i].due = due;
  s_ring[i].seq = ++s_seq;
  if (due > s_last_due)
    s_last_due = due;
  s_posted++;
  enc_mutex_unlock(s_mu);
}

/* game thread: src (w x h, wide) filed as due */
static void post(const uint32_t *src, int w, int h, int wide, int due, const PresentFrame *f) {
  int i = acquire();
  memcpy(s_ring[i].rgba, src, (size_t)w * h * 4);
  publish(i, w, h, wide, 0, due, f);
}

void pc_frames_post_display(int due) {
  int w, h;
  if (g_pc_gpu) {
    int i = acquire();
    if (pc_gpu_display(i, &w, &h) == 0) {
      s_display_posts++;
      publish(i, w, h, 0, 1, due, NULL);
      pc_gpu_picture_end();
      return;
    }
    s_ring[i].seq = 0; /* unused: the oldest, taken again below */
    pc_gpu_sync_display(); /* a 24-bit display: from VRAM, on the CPU */
  }
  copy_display(s_pending, &w, &h);
  s_display_posts++;
  post(s_pending, w, h, 0, due, NULL);
}

/* present.c's platform capture (present.h PresentCapture) */
static void cap_capture(int x, int y, int w, int h) {
  if (g_pc_gpu) {
    if (s_pend_slot < 0)
      s_pend_slot = acquire();
    pc_gpu_capture(s_pend_slot, x, y, w, h, &s_pend_w, &s_pend_h);
    return;
  }
  copy_rect(s_pending, x, y, w, h, &s_pend_w, &s_pend_h);
}

static void cap_commit(const PresentFrame *f) {
  if (g_pc_gpu) {
    if (s_pend_slot < 0) { /* nothing captured: still the queue's drop */
      enc_mutex_lock(s_mu);
      follow_queue(f);
      enc_mutex_unlock(s_mu);
      return;
    }
    publish(s_pend_slot, s_pend_w, s_pend_h, f->wide, 1, f->due, f);
    s_pend_slot = -1;
    pc_gpu_picture_end();
    return;
  }
  post(s_pending, s_pend_w, s_pend_h, f->wide, f->due, f);
}

static const PresentCapture k_capture = {cap_capture, cap_commit, NULL};

void pc_frames_install_capture(void) { g_present_capture = &k_capture; }

int pc_frames_take(int now, PcFrame *out) {
  int i, best = -1;
  enc_mutex_lock(s_mu);
  /* the newest filed frame due by now: by due, then by filing order (a
   * display post and a presenter frame due at the same vblank: the later) */
  for (i = 0; i < RING; i++) {
    const Slot *s = &s_ring[i];
    if (!s->seq || s->due > now)
      continue;
    if (best < 0 || s->due > s_ring[best].due ||
        (s->due == s_ring[best].due && s->seq > s_ring[best].seq))
      best = i;
  }
  if (best < 0 || s_ring[best].seq <= s_taken_seq) {
    enc_mutex_unlock(s_mu);
    return 0;
  }
  if (s_taken_seq && s_ring[best].seq > s_taken_seq + 1)
    s_superseded += s_ring[best].seq - s_taken_seq - 1;
  s_taken_seq = s_ring[best].seq;
  s_held = best;
  out->rgba = s_ring[best].gpu ? NULL : s_ring[best].rgba;
  out->slot = best;
  out->w = s_ring[best].w;
  out->h = s_ring[best].h;
  out->wide = s_ring[best].wide;
  out->due = s_ring[best].due;
  out->seq = s_ring[best].seq;
  s_shown++;
  enc_mutex_unlock(s_mu);
  return 1;
}

void pc_frames_release(void) {
  enc_mutex_lock(s_mu);
  s_held = -1;
  enc_mutex_unlock(s_mu);
}

void pc_frames_report(int vblank) {
  static unsigned p0, s0, d0, x0;
  host_log("frames %d: %u filed (%u of the PS1 display), %u shown, %u never shown", vblank,
           s_posted - p0, s_display_posts - d0, s_shown - s0, s_superseded - x0);
  p0 = s_posted;
  s0 = s_shown;
  d0 = s_display_posts;
  x0 = s_superseded;
}

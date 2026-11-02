/* The render thread (config.txt render_thread=1, decisions D63): every call
 * into vitaGL (gpu_gl.c, D35) runs on its own thread on core 2, so the game
 * thread's tick N+1 overlaps the GL work of frame N.
 *
 * The game thread records the GR_* calls PsyCross makes (gpu.c sends them
 * here) as commands in a ring of bytes; the render thread runs them in the
 * same order through the same gl_* functions the game thread calls with
 * render_thread=0. What a command holds is copied out of the game's memory
 * when it's recorded, never pointed at:
 *   - a split's environments (the DRAWENV and DISPENV PsyCross passes), its
 *     texture and blend mode, and its first vertex and count;
 *   - the vertices of a DrawAllSplits (GR_UpdateVertexBuffer: PsyCross's
 *     buffer, after its parse) in one of NSLAB vertex slabs, with the vertex
 *     cache's answers when persp or subpixel are on. A slab is written again
 *     only once the render thread has started two later vertex buffers, so
 *     the primitive buffers (D54, D61) are never read after their DrawOTag;
 *   - LoadImage's pixels (GR_CopyVRAM). One too big for the ring waits for
 *     the render thread instead and is run with the game's own pointer;
 *   - what the GL side reads of the game's display state (g_gl_env:
 *     currentDispEnv, g_GPUDisabledState, widescreen), sent before the
 *     commands that read it whenever it changed.
 * Calls that return something, or that let the game thread read what the
 * render thread owns (s_vram, the targets), wait until every command before
 * them has run ("sync"): StoreImage (GR_ReadVRAM), shots, save states,
 * rescales, option changes, start-up (vglInit is on the render thread too).
 * So the game reads exactly what it read with render_thread=0.
 *
 * Presents: whether a vblank shows a new picture is decided here, on the
 * game thread, from the same state gpu_gl.c used to look at, so the picture
 * count, bench= and the boot screen's close don't depend on the threads'
 * timing. The render thread only draws and swaps. The game thread posts a
 * present only once the previous one has run: frame N is still shown at the
 * VSync after tick N+1's render path, as with render_thread=0, and a slow
 * GPU makes the game wait there (charged to "wait" in the time lines) rather
 * than queue frames. Presents with the options panel up (the menu), 24-bit
 * displays (boot logos), shot_hd and the first picture wait until shown.
 *
 * Nothing here changes what the game computes: the logic gets nothing from
 * the render thread except through the sync calls above. */
#include "vita.h"
#include "gpu_rt.h"
#include "watchdog.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PsyX/PsyX_render.h"
#include "gpu/PsyX_GPU.h"
#include "../../game/present.h"
#include "psx_walk.h"

GlEnv g_gl_env;
int g_rt_active;

#define RING (1u << 20)        /* commands; a frame's are ~40 KB */
#define INLINE_MAX (192 << 10) /* a LoadImage bigger than this waits instead */
#define NSLAB 4
#define PRIORITY 0x80   /* behind the mixer (0x70), ahead of the game (0xA0) */
#define STACK (4 << 20) /* the shader compiler runs here too: the main thread's size */

static uint64_t now_us(void) { return sceKernelGetProcessTimeWide(); }

typedef struct { /* PsyX_GPU.cpp's GPUDrawSplit starts like this (gpu_gl.c) */
  DRAWENV drawenv;
  DISPENV dispenv;
} SplitHead;

enum { C_WRAP, C_ENV, C_CLIP, C_TEX, C_BLEND, C_VERTS, C_DRAW, C_CLEAR, C_COPY, C_CALL,
       C_PRESENT, C_CAPTURE, C_COMMIT };

typedef struct {
  uint16_t type, pad;
  uint32_t size; /* bytes, this header included; a multiple of 8 */
} Cmd;

typedef struct { Cmd hd; GlEnv env; } CEnv;
typedef struct { Cmd hd; int enable; SplitHead head; } CClip;
typedef struct { Cmd hd; int a, b; } CTwo; /* C_TEX, C_BLEND, C_DRAW, C_COMMIT */
typedef struct { Cmd hd; int count, slab, side; } CVerts;
typedef struct { Cmd hd; int x, y, w, h; unsigned char r, g, b; } CClear;
typedef struct { Cmd hd; int x, y, w, h, dx, dy, has_src; } CCopy; /* + w*h words */
typedef struct { Cmd hd; void (*fn)(void *); void *ptr; uint8_t arg[32]; } CCall;
typedef struct { Cmd hd; int slot, wide, shot; } CPresent;
typedef struct { Cmd hd; int x, y, w, h; } CCapture;

static uint8_t *s_ring;
static uint32_t s_w;              /* game thread: bytes recorded */
static volatile uint32_t s_wpub;  /* bytes the render thread may run */
static uint32_t s_nposted;        /* game thread: commands recorded */
static SceUID s_rt_sema, s_game_sema;
static volatile int s_rt_sleeping;

/* What the render thread has done, each counted up; the game thread waits
 * for one of them to reach a target. */
enum { K_BYTES, K_CMDS, K_PRESENTS, K_VERTS, K_COUNT };
static volatile uint32_t s_done[K_COUNT];
static volatile int s_game_waiting; /* 1 + the K_ it waits for, 0: none */
static volatile uint32_t s_game_target;

static GrVertex *s_slab[NSLAB];
static VCacheVert *s_slab_side[NSLAB];
static uint32_t s_verts_posted, s_presents_posted;

/* stats: the render thread's (32-bit, counted up; the report takes the
 * differences) and the game thread's waits by reason */
static volatile uint32_t s_rt_busy_us, s_rt_cmds, s_rt_bad; /* bad: unknown commands */
static volatile uint32_t s_rt_picture_us; /* busy per picture, averaged over ~8 */
static uint64_t s_bytes_posted;
enum { W_RING, W_FRAME, W_SYNC, W_SLAB, W_START, W_COUNT };
static const char *const k_wait_names[W_COUNT] = {"ring full", "previous frame not shown yet",
                                                  "sync calls", "vertex slabs",
                                                  "GPU start-up and shader compiles"};
static uint64_t s_wait_us[W_COUNT];
static unsigned s_waits[W_COUNT];

static int reached(int k, uint32_t target) { return (int32_t)(s_done[k] - target) >= 0; }

/* ---- the render thread ---- */

static void rt_wake_game(void) {
  int k = __atomic_load_n(&s_game_waiting, __ATOMIC_SEQ_CST);
  if (k && reached(k - 1, s_game_target) && __atomic_exchange_n(&s_game_waiting, 0, __ATOMIC_SEQ_CST))
    sceKernelSignalSema(s_game_sema, 1);
}

static void count(int k) {
  __atomic_store_n(&s_done[k], s_done[k] + 1, __ATOMIC_RELEASE);
}

static void run(const Cmd *c) {
  switch (c->type) {
  case C_WRAP:
    break;
  case C_ENV:
    g_gl_env = ((const CEnv *)c)->env;
    break;
  case C_CLIP: {
    const CClip *k = (const CClip *)c;
    gl_GR_SetupClipMode(&k->head.drawenv.clip, k->enable);
    break;
  }
  case C_TEX:
    gl_GR_SetTexture((TextureID)((const CTwo *)c)->a, (TexFormat)((const CTwo *)c)->b);
    break;
  case C_BLEND:
    gl_GR_SetBlendMode((BlendMode)((const CTwo *)c)->a);
    break;
  case C_VERTS: {
    const CVerts *k = (const CVerts *)c;
    gl_vertices(s_slab[k->slab], k->side ? s_slab_side[k->slab] : g_vcache_side, k->count);
    count(K_VERTS); /* the slab before this one is free from here */
    break;
  }
  case C_DRAW:
    gl_GR_DrawTriangles(((const CTwo *)c)->a, ((const CTwo *)c)->b);
    break;
  case C_CLEAR: {
    const CClear *k = (const CClear *)c;
    gl_GR_ClearVRAM(k->x, k->y, k->w, k->h, k->r, k->g, k->b);
    break;
  }
  case C_COPY: {
    const CCopy *k = (const CCopy *)c;
    if (k->has_src)
      gl_GR_CopyVRAM((unsigned short *)(k + 1), 0, 0, k->w, k->h, k->dx, k->dy);
    else
      gl_GR_CopyVRAM(NULL, k->x, k->y, k->w, k->h, k->dx, k->dy);
    break;
  }
  case C_CALL: {
    const CCall *k = (const CCall *)c;
    k->fn(k->ptr ? k->ptr : (void *)k->arg);
    break;
  }
  case C_PRESENT: {
    const CPresent *k = (const CPresent *)c;
    gl_present(k->slot, k->wide, k->shot);
    count(K_PRESENTS);
    break;
  }
  case C_CAPTURE: {
    const CCapture *k = (const CCapture *)c;
    gl_pres_capture(k->x, k->y, k->w, k->h);
    break;
  }
  case C_COMMIT:
    gl_pres_commit(((const CTwo *)c)->a);
    break;
  default:
    s_rt_bad++;
    break;
  }
}

static int rt_main(SceSize args, void *argp) {
  uint32_t r = 0;
  (void)args;
  (void)argp;
  for (;;) {
    const Cmd *c;
    uint64_t t0;
    if (__atomic_load_n(&s_wpub, __ATOMIC_ACQUIRE) == r) {
      /* nothing to run: sleep until the game thread posts (it signals only
       * when it sees s_rt_sleeping, so nothing is lost either way) */
      __atomic_store_n(&s_rt_sleeping, 1, __ATOMIC_SEQ_CST);
      g_wd.rt = 0; /* breadcrumbs (watchdog.c, D73) */
      if (__atomic_load_n(&s_wpub, __ATOMIC_SEQ_CST) != r) {
        if (!__atomic_exchange_n(&s_rt_sleeping, 0, __ATOMIC_SEQ_CST))
          sceKernelWaitSema(s_rt_sema, 1, NULL); /* its signal is on the way */
      } else {
        sceKernelWaitSema(s_rt_sema, 1, NULL);
      }
      continue;
    }
    c = (const Cmd *)(s_ring + (r & (RING - 1)));
    t0 = now_us();
    g_wd.rt = c->type + 1, g_wd.rt_runs++;
    run(c);
    s_rt_busy_us += (uint32_t)(now_us() - t0);
    if (c->type == C_PRESENT) { /* what the picture since the last one took */
      static uint32_t busy0, fin0;
      uint32_t d = (s_rt_busy_us - busy0) - (g_gl_finish_us - fin0); /* gpu_time's waits aside */
      busy0 = s_rt_busy_us;
      fin0 = g_gl_finish_us;
      s_rt_picture_us = (s_rt_picture_us * 7 + d) / 8;
    }
    s_rt_cmds++;
    r += c->size;
    __atomic_store_n(&s_done[K_BYTES], r, __ATOMIC_RELEASE);
    count(K_CMDS);
    rt_wake_game();
  }
  return 0;
}

/* ---- the game thread ---- */

/* Until the render thread's count k reaches target. */
static void game_wait(int why, int k, uint32_t target) {
  uint64_t t0;
  if (reached(k, target))
    return;
  t0 = now_us();
  prof_enter(PROF_WAIT);
  g_wd.rt_wait = why + 1;
  while (!reached(k, target)) {
    SceUInt tmo = 5000000; /* start-up's shader compile takes seconds */
    s_game_target = target;
    __atomic_store_n(&s_game_waiting, k + 1, __ATOMIC_SEQ_CST);
    if (reached(k, target)) {
      if (!__atomic_exchange_n(&s_game_waiting, 0, __ATOMIC_SEQ_CST))
        sceKernelWaitSema(s_game_sema, 1, NULL); /* its signal is on the way */
      break;
    }
    if (sceKernelWaitSema(s_game_sema, 1, &tmo) < 0) {
      if (why == W_START) /* expected: seconds on hardware */
        vita_log("render: GPU start-up still running after %u s (vitaGL and the shaders)",
                 (unsigned)((now_us() - t0) / 1000000));
      else
        vita_log("render: still waiting (%s): count %d at %u of %u; %u commands posted, %u run",
                 k_wait_names[why], k, (unsigned)s_done[k], (unsigned)target,
                 (unsigned)s_nposted, (unsigned)s_done[K_CMDS]);
    }
  }
  g_wd.rt_wait = 0;
  prof_leave();
  s_wait_us[why] += now_us() - t0;
  s_waits[why]++;
}

/* Room for a command of `size` bytes (a multiple of 8) at s_w. With the
 * parse thread (D71) the ring has one writer at a time: that thread from a
 * DrawOTag's handover to the join, the game thread otherwise, so the game
 * thread joins first. */
static Cmd *reserve(int type, uint32_t size) {
  uint32_t off;
  Cmd *c;
  if (!vita_parse_on_walker())
    psx_walk_join();
  off = s_w & (RING - 1);
  if (off + size > RING) { /* the rest of the ring is skipped */
    game_wait(W_RING, K_BYTES, s_w + (RING - off) - RING);
    c = (Cmd *)(s_ring + off);
    c->type = C_WRAP;
    c->size = RING - off;
    s_w += RING - off;
    s_nposted++;
    off = 0;
  }
  game_wait(W_RING, K_BYTES, s_w + size - RING);
  c = (Cmd *)(s_ring + off);
  c->type = (uint16_t)type;
  c->size = size;
  return c;
}

#define RESERVE(T, type, extra) ((T *)reserve(type, (sizeof(T) + (extra) + 7) & ~7u))

static void post(const Cmd *c) {
  s_w += c->size;
  s_bytes_posted += c->size;
  s_nposted++;
  __atomic_store_n(&s_wpub, s_w, __ATOMIC_SEQ_CST);
  if (__atomic_load_n(&s_rt_sleeping, __ATOMIC_SEQ_CST) &&
      __atomic_exchange_n(&s_rt_sleeping, 0, __ATOMIC_SEQ_CST))
    sceKernelSignalSema(s_rt_sema, 1);
}

/* Everything posted has run. */
static int s_sync_why = W_SYNC;
static void finish(void) { game_wait(s_sync_why, K_CMDS, s_nposted); }

static void env_now(GlEnv *e) {
  memset(e, 0, sizeof *e);
  memcpy(&e->disp, &currentDispEnv, sizeof e->disp);
  e->disabled = g_GPUDisabledState;
  e->wide = g_present.widescreen;
}

/* The display state when a DrawOTag handed its list to the parse thread
 * (D71): what the walk's draws see, as they would have in the DrawOTag. */
static GlEnv s_walk_env;

void vita_gpu_walk_submitted(void) { env_now(&s_walk_env); }

/* The game's display state as the GL side should see it from here on. */
static void env_sync(void) {
  static GlEnv sent;
  static int sent_valid;
  GlEnv e;
  if (vita_parse_on_walker()) {
    e = s_walk_env;
  } else {
    psx_walk_join(); /* the walk's commands go first */
    env_now(&e);
  }
  if (!g_rt_active) {
    g_gl_env = e;
    return;
  }
  if (sent_valid && !memcmp(&e, &sent, sizeof e))
    return;
  sent = e;
  sent_valid = 1;
  {
    CEnv *c = RESERVE(CEnv, C_ENV, 0);
    c->env = e;
    post(&c->hd);
  }
}

/* fn(arg) where the GL is: directly, or on the render thread. sync: wait for
 * it (arg may then point at the caller's stack); else arg is copied. */
static void call(void (*fn)(void *), void *arg, int size, int sync) {
  CCall *c;
  env_sync();
  if (!g_rt_active) {
    fn(arg);
    return;
  }
  c = RESERVE(CCall, C_CALL, 0);
  c->fn = fn;
  c->ptr = sync ? arg : NULL;
  if (!sync && size > 0)
    memcpy(c->arg, arg, size > (int)sizeof c->arg ? (int)sizeof c->arg : size);
  post(&c->hd);
  if (sync)
    finish();
}

static int start(void) {
  SceUID th;
  int i;
  s_ring = memalign(64, RING);
  for (i = 0; i < NSLAB; i++)
    s_slab[i] = malloc(sizeof(GrVertex) * MAX_VERTEX_BUFFER_SIZE);
  s_rt_sema = sceKernelCreateSema("spyro_render_run", 0, 0, 1, NULL);
  s_game_sema = sceKernelCreateSema("spyro_render_game", 0, 0, 1, NULL);
  if (!s_ring || !s_slab[NSLAB - 1] || s_rt_sema < 0 || s_game_sema < 0) {
    vita_log("render: no memory or semaphores for the render thread; GL on the game thread");
    return -1;
  }
  th = sceKernelCreateThread("spyro_render", rt_main, PRIORITY, STACK, 0,
                             SCE_KERNEL_CPU_MASK_USER_2, NULL);
  if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("render: no thread (%08x); GL on the game thread", (unsigned)th);
    return -1;
  }
  g_rt_active = 1;
  vita_watchdog_thread(WD_T_RENDER, th);
  /* the game thread on core 0: the mixer has core 1 (D37), this core 2 */
  sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
  vita_log("render: thread on core 2, priority 0x%x; game thread on core 0 (affinity %08x), "
           "priority 0x%x (render_thread=1, D63)",
           PRIORITY, (unsigned)sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId()),
           (unsigned)sceKernelGetThreadCurrentPriority());
  return 0;
}

/* ---- GR_* from PsyCross (gpu.c, when the render thread runs) ---- */

void rt_GR_SetupClipMode(const RECT16 *rect, int enable) {
  CClip *c;
  env_sync(); /* a new target's scale reads the display (set_scale) */
  c = RESERVE(CClip, C_CLIP, 0);
  c->enable = enable;
  memcpy(&c->head, rect, sizeof c->head); /* rect is the split's drawenv.clip */
  post(&c->hd);
}

static void post_two(int type, int a, int b) {
  CTwo *c = RESERVE(CTwo, type, 0);
  c->a = a;
  c->b = b;
  post(&c->hd);
}

void rt_GR_SetTexture(TextureID texture, TexFormat texFormat) {
  post_two(C_TEX, (int)texture, (int)texFormat);
}
void rt_GR_SetBlendMode(BlendMode blendMode) { post_two(C_BLEND, (int)blendMode, 0); }
void rt_GR_DrawTriangles(int start_vertex, int triangles) {
  post_two(C_DRAW, start_vertex, triangles);
}

/* gpu_gl.c does nothing for these */
void rt_GR_SetOffscreenState(const RECT16 *rect, int enable) {
  (void)rect;
  (void)enable;
}
void rt_GR_SetStencilMode(int drawPrim) { (void)drawPrim; }
void rt_GR_SetOverrideTextureSize(int width, int height) {
  (void)width;
  (void)height;
}
void rt_GR_PushDebugLabel(const char *label) { (void)label; }
void rt_GR_PopDebugLabel(void) {}
void rt_GR_UpdateVRAM(void) {}
void rt_GR_ReadFramebufferDataToVRAM(void) {}
void rt_GR_Clear(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b) {
  (void)x, (void)y, (void)w, (void)h, (void)r, (void)g, (void)b;
}

void rt_GR_UpdateVertexBuffer(const GrVertex *vertices, int count) {
  int slab = (int)(s_verts_posted % NSLAB), side = gl_vcache_side_used();
  CVerts *c;
  if (count > MAX_VERTEX_BUFFER_SIZE)
    count = MAX_VERTEX_BUFFER_SIZE;
  if (count < 0)
    count = 0;
  /* this slab's last vertex buffer is NSLAB back; the render thread has
   * left it once it has started the one after it */
  if (s_verts_posted >= NSLAB)
    game_wait(W_SLAB, K_VERTS, s_verts_posted - NSLAB + 2);
  memcpy(s_slab[slab], vertices, (size_t)count * sizeof *vertices);
  if (side) {
    psx_vcache_set_base(vertices);
    if (!s_slab_side[slab])
      s_slab_side[slab] = malloc(sizeof(VCacheVert) * (MAX_VERTEX_BUFFER_SIZE + 6));
    if (s_slab_side[slab])
      memcpy(s_slab_side[slab], g_vcache_side, (size_t)count * sizeof(VCacheVert));
    else
      side = 0;
  }
  c = RESERVE(CVerts, C_VERTS, 0);
  c->count = count;
  c->slab = slab;
  c->side = side;
  post(&c->hd);
  s_verts_posted++;
}

void rt_GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g,
                     unsigned char b) {
  CClear *c = RESERVE(CClear, C_CLEAR, 0);
  c->x = x, c->y = y, c->w = w, c->h = h;
  c->r = r, c->g = g, c->b = b;
  post(&c->hd);
}

typedef struct {
  unsigned short *p;
  int x, y, w, h, dx, dy;
} XferArgs;

static void k_copy(void *a) {
  const XferArgs *x = a;
  gl_GR_CopyVRAM(x->p, x->x, x->y, x->w, x->h, x->dx, x->dy);
}

static void k_read(void *a) {
  const XferArgs *x = a;
  gl_GR_ReadVRAM(x->p, x->x, x->y, x->w, x->h);
}

void rt_GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y) {
  size_t bytes = w > 0 && h > 0 ? (size_t)w * h * 2 : 0;
  CCopy *c;
  if (src && bytes > INLINE_MAX) { /* a level's textures: run with the game's pointer */
    XferArgs a = {src, x, y, w, h, dst_x, dst_y};
    call(k_copy, &a, 0, 1);
    return;
  }
  c = RESERVE(CCopy, C_COPY, src ? bytes : 0);
  c->x = x, c->y = y, c->w = w, c->h = h, c->dx = dst_x, c->dy = dst_y;
  c->has_src = src != NULL;
  if (src) { /* rows y.. of a w-wide image, from column x, packed */
    unsigned short *d = (unsigned short *)(c + 1);
    int i;
    for (i = 0; i < h; i++)
      memcpy(d + (size_t)i * w, src + (size_t)(y + i) * w + x, (size_t)w * 2);
  }
  post(&c->hd);
}

void rt_GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h) {
  XferArgs a = {dst, x, y, dst_w, dst_h, 0, 0};
  call(k_read, &a, 0, 1);
}

/* ---- presents (vblank.c through video.c) ---- */

static struct {
  unsigned vgen;
  DISPENV disp;
  int disabled, valid;
} s_last;                 /* the display rect last presented */
static int s_on_screen = -1; /* presentation slot the screen shows, -1: the display rect */
static int s_hd_shot;     /* vblank of a pending 960x544 shot, 0 = none */
static const uint8_t *s_ovl;
static int s_shown_any;

void vita_gpu_shot_hd(int vblank) { s_hd_shot = vblank; }

static void post_present(int slot, int wide) {
  int hd = s_hd_shot, sync;
  CPresent *c;
  s_hd_shot = 0;
  env_sync();
  if (!g_rt_active) {
    gl_present(slot, wide, hd);
    return;
  }
  /* waits until shown: the panel's pixels are the menu's (it redraws them),
   * a 24-bit display and the software rasterizer's picture are read from
   * the game's side, an HD shot wants the file, and the first picture lets
   * the disc screen's framebuffer go (vblank.c) */
  sync = hd || s_ovl || !g_vita_gpu || !s_shown_any ||
         (slot < 0 && currentDispEnv.isrgb24 && currentDispEnv.disp.w > 0 && !g_GPUDisabledState);
  s_shown_any = 1;
  /* the frame before has been shown */
  game_wait(W_FRAME, K_PRESENTS, s_presents_posted);
  c = RESERVE(CPresent, C_PRESENT, 0);
  c->slot = slot;
  c->wide = wide;
  c->shot = hd;
  post(&c->hd);
  s_presents_posted++;
  if (sync)
    game_wait(W_SYNC, K_PRESENTS, s_presents_posted);
}

/* The display rect, unless it's what the screen shows already. */
static int present_display(void) {
  const DISPENV *d = &currentDispEnv;
  if (s_last.valid && s_last.vgen == prof_vram_gen() && s_last.disabled == g_GPUDisabledState &&
      !memcmp(&s_last.disp, d, sizeof s_last.disp) && !s_hd_shot)
    return 0; /* the display keeps showing the last picture */
  s_last.valid = 1;
  s_last.vgen = prof_vram_gen();
  s_last.disabled = g_GPUDisabledState;
  memcpy(&s_last.disp, d, sizeof s_last.disp);
  post_present(-1, 0);
  return 1;
}

/* One frame: a presentation frame (widescreen, 60 fps), or the display
 * rect. Returns 1 if it was a new picture. */
int vita_gpu_present(void) {
  int slot = port_present_frame_slot();
  if (slot >= 0) {
    if (!port_present_frame_new() && s_on_screen == slot && !s_hd_shot)
      return 0; /* the display keeps showing it */
    s_on_screen = slot;
    s_last.valid = 0; /* the display rect must be drawn again when it's back */
    post_present(slot, port_present_frame_is_wide());
    return 1;
  }
  s_on_screen = -1;
  return present_display();
}

void vita_gpu_force_redraw(void) {
  s_last.valid = 0;
  s_on_screen = -1;
}

/* The picture on screen drawn and swapped again, without taking a new
 * presentation frame (shot_hold: Vita3K can show an older swap buffer). */
void vita_gpu_repeat(void) {
  int slot = s_on_screen;
  vita_gpu_force_redraw();
  if (slot >= 0) {
    s_on_screen = slot;
    post_present(slot, port_present_frame_is_wide());
  } else {
    present_display();
  }
  if (g_rt_active)
    finish();
}

typedef struct {
  const uint8_t *rgb;
  int w, h;
} OvlArgs;

static void k_overlay(void *a) {
  const OvlArgs *o = a;
  gl_overlay(o->rgb, o->w, o->h);
}

/* The options panel (menu.c) over the picture, NULL: none. */
void vita_gpu_overlay(const uint8_t *rgb, int w, int h) {
  OvlArgs a = {rgb, w, h};
  s_ovl = rgb;
  call(k_overlay, &a, 0, 1);
}

/* present.c's captures (widescreen, 60 fps; D30) */
static void capture(int x, int y, int w, int h) {
  CCapture *c;
  env_sync();
  if (!g_rt_active) {
    gl_pres_capture(x, y, w, h);
    return;
  }
  c = RESERVE(CCapture, C_CAPTURE, 0);
  c->x = x, c->y = y, c->w = w, c->h = h;
  post(&c->hd);
}

static void commit(int slot) {
  if (slot == s_on_screen)
    s_on_screen = -1; /* new content: draw it again */
  if (!g_rt_active)
    gl_pres_commit(slot);
  else
    post_two(C_COMMIT, slot, 0);
}

static const PresentCapture s_capture = {capture, commit};

const struct PresentCapture *vita_gpu_present_capture(void) { return &s_capture; }

/* ---- the rest of vita.h's gpu calls ---- */

static void k_init(void *a) { *(int *)a = gl_gpu_init(); }

int vita_gpu_init(void) {
  int r;
  if (g_vita_opts.render_thread)
    start();
  else
    vita_log("render: GL calls on the game thread (render_thread=0)");
  s_sync_why = W_START;
  call(k_init, &r, 0, 1);
  s_sync_why = W_SYNC;
  return r;
}

static void k_sync_display(void *a) {
  (void)a;
  gl_sync_display();
}

/* targets over the display rect -> s_vram (screenshots, 24-bit display);
 * then the render thread is idle until the game thread posts again */
void vita_gpu_sync_display(void) { call(k_sync_display, NULL, 0, 1); }

static void k_rescale(void *a) {
  (void)a;
  gl_rescale();
}

/* Widescreen switched (menu.c), bench=, a resolution change: every target
 * drawn again at the new width. */
void vita_gpu_rescale(void) {
  s_last.valid = 0;
  s_on_screen = -1; /* the presentation slots may be made again */
  call(k_rescale, NULL, 0, 1);
}

/* Save states (D51): VRAM as the PS1 has it. Every target dropped first:
 * drop_target() replays its log into s_vram (no GPU readback). The render
 * thread then has nothing to run until the game thread posts again, so the
 * caller may read and write s_vram. */
uint16_t *vita_gpu_vram_take(void) {
  vita_gpu_rescale();
  return gl_host_vram();
}

static void k_vram_loaded(void *a) {
  (void)a;
  gl_vram_loaded();
}

void vita_gpu_vram_loaded(void) { call(k_vram_loaded, NULL, 0, 0); }

typedef struct {
  int res, filter, persp, subpixel, cache, result;
} OptArgs;

static void k_options(void *a) {
  OptArgs *o = a;
  o->result = gl_set_options(o->res, o->filter, o->persp, o->subpixel, o->cache);
}

int vita_gpu_set_options(int res, int filter, int persp, int subpixel, int cache) {
  OptArgs a = {res, filter, persp, subpixel, cache, 0};
  int old_res = g_vita_opts.gpu_res;
  call(k_options, &a, 0, 1);
  if (g_vita_opts.gpu_res != old_res) { /* it rescaled */
    s_last.valid = 0;
    s_on_screen = -1;
  }
  return a.result;
}

static void k_arm(void *a) {
  (void)a;
  gl_arm_sample();
}

void vita_gpu_arm_sample(void) { call(k_arm, NULL, 0, 0); }
int vita_gpu_take_sample(unsigned *frame_us, unsigned *tail_us) {
  return gl_take_sample(frame_us, tail_us);
}

static void k_report(void *a) { gl_report(*(const int *)a); }

/* "gpu" lines (from where the GL is), and with the render thread a "render"
 * line: how busy it was, and how long the game thread waited for it. */
void vita_gpu_report(int vblank) {
  static uint32_t busy0, cmds0;
  static uint64_t t0;
  static int v0;
  uint64_t t = now_us();
  call(k_report, &vblank, sizeof vblank, 0);
  if (g_rt_active && t0 && vblank > v0) {
    uint32_t busy = s_rt_busy_us - busy0, cmds = s_rt_cmds - cmds0;
    unsigned wall = (unsigned)(t - t0), n = (unsigned)(vblank - v0);
    char waits[256];
    int i, len = 0;
    for (i = 0; i < W_START; i++) /* the start-up's has its own line (gl_gpu_init) */
      len += snprintf(waits + len, sizeof waits - len, "%s%s %u.%02u ms in %u", i ? ", " : "",
                      k_wait_names[i], (unsigned)(s_wait_us[i] / 1000),
                      (unsigned)(s_wait_us[i] % 1000 / 10), s_waits[i]);
    vita_log("render %d: render thread busy %u.%02u ms per vblank (%u%%), %u.%02u ms per picture; "
             "%u commands run (%u KB), %u in flight, %u unknown; game thread waited for it: %s",
             vblank, busy / n / 1000, busy / n % 1000 / 10, wall ? (unsigned)(busy * 100ull / wall) : 0,
             (unsigned)s_rt_picture_us / 1000, (unsigned)s_rt_picture_us % 1000 / 10, cmds,
             (unsigned)(s_bytes_posted / 1024), (unsigned)(s_nposted - s_done[K_CMDS]),
             (unsigned)s_rt_bad, waits);
  }
  busy0 = s_rt_busy_us;
  cmds0 = s_rt_cmds;
  t0 = t;
  v0 = vblank;
  s_bytes_posted = 0;
  memset(s_wait_us, 0, sizeof s_wait_us);
  memset(s_waits, 0, sizeof s_waits);
}

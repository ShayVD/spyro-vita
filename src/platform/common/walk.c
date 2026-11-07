/* The OT walk from a copy of the frame's primitives (psx_walk.h, decisions
 * D71).
 *
 * DrawOTag (libgpu.c) hands its list over (psx_walk_hand_over) instead of
 * walking it.
 * What the list can reach is copied first into a second 16 MB-aligned arena
 * at the same offsets: the draw's primitive span and the OT's terminator
 * (present.c port_present_prim_ranges; the game links its OT buckets
 * straight from primitive to primitive, order_table.c). The walk then
 * follows the copy (its next_packet), so the game may write its next
 * frame's primitives meanwhile. A packet outside the copy is read in place
 * and counted (stats.foreign): none is expected.
 *
 * Who walks it:
 *  - PSX_WALK_THREAD (the Vita, config.txt parse_thread=1): the platform's
 *    thread, at once;
 *  - PSX_WALK_LAZY (the host's --walk-lazy): the game thread itself, at its
 *    next gate. That's the latest a walk can happen, so comparing a host run
 *    with the in-place walk checks every gate and every deferral.
 *
 * The walk owns the GPU state (gpu_walk.c: the draw and display environments, the
 * splits, the vertex buffer) and, on the Vita, the render thread's command
 * ring, from the handover to the join. The game thread joins before it
 * touches any of it: libgpu's calls and VSync join first (psx_walk_join), save
 * states join, and present.c defers its few touches after a DrawOTag
 * (psx_walk_after). What the walk reads of the game's other state is taken
 * at the handover: g_psx_vram_frozen here, the vertex cache's table
 * (psx_vcache_frame_take), the display state the GL side sees (the Vita's
 * PsxWalkThread.submitted). */
#include "psx_walk.h"
#include "psx_arena.h"
#include "../../game/psx_mem.h"
#include "psx_vcache.h"
#include "psx_libgpu.h"
#include "psx_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <sys/mman.h>
#else
#include <malloc.h>
#endif

#define MAX_RANGES 8
#define DEFER_BYTES 2048

static int s_mode;
static PsxWalkRangesFn s_ranges_fn;
static PsxWalkThread s_thr;
static uintptr_t s_copy; /* the second arena */
static unsigned long long (*s_now)(void);

static struct {
  PsxWalkRange r[MAX_RANGES];
  int n;
  uintptr_t head; /* the list's first packet, in the copy */
  int tag;        /* g_psx_walk_tag at the handover */
  int nd;         /* g_psx_walk_nd at the handover */
  int frozen;     /* g_psx_vram_frozen at the handover */
  PsxVcacheFrame vc;
} s_job;

static int s_pending;          /* game thread: handed over, not joined */
static volatile int s_in_job;  /* psx_walk_run is running */
static unsigned char s_defer[DEFER_BYTES];
static int s_defer_len;
static PsxWalkStats s_stats;
int g_psx_walk_tag = -1;
int g_psx_walk_nd;

typedef struct {
  void (*fn)(void *);
  int size; /* bytes of argument after this header, rounded up to 8 */
  int pad;
} Deferred;

static unsigned long long now(void) { return s_now ? s_now() : 0; }

void psx_walk_set_clock(unsigned long long (*now_us)(void)) { s_now = now_us; }

int psx_walk_mode(void) { return s_mode; }
int psx_walk_pending(void) { return s_pending; }

/* the calling thread is the one running psx_walk_run() */
static int on_walker(void) {
  return s_mode == PSX_WALK_THREAD ? s_thr.on_walker() : s_in_job;
}

/* The packet at PS1 offset a: in the copy if it lies wholly in a copied
 * range, else in place. */
static uintptr_t place(uint32_t a) {
  int i;
  for (i = 0; i < s_job.n; i++) {
    const PsxWalkRange *r = &s_job.r[i];
    if (a >= r->lo && a + 4 <= r->hi) {
      uint32_t len = *(const uint8_t *)(s_copy + a + 3); /* the tag's length byte */
      if (a + 4 + len * 4 <= r->hi)
        return s_copy + a;
      break;
    }
  }
  s_stats.foreign++;
  s_stats.foreign_at = PSX_MEM_PSX + a;
  return g_psx_arena_base + a;
}

static uintptr_t next_packet(uintptr_t packet) {
  return place(MEM_OT_OFS(*(const uint32_t *)packet));
}

int psx_walk_vram_frozen(void) { return on_walker() ? s_job.frozen : g_psx_vram_frozen; }

int psx_walk_nd(void) { return on_walker() ? s_job.nd : g_psx_walk_nd; }

int psx_walk_hand_over(u_long *p) {
  uint32_t head = (uint32_t)((uintptr_t)p - g_psx_arena_base);
  unsigned long long t0;
  int i;
  if (s_mode == PSX_WALK_OFF)
    return 0; /* no walk: DrawOTag walks in place, uncounted */
  if ((uintptr_t)p < g_psx_arena_base || head >= PSX_MEM_SIZE) {
    s_stats.in_place++;
    return 0; /* DrawOTag walks it in place now */
  }
  t0 = now();
  s_job.n = s_ranges_fn(s_job.r, MAX_RANGES);
  for (i = 0; i < s_job.n; i++) {
    PsxWalkRange *r = &s_job.r[i];
    if (r->hi > PSX_MEM_SIZE)
      r->hi = PSX_MEM_SIZE;
    if (r->lo >= r->hi) {
      r->lo = r->hi = 0;
      continue;
    }
    memcpy((void *)(s_copy + r->lo), (const void *)(g_psx_arena_base + r->lo), r->hi - r->lo);
    s_stats.copied += r->hi - r->lo;
  }
  {
    unsigned foreign = s_stats.foreign;
    s_job.head = place(head);
    if (s_stats.foreign != foreign) { /* the list starts outside the copy: walk it here */
      s_stats.foreign = foreign;
      s_stats.in_place++;
      s_stats.copy_us += now() - t0;
      return 0;
    }
  }
  s_job.frozen = g_psx_vram_frozen;
  s_job.tag = g_psx_walk_tag;
  s_job.nd = g_psx_walk_nd;
  psx_vcache_frame_take(&s_job.vc);
  s_pending = 1;
  s_stats.walks++;
  s_stats.copy_us += now() - t0;
  if (s_mode == PSX_WALK_THREAD) {
    s_thr.submitted();
    s_thr.post();
  }
  return 1;
}

void psx_walk_run(void) {
  unsigned long long t0 = now();
  PsxGpuWalk *const w = psx_gpu_game_walk();
  s_in_job = 1;
  psx_vcache_frame_use(&s_job.vc);
  /* DrawOTag's own walk (libgpu.c) from here on, over the copy: the
   * packets its substitutions name (present.c's narrowed HUD) are found
   * by their origin (D122) */
  w->copy = s_copy;
  psx_gpu_walk_scene_in(w, (u_long *)s_job.head, 0, next_packet);
  w->copy = 0;
  psx_gpu_draw_splits_in(w);
  psx_vcache_frame_use(NULL);
  s_in_job = 0;
  t0 = now() - t0;
  s_stats.walk_us += t0;
  if ((unsigned)s_job.tag < PSX_WALK_TAGS) {
    s_stats.walks_tag[s_job.tag]++;
    s_stats.walk_us_tag[s_job.tag] += t0;
  }
}

static void run_deferred(void) {
  int off = 0, len = s_defer_len;
  s_defer_len = 0;
  while (off < len) {
    const Deferred *d = (const Deferred *)(s_defer + off);
    d->fn((void *)(d + 1));
    off += (int)sizeof *d + d->size;
  }
}

void psx_walk_join(void) {
  unsigned long long t0;
  if (!s_pending || on_walker())
    return; /* nothing handed over; or the walk's own calls (a fill's ClearImage) */
  s_pending = 0;
  t0 = now();
  if (s_mode == PSX_WALK_LAZY) {
    psx_walk_run();
  } else {
    s_thr.wait();
    t0 = now() - t0;
    s_stats.wait_us += t0;
    if ((unsigned)s_job.tag < PSX_WALK_TAGS)
      s_stats.wait_us_tag[s_job.tag] += t0;
  }
  s_stats.joins++;
  run_deferred();
}

void psx_walk_after(void (*fn)(void *), const void *arg, int size) {
  int room = (size + 7) & ~7;
  Deferred *d;
  if (!s_pending) {
    fn((void *)arg);
    return;
  }
  if (s_defer_len + (int)sizeof *d + room > DEFER_BYTES) {
    psx_walk_join(); /* runs what's queued first, so the order holds */
    fn((void *)arg);
    return;
  }
  d = (Deferred *)(s_defer + s_defer_len);
  d->fn = fn;
  d->size = room;
  if (size > 0)
    memcpy(d + 1, arg, (size_t)size);
  s_defer_len += (int)sizeof *d + room;
  s_stats.deferred++;
}

void psx_walk_stats_take(PsxWalkStats *out) {
  *out = s_stats;
  memset(&s_stats, 0, sizeof s_stats);
}

/* The second arena: 16 MB-aligned like the first (24-bit links rebuild as
 * base | offset; the native build decodes them relative to the base, so any
 * address will do, D100). Its contents outside the copied ranges are never
 * read. */
static uintptr_t copy_arena(void) {
  void *p = NULL;
#if SPYRO_NATIVE_MEM
  p = malloc(PSX_MEM_SIZE);
  if (!p)
    return 0;
#else
#if defined(__linux__)
  uintptr_t at;
  for (at = PSX_RAM_BASE + 0x1000000u; at != 0; at += 0x1000000u) {
    p = mmap((void *)at, PSX_RAM_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != MAP_FAILED && (uintptr_t)p == at)
      break;
    if (p != MAP_FAILED)
      munmap(p, PSX_RAM_SIZE);
    p = NULL;
  }
#else
  p = memalign(0x1000000, PSX_RAM_SIZE);
#endif
  if (!p || ((uintptr_t)p & 0xFFFFFF) != 0)
    return 0;
#endif
  memset(p, 0, PSX_MEM_SIZE);
  return (uintptr_t)p;
}

int psx_walk_init(int mode, PsxWalkRangesFn ranges, const PsxWalkThread *thr) {
  if (mode == PSX_WALK_OFF || !ranges || (mode == PSX_WALK_THREAD && !thr))
    return -1;
  s_copy = copy_arena();
  if (!s_copy || psx_vcache_double() != 0)
    return -1;
  s_mode = mode;
  s_ranges_fn = ranges;
  if (thr)
    s_thr = *thr;
  psx_vcache_double();
  return 0;
}

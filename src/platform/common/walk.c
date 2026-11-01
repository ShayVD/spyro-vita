/* The OT walk from a copy of the frame's primitives (psx_walk.h, decisions
 * D69).
 *
 * DrawOTag hands its list over (g_psyxDrawOTagWalk) instead of walking it.
 * What the list can reach is copied first into a second 16 MB-aligned arena
 * at the same offsets: the draw's primitive span and the OT's terminator
 * (present.c port_present_prim_ranges; the game links its OT buckets
 * straight from primitive to primitive, order_table.c). The walk then
 * follows the copy (g_psyxNextPacket), so the game may write its next
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
 * The walk owns PsyCross's GPU state (the draw and display environments, the
 * splits, the vertex buffer) and, on the Vita, the render thread's command
 * ring, from the handover to the join. The game thread joins before it
 * touches any of it: PsyCross's calls and VSync gate (g_psyxGpuGate), save
 * states join, and present.c defers its few touches after a DrawOTag
 * (psx_walk_after). What the walk reads of the game's other state is taken
 * at the handover: g_psyxVramFrozen here, the vertex cache's table
 * (psx_vcache_frame_take), the display state the GL side sees (the Vita's
 * PsxWalkThread.submitted). */
#include "psx_walk.h"
#include "psx_arena.h"
#include "psx_vcache.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx/libgte.h"
#include "psx/libgpu.h"

#if defined(__linux__)
#include <sys/mman.h>
#else
#include <malloc.h>
#endif

/* PsyCross (gpu/PsyX_GPU.h) and the platform's vblank.c */
void ClearSplits(void);
void DrawAllSplits(void);
void ParsePrimitivesLinkedList(u_long *p, int singlePrimitive);
char PsyX_BeginScene(void);

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
  int frozen;     /* g_psyxVramFrozen at the handover */
  PsxVcacheFrame vc;
} s_job;

static int s_pending;          /* game thread: handed over, not joined */
static volatile int s_in_job;  /* psx_walk_run is running */
static unsigned char s_defer[DEFER_BYTES];
static int s_defer_len;
static PsxWalkStats s_stats;

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
  s_stats.foreign_at = 0x80000000u + a;
  return g_psx_arena_base + a;
}

static uintptr_t next_packet(uintptr_t packet) {
  return place(*(const uint32_t *)packet & 0xFFFFFFu);
}

static int vram_frozen(void) { return on_walker() ? s_job.frozen : g_psyxVramFrozen; }

static int walk_hook(u_long *p) {
  uint32_t head = (uint32_t)((uintptr_t)p - g_psx_arena_base);
  unsigned long long t0;
  int i;
  if ((uintptr_t)p < g_psx_arena_base || head >= PSX_RAM_SIZE) {
    s_stats.in_place++;
    return 0; /* PsyCross walks it now */
  }
  t0 = now();
  s_job.n = s_ranges_fn(s_job.r, MAX_RANGES);
  for (i = 0; i < s_job.n; i++) {
    PsxWalkRange *r = &s_job.r[i];
    if (r->hi > PSX_RAM_SIZE)
      r->hi = PSX_RAM_SIZE;
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
  s_job.frozen = g_psyxVramFrozen;
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
  s_in_job = 1;
  g_psyxNextPacket = next_packet;
  psx_vcache_frame_use(&s_job.vc);
  /* DrawOTag's own walk (LIBGPU.C) from here on */
  if (PsyX_BeginScene())
    ClearSplits();
  ParsePrimitivesLinkedList((u_long *)s_job.head, 0);
  DrawAllSplits();
  psx_vcache_frame_use(NULL);
  g_psyxNextPacket = NULL;
  s_in_job = 0;
  s_stats.walk_us += now() - t0;
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
    s_stats.wait_us += now() - t0;
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
 * base | offset). Its contents outside the copied ranges are never read. */
static uintptr_t copy_arena(void) {
  void *p = NULL;
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
  memset(p, 0, PSX_RAM_SIZE);
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
  g_psyxGpuGate = psx_walk_join;
  g_psyxVramFrozenGet = vram_frozen;
  g_psyxDrawOTagWalk = walk_hook;
  return 0;
}

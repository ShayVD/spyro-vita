/* Vertex cache (psx_vcache.h, decisions D39).
 *
 * RTPS/RTPT: PsyCross's GTE, and gte_inline.h's commands, call rtp_hook
 * after the integer projection; the hand renderers' native RTPS
 * (fast_render=2) call rtp_value_hook with the same values (D89). The hook
 * recomputes the same transform from the input registers without
 * the truncations (MAC >> 12, h/SZ3 by UNR division) and records the exact
 * screen position and depth under the SXY2 the game got. It only reads
 * (gteRegs, or the values it is given) and writes only this file's table.
 *
 * Spyro projects every vertex it draws through RTPS, including the midpoints
 * of subdivided near terrain (r_environment.s averages them in 3D, then
 * projects), and stores SXY2 into its prims unchanged, so the prim's
 * coordinates find the entry.
 *
 * Depth comes with a scale. The world renderer (r_environment.s) zeroes the
 * GTE's translation and projects camera-relative vertices, in units that
 * differ between its passes by powers of two: the same screen point comes
 * back with depths 4 or 16 times apart, and its near "precision" path
 * (all three coordinates within +-256) projects single vertices with their
 * coordinates times 16, so one polygon can mix scales. Two different
 * vertices can also land on the same integer pixel in one frame. So each
 * entry keeps up to four candidates (position and depth): every distinct
 * projection under its key, plus depth / 16 for a vertex that may be a
 * precision one (no translation, every coordinate a multiple of 16 below
 * 4096, but for the grids' borrow in y, #181). psx_vcache_poly then picks,
 * for each polygon, the candidates whose depths have the smallest spread:
 * one point at another scale is 4 or 16 times off, another point on the
 * same pixel is as far off as it is in depth, and a real polygon's depths
 * are much closer. A key with more than four is ambiguous (since #181,
 * resolved from the frame's order where it can be, below).
 *
 * Clamped projections (#178). When the GTE saturated IR1/IR2, SX/SY or
 * SZ3, or its division overflowed (H >= 2 SZ3), the SXY2 the game got isn't
 * the vertex's projection: at 16:9 the sides show large, close polygons
 * that cross the edge of the screen the 4:3 projection was made for. Until
 * #178 such a vertex was invalid and its polygons fell back (affine, the
 * clamped point pulled in). The hook checks the key against the GTE's own
 * answer from the same inputs, with its clamps, and records the exact,
 * unclamped position and depth under it, which the renderer draws even
 * without subpixel (VREF_CLAMP). Such a position can lie far outside the
 * screen: one that doesn't fit a candidate's int16 (1/256 pixel, +-128
 * pixels) goes to the table's far array (VC_FAR floats a frame), and the
 * candidate holds its index (dy FAR_MARK). Saturated keys collide (one
 * SX = 1023 for many points), so a clamped projection and another point
 * under one key make it ambiguous, never "multi" (T_MULTI's integer
 * position is the clamped one); since #192 the frame's order can name the
 * clamped point for a polygon (resolve, below). Still invalid: a depth of 0
 * or less, and a key that isn't the GTE's answer for these inputs.
 *
 * Ambiguous keys (#181). More than four different points under one key
 * (far mobys and far ground at a long draw distance) made it ambiguous,
 * never used: every polygon with such a vertex was drawn affine, and as
 * the camera moved, polygons switched between perspective and affine. The
 * table keeps the answer of each projection under a key with different
 * points (lc), so a polygon whose other vertices are found takes, for that
 * vertex, the depths of the projections under its key near theirs in the
 * frame's order (resolve), its position the key's integer one (#192: a
 * clamped point's own place, when the order names it alone).
 *
 * Sub-pixel positions are decided per key, not per polygon: a key that
 * is found gives every polygon using it the same point, and one that isn't
 * leaves it at the integer position everywhere, so polygons sharing a
 * vertex never crack apart. So the position is always candidate 0's, and
 * a key whose candidates are more than 1/8 pixel apart keeps the integer
 * position, whichever candidate's depth a polygon takes: with each
 * polygon's own pick, neighbours took points up to a pixel apart and the
 * waterfall showed a dashed crack.
 *
 * DrawOTag: each polygon's vertices are looked up (psx_vcache_poly) while the OT
 * is parsed, which happens inside DrawOTag, before the table moves on to the
 * next frame (done_hook). A frame is everything projected between two
 * DrawOTag calls; Spyro builds and draws its OT in the same frame
 * (gamestates/draw.c), and the presentation passes (D30) do too.
 *
 * When the OT is walked later or elsewhere (walk.c, D71), the frame's table
 * goes with the list (psx_vcache_frame_take) and the next frame records
 * into a second one: the two tables take turns.
 *
 * The presenter (D125-D129) draws every frame the logic draw doesn't, from
 * its own build of the renderers, on the game thread when the cache is on
 * (D126). Its projections come through its own hook (g_pres_rtp_value_hook)
 * into a table of its own, and from psx_vcache_pres_begin to its DrawOTag's
 * done hook its polygons are looked up there (#158). Before, it recorded
 * nothing and looked up the logic draw's table, between two logic frames:
 * its vertices were found in the update's projections or not at all, and
 * its done hook cut the logic frame's table short.
 *
 * On its own thread (the Vita's pres_thread=1, D126; #162) the presenter's
 * frame runs beside the logic draw, so nothing global may say whose frame
 * it is. The game thread opens it as above, then hands it over
 * (psx_vcache_pres_detach, as it places the job's buffer): from there a
 * polygon is the presenter's when its vertices aren't in the game's walk's
 * buffer (psx_gpu_game_walk, the only buffer the logic draw walks into),
 * and its answers go to a side array of the presenter's own (made with its
 * table, at its first frame, drawn inline), at the index its walk gives
 * (D152), which travels with its buffer to the render thread. The worker
 * ends the frame as its buffer closes (psx_vcache_pres_end). The
 * presenter's thread touches only its own table, its own side array, its
 * own counters and the presenter's VCacheStats; the logic draw never
 * reads the flag that says a presenter's frame is open.
 *
 * Vertices the renderer moved after projecting them (#183). The world
 * renderer's subdivision grids move their inner points on screen
 * (env_hand.c GridCorrect, the PS1's blend toward its affine look), so the
 * key such a point is drawn under was never recorded, and every quad using
 * it fell back to the PS1's drawing: they were most of the vertices with
 * no entry. A quad with one such vertex finds it again (derive): the grids
 * are projected in order, so the frame's order of projections (each
 * table's log) names the projection it was, and it is drawn there with
 * that depth, the place kept per key for the frame.
 *
 * The renderers' other screen-space vertices (#189): moby shadows' nudged
 * corners, the sky's portal cuts, glows' rims, the world's split midpoints
 * and Spyro's floor clip are made from projected points on screen, so they
 * have no projection either. Their renderers hand them over as they make
 * them (PSX_VC_DERIVE: an alias, a 2D cut at an outline, an offset, a 3D point between
 * two), and the cache keeps each one's exact place and depths, from its
 * sources' entries, in the table's hand-overs (hk). A vertex with no entry
 * is looked for there; one with an entry keeps it. 2D polygons (the HUD,
 * the title, fades, the border) stay as they were.
 *
 * Both at once (#197): a quad whose one vertex with no entry the order
 * names (derive) and whose other missing ones are ambiguous keys (resolve)
 * gets both: the projection derive names is one more of the polygon's
 * places in the order for resolve, and the depths resolve gives are the
 * third corner derive's plane needs. A vertex whose hand-over was dropped
 * because its source was ambiguous still has none.
 *
 * The cost (#159; D89 measured the cache at ~4.5 ms a vblank of the game
 * thread on hardware): every answer is as before, with less work. A record
 * finds its entry before the maths, and an ambiguous key needs none. Depth
 * ratios are compared without dividing unless the quotient's rounding
 * could decide the comparison (same_depth, under4, under3999). An entry is
 * 40 bytes, not 44. The g_vcache_cost counters count the divisions, probes
 * and table lines.
 *
 * A cheaper record (#205; D199 put the record at 85% of the logic draw's
 * non-raster time on the host): the hand renderers' native projections
 * call the record by name (ngte_vcache, pres_vcache_ngte) with their
 * registers by pointer and the sums and SZ3 the GTE emulation worked out,
 * so the record neither copies 15 registers a vertex for the value hook nor
 * multiplies them again (Pt), and a kept projection's registers are
 * compared with the last ones kept by their generation (gte_native.h
 * NgteProj gen), not word by word. The same sums, the same registers kept:
 * every answer is as before. */
#include "psx_vcache.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "psx/gtereg.h"
#include "psx/libgpu.h"
#include "../../game/gte_native.h" /* NgteProj, NgteRtp: the native projections' record (#205) */

void PsyX_Log(const char *fmt, ...); /* the platform's log (gpu_walk.c's too) */

/* 8192 entries (320 KB) stay in the A9's L2; a frame projects up to ~4100
 * distinct points (logs/run6-host-demo-vcache2), and recording stops at
 * 3/4 full rather than probe far */
#define VC_BITS 13
#define VC_SIZE (1u << VC_BITS)
#define VC_PROBE 32
#define VC_MAX (VC_SIZE * 3 / 4) /* entries per frame before recording stops */
#define VC_CANDS 4
#define VC_FAR 1024 /* exact positions out of a candidate's int16 range, per frame (#178) */
#define FAR_MARK INT16_MIN /* a candidate's dy: dx is the far array's index (fix8 never gives it) */
#define VREF_CAND_SHIFT 24 /* which of the entry's candidates (VREF bits 24-25) */
#define VC_DER 512   /* keys a frame's polygons derived (#183) */
#define VC_LOG 8192  /* projections a frame in the order they came (#183) */
#define VC_NOSEQ 0xFFFFu
#define LOG_LC 0x8000u /* a log place's flag: lc holds its answer (#181) */
#define LOG_CL 0x4000u /* ... a clamped projection's (#192: its own place, in lc or lfar) */
#define VC_LFAR 1024   /* those places out of a candidate's int16 range, per frame (#192) */
#define LOG_E(x) ((x) & 0x1FFFu) /* a log place's entry */

enum { E_VALID = 1, E_INVALID = 2, E_AMBIGUOUS = 3 };

/* One projection recorded under a key: its position (1/256 pixel, from the
 * pixel centre) and depth. */
typedef struct {
  int16_t dx, dy;
  float w;
} Cand;

/* An entry: its key, a tag, and the candidates. The tag holds the entry's
 * frame (24 bits: an entry from another frame is empty) and the rest of
 * what was its own 32-bit header before #159, so an entry is 40 bytes, not
 * 44: a probe and the first candidate fall in one 32-byte line more often. */
#define T_STATE(t) ((t) & 3u)      /* E_* (0: just made) */
#define T_N(t) (((t) >> 2) & 7u)   /* candidates */
#define T_MULTI 0x20u              /* candidates more than 1/8 pixel apart: different points */
#define T_SRC 0x40u                /* made by the presenter's projections (VC_PRES) */
#define T_CLAMP 0x80u              /* a projection under it was clamped by the GTE (#178) */
#define T_PEND 0x100u              /* projections waiting to be worked out (the presenter's, #193) */
#define T_GEN_SHIFT 9
#define T_GEN(t) ((t) >> T_GEN_SHIFT)
#define VC_GEN_END (1u << 23)      /* a table's frames count 1 to this, then from 1 again */

typedef struct {
  uint32_t key; /* SXY2: (SY << 16) | (SX & 0xFFFF) */
  uint32_t tag;
  Cand c[VC_CANDS];
} Entry;

/* A key with no entry whose position a polygon derived (#183): the same
 * for every polygon of the frame that uses it, so they don't crack apart.
 * dx, dy: pixels from the key's pixel centre; slot: the entry + 1 of the
 * projection it is, or 0 (kept at its key, the polygon's plane depth). */
typedef struct {
  uint32_t key, gen;
  float dx, dy;
  uint32_t slot;
} Der;

/* A point a renderer handed over (#189, psx_vcache.h PSX_VC_DERIVE): its
 * key, frame, place (pixels from the key's pixel centre) and depths (c[].w;
 * their dx, dy unused), the renderer (VCD_SHADOWS...), and dead: placed
 * differently by two hand-overs, so not used. */
#define VC_HK 1024 /* hand-overs a frame */
typedef struct {
  uint32_t key, gen;
  float dx, dy;
  uint8_t n, who, dead, placed; /* n 0: a place only (flat: a portal face's cut) */
  Cand c[VC_CANDS];
} Hk;

struct Lazy; /* the projections kept for later (#193, #179; below) */

typedef struct {
  Entry s[VC_SIZE];
  int stale; /* holds frames from before its count came round: cleared before it records */
  Hk hk[VC_HK]; /* the frame's hand-overs (#189), written by its renderers */
  /* the keys the walk of this table's frame derived (#183), written only
   * by that walk */
  Der der[VC_DER];
  /* The frame's projections in the order they came (#183): each one's
   * entry (log, nlog of them), written as they're recorded. The walk's
   * first derivation of the frame (idx_gen) indexes them: each entry's
   * last (last, VC_NOSEQ: none) and the one before it under the same
   * entry (prev). */
  unsigned nlog;
  uint32_t idx_gen;
  uint16_t log[VC_LOG], prev[VC_LOG], last[VC_SIZE];
  /* The answer of each projection a key with different points (or one
   * made ambiguous) had after its first (#181), at its log place, flagged
   * LOG_LC there: position as a candidate's, w > 0 its depth, w < 0 minus
   * the depth of a point that may be a precision one (-w / 16 is then a
   * depth too), 0 none (invalid, clamped or far). A place without the flag
   * is the entry's first projection or the first's point again: candidate
   * 0's position at one of the candidates' depths there. */
  Cand lc[VC_LOG];
  unsigned lc_line; /* the cost model's: the line of lc last written, + 1 (0: none this frame) */
  /* the exact positions (pixels from the pixel centre) of this table's
   * frame's candidates that don't fit an int16 (#178): only indexed by the
   * frame's own entries, so never cleared */
  float far[VC_FAR][2];
  /* #179 (last, so the cost model's lines of the rest stay where they
   * were): this frame's projections kept on their entries' lists (lazy, in
   * the store lz) or worked out at once; and the far array's positions this
   * frame (#178; was the Rec's: the walk now works out a frame's
   * projections while the next frame records into the other table) */
  int lazy, lz_tried;
  struct Lazy *lz;
  unsigned nfar;
  /* #192: the exact positions of the clamped projections lc holds that
   * don't fit an int16, one per projection (far is one per key's
   * candidate), nlfar of them this frame; only indexed by the frame's own
   * lc, so never cleared */
  unsigned nlfar;
  float lfar[VC_LFAR][2];
} Table;

/* A frame's table: the projections of one kind of frame (VC_LOGIC, VC_PRES)
 * record into their own, so each frame looks up only its own (#158). Each
 * on a line of its own (#193): the logic draw's count moves on core 0 while
 * the presenter's does on core 1. */
typedef struct {
  Table *tab;     /* the table recording */
  uint32_t gen;   /* its frame */
  unsigned count; /* entries this frame */
} __attribute__((aligned(64))) Rec;

static Table s_tab0 __attribute__((aligned(64)));
static VCacheVert *s_pres_side; /* the presenter's answers while it runs on its own thread (#162) */
static Table *s_tab1;                  /* the logic draw's other table (D71) */
static Rec s_rec[2] = {{&s_tab0, 1, 0}, {NULL, 1, 0}}; /* the presenter's: psx_vcache_pres_begin */
static const Table *s_rtab;            /* the walk's table and frame, NULL: this one */
static uint32_t s_rgen;

/* a table on the heap, line-aligned like s_tab0 (never freed) */
static Table *table_new(void) {
  char *const p = calloc(1, sizeof(Table) + 63);
  return p ? (Table *)(((uintptr_t)p + 63) & ~(uintptr_t)63) : NULL;
}

static void table_clear(Table *t) {
  memset(t->s, 0, sizeof t->s);
  memset(t->der, 0, sizeof t->der);
  memset(t->hk, 0, sizeof t->hk);
  t->stale = 0;
}

VCacheVert g_vcache_side[MAX_VERTEX_BUFFER_SIZE + 6];

static int s_pres; /* the presenter's frame, inline: its DrawOTag hasn't returned (psx_vcache_pres_begin) */
/* the presenter's frame on its own thread (#162): from psx_vcache_pres_detach
 * (the game thread, before it posts the job) to psx_vcache_pres_end (the
 * worker, at its buffer's close); read only for vertices outside the game's
 * walk's buffer */
static int s_pres_async;

VCacheStats g_vcache_stats[2];
/* Work since the start, never reset, for the Vita's work counters (issue
 * #160, D165; src/platform/vita/perfcount.c): projections recorded,
 * polygons looked up, their vertices, and those not found. g_vcache_work
 * is the game thread's (and its walk's: the logic draw, and the presenter
 * inline), g_vcache_pres_work the presenter's thread's (#162): one writer
 * each, plain adds. */
VCacheCounters g_vcache_ctr[2]; /* each thread's on lines of its own (#193) */
/* What that work cost (issue #159), the same way, counts that don't depend
 * on timing: float divisions, the table's slots a record's or a lookup's
 * probe examined, and the 32-byte lines of the table each touched (a model:
 * the lines its accesses fall in, from a line-aligned table). */
#define COST(work) ((work) == g_vcache_work ? g_vcache_cost : g_vcache_pres_cost)

/* Where a projection's counts go (#179): its thread's work and cost
 * counters, its kind of frame's stats, and (the logic draw's) its side of
 * g_vcache_rec, NULL for the presenter's.
 *
 * The logic draw's records count into the game thread's block (s_acc[0])
 * and the maths its walk works out for them into the walk's (s_acc[1]),
 * each added to g_vcache_ctr[0] and g_vcache_stats[VC_LOGIC] once a frame
 * by its own thread (done_hook; the walk's end, psx_vcache_frame_use), with
 * atomic adds. Until #179 every record added to the lines the walk's
 * lookups add to, from the other core on the Vita (D71, D196): those lines
 * moved between the cores for each projection. The walk's lookups still add
 * to their own words (polygons, vertices, misses...) directly: no word has a
 * plain add on one thread and any add on another. */
typedef struct {
  unsigned *work, *cost;
  VCacheStats *st;
  unsigned *rec;
} Sink;
typedef struct {
  unsigned work[VCW_N], cost[VCC_N], rec[VCR_N];
  VCacheStats st;
} __attribute__((aligned(64))) Acc;
static Acc s_acc[2]; /* [0] the game thread's (the logic draw's records), [1] its walk's */
unsigned g_vcache_rec[2][VCR_N];
#define SINK_ACC(side) {s_acc[side].work, s_acc[side].cost, &s_acc[side].st, s_acc[side].rec}

static void acc_add(unsigned *to, unsigned *from, size_t n) {
  size_t i;
  for (i = 0; i < n; i++)
    if (from[i]) {
      __atomic_fetch_add(&to[i], from[i], __ATOMIC_RELAXED);
      from[i] = 0;
    }
}

/* side's block into the counters (by its own thread) */
static void acc_publish(int side) {
  Acc *const a = &s_acc[side];
  unsigned *const r = g_vcache_rec[side];
  a->rec[VCR_DIV] = a->cost[VCC_REC_DIV];
  a->rec[VCR_PROBES] = a->cost[VCC_REC_PROBES];
  a->rec[VCR_LINES] = a->cost[VCC_REC_LINES];
  acc_add(r, a->rec, VCR_N);
  acc_add(g_vcache_ctr[0].work, a->work, VCW_N);
  acc_add(g_vcache_ctr[0].cost, a->cost, VCC_N);
  acc_add((unsigned *)&g_vcache_stats[VC_LOGIC], (unsigned *)&a->st,
          sizeof a->st / sizeof(unsigned));
}

/* The distinct lines one record or lookup touches (it reads upwards). */
typedef struct {
  uint32_t last;
  unsigned n;
} Touch;
static inline void touch(Touch *t, size_t at, size_t len) {
  uint32_t l = (uint32_t)(at / 32), h = (uint32_t)((at + len - 1) / 32);
  for (; l <= h; l++)
    if (l != t->last) {
      t->last = l;
      t->n++;
    }
}
#define TOUCH0 {0xFFFFFFFFu, 0}

void psx_vcache_stats_reset(void) { memset(g_vcache_stats, 0, sizeof g_vcache_stats); }

/* the cost line, since the last one */
static void cost_line(void) {
  static unsigned last[2][VCC_N], wlast[2][4];
  unsigned d[2][VCC_N], w[2][4], t, i;
  for (t = 0; t < 2; t++) {
    for (i = 0; i < VCC_N; i++) {
      const unsigned v = (t ? g_vcache_pres_cost : g_vcache_cost)[i];
      d[t][i] = v - last[t][i];
      last[t][i] = v;
    }
    for (i = 0; i < 4; i++) {
      const unsigned v = (t ? g_vcache_pres_work : g_vcache_work)[i];
      w[t][i] = v - wlast[t][i];
      wlast[t][i] = v;
    }
  }
  for (t = 0; t < 2; t++) {
    const unsigned p = w[t][0], v = w[t][2];
#define PER(x, n) (x), (unsigned)((n) ? (x) / (n) : 0), (unsigned)((n) ? 100ull * (x) / (n) % 100 : 0)
    PsyX_Log("vcache cost%s: %u projected: %u divisions (%u.%02u each), %u probes (%u.%02u), "
             "%u table lines (%u.%02u); %u vertices looked up: %u divisions (%u.%02u each), "
             "%u probes (%u.%02u), %u table lines (%u.%02u)",
             t ? " (the presenter's thread)" : " (the game thread)", p,
             PER(d[t][VCC_REC_DIV], p), PER(d[t][VCC_REC_PROBES], p),
             PER(d[t][VCC_REC_LINES], p), v, PER(d[t][VCC_LOOK_DIV], v),
             PER(d[t][VCC_LOOK_PROBES], v), PER(d[t][VCC_LOOK_LINES], v));
#undef PER
  }
  { /* #179: the logic draw's records, the game thread's work and its walk's */
    static unsigned rl[2][VCR_N];
    unsigned r[2][VCR_N];
    for (t = 0; t < 2; t++)
      for (i = 0; i < VCR_N; i++) {
        const unsigned v = __atomic_load_n(&g_vcache_rec[t][i], __ATOMIC_RELAXED);
        r[t][i] = v - rl[t][i];
        rl[t][i] = v;
      }
    PsyX_Log("vcache records: the game thread kept %u projections for later and worked out %u at "
             "once, %u in all (%u divisions, %u probes, %u table lines, %u lines of the store); its "
             "walk worked out %u (%u divisions, %u table lines); %u never needed",
             r[0][VCR_KEPT], r[0][VCR_ONCE], r[0][VCR_WORKED], r[0][VCR_DIV], r[0][VCR_PROBES],
             r[0][VCR_LINES], r[0][VCR_STORE], r[1][VCR_WORKED], r[1][VCR_DIV], r[1][VCR_LINES],
             r[0][VCR_KEPT] + r[0][VCR_ONCE] - r[0][VCR_WORKED] - r[1][VCR_WORKED]);
  }
}

/* the two kinds' counters added (peak: the larger), and the report's lines */
static void sum_report(const VCacheStats k[2], VCacheStats *out) {
  const unsigned *a = (const unsigned *)&k[0], *b = (const unsigned *)&k[1];
  unsigned *o = (unsigned *)out;
  size_t i;
  for (i = 0; i < sizeof *out / sizeof(unsigned); i++)
    o[i] = a[i] + b[i];
  out->peak = k[0].peak > k[1].peak ? k[0].peak : k[1].peak;
  cost_line();
  /* #183: the quads' vertices with no entry found again from their polygon */
  PsyX_Log("vcache derived: %u vertices with no entry drawn with their polygon's depth (%u at the "
           "projection recorded before the renderer moved them, %u at their key); %u left with no "
           "entry",
           out->derived, out->der_snap, out->derived - out->der_snap, out->miss_none);
  /* #181: ambiguous keys' vertices found again from the frame's order */
  PsyX_Log("vcache resolved: %u vertices with an ambiguous key drawn with a depth from its "
           "projections near their polygon's others (%u where the order left one point, %u with "
           "their quad's vertex with no entry derived); %u left ambiguous: %u with a vertex with "
           "no entry, %u with an invalid vertex, %u with a place-only hand-over, %u an invalid "
           "projection near, %u a clamped point among several, %u depths 4x apart, %u log full",
           out->resolved, out->res_order, out->res_der, out->miss_ambiguous,
           out->res_why[RW_NONE], out->res_why[RW_INVALID], out->res_why[RW_FLAT],
           out->res_why[RW_INV_NEAR], out->res_why[RW_CLAMP_SEV], out->res_why[RW_SPREAD],
           out->res_why[RW_FULL]);
  /* #189: the renderers' hand-overs */
  PsyX_Log("vcache hooked: %u vertices with no entry found from their renderer's hand-over "
           "(shadows %u, sky clip %u, glows %u, world splits %u, Spyro's clip %u); %u hand-overs, "
           "%u kept, %u with a source not usable, %u placed differently, %u not kept (full)",
           out->hooked[VCD_SHADOWS] + out->hooked[VCD_SKY_CLIP] + out->hooked[VCD_GLOWS] +
               out->hooked[VCD_ENV_SPLIT] + out->hooked[VCD_PETE_CLIP],
           out->hooked[VCD_SHADOWS], out->hooked[VCD_SKY_CLIP], out->hooked[VCD_GLOWS],
           out->hooked[VCD_ENV_SPLIT], out->hooked[VCD_PETE_CLIP], out->hk_calls, out->hk_kept,
           out->hk_nosrc, out->hk_conflict, out->hk_full);
}

/* (the host's, on the game thread with no other: the counts of the frame
 * and walk in progress too, #179) */
void psx_vcache_stats_sum(VCacheStats *out) {
  acc_publish(0);
  acc_publish(1);
  sum_report(g_vcache_stats, out);
}

/* #181: the counters are never written by the report's thread (only the
 * peaks, below), so an add on the walk's thread can't undo a reset */
void psx_vcache_stats_take(VCacheStats win[2], VCacheStats *sum) {
  static VCacheStats base[2];
  size_t k, i;
  for (k = 0; k < 2; k++) {
    const volatile unsigned *c = (const volatile unsigned *)&g_vcache_stats[k];
    unsigned *b = (unsigned *)&base[k], *w = (unsigned *)&win[k];
    for (i = 0; i < sizeof win[k] / sizeof(unsigned); i++) {
      const unsigned v = c[i];
      w[i] = v - b[i];
      b[i] = v;
    }
    /* the largest in the window: started again (a frame's own thread may
     * store its larger one meanwhile; then it counts in the next) */
    win[k].peak = g_vcache_stats[k].peak;
    g_vcache_stats[k].peak = 0;
    base[k].peak = 0;
  }
  sum_report(win, sum);
}

static inline uint32_t hash(uint32_t key) { return (key * 0x9E3779B1u) >> (32 - VC_BITS); }

/* The entry for key in r's frame, or a free one for it (gen set, state 0),
 * or -1 when the probe is exhausted. */
static int find(Rec *r, int kind, uint32_t key, int add, unsigned *probes, Touch *t) {
  Entry *const s = r->tab->s;
  const uint32_t gen = r->gen;
  uint32_t i = hash(key), n;
  for (n = 0; n < VC_PROBE; n++, i = (i + 1) & (VC_SIZE - 1)) {
    ++*probes;
    touch(t, i * sizeof(Entry), 8); /* key, tag */
    if (T_GEN(s[i].tag) != gen) {
      if (!add || r->count >= VC_MAX)
        return -1;
      s[i].key = key;
      s[i].tag = gen << T_GEN_SHIFT | (kind == VC_PRES ? T_SRC : 0);
      r->count++;
      return (int)i;
    }
    if (s[i].key == key)
      return (int)i;
  }
  return -1;
}

/* psx_vcache_poly's lookup: the entry for key in the frame being walked
 * (pres: the presenter's own, in its frame), and its table */
static int look(int pres, uint32_t key, unsigned *probes, Touch *t, const Table **tab) {
  uint32_t i = hash(key), n;
  if (pres || !s_rtab) {
    Rec *const r = &s_rec[pres ? VC_PRES : VC_LOGIC];
    *tab = r->tab;
    return find(r, pres ? VC_PRES : VC_LOGIC, key, 0, probes, t);
  }
  *tab = s_rtab;
  for (n = 0; n < VC_PROBE; n++, i = (i + 1) & (VC_SIZE - 1)) {
    const Entry *const e = &s_rtab->s[i];
    ++*probes;
    touch(t, i * sizeof(Entry), 8); /* key, tag */
    if (T_GEN(e->tag) != s_rgen)
      return -1;
    if (e->key == key)
      return (int)i;
  }
  return -1;
}

/* x as a float: the host's x87 (i686, no SSE) can keep an expression at
 * long double precision until it's stored, and the answers were float
 * since they went to record() as arguments; elsewhere a float is a float */
#if defined(__i386__) && !defined(__SSE_MATH__)
static inline float flt(float x) {
  volatile float v = x;
  return v;
}
#else
#define flt(x) (x)
#endif

static inline float ratio(float a, float b) { return a > b ? a / b : b / a; }

static inline int16_t fix8(float d) {
  float f = d * 256.0f;
  return (int16_t)(f > 32767.0f ? 32767 : f < -32767.0f ? -32767 : f);
}

/* ratio(a, b) <= 1.02f, the same answer without the division unless the
 * ratio is within 1e-4 of the bound: outside that, how the quotient would
 * be rounded (float, or the host's x87) can't change the comparison. The
 * depths are > 0. */
static inline int same_depth(float a, float b, unsigned *div) {
  const float hi = a > b ? a : b, lo = a > b ? b : a;
  if (hi <= lo * 1.0199f)
    return 1;
  if (hi >= lo * 1.0201f)
    return 0;
  ++*div;
  return ratio(a, b) <= 1.02f;
}

/* candidate c's position in table t, pixels from the pixel centre */
static inline void cand_pos(const Table *t, const Cand *c, float *x, float *y) {
  if (c->dy == FAR_MARK) {
    *x = t->far[(uint16_t)c->dx][0];
    *y = t->far[(uint16_t)c->dx][1];
  } else {
    *x = c->dx * (1.0f / 256);
    *y = c->dy * (1.0f / 256);
  }
}

/* One more projection under slot s's key in rec's table: 0 = kept (or
 * already there), -1 = no room, or the key can't be used (#178: a clamped
 * projection and another point under it). The same point at the same depth
 * is kept once. Its position: dx, dy (1/256 pixel), or dy FAR_MARK and the
 * exact px, py (pixels), which go to the far array when kept. */
static int add_cand(Table *tb, Entry *s, int16_t dx, int16_t dy, float px, float py, float w,
                    int clamped, unsigned *cost, Touch *t) {
  Cand *const c = s->c;
  const unsigned n = T_N(s->tag);
  const int far = dy == FAR_MARK;
  int reuse = -1; /* a far candidate at this position: its index */
  unsigned j;
  if (clamped && (s->tag & T_MULTI))
    return -1; /* different points already share the key */
  for (j = 0; j < n; j++) {
    int differ;
    if (!far && c[j].dy != FAR_MARK) {
      int ddx = c[j].dx - dx, ddy = c[j].dy - dy;
      differ = ddx > 32 || ddx < -32 || ddy > 32 || ddy < -32;
    } else {
      float ax, ay;
      cand_pos(tb, &c[j], &ax, &ay);
      if (c[j].dy == FAR_MARK)
        touch(t, offsetof(Table, far) + (size_t)(uint16_t)c[j].dx * sizeof tb->far[0],
              sizeof tb->far[0]);
      differ = !(ax - px <= 0.125f && ax - px >= -0.125f && ay - py <= 0.125f && ay - py >= -0.125f);
      if (!differ && far && c[j].dy == FAR_MARK)
        reuse = (uint16_t)c[j].dx;
    }
    if (differ) { /* more than 1/8 pixel apart: not one point */
      if (clamped || (s->tag & T_CLAMP))
        return -1; /* a clamped point's key: the prims can't be told apart (#178) */
      s->tag |= T_MULTI;
    } else if (same_depth(w, c[j].w, &cost[VCC_REC_DIV])) {
      if (clamped)
        s->tag |= T_CLAMP;
      return 0;
    }
  }
  if (n == VC_CANDS)
    return -1;
  if (far && reuse < 0) {
    if (tb->nfar >= VC_FAR)
      return -1;
    reuse = (int)tb->nfar++;
    tb->far[reuse][0] = px;
    tb->far[reuse][1] = py;
    touch(t, offsetof(Table, far) + (size_t)reuse * sizeof tb->far[0], sizeof tb->far[0]);
  }
  c[n].dx = far ? (int16_t)reuse : dx;
  c[n].dy = dy;
  c[n].w = w;
  s->tag += 1u << 2; /* n + 1 */
  if (clamped)
    s->tag |= T_CLAMP;
  return 0;
}

static inline void set_state(Entry *s, unsigned state) { s->tag = (s->tag & ~3u) | state; }

/* A projection's own answer at its log place q (Table lc, #181). A clamped
 * one's too (#192), flagged LOG_CL: its exact position as a candidate's, or
 * dy FAR_MARK and dx its index in lfar (px, py), so resolve can tell it
 * from the key's other points by the frame's order (w 0 when lfar is
 * full). */
static inline void log_answer(Table *tb, int q, unsigned state, int clamped, int16_t fx, int16_t fy,
                              float px, float py, float w, float w_alt, unsigned *cost) {
  Cand *const l = &tb->lc[q];
  tb->log[q] |= LOG_LC;
  if (state == E_VALID && (fy != FAR_MARK || tb->nlfar < VC_LFAR)) {
    if (clamped)
      tb->log[q] |= LOG_CL;
    if (fy == FAR_MARK) {
      tb->lfar[tb->nlfar][0] = px;
      tb->lfar[tb->nlfar][1] = py;
      fx = (int16_t)tb->nlfar++;
      cost[VCC_REC_LINES]++; /* a model: a line each */
    }
    l->dx = fx;
    l->dy = fy;
    l->w = w_alt > 0 ? -w : w;
  } else {
    l->dx = l->dy = 0;
    l->w = 0;
  }
  if ((unsigned)q / 4 + 1 != tb->lc_line) { /* a model: written in order, 4 a line */
    tb->lc_line = (unsigned)q / 4 + 1;
    cost[VCC_REC_LINES]++;
  }
}

/* PsyX_GTE.cpp's VX/VY/VZ for idx 0-2 */
#define V_X(n) (gteRegs.CP2D.p[(n) << 1].sw.l)
#define V_Y(n) (gteRegs.CP2D.p[(n) << 1].sw.h)
#define V_Z(n) (gteRegs.CP2D.p[((n) << 1) + 1].sw.l)

/* One projection: the vertex, what the GTE gave (key = SXY2, sz = SZ3) and
 * the registers it read, proj = R11-R33, TRX-TRZ, OFX, OFY, H (PROJ_*). */
enum { PROJ_TR = 9, PROJ_OFX = 12, PROJ_OFY = 13, PROJ_H = 14, PROJ_N = 15 };

/* What a projection's maths reads (#205): the 44-bit sums the GTE shifts
 * into MAC1-3, the vertex (for the x16 precision test), whether there is a
 * translation, and OFX, OFY and H. A native projection's sums are the GTE
 * emulation's own (gte_native.h NgteRtp m1-m3), with its registers read
 * where they are (pt_ngte); until #205 its 15 registers were copied for the
 * value hook and its sums worked out again here. The register file's, the
 * sky's and a kept projection's are summed from their words (pt_words), in
 * the GTE's own expression: the same sums. */
typedef struct {
  int64_t mx, my, mz;
  int vx, vy, vz;
  int tr; /* the translation isn't 0 */
  int ofx, ofy, h;
} Pt;

static inline void pt_words(Pt *a, const int *proj, int vx, int vy, int vz) {
  const int *rm = proj, *tr = proj + PROJ_TR;
  a->mx = ((int64_t)tr[0] << 12) + (int64_t)(rm[0] * vx) + (rm[1] * vy) + (rm[2] * vz);
  a->my = ((int64_t)tr[1] << 12) + (int64_t)(rm[3] * vx) + (rm[4] * vy) + (rm[5] * vz);
  a->mz = ((int64_t)tr[2] << 12) + (int64_t)(rm[6] * vx) + (rm[7] * vy) + (rm[8] * vz);
  a->vx = vx, a->vy = vy, a->vz = vz;
  a->tr = tr[0] || tr[1] || tr[2];
  a->ofx = proj[PROJ_OFX], a->ofy = proj[PROJ_OFY], a->h = proj[PROJ_H];
}

static inline void pt_ngte(Pt *a, const NgteProj *p, const NgteRtp *o, int vx, int vy, int vz) {
  a->mx = o->m1, a->my = o->m2, a->mz = o->m3;
  a->vx = vx, a->vy = vy, a->vz = vz;
  a->tr = p->trx || p->try_ || p->trz;
  a->ofx = p->ofx, a->ofy = p->ofy, a->h = p->h;
}

/* The projection's work once its entry (slot i of table tb) and its place
 * in the frame's order (q, -1: none) are known: the exact point, and the
 * entry's candidates and state (rtp_point, or lz_flush, #193, #179). k: where
 * its counts go. tp: the lines it touches, which the caller adds to the
 * cost. */
static void rtp_apply(Table *tb, const Sink *k, const Pt *a, uint32_t key, int sz, int sf, int i,
                      int q, Touch *tp) {
  unsigned *const cost = k->cost, *const work = k->work;
  VCacheStats *const st = k->st;
  const int sx = (short)(key & 0xFFFF), sy = (short)(key >> 16), h = a->h;
  const int vx = a->vx, vy = a->vy, vz = a->vz;
  const int64_t mx = a->mx, my = a->my, mz = a->mz; /* the 44-bit sums (Pt) */
  float x, y, z, r, iz, isz, ox, oy, ex, ey, gx, gy, qd, px = 0, py = 0, w = 0, w_alt = 0;
  Entry *const s = &tb->s[i];
  const unsigned prev = T_STATE(s->tag);
  unsigned state = E_INVALID;
  int16_t fx = 0, fy = 0;
  int ix, iy, clamped = 0, amb;

  /* A key already ambiguous stays so, but since #181 its projections are
   * still worked out, for the lookup's log of answers (its counters are
   * left as they were: only the answer is kept). */
  amb = prev == E_AMBIGUOUS;
  if (k->rec)
    k->rec[VCR_WORKED]++;
  /* Spyro's renderers always set sf. A depth of 0 or less (the GTE's SZ3
   * or the exact one) has no projection. */
  if (!sf || sz <= 0) {
    st->inv_depth += !amb;
    goto done;
  }
  /* (the 12 low bits as an int: the same value, and no 64-bit
   * conversion, a library call on the A9) */
  x = (float)(int32_t)(mx >> 12) + (float)(int32_t)(mx & 0xFFF) * (1.0f / 4096);
  y = (float)(int32_t)(my >> 12) + (float)(int32_t)(my & 0xFFF) * (1.0f / 4096);
  z = (float)(int32_t)(mz >> 12) + (float)(int32_t)(mz & 0xFFF) * (1.0f / 4096);
  ox = (float)a->ofx * (1.0f / 65536);
  oy = (float)a->ofy * (1.0f / 65536);
  if (z <= 0) {
    st->inv_depth += !amb;
    goto done;
  }
  r = (float)h / (z * sz); /* one division for h / sz and h / z */
  cost[VCC_REC_DIV]++;
  isz = r * z;
  iz = r * sz;
  /* The GTE's own answer from the same inputs (truncated x, y and SZ3) and
   * its clamps (#178): IR1/IR2 saturated to 16 bits, the quotient to
   * 0x1FFFF (divide overflow, H >= 2 SZ3), SX/SY to 11 bits. Off by more
   * than its rounding: the key isn't this projection's. */
  ix = (mx >> 12) > 0x7FFF ? 0x7FFF : (mx >> 12) < -0x8000 ? -0x8000 : (int)(mx >> 12);
  iy = (my >> 12) > 0x7FFF ? 0x7FFF : (my >> 12) < -0x8000 ? -0x8000 : (int)(my >> 12);
  qd = h >= 2 * sz ? (float)0x1FFFF * (1.0f / 65536) : isz;
  gx = ox + (float)ix * qd;
  gy = oy + (float)iy * qd;
  clamped = ix != (mx >> 12) || iy != (my >> 12) || h >= 2 * sz || (mz >> 12) > 0xFFFF ||
            gx < -1024 || gx >= 1024 || gy < -1024 || gy >= 1024;
  ex = (gx < -1024 ? -1024 : gx > 1023 ? 1023 : gx) - sx;
  ey = (gy < -1024 ? -1024 : gy > 1023 ? 1023 : gy) - sy;
  if (ex < -1.5f || ex > 2.5f || ey < -1.5f || ey > 2.5f) {
    st->inv_key += !amb;
    goto done;
  }
  /* Maybe r_environment's x16 precision path: no translation, every
   * coordinate times 16 within +-4096. The grids' (env_hand.c GridVertex)
   * put x << 4 and y << 20 in one word, so a negative x borrows 1 from y:
   * there VY is 16 y - 1 (#181: until then those points had no second
   * depth, and their big close quads, mixing scales, were drawn affine). */
  if (!a->tr && !((vx | vz) & 15) &&
      ((vy & 15) == 0 || (vx < 0 && (vy & 15) == 15)) && vx >= -4096 && vx < 4096 &&
      vy >= -4097 && vy < 4096 && vz >= -4096 && vz < 4096)
    w_alt = flt(z * (1.0f / 16));
  /* The exact point, relative to the pixel centre the PS1 draws the vertex
   * at: unbiased against vertices drawn at their integer position. In 1/256
   * pixel when it fits an int16, else in the far array (#178). */
  px = flt(ox + x * iz - sx - 0.5f);
  py = flt(oy + y * iz - sy - 0.5f);
  if (px * 256.0f > 32767.0f || px * 256.0f < -32767.0f || py * 256.0f > 32767.0f ||
      py * 256.0f < -32767.0f) {
    fy = FAR_MARK;
    clamped = 1; /* the key is that far from the point: the GTE's answer was clamped */
  } else {
    fx = fix8(px);
    fy = fix8(py);
  }
  w = flt(z);
  state = E_VALID;
done:
  if (amb) { /* #181: only its answer */
    if (q >= 0)
      log_answer(tb, q, state, clamped, fx, fy, px, py, w, w_alt, cost);
    return;
  }
  if (state == E_INVALID)
    work[VCW_INVALID]++;
  else if (clamped) {
    work[VCW_CLAMPED]++;
    st->clamped++;
  }
  if (prev == 0) {
    set_state(s, state);
    if (state == E_INVALID) {
      st->invalid++;
      return;
    }
  } else if (prev != state || state != E_VALID) {
    /* a clamped projection and another one under the same key: the prims
     * that use it can't be told apart */
    if (!(prev == E_INVALID && state == E_INVALID)) {
      st->ambiguous++;
      set_state(s, E_AMBIGUOUS);
    }
    if (q >= 0) /* #181 */
      log_answer(tb, q, state, clamped, fx, fy, px, py, w, w_alt, cost);
    return;
  }
  /* Another projection under this key: the same point again (shared by
   * several polygons), the same point at another scale, or a different
   * point on the same pixel. Each is a candidate; each polygon picks. */
  touch(tp, (size_t)i * sizeof(Entry) + offsetof(Entry, c),
        sizeof(Cand) * (T_N(s->tag) < VC_CANDS ? T_N(s->tag) + 1u : VC_CANDS));
  if (add_cand(tb, s, fx, fy, px, py, w, clamped, cost, tp) ||
      (w_alt > 0 && add_cand(tb, s, fx, fy, px, py, w_alt, clamped, cost, tp))) {
    st->ambiguous++;
    set_state(s, E_AMBIGUOUS);
  }
  /* #181: a key with different points (or none usable) keeps each
   * projection's own answer after its first; under any other, every
   * projection is the first's point at one of the candidates' depths */
  if (prev != 0 && q >= 0 && (T_STATE(s->tag) != E_VALID || (s->tag & T_MULTI)))
    log_answer(tb, q, state, clamped, fx, fy, px, py, w, w_alt, cost);
}

/* Projections worked out when they're needed (#193, #179).
 *
 * On Toasty's heavy opening the presenter's thread had no time left at 30 Hz
 * with in-between frames (D179's AP10). Of its projections, a quarter to a
 * third are under keys none of its polygons looks up (backfaces, culled
 * sectors, points a renderer projects and never draws): on Vita3K, 25-34%
 * a 300-vblank window in Artisans (wobble60), 26-35% in Toasty's heavy
 * opening. Their maths and their entries' updates were the presenter's
 * thread's work for nothing.
 *
 * So the presenter's frame records each projection's entry and place in
 * the frame's order at once, as before (the table's count and VC_MAX, the
 * log that derive and resolve read), and keeps its inputs on the entry's
 * list (T_PEND). Whatever reads the entry first works out its list, in the
 * order the projections came: a polygon's lookup, a hand-over's source
 * (hk_src) and a derived point's projection (derive). Every key's
 * candidates and state come out of the same projections in the same order,
 * so every answer is the one the projections would have given at once. The
 * far array's slots go in another order: the places they hold are the same.
 * The counters of what the projections were (invalid, ambiguous, clamped)
 * count only those worked out.
 *
 * The logic draw's frames too (#179), with 60 Hz logic: there the game
 * thread is the bottleneck (D196) and the vertex cache's maths was most of
 * the hand renderers' cost with the fix on (D177, ~0.094 ms per KB of
 * primitives on hardware, D196). The game thread now only finds the entry,
 * logs the place and keeps the inputs; the walk (the parse thread on the
 * Vita, core 1, D71) works them out as it looks the polygons up, and a
 * quarter to a third never are. Each logic table has its own store, so the
 * walk works out frame N's in its table while the game thread records frame
 * N + 1 into the other (D71's turns): each table, its store and its far
 * array have one thread at a time, the handover and the join between them.
 * A projection is kept only while the last frame's walk is done: while
 * it runs, the walk is the thread short of time (Vita3K's owner60 at
 * vblank 6,900, everything kept: the parse thread 3.4 -> 7.5 ms a vblank
 * and the game thread waiting 5.7 ms for it), so the game thread works it
 * out itself. The split finds its own balance: the walk takes what it has
 * time for. Which ones are kept never changes an answer.
 * While the presenter draws in-between frames (30 Hz logic with 60 fps,
 * D164, D167) the logic draw's frames work theirs out at once, as before:
 * there the walk shares core 1 with the presenter's thread, which was the
 * one short of time (D186), and the game thread waits for the walk at the
 * presenter's records_begin anyway (D196). psx_vcache_set_lazy_logic.
 *
 * A projection is worked out at once, after its entry's list, when the
 * frame's order or its table's store is full. psx_vcache_set_lazy(0) turns
 * every deferral off. */
typedef struct {
  int16_t vx, vy, vz; /* the GTE's V0: 16 bits */
  uint16_t sz;        /* SZ3: 16 bits */
  uint16_t q;         /* its place in the frame's order */
  uint16_t next;      /* the entry's one before, + 1 (0: its first; #179: the list is kept
                         newest first and turned round as it's worked out) */
  uint16_t proj;      /* its registers (Lazy proj), LZ_SF: sf set */
  uint16_t pad;
} LzRec; /* 16 bytes */
#define LZ_SF 0x8000u
#define LZ_PROJ 1024 /* different registers a frame: models, sectors, the sky */
/* a kept projection's registers: their words (PROJ_*) and, last, their
 * generation (gte_native.h NgteProj gen, #205; 0: none), 64 bytes */
#define PROJ_GEN PROJ_N
#define PROJ_ROW (PROJ_N + 1)
typedef struct Lazy {
  LzRec rec[VC_LOG];
  unsigned n, nproj;
  int proj[LZ_PROJ][PROJ_ROW];
  uint16_t head[VC_SIZE]; /* each pending entry's list (its newest), + 1 */
} Lazy;
static int s_lazy = 1;       /* psx_vcache_set_lazy */
static int s_lazy_logic = 1; /* psx_vcache_set_lazy_logic */
/* the logic draw's frames since the presenter's last (#179), up to 255 */
static unsigned s_since_pres = 255;
/* the logic draw's walk is running on its own thread (#179): set and
 * cleared by the walk (psx_vcache_frame_use), read by the game thread's
 * records; a line of its own */
static struct {
  int busy;
} __attribute__((aligned(64))) s_walk;
#define LZ_QUIET 2 /* ... more than this: no in-between frames */

void psx_vcache_set_lazy(int on) { s_lazy = on != 0; }
void psx_vcache_set_lazy_logic(int mode) { s_lazy_logic = mode < 0 ? 0 : mode > 2 ? 2 : mode; }

/* tb's next frame: kept for later or worked out at once (each frame
 * decided as it starts) */
static void lz_next(Table *tb, int lazy) {
  if (lazy && !tb->lz && !tb->lz_tried) { /* its store, once */
    tb->lz_tried = 1;
    tb->lz = malloc(sizeof *tb->lz);
  }
  tb->lazy = lazy && tb->lz;
  if (tb->lz)
    tb->lz->n = tb->lz->nproj = 0;
}

/* the registers the last projection kept had (a model's or a sector's:
 * the translation differs first) */
static inline int same_proj(const int *a, const int *b) {
  int k;
  if (a[PROJ_TR] != b[PROJ_TR] || a[PROJ_TR + 1] != b[PROJ_TR + 1] || a[PROJ_TR + 2] != b[PROJ_TR + 2])
    return 0;
  for (k = 0; k < PROJ_TR; k++)
    if (a[k] != b[k])
      return 0;
  return a[PROJ_OFX] == b[PROJ_OFX] && a[PROJ_OFY] == b[PROJ_OFY] && a[PROJ_H] == b[PROJ_H];
}

/* A native projection's registers (np) are the last ones kept (w), by
 * their generation (#205): one compare where same_proj made up to 15. A
 * generation not seen yet (or none) compares the words once, and when they
 * are the same takes the generation, so the projections after it compare
 * that alone. Which registers are kept, and where, are as with same_proj. */
static inline int same_ngte(int *w, const NgteProj *np) {
  if (np->gen && (unsigned)w[PROJ_GEN] == np->gen)
    return 1;
  if (w[PROJ_TR] != np->trx || w[PROJ_TR + 1] != np->try_ || w[PROJ_TR + 2] != np->trz ||
      w[0] != np->r11 || w[1] != np->r12 || w[2] != np->r13 || w[3] != np->r21 ||
      w[4] != np->r22 || w[5] != np->r23 || w[6] != np->r31 || w[7] != np->r32 ||
      w[8] != np->r33 || w[PROJ_OFX] != np->ofx || w[PROJ_OFY] != np->ofy || w[PROJ_H] != np->h)
    return 0;
  w[PROJ_GEN] = (int)np->gen;
  return 1;
}

/* the projection kept on tb's slot i's list: 0 when there's no room. Its
 * registers: proj's words, or np's (a native projection, #205; proj NULL).
 * *lines: the store's lines it wrote (a model: the record's, a line each
 * two in order; the registers' two when new; the list's head) */
static int lz_add(Table *tb, int i, int q, const int *proj, const NgteProj *np, int vx, int vy,
                  int vz, int sz, int sf, unsigned *lines) {
  Lazy *const lz = tb->lz;
  Entry *const s = &tb->s[i];
  LzRec *r;
  unsigned p;
  if (q < 0 || lz->n >= VC_LOG || vx != (int16_t)vx || vy != (int16_t)vy || vz != (int16_t)vz ||
      sz != (uint16_t)sz)
    return 0; /* (each fits: the GTE's registers) */
  p = lz->nproj;
  if (p && (np ? same_ngte(lz->proj[p - 1], np) : same_proj(lz->proj[p - 1], proj)))
    p--;
  else if (p < LZ_PROJ) {
    int *const w = lz->proj[lz->nproj++];
    if (np) {
      w[0] = np->r11, w[1] = np->r12, w[2] = np->r13;
      w[3] = np->r21, w[4] = np->r22, w[5] = np->r23;
      w[6] = np->r31, w[7] = np->r32, w[8] = np->r33;
      w[PROJ_TR] = np->trx, w[PROJ_TR + 1] = np->try_, w[PROJ_TR + 2] = np->trz;
      w[PROJ_OFX] = np->ofx, w[PROJ_OFY] = np->ofy, w[PROJ_H] = np->h;
      w[PROJ_GEN] = (int)np->gen;
    } else {
      memcpy(w, proj, sizeof(int) * PROJ_N);
      w[PROJ_GEN] = 0;
    }
    *lines += 2;
  } else
    return 0;
  r = &lz->rec[lz->n];
  r->vx = (int16_t)vx, r->vy = (int16_t)vy, r->vz = (int16_t)vz;
  r->sz = (uint16_t)sz;
  r->q = (uint16_t)q;
  r->next = 0;
  r->proj = (uint16_t)(p | (sf ? LZ_SF : 0));
  *lines += (lz->n & 1) == 0;
  r->next = s->tag & T_PEND ? lz->head[i] : 0;
  s->tag |= T_PEND;
  lz->head[i] = (uint16_t)++lz->n;
  *lines += 1;
  return 1;
}

/* tb's slot i's projections worked out, in their order (k: where their
 * counts go). The reader has just found the entry, so its first line isn't
 * counted again. */
static void lz_flush(Table *tb, const Sink *k, int i) {
  Lazy *const lz = tb->lz;
  Entry *const s = &tb->s[i];
  unsigned r = lz->head[i], o = 0;
  Touch t = {(uint32_t)((size_t)i * sizeof(Entry) / 32), 0};
  s->tag &= ~T_PEND;
  while (r) { /* oldest first */
    LzRec *const z = &lz->rec[r - 1];
    const unsigned nx = z->next;
    z->next = (uint16_t)o;
    o = r;
    r = nx;
  }
  r = o;
  while (r) {
    const LzRec *const z = &lz->rec[r - 1];
    Pt a;
    pt_words(&a, lz->proj[z->proj & ~LZ_SF], z->vx, z->vy, z->vz);
    rtp_apply(tb, k, &a, s->key, z->sz, (z->proj & LZ_SF) != 0, i, z->q, &t);
    r = z->next;
  }
  k->cost[VCC_REC_LINES] += t.n;
}

/* entry e of table tab, read next: its pending projections first */
static inline void lz_need(const Table *tab, const Sink *k, int e) {
  if (__builtin_expect((tab->s[e].tag & T_PEND) != 0, 0))
    lz_flush((Table *)tab, k, e);
}

/* One projection recorded: its registers as words (proj), or a native
 * projection's registers by pointer (np) with the GTE's own sums (o) (#205;
 * proj NULL). Inlined into each caller, so each keeps only its own form. */
static inline __attribute__((always_inline)) void rtp_point(int kind, const Sink *k,
                                                           const int *proj, const NgteProj *np,
                                                           const NgteRtp *o, int vx, int vy,
                                                           int vz, uint32_t key, int sz, int sf) {
  unsigned *const cost = k->cost;
  Rec *const rec = &s_rec[kind];
  Table *const tb = rec->tab;
  Touch t = TOUCH0;
  int i, q = -1;

  k->st->records++;
  k->work[VCW_PROJ]++;
  /* The entry first (made if it's new): a key already ambiguous stays so,
   * whatever this projection is, so it needs none of the maths below. */
  i = find(rec, kind, key, 1, &cost[VCC_REC_PROBES], &t);
  if (i < 0) {
    k->st->full++;
    cost[VCC_REC_LINES] += t.n;
    return;
  }
  if (tb->nlog < VC_LOG) { /* its place in the frame's order (#183) */
    q = (int)tb->nlog++;
    tb->log[q] = (uint16_t)i;
    cost[VCC_REC_LINES] += (q & 15) == 0; /* written in order: a line every 16 */
  }
  /* #193, #179: the logic draw's kept for its walk only while the walk is
   * idle: with the last frame's walk still running, the walk is the
   * thread short of time, so the game thread works them out itself */
  if (tb->lazy && (kind == VC_PRES || !__atomic_load_n(&s_walk.busy, __ATOMIC_RELAXED))) {
    unsigned lines = 0;
    if (lz_add(tb, i, q, proj, np, vx, vy, vz, sz, sf, &lines)) {
      cost[VCC_REC_LINES] += t.n; /* (the table's: the store's apart, #179) */
      if (k->rec) {
        k->rec[VCR_KEPT]++;
        k->rec[VCR_STORE] += lines;
      }
      return;
    }
    lz_need(tb, k, i); /* no room: the entry's list first, then this one */
  }
  if (k->rec)
    k->rec[VCR_ONCE]++;
  {
    Pt a;
    if (np)
      pt_ngte(&a, np, o, vx, vy, vz);
    else
      pt_words(&a, proj, vx, vy, vz);
    rtp_apply(tb, k, &a, key, sz, sf, i, q, &t);
  }
  cost[VCC_REC_LINES] += t.n;
}

/* the logic draw's records: the game thread's block (#179) */
static const Sink k_game = SINK_ACC(0);

/* RTPS/RTPT on the register file: PsyCross's GTE_operator (fast_render=0)
 * and gte_inline.h's commands (the mips2c units and the hand renderers'
 * register-file code at fast_render 1 and 2, D89). */
static void rtp_hook(int idx, int sf) {
  const int proj[PROJ_N] = {C2_R11, C2_R12, C2_R13, C2_R21, C2_R22, C2_R23, C2_R31, C2_R32, C2_R33,
                            C2_TRX, C2_TRY, C2_TRZ, C2_OFX, C2_OFY, (unsigned short)C2_H};
  rtp_point(VC_LOGIC, &k_game, proj, NULL, NULL, V_X(idx), V_Y(idx), V_Z(idx), C2_SXY2,
            (unsigned short)C2_SZ3, sf);
}

/* The same projection given as values (D89): the sky's records
 * (sky_hand.c), and whatever calls the value hook; since #205 gte_native.h's
 * RTPS and RTPT call ngte_vcache (below) instead. */
static void rtp_value_hook(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3) {
  rtp_point(VC_LOGIC, &k_game, proj, NULL, NULL, vx, vy, vz, sxy, sz3, 1);
}

/* The registers' generations (gte_native.h NgteProj gen, #205): the game's
 * renderers' and the presenter's build's (render_ctx_names.h), each counted
 * on its own thread. */
unsigned g_ngte_gen;
unsigned g_pres_ngte_gen;

/* The hand renderers' native RTPS and RTPT (gte_native.h, fast_render=2,
 * D80, D89; #205): the registers by pointer and the sums and SZ3 the GTE
 * emulation worked out, where gte_ops.c copied 15 registers into the value
 * hook's form for each vertex. Called only while the value hook is set,
 * which is rtp_value_hook then (psx_vcache_enable): the same record. */
void ngte_vcache(const NgteProj *p, unsigned int vxy, unsigned int vz, const NgteRtp *o) {
  rtp_point(VC_LOGIC, &k_game, NULL, p, o, ngte_vx(vxy), ngte_vy(vxy), ngte_vz(vz), o->sxy,
            (int)o->sz3, 1);
}

/* the presenter's counts: its thread's, or the game thread's inline */
static void pres_sink(Sink *k) {
  k->work = s_pres_async ? g_vcache_pres_work : g_vcache_work;
  k->cost = COST(k->work);
  k->st = &g_vcache_stats[VC_PRES];
  k->rec = NULL;
}

/* The presenter's renderers' native RTPS (render_ctx.c pres_ngte_vcache,
 * on g_pres_rtp_value_hook): into the presenter's own table (#158), on its
 * own thread or inline (#162) */
static void pres_rtp_value_hook(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3) {
  Sink k;
  pres_sink(&k);
  rtp_point(VC_PRES, &k, proj, NULL, NULL, vx, vy, vz, sxy, sz3, 1);
}

/* The presenter's renderers' native RTPS and RTPT (its build's ngte_vcache,
 * render_ctx_names.h; #205), as ngte_vcache above into its own table. Called
 * only while g_pres_rtp_value_hook is set, which is pres_rtp_value_hook then
 * (psx_vcache_pres_begin): the same record. */
void pres_vcache_ngte(const NgteProj *p, unsigned int vxy, unsigned int vz, const NgteRtp *o) {
  Sink k;
  pres_sink(&k);
  rtp_point(VC_PRES, &k, NULL, p, o, ngte_vx(vxy), ngte_vy(vxy), ngte_vz(vz), o->sxy,
            (int)o->sz3, 1);
}

/* ratio(a, b) < 4, exactly (depths > 0; times 4 isn't rounded) */
static inline int under4(float a, float b) { return a > b ? a < b * 4.0f : b < a * 4.0f; }
/* ratio(a, b) < 3.999 or so: the quotient, however rounded, is under 4 */
static inline int under3999(float a, float b) { return a > b ? a < b * 3.999f : b < a * 3.999f; }

/* The same two tests on the depths' bits (#206), for the walk's choice. A
 * positive normal float's bits grow with it, and times 4 adds 2 to its
 * exponent exactly: for depths in W_LO..W_HI (4 times one is still normal)
 * a < 4b is ua < ub + 4 << 22, with no float compare (a library call's
 * worth on the host's emulated x87, a VFP compare and a flag move on the
 * A9). Bits apart by less than 0x00FE0000 are at most 3.96875 times apart,
 * so under 3.999 however the product is rounded; past that the float test
 * decides, as before. Any other value takes the float tests. */
#define W_LO 0x00800000u
#define W_HI 0x7E800000u
static inline uint32_t w_bits(const float *w) {
  uint32_t u;
  memcpy(&u, w, sizeof u);
  return u;
}
static inline int w_normal(uint32_t u) { return u - W_LO < W_HI - W_LO; }
static inline int under4b(const float *a, const float *b) {
  const uint32_t ua = w_bits(a), ub = w_bits(b);
  if (__builtin_expect(w_normal(ua) && w_normal(ub), 1))
    return (ua > ub ? ua - ub : ub - ua) < 0x01000000u;
  return under4(*a, *b);
}
/* same_depth on the bits (#206, resolve's): bits apart by at most 0x28000
 * are at most 1.01954 times apart (inside 1.0199), by 0x53000 or more at
 * least 1.02068 (past 1.0201); between, the float test as before */
static inline int same_depthb(const float *a, const float *b, unsigned *div) {
  const uint32_t ua = w_bits(a), ub = w_bits(b);
  if (__builtin_expect(w_normal(ua) && w_normal(ub), 1)) {
    const uint32_t d = ua > ub ? ua - ub : ub - ua;
    if (d <= 0x28000u)
      return 1;
    if (d >= 0x53000u)
      return 0;
  }
  return same_depth(*a, *b, div);
}
static inline int under3999b(const float *a, const float *b) {
  const uint32_t ua = w_bits(a), ub = w_bits(b);
  if (__builtin_expect(w_normal(ua) && w_normal(ub), 1) &&
      (ua > ub ? ua - ub : ub - ua) < 0x00FE0000u)
    return 1;
  return under3999(*a, *b);
}

/* ------------------------------------------------------------ hand-overs (#189) */

/* key's hand-over in tab's frame gen, or (add) a free one for it (gen set,
 * nothing in it); NULL: none, or the probe is exhausted */
static Hk *hk_slot(Table *tb, uint32_t gen, uint32_t key, int add, unsigned *probes, Touch *t) {
  uint32_t h = hash(key) & (VC_HK - 1), p;
  for (p = 0; p < 8; p++, h = (h + 1) & (VC_HK - 1)) {
    Hk *const e = &tb->hk[h];
    ++*probes;
    touch(t, offsetof(Table, hk) + (size_t)h * sizeof(Hk), 8);
    if (e->gen != gen) {
      if (!add)
        return NULL;
      e->key = key;
      e->gen = gen;
      e->n = e->dead = e->placed = 0;
      return e;
    }
    if (e->key == key)
      return e;
  }
  return NULL;
}

/* A hand-over's source in rec's frame: its place on screen (pixels) and
 * depths w[], as a polygon using the key would be given them. A
 * projection's valid entry (its exact place, or the key's pixel centre for
 * different points on it, T_MULTI, whose depths each polygon picks from),
 * or, with no entry, another hand-over (a cut of a cut point; its place,
 * and its depths if it has any). With flat, a key with neither is a 2D
 * point at its pixel centre, without depths (the screen's corners of a
 * portal's face). Returns the depths' count (0: the place only), -1 not
 * usable; *exact: the place came from a projection. */
static int hk_src(Rec *rec, int kind, const Sink *k, uint32_t key, float *x, float *y, float *w,
                  int flat, int *exact, unsigned *cost, Touch *t) {
  Table *const tb = rec->tab;
  const int i = find(rec, kind, key, 0, &cost[VCC_REC_PROBES], t);
  const float kx = (short)(key & 0xFFFF) + 0.5f, ky = (short)(key >> 16) + 0.5f;
  unsigned n, j;
  *exact = 1;
  if (i >= 0) {
    const Entry *const s = &tb->s[i];
    lz_need(tb, k, i); /* #193 */
    n = T_N(s->tag);
    if (T_STATE(s->tag) != E_VALID || !n)
      return -1;
    touch(t, (size_t)i * sizeof(Entry) + offsetof(Entry, c), sizeof(Cand) * n);
    if (s->tag & T_MULTI) /* different points: drawn at the key, as the source is */
      *x = *y = 0;
    else
      cand_pos(tb, &s->c[0], x, y);
    for (j = 0; j < n; j++)
      w[j] = s->c[j].w;
  } else {
    const Hk *const h = hk_slot(tb, rec->gen, key, 0, &cost[VCC_REC_PROBES], t);
    if (!h || h->dead) {
      if (!flat || h)
        return -1;
      *exact = 0; /* 2D */
      *x = *y = 0;
      n = 0;
    } else {
      n = h->n;
      *x = h->dx;
      *y = h->dy;
      for (j = 0; j < n; j++)
        w[j] = h->c[j].w;
    }
  }
  *x += kx;
  *y += ky;
  return (int)n;
}

/* The depths of a point the fraction f of the way from a to b (depths aw,
 * bw): each of a's with b's nearest (one scale), in 3D (persp) or on
 * screen (1/w linear); into w (count returned), and the nearest pair's in
 * *pa, *pb. */
static unsigned hk_pair(const float *aw, int na, const float *bw, int nb, float f, int persp,
                        float *w, float *pa, float *pb, unsigned *cost) {
  float best = 4.0f;
  unsigned n = 0, j;
  int i, k;
  for (i = 0; i < na; i++) {
    float rb = 4.0f, wd;
    int jb = -1;
    for (k = 0; k < nb; k++)
      if (under4(aw[i], bw[k])) {
        const float r = ratio(aw[i], bw[k]);
        cost[VCC_REC_DIV]++;
        if (r < rb) {
          rb = r;
          jb = k;
        }
      }
    if (jb < 0)
      continue;
    if (persp)
      wd = flt((1.0f - f) * aw[i] + f * bw[jb]);
    else {
      wd = flt(1.0f / ((1.0f - f) / aw[i] + f / bw[jb]));
      cost[VCC_REC_DIV] += 3;
    }
    if (!(wd > 0))
      continue;
    for (j = 0; j < n; j++)
      if (same_depth(w[j], wd, &cost[VCC_REC_DIV]))
        break;
    if (j == n)
      w[n++] = wd;
    if (rb < best) {
      best = rb;
      *pa = aw[i];
      *pb = bw[jb];
    }
  }
  return n;
}

/* One hand-over (psx_vcache.h PSX_VC_DERIVE) into rec's frame */
static void derive_hand(int kind, const Sink *k, int how, uint32_t key, uint32_t a, uint32_t b,
                        int p, int q) {
  unsigned *const cost = k->cost;
  Rec *const rec = &s_rec[kind];
  VCacheStats *const st = k->st;
  const int form = how & 15, two = form == VCD_LERP || form == VCD_PERSP, cut = form == VCD_LERP;
  const unsigned who = (unsigned)how >> 4 < VCD_WHO_N ? (unsigned)how >> 4 : 0;
  float ax, ay, bx = 0, by = 0, aw[VC_CANDS], bw[VC_CANDS], w[VC_CANDS], x, y, dx, dy;
  float lim = 1024, pa = 0, pb = 0;
  int na, nb = 0, ea, eb = 0;
  unsigned n = 0, i, j;
  Touch t = TOUCH0;
  Hk *h;
  if (!rec->tab)
    return;
  st->hk_calls++;
  na = hk_src(rec, kind, k, a, &ax, &ay, aw, cut, &ea, cost, &t);
  if (na >= 0 && two)
    nb = hk_src(rec, kind, k, b, &bx, &by, bw, cut, &eb, cost, &t);
  if (na < 0 || nb < 0 || (form == VCD_PERSP && (!na || !nb)))
    goto nosrc;
  if (form == VCD_LERP) {
    /* where the side a-b crosses the line through keys p and q (the
     * portal's outline edge): each end exact where it has a projection */
    float ex0, ey0, ex1, ey1, ew[VC_CANDS], nx, ny, den, f;
    int e0, e1;
    if (hk_src(rec, kind, k, (uint32_t)p, &ex0, &ey0, ew, 1, &e0, cost, &t) < 0 ||
        hk_src(rec, kind, k, (uint32_t)q, &ex1, &ey1, ew, 1, &e1, cost, &t) < 0 ||
        !(ea | eb | e0 | e1))
      goto nosrc; /* all four 2D: nothing to place exactly */
    nx = ey0 - ey1;
    ny = ex1 - ex0;
    den = nx * (bx - ax) + ny * (by - ay);
    if (!(den > 1e-6f || den < -1e-6f))
      goto nosrc;
    f = (nx * (ex0 - ax) + ny * (ey0 - ay)) / den;
    cost[VCC_REC_DIV]++;
    x = ax + f * (bx - ax);
    y = ay + f * (by - ay);
    lim = 8; /* the PS1's cut is within a pixel or two of it */
    if (na && nb)
      n = hk_pair(aw, na, bw, nb, f, 0, w, &pa, &pb, cost);
  } else if (form == VCD_PERSP) {
    /* the 3D point f of the way: the place from the nearest pair (another
     * scale's pair gives the same) */
    const float f = (float)p * (1.0f / 4096);
    float ka, kb, r;
    n = hk_pair(aw, na, bw, nb, f, 1, w, &pa, &pb, cost);
    if (!n)
      goto nosrc;
    ka = (1.0f - f) * pa;
    kb = f * pb;
    r = 1.0f / (ka + kb);
    cost[VCC_REC_DIV]++;
    x = (ax * ka + bx * kb) * r;
    y = (ay * ka + by * kb) * r;
  } else { /* one point: its depths, its place (and the offset) */
    x = ax + (form == VCD_OFFSET ? (float)p : 0.0f);
    y = ay + (form == VCD_OFFSET ? (float)q : 0.0f);
    for (i = 0; i < (unsigned)na; i++)
      w[n++] = aw[i];
  }
  dx = flt(x - ((short)(key & 0xFFFF) + 0.5f));
  dy = flt(y - ((short)(key >> 16) + 0.5f));
  if (!(dx < lim && dx > -lim && dy < lim && dy > -lim)) /* not this key's point */
    goto nosrc;
  h = hk_slot(rec->tab, rec->gen, key, 1, &cost[VCC_REC_PROBES], &t);
  if (!h) {
    st->hk_full++;
    goto out;
  }
  touch(&t, offsetof(Table, hk) + (size_t)(h - rec->tab->hk) * sizeof(Hk), sizeof(Hk));
  if (h->dead)
    goto out;
  if (h->placed) { /* handed over before: the same point (its depths added), or not used */
    if (!(h->dx - dx <= 0.125f && h->dx - dx >= -0.125f && h->dy - dy <= 0.125f &&
          h->dy - dy >= -0.125f)) {
      h->dead = 1;
      st->hk_conflict++;
      goto out;
    }
  } else {
    h->dx = dx;
    h->dy = dy;
    h->who = (uint8_t)who;
    h->placed = 1;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < h->n; j++)
      if (same_depth(h->c[j].w, w[i], &cost[VCC_REC_DIV]))
        break;
    if (j == h->n && h->n < VC_CANDS) {
      h->c[h->n].dx = h->c[h->n].dy = 0;
      h->c[h->n++].w = w[i];
    }
  }
  st->hk_kept++;
  goto out;
nosrc:
  st->hk_nosrc++;
out:
  cost[VCC_REC_LINES] += t.n;
}

PsxVcacheDeriveHook g_psyxRtpDeriveHook;
static PsxVcacheDeriveHook *s_pres_derive; /* the presenter's (render_ctx.c), set with its frame */

/* the game's renderers' (the logic draw's frame) */
static void derive_hook(int how, unsigned int sxy, unsigned int a, unsigned int b, int p, int q) {
  derive_hand(VC_LOGIC, &k_game, how, sxy, a, b, p, q); /* (#179: the game thread's block) */
}

/* the presenter's renderers' (render_ctx.c g_pres_rtp_derive_hook): into
 * its own table, on its own thread or inline, as its projections */
static void pres_derive_hook(int how, unsigned int sxy, unsigned int a, unsigned int b, int p,
                             int q) {
  Sink k;
  pres_sink(&k);
  derive_hand(VC_PRES, &k, how, sxy, a, b, p, q);
}

void psx_vcache_pres_derive_at(PsxVcacheDeriveHook *hook) { s_pres_derive = hook; }

/* PSX_VC_DERIVE's call (the game's renderers) */
void psx_vc_derive(int how, unsigned int sxy, unsigned int a, unsigned int b, int p, int q) {
  g_psyxRtpDeriveHook(how, sxy, a, b, p, q);
}

/* a polygon vertex with no entry: its hand-over in tab's frame gen, if one
 * is usable (#189) */
static const Hk *hk_look(const Table *tb, uint32_t gen, uint32_t key, unsigned *probes, Touch *t) {
  uint32_t h = hash(key) & (VC_HK - 1), p;
  for (p = 0; p < 8; p++, h = (h + 1) & (VC_HK - 1)) {
    const Hk *const e = &tb->hk[h];
    ++*probes;
    touch(t, offsetof(Table, hk) + (size_t)h * sizeof(Hk), 8);
    if (e->gen != gen)
      return NULL;
    if (e->key == key) {
      if (e->dead || !e->placed)
        return NULL;
      touch(t, offsetof(Table, hk) + (size_t)h * sizeof(Hk), sizeof(Hk));
      return e;
    }
  }
  return NULL;
}

/* The depth choice for the polygon's vertices idx[0..m-1] (all found):
 * each candidate of the first against the closest of every other's; the
 * choice whose worst depth ratio is smallest. Candidates of one point at
 * other scales differ by 4 or 16 times, different points on one pixel by
 * their distance: a polygon's real depths are much closer. 1 when a choice
 * under 4 was found (pick[] set for those vertices), else 0.
 *
 * The same choice with fewer divisions (#159). A ratio of 4 or more (4b <=
 * a, exact: b times 4 isn't rounded) can't be a polygon's: if it were the
 * closest, the worst would be at least 4 and the choice not taken, so such
 * candidates are passed over without dividing. With one candidate for
 * the first vertex, only whether each other vertex's closest ratio is
 * under 4 matters; a vertex with one candidate left is then its own
 * closest, and it's under 4 without dividing when it's under 3.999 (how
 * the quotient would round can't reach 4). Every other ratio is divided
 * as before, so the choice and the depths are the same. */
static int choose(const Cand *const *e, const unsigned *en, const int *idx, int m, unsigned *cost,
                  int *pick) {
  const int f = idx[0];
  int c, i, j, best_c = -1, try_pick[4] = {0, 0, 0, 0};
  float best = 4.0f; /* a polygon's depths are never 4x apart */
  for (c = 0; c < (int)en[f]; c++) {
    const float w0 = e[f][c].w;
    float worst = 1;
    int ok = 1;
    try_pick[f] = c;
    for (i = 1; ok && i < m; i++) {
      const int v = idx[i];
      float r_best = 1e30f;
      int left = 0, last = 0;
      unsigned u4 = 0; /* #206: which are under 4, tested once */
      for (j = 0; j < (int)en[v]; j++)
        if (under4b(&e[v][j].w, &w0)) {
          left++;
          last = j;
          u4 |= 1u << j;
        }
      if (!left) {
        ok = 0;
        break;
      }
      if (left == 1 && en[f] == 1) {
        try_pick[v] = last;
        if (!under3999b(&e[v][last].w, &w0)) {
          const float r = ratio(e[v][last].w, w0);
          cost[VCC_LOOK_DIV]++;
          worst = r > worst ? r : worst;
        }
        continue;
      }
      for (j = 0; j < (int)en[v]; j++)
        if (u4 & 1u << j) {
          const float r = ratio(e[v][j].w, w0);
          cost[VCC_LOOK_DIV]++;
          if (r < r_best) {
            r_best = r;
            try_pick[v] = j;
          }
        }
      worst = r_best > worst ? r_best : worst;
    }
    if (ok && worst < best) {
      best = worst;
      best_c = c;
      memcpy(pick, try_pick, sizeof try_pick);
    }
  }
  return best_c >= 0;
}

/* The frame's order, indexed by entry, once a frame (the walk's first
 * derive or resolve): each entry's last place in the log (VC_NOSEQ: none)
 * and, for each place, the one before it under the same entry. */
static void log_index(Table *tab, uint32_t gen, unsigned *cost) {
  unsigned r;
  if (tab->idx_gen == gen)
    return;
  for (r = 0; r < tab->nlog; r++)
    tab->last[LOG_E(tab->log[r])] = VC_NOSEQ;
  for (r = 0; r < tab->nlog; r++) {
    tab->prev[r] = tab->last[LOG_E(tab->log[r])];
    tab->last[LOG_E(tab->log[r])] = (uint16_t)r;
  }
  tab->idx_gen = gen;
  /* a model: the log read twice and prev written in order, an entry's
   * last word apiece (at most the array) */
  cost[VCC_LOOK_LINES] += tab->nlog * 6 / 32 + (tab->nlog < 512 ? tab->nlog : 512);
}

/* A quad's vertex with no entry, its three others found (#183). Nearly
 * all of them are the world renderer's subdivision grids' inner points
 * (env_hand.c GridCorrect): projected like every other point, then moved
 * on screen toward the midpoint of two neighbours (the PS1's blend toward
 * its affine look), so no projection was recorded under the key they end
 * up with. The vertex is found again from its polygon.
 *
 * Its place: the projection recorded before the renderer moved it. The
 * grids' points are projected one after another in the grid's order (3x3
 * or 5x5, GridTransform), so with the quad's corners round it (the walk's
 * order, MakeVertexQuad's 0 1 3 2 of the primitive's), the missing one m,
 * the one opposite it o and the two beside it a and b, m's place in the
 * frame's order is a + b - o, as on the grid: a and b are o's neighbours
 * along the grid, one of them 1 on, the other a row (3 or 5) on. Each
 * corner's projections are walked (a point shared with a neighbouring grid
 * was projected once for each); exactly one such place must come out, and
 * its entry must be valid and a single point, or none is taken. Then the
 * vertex is drawn there (VREF_CLAMP: even without subpixel, as the quad's
 * other points are). Not found: it stays at its key.
 *
 * The place is decided per key for the frame (Table der): a polygon that
 * finds nothing takes the place another found, so the polygons sharing
 * the point don't crack apart (two grids' points moved onto one pixel
 * each find their own).
 *
 * Its depth: the plane's through the other three at that place (1/w is
 * linear on screen over a plane), or, at the recorded projection, that
 * projection's own depth at the scale the polygon's others use.
 *
 * Returns 1 with the vertex's answer in *out and the others' picks in
 * pick[]; else 0, the polygon drawn affine as before, *out still holding
 * the vertex's place (moved when it isn't the key's own). */
/* derive's place (above): the corners' entries slotf[] (each found or,
 * #197, an ambiguous key's), the vertex's place in *out and the projection
 * it is (*slot, entry + 1, 0: none). The key's place for the frame's other
 * polygons goes to *dr when it should be kept there: at once with commit,
 * else the caller keeps it (der_keep) once the polygon is drawn so. */
static void derive_place(Table *tab, uint32_t gen, short *const *xy, const uint32_t *slotf, int m,
                         const Sink *lk, unsigned *cost, VCacheVert *out, uint32_t *slot_out,
                         Der **dr_out, int commit) {
  static const int k_opp[4] = {2, 3, 0, 1};
  const uint32_t key = (uint16_t)xy[m][0] | (uint32_t)(uint16_t)xy[m][1] << 16;
  const float kx = xy[m][0] + 0.5f, ky = xy[m][1] + 0.5f;
  const int o = k_opp[m];
  int ab[2], nab = 0, i, have = 0;
  float x, y;
  uint32_t slot = 0, h, p;
  Der *dr = NULL, *hd = NULL;
  for (i = 0; i < 4; i++)
    if (i != m && i != o)
      ab[nab++] = i;
  out->dx = out->dy = 0;
  out->w = 1;
  *dr_out = NULL;
  /* the key's place this frame, if a polygon gave it one */
  h = hash(key) & (VC_DER - 1);
  for (p = 0; p < 8; p++, h = (h + 1) & (VC_DER - 1)) {
    Der *const t = &tab->der[h];
    if (t->gen != gen) {
      dr = t; /* its place, if it gets one */
      break;
    }
    if (t->key == key) {
      hd = t;
      have = 1;
      break;
    }
  }
  {
    /* the place in the frame's order a + b - o */
    const uint32_t so0 = slotf[o], sa0 = slotf[ab[0]], sb0 = slotf[ab[1]];
    unsigned so, sa, sb, no, na, nb, one = VC_NOSEQ;
    int conflict = 0;
    log_index(tab, gen, cost);
    for (so = tab->last[so0], no = 0; so != VC_NOSEQ && no < 8; so = tab->prev[so], no++)
      for (sa = tab->last[sa0], na = 0; sa != VC_NOSEQ && na < 8; sa = tab->prev[sa], na++) {
        const unsigned da = sa > so ? sa - so : so - sa;
        if (da != 1 && da != 3 && da != 5)
          continue;
        for (sb = tab->last[sb0], nb = 0; sb != VC_NOSEQ && nb < 8; sb = tab->prev[sb], nb++) {
          const unsigned db = sb > so ? sb - so : so - sb;
          unsigned sm;
          if (!(da == 1 ? db == 3 || db == 5 : db == 1) || sa + sb < so)
            continue;
          sm = sa + sb - so;
          if (sm >= tab->nlog)
            continue;
          if (one != VC_NOSEQ && sm != one)
            conflict = 1; /* two places: none is taken */
          one = sm;
        }
      }
    if (one != VC_NOSEQ && !conflict) {
      const uint32_t k2 = LOG_E(tab->log[one]);
      uint32_t tag;
      lz_need(tab, lk, (int)k2); /* #193 */
      tag = tab->s[k2].tag;
      if (k2 != so0 && k2 != sa0 && k2 != sb0 && T_STATE(tag) == E_VALID && T_N(tag) &&
          !(tag & T_MULTI)) {
        slot = k2 + 1;
        cand_pos(tab, &tab->s[k2].c[0], &x, &y);
        out->dx = (short)tab->s[k2].key + 0.5f + x - kx;
        out->dy = (short)(tab->s[k2].key >> 16) + 0.5f + y - ky;
      }
    }
    if (!slot && have) { /* not found: the place another polygon found */
      out->dx = hd->dx;
      out->dy = hd->dy;
      slot = hd->slot;
    } else if (slot && dr) { /* for this frame's polygons that find nothing */
      dr->key = key;
      dr->gen = gen;
      dr->dx = out->dx;
      dr->dy = out->dy;
      dr->slot = slot;
      if (!commit) { /* (#197: kept by the caller) */
        dr->gen = gen - 1;
        *dr_out = dr;
      }
    }
  }
  *slot_out = slot;
}

/* the key's place derive_place left to the caller (#197), now kept */
static inline void der_keep(Der *dr, uint32_t gen) {
  if (dr)
    dr->gen = gen;
}

/* derive's depth (above), at the place derive_place gave (*out, slot): the
 * other corners' candidates e[], en[]; ov (#197, NULL: none) a corner's
 * place (pixels from its pixel centre) where ov[v][2] is set, in place of
 * its candidate's: a resolved vertex is drawn at its key, or at a clamped
 * point's own place */
static int derive_depth(Table *tab, short *const *xy, const Cand *const *e, const unsigned *en,
                        int m, uint32_t slot, const float (*ov)[3], unsigned *cost, int *pick,
                        VCacheVert *out) {
  const float kx = xy[m][0] + 0.5f, ky = xy[m][1] + 0.5f;
  int idx[3], ni = 0, i, j;
  float sx[4], sy[4], z[4], x, y, w, d, q;
  for (i = 0; i < 4; i++)
    if (i != m)
      idx[ni++] = i;
  if (!choose(e, en, idx, 3, cost, pick))
    return 0;
  for (i = 0; i < 3; i++) {
    const int v = idx[i];
    if (ov && ov[v][2] != 0) {
      x = ov[v][0];
      y = ov[v][1];
    } else
      cand_pos(tab, &e[v][pick[v]], &x, &y);
    sx[v] = xy[v][0] + 0.5f + x;
    sy[v] = xy[v][1] + 0.5f + y;
    z[v] = e[v][pick[v]].w;
  }
  x = kx + out->dx;
  y = ky + out->dy;
  d = (sx[idx[1]] - sx[idx[0]]) * (sy[idx[2]] - sy[idx[0]]) -
      (sx[idx[2]] - sx[idx[0]]) * (sy[idx[1]] - sy[idx[0]]);
  if (!(d > 1e-3f || d < -1e-3f))
    return 0;
  {
    const float q0 = 1 / z[idx[0]], q1 = 1 / z[idx[1]], q2 = 1 / z[idx[2]];
    q = q0 + ((x - sx[idx[0]]) * ((q1 - q0) * (sy[idx[2]] - sy[idx[0]]) -
                                  (q2 - q0) * (sy[idx[1]] - sy[idx[0]])) +
              (y - sy[idx[0]]) * ((q2 - q0) * (sx[idx[1]] - sx[idx[0]]) -
                                  (q1 - q0) * (sx[idx[2]] - sx[idx[0]]))) /
                 d;
    cost[VCC_LOOK_DIV] += 4;
  }
  if (!(q > 0))
    return 0;
  w = 1 / q;
  cost[VCC_LOOK_DIV]++;
  if (slot) { /* the projection's own depth, at the scale the polygon uses */
    const Entry *const s2 = &tab->s[slot - 1];
    float bw = 0, br = 2.0f; /* other scales are 4 or 16 times off */
    for (j = 0; j < (int)T_N(s2->tag); j++) {
      const float r = ratio(s2->c[j].w, w);
      cost[VCC_LOOK_DIV]++;
      if (r <= br) {
        br = r;
        bw = s2->c[j].w;
      }
    }
    if (bw > 0)
      w = bw;
  }
  w = flt(w);
  for (i = 0; i < 3; i++)
    if (!under4(w, z[idx[i]]))
      return 0;
  out->w = w;
  return 1;
}

/* derive (above): its place, then its depth, the others all found */
static int derive(int pres, Table *tab, uint32_t gen, short *const *xy, const Cand *const *e,
                  const unsigned *en, int m, const Sink *lk, unsigned *cost, int *pick,
                  VCacheVert *out, int *moved, int *snapped) {
  uint32_t slotf[4] = {0, 0, 0, 0}, slot;
  Der *dr;
  int i;
  (void)pres;
  for (i = 0; i < 4; i++)
    if (i != m)
      slotf[i] = (uint32_t)(((const char *)e[i] - (const char *)tab->s) / sizeof(Entry));
  derive_place(tab, gen, xy, slotf, m, lk, cost, out, &slot, &dr, 1);
  *moved = out->dx != 0 || out->dy != 0;
  *snapped = slot != 0;
  return derive_depth(tab, xy, e, en, m, slot, NULL, cost, pick, out);
}

/* Ambiguous keys (#181). Different points on one pixel: more than the
 * entry's four candidates (nearly all of them: far mobys, many of whose
 * vertices fall on a few pixels, and far ground, at a long draw distance),
 * so the key was never used and every polygon with such a vertex was drawn
 * affine. Its projections are all in the frame's log (lc holds the answer
 * of each made under the key once it had different points; the others are
 * candidate 0's point at one of its candidates' depths), so a polygon whose
 * other vertices are found takes, for that vertex, the projections under
 * its key near theirs in the frame's order: a renderer projects a model's
 * or a sector's points together and builds its polygons from them, so the
 * vertex's own projection is within the batch its neighbours' are in. Of
 * those, the polygon's depth choice (choose) picks as for any key with
 * several points. The position stays the key's integer one, as for every
 * key with different points (T_MULTI): the same for every polygon using it.
 *
 * Clamped points (#192). A clamped projection's point lies off its pixel
 * (the GTE's saturated SXY: one key for many points), so it can't be drawn
 * at the key. lc holds its own exact place too (LOG_CL; lfar when it
 * doesn't fit a candidate), and when the projections near the polygon's
 * others are all of one point (within 1/8 pixel of each other), the vertex
 * is drawn at that point's place: its oldest projection's, the same for
 * every polygon whose order names it (rx[v], rp[v]). With a clamped point
 * among several near ones it stays ambiguous: no place can be told.
 *
 * With a quad's vertex with no entry (#197, psx_vcache_poly), the
 * projection derive names for it counts as one of the polygon's found
 * vertices' places.
 *
 * Not resolved (the vertex stays ambiguous, the polygon affine): an
 * invalid projection among those near the others (the prim may be that
 * one; #192: one elsewhere in the frame doesn't count), a clamped point
 * among several near ones, more than RC_MAX depths, or a frame whose log is
 * full (a projection may be missing). Until #192 a key with more than 32
 * projections, any clamped or invalid one among them, or a clamped first
 * point stayed ambiguous too.
 *
 * Fills rc[v], rn[v] for each vertex v with amb[v] set (its entry ks[v]):
 * depths only (dx, dy 0), and rx[v] 1 with its place in rp[v] (pixels from
 * the pixel centre) for a clamped point, else 0 (the key's integer
 * position). *one: every such vertex had one point near the others in the
 * order. Returns 1, or 0 not resolved, with *why (RW_*, #197). */
#define RC_MAX 16
#define VC_NEAR 256 /* a batch: a sector's or a model's points (up to 255 a sector) */
#define RC_PTS 32   /* the near points kept to count them (#192: not a limit on projections) */

/* within 1/8 pixel (a candidate's 32/256: exact for those, as add_cand's) */
static inline int near8(float ax, float ay, float bx, float by) {
  return ax - bx <= 0.125f && bx - ax <= 0.125f && ay - by <= 0.125f && by - ay <= 0.125f;
}

/* Log place r under entry en: its point (pixels from the pixel centre), its
 * depth (0: candidate 0's point, at its candidates' depths there), alt (a
 * maybe-precision depth: w / 16 too) and clamped. 0: no usable answer (an
 * invalid projection, or a first one with no candidate). */
static int res_place(const Table *tab, const Entry *en, unsigned r, float *x, float *y, float *w,
                     int *alt, int *cl) {
  if (!(tab->log[r] & LOG_LC)) { /* the first, or the first's point again */
    if (!T_N(en->tag))
      return 0;
    cand_pos(tab, &en->c[0], x, y);
    *w = 0;
    *alt = 0;
    *cl = (en->tag & T_CLAMP) != 0;
  } else {
    const Cand *const l = &tab->lc[r];
    if (l->w == 0)
      return 0;
    *alt = l->w < 0;
    *w = *alt ? -l->w : l->w;
    if (l->dy == FAR_MARK) {
      *x = tab->lfar[(uint16_t)l->dx][0];
      *y = tab->lfar[(uint16_t)l->dx][1];
    } else {
      *x = l->dx * (1.0f / 256);
      *y = l->dy * (1.0f / 256);
    }
    *cl = (tab->log[r] & LOG_CL) != 0;
  }
  return 1;
}

static int resolve(Table *tab, uint32_t gen, const int *ks, const int *amb, int n,
                   Cand rc[4][RC_MAX], unsigned *rn, float rp[4][2], int *rx, unsigned *cost,
                   int *one, int *why) {
  unsigned anc[32], na = 0, a;
  int i;
  *one = 1;
  *why = RW_FULL;
  if (tab->nlog >= VC_LOG)
    return 0;
  log_index(tab, gen, cost);
  for (i = 0; i < n; i++) /* the places of the polygon's found vertices (#189: not a hand-over's) */
    if (!amb[i] && ks[i] >= 0) {
      unsigned r = tab->last[ks[i]], k;
      for (k = 0; r != VC_NOSEQ && k < 8 && na < 32; r = tab->prev[r], k++) {
        /* #206: kept largest first, for the walk below */
        for (a = na++; a && anc[a - 1] < r; a--)
          anc[a] = anc[a - 1];
        anc[a] = r;
      }
    }
  for (i = 0; i < n; i++) {
    const Entry *const en = &tab->s[amb[i] ? ks[i] : 0]; /* (#189: a hand-over's ks is -1) */
    const Cand *const c = en->c;
    const unsigned nc = T_N(en->tag);
    float kx[RC_PTS], ky[RC_PTS], fx = 0, fy = 0, x, y, w;
    unsigned r, j, k, m, nk, nsel, pts, near = 0, seen = 0;
    int pass, alt, ccl, cl, onept;
    if (!amb[i])
      continue;
    rx[i] = 0;
    /* The projections near the others in the order, or all of them if none
     * is (a second pass). #192: the whole chain, however long; only those
     * taken are kept, and only to count the points. */
    for (pass = 0;; pass++) {
      rn[i] = nk = nsel = pts = 0;
      cl = 0;
      onept = 1;
      a = 0;
      for (r = tab->last[ks[i]]; r != VC_NOSEQ; r = tab->prev[r]) {
        seen++;
        if (!pass && na) { /* only whether one is near counts (#193: not the nearest) */
          /* |r - anc[a]| <= VC_NEAR for some a. #206: the chain's places
           * fall and the anchors are largest first, so one walk of both:
           * an anchor past r + VC_NEAR is past every later r too, and the
           * first one left is the nearest above r - VC_NEAR if any is */
          while (a < na && anc[a] > r + VC_NEAR)
            a++;
          if (a == na)
            break; /* (none near the rest of the chain either) */
          if (anc[a] + VC_NEAR < r)
            continue;
        }
        near += !pass;
        if (!res_place(tab, en, r, &x, &y, &w, &alt, &ccl)) {
          *why = RW_INV_NEAR;
          return 0; /* invalid: the prim may be that one */
        }
        if (!nsel++) {
          fx = x;
          fy = y;
        } else if (!near8(fx, fy, x, y))
          onept = 0;
        cl |= ccl;
        if (pts < 2) { /* one point (as add_cand) counted once (#193: past two, only that it isn't one) */
          for (k = 0; k < nk; k++)
            if (near8(kx[k], ky[k], x, y))
              break;
          pts += k == nk;
          if (nk < RC_PTS) {
            kx[nk] = x;
            ky[nk] = y;
            nk++;
          }
        }
        for (m = 0; m < (w == 0 ? nc : 1u + (alt != 0)); m++) {
          float wd;
          if (w == 0) { /* candidate 0's point: the candidates there */
            if (c[m].dy == FAR_MARK || c[0].dy == FAR_MARK) {
              float cx, cy;
              cand_pos(tab, &c[m], &cx, &cy);
              if (!near8(cx, cy, x, y))
                continue;
            } else if (c[m].dx - c[0].dx > 32 || c[0].dx - c[m].dx > 32 ||
                       c[m].dy - c[0].dy > 32 || c[0].dy - c[m].dy > 32)
              continue;
            wd = c[m].w;
          } else
            wd = m ? flt(w * (1.0f / 16)) : w;
          for (j = 0; j < rn[i]; j++)
            if (same_depthb(&rc[i][j].w, &wd, &cost[VCC_LOOK_DIV]))
              break;
          if (j < rn[i])
            continue;
          if (rn[i] == RC_MAX)
            return 0;
          rc[i][rn[i]].dx = rc[i][rn[i]].dy = 0;
          rc[i][rn[i]].w = wd;
          rn[i]++;
        }
      }
      if (pass || !na || near)
        break;
    }
    if (pts != 1 || !near)
      *one = 0;
    if (cl) { /* a clamped point near: drawn at its place, if the order names it alone */
      if (!onept) {
        *why = RW_CLAMP_SEV;
        return 0;
      }
      rx[i] = 1;
      rp[i][0] = fx;
      rp[i][1] = fy;
      for (r = tab->last[ks[i]]; r != VC_NOSEQ; r = tab->prev[r], seen++) /* its oldest */
        if (res_place(tab, en, r, &x, &y, &w, &alt, &ccl) && near8(fx, fy, x, y)) {
          rp[i][0] = x;
          rp[i][1] = y;
        }
    }
    cost[VCC_LOOK_LINES] += seen; /* a model: a line each */
  }
  return 1;
}


int psx_vcache_polys;

void psx_vcache_poly(void *vertex_, short *const *xy, int n, VCacheVert *side) {
  static const int k_all[4] = {0, 1, 2, 3};
  GrVertex *const vertex = (GrVertex *)vertex_;
  const Cand *e[4];   /* each vertex's candidates, NULL: not found */
  uint32_t et[4];     /* and its slot's tag */
  unsigned en[4];     /* how many */
  uint32_t ref[4], persp = VREF_PERSP;
  const Table *ptab = NULL; /* the table its vertices were looked up in (the same for each) */
  int i, pick[4] = {0, 0, 0, 0};
  int inv = 0, clamp = 0; /* a vertex's key invalid; a clamped vertex found (#178) */
  /* vertices with no entry (#183): the last one, how many; and all not found */
  int none = -1, nnone = 0, nmiss = 0;
  int der = 0, der_moved = 0, der_snap = 0; /* the one with no entry derived (#183) */
  /* vertices whose key is ambiguous (#181): their entries, how many */
  int ks[4] = {0, 0, 0, 0}, amb[4] = {0, 0, 0, 0}, nambig = 0;
  Cand rc[4][RC_MAX];
  unsigned rn[4];
  int res = 0; /* those resolved */
  int rwhy = -1; /* ... why not, when resolve ran (#197, RW_*) */
  /* ... those whose order named a clamped point alone, and its place (#192) */
  float rp[4][2];
  int rx[4] = {0, 0, 0, 0};
  VCacheVert dv = {0, 0, 1};
  /* vertices with no entry their renderer handed over (#189): its
   * hand-over, and those used */
  const Hk *hkc[4] = {NULL, NULL, NULL, NULL}, *hk[4] = {NULL, NULL, NULL, NULL};
  int nflat = 0; /* ... those with a place only */
  int nhk = 0;
  /* Whose polygon: the presenter's inline (s_pres), the presenter's on its
   * own thread (a vertex outside the game's walk's buffer while one of its
   * frames is open, #162; the logic draw's walk never reads s_pres_async),
   * or the logic draw's */
  const GrVertex *const gvb = s_pres ? NULL : psx_gpu_game_walk()->vb; /* (#206: asked once) */
  const int async = !s_pres && !(vertex >= gvb && vertex < gvb + MAX_VERTEX_BUFFER_SIZE) &&
                    s_pres_async;
  const int pres = s_pres || async;
  VCacheStats *const st = &g_vcache_stats[pres];
  unsigned *const work = async ? g_vcache_pres_work : g_vcache_work;
  unsigned *const cost = COST(work);
  /* where the maths of projections worked out here goes (#179): the
   * presenter's as its lookups', the logic draw's walk's own block (the
   * game thread's when it walks inside DrawOTag) */
  const Sink lk = pres ? (Sink){work, cost, st, NULL}
                       : s_rtab ? (Sink)SINK_ACC(1) : (Sink)SINK_ACC(0);

  st->polys++;
  st->verts += n;
  work[1]++;
  work[2] += (unsigned)n;
  if (!g_psyxRtpHook) { /* tex_filter only: polygons are just marked */
    for (i = 0; i < n; i++) {
      uint32_t r = VREF_POLY;
      memcpy(&vertex[i].tcx, &r, sizeof r);
    }
    return;
  }
  for (i = 0; i < n; i++) {
    uint32_t key = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
    Touch t = TOUCH0;
    const Table *tab;
    const int k = look(pres, key, &cost[VCC_LOOK_PROBES], &t, &tab);
    uint32_t tag;
    if (k >= 0)
      lz_need(tab, &lk, k); /* the projections kept under it (#193, #179) */
    tag = k >= 0 ? tab->s[k].tag : 0;
    ptab = tab;
    ref[i] = VREF_POLY;
    if (k >= 0 && T_STATE(tag) == E_VALID && T_N(tag)) {
      touch(&t, (size_t)k * sizeof(Entry) + offsetof(Entry, c), sizeof(Cand) * T_N(tag));
      if (tab->s[k].c[0].dy == FAR_MARK && !(tag & T_MULTI)) /* its position, below */
        touch(&t, offsetof(Table, far) + (size_t)(uint16_t)tab->s[k].c[0].dx * sizeof tab->far[0],
              sizeof tab->far[0]);
      clamp |= (tag & T_CLAMP) != 0;
      e[i] = tab->s[k].c;
      et[i] = tag;
      en[i] = T_N(tag);
      ref[i] |= (uint32_t)k + 1;
      st->hits++;
      if (!(tag & T_SRC) != !pres)
        st->other++;
    } else {
      e[i] = NULL;
      et[i] = 0;
      en[i] = 0;
      persp = 0;
      nmiss++;
      if (k < 0) {
        none = i;
        nnone++;
        /* its renderer's hand-over, if any (#189; used below) */
        hkc[i] = hk_look(tab, pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen,
                           key, &cost[VCC_LOOK_PROBES], &t);
      } else if (T_STATE(tag) == E_INVALID) {
        work[3]++;
        st->miss_invalid++;
        inv = 1;
      } else { /* counted below, if not resolved */
        ks[i] = k;
        amb[i] = 1;
        nambig++;
      }
    }
    cost[VCC_LOOK_LINES] += t.n;
  }
  if (n == 4 && nnone == 1 && nmiss == 1) {
    /* a quad with one vertex the renderer moved on screen (#183) */
    const uint32_t gen = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen;
    der = derive(pres, (Table *)ptab, gen, xy, e, en, none, &lk, cost, pick, &dv, &der_moved,
                 &der_snap);
    if (der) {
      persp = VREF_PERSP;
      st->hits++;
      st->derived++;
      st->der_snap += der_snap != 0;
    } else
      memset(pick, 0, sizeof pick); /* as before: drawn affine */
  }
  if (!der && nnone) {
    /* #189: vertices with no entry that their renderer handed over (after
     * #183's derivation, which keeps its quads): its place and depths, its
     * slot 0 */
    for (i = 0; i < n; i++)
      if (!e[i] && hkc[i]) {
        hk[i] = hkc[i];
        if (hk[i]->n) {
          e[i] = hk[i]->c;
          et[i] = 0;
          en[i] = hk[i]->n;
        } else
          nflat++; /* its place only: the polygon stays affine */
        nhk++;
        nnone--;
        nmiss--;
        st->hits++;
        st->hooked[hk[i]->who]++;
      }
    if (nhk && !nmiss && !nflat)
      persp = VREF_PERSP;
  }
  if (n == 4 && nambig && nnone == 1 && !nhk && !inv && nmiss == nambig + 1) {
    /* #197: a quad's one vertex with no entry and its ambiguous ones, the
     * rest found. derive's place first (an ambiguous corner's projections
     * are in the order too): the projection it names is one more of the
     * polygon's places in the order for resolve. Then the ambiguous ones'
     * depths (resolve), which give derive's depth its third corner. Kept
     * only when both are found: the place for the frame's other polygons
     * too (der_keep). */
    const uint32_t gen = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen;
    Table *const tab = (Table *)ptab;
    const Cand *e2[4];
    unsigned en2[4];
    uint32_t slotf[4], slot;
    float ov[4][3];
    VCacheVert pv;
    Der *dr;
    int one, ok = 0;
    for (i = 0; i < n; i++) {
      if (!amb[i] && i != none)
        ks[i] = (int)((ref[i] & VREF_SLOT) - 1);
      slotf[i] = i == none ? 0 : (uint32_t)ks[i];
    }
    derive_place(tab, gen, xy, slotf, none, &lk, cost, &pv, &slot, &dr, 0);
    ks[none] = slot ? (int)slot - 1 : -1; /* its projection's places: one more of the polygon's */
    if (resolve(tab, gen, ks, amb, n, rc, rn, rp, rx, cost, &one, &rwhy)) {
      rwhy = RW_SPREAD;
      for (i = 0; i < n; i++) {
        e2[i] = amb[i] ? rc[i] : e[i];
        en2[i] = amb[i] ? rn[i] : en[i];
        ov[i][0] = amb[i] && rx[i] ? rp[i][0] : 0;
        ov[i][1] = amb[i] && rx[i] ? rp[i][1] : 0;
        ov[i][2] = amb[i] ? 1.0f : 0.0f; /* drawn at its key, or a clamped point's place */
      }
      ok = derive_depth(tab, xy, e2, en2, none, slot, (const float(*)[3])ov, cost, pick, &pv);
    }
    if (ok) {
      der_keep(dr, gen);
      dv = pv;
      der = 1;
      der_moved = pv.dx != 0 || pv.dy != 0;
      der_snap = slot != 0;
      for (i = 0; i < n; i++)
        if (amb[i]) {
          e[i] = rc[i];
          en[i] = rn[i];
          et[i] = rx[i] ? (ptab->s[ks[i]].tag & ~T_MULTI) | T_CLAMP
                        : (ptab->s[ks[i]].tag | T_MULTI) & ~T_CLAMP;
          ref[i] |= (uint32_t)ks[i] + 1;
        }
      persp = VREF_PERSP;
      res = 1;
      st->hits += (unsigned)nambig + 1;
      st->resolved += (unsigned)nambig;
      st->res_order += one ? (unsigned)nambig : 0;
      st->res_der += (unsigned)nambig;
      st->derived++;
      st->der_snap += der_snap != 0;
    } else {
      memset(pick, 0, sizeof pick);
      memset(rx, 0, sizeof rx); /* as before: drawn affine, each vertex at its key */
    }
  } else if (nambig && nambig == nmiss && !nnone && !nflat) {
    /* every vertex not found an ambiguous key's (#181): its depths from
     * the projections under it near the others' */
    const uint32_t gen = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen;
    const Cand *e2[4];
    unsigned en2[4];
    int one;
    for (i = 0; i < n; i++)
      if (!amb[i])
        ks[i] = (int)((ref[i] & VREF_SLOT) - 1);
    if (resolve((Table *)ptab, gen, ks, amb, n, rc, rn, rp, rx, cost, &one, &rwhy)) {
      rwhy = RW_SPREAD;
      for (i = 0; i < n; i++) {
        e2[i] = amb[i] ? rc[i] : e[i];
        en2[i] = amb[i] ? rn[i] : en[i];
      }
      if (choose(e2, en2, k_all, n, cost, pick)) {
        for (i = 0; i < n; i++)
          if (amb[i]) {
            e[i] = rc[i];
            en[i] = rn[i];
            /* its key's integer position, or a clamped point's own (#192) */
            et[i] = rx[i] ? (ptab->s[ks[i]].tag & ~T_MULTI) | T_CLAMP
                          : (ptab->s[ks[i]].tag | T_MULTI) & ~T_CLAMP;
            ref[i] |= (uint32_t)ks[i] + 1;
          }
        persp = VREF_PERSP;
        res = 1;
        st->hits += (unsigned)nambig;
        st->resolved += (unsigned)nambig;
        st->res_order += one ? (unsigned)nambig : 0;
      } else
        memset(pick, 0, sizeof pick);
    }
  }
  if (nambig && !res) { /* not found: ambiguous */
    work[3] += (unsigned)nambig;
    st->miss_ambiguous += (unsigned)nambig;
    /* and why (#197): resolve's, or what kept it from running */
    st->res_why[rwhy >= 0 ? rwhy : nnone ? RW_NONE : inv ? RW_INVALID : RW_FLAT] +=
        (unsigned)nambig;
  }
  for (i = 0; i < n; i++) /* #192: a clamped point the order named is drawn at its place, as a
                             found clamped key's is (D195), even with the polygon affine */
    if (rx[i])
      clamp = 1;
  if (res || der) {
    /* chosen above */
  } else if (persp) {
    if (!choose(e, en, k_all, n, cost, pick)) {
      st->spread++;
      persp = 0;
    }
  }
  if (nnone && !der) { /* not found: no entry */
    st->miss_none += (unsigned)nnone;
    work[3] += (unsigned)nnone;
  }
  if (persp)
    st->persp++;
  else if (inv) {
    st->fallback++;
    work[VCW_FALLBACK]++;
  }
  if (clamp) {
    st->clamp_polys++;
    st->clamp_persp += persp != 0;
  }
  for (i = 0; i < n; i++) {
    /* (a resolved vertex's pick is among its key's projections: not one
     * of the entry's candidates) */
    uint32_t r = ref[i] | persp | (uint32_t)(res && amb[i] ? 0 : pick[i]) << VREF_CAND_SHIFT;
    if (et[i] & T_MULTI)
      r |= VREF_NOPOS;
    /* (#189: moved by its renderer; #192: a clamped point the order named) */
    if ((et[i] & T_CLAMP) || (i == none && der_moved) || hk[i] || rx[i])
      r |= VREF_CLAMP;
    memcpy(&vertex[i].tcx, &r, sizeof r);
  }
  /* The walk says where the answers go (D152): g_vcache_side at the
   * polygon's first vertex's index in its own buffer. On its own thread the
   * presenter's go to its own array at the same index (#162), as the logic
   * draw's walk fills g_vcache_side beside it. */
  if (side && async)
    side = s_pres_side ? s_pres_side + (side - g_vcache_side) : NULL;
  if (side) {
    /* the walk splits a quad 0 1 2 3 into 0 1 3 / 1 3 2 (TriangulateQuad) */
    static const int k_quad[6] = {0, 1, 3, 1, 3, 2}, k_tri[3] = {0, 1, 2};
    const int *order = n == 4 ? k_quad : k_tri;
    VCacheVert c[4];
    for (i = 0; i < n; i++) {
      c[i].dx = c[i].dy = 0;
      c[i].w = 1;
      if (rx[i]) { /* #192 */
        c[i].dx = rp[i][0];
        c[i].dy = rp[i][1];
      }
      if (e[i]) {
        if (hk[i]) { /* #189 */
          c[i].dx = hk[i]->dx;
          c[i].dy = hk[i]->dy;
        } else if (!(et[i] & T_MULTI) && !rx[i])
          cand_pos(ptab, &e[i][0], &c[i].dx, &c[i].dy);
        if (persp)
          c[i].w = e[i][pick[i]].w;
      } else if (hk[i]) { /* #189: a place only */
        c[i].dx = hk[i]->dx;
        c[i].dy = hk[i]->dy;
      } else if (i == none && (der || der_moved)) {
        c[i].dx = dv.dx;
        c[i].dy = dv.dy;
        if (der)
          c[i].w = dv.w;
      }
    }
    for (i = 0; i < (n == 4 ? 6 : 3); i++)
      side[i] = c[order[i]];
  } else if (async) {
    st->side_lost += n;
  }
}

void psx_vcache_poly_ref(void *vertex, short *const *xy, int n) {
  psx_vcache_poly(vertex, xy, n, NULL);
}

static int s_ot_done; /* psx_vcache_enable: mode != 0 */

/* the presenter's next frame (psx_vcache_pres_begin): its table's entries
 * from before are old */
static void pres_next(void) {
  Rec *const r = &s_rec[VC_PRES];
  r->count = 0;
  r->tab->nfar = 0;
  r->tab->nlfar = 0;
  r->tab->nlog = 0;
  r->tab->idx_gen = 0;
  r->tab->lc_line = 0;
  lz_next(r->tab, s_lazy); /* #193 */
  s_since_pres = 0;        /* (#179: the logic draw's then work theirs out at once) */
  if (++r->gen == VC_GEN_END) {
    table_clear(r->tab);
    r->gen = 1;
  }
}

static void done_hook(void) {
  Rec *const r = &s_rec[s_pres];
  VCacheStats *const st = &g_vcache_stats[s_pres];
  st->frames++;
  if (r->count > st->peak)
    st->peak = r->count;
  if (s_pres) { /* the presenter's: its next frame begins at psx_vcache_pres_begin */
    s_pres = 0;
    return;
  }
  acc_publish(0); /* the game thread's counts of this frame (#179) */
  r->count = 0;
  if (s_tab1) /* the other table records the next frame (D71) */
    r->tab = r->tab == &s_tab0 ? s_tab1 : &s_tab0;
  if (++r->gen == VC_GEN_END) { /* came round: no old entry may look current */
    s_tab0.stale = 1;
    if (s_tab1)
      s_tab1->stale = 1;
    r->gen = 1;
  }
  /* each table is cleared as it starts recording, not while the other
   * frame's walk may still read it */
  if (r->tab->stale)
    table_clear(r->tab);
  r->tab->nfar = 0;
  r->tab->nlfar = 0;
  r->tab->nlog = 0;
  r->tab->idx_gen = 0;
  r->tab->lc_line = 0;
  /* its projections kept for its walk (#179), unless the presenter draws
   * in-between frames */
  if (s_since_pres < 255)
    s_since_pres++;
  lz_next(r->tab, (s_lazy_logic == 2 || /* not tied to s_lazy (#198) */ (s_lazy_logic == 1 && s_since_pres > LZ_QUIET)));
}

int psx_vcache_double(void) {
  if (!s_tab1)
    s_tab1 = table_new();
  return s_tab1 ? 0 : -1;
}

void psx_vcache_frame_take(PsxVcacheFrame *f) {
  f->tab = s_rec[VC_LOGIC].tab;
  f->gen = s_rec[VC_LOGIC].gen;
}

void psx_vcache_frame_use(const PsxVcacheFrame *f) {
  if (!f && s_rtab)
    acc_publish(1); /* the walk's end: its counts (#179) */
  s_rtab = f ? (const Table *)f->tab : NULL;
  __atomic_store_n(&s_walk.busy, f != NULL, __ATOMIC_RELAXED); /* #179 */
  s_rgen = f ? f->gen : 0;
}

int psx_vcache_get(uint32_t ref, VCacheVert *out) {
  uint32_t slot = ref & VREF_SLOT, d = (ref >> VREF_CAND_SHIFT) & 3;
  const Table *t = s_rec[VC_LOGIC].tab;
  if (!slot || slot > VC_SIZE)
    return 0;
  if (d >= T_N(t->s[slot - 1].tag))
    d = 0;
  cand_pos(t, &t->s[slot - 1].c[0], &out->dx, &out->dy); /* the same for every polygon */
  out->w = t->s[slot - 1].c[d].w;
  return 1;
}

void psx_vcache_enable(int mode) {
  g_psyxRtpHook = mode >= 2 ? rtp_hook : NULL;
  g_psyxRtpValueHook = mode >= 2 ? rtp_value_hook : NULL;
  g_psyxRtpDeriveHook = mode >= 2 ? derive_hook : NULL; /* #189 */
  psx_vcache_polys = mode != 0;
  s_ot_done = mode != 0;
}

void psx_vcache_ot_done(void) {
  if (s_ot_done)
    done_hook();
}

static PsxVcacheValueHook pres_begin(int walks) {
  Rec *const r = &s_rec[VC_PRES];
  s_pres = 0;
  s_pres_async = 0; /* (the last one ended at its buffer's close, before the join) */
  if (!s_ot_done)
    return NULL;
  if (g_psyxRtpValueHook && !r->tab) { /* recording: the presenter's table, once */
    r->tab = table_new(); /* (its projections' store, #193: lz_next) */
    /* and its answers' array for its frames on its own thread (#162),
     * "not found" where its walk hasn't written (integer, affine) */
    if (r->tab && !s_pres_side) {
      VCacheVert *const side = malloc(sizeof(VCacheVert) * (MAX_VERTEX_BUFFER_SIZE + 6));
      int i;
      if (side) {
        for (i = 0; i < MAX_VERTEX_BUFFER_SIZE + 6; i++) {
          side[i].dx = side[i].dy = 0;
          side[i].w = 1;
        }
        s_pres_side = side;
      }
    }
  }
  if (!r->tab) /* tex_filter only (polygons just marked), or no memory: as before #158 */
    return NULL;
  pres_next();
  s_pres = walks != 0;
  return s_pres && g_psyxRtpValueHook ? pres_rtp_value_hook : NULL;
}

/* the presenter's derive hook (#189) on with its value hook */
PsxVcacheValueHook psx_vcache_pres_begin(int walks) {
  const PsxVcacheValueHook h = pres_begin(walks);
  if (s_pres_derive)
    *s_pres_derive = h ? pres_derive_hook : NULL;
  return h;
}

/* The presenter's frame on its own thread (#162) */
int psx_vcache_pres_async_ok(void) {
  return g_psyxRtpValueHook && s_rec[VC_PRES].tab && s_pres_side;
}

void psx_vcache_pres_detach(void) {
  if (s_pres) {
    s_pres = 0;
    s_pres_async = 1;
  }
}

void psx_vcache_pres_end(void) {
  const Rec *const r = &s_rec[VC_PRES];
  VCacheStats *const st = &g_vcache_stats[VC_PRES];
  if (!s_pres_async)
    return;
  st->frames++; /* done_hook's, for the presenter's frame */
  if (r->count > st->peak)
    st->peak = r->count;
  s_pres_async = 0;
}

const VCacheVert *psx_vcache_pres_side(void) { return s_pres_side; }

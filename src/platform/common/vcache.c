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
 * every answer is as before. Since #213 the sky's records (sky_hand.c) come
 * the same way, with no sums (NGTE_NO_SUMS: summed from the registers when
 * worked out at once, as from the words before), and the portal sky's
 * (sky_clip_hand.c) take one generation a draw, not one a sector; only the
 * register file's (rtp_hook) still pass words.
 *
 * The last misses (#202), tagged by renderer on host routes through
 * Artisans, its portal into Stone Hill and Stone Hill at 2x:
 *  - As Spyro flies into a portal, the sky through it fills the screen
 *    and its cuts at the portal's outline are most of the vertices with no
 *    entry: many sides cross one edge at the same integer pixel, each at
 *    its own exact point, and a key two hand-overs placed apart was
 *    dropped. Cuts of one renderer at one key within HK_MERGE pixels now
 *    share a place, their mean, which lies on the edge. So that nothing
 *    placed before changes, a place read from a merged one (taint) counts
 *    only where nothing else placed the key, and every hand-over's sources
 *    are first taken as before (hk_place with taint NULL).
 *  - A derived grid point at the projection the order named whose plane
 *    gives no depth (behind the eye, or 4 times off) takes that
 *    projection's own depth, chosen with the quad's other corners'.
 *  - A shadow's nudged corner or a glow's rim whose source key is
 *    ambiguous is kept as that key's (asrc), and a polygon whose vertices
 *    with no entry are all such is resolved from the frame's order, each
 *    drawn at its own key, as an ambiguous vertex is.
 * Left: 2D polygons, far mobys' clamped points among several, sky cuts
 * too far from their key or from an ambiguous end. (The particles'
 * screen-facing quads: since #209 their renderer hands over each corner
 * as an offset from the projected centre, VCD_PARTICLES.)
 *
 * Cheaper walk lookups (#206; D199 put psx_vcache_poly and choose at about
 * 30% of the logic draw's non-raster host time, and resolve grows at
 * 2x): the polygon's depth choice and resolve's depth list test
 * ratios against 4, 3.999 and 1.02 on the depths' bits where that is exact
 * (under4b, under3999b, same_depthb), the float tests deciding otherwise;
 * choose tests each candidate against 4 once; and resolve finds a key's
 * projections near the polygon's others by walking its chain and the
 * anchors, sorted, together, not each place against every anchor. Every
 * answer is as before. */
#include "psx_vcache.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <math.h>
#include <stddef.h>
#include <stdio.h> /* #211: snprintf */
#include <stdlib.h>
#include <string.h>
#include <time.h> /* #204: the quota's clock */
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#endif

#include "psx_arena.h" /* #222: g_psx_arena_base, PSX_MEM_SIZE (cx_ofs) */
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
/* #221: entries a frame for records whose probe ran out, apart from the
 * table's slots (s[VC_SIZE...]; ov_find) */
#define VC_OV 64
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
 * differently by two hand-overs, so not used (#202: but for cuts at one
 * edge's pixel, merged; and taint, asrc, below). */
#define VC_HK 1024 /* hand-overs a frame */
#define HK_MERGE 2.0f /* #202: cuts at one edge's pixel this close share a place */
typedef struct {
  uint32_t key, gen;
  float dx, dy;
  uint8_t n, who, dead, placed; /* n 0: a place only (flat: a portal face's cut) */
  Cand c[VC_CANDS];
  float sx, sy; /* #202: the places merged on one edge, added (placed: how many) */
  uint8_t taint; /* #202: made from a merged place (only where nothing else placed the key) */
  uint16_t asrc; /* #202: an alias or offset of an ambiguous key: its entry + 1 (taint set, no place) */
  uint8_t low; /* #209: a particle quad's corner: gives way to any other renderer's, and is no
                * source for one (as if not there for every other hand-over) */
} Hk;
/* #217: a world split point one of whose ends has no projection (behind the
 * eye): not placed, low set, so no hand-over reads it, and the next that
 * places the key overwrites it as an empty slot; its vertex is exempt
 * (VCX_NO_PROJ) */
#define HK_NOPROJ(h) (!(h)->placed && (h)->low && !(h)->dead)

/* A renderer context's primitives in a frame (#211, psx_vcache.h VCX_*):
 * [lo, hi), the low 24 bits of the primitive buffer's addresses (the walk
 * may read a copy in another 16 MB window, walk.c) when w24, else a record
 * pool's whole addresses (native_draw). #222: the counters' form, olo (for
 * hi - lo bytes, if ok): a w24 range's offset in the game's memory
 * (cx_ofs), which the native build's copy keeps where its low 24 bits
 * don't (neither block is 16 MB-aligned there); the placement's gates keep
 * lo/hi, as before */
#define VC_CX 128 /* ranges a frame */
#define VC_FR 256 /* #215: cuts a frame placed at their own fraction (frac_hand) */
typedef struct {
  uintptr_t lo, hi, olo;
  uint8_t cx, w24, ok;
} Cx;

/* #216: a glow's or moby shadow's own point (psx_vcache.h VCD_OWN,
 * VCD_SELF): its key and its polygon's first vertex's (anchor), its frame,
 * the projection it comes from (r, its place in the frame's order) and its
 * offset in pixels from that projection's place; dead: two different
 * points under one key and anchor. The place is worked out by the walk,
 * and only for a polygon nothing else finds (own_point). */
#define VC_OW 2048
typedef struct {
  uint32_t key, anchor, gen;
  uint16_t r;
  uint8_t dead, placed;
  int32_t ox, oy;
} Ow;

/* #228: the last projection each entry's maths was worked out for at once
 * this frame (rtp_point): its registers' generation (gte_native.h NgteProj
 * gen; 0: none, or the entry made since) and vertex, and whether it was
 * clamped. A projection the same in each (an exact repeat: a point two
 * sectors share, a grid's corner projected again) has the same answer; while
 * its entry is valid with one point, that answer is already among its
 * candidates and is logged nowhere, so the repeat changes nothing and its
 * maths is skipped. 12 bytes an entry, apart from the 40 the probe reads. */
typedef struct {
  uint32_t g;
  int16_t vx, vy, vz;
  uint16_t clamped;
} Rp;

/* #229: a world point kept as its piece's own (psx_vcache.h VCD_EDGE): a
 * split point under its piece's anchor and edge, moved to each GT3 that
 * keeps it, or a corner the grid moved, under its piece's anchor and corner
 * number. Its place (pixels from its key's pixel centre) and depths, worked
 * out when it is made; dead: two different points under one key and
 * anchor. No hand-over reads these (hk stays as it was): only the walk's
 * own-point step, for a world polygon nothing else finds, and a split
 * point's end here (sp_place). */
#define VC_SP 1024 /* (as hk: a frame's split points, their moves and moved corners; sp_full counts any not kept) */
typedef struct {
  uint32_t key, anchor, gen;
  float dx, dy;
  float w[VC_CANDS];
  uint8_t n, dead, placed;
  uint16_t r; /* a moved corner's projection, its place in the frame's order (SP_NOR: a split point) */
} Sp;
#define SP_NOR 0xFFFFu

struct Lazy; /* the projections kept for later (#193, #179; below) */

typedef struct {
  Entry s[VC_SIZE + VC_OV]; /* (#221: the overflow's after the slots) */
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
  uint16_t log[VC_LOG], prev[VC_LOG], last[VC_SIZE + VC_OV];
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
  /* #211 (last, as above): the primitives each renderer wrote in this
   * table's frame (cx_gen; psx_vc_ctx), ncx of them */
  uint32_t cx_gen;
  unsigned ncx;
  Cx cx[VC_CX];
  /* #215 (last, as above): the cuts placed at their own fraction in this
   * table's frame (VCD_T, frac_hand), apart from hk: no hand-over reads
   * them */
  Hk fr[VC_FR];
  /* #216 (last, as above): the glows' and moby shadows' own points in this
   * table's frame (VCD_OWN, VCD_SELF), apart from hk: no hand-over reads
   * them */
  Ow ow[VC_OW];
  /* #228 (last, as above): each entry's last projection worked out at once,
   * by the slot (Rp) */
  Rp rp[VC_SIZE + VC_OV];
  /* #229 (last, as above): the world's points kept as their piece's own */
  Sp sp[VC_SP];
  /* #221 (last, as above): the frame whose records last made an overflow
   * entry (the walk looks there only in that frame; 0: none) */
  uint32_t ov_gen;
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
  memset(t->fr, 0, sizeof t->fr); /* #215 */
  memset(t->ow, 0, sizeof t->ow); /* #216 */
  memset(t->sp, 0, sizeof t->sp); /* #229 */
  t->ov_gen = 0; /* #221 */
  t->cx_gen = 0;
  t->ncx = 0;
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

static void qt_line(void);

/* #218: the generations' check (below, by the renderer contexts), in host
 * builds only: compiled out of the Vita's */
#ifndef __vita__
#define VC_GEN_CHECK 1
static int s_gc = -1; /* 1: on, 0: off, -1: VC_CHECK_GEN not read yet */
static void gc_check(int kind, const NgteProj *p, unsigned vxy, unsigned vz, const void *from);
static void gc_line(void);
#else
#define VC_GEN_CHECK 0
#endif

static int s_repeat = 1;     /* psx_vcache_set_repeat (#228) */

/* #221: the overflow (VC_OV entries after the table's slots, ov_find): on
 * by default. At ~60% of the slots late in a busy frame (Stone Hill at 2x
 * draw distance, ~5,000 keys), a run of VC_PROBE (32) taken slots is common
 * enough that a record's probe runs out: the main sky's records, which come
 * last, made no entry for a vertex now and then, and every polygon of its
 * fan was missed. Such a record now makes its entry in the overflow, apart
 * from the slots, so every slot holds what it held before (a later key's
 * probe ends where it did). An overflow entry takes no place in the frame's
 * order (#183), is worked out at once (not kept, #179), is no hand-over's
 * source (those look in the slots), and the walk looks there only for a
 * SkyDraw polygon's vertex with no entry (the sky's records are its own
 * exact projections): every other polygon finds what it found before. */
static int s_ov = 1;
void psx_vcache_set_overflow(int on) { s_ov = on != 0; }
/* #228: the exact repeats whose maths was skipped, per kind of frame (each
 * added by the one thread recording that kind; the report reads) */
static unsigned s_rp_hits[2], s_rp_tried[2];
static unsigned s_rp_lz_hits; /* ... of the kept ones, as they're worked out (either thread) */

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
  { /* #228: the exact repeats skipped, since the last line */
    static unsigned hl[2], tl[2];
    unsigned h[2], n[2];
    for (t = 0; t < 2; t++) {
      const unsigned hv = __atomic_load_n(&s_rp_hits[t], __ATOMIC_RELAXED),
                     tv = __atomic_load_n(&s_rp_tried[t], __ATOMIC_RELAXED);
      h[t] = hv - hl[t], n[t] = tv - tl[t];
      hl[t] = hv, tl[t] = tv;
    }
    static unsigned zl;
    const unsigned zv = __atomic_load_n(&s_rp_lz_hits, __ATOMIC_RELAXED);
    PsyX_Log("vcache repeats: the logic draw's %u of %u projections worked out at once by name "
             "were exact repeats of their entry's last, their maths skipped; the presenter's %u "
             "of %u; %u kept ones as they were worked out (rp=%d, #228)",
             h[0], n[0], h[1], n[1], zv - zl, s_repeat);
    zl = zv;
  }
  qt_line(); /* #204 */
#if VC_GEN_CHECK
  gc_line(); /* #218 */
#endif
}

static void cx_text(const VCacheStats *s, char *buf, size_t size); /* #211, below */

/* the two kinds' counters added (peak: the larger), and the report's lines */
static void sum_report(const VCacheStats k[2], VCacheStats *out) {
  const unsigned *a = (const unsigned *)&k[0], *b = (const unsigned *)&k[1];
  unsigned *o = (unsigned *)out;
  char cx[256];
  size_t i;
  for (i = 0; i < sizeof *out / sizeof(unsigned); i++)
    o[i] = a[i] + b[i];
  out->peak = k[0].peak > k[1].peak ? k[0].peak : k[1].peak;
  cost_line();
  cx_text(out, cx, sizeof cx);
  /* #183: the quads' vertices with no entry found again from their polygon;
   * #211: those left, missed or exempt by their renderer context */
  PsyX_Log("vcache derived: %u vertices with no entry drawn with their polygon's depth (%u at the "
           "projection recorded before the renderer moved them, %u at their key); %u left with no "
           "entry (%s); %u at that projection with its own depth, the plane giving none (#202)",
           out->derived, out->der_snap, out->derived - out->der_snap, out->miss_none, cx,
           out->der_own);
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
           "(shadows %u, sky clip %u, glows %u, world splits %u, Spyro's clip %u, particles %u); %u "
           "hand-overs, "
           "%u kept, %u with a source not usable, %u placed differently, %u not kept (full); %u "
           "merged with another cut at their edge's pixel, %u an ambiguous key's alias or offset "
           "(%u vertices found so, from the frame's order) (#202)",
           out->hooked[VCD_SHADOWS] + out->hooked[VCD_SKY_CLIP] + out->hooked[VCD_GLOWS] +
               out->hooked[VCD_ENV_SPLIT] + out->hooked[VCD_PETE_CLIP] + out->hooked[VCD_PARTICLES],
           out->hooked[VCD_SHADOWS], out->hooked[VCD_SKY_CLIP], out->hooked[VCD_GLOWS],
           out->hooked[VCD_ENV_SPLIT], out->hooked[VCD_PETE_CLIP], out->hooked[VCD_PARTICLES],
           out->hk_calls, out->hk_kept,
           out->hk_nosrc, out->hk_conflict, out->hk_full, out->hk_merged,
           out->hk_ambsrc, out->res_alias);
  /* #215 */
  PsyX_Log("vcache cut fraction: %u vertices with no entry found at their cut's own fraction; "
           "%u cuts placed so, %u with an end not usable or more than 8 pixels off (#215)",
           out->hk_frac, out->fr_kept, out->fr_nosrc);
  /* #216 */
  PsyX_Log("vcache own points: %u vertices found from their renderer's own point (%u "
           "of them at an ambiguous key, and %u found ones of their polygons drawn at theirs); "
           "%u own points kept, %u with no source, %u not kept (full) (#216)",
           out->own_found, out->own_amb, out->own_over, out->own_kept, out->own_nosrc,
           out->own_full);
  /* #217 */
  PsyX_Log("vcache world: %u derived vertices at their key with a depth the order's projections "
           "named, the plane giving none; %u world splits placed from their ends' own points, "
           "%u not placed with an end behind the eye (#217)",
           out->der_named, out->hk_own, out->hk_noproj);
  /* #229 */
  PsyX_Log("vcache world own: %u vertices found from their piece's own split point or moved "
           "corner; %u kept, %u not kept (full); %u split points paired across 4x depths, %u "
           "corners the grid moved; %u world polygons found from their own points drawn affine, "
           "their depths 4x apart; %u ambiguous corners resolved with them, %u moved corners "
           "found where they weren't (the polygon affine) (#229)",
           out->sp_found, out->sp_kept, out->sp_full, out->sp_wide, out->sp_moved, out->sp_spread,
           out->sp_res, out->sp_part);
  /* #221 */
  PsyX_Log("vcache overflow: %u projections recorded in the overflow, their probe past %u slots "
           "(none recorded before); %u vertices of SkyDraw polygons found there; %u not recorded "
           "(table and overflow full) (ov=%d, #221)",
           out->ov_kept, VC_PROBE, out->ov_found, out->full, s_ov);
  /* #242 */
  PsyX_Log("vcache Spyro and shadows own: %u vertices found from their polygon's own clip point "
           "of Spyro's; %u kept, %u not kept (full); %u shadow corners' own points from their "
           "corner's projection, the source's dead (#242); %u shadow centres found from their "
           "own point under their own anchor, their own under the centre's dead (#226)",
           out->pk_found, out->pk_kept, out->pk_full, out->own_corner, out->own_centre);
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
      r->tab->rp[i].g = 0; /* #228: no projection worked out under it yet */
      r->count++;
      return (int)i;
    }
    if (s[i].key == key)
      return (int)i;
  }
  return -1;
}

/* #221: key's entry in tb's frame gen in the overflow (s[VC_SIZE...]), or
 * (add, kind) a free one made for it, or -1. Only a record whose probe ran
 * out makes one, and only the walk's lookup of a SkyDraw polygon's vertex
 * reads one, so a scan of the few each frame. Its last (#183) is VC_NOSEQ:
 * no place in the frame's order. */
static int ov_find(Table *tb, uint32_t gen, uint32_t key, int add, int kind, unsigned *probes) {
  int i, fr = -1;
  for (i = VC_SIZE; i < (int)(VC_SIZE + VC_OV); i++) {
    ++*probes;
    if (T_GEN(tb->s[i].tag) != gen) {
      if (fr < 0)
        fr = i;
    } else if (tb->s[i].key == key)
      return i;
  }
  if (!add || fr < 0)
    return -1;
  tb->s[fr].key = key;
  tb->s[fr].tag = gen << T_GEN_SHIFT | (kind == VC_PRES ? T_SRC : 0);
  tb->rp[fr].g = 0;
  tb->last[fr] = VC_NOSEQ;
  tb->ov_gen = gen;
  return fr;
}

/* psx_vcache_poly's lookup: the entry for key in the frame being walked
 * (pres: the presenter's own, in its frame), and its table */
static inline __attribute__((always_inline)) int look(int pres, uint32_t key, unsigned *probes,
                                                      Touch *t, const Table **tab) {
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

/* a native projection's sums from its registers, as pt_words from the
 * words: a record that passed none (NGTE_NO_SUMS, the sky's, #213) */
static inline void pt_np(Pt *a, const NgteProj *p, int vx, int vy, int vz) {
  a->mx = ((int64_t)p->trx << 12) + (int64_t)(p->r11 * vx) + (p->r12 * vy) + (p->r13 * vz);
  a->my = ((int64_t)p->try_ << 12) + (int64_t)(p->r21 * vx) + (p->r22 * vy) + (p->r23 * vz);
  a->mz = ((int64_t)p->trz << 12) + (int64_t)(p->r31 * vx) + (p->r32 * vy) + (p->r33 * vz);
  a->vx = vx, a->vy = vy, a->vz = vz;
  a->tr = p->trx || p->try_ || p->trz;
  a->ofx = p->ofx, a->ofy = p->ofy, a->h = p->h;
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
 * cost. Returns whether the projection was clamped (its answer's, for the
 * repeat's record, #228). */
static int rtp_apply(Table *tb, const Sink *k, const Pt *a, uint32_t key, int sz, int sf, int i,
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
    return clamped;
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
      return clamped;
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
    return clamped;
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
  return clamped;
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
  uint16_t head[VC_SIZE + VC_OV]; /* each pending entry's list (its newest), + 1 */
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
  /* #204: when the last walk began and ended by the walk's clock, and how
   * many have ended (written by the walk, read by the game thread at its
   * frame's end) */
  unsigned ends;
  uint32_t start_us, end_us;
} __attribute__((aligned(64))) s_walk;
#define LZ_QUIET 2 /* ... more than this: no in-between frames */

/* A quota of the logic draw's projections kept for its walk
 * (vc_lazy_logic=3, #204).
 *
 * The idle rule (1, D188) keeps a projection only while the last frame's
 * walk has ended. The walk of frame N runs through the first part of frame
 * N + 1's records, so the walk's time after it ends, up to the game
 * thread's join, is left: in the owner's Stone Hill window the walk was
 * 68% busy and 76% of the projections were still worked out at once on
 * the game thread (D199). Keeping every one (2) overloaded the walk (D188).
 *
 * So the game thread also keeps a frame's first q projections while the
 * walk is still running: kept, then (past q, the walk still running)
 * worked out at once, the entry's kept ones first (lz_need: the same
 * order, so the same answers), then kept again once the walk has ended,
 * as the idle rule does. q comes from the walk's idle time in recent
 * frames: the walk's thread stamps its start and end
 * (psx_vcache_frame_use), the game thread stamps every QT_EVERY records,
 * and at a frame's end (done_hook) the walk that ran beside it was idle
 *   idle = the frame's last stamp - the walk's end
 * before the game thread's last projections. Those come just before its
 * DrawSync, where it joins the walk (D196), so this is the time the walk
 * could still have taken without the game thread waiting; the wait for the
 * vblank after the join isn't counted (a walk running into it would hold
 * the game thread at the join, the `parse N:` line's wait). The idle is
 * kept at a headroom of an eighth of the walk's window (its start to that
 * stamp), at least QT_HEAD_MIN us:
 *  - past the headroom in each of the last QT_HIST frames: q grows by the
 *    lesser over QT_US_UP us a projection (about the walk's maths for a
 *    kept one in Vita3K, 1-1.5 us, a third of them never needed), at most
 *    by half again plus QT_SLACK;
 *  - under it: q falls by the shortfall over QT_US_DOWN us, at most by a
 *    quarter (a late frame, which Vita3K's timing gives one in ten
 *    frames, doesn't empty it).
 * q is held to the frame's records plus QT_SLACK, so a light stretch
 * doesn't leave a quota that overloads the next heavy frame. With no walk
 * on its own thread (in place: the host without --walk-lazy,
 * parse_thread=0) every projection is kept, as the idle rule keeps them
 * there. Which are kept never changes an answer. */
#define LZ_QUOTA 3
#define QT_EVERY 128 /* records a stamp */
#define QT_HIST 2
#define QT_HEAD_MIN 250
#define QT_US_UP 1
#define QT_US_DOWN 1
#define QT_SLACK 256
#define QT_START 256
static inline uint32_t qt_now(void) {
#ifdef __vita__
  return (uint32_t)sceKernelGetProcessTimeLow();
#elif defined(CLOCK_MONOTONIC)
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint32_t)t.tv_sec * 1000000u + (uint32_t)(t.tv_nsec / 1000);
#else
  return 0; /* no clock: q only falls */
#endif
}
/* the game thread's */
static struct {
  unsigned q;            /* this frame's quota */
  unsigned n;            /* this frame's records (a stamp every QT_EVERY) */
  uint32_t last;         /* the last stamp */
  unsigned ends;         /* s_walk.ends at the last frame's end */
  int hist[QT_HIST], nh; /* the last frames' idle past the headroom, us */
} s_qt = {QT_START, 0, 0, 0, {0}, 0};
/* What it did, never reset (the game thread adds, the report reads): the
 * frames measured, the sum of their next quotas, their idle us (ended
 * before the last stamp, and after it), the walk still running at the last
 * stamp, the frames with no walk on its own thread. */
enum { QS_FRAMES, QS_Q, QS_IDLE_POS, QS_IDLE_NEG, QS_LATE, QS_NOWALK, QS_N };
static unsigned s_qstat[QS_N];

void psx_vcache_set_lazy(int on) { s_lazy = on != 0; }
/* #228: skip an exact repeat's maths (on by default; 0 works every one out) */
void psx_vcache_set_repeat(int on);
void psx_vcache_set_repeat(int on) { s_repeat = on != 0; }
void psx_vcache_set_lazy_logic(int mode) { s_lazy_logic = mode < 0 ? 0 : mode > 3 ? 3 : mode; }

/* The game thread, at the end of a frame that kept (done_hook, records:
 * its projections): the next frame's quota, from the walk that ran beside
 * this frame */
static void qt_frame_end(unsigned records) {
  const unsigned ends = __atomic_load_n(&s_walk.ends, __ATOMIC_ACQUIRE);
  unsigned q = s_qt.q, i;
  int idle, window, head, e, emin;
  if (ends == s_qt.ends) { /* no walk ended on its thread: it runs in place */
    s_qt.q = VC_LOG;
    s_qt.nh = 0;
    __atomic_fetch_add(&s_qstat[QS_NOWALK], 1, __ATOMIC_RELAXED);
    return;
  }
  s_qt.ends = ends;
  if (s_qt.n < QT_EVERY) /* no stamp to place the walk's end against */
    return;
  idle = (int)(s_qt.last - s_walk.end_us);
  window = (int)(s_qt.last - s_walk.start_us);
  head = window / 8 > QT_HEAD_MIN ? window / 8 : QT_HEAD_MIN;
  e = idle - head;
  for (i = QT_HIST - 1; i > 0; i--)
    s_qt.hist[i] = s_qt.hist[i - 1];
  s_qt.hist[0] = e;
  if (s_qt.nh < QT_HIST)
    s_qt.nh++;
  emin = e;
  for (i = 1; i < (unsigned)s_qt.nh; i++)
    if (s_qt.hist[i] < emin)
      emin = s_qt.hist[i];
  if (q > records + QT_SLACK) /* (a light frame's: the quota didn't bind) */
    q = records + QT_SLACK;
  if (e < 0) {
    unsigned down = (unsigned)(-e) / QT_US_DOWN;
    if (down > q / 4) /* (a late frame, as Vita3K has now and then: a quarter at most) */
      down = q / 4;
    q -= down;
  } else if (emin > 0 && s_qt.nh == QT_HIST) {
    unsigned up = (unsigned)emin / QT_US_UP;
    if (up > q / 2 + QT_SLACK)
      up = q / 2 + QT_SLACK;
    q += up;
  }
  s_qt.q = q < VC_LOG ? q : VC_LOG;
  __atomic_fetch_add(&s_qstat[QS_FRAMES], 1, __ATOMIC_RELAXED);
  __atomic_fetch_add(&s_qstat[QS_Q], s_qt.q, __ATOMIC_RELAXED);
  if (idle >= 0)
    __atomic_fetch_add(&s_qstat[QS_IDLE_POS], (unsigned)idle, __ATOMIC_RELAXED);
  else {
    __atomic_fetch_add(&s_qstat[QS_IDLE_NEG], (unsigned)-idle, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_qstat[QS_LATE], 1, __ATOMIC_RELAXED);
  }
}

/* the quota's line (#204), since the last one: with the cost lines */
static void qt_line(void) {
  if (s_lazy_logic == LZ_QUOTA) {
    static unsigned ql[QS_N];
    unsigned d[QS_N], f, i;
    for (i = 0; i < QS_N; i++) {
      const unsigned v = __atomic_load_n(&s_qstat[i], __ATOMIC_RELAXED);
      d[i] = v - ql[i];
      ql[i] = v;
    }
    f = d[QS_FRAMES];
    PsyX_Log("vcache quota: %u frames measured, a quota of %u projections a frame on average kept "
             "while the walk still runs; the "
             "walk ended %u us before the game thread's last projections on average (%u frames "
             "still running then, %u us after on average); %u frames with the walk in place "
             "(vc_lazy_logic=3, #204)",
             f, f ? d[QS_Q] / f : 0, f > d[QS_LATE] ? d[QS_IDLE_POS] / (f - d[QS_LATE]) : 0,
             d[QS_LATE], d[QS_LATE] ? d[QS_IDLE_NEG] / d[QS_LATE] : 0, d[QS_NOWALK]);
  }
}

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

/* #228: slot i's projection with registers of generation g (sf set) and
 * vertex vx, vy, vz is an exact repeat of the last one worked out under it
 * (Rp) while the entry is valid with one point: then it would change
 * nothing (rtp_point's note), and only its counts are kept. 1: skipped. */
static inline int rp_skip(Table *tb, const Sink *k, int i, uint32_t g, int vx, int vy, int vz) {
  const Rp *const r = &tb->rp[i];
  const uint32_t tag = tb->s[i].tag;
  if (!g || r->g != g || r->vx != vx || r->vy != vy || r->vz != vz ||
      T_STATE(tag) != E_VALID || (tag & (T_MULTI | T_PEND)))
    return 0;
  if (k->rec)
    k->rec[VCR_WORKED]++;
  if (r->clamped) {
    k->work[VCW_CLAMPED]++;
    k->st->clamped++;
  }
  return 1;
}
/* ... and the one worked out, for the next */
static inline void rp_keep(Table *tb, int i, uint32_t g, int vx, int vy, int vz, int clamped) {
  Rp *const r = &tb->rp[i];
  r->g = g;
  r->vx = (int16_t)vx, r->vy = (int16_t)vy, r->vz = (int16_t)vz;
  r->clamped = (uint16_t)clamped;
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
    const int *const w = lz->proj[z->proj & ~LZ_SF];
    /* #228: a native projection's registers kept by generation (lz_add,
     * same_ngte: the words are that generation's); 0 for the words' */
    const uint32_t g = s_repeat && (z->proj & LZ_SF) ? (uint32_t)w[PROJ_GEN] : 0;
    if (g && rp_skip(tb, k, i, g, z->vx, z->vy, z->vz))
      __atomic_fetch_add(&s_rp_lz_hits, 1, __ATOMIC_RELAXED);
    else {
      Pt a;
      int clamped;
      pt_words(&a, w, z->vx, z->vy, z->vz);
      clamped = rtp_apply(tb, k, &a, s->key, z->sz, (z->proj & LZ_SF) != 0, i, z->q, &t);
      if (g)
        rp_keep(tb, i, g, z->vx, z->vy, z->vz, clamped);
    }
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
 * proj NULL), or with none (o NULL: the sky's, #213, summed here from np
 * when worked out at once). Inlined into each caller, so each keeps only
 * its own forms. */
static inline __attribute__((always_inline)) void rtp_point(int kind, const Sink *k,
                                                           const int *proj, const NgteProj *np,
                                                           const NgteRtp *o, int vx, int vy,
                                                           int vz, uint32_t key, int sz, int sf) {
  unsigned *const cost = k->cost;
  Rec *const rec = &s_rec[kind];
  Table *const tb = rec->tab;
  Touch t = TOUCH0;
  int i, q = -1, ov = 0;

  k->st->records++;
  k->work[VCW_PROJ]++;
  /* The entry first (made if it's new): a key already ambiguous stays so,
   * whatever this projection is, so it needs none of the maths below. */
  i = find(rec, kind, key, 1, &cost[VCC_REC_PROBES], &t);
  if (i < 0 && s_ov && rec->count < VC_MAX &&
      (i = ov_find(tb, rec->gen, key, 1, kind, &cost[VCC_REC_PROBES])) >= 0) {
    /* #221: the probe ran out; its entry in the overflow, worked out at
     * once, with no place in the frame's order */
    k->st->ov_kept++;
    ov = 1;
  }
  if (i < 0) {
    k->st->full++;
    cost[VCC_REC_LINES] += t.n;
    return;
  }
  if (!ov && tb->nlog < VC_LOG) { /* its place in the frame's order (#183) */
    q = (int)tb->nlog++;
    tb->log[q] = (uint16_t)i;
    cost[VCC_REC_LINES] += (q & 15) == 0; /* written in order: a line every 16 */
  }
  /* #193, #179: the logic draw's kept for its walk only while the walk is
   * idle: with the last frame's walk still running, the walk is the
   * thread short of time, so the game thread works them out itself.
   * #204: and (vc_lazy_logic=3) the frame's first s_qt.q, whatever the
   * walk is doing */
  if (tb->lazy && !ov) { /* (#221: the overflow's at once) */
    int keep;
    if (kind == VC_PRES)
      keep = 1;
    else if (s_lazy_logic == LZ_QUOTA) {
      keep = tb->lz->n < s_qt.q || !__atomic_load_n(&s_walk.busy, __ATOMIC_RELAXED);
      if ((++s_qt.n & (QT_EVERY - 1)) == 0)
        s_qt.last = qt_now();
    } else
      keep = !__atomic_load_n(&s_walk.busy, __ATOMIC_RELAXED);
    if (keep) {
      unsigned lines = 0;
      if (lz_add(tb, i, q, proj, np, vx, vy, vz, sz, sf, &lines)) {
        cost[VCC_REC_LINES] += t.n; /* (the table's: the store's apart, #179) */
        if (k->rec) {
          k->rec[VCR_KEPT]++;
          k->rec[VCR_STORE] += lines;
        }
        return;
      }
    }
    /* no room, or past the quota: the entry's list first, then this one
     * (the idle rule alone never has one here: the walk only ends in a
     * frame, so its kept ones come after) */
    lz_need(tb, k, i);
  }
  if (k->rec)
    k->rec[VCR_ONCE]++;
  /* #228: an exact repeat of the entry's last projection worked out at once
   * (the same registers by generation, the same vertex; so the same key, SZ3
   * and answer). While the entry is valid with one point, that answer is
   * among its candidates (one point: every candidate before it was at its
   * place at another depth, so the repeat stops at the same one), the entry
   * keeps no answer per projection (#181's log is for keys with different
   * points), and so the repeat would change nothing: only its counts. Its
   * registers are kept by generation as the store's (same_ngte, #205). */
  if (np && np->gen && s_repeat) {
    s_rp_tried[kind]++;
    if (rp_skip(tb, k, i, np->gen, vx, vy, vz)) {
      s_rp_hits[kind]++;
      cost[VCC_REC_LINES] += t.n;
      return;
    }
  }
  {
    Pt a;
    int clamped;
    if (np && o)
      pt_ngte(&a, np, o, vx, vy, vz);
    else if (np)
      pt_np(&a, np, vx, vy, vz);
    else
      pt_words(&a, proj, vx, vy, vz);
    clamped = rtp_apply(tb, k, &a, key, sz, sf, i, q, &t);
    if (np && s_repeat) /* #228: the entry's last, for a repeat */
      rp_keep(tb, i, np->gen, vx, vy, vz, clamped);
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

/* The same projection given as values (D89), for whatever calls the value
 * hook. Since #205 gte_native.h's RTPS and RTPT, and since #213 the sky's
 * records (sky_hand.c), call ngte_vcache (below) instead; the hook being
 * set still says that the cache records. */
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
#if VC_GEN_CHECK /* #218 (host builds) */
  if (__builtin_expect(s_gc != 0, 0))
    gc_check(VC_LOGIC, p, vxy, vz, __builtin_return_address(0));
#endif
  rtp_point(VC_LOGIC, &k_game, NULL, p, (o->flag & NGTE_NO_SUMS) ? NULL : o, ngte_vx(vxy),
            ngte_vy(vxy), ngte_vz(vz), o->sxy, (int)o->sz3, 1);
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
#if VC_GEN_CHECK /* #218 (host builds) */
  if (__builtin_expect(s_gc != 0, 0))
    gc_check(VC_PRES, p, vxy, vz, __builtin_return_address(0));
#endif
  pres_sink(&k);
  rtp_point(VC_PRES, &k, NULL, p, (o->flag & NGTE_NO_SUMS) ? NULL : o, ngte_vx(vxy),
            ngte_vy(vxy), ngte_vz(vz), o->sxy, (int)o->sz3, 1);
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
      e->n = e->dead = e->placed = e->taint = e->low = 0;
      e->asrc = 0;
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
static int hk_src0(Rec *rec, int kind, const Sink *k, uint32_t key, float *x, float *y, float *w,
                   int flat, int *exact, int *taint, unsigned *cost, Touch *t) {
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
    const Hk *h = hk_slot(tb, rec->gen, key, 0, &cost[VCC_REC_PROBES], t);
    int gone;
    if (h && h->low)
      h = NULL; /* #209: a particle quad's corner: as if not there */
    gone = h && h->dead;
    if (h && (h->asrc || (!taint && h->taint)))
      h = NULL; /* #202: placed only from a merged place, or no place: as if not there */
    else if (h && !taint && h->placed > 1)
      gone = 1; /* #202: merged: as before, when two places left it unused */
    if (!h || gone) {
      if (!flat || h)
        return -1;
      *exact = 0; /* 2D */
      *x = *y = 0;
      n = 0;
    } else {
      n = h->n;
      *x = h->dx;
      *y = h->dy;
      if (taint) /* #202 (NULL: neither, above) */
        *taint |= h->placed > 1 || h->taint;
      for (j = 0; j < n; j++)
        w[j] = h->c[j].w;
    }
  }
  *x += kx;
  *y += ky;
  return (int)n;
}

static Ow *own_slot(Table *tb, uint32_t gen, uint32_t key, uint32_t anchor, int add,
                    unsigned *probes, Touch *t);
static int res_place(const Table *tab, const Entry *en, unsigned r, float *x, float *y, float *w,
                     int *alt, int *cl);
static inline int near8(float ax, float ay, float bx, float by);

/* #217: a 3D point's end by its own point under anchor (psx_vcache.h
 * VCD_OWN on VCD_PERSP; the world's pieces split on the last list name
 * their corners as they project them): that projection's own place on
 * screen (pixels; a clamped one's exact place, as a found clamped key is
 * drawn) and depths. Returns the depths' count, -1 none usable. */
static int log_point(Table *tb, const Sink *k, unsigned r, int ox, int oy, float *x, float *y,
                     float *w, Touch *t);
static int own_end(Rec *rec, const Sink *k, uint32_t key, uint32_t anchor, float *x, float *y,
                   float *w, unsigned *cost, Touch *t) {
  Table *const tb = rec->tab;
  const Ow *const o = own_slot(tb, rec->gen, key, anchor, 0, &cost[VCC_REC_PROBES], t);
  if (!o || !o->placed || o->dead || o->r >= tb->nlog)
    return -1;
  return log_point(tb, k, o->r, o->ox, o->oy, x, y, w, t);
}

/* #217 (own_end's, #229: and a moved corner's): log place r's own place on
 * screen (pixels), offset (ox, oy), and depths; -1 none usable */
static int log_point(Table *tb, const Sink *k, unsigned r, int ox, int oy, float *x, float *y,
                     float *w, Touch *t) {
  const Entry *en;
  float px, py, wv;
  int e, alt, cl, rk;
  unsigned m, n = 0;
  e = (int)LOG_E(tb->log[r]);
  en = &tb->s[e];
  lz_need(tb, k, e);
  touch(t, (size_t)e * sizeof(Entry) + offsetof(Entry, c), sizeof(Cand) * VC_CANDS);
  rk = res_place(tb, en, r, &px, &py, &wv, &alt, &cl);
  if (!rk)
    return -1;
  if (rk == 2) { /* candidate 0's point: the candidates there (resolve's) */
    for (m = 0; m < T_N(en->tag); m++) {
      const Cand *const cm = &en->c[m];
      if (cm->dy == FAR_MARK || en->c[0].dy == FAR_MARK) {
        float cx, cy;
        cand_pos(tb, cm, &cx, &cy);
        if (!near8(cx, cy, px, py))
          continue;
      } else if (cm->dx - en->c[0].dx > 32 || en->c[0].dx - cm->dx > 32 ||
                 cm->dy - en->c[0].dy > 32 || en->c[0].dy - cm->dy > 32)
        continue;
      w[n++] = cm->w;
    }
  } else {
    w[n++] = wv;
    if (alt)
      w[n++] = flt(wv * (1.0f / 16));
  }
  if (!n)
    return -1;
  *x = flt(px + ((short)(en->key & 0xFFFF) + 0.5f) + (float)ox);
  *y = flt(py + ((short)(en->key >> 16) + 0.5f) + (float)oy);
  return (int)n;
}

/* #229: (key, anchor)'s world point kept as its piece's own in tb's frame
 * gen, or (add) a free one for it */
static Sp *sp_slot(Table *tb, uint32_t gen, uint32_t key, uint32_t anchor, int add,
                   unsigned *probes, Touch *t) {
  uint32_t h = (key * 0x9E3779B1u ^ anchor * 0x85EBCA77u) >> 21 & (VC_SP - 1), pr;
  for (pr = 0; pr < 8; pr++, h = (h + 1) & (VC_SP - 1)) {
    Sp *const o = &tb->sp[h];
    ++*probes;
    touch(t, offsetof(Table, sp) + (size_t)h * sizeof(Sp), sizeof(Sp));
    if (o->gen != gen) {
      if (!add)
        return NULL;
      o->key = key;
      o->anchor = anchor;
      o->gen = gen;
      o->n = o->dead = o->placed = 0;
      return o;
    }
    if (o->key == key && o->anchor == anchor)
      return o;
  }
  return NULL;
}

/* #229: keep key's point under anchor (place dx, dy from its pixel centre,
 * depths w): a second, different one leaves neither; the same one adds its
 * depths. #242: pk, Spyro's clip point (counted apart) */
static void sp_put(Rec *rec, const Sink *k, uint32_t key, uint32_t anchor, float dx, float dy,
                   const float *w, unsigned n, unsigned r, Touch *t, int pk) {
  Sp *const o = sp_slot(rec->tab, rec->gen, key, anchor, 1, &k->cost[VCC_REC_PROBES], t);
  unsigned i, j;
  if (!o) {
    if (pk)
      k->st->pk_full++;
    else
      k->st->sp_full++;
    return;
  }
  if (o->placed) {
    if (o->dead)
      return;
    if (!(o->dx - dx <= 0.125f && o->dx - dx >= -0.125f && o->dy - dy <= 0.125f &&
          o->dy - dy >= -0.125f)) {
      o->dead = 1;
      return;
    }
  } else {
    o->dx = dx;
    o->dy = dy;
    o->r = (uint16_t)r;
    o->placed = 1;
    if (pk)
      k->st->pk_kept++;
    else
      k->st->sp_kept++;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < o->n; j++)
      if (same_depth(o->w[j], w[i], &k->cost[VCC_REC_DIV]))
        break;
    if (j == o->n && o->n < VC_CANDS)
      o->w[o->n++] = w[i];
  }
}

/* #229: a split point's end by its piece's own point kept here (a split
 * point of the piece it was split from, or a corner the grid moved): its
 * place on screen (pixels) and depths; -1 none usable */
static int sp_end(Rec *rec, const Sink *k, uint32_t key, uint32_t anchor, float *x, float *y,
                  float *w, unsigned *cost, Touch *t) {
  const Sp *const o = sp_slot(rec->tab, rec->gen, key, anchor, 0, &cost[VCC_REC_PROBES], t);
  unsigned j;
  (void)k;
  if (!o || !o->placed || o->dead || !o->n)
    return -1;
  *x = flt(o->dx + ((short)(key & 0xFFFF) + 0.5f));
  *y = flt(o->dy + ((short)(key >> 16) + 0.5f));
  for (j = 0; j < o->n; j++)
    w[j] = o->w[j];
  return (int)o->n;
}

/* hk_src0 (above); #217: with own, an end it can't use (an ambiguous or
 * invalid key, or none with no hand-over) by its own point under anchor
 * (own_end), which taints what is made from it; #229: own 2, else by its
 * piece's own point kept apart (sp_end) */
static int hk_src(Rec *rec, int kind, const Sink *k, uint32_t key, float *x, float *y, float *w,
                  int flat, int *exact, int *taint, unsigned *cost, Touch *t, int own,
                  uint32_t anchor) {
  int n = hk_src0(rec, kind, k, key, x, y, w, flat, exact, taint, cost, t);
  if (n >= 0 || !own)
    return n;
  n = own_end(rec, k, key, anchor, x, y, w, cost, t);
  if (n <= 0 && own == 2)
    n = sp_end(rec, k, key, anchor, x, y, w, cost, t);
  if (n > 0) {
    *exact = 1;
    if (taint)
      *taint = 1;
  }
  return n > 0 ? n : -1;
}

/* The depths of a point the fraction f of the way from a to b (depths aw,
 * bw): each of a's with b's nearest (one scale), in 3D (persp) or on
 * screen (1/w linear); into w (count returned), and the nearest pair's in
 * *pa, *pb. */
static unsigned hk_pair(const float *aw, int na, const float *bw, int nb, float f, int persp,
                        float *w, float *pa, float *pb, unsigned *cost, int *wide) {
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
  if (!n && wide && persp) {
    /* #229 (a split point kept as its piece's own only): no pair under 4
     * times apart, the nearest pair however far (a piece from near the
     * eye to far off: each end's one depth) */
    float bwd = 0;
    int ib = -1, kb = -1;
    for (i = 0; i < na; i++)
      for (k = 0; k < nb; k++) {
        const float r = ratio(aw[i], bw[k]);
        cost[VCC_REC_DIV]++;
        if (ib < 0 || r < bwd) {
          bwd = r;
          ib = i;
          kb = k;
        }
      }
    if (ib >= 0 && aw[ib] > 0 && bw[kb] > 0) {
      w[n++] = flt((1.0f - f) * aw[ib] + f * bw[kb]);
      *pa = aw[ib];
      *pb = bw[kb];
      *wide = 1;
    }
  }
  return n;
}

/* A hand-over's place (pixels from its key's pixel centre) and depths w
 * (*no of them) from its sources: 0, or -1 when no source is usable.
 * taint (#202) NULL: the sources as they were before #202 (a merged place
 * not usable, a place made from one not there); else any, and *taint set
 * when a merged place, or one made from it, was read. own (#217): a's and
 * b's own points, under q's corners numbered in how (psx_vcache.h
 * VCD_ENDS), for an end hk_src0 can't use; #229: own 2, else by its
 * piece's own point kept apart (sp_end), and a 3D point's ends paired
 * however far apart their depths are (*widep set when so; only for the
 * point kept as its piece's own, never for hk). */
static int hk_place(Rec *rec, int kind, const Sink *k, int how, uint32_t key, uint32_t a,
                    uint32_t b, int p, int q, int *taint, float *dxo, float *dyo, float *w,
                    unsigned *no, unsigned *cost, Touch *t, int own, int *widep) {
  const int form = how & 15, two = form == VCD_LERP || form == VCD_PERSP, cut = form == VCD_LERP;
  float ax, ay, bx = 0, by = 0, aw[VC_CANDS], bw[VC_CANDS], x, y, dx, dy;
  float lim = 1024, pa = 0, pb = 0;
  int na, nb = 0, ea, eb = 0, wide = 0;
  unsigned n = 0, i;
  na = hk_src(rec, kind, k, a, &ax, &ay, aw, cut, &ea, taint, cost, t, own,
              VCD_CORNER(q, VCD_ENDS_A(how)));
  if (na >= 0 && two)
    nb = hk_src(rec, kind, k, b, &bx, &by, bw, cut, &eb, taint, cost, t, own,
                VCD_CORNER(q, VCD_ENDS_B(how)));
  if (na < 0 || nb < 0 || (form == VCD_PERSP && (!na || !nb)))
    return -1;
  if (form == VCD_LERP) {
    /* where the side a-b crosses the line through keys p and q (the
     * portal's outline edge): each end exact where it has a projection */
    float ex0, ey0, ex1, ey1, ew[VC_CANDS], nx, ny, den, f;
    int e0, e1;
    if (hk_src(rec, kind, k, (uint32_t)p, &ex0, &ey0, ew, 1, &e0, taint, cost, t, 0, 0) < 0 ||
        hk_src(rec, kind, k, (uint32_t)q, &ex1, &ey1, ew, 1, &e1, taint, cost, t, 0, 0) < 0 ||
        !(ea | eb | e0 | e1))
      return -1; /* all four 2D: nothing to place exactly */
    nx = ey0 - ey1;
    ny = ex1 - ex0;
    den = nx * (bx - ax) + ny * (by - ay);
    if (!(den > 1e-6f || den < -1e-6f))
      return -1;
    f = (nx * (ex0 - ax) + ny * (ey0 - ay)) / den;
    cost[VCC_REC_DIV]++;
    x = ax + f * (bx - ax);
    y = ay + f * (by - ay);
    lim = 8; /* the PS1's cut is within a pixel or two of it */
    if (na && nb)
      n = hk_pair(aw, na, bw, nb, f, 0, w, &pa, &pb, cost, NULL);
  } else if (form == VCD_PERSP) {
    /* the 3D point f of the way: the place from the nearest pair (another
     * scale's pair gives the same) */
    const float f = (float)p * (1.0f / 4096);
    float ka, kb, r;
    n = hk_pair(aw, na, bw, nb, f, 1, w, &pa, &pb, cost, own == 2 ? &wide : NULL);
    if (!n)
      return -1;
    ka = (1.0f - f) * pa;
    kb = f * pb;
    r = 1.0f / (ka + kb);
    cost[VCC_REC_DIV]++;
    x = (ax * ka + bx * kb) * r;
    y = (ay * ka + by * kb) * r;
    /* #217: ends named by their projections: a clamped end's exact place
     * may lie far off the screen, and so may the 3D point, far from the
     * PS1's key */
    if (own)
      lim = 16384;
  } else { /* one point: its depths, its place (and the offset) */
    x = ax + (form == VCD_OFFSET ? (float)p : 0.0f);
    y = ay + (form == VCD_OFFSET ? (float)q : 0.0f);
    for (i = 0; i < (unsigned)na; i++)
      w[n++] = aw[i];
  }
  dx = flt(x - ((short)(key & 0xFFFF) + 0.5f));
  dy = flt(y - ((short)(key >> 16) + 0.5f));
  if (!(dx < lim && dx > -lim && dy < lim && dy > -lim)) /* not this key's point */
    return -1;
  *dxo = dx;
  *dyo = dy;
  *no = n;
  if (widep)
    *widep = wide;
  return 0;
}

/* #242: Spyro's clip point (VCD_KEEP) where hk_place's sources give none:
 * an end hk_src0 can't use (two of his vertices on one key) at its own
 * projection, named under VCD_PETE_V(its index, in how's VCD_PETE_ENDS);
 * the 3D point p / 4096 of the way, as hk_place's. 0, or -1 none usable */
static int pk_place(Rec *rec, int kind, const Sink *k, int how, uint32_t key, uint32_t a,
                    uint32_t b, int p, float *dxo, float *dyo, float *w, unsigned *no,
                    unsigned *cost, Touch *t) {
  const float f = (float)p * (1.0f / 4096);
  float ax, ay, bx = 0, by = 0, aw[VC_CANDS], bw[VC_CANDS], pa = 0, pb = 0, ka, kb, r, dx, dy;
  int na, nb = -1, ea, eb, taint = 0;
  unsigned n;
  na = hk_src(rec, kind, k, a, &ax, &ay, aw, 0, &ea, &taint, cost, t, 1,
              VCD_PETE_V(VCD_PETE_ENDS_A(how)));
  if (na > 0)
    nb = hk_src(rec, kind, k, b, &bx, &by, bw, 0, &eb, &taint, cost, t, 1,
                VCD_PETE_V(VCD_PETE_ENDS_B(how)));
  if (na <= 0 || nb <= 0)
    return -1;
  n = hk_pair(aw, na, bw, nb, f, 1, w, &pa, &pb, cost, NULL);
  if (!n)
    return -1;
  ka = (1.0f - f) * pa;
  kb = f * pb;
  r = 1.0f / (ka + kb);
  cost[VCC_REC_DIV]++;
  dx = flt((ax * ka + bx * kb) * r - ((short)(key & 0xFFFF) + 0.5f));
  dy = flt((ay * ka + by * kb) * r - ((short)(key >> 16) + 0.5f));
  if (!(dx < 1024 && dx > -1024 && dy < 1024 && dy > -1024))
    return -1;
  *dxo = dx;
  *dyo = dy;
  *no = n;
  return 0;
}

/* #215: a cut placed at its own fraction (psx_vcache.h VCD_T), kept in the
 * frame's fr, apart from the hand-overs (hk). The PS1 cuts a portal sky
 * triangle's side where it crosses the portal outline's edge, from integer
 * ends. Where the cut through the exact edge's line leaves the key with no
 * place, the side's own fraction still gives one. Tagged on host route P,
 * those keys were:
 *  - the cut far from its key (8 to 64 pixels, half of them past the side's
 *    ends): the outline is an arc of edges a few pixels long, nearly in
 *    line, so a cut on one edge's line just past its end is still inside
 *    the next, and the exact ends' line, extended, runs pixels away from
 *    the integer one;
 *  - the side's outer end an ambiguous key (sky sectors' copies of a
 *    border vertex, a fraction of a pixel apart);
 *  - two cuts at one key more than HK_MERGE apart.
 * t of the way between the side's exact ends is on the side the
 * neighbouring sky triangles draw, within a pixel or so of the PS1's cut
 * (the ends are each within half a pixel of their keys), and needs no edge.
 * So that every polygon found before stays as it was, such a place is no
 * source for a hand-over (hk_src never reads fr), is made only where the
 * hand-overs leave the key with none, and a polygon takes it only for a
 * vertex nothing else finds (psx_vcache_poly). */

#define FR_NEAR 256 /* the log places an ambiguous end is looked for in: a sector's (resolve's VC_NEAR) */

/* key's place in tab's frame gen, or (add) a free one for it */
static Hk *fr_slot(Table *tb, uint32_t gen, uint32_t key, int add, unsigned *probes, Touch *t) {
  uint32_t h = hash(key) & (VC_FR - 1), p;
  for (p = 0; p < 8; p++, h = (h + 1) & (VC_FR - 1)) {
    Hk *const e = &tb->fr[h];
    ++*probes;
    touch(t, offsetof(Table, fr) + (size_t)h * sizeof(Hk), 8);
    if (e->gen != gen) {
      if (!add)
        return NULL;
      e->key = key;
      e->gen = gen;
      e->n = e->dead = e->placed = e->taint = e->low = 0;
      e->asrc = 0;
      return e;
    }
    if (e->key == key)
      return e;
  }
  return NULL;
}

static int res_place(const Table *tab, const Entry *en, unsigned r, float *x, float *y, float *w,
                     int *alt, int *cl);

/* A cut's end in rec's frame: its place on screen (pixels) and depths, as
 * the walk would draw the key. A valid entry (as hk_src); an ambiguous
 * key's last projection in the frame's order (the sky's clipper cuts a
 * sector's triangles right after projecting its vertices, so the last
 * projection under the key is this sector's own); with no entry, the
 * hand-over the walk takes for the key, else this frame's fr place.
 * Returns the depths' count, 0 or less: not usable. */
static int fr_src(Rec *rec, int kind, const Sink *k, uint32_t key, float *x, float *y, float *w,
                  unsigned *cost, Touch *t) {
  Table *const tb = rec->tab;
  const int i = find(rec, kind, key, 0, &cost[VCC_REC_PROBES], t);
  unsigned n = 0, j;
  if (i >= 0) {
    const Entry *const s = &tb->s[i];
    lz_need(tb, k, i);
    n = T_N(s->tag);
    touch(t, (size_t)i * sizeof(Entry) + offsetof(Entry, c), sizeof(Cand) * (n ? n : 1));
    if (T_STATE(s->tag) == E_VALID && n) {
      if (s->tag & T_MULTI)
        *x = *y = 0;
      else
        cand_pos(tb, &s->c[0], x, y);
      for (j = 0; j < n; j++)
        w[j] = s->c[j].w;
    } else if (T_STATE(s->tag) == E_AMBIGUOUS && tb->nlog && tb->nlog < VC_LOG) {
      const unsigned lo = tb->nlog > FR_NEAR ? tb->nlog - FR_NEAR : 0;
      unsigned r = tb->nlog;
      float wv;
      int alt, cl;
      while (r > lo && LOG_E(tb->log[--r]) != (unsigned)i)
        ;
      cost[VCC_REC_LINES] += (tb->nlog - r) / 16 + 1; /* a model: the log read backwards */
      if (LOG_E(tb->log[r]) != (unsigned)i || !res_place(tb, s, r, x, y, &wv, &alt, &cl) || cl)
        return -1;
      if (wv > 0) { /* its own answer */
        n = 0;
        w[n++] = wv;
        if (alt)
          w[n++] = flt(wv * (1.0f / 16));
      } else /* the first projection's point: at its candidates' depths */
        for (j = 0; j < n; j++)
          w[j] = s->c[j].w;
    } else
      return -1;
  } else {
    const Hk *h = hk_slot(tb, rec->gen, key, 0, &cost[VCC_REC_PROBES], t);
    if (!h || h->dead || !h->placed) /* the walk's next look (psx_vcache_poly) */
      h = fr_slot(tb, rec->gen, key, 0, &cost[VCC_REC_PROBES], t);
    if (!h || h->dead || !h->placed || h->asrc || h->low || !h->n)
      return -1;
    n = h->n;
    *x = h->dx;
    *y = h->dy;
    for (j = 0; j < n; j++)
      w[j] = h->c[j].w;
  }
  *x += (short)(key & 0xFFFF) + 0.5f;
  *y += (short)(key >> 16) + 0.5f;
  return (int)n;
}

/* A VCD_LERP cut at its own fraction t / 4096 from a to b, into rec's fr
 * (derive_hand, where the hand-overs leave key with no place): both ends
 * need depths; the depth there is 1/w's on screen (hk_pair). Two places at
 * one key: as hand-overs, the same point (depths added), within HK_MERGE
 * their mean, else neither. */
static void frac_hand(int kind, const Sink *k, unsigned who, uint32_t key, uint32_t a, uint32_t b,
                      int tf) {
  unsigned *const cost = k->cost;
  Rec *const rec = &s_rec[kind];
  VCacheStats *const st = k->st;
  float ax, ay, bx, by, aw[VC_CANDS], bw[VC_CANDS], w[VC_CANDS], pa = 0, pb = 0, f, dx, dy;
  int na, nb;
  unsigned n, i, j;
  Touch t = TOUCH0;
  Hk *h;
  na = fr_src(rec, kind, k, a, &ax, &ay, aw, cost, &t);
  nb = na > 0 ? fr_src(rec, kind, k, b, &bx, &by, bw, cost, &t) : 0;
  if (na <= 0 || nb <= 0)
    goto nosrc;
  f = (float)tf * (1.0f / 4096);
  n = hk_pair(aw, na, bw, nb, f, 0, w, &pa, &pb, cost, NULL);
  dx = flt(flt(ax + f * (bx - ax)) - ((short)(key & 0xFFFF) + 0.5f));
  dy = flt(flt(ay + f * (by - ay)) - ((short)(key >> 16) + 0.5f));
  if (!n || !(dx < 8 && dx > -8 && dy < 8 && dy > -8))
    goto nosrc;
  h = fr_slot(rec->tab, rec->gen, key, 1, &cost[VCC_REC_PROBES], &t);
  if (!h) {
    st->hk_full++;
    goto out;
  }
  touch(&t, offsetof(Table, fr) + (size_t)(h - rec->tab->fr) * sizeof(Hk), sizeof(Hk));
  if (h->dead)
    goto out;
  if (h->placed) {
    if (!(h->dx - dx <= 0.125f && h->dx - dx >= -0.125f && h->dy - dy <= 0.125f &&
          h->dy - dy >= -0.125f)) {
      if (h->placed < 255 && h->dx - dx <= HK_MERGE && dx - h->dx <= HK_MERGE &&
          h->dy - dy <= HK_MERGE && dy - h->dy <= HK_MERGE) {
        h->sx += dx;
        h->sy += dy;
        h->placed++;
        h->dx = flt(h->sx / h->placed);
        h->dy = flt(h->sy / h->placed);
        cost[VCC_REC_DIV] += 2;
      } else {
        h->dead = 1;
        goto out;
      }
    }
  } else {
    h->dx = h->sx = dx;
    h->dy = h->sy = dy;
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
  st->fr_kept++;
  goto out;
nosrc:
  st->fr_nosrc++;
out:
  cost[VCC_REC_LINES] += t.n;
}

/* ------------------------------------------------------------ own points (#216) */

/* A glow's or moby shadow's own point (psx_vcache.h VCD_OWN, VCD_SELF).
 * Tagged on host routes S, P and Pf (both draws), the glows' and shadows'
 * vertices left with no entry were:
 *  - a rim or nudged corner whose key another hand-over placed differently,
 *    so neither was kept: a portal sky cut at a glow's rim pixel, or two
 *    mobys' shadow corners nudged onto one pixel;
 *  - a rim or corner whose source key is ambiguous, where #202's
 *    resolution from the frame's order finds no depth (too many under the
 *    key, or 4 times apart);
 *  - a glow behind the eye: its centre's projection is invalid (SZ3 0, the
 *    PS1's place clamped to the screen's limit), so nothing could find it.
 * Each is one point the renderer made from a projection it had just made.
 * So it names that projection by its place in the frame's order (the
 * fan's centre's, a shadow's corner's), and the point is kept under its
 * key and its polygon's first vertex's (the centre, the walk's anchor):
 * another renderer's point, or another moby's, at its pixel is under
 * another anchor. */

/* (key, anchor)'s own point in tab's frame gen, or (add) a free one for it */
static Ow *own_slot(Table *tb, uint32_t gen, uint32_t key, uint32_t anchor, int add,
                    unsigned *probes, Touch *t) {
  uint32_t h = (key * 0x9E3779B1u ^ anchor * 0x85EBCA77u) >> 21 & (VC_OW - 1), pr;
  for (pr = 0; pr < 8; pr++, h = (h + 1) & (VC_OW - 1)) {
    Ow *const o = &tb->ow[h];
    ++*probes;
    touch(t, offsetof(Table, ow) + (size_t)h * sizeof(Ow), sizeof(Ow));
    if (o->gen != gen) {
      if (!add)
        return NULL;
      o->key = key;
      o->anchor = anchor;
      o->gen = gen;
      o->dead = o->placed = 0;
      return o;
    }
    if (o->key == key && o->anchor == anchor)
      return o;
  }
  return NULL;
}

/* key's own point under anchor: from projection r (its place in the
 * frame's order), offset (ox, oy) pixels from it; a second, different one
 * leaves the key with neither. #242: one own_corner gave (placed 2) is as if
 * not there */
static void own_put(int kind, const Sink *k, uint32_t key, uint32_t anchor, unsigned r, int ox,
                    int oy, Touch *t) {
  Rec *const rec = &s_rec[kind];
  Ow *const o = own_slot(rec->tab, rec->gen, key, anchor, 1, &k->cost[VCC_REC_PROBES], t);
  if (!o) {
    k->st->own_full++;
    return;
  }
  if (o->placed == 1) {
    if (o->r != r || o->ox != ox || o->oy != oy)
      o->dead = 1;
    return;
  }
  o->dead = 0;
  o->r = (uint16_t)r;
  o->ox = ox;
  o->oy = oy;
  o->placed = 1;
  k->st->own_kept++;
}

/* The projection a's point comes from under anchor: its own point's (and
 * offset), else a's last projection in the frame's order (the renderer's
 * own, made just before); 0, or -1 when none is usable */
static int own_src(int kind, const Sink *k, uint32_t a, uint32_t anchor, unsigned *r, int *ox,
                   int *oy, Touch *t) {
  Rec *const rec = &s_rec[kind];
  Table *const tb = rec->tab;
  const Ow *const o = own_slot(tb, rec->gen, a, anchor, 0, &k->cost[VCC_REC_PROBES], t);
  int i;
  unsigned q, lo;
  if (o && o->placed == 1) { /* (#242: not own_corner's) */
    if (o->dead)
      return -1;
    *r = o->r;
    *ox = o->ox;
    *oy = o->oy;
    return 0;
  }
  i = find(rec, kind, a, 0, &k->cost[VCC_REC_PROBES], t);
  if (i < 0 || !tb->nlog)
    return -1;
  lo = tb->nlog > FR_NEAR ? tb->nlog - FR_NEAR : 0;
  for (q = tb->nlog; q > lo;)
    if (LOG_E(tb->log[--q]) == (unsigned)i) {
      k->cost[VCC_REC_LINES] += (tb->nlog - q) / 16 + 1; /* a model: the log read backwards */
      *r = q;
      *ox = *oy = 0;
      return 0;
    }
  return -1;
}

/* #242: a nudged corner's own point from own_corner (placed 2): kept apart
 * from every other, so every own point kept before is as it was: it gives
 * way to any other own point under key and anchor (own_put replaces it, and
 * it is never put where one is), two different ones leave neither, and no
 * hand-over's source (own_src) nor a found vertex (the walk) reads it */
static void own_put2(int kind, const Sink *k, uint32_t key, uint32_t anchor, unsigned r, Touch *t) {
  Rec *const rec = &s_rec[kind];
  Ow *const o = own_slot(rec->tab, rec->gen, key, anchor, 1, &k->cost[VCC_REC_PROBES], t);
  if (!o) {
    k->st->own_full++;
    return;
  }
  if (o->placed) {
    if (o->placed == 2 && o->r != r)
      o->dead = 1;
    return;
  }
  o->r = (uint16_t)r;
  o->ox = o->oy = 0;
  o->placed = 2;
  k->st->own_corner++;
}

/* #242: a's own point under anchor dead (two different points), and the
 * projection q - 1 back a's: that one's place in the frame's order (*r) */
static int own_corner(int kind, const Sink *k, uint32_t a, uint32_t anchor, unsigned q, unsigned *r,
                      Touch *t) {
  Rec *const rec = &s_rec[kind];
  Table *const tb = rec->tab;
  const Ow *const o = own_slot(tb, rec->gen, a, anchor, 0, &k->cost[VCC_REC_PROBES], t);
  int i;
  if (!o || o->placed != 1 || !o->dead || q > tb->nlog)
    return 0;
  i = find(rec, kind, a, 0, &k->cost[VCC_REC_PROBES], t);
  if (i < 0 || LOG_E(tb->log[tb->nlog - q]) != (unsigned)i)
    return 0;
  *r = tb->nlog - q;
  return 1;
}

/* VCD_SELF: key is the projection made p back (0: the last), by its place
 * in the frame's order. #229: world (a world piece's corner) where that
 * projection is not key's (the grid moved the point after it: no entry at
 * key, or another point's), its place and depths are kept as the piece's
 * own (Sp), apart from the own points, which stay as they were */
static void own_self(int kind, const Sink *k, uint32_t key, uint32_t anchor, int p, int world) {
  Rec *const rec = &s_rec[kind];
  Table *const tb = rec->tab;
  Touch t = TOUCH0;
  const int i = find(rec, kind, key, 0, &k->cost[VCC_REC_PROBES], &t);
  if (i >= 0 && p >= 0 && (unsigned)p < tb->nlog &&
      LOG_E(tb->log[tb->nlog - 1u - (unsigned)p]) == (unsigned)i)
    own_put(kind, k, key, anchor, tb->nlog - 1u - (unsigned)p, 0, 0, &t);
  else {
    k->st->own_nosrc++;
    if (world && p >= 0 && (unsigned)p < tb->nlog && tb->nlog < VC_LOG) {
      float x, y, w[VC_CANDS], dx, dy;
      const int n = log_point(tb, k, tb->nlog - 1u - (unsigned)p, 0, 0, &x, &y, w, &t);
      dx = flt(x - ((short)(key & 0xFFFF) + 0.5f));
      dy = flt(y - ((short)(key >> 16) + 0.5f));
      if (n > 0 && dx < 1024 && dx > -1024 && dy < 1024 && dy > -1024) {
        sp_put(rec, k, key, anchor, dx, dy, w, (unsigned)n, tb->nlog - 1u - (unsigned)p, &t, 0);
        k->st->sp_moved++;
      }
    }
  }
  k->cost[VCC_REC_LINES] += t.n;
}

/* VCD_OWN with an alias or offset: its source's own point, offset. #242:
 * an alias with q non-zero (a moby shadow's nudged corner) whose source's own
 * point is dead (two of its shadow's corners on one key) is the projection
 * q - 1 back, where that projection is the source key's */
static void own_hand(int kind, const Sink *k, int form, uint32_t key, uint32_t a, uint32_t anchor,
                     int p, int q) {
  Touch t = TOUCH0;
  unsigned r;
  int ox, oy;
  if ((form == VCD_ALIAS || form == VCD_OFFSET) &&
      own_src(kind, k, a, anchor, &r, &ox, &oy, &t) == 0)
    own_put(kind, k, key, anchor, r, ox + (form == VCD_OFFSET ? p : 0),
            oy + (form == VCD_OFFSET ? q : 0), &t);
  else if (form == VCD_ALIAS && q > 0 && own_corner(kind, k, a, anchor, (unsigned)q, &r, &t))
    own_put2(kind, k, key, anchor, r, &t);
  else
    k->st->own_nosrc++;
  k->cost[VCC_REC_LINES] += t.n;
}

/* #217: VCD_SELF with VCD_OWN: key's own point under anchor old, if it
 * has one, is its own point under anchor too (a piece the world's last
 * list split again names the corners it keeps by its own first vertex).
 * #229: else (and always for a split point: split) its world point kept
 * under old (Sp), under anchor too */
static void own_move(int kind, const Sink *k, uint32_t key, uint32_t anchor, uint32_t old,
                     int split) {
  Rec *const rec = &s_rec[kind];
  Touch t = TOUCH0;
  const Ow *const o =
      split ? NULL : own_slot(rec->tab, rec->gen, key, old, 0, &k->cost[VCC_REC_PROBES], &t);
  if (o && o->placed) {
    if (!o->dead)
      own_put(kind, k, key, anchor, o->r, o->ox, o->oy, &t);
  } else {
    const Sp *const sp = sp_slot(rec->tab, rec->gen, key, old, 0, &k->cost[VCC_REC_PROBES], &t);
    if (sp && sp->placed && !sp->dead)
      sp_put(rec, k, key, anchor, sp->dx, sp->dy, sp->w, sp->n, sp->r, &t, 0);
  }
  k->cost[VCC_REC_LINES] += t.n;
}

/* #217: a world split point's end with no projection: its key's only
 * projections invalid (behind the eye: the GTE's SXY is the screen's limit,
 * and there is no depth), or its own point's projection invalid, or itself
 * a split point with no projection (below) */
static int end_noproj(Rec *rec, int kind, const Sink *k, uint32_t key, uint32_t anchor,
                      unsigned *cost, Touch *t) {
  Table *const tb = rec->tab;
  const int i = find(rec, kind, key, 0, &cost[VCC_REC_PROBES], t);
  const Ow *o;
  if (i < 0) {
    const Hk *const h = hk_slot(tb, rec->gen, key, 0, &cost[VCC_REC_PROBES], t);
    return h && HK_NOPROJ(h);
  }
  lz_need(tb, k, i);
  if (T_STATE(tb->s[i].tag) == E_INVALID)
    return 1;
  o = own_slot(tb, rec->gen, key, anchor, 0, &cost[VCC_REC_PROBES], t);
  if (o && o->placed && !o->dead && o->r < tb->nlog) {
    const int e = (int)LOG_E(tb->log[o->r]);
    float x, y, w;
    int alt, cl;
    lz_need(tb, k, e);
    return !res_place(tb, &tb->s[e], o->r, &x, &y, &w, &alt, &cl);
  }
  return 0;
}

/* #217: a 3D point placed from its ends' own points (derive_hand): counted,
 * and tainted */
static inline int own_taken(VCacheStats *st, int *taint) {
  st->hk_own++;
  *taint = 1;
  return 1;
}

/* One hand-over (psx_vcache.h PSX_VC_DERIVE) into rec's frame */
static void derive_hand(int kind, const Sink *k, int how, uint32_t key, uint32_t a, uint32_t b,
                        int p, int q) {
  unsigned *const cost = k->cost;
  Rec *const rec = &s_rec[kind];
  VCacheStats *const st = k->st;
  const int form = how & 15;
  const unsigned who = ((unsigned)how >> 4 & 15) < VCD_WHO_N ? (unsigned)how >> 4 & 15 : 0;
  const int low = who == VCD_PARTICLES; /* #209: gives way to every other renderer's */
  float w[VC_CANDS], dx, dy;
  int taint = 0, left = 0; /* left (#215): the key has no usable place after this call */
  int first, placed;
  unsigned n = 0, i, j;
  Touch t = TOUCH0;
  Hk *h;
  if (!rec->tab)
    return;
  if ((how & (VCD_SELF | VCD_KEEP)) == (VCD_SELF | VCD_KEEP)) {
    /* #242: a drawn vertex of Spyro's clipped primitive kept as its own
     * (under q) at its own projection (named under b) */
    float x, y;
    const int nn = own_end(rec, k, key, b, &x, &y, w, cost, &t);
    if (nn > 0) {
      dx = flt(x - ((short)(key & 0xFFFF) + 0.5f));
      dy = flt(y - ((short)(key >> 16) + 0.5f));
      if (dx < 1024 && dx > -1024 && dy < 1024 && dy > -1024)
        sp_put(rec, k, key, (uint32_t)q, dx, dy, w, (unsigned)nn, SP_NOR, &t, 1);
    }
    cost[VCC_REC_LINES] += t.n;
    return;
  }
  if (how & VCD_SELF) { /* #216: an own point only; #217: or one moved to another anchor */
    if (how & VCD_OWN)
      own_move(kind, k, key, b, (uint32_t)q, p); /* (#229: p 1, a split point) */
    else
      own_self(kind, k, key, b, p, who == VCD_ENV_SPLIT);
    return;
  }
  st->hk_calls++;
  /* #202: the sources as before first; a merged place only where they
   * give none; #217: then a 3D point's ends' own points, where neither
   * gives one (tainted, as a merged place: it counts only where nothing
   * else placed the key) */
  first = hk_place(rec, kind, k, how, key, a, b, p, q, NULL, &dx, &dy, w, &n, cost, &t, 0, NULL) == 0;
  placed = first ||
           hk_place(rec, kind, k, how, key, a, b, p, q, &taint, &dx, &dy, w, &n, cost, &t, 0,
                    NULL) == 0 ||
           (form == VCD_PERSP && (how & VCD_OWN) &&
            hk_place(rec, kind, k, how, key, a, b, p, q, &taint, &dx, &dy, w, &n, cost, &t, 1,
                     NULL) == 0 &&
            own_taken(st, &taint));
  if (form == VCD_PERSP && (how & VCD_KEEP)) {
    /* #242: Spyro's clip point kept as its polygon's own too, under q (its
     * primitive's anchor and its place in it), apart from hk: as hk has it,
     * so two clip points on one pixel, each its own polygon's, stay apart;
     * where hk's sources give none, from its ends' own projections */
    float kw[VC_CANDS], kdx, kdy;
    unsigned kn = 0;
    if (placed && n)
      sp_put(rec, k, key, (uint32_t)q, dx, dy, w, n, SP_NOR, &t, 1);
    else if (pk_place(rec, kind, k, how, key, a, b, p, &kdx, &kdy, kw, &kn, cost, &t) == 0 && kn)
      sp_put(rec, k, key, (uint32_t)q, kdx, kdy, kw, kn, SP_NOR, &t, 1);
  }
  if (form == VCD_PERSP && (how & VCD_OWN)) {
    /* #229: the world's split point kept as its piece's own too, under the
     * piece's anchor and edge (psx_vcache.h VCD_EDGE), apart from hk: from
     * its ends' keys where both give one at once (as hk has it), else from
     * the piece's own ends (a corner's own point, or a split point or moved
     * corner kept so), paired however far apart their depths are, else as
     * hk has it */
    const uint32_t an = VCD_CORNER(q, VCD_EDGE(VCD_ENDS_A(how), VCD_ENDS_B(how)));
    float sw[VC_CANDS], sdx, sdy;
    unsigned sn = 0;
    int st2 = 0, wide = 0;
    if (first)
      sp_put(rec, k, key, an, dx, dy, w, n, SP_NOR, &t, 0);
    else if (hk_place(rec, kind, k, how, key, a, b, p, q, &st2, &sdx, &sdy, sw, &sn, cost, &t, 2,
                      &wide) == 0 &&
             sn) {
      sp_put(rec, k, key, an, sdx, sdy, sw, sn, SP_NOR, &t, 0);
      st->sp_wide += (unsigned)wide;
    } else if (placed)
      sp_put(rec, k, key, an, dx, dy, w, n, SP_NOR, &t, 0);
  }
  if (!placed) {
    if (form == VCD_ALIAS || form == VCD_OFFSET) {
      /* #202: one point's alias or offset whose source key is ambiguous:
       * kept as that key's, for a polygon that finds nothing else to
       * resolve it from the frame's order as an ambiguous vertex of its
       * own (drawn at its own key) */
      const int i0 = find(rec, kind, a, 0, &cost[VCC_REC_PROBES], &t);
      if (i0 >= 0) {
        lz_need(rec->tab, k, i0);
        if (T_STATE(rec->tab->s[i0].tag) == E_AMBIGUOUS) {
          h = hk_slot(rec->tab, rec->gen, key, 1, &cost[VCC_REC_PROBES], &t);
          if (!h) {
            st->hk_full++;
            goto out;
          }
          if (h->placed && h->low != low) { /* #209: a particle's gives way */
            if (low)
              goto out;
            h->n = h->dead = h->placed = h->taint = h->low = 0;
            h->asrc = 0;
          }
          if (!h->placed) {
            h->placed = 1;
            h->taint = 1;
            h->low = (uint8_t)low;
            h->asrc = (uint16_t)(i0 + 1);
            h->n = 0;
            h->dx = h->dy = h->sx = h->sy = 0;
            h->who = (uint8_t)who;
            st->hk_ambsrc++;
            goto out;
          }
          if (h->asrc && h->asrc != (uint16_t)(i0 + 1))
            h->dead = 1; /* two keys: neither */
          goto out;
        }
      }
    }
    if (form == VCD_PERSP && (how & VCD_OWN) &&
        (end_noproj(rec, kind, k, a, VCD_CORNER(q, VCD_ENDS_A(how)), cost, &t) ||
         end_noproj(rec, kind, k, b, VCD_CORNER(q, VCD_ENDS_B(how)), cost, &t))) {
      /* #217: an end with no projection: the key is marked so (no place;
       * any hand-over that places it replaces the mark), and its vertex is
       * exempt */
      h = hk_slot(rec->tab, rec->gen, key, 1, &cost[VCC_REC_PROBES], &t);
      if (h && !h->placed && !h->dead) {
        h->low = 1;
        h->who = (uint8_t)who;
        st->hk_noproj++;
      }
    }
    goto nosrc;
  }
  h = hk_slot(rec->tab, rec->gen, key, 1, &cost[VCC_REC_PROBES], &t);
  if (!h) {
    st->hk_full++;
    left = 1;
    goto out;
  }
  touch(&t, offsetof(Table, hk) + (size_t)(h - rec->tab->hk) * sizeof(Hk), sizeof(Hk));
  /* #209: a particle quad's corner counts only where no other renderer
   * placed the key (even placed it differently, dead), and gives way to
   * any other's: every point placed before #209 stays as it was */
  if (h->placed && h->low != low) {
    if (low) {
      st->hk_kept++;
      goto out;
    }
    h->n = h->dead = h->placed = h->taint = h->low = 0;
    h->asrc = 0;
  }
  /* #202: a hand-over made from a merged place counts only where nothing
   * else placed the key, and gives way to one that isn't (whatever
   * merged places come to, every point placed without them stays as it
   * was) */
  if (h->placed && taint && !h->taint) {
    st->hk_kept++;
    goto out;
  }
  if (h->placed && (h->asrc || (!taint && h->taint))) {
    h->n = h->dead = h->placed = h->taint = 0;
    h->asrc = 0;
  }
  if (h->dead) {
    left = 1;
    goto out;
  }
  if (h->placed) { /* handed over before: the same point (its depths added), or not used */
    if (!(h->dx - dx <= 0.125f && h->dx - dx >= -0.125f && h->dy - dy <= 0.125f &&
          h->dy - dy >= -0.125f)) {
      if (form == VCD_LERP && h->who == who && h->placed < 255 &&
          h->dx - dx <= HK_MERGE && dx - h->dx <= HK_MERGE && h->dy - dy <= HK_MERGE &&
          dy - h->dy <= HK_MERGE) {
        /* #202: another cut at the same edge's pixel: one place, their mean */
        h->sx += dx;
        h->sy += dy;
        h->placed++;
        h->dx = flt(h->sx / h->placed);
        h->dy = flt(h->sy / h->placed);
        cost[VCC_REC_DIV] += 2;
        st->hk_merged++;
        goto depths;
      }
      h->dead = 1;
      st->hk_conflict++;
      left = 1;
      goto out;
    }
  } else {
    h->dx = h->sx = dx;
    h->dy = h->sy = dy;
    h->who = (uint8_t)who;
    h->placed = 1;
    h->taint = (uint8_t)taint;
    h->low = (uint8_t)low;
  }
depths:
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
  left = 1;
out:
  cost[VCC_REC_LINES] += t.n;
  if (left && form == VCD_LERP && VCD_HAS_T(how)) /* #215 */
    frac_hand(kind, k, who, key, a, b, VCD_T_OF(how));
  if ((how & VCD_OWN) && form != VCD_PERSP) /* #216 (#217: a 3D point's q is its ends' anchor) */
    own_hand(kind, k, form, key, a, b, p, q);
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

/* #211: the renderer contexts (psx_vcache.h VCX_*), by their hand C's name */
static const char *const k_cx_names[VCX_N] = {
    "EnvDraw",     "MobyDraw",      "LitDraw",          "SpecDraw",   "SpyroDraw",
    "SpyroShadow", "MobyShadows",   "FlameDraw",        "ParticlesDraw", "GlowsAndSparkles",
    "DragonRing",  "SkyDraw",       "FogSky",           "PortalSkyPlain", "PortalSkyFog",
    "PortalFace",  "other",         "outside",          "unmarked",   "no projection"};
#define CX_EXEMPT(c) ((c) == VCX_PORTAL_FACE || (c) == VCX_OUTSIDE || (c) == VCX_NO_PROJ)

/* name's context: the last pointers seen first (each kind's, written only
 * by the thread its renderers run on), then the names */
static int cx_of_name(int kind, const char *name) {
  static const char *seen[2][VCX_OTHER];
  int i;
  for (i = 0; i < VCX_OTHER; i++)
    if (seen[kind][i] == name)
      return i;
  for (i = 0; i < VCX_OTHER; i++)
    if (!strcmp(k_cx_names[i], name)) {
      seen[kind][i] = name;
      return i;
    }
  return VCX_OTHER;
}

#if VC_GEN_CHECK
/* #218: the generations' check, host builds only (VC_CHECK_GEN=1 in the
 * environment, or psx_vcache_set_check_gen). The record takes a native
 * projection's registers as the same when their generation is (same_ngte,
 * #205; rp_skip, #228), which holds only while every renderer that changes
 * an NgteProj field after a projection takes a new generation (gte_native.h
 * ngte_proj_regen). Every native projection reaches the record through
 * ngte_vcache or pres_vcache_ngte, so the check sits there: it keeps each
 * generation's fields (all but gen) as its first projection had them, a
 * slot for each of the last GC_N generations, and compares every later
 * projection of that generation with them word by word. Each projection
 * whose fields differ counts once (gen_stale), under the renderer context
 * that returns next (psx_vc_ctx: a renderer's projections come before its
 * return), or "not named" if none has by the report; the first GC_LOG are
 * logged with the field and the caller's address (addr2line gives the
 * renderer's line). Each kind (VC_LOGIC, VC_PRES) has its own generations
 * (g_ngte_gen, g_pres_ngte_gen) and slots, written only by the thread its
 * renderers run on. Counts only: no answer changes. */
#define GC_N 8192u
#define GC_W 17
#define GC_LOG 16
typedef struct {
  uint32_t gen;
  int w[GC_W];
} Gc;
static Gc *s_gc_tab[2];
/* per kind: generations seen, projections compared, stale ones not named
 * yet, and the stale ones by context */
static unsigned s_gc_seen[2], s_gc_cmp[2], s_gc_pend[2], s_gc_stale[2][VCX_N];
static unsigned s_gc_logged;
static const char *const k_gc_field[GC_W] = {"r11", "r12", "r13", "r21", "r22", "r23",
                                             "r31", "r32", "r33", "trx", "try", "trz",
                                             "ofx", "ofy", "h",   "dqa", "dqb"};

static void gc_words(const NgteProj *p, int *w) {
  w[0] = p->r11, w[1] = p->r12, w[2] = p->r13;
  w[3] = p->r21, w[4] = p->r22, w[5] = p->r23;
  w[6] = p->r31, w[7] = p->r32, w[8] = p->r33;
  w[9] = p->trx, w[10] = p->try_, w[11] = p->trz;
  w[12] = p->ofx, w[13] = p->ofy, w[14] = p->h;
  w[15] = p->dqa, w[16] = p->dqb;
}

static __attribute__((noinline)) void gc_check(int kind, const NgteProj *p, unsigned vxy,
                                               unsigned vz, const void *from) {
  Gc *t, *e;
  int w[GC_W], f;
  if (s_gc < 0) { /* the environment's, once (before either thread records) */
    const char *const v = getenv("VC_CHECK_GEN");
    s_gc = v && atoi(v) > 0;
  }
  if (!s_gc || !p->gen)
    return; /* (0: no generation, the words compared each time) */
  t = s_gc_tab[kind];
  if (!t && !(t = s_gc_tab[kind] = calloc(GC_N, sizeof(Gc))))
    return;
  e = &t[p->gen & (GC_N - 1)];
  gc_words(p, w);
  if (e->gen != p->gen) { /* its first projection (or its slot's older one gone) */
    e->gen = p->gen;
    memcpy(e->w, w, sizeof w);
    s_gc_seen[kind]++;
    return;
  }
  s_gc_cmp[kind]++;
  for (f = 0; f < GC_W && e->w[f] == w[f]; f++)
    ;
  if (f == GC_W)
    return;
  s_gc_pend[kind]++;
  if (__atomic_fetch_add(&s_gc_logged, 1, __ATOMIC_RELAXED) < GC_LOG)
    PsyX_Log("vcache gen check: stale generation %u (%s): %s was %d, now %d; vertex %d %d %d; "
             "called from %p (#218)",
             p->gen, kind == VC_PRES ? "the presenter's" : "the logic draw's", k_gc_field[f],
             e->w[f], w[f], ngte_vx(vxy), ngte_vy(vxy), ngte_vz(vz), from);
}

/* the stale ones since the last renderer returned, under name's context */
static inline void gc_name(int kind, const char *name) {
  if (__builtin_expect(s_gc_pend[kind] != 0, 0)) {
    s_gc_stale[kind][cx_of_name(kind, name)] += s_gc_pend[kind];
    s_gc_pend[kind] = 0;
  }
}

/* each kind's count, by context (-1: all; VCX_N: not named yet) */
static unsigned gc_count(int kind, int cx) {
  unsigned n = 0;
  int c;
  if (cx >= 0)
    return __atomic_load_n(cx == VCX_N ? &s_gc_pend[kind] : &s_gc_stale[kind][cx],
                           __ATOMIC_RELAXED);
  for (c = 0; c <= VCX_N; c++)
    n += gc_count(kind, c);
  return n;
}

/* the report's line, the totals since the start (only while the check is on) */
static void gc_line(void) {
  char b[2][256];
  int k, c, n, first;
  if (s_gc <= 0)
    return;
  for (k = 0; k < 2; k++) {
    const unsigned all = gc_count(k, -1);
    n = snprintf(b[k], sizeof b[k], "gen_stale %u", all);
    for (c = 0, first = 1; c <= VCX_N && all && n > 0 && (size_t)n < sizeof b[k]; c++) {
      const unsigned v = gc_count(k, c);
      if (v) {
        n += snprintf(b[k] + n, sizeof b[k] - (size_t)n, "%s%s %u", first ? " (" : ", ",
                      c == VCX_N ? "not named" : k_cx_names[c], v);
        first = 0;
      }
    }
    if (all && n > 0 && (size_t)n < sizeof b[k])
      snprintf(b[k] + n, sizeof b[k] - (size_t)n, ")");
  }
  PsyX_Log("vcache gen check: the logic draw's %u generations, %u projections compared with "
           "their generation's first, %s; the presenter's %u, %u, %s (VC_CHECK_GEN=1, #218)",
           s_gc_seen[0], s_gc_cmp[0], b[0], __atomic_load_n(&s_gc_seen[1], __ATOMIC_RELAXED),
           __atomic_load_n(&s_gc_cmp[1], __ATOMIC_RELAXED), b[1]);
}

void psx_vcache_set_check_gen(int on) { s_gc = on != 0; }

unsigned psx_vcache_gen_stale(int kind, int cx) {
  return kind == VC_LOGIC || kind == VC_PRES ? gc_count(kind, cx) : 0;
}
#endif

/* #222: where the counters look for a primitive at a: its offset in the
 * game's memory (psx_arena.h), whether the walk reads it there or in
 * walk.c's copy (copy, the game walk's; 0: none); anything else (present.c's
 * narrowed HUD copies, a buffer of the presenter's own) by its low 24 bits,
 * apart from those (CX_FAR). In the arena build both blocks are 16 MB-
 * aligned, so an offset is the low 24 bits, as before; in the native build
 * (the Vita's) neither is, so only the offset finds a primitive the walk
 * reads from the copy (the PS1 primitives: the portals' sky and faces). */
#define CX_FAR ((uintptr_t)1 << 31)
static uintptr_t cx_ofs(uintptr_t a, uintptr_t copy) {
  if (a - g_psx_arena_base < PSX_MEM_SIZE)
    return a - g_psx_arena_base;
  if (copy && a - copy < PSX_MEM_SIZE)
    return a - copy;
  return (a & 0xFFFFFFu) | CX_FAR;
}

static void cx_add(Table *t, int cx, uintptr_t lo, uintptr_t hi, int w24, uintptr_t olo,
                   int ok) {
  if (t->ncx < VC_CX) {
    Cx *const c = &t->cx[t->ncx++];
    c->lo = lo;
    c->hi = hi;
    c->olo = olo;
    c->cx = (uint8_t)cx;
    c->w24 = (uint8_t)w24;
    c->ok = (uint8_t)ok;
  }
}

/* a renderer's primitives in the frame kind records (psx_vc_ctx) */
static void vc_ctx(int kind, const char *name, uintptr_t p0, uintptr_t p1, uintptr_t n0,
                   uintptr_t n1, uintptr_t nb) {
  Rec *const r = &s_rec[kind];
  Table *const t = r->tab;
  int cx;
#if VC_GEN_CHECK
  gc_name(kind, name); /* #218 */
#endif
  if (!t)
    return;
  if (t->cx_gen != r->gen) {
    t->cx_gen = r->gen;
    t->ncx = 0;
  }
  cx = cx_of_name(kind, name);
  /* the primitive buffer, by its addresses' offsets in their 16 MB window
   * (the HUD's mobys are drawn from the buffer's top: no forward move) */
  if (p1 > p0 && p1 - p0 < 0x100000u && (p0 >> 24) == ((p1 - 1) >> 24)) {
    /* #222: and by its offset in the game's memory, if both ends are in
     * one block (cx_ofs) */
    const uintptr_t o0 = cx_ofs(p0, 0);
    cx_add(t, cx, p0 & 0xFFFFFFu, ((p1 - 1) & 0xFFFFFFu) + 1, 1, o0,
           cx_ofs(p1 - 1, 0) - o0 == p1 - 1 - p0);
  }
  /* native_draw's records: the cursor's move, or from the start of a pool
   * begun in the call */
  if (n1 > n0 && n1 - n0 < 0x1000000u)
    cx_add(t, cx, n0, n1, 0, n0, 1);
  else if (n1 != n0 && nb && n1 > nb)
    cx_add(t, cx, nb, n1, 0, nb, 1);
}

void psx_vc_ctx(const char *name, uintptr_t p0, uintptr_t p1, uintptr_t n0, uintptr_t n1,
                uintptr_t nb) {
  if (g_psyxRtpDeriveHook)
    vc_ctx(VC_LOGIC, name, p0, p1, n0, n1, nb);
}

void pres_vc_ctx(const char *name, uintptr_t p0, uintptr_t p1, uintptr_t n0, uintptr_t n1,
                 uintptr_t nb) {
  vc_ctx(VC_PRES, name, p0, p1, n0, n1, nb);
}

/* the context of the polygon whose first vertex's x is at p, in tab's
 * frame gen: the smallest range holding it, a record pool's before the
 * primitive buffer's. ofs (#222, the counters'): the primitive buffer's
 * ranges by their offsets in the game's memory, the polygon's in the game
 * walk's copy too (cx_ofs); 0 (the placement's gates, cx_find): by their
 * low 24 bits, as before #222 */
static int cx_find_(const Table *tab, uint32_t gen, const void *p, int ofs) {
  const uintptr_t a = (uintptr_t)p;
  const uintptr_t a24 = ofs ? cx_ofs(a, psx_gpu_game_walk()->copy) : a & 0xFFFFFFu;
  uintptr_t best = ~(uintptr_t)0;
  int cx = VCX_OUTSIDE, rank = 2; /* the best hit's w24 (a pool's, 0, first) */
  unsigned i;
  if (!tab || tab->cx_gen != gen || !tab->ncx)
    return VCX_UNMARKED;
  for (i = 0; i < tab->ncx; i++) {
    const Cx *const c = &tab->cx[i];
    const uintptr_t x = c->w24 ? a24 : a, lo = ofs ? c->olo : c->lo, hi = lo + (c->hi - c->lo);
    if (ofs && !c->ok)
      continue;
    if (x >= lo && x < hi &&
        (c->w24 < rank || (c->w24 == rank && c->hi - c->lo < best))) {
      best = c->hi - c->lo;
      cx = c->cx;
      rank = c->w24;
    }
  }
  return cx;
}
static int cx_find(const Table *tab, uint32_t gen, const void *p) {
  return cx_find_(tab, gen, p, 0);
}

/* the report's words for the vertices left with no entry (#211): "missed
 * N (top: <ctx> n, <ctx> n, <ctx> n), exempt E (outside the renderers O,
 * portal faces F), unmarked U" */
static void cx_text(const VCacheStats *s, char *buf, size_t size) {
  unsigned missed = 0, top[3] = {0, 0, 0};
  int topc[3] = {-1, -1, -1}, c, j, n;
  for (c = 0; c < VCX_N; c++) {
    unsigned v = s->ne[c];
    if (CX_EXEMPT(c) || c == VCX_UNMARKED || !v)
      continue;
    missed += v;
    for (j = 0; j < 3; j++)
      if (v > top[j]) {
        int k;
        for (k = 2; k > j; k--) {
          top[k] = top[k - 1];
          topc[k] = topc[k - 1];
        }
        top[j] = v;
        topc[j] = c;
        break;
      }
  }
  n = snprintf(buf, size, "missed %u (top:", missed);
  for (j = 0; j < 3 && topc[j] >= 0 && n > 0 && (size_t)n < size; j++)
    n += snprintf(buf + n, size - (size_t)n, "%s %s %u", j ? "," : "", k_cx_names[topc[j]],
                  top[j]);
  if (n > 0 && (size_t)n < size)
    snprintf(buf + n, size - (size_t)n,
             "%s), exempt %u (outside the renderers %u, portal faces %u, no projection %u), "
             "unmarked %u",
             topc[0] < 0 ? " none" : "",
             s->ne[VCX_OUTSIDE] + s->ne[VCX_PORTAL_FACE] + s->ne[VCX_NO_PROJ], s->ne[VCX_OUTSIDE],
             s->ne[VCX_PORTAL_FACE], s->ne[VCX_NO_PROJ], s->ne[VCX_UNMARKED]);
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

/* #217: key marked as a world split point with an end behind the eye
 * (HK_NOPROJ) in tb's frame gen */
static int hk_noproj(const Table *tb, uint32_t gen, uint32_t key, unsigned *probes, Touch *t) {
  uint32_t h = hash(key) & (VC_HK - 1), p;
  for (p = 0; p < 8; p++, h = (h + 1) & (VC_HK - 1)) {
    const Hk *const e = &tb->hk[h];
    ++*probes;
    touch(t, offsetof(Table, hk) + (size_t)h * sizeof(Hk), sizeof(Hk));
    if (e->gen != gen)
      return 0;
    if (e->key == key)
      return HK_NOPROJ(e);
  }
  return 0;
}

/* #215: ... else its cut's place at its own fraction (frac_hand) */
static const Hk *fr_look(const Table *tb, uint32_t gen, uint32_t key, unsigned *probes, Touch *t) {
  uint32_t h = hash(key) & (VC_FR - 1), p;
  for (p = 0; p < 8; p++, h = (h + 1) & (VC_FR - 1)) {
    const Hk *const e = &tb->fr[h];
    ++*probes;
    touch(t, offsetof(Table, fr) + (size_t)h * sizeof(Hk), 8);
    if (e->gen != gen)
      return NULL;
    if (e->key == key) {
      if (e->dead || !e->placed)
        return NULL;
      touch(t, offsetof(Table, fr) + (size_t)h * sizeof(Hk), sizeof(Hk));
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
 * as before, so the choice and the depths are the same.
 *
 * Since #214 that division is only the fallback (choose_f below, for
 * depths that aren't positive normal floats); choose compares the ratios
 * without dividing (Ratio). */
static int choose_f(const Cand *const *e, const unsigned *en, const int *idx, int m,
                    unsigned *cost, int *pick) {
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

/* The polygon's depth choice without dividing (#214). choose_f's ratios
 * are only compared with each other, with 1 and with 4, so each is kept as
 * its two depths' significands and exponents: the larger depth n over the
 * smaller d (choose_f's ratio(a, b), either order). Two ratios compare
 * exactly by cross-multiplying, in 64 bits (24-bit significands). A float
 * quotient is the real one rounded, and rounding keeps order, so the real
 * order is the floats' unless they would round to one value: when the two
 * are within 2^-22 of each other (relative; the floats' half-ulps add up
 * to under 2^-23 of the larger), they are divided and compared as
 * choose_f does. Equal reals are equal floats. Against 4: the float
 * quotient is under 4 exactly when n/d < 4 - 2^-23 (from there it rounds to
 * 4: the tie goes to 4's even significand), an integer test. Against 1:
 * every ratio's float is 1 or more, so the first vertex's ratio is the
 * worst so far without a test. With one candidate for the first vertex
 * (en[f] 1), only whether every vertex's closest ratio is under 4 counts,
 * as choose_f's best stays 4: the choice fails at the first that isn't.
 * Depths that aren't positive normal floats (W_LO..W_HI): choose_f. */
typedef struct {
  float n, d;      /* the larger depth and the smaller */
  uint32_t mn, md; /* their significands, the leading 1 set */
  int e;           /* exponent(n) - exponent(d) */
} Ratio;

/* the ratio of two positive normal depths' bits ua, ub (and the floats) */
static inline void rat_make(Ratio *r, uint32_t ua, uint32_t ub, float a, float b) {
  const uint32_t un = ua > ub ? ua : ub, ud = ua > ub ? ub : ua;
  r->n = ua > ub ? a : b;
  r->d = ua > ub ? b : a;
  r->mn = (un & 0x7FFFFFu) | 0x800000u;
  r->md = (ud & 0x7FFFFFu) | 0x800000u;
  r->e = (int)(un >> 23) - (int)(ud >> 23);
}

/* The host's x87 (i686, no SSE; see flt): choose_f's quotients stay on the
 * x87 stack at 64-bit precision (its compiled code compares them there,
 * never stored as floats), where two different ones never round to one
 * value (24-bit significands: they differ by 2^-50 of themselves or more)
 * and one under 4 stays under. There the answers are the real order. */
#if defined(__i386__) && !defined(__SSE_MATH__)
#define RAT_REAL 1
#else
#define RAT_REAL 0
#endif

/* choose_f's ratio of a < ratio of b, divided as choose_f does */
static int rat_lt_div(const Ratio *a, const Ratio *b, unsigned *cost) {
  const float ra = a->n / a->d, rb = b->n / b->d;
  cost[VCC_LOOK_DIV] += 2;
  return ra < rb;
}

static inline int rat_lt(const Ratio *a, const Ratio *b, unsigned *cost) {
  uint64_t p = (uint64_t)a->mn * b->md, q = (uint64_t)b->mn * a->md;
  const int s = a->e - b->e;
  if (s > 8 || s < -8) /* (a ratio under 4 has e 0..2) */
    return rat_lt_div(a, b, cost);
  if (s >= 0)
    p <<= s;
  else
    q <<= -s;
  if (RAT_REAL)
    return p < q;
  if (p < q) {
    if (q - p > q >> 22)
      return 1;
  } else if (p > q) {
    if (p - q > p >> 22)
      return 0;
  } else
    return 0;
  return rat_lt_div(a, b, cost);
}

/* the quotient n / d as choose_f has it is under 4: as a float, n/d <
 * (2^25 - 1) / 2^23; on the x87, n/d < 4 */
static inline int rat_under4(const Ratio *r) {
  if (r->e > 8)
    return 0;
  if (RAT_REAL)
    return ((uint64_t)r->mn << r->e) < ((uint64_t)r->md << 2);
  return ((uint64_t)r->mn << (r->e + 23)) < (uint64_t)r->md * ((1u << 25) - 1);
}

static int choose(const Cand *const *e, const unsigned *en, const int *idx, int m, unsigned *cost,
                  int *pick) {
  const int f = idx[0];
  int c, i, j, best_c = -1, hb = 0, try_pick[4] = {0, 0, 0, 0};
  Ratio best;
  for (c = 0; c < (int)en[f]; c++) {
    const float w0 = e[f][c].w;
    const uint32_t u0 = w_bits(&w0);
    Ratio worst;
    int ok = 1, hw = 0;
    try_pick[f] = c;
    for (i = 1; ok && i < m; i++) {
      const int v = idx[i];
      Ratio rb;
      int left = 0, last = 0, have = 0;
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
      if (!w_normal(u0))
        return choose_f(e, en, idx, m, cost, pick);
      if (left == 1 && en[f] == 1) {
        try_pick[v] = last;
        if (!under3999b(&e[v][last].w, &w0)) {
          const uint32_t ua = w_bits(&e[v][last].w);
          if (!w_normal(ua))
            return choose_f(e, en, idx, m, cost, pick);
          rat_make(&rb, ua, u0, e[v][last].w, w0);
          if (!rat_under4(&rb)) {
            ok = 0;
            break;
          }
        }
        continue;
      }
      for (j = 0; j < (int)en[v]; j++)
        if (u4 & 1u << j) {
          const uint32_t ua = w_bits(&e[v][j].w);
          Ratio r;
          if (!w_normal(ua))
            return choose_f(e, en, idx, m, cost, pick);
          rat_make(&r, ua, u0, e[v][j].w, w0);
          if (!have || rat_lt(&r, &rb, cost)) {
            rb = r;
            have = 1;
            try_pick[v] = j;
          }
        }
      if (en[f] == 1) {
        if (!rat_under4(&rb)) {
          ok = 0;
          break;
        }
      } else if (!hw || rat_lt(&worst, &rb, cost)) {
        worst = rb;
        hw = 1;
      }
    }
    if (!ok)
      continue;
    if (en[f] == 1 || !hw) { /* worst under 4 (1 with none) */
      best_c = c;
      memcpy(pick, try_pick, sizeof try_pick);
      /* (en[f] > 1 and none: m 1, worst 1, smaller than any other) */
      if (!hw)
        break;
    } else if (hb ? rat_lt(&worst, &best, cost) : rat_under4(&worst)) {
      best = worst;
      hb = 1;
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
                         Der **dr_out, int commit, unsigned *named, unsigned *nnamed) {
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
  if (nnamed)
    *nnamed = 0;
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
          if (nnamed && *nnamed < 4) { /* #217: each place the order names, once */
            unsigned j;
            for (j = 0; j < *nnamed && named[j] != sm; j++)
              ;
            if (j == *nnamed)
              named[(*nnamed)++] = sm;
          }
        }
      }
    if (one != VC_NOSEQ && !conflict) {
      const uint32_t k2 = LOG_E(tab->log[one]);
      uint32_t tag;
      lz_need(tab, lk, (int)k2); /* #193 */
      tag = tab->s[k2].tag;
      if (nnamed && (k2 == so0 || k2 == sa0 || k2 == sb0 || T_STATE(tag) != E_VALID))
        *nnamed = 0; /* #217: a corner's own projection, or not a usable one */
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

/* derive (above): its place, then its depth, the others all found.
 * Returns 1, 2 (#202: the depth the projection's own), 3 (#217: no projection
 * taken, the depth a named one's), or 0. */
static int derive(int pres, Table *tab, uint32_t gen, short *const *xy, const Cand *const *e,
                  const unsigned *en, int m, const Sink *lk, unsigned *cost, int *pick,
                  VCacheVert *out, int *moved, int *snapped) {
  uint32_t slotf[4] = {0, 0, 0, 0}, slot;
  unsigned named[4], nnamed;
  Der *dr;
  int i;
  (void)pres;
  for (i = 0; i < 4; i++)
    if (i != m)
      slotf[i] = (uint32_t)(((const char *)e[i] - (const char *)tab->s) / sizeof(Entry));
  derive_place(tab, gen, xy, slotf, m, lk, cost, out, &slot, &dr, 1, named, &nnamed);
  *moved = out->dx != 0 || out->dy != 0;
  *snapped = slot != 0;
  if (derive_depth(tab, xy, e, en, m, slot, NULL, cost, pick, out))
    return 1;
  if (slot) {
    /* #202: at the projection the order named, but the plane through the
     * other three gives no depth there (behind the eye, or past 4 times
     * theirs: big close quads whose corners the grid moved): that
     * projection's own candidates, chosen with the others' as for any
     * polygon whose every vertex was found */
    static const int k_all4[4] = {0, 1, 2, 3};
    const Cand *e2[4];
    unsigned en2[4];
    const Entry *const s2 = &tab->s[slot - 1];
    for (i = 0; i < 4; i++) {
      e2[i] = e[i];
      en2[i] = en[i];
    }
    e2[m] = s2->c;
    en2[m] = T_N(s2->tag);
    memset(pick, 0, sizeof(int) * 4);
    if (choose(e2, en2, k_all4, 4, cost, pick)) {
      out->w = flt(e2[m][pick[m]].w);
      pick[m] = 0;
      return 2;
    }
  } else if (nnamed) {
    /* #217: the order named projections but none was taken (two places, or
     * one under a key with different points: the grid's point moved onto
     * another's pixel), and the plane through the other three gives no
     * depth at the key (they lie on one line on screen: an edge-on piece;
     * or behind the eye, or past 4 times theirs). The vertex stays at the
     * place it has (the key's, or the one another polygon found, as its
     * neighbours draw it); its depth is a named projection's own, chosen
     * with the others' */
    static const int k_all4[4] = {0, 1, 2, 3};
    const Cand *e2[4];
    unsigned en2[4], nc = 0, j, q;
    Cand c[VC_CANDS];
    for (j = 0; j < nnamed; j++) {
      const unsigned r = named[j], k2 = LOG_E(tab->log[r]);
      const Entry *const s2 = &tab->s[k2];
      float x, y, w;
      int alt, cl, rk;
      if (k2 == slotf[(m + 2) & 3] || k2 == slotf[(m + 1) & 3] || k2 == slotf[(m + 3) & 3])
        continue; /* a corner's own projection */
      lz_need(tab, lk, (int)k2);
      if (T_STATE(s2->tag) != E_VALID && T_STATE(s2->tag) != E_AMBIGUOUS)
        continue;
      rk = res_place(tab, s2, r, &x, &y, &w, &alt, &cl);
      if (!rk)
        continue;
      for (q = 0; q < (rk == 2 ? T_N(s2->tag) : 1u + (alt != 0)); q++) {
        float wd;
        unsigned d;
        if (rk == 2) { /* candidate 0's point: the candidates there (resolve's) */
          const Cand *const cm = &s2->c[q];
          if (cm->dy == FAR_MARK || s2->c[0].dy == FAR_MARK) {
            float cx, cy;
            cand_pos(tab, cm, &cx, &cy);
            if (!near8(cx, cy, x, y))
              continue;
          } else if (cm->dx - s2->c[0].dx > 32 || s2->c[0].dx - cm->dx > 32 ||
                     cm->dy - s2->c[0].dy > 32 || s2->c[0].dy - cm->dy > 32)
            continue;
          wd = cm->w;
        } else
          wd = q ? flt(w * (1.0f / 16)) : w;
        for (d = 0; d < nc; d++)
          if (same_depthb(&c[d].w, &wd, &cost[VCC_LOOK_DIV]))
            break;
        if (d == nc && nc < VC_CANDS) {
          c[nc].dx = c[nc].dy = 0;
          c[nc++].w = wd;
        }
      }
    }
    if (nc) {
      for (i = 0; i < 4; i++) {
        e2[i] = e[i];
        en2[i] = en[i];
      }
      e2[m] = c;
      en2[m] = nc;
      memset(pick, 0, sizeof(int) * 4);
      if (choose(e2, en2, k_all4, 4, cost, pick)) {
        out->w = flt(c[pick[m]].w);
        pick[m] = 0;
        return 3;
      }
    }
  }
  return 0;
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
 * invalid projection, or a first one with no candidate); 2: candidate 0's
 * point (w 0, #214: so the caller needn't compare w), else 1.
 *
 * #214: lc's depth is tested on its bits when it is a normal float (its
 * sign is alt, the rest its size: the float tests' answers), as the
 * float tests before for any other value (0, subnormal, infinite, NaN). */
static int res_place(const Table *tab, const Entry *en, unsigned r, float *x, float *y, float *w,
                     int *alt, int *cl) {
  if (!(tab->log[r] & LOG_LC)) { /* the first, or the first's point again */
    if (!T_N(en->tag))
      return 0;
    cand_pos(tab, &en->c[0], x, y);
    *w = 0;
    *alt = 0;
    *cl = (en->tag & T_CLAMP) != 0;
    return 2;
  } else {
    const Cand *const l = &tab->lc[r];
    const uint32_t u = w_bits(&l->w), mag = u & 0x7FFFFFFFu;
    if (__builtin_expect(mag - W_LO < 0x7F800000u - W_LO, 1)) {
      *alt = (int)(u >> 31);
      memcpy(w, &mag, sizeof mag);
    } else {
      if (l->w == 0)
        return 0;
      *alt = l->w < 0;
      *w = *alt ? -l->w : l->w;
    }
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
    int pass, alt, ccl, cl, onept, rk;
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
        if (!(rk = res_place(tab, en, r, &x, &y, &w, &alt, &ccl))) {
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
        for (m = 0; m < (rk == 2 ? nc : 1u + (alt != 0)); m++) { /* (#214: rk 2, w 0) */
          float wd;
          if (rk == 2) { /* candidate 0's point: the candidates there */
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


/* #216: (key, anchor)'s own point in tab's frame gen, if placed */
static const Ow *own_look(const Table *tb, uint32_t gen, uint32_t key, uint32_t anchor,
                          unsigned *probes, Touch *t) {
  uint32_t h = (key * 0x9E3779B1u ^ anchor * 0x85EBCA77u) >> 21 & (VC_OW - 1), pr;
  for (pr = 0; pr < 8; pr++, h = (h + 1) & (VC_OW - 1)) {
    const Ow *const o = &tb->ow[h];
    ++*probes;
    touch(t, offsetof(Table, ow) + (size_t)h * sizeof(Ow), sizeof(Ow));
    if (o->gen != gen)
      return NULL;
    if (o->key == key && o->anchor == anchor)
      return o->placed ? o : NULL;
  }
  return NULL;
}

/* #216: an own point's place (pixels from key's pixel centre) and depths,
 * from the projection it names, as resolve reads a log place: 1; 0 that
 * projection has no answer (the GTE could not give it: behind the eye);
 * -1 not usable (two points, or farther than lim from its key: 1024; #229:
 * 1048576 for a world piece's corner, whose clamped projection's own place
 * may lie far off the screen: a corner nearly level with the eye) */
static int own_point(const Table *tab, const Sink *k, const Ow *o, uint32_t key, float *dx,
                     float *dy, Cand *c, unsigned *nc, float lim) {
  const Entry *en;
  float x, y, w;
  int e, alt, cl, rk;
  unsigned m, n = 0;
  if (o->dead || o->r >= tab->nlog)
    return -1;
  e = (int)LOG_E(tab->log[o->r]);
  en = &tab->s[e];
  lz_need(tab, k, e);
  rk = res_place(tab, en, o->r, &x, &y, &w, &alt, &cl);
  if (!rk)
    return 0;
  if (rk == 2) { /* candidate 0's point: the candidates there (resolve's) */
    for (m = 0; m < T_N(en->tag); m++) {
      const Cand *const cm = &en->c[m];
      if (cm->dy == FAR_MARK || en->c[0].dy == FAR_MARK) {
        float cx, cy;
        cand_pos(tab, cm, &cx, &cy);
        if (!near8(cx, cy, x, y))
          continue;
      } else if (cm->dx - en->c[0].dx > 32 || en->c[0].dx - cm->dx > 32 ||
                 cm->dy - en->c[0].dy > 32 || en->c[0].dy - cm->dy > 32)
        continue;
      c[n].dx = c[n].dy = 0;
      c[n++].w = cm->w;
    }
  } else {
    c[n].dx = c[n].dy = 0;
    c[n++].w = w;
    if (alt) {
      c[n].dx = c[n].dy = 0;
      c[n++].w = flt(w * (1.0f / 16));
    }
  }
  if (!n)
    return 0;
  x = flt(x + ((short)(en->key & 0xFFFF) + 0.5f) + (float)o->ox - ((short)(key & 0xFFFF) + 0.5f));
  y = flt(y + ((short)(en->key >> 16) + 0.5f) + (float)o->oy - ((short)(key >> 16) + 0.5f));
  if (!(x < lim && x > -lim && y < lim && y > -lim))
    return -1;
  *dx = x;
  *dy = y;
  *nc = n;
  return 1;
}

/* #229: (key, anchor)'s world point kept as its piece's own (Sp) in tab's
 * frame gen: its place (pixels from key's pixel centre) and depths into c:
 * 1; -1 none usable (none, or two different points) */
static int sp_point(const Table *tb, uint32_t gen, uint32_t key, uint32_t anchor, float *dx,
                    float *dy, Cand *c, unsigned *nc, int *ent, unsigned *probes, Touch *t) {
  uint32_t h = (key * 0x9E3779B1u ^ anchor * 0x85EBCA77u) >> 21 & (VC_SP - 1), pr;
  unsigned j;
  for (pr = 0; pr < 8; pr++, h = (h + 1) & (VC_SP - 1)) {
    const Sp *const o = &tb->sp[h];
    ++*probes;
    touch(t, offsetof(Table, sp) + (size_t)h * sizeof(Sp), sizeof(Sp));
    if (o->gen != gen)
      return -1;
    if (o->key == key && o->anchor == anchor) {
      if (!o->placed || o->dead || !o->n)
        return -1;
      *dx = o->dx;
      *dy = o->dy;
      *ent = o->r != SP_NOR && o->r < tb->nlog ? (int)LOG_E(tb->log[o->r]) : -1;
      for (j = 0; j < o->n; j++) {
        c[j].dx = c[j].dy = 0;
        c[j].w = o->w[j];
      }
      *nc = o->n;
      return 1;
    }
  }
  return -1;
}

int psx_vcache_polys;

/* The whole lookup (psx_vcache_poly's counts of the polygon already made):
 * every polygon until #233; since, those found_fast doesn't take (below) */
static __attribute__((noinline)) void poly_full(void *vertex_, short *const *xy, int n,
                                                VCacheVert *side) {
  static const int k_all[4] = {0, 1, 2, 3};
  GrVertex *const vertex = (GrVertex *)vertex_;
  const Cand *e[4];   /* each vertex's candidates, NULL: not found */
  uint32_t et[4];     /* and its slot's tag */
  unsigned en[4];     /* how many */
  uint32_t ref[4], persp = VREF_PERSP;
  const Table *ptab = NULL; /* the table its vertices were looked up in (the same for each) */
  int i, pick[4] = {0, 0, 0, 0};
  int inv = 0, clamp = 0; /* a vertex's key invalid; a clamped vertex found (#178) */
  int sky = -1; /* #221: a SkyDraw polygon (-1: not asked yet) */
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
  int hkfr[4] = {0, 0, 0, 0}; /* #215: hkc is a cut's place at its own fraction */
  int nflat = 0; /* ... those with a place only */
  int nhk = 0;
  /* #216: vertices with no entry; those found from their renderer's own
   * point, its place and depths; and those whose own point has no
   * projection (exempt) */
  int noent[4] = {0, 0, 0, 0}, ow[4] = {0, 0, 0, 0}, nnp = 0, npv[4] = {0, 0, 0, 0};
  float owp[4][2];
  Cand oc[4][VC_CANDS];
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

  for (i = 0; i < n; i++) {
    uint32_t key = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
    Touch t = TOUCH0;
    const Table *tab;
    int k = look(pres, key, &cost[VCC_LOOK_PROBES], &t, &tab);
    uint32_t tag;
    if (k < 0 && s_ov &&
        tab->ov_gen == (pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen)) {
      /* #221: a SkyDraw polygon's vertex with no entry in the slots: its
       * record's in the overflow if its probe ran out (only this reads it,
       * and only in a frame whose records made one) */
      const uint32_t g = tab->ov_gen;
      if (sky < 0)
        sky = cx_find(tab, g, xy[0]) == VCX_SKY;
      if (sky && (k = ov_find((Table *)tab, g, key, 0, 0, &cost[VCC_LOOK_PROBES])) >= 0)
        st->ov_found++;
    }
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
        noent[i] = 1; /* #216 */
        /* its renderer's hand-over, if any (#189; used below); else its
         * cut's place at its own fraction (#215, fr: hkfr) */
        const uint32_t hgen = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen;
        hkc[i] = hk_look(tab, hgen, key, &cost[VCC_LOOK_PROBES], &t);
        if (!hkc[i] && (hkc[i] = fr_look(tab, hgen, key, &cost[VCC_LOOK_PROBES], &t)) != NULL)
          hkfr[i] = 1;
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
      st->der_own += der == 2; /* #202 */
      st->der_named += der == 3; /* #217 */
      der = 1;
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
    int pass;
    for (pass = 0; pass < 2; pass++) {
      /* #215: a cut's place at its own fraction after the hand-overs, and
       * only where #197 below wouldn't take the polygon (its one vertex
       * with no entry and its ambiguous ones): every polygon found before
       * is found as it was. Only for the portal sky's polygons (or one the
       * frame names no renderer for: Vita3K filed the sky's there): a
       * place is a pixel's, and another renderer's vertex with no entry
       * may share it */
      if (pass && (!nnone || (n == 4 && nambig && nnone == 1 && !nhk && !inv &&
                              nmiss == nambig + 1)))
        break;
      if (pass) { /* and not for another renderer's polygon at the cut's pixel (a glow's) */
        const int cx = cx_find(ptab, pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen,
                               xy[0]);
        if (cx != VCX_PORTAL_SKY && cx != VCX_PORTAL_SKY_FOG && cx != VCX_OUTSIDE &&
            cx != VCX_UNMARKED)
          break;
      }
      for (i = 0; i < n; i++)
        if (!e[i] && hkc[i] && !hkc[i]->asrc && hkfr[i] == pass) {
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
          st->hk_frac += (unsigned)pass;
        }
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
    derive_place(tab, gen, xy, slotf, none, &lk, cost, &pv, &slot, &dr, 0, NULL, NULL);
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
  if (!res && !der && nnone && !inv && !nflat) {
    /* #202: the vertices with no entry all an ambiguous key's alias or
     * offset (a shadow's nudged corner, a glow's rim): each resolved as
     * an ambiguous vertex under its source's key, drawn at its own key */
    int na = 0, aal[4] = {0, 0, 0, 0};
    for (i = 0; i < n; i++)
      if (!e[i] && !amb[i] && !hk[i] && hkc[i] && hkc[i]->asrc)
        aal[i] = 1, na++;
    if (na == nnone && nambig + na == nmiss) {
      const uint32_t gen = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen;
      const Cand *e2[4];
      unsigned en2[4];
      int one, ok = 0, why2 = -1;
      for (i = 0; i < n; i++) {
        if (aal[i]) {
          amb[i] = 1;
          ks[i] = (int)hkc[i]->asrc - 1;
        } else if (!amb[i])
          ks[i] = e[i] && !hk[i] ? (int)((ref[i] & VREF_SLOT) - 1) : -1;
      }
      if (resolve((Table *)ptab, gen, ks, amb, n, rc, rn, rp, rx, cost, &one, &why2)) {
        for (i = 0; i < n; i++) {
          e2[i] = amb[i] ? rc[i] : e[i];
          en2[i] = amb[i] ? rn[i] : en[i];
          if (aal[i] && rx[i])
            break; /* a clamped point's own place: not this vertex's */
        }
        ok = i == n && choose(e2, en2, k_all, n, cost, pick);
      }
      if (ok) {
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
        st->hits += (unsigned)(nambig + na);
        st->resolved += (unsigned)nambig;
        st->res_order += one ? (unsigned)nambig : 0;
        st->res_alias += (unsigned)na;
        nnone = 0;
        none = -1;
      } else {
        for (i = 0; i < n; i++)
          if (aal[i])
            amb[i] = 0;
        memset(pick, 0, sizeof pick);
        memset(rx, 0, sizeof rx);
      }
    }
  }
  if (!res && !der && (nnone || nambig) && !nflat) {
    /* #216: a glow's or moby shadow's polygon nothing above found: each
     * vertex not found (no entry, or an ambiguous key) at its renderer's
     * own point, from the projection that made it, under the polygon's
     * first vertex's key; only when every one has one. Its found vertices
     * with one take theirs too (ow 2), so the depths are all the
     * renderer's own projections'; if those choose none, as found.
     * #217: a world piece's too (the last list's, whose corners the
     * renderer named: a corner the GTE clamped onto a key other points
     * share). */
    const uint32_t gen = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen;
    const int cx = cx_find(ptab, gen, xy[0]);
    if (cx == VCX_GLOWS || cx == VCX_MOBY_SHADOWS || cx == VCX_ENV) { /* (#217: a split piece's) */
      /* (#229: a world piece's, its corners' keys and u/v mixed, as
       * env_hand.c PieceAnchor: each u/v the primitive's halfword after its
       * x/y; a quad's vertices here are its primitive's 0 1 3 2) */
      uint32_t anchor = (uint16_t)xy[0][0] | (uint32_t)(uint16_t)xy[0][1] << 16;
      if (cx == VCX_ENV) {
        static const int k_prim[2][4] = {{0, 1, 2, 3}, {0, 1, 3, 2}};
        anchor = 0;
        for (i = 0; i < n; i++)
          anchor ^= VCD_PIECE_V((uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16, xy[i][2],
                                k_prim[n == 4][i]);
      }
      const Cand *e2[4];
      unsigned en2[4], eo[4];
      int m = 0, all = !inv, ok = 0, spv[4] = {0, 0, 0, 0};
      /* #229: each own point's projection's entry (-1: none named); a world
       * quad's ambiguous corners with none, resolved from the frame's order
       * near the rest (ares) */
      int oent[4] = {-1, -1, -1, -1}, ares[4] = {0, 0, 0, 0}, nares = 0, part = 0;
      /* #242: a found vertex's own point from own_corner (read only after the
       * rest fail), and those whose own point was taken first (ow 2) */
      const Ow *o2[4] = {NULL, NULL, NULL, NULL};
      int had[4] = {0, 0, 0, 0};
      int cen = 0; /* #226: the centre's own point taken under VCD_CENTRE */
      for (i = 0; i < n; i++) {
        const uint32_t key = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
        Touch t = TOUCH0;
        const Ow *o;
        int r;
        e2[i] = e[i];
        en2[i] = en[i];
        if (!e[i] && !noent[i] && !amb[i]) { /* invalid */
          all = 0;
          continue;
        }
        o = own_look(ptab, gen, key, cx == VCX_ENV ? VCD_CORNER(anchor, i) : anchor,
                     &cost[VCC_LOOK_PROBES], &t); /* (#217: a world piece's corner i) */
        if (o && o->placed == 2 && e[i]) { /* #242: own_corner's: a vertex not found's first */
          o2[i] = o;
          o = NULL;
        }
        if (cx == VCX_MOBY_SHADOWS && i == 0 && !e[i] && (!o || o->dead)) {
          /* #226: a shadow's centre not found whose own point under (centre,
           * centre) a corner on its pixel killed: its own under VCD_CENTRE
           * (at base such a polygon was never found here: all 0) */
          const Ow *const c0 = own_look(ptab, gen, key, VCD_CENTRE(anchor), &cost[VCC_LOOK_PROBES], &t);
          if (c0) {
            o = c0;
            cen = 1;
          }
        }
        r = o ? own_point(ptab, &lk, o, key, &owp[i][0], &owp[i][1], oc[i], &eo[i],
                          cx == VCX_ENV ? 1048576.0f : 1024.0f)
              : -1;
        if (o && o->r < ptab->nlog)
          oent[i] = (int)LOG_E(ptab->log[o->r]);
        if (!o && cx == VCX_ENV) { /* #229: its piece's split point or moved corner */
          r = sp_point(ptab, gen, key, VCD_CORNER(anchor, i), &owp[i][0], &owp[i][1], oc[i],
                       &eo[i], &oent[i], &cost[VCC_LOOK_PROBES], &t);
          if (r != 1 && !e[i] && noent[i]) /* ... or its grid's moved point */
            r = sp_point(ptab, gen, key, VCD_MOVED, &owp[i][0], &owp[i][1], oc[i], &eo[i],
                         &oent[i], &cost[VCC_LOOK_PROBES], &t);
          spv[i] = r == 1;
        }
        cost[VCC_LOOK_LINES] += t.n;
        if (r == 1) {
          ow[i] = e[i] ? 2 : 1;
          e2[i] = oc[i];
          en2[i] = eo[i];
          m += !e[i];
        } else if (!e[i]) {
          if (r == -1 && amb[i] && cx == VCX_ENV) {
            ares[i] = 1; /* #229: resolved below, if the rest are found */
            nares++;
            continue;
          }
          all = 0;
          if (r == 0 && noent[i])
            npv[i] = 1, nnp++;
        }
      }
      if (all && m && nares) {
        /* #229: a world quad's corner the grid moved (its own point) and its
         * ambiguous ones (none): those resolved from the frame's order near
         * the polygon's other places, as #197 does with derive's guess, here
         * with the moved corner's own projection */
        int ks2[4], one, why2;
        for (i = 0; i < n; i++)
          ks2[i] = ares[i] ? ks[i] : ow[i] ? oent[i] : e[i] && !hk[i] ? (int)((ref[i] & VREF_SLOT) - 1) : -1;
        if (resolve((Table *)ptab, gen, ks2, ares, n, rc, rn, rp, rx, cost, &one, &why2)) {
          for (i = 0; i < n; i++)
            if (ares[i]) {
              e2[i] = rc[i];
              en2[i] = rn[i];
            }
        } else {
          /* none: the moved corner is still found at its own point, and the
           * polygon drawn affine with its ambiguous corners at their keys
           * (as any polygon with an ambiguous vertex left) */
          all = 0;
          part = 1;
          memset(rx, 0, sizeof rx);
        }
      } else if (nares)
        all = 0;
      if (all && m) {
        ok = choose(e2, en2, k_all, n, cost, pick);
        if (!ok) { /* as found, the rest at their own points */
          for (i = 0; i < n; i++)
            if (ow[i] == 2) {
              ow[i] = 0;
              had[i] = 1;
              e2[i] = e[i];
              en2[i] = en[i];
            }
          ok = choose(e2, en2, k_all, n, cost, pick);
        }
        if (!ok && cx == VCX_MOBY_SHADOWS) {
          /* #242: then every found vertex at its own point, own_corner's
           * too (two nudged corners of one far shadow from one key: one found
           * at another point's key); only a polygon found no other way */
          int any = 0;
          for (i = 0; i < n; i++)
            if (had[i] ||
                (o2[i] && own_point(ptab, &lk, o2[i],
                                    (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16,
                                    &owp[i][0], &owp[i][1], oc[i], &eo[i], 1024.0f) == 1)) {
              ow[i] = 2;
              e2[i] = oc[i];
              en2[i] = eo[i];
              any |= o2[i] != NULL;
            }
          ok = any && choose(e2, en2, k_all, n, cost, pick);
          if (!ok)
            for (i = 0; i < n; i++)
              if (ow[i] == 2) {
                ow[i] = 0;
                e2[i] = e[i];
                en2[i] = en[i];
              }
        }
        if (!ok && cx == VCX_ENV) {
          /* #229: a world piece whose depths no choice brings under 4 times
           * apart (a split point between ends 4 times apart or more, kept
           * as its piece's own): found, and drawn affine at those places, as
           * any polygon found whole whose depths are so far apart (spread) */
          memset(pick, 0, sizeof pick);
          ok = 2;
        }
      }
      if (ok) {
        for (i = 0; i < n; i++)
          if (ares[i]) { /* #229 (as #197's) */
            e[i] = rc[i];
            en[i] = rn[i];
            et[i] = rx[i] ? (ptab->s[ks[i]].tag & ~T_MULTI) | T_CLAMP
                          : (ptab->s[ks[i]].tag | T_MULTI) & ~T_CLAMP;
            ref[i] |= (uint32_t)ks[i] + 1;
            amb[i] = 0;
            nambig--;
            nmiss--;
            st->hits++;
            st->resolved++;
            st->sp_res++;
          }
        for (i = 0; i < n; i++)
          if (ow[i]) {
            e[i] = oc[i];
            en[i] = eo[i];
            et[i] = 0;
            if (ow[i] == 2) {
              hk[i] = NULL;
              st->own_over++;
              continue;
            }
            if (amb[i]) {
              amb[i] = 0;
              nambig--;
              st->own_amb++;
            } else
              nnone--;
            nmiss--;
            st->hits++;
            st->own_found++;
            st->sp_found += (unsigned)spv[i];
            st->own_centre += (unsigned)(i == 0 && cen); /* #226 */
          }
        persp = ok == 2 ? 0 : VREF_PERSP; /* (#229: spread, drawn affine) */
        st->spread += ok == 2;
        st->sp_spread += ok == 2;
        res = 1;
        none = -1;
      } else if (part) { /* #229 (above) */
        for (i = 0; i < n; i++)
          if (ow[i] == 1) {
            e[i] = oc[i];
            en[i] = eo[i];
            et[i] = 0;
            if (amb[i]) {
              amb[i] = 0;
              nambig--;
              st->own_amb++;
            } else
              nnone--;
            nmiss--;
            st->hits++;
            st->own_found++;
            st->sp_found += (unsigned)spv[i];
            st->sp_part++;
          } else
            ow[i] = 0;
        memset(pick, 0, sizeof pick);
      } else {
        memset(ow, 0, sizeof ow);
        memset(pick, 0, sizeof pick);
        if (nares)
          memset(rx, 0, sizeof rx);
      }
    }
  }
  if (!res && !der && (nnone || nambig) && !inv && !nflat) {
    /* #242: a SpyroDraw polygon nothing above found: each vertex not found
     * (no entry, or an ambiguous key) at its polygon's own point (VCD_KEEP:
     * two clip points on one pixel, so the hand-over left the key with none;
     * two of his vertices on one pixel), under the anchor its renderer gave
     * (its vertices' keys and u/v, as #229's world pieces; psx_vcache.h
     * VCD_PIECE_V) and its place in the primitive; only when every one has
     * one. Its found vertices stay as they are. */
    const uint32_t gen = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen;
    if (cx_find(ptab, gen, xy[0]) == VCX_SPYRO) {
      static const int k_prim[2][4] = {{0, 1, 2, 3}, {0, 1, 3, 2}};
      const Cand *e2[4];
      unsigned en2[4], eo[4];
      uint32_t anchor = 0;
      int m = 0, all = 1, ent;
      for (i = 0; i < n; i++)
        anchor ^= VCD_PIECE_V((uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16, xy[i][2],
                              k_prim[n == 4][i]);
      for (i = 0; i < n && all; i++) {
        const uint32_t key = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
        Touch t = TOUCH0;
        e2[i] = e[i];
        en2[i] = en[i];
        if (e[i])
          continue;
        if ((noent[i] || amb[i]) &&
            sp_point(ptab, gen, key, VCD_CORNER(anchor, k_prim[n == 4][i]), &owp[i][0], &owp[i][1],
                     oc[i], &eo[i], &ent, &cost[VCC_LOOK_PROBES], &t) == 1) {
          ow[i] = 1;
          e2[i] = oc[i];
          en2[i] = eo[i];
          m++;
        } else
          all = 0;
        cost[VCC_LOOK_LINES] += t.n;
      }
      if (all && m && choose(e2, en2, k_all, n, cost, pick)) {
        for (i = 0; i < n; i++)
          if (ow[i]) {
            e[i] = oc[i];
            en[i] = eo[i];
            et[i] = 0;
            if (amb[i]) {
              amb[i] = 0;
              nambig--;
              st->own_amb++;
            } else
              nnone--;
            nmiss--;
            st->hits++;
            st->own_found++;
            st->pk_found++;
          }
        persp = VREF_PERSP;
        res = 1;
        none = -1;
      } else {
        memset(ow, 0, sizeof ow);
        memset(pick, 0, sizeof pick);
      }
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
    const uint32_t gen = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].gen : s_rgen;
    const int cx = cx_find_(ptab, gen, xy[0], 1); /* (#222: by offset, the copy's too) */
    st->miss_none += (unsigned)nnone;
    if (cx == VCX_ENV) /* #217: a world split point with an end behind the eye, exempt too */
      for (i = 0; i < n; i++)
        if (noent[i] && !e[i] && !hk[i] && !hkc[i] && !npv[i]) {
          const uint32_t key = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
          Touch t = TOUCH0;
          if (hk_noproj(ptab, gen, key, &cost[VCC_LOOK_PROBES], &t))
            nnp++;
          cost[VCC_LOOK_LINES] += t.n;
        }
    /* #211: whose polygon; #216: a glow's or shadow's point with no projection, exempt */
    st->ne[cx] += (unsigned)(nnone - nnp);
    st->ne[VCX_NO_PROJ] += (unsigned)nnp;
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
    if ((et[i] & T_CLAMP) || (i == none && der_moved) || hk[i] || rx[i] || ow[i]) /* (#216) */
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
        if (ow[i]) { /* #216 */
          c[i].dx = owp[i][0];
          c[i].dy = owp[i][1];
        } else if (hk[i]) { /* #189 */
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

/* #233: the walk's lookup of a polygon whose vertices are all found (99% of
 * them: hardware session 7's Toasty, logs/hw-ap18t), on a short path of its
 * own. poly_full keeps every case's state (the hand-overs, derived and
 * ambiguous vertices, own points: some 1.4 KB of stack, set up for each
 * polygon), so on the A9 its found polygons paid that set-up and the tests
 * for each case. Here the lookups come first, as poly_full's loop makes
 * them (look, then the entry's kept projections, lz_need); the first
 * vertex not found valid hands the polygon to poly_full, which looks every
 * vertex up again: the kept projections are worked out once (an entry's
 * list is emptied as it's worked out, and working one entry's out changes
 * no other entry), and the probes and lines counted here are dropped, so
 * poly_full counts each lookup as before. A polygon found whole is then
 * finished as poly_full finishes one: the depth choice, its counts, the
 * vertices' references and the answers at side. */
static int s_found_fast = 2; /* psx_vcache_set_found_fast */
void psx_vcache_set_found_fast(int mode) { s_found_fast = mode < 0 ? 0 : mode > 2 ? 2 : mode; }

void psx_vcache_poly(void *vertex_, short *const *xy, int n, VCacheVert *side) {
  static const int k_all[4] = {0, 1, 2, 3};
  GrVertex *const vertex = (GrVertex *)vertex_;
  const GrVertex *const gvb = s_pres ? NULL : psx_gpu_game_walk()->vb; /* (as poly_full's) */
  const int async = !s_pres && !(vertex >= gvb && vertex < gvb + MAX_VERTEX_BUFFER_SIZE) &&
                    s_pres_async;
  const int pres = s_pres || async;
  VCacheStats *const st = &g_vcache_stats[pres];
  unsigned *const work = async ? g_vcache_pres_work : g_vcache_work;
  unsigned *const cost = COST(work);
  const Cand *e[4];
  uint32_t et[4], ref[4], persp = VREF_PERSP;
  unsigned en[4], probes = 0, lines = 0, other = 0;
  const Table *tab = NULL;
  int i, pick[4] = {0, 0, 0, 0}, clamp = 0;

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
  if (!s_found_fast) {
    poly_full(vertex_, xy, n, side);
    return;
  }
  {
    /* where the kept projections' maths goes: poly_full's lk */
    const Sink lk = pres ? (Sink){work, cost, st, NULL}
                         : s_rtab ? (Sink)SINK_ACC(1) : (Sink)SINK_ACC(0);
    uint32_t keys[4];
    for (i = 0; i < n; i++)
      keys[i] = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
    if (s_found_fast == 2) {
      /* each vertex's first probe read up front (n is 3 or 4), so a
       * polygon's misses on the table (another core wrote it, a frame ago)
       * can overlap on the A9 instead of each waiting for the one before;
       * nothing is decided here. Vita3K shows no change (#233): only the
       * hardware can tell, hence mode 1 without it. */
      const Table *const lt = pres || !s_rtab ? s_rec[pres ? VC_PRES : VC_LOGIC].tab : s_rtab;
      uint32_t w0, w1, w2, w3 = 0;
      w0 = lt->s[hash(keys[0])].tag;
      w1 = lt->s[hash(keys[1])].tag;
      w2 = lt->s[hash(keys[2])].tag;
      if (n == 4)
        w3 = lt->s[hash(keys[3])].tag;
      __asm__ volatile("" : : "r"(w0), "r"(w1), "r"(w2), "r"(w3));
    }
    for (i = 0; i < n; i++) {
      const uint32_t key = keys[i];
      Touch t = TOUCH0;
      const int k = look(pres, key, &probes, &t, &tab);
      uint32_t tag;
      if (k < 0) {
        poly_full(vertex_, xy, n, side);
        return;
      }
      lz_need(tab, &lk, k); /* (#193, #179) */
      tag = tab->s[k].tag;
      if (T_STATE(tag) != E_VALID || !T_N(tag)) {
        poly_full(vertex_, xy, n, side);
        return;
      }
      touch(&t, (size_t)k * sizeof(Entry) + offsetof(Entry, c), sizeof(Cand) * T_N(tag));
      if (tab->s[k].c[0].dy == FAR_MARK && !(tag & T_MULTI))
        touch(&t, offsetof(Table, far) + (size_t)(uint16_t)tab->s[k].c[0].dx * sizeof tab->far[0],
              sizeof tab->far[0]);
      clamp |= (tag & T_CLAMP) != 0;
      e[i] = tab->s[k].c;
      et[i] = tag;
      en[i] = T_N(tag);
      ref[i] = VREF_POLY | ((uint32_t)k + 1);
      other += !(tag & T_SRC) != !pres;
      lines += t.n;
    }
  }
  /* found whole: poly_full's counts and its end for such a polygon */
  cost[VCC_LOOK_PROBES] += probes;
  cost[VCC_LOOK_LINES] += lines;
  st->hits += (unsigned)n;
  st->other += other;
  if (!choose(e, en, k_all, n, cost, pick)) {
    st->spread++;
    persp = 0;
  }
  if (persp)
    st->persp++;
  if (clamp) {
    st->clamp_polys++;
    st->clamp_persp += persp != 0;
  }
  for (i = 0; i < n; i++) {
    uint32_t r = ref[i] | persp | (uint32_t)pick[i] << VREF_CAND_SHIFT;
    if (et[i] & T_MULTI)
      r |= VREF_NOPOS;
    if (et[i] & T_CLAMP)
      r |= VREF_CLAMP;
    memcpy(&vertex[i].tcx, &r, sizeof r);
  }
  if (side && async)
    side = s_pres_side ? s_pres_side + (side - g_vcache_side) : NULL;
  if (side) {
    static const int k_quad[6] = {0, 1, 3, 1, 3, 2}, k_tri[3] = {0, 1, 2};
    const int *order = n == 4 ? k_quad : k_tri;
    VCacheVert c[4];
    for (i = 0; i < n; i++) {
      c[i].dx = c[i].dy = 0;
      c[i].w = 1;
      if (!(et[i] & T_MULTI))
        cand_pos(tab, &e[i][0], &c[i].dx, &c[i].dy);
      if (persp)
        c[i].w = e[i][pick[i]].w;
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
  if (s_lazy_logic == LZ_QUOTA) { /* #204: the next frame's quota */
    if (r->tab->lazy)
      qt_frame_end(s_qt.n);
    else { /* (beside in-between frames: worked out at once) */
      s_qt.q = QT_START;
      s_qt.nh = 0;
      s_qt.ends = __atomic_load_n(&s_walk.ends, __ATOMIC_ACQUIRE);
    }
    s_qt.n = 0;
  }
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
  lz_next(r->tab, (s_lazy_logic == 2 || /* not tied to s_lazy (#198) */
                   ((s_lazy_logic == 1 || s_lazy_logic == LZ_QUOTA) && s_since_pres > LZ_QUIET)));
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
  if (s_lazy_logic == LZ_QUOTA) { /* #204: the quota's clock, on the walk's thread */
    if (f)
      s_walk.start_us = qt_now();
    else {
      s_walk.end_us = qt_now();
      __atomic_fetch_add(&s_walk.ends, 1, __ATOMIC_RELEASE);
    }
  }
  __atomic_store_n(&s_walk.busy, f != NULL, __ATOMIC_RELAXED); /* #179 */
  s_rgen = f ? f->gen : 0;
}

int psx_vcache_get(uint32_t ref, VCacheVert *out) {
  uint32_t slot = ref & VREF_SLOT, d = (ref >> VREF_CAND_SHIFT) & 3;
  const Table *t = s_rec[VC_LOGIC].tab;
  if (!slot || slot > VC_SIZE + VC_OV) /* (#221: the overflow's too) */
    return 0;
  if (d >= T_N(t->s[slot - 1].tag))
    d = 0;
  cand_pos(t, &t->s[slot - 1].c[0], &out->dx, &out->dy); /* the same for every polygon */
  out->w = t->s[slot - 1].c[d].w;
  return 1;
}

void psx_vcache_enable(int mode) {
#ifndef __vita__
  { /* #221: VC_OVERFLOW=0, the probe's 32 slots as before (host builds) */
    const char *const v = getenv("VC_OVERFLOW");
    if (v && v[0] == '0')
      psx_vcache_set_overflow(0);
  }
#endif
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

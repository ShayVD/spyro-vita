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
 * 4096). psx_vcache_poly then picks, for each polygon, the candidates whose depths
 * have the smallest spread: one point at another scale is 4 or 16 times
 * off, another point on the same pixel is as far off as it is in depth,
 * and a real polygon's depths are much closer. A key with more than four
 * is ambiguous and never used.
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
 * position is the clamped one). Still invalid: a depth of 0 or less, and a
 * key that isn't the GTE's answer for these inputs.
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
 * The cost (#159; D89 measured the cache at ~4.5 ms a vblank of the game
 * thread on hardware): every answer is as before, with less work. A record
 * finds its entry before the maths, and an ambiguous key needs none. Depth
 * ratios are compared without dividing unless the quotient's rounding
 * could decide the comparison (same_depth, under4, under3999). An entry is
 * 40 bytes, not 44. The g_vcache_cost counters count the divisions, probes
 * and table lines. */
#include "psx_vcache.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "psx/gtereg.h"
#include "psx/libgpu.h"

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
#define T_GEN(t) ((t) >> 8)
#define VC_GEN_END (1u << 24)      /* a table's frames count 1 to this, then from 1 again */

typedef struct {
  uint32_t key; /* SXY2: (SY << 16) | (SX & 0xFFFF) */
  uint32_t tag;
  Cand c[VC_CANDS];
} Entry;

typedef struct {
  Entry s[VC_SIZE];
  int stale; /* holds frames from before its count came round: cleared before it records */
  /* the exact positions (pixels from the pixel centre) of this table's
   * frame's candidates that don't fit an int16 (#178): only indexed by the
   * frame's own entries, so never cleared */
  float far[VC_FAR][2];
} Table;

/* A frame's table: the projections of one kind of frame (VC_LOGIC, VC_PRES)
 * record into their own, so each frame looks up only its own (#158) */
typedef struct {
  Table *tab;     /* the table recording */
  uint32_t gen;   /* its frame */
  unsigned count; /* entries this frame */
  unsigned nfar;  /* the table's far positions this frame (#178) */
} Rec;

static Table s_tab0 __attribute__((aligned(64)));
static VCacheVert *s_pres_side; /* the presenter's answers while it runs on its own thread (#162) */
static Table *s_tab1;                  /* the logic draw's other table (D71) */
static Rec s_rec[2] = {{&s_tab0, 1, 0, 0}, {NULL, 1, 0, 0}}; /* the presenter's: psx_vcache_pres_begin */
static const Table *s_rtab;            /* the walk's table and frame, NULL: this one */
static uint32_t s_rgen;

/* a table on the heap, line-aligned like s_tab0 (never freed) */
static Table *table_new(void) {
  char *const p = calloc(1, sizeof(Table) + 63);
  return p ? (Table *)(((uintptr_t)p + 63) & ~(uintptr_t)63) : NULL;
}

static void table_clear(Table *t) {
  memset(t->s, 0, sizeof t->s);
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
unsigned g_vcache_work[VCW_N], g_vcache_pres_work[VCW_N];
/* What that work cost (issue #159), the same way, counts that don't depend
 * on timing: float divisions, the table's slots a record's or a lookup's
 * probe examined, and the 32-byte lines of the table each touched (a model:
 * the lines its accesses fall in, from a line-aligned table). */
unsigned g_vcache_cost[VCC_N], g_vcache_pres_cost[VCC_N];
#define COST(work) ((work) == g_vcache_work ? g_vcache_cost : g_vcache_pres_cost)

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
}

void psx_vcache_stats_sum(VCacheStats *out) {
  const unsigned *a = (const unsigned *)&g_vcache_stats[0], *b = (const unsigned *)&g_vcache_stats[1];
  unsigned *o = (unsigned *)out;
  size_t i;
  for (i = 0; i < sizeof *out / sizeof(unsigned); i++)
    o[i] = a[i] + b[i];
  out->peak = g_vcache_stats[0].peak > g_vcache_stats[1].peak ? g_vcache_stats[0].peak
                                                               : g_vcache_stats[1].peak;
  cost_line();
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
      s[i].tag = gen << 8 | (kind == VC_PRES ? T_SRC : 0);
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
static int add_cand(Rec *rec, Entry *s, int16_t dx, int16_t dy, float px, float py, float w,
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
      cand_pos(rec->tab, &c[j], &ax, &ay);
      if (c[j].dy == FAR_MARK)
        touch(t, offsetof(Table, far) + (size_t)(uint16_t)c[j].dx * sizeof rec->tab->far[0],
              sizeof rec->tab->far[0]);
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
    if (rec->nfar >= VC_FAR)
      return -1;
    reuse = (int)rec->nfar++;
    rec->tab->far[reuse][0] = px;
    rec->tab->far[reuse][1] = py;
    touch(t, offsetof(Table, far) + (size_t)reuse * sizeof rec->tab->far[0],
          sizeof rec->tab->far[0]);
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

/* PsyX_GTE.cpp's VX/VY/VZ for idx 0-2 */
#define V_X(n) (gteRegs.CP2D.p[(n) << 1].sw.l)
#define V_Y(n) (gteRegs.CP2D.p[(n) << 1].sw.h)
#define V_Z(n) (gteRegs.CP2D.p[((n) << 1) + 1].sw.l)

/* One projection: the vertex, what the GTE gave (key = SXY2, sz = SZ3) and
 * the registers it read, proj = R11-R33, TRX-TRZ, OFX, OFY, H (PROJ_*). */
enum { PROJ_TR = 9, PROJ_OFX = 12, PROJ_OFY = 13, PROJ_H = 14, PROJ_N = 15 };

static void rtp_point(int kind, unsigned *work, const int *proj, int vx, int vy, int vz,
                      uint32_t key, int sz, int sf) {
  unsigned *const cost = COST(work);
  Rec *const rec = &s_rec[kind];
  VCacheStats *const st = &g_vcache_stats[kind];
  const int sx = (short)(key & 0xFFFF), sy = (short)(key >> 16), h = proj[PROJ_H];
  const int *rm = proj, *tr = proj + PROJ_TR;
  int64_t mx, my, mz;
  float x, y, z, r, iz, isz, ox, oy, ex, ey, gx, gy, qd, px = 0, py = 0, w = 0, w_alt = 0;
  Touch t = TOUCH0;
  Entry *s;
  unsigned prev, state = E_INVALID;
  int16_t fx = 0, fy = 0;
  int i, ix, iy, clamped = 0;

  st->records++;
  work[0]++;
  /* The entry first (made if it's new): a key already ambiguous stays so,
   * whatever this projection is, so it needs none of the maths below. */
  i = find(rec, kind, key, 1, &cost[VCC_REC_PROBES], &t);
  if (i < 0) {
    st->full++;
    cost[VCC_REC_LINES] += t.n;
    return;
  }
  s = &rec->tab->s[i];
  prev = T_STATE(s->tag);
  if (prev == E_AMBIGUOUS) {
    cost[VCC_REC_LINES] += t.n;
    return;
  }
  /* the 44-bit sums the GTE shifts into MAC1-3 */
  mx = ((int64_t)tr[0] << 12) + (int64_t)(rm[0] * vx) + (rm[1] * vy) + (rm[2] * vz);
  my = ((int64_t)tr[1] << 12) + (int64_t)(rm[3] * vx) + (rm[4] * vy) + (rm[5] * vz);
  mz = ((int64_t)tr[2] << 12) + (int64_t)(rm[6] * vx) + (rm[7] * vy) + (rm[8] * vz);
  /* Spyro's renderers always set sf. A depth of 0 or less (the GTE's SZ3
   * or the exact one) has no projection. */
  if (!sf || sz <= 0) {
    st->inv_depth++;
    goto done;
  }
  x = (float)(int32_t)(mx >> 12) + (float)(mx & 0xFFF) * (1.0f / 4096);
  y = (float)(int32_t)(my >> 12) + (float)(my & 0xFFF) * (1.0f / 4096);
  z = (float)(int32_t)(mz >> 12) + (float)(mz & 0xFFF) * (1.0f / 4096);
  ox = (float)proj[PROJ_OFX] * (1.0f / 65536);
  oy = (float)proj[PROJ_OFY] * (1.0f / 65536);
  if (z <= 0) {
    st->inv_depth++;
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
    st->inv_key++;
    goto done;
  }
  if (!tr[0] && !tr[1] && !tr[2] && !((vx | vy | vz) & 15) && vx >= -4096 && vx < 4096 &&
      vy >= -4096 && vy < 4096 && vz >= -4096 && vz < 4096)
    w_alt = flt(z * (1.0f / 16)); /* maybe r_environment's x16 precision path */
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
      cost[VCC_REC_LINES] += t.n;
      return;
    }
  } else if (prev != state || state != E_VALID) {
    /* a clamped projection and another one under the same key: the prims
     * that use it can't be told apart */
    if (!(prev == E_INVALID && state == E_INVALID)) {
      st->ambiguous++;
      set_state(s, E_AMBIGUOUS);
    }
    cost[VCC_REC_LINES] += t.n;
    return;
  }
  /* Another projection under this key: the same point again (shared by
   * several polygons), the same point at another scale, or a different
   * point on the same pixel. Each is a candidate; each polygon picks. */
  touch(&t, (size_t)i * sizeof(Entry) + offsetof(Entry, c),
        sizeof(Cand) * (T_N(s->tag) < VC_CANDS ? T_N(s->tag) + 1u : VC_CANDS));
  if (add_cand(rec, s, fx, fy, px, py, w, clamped, cost, &t) ||
      (w_alt > 0 && add_cand(rec, s, fx, fy, px, py, w_alt, clamped, cost, &t))) {
    st->ambiguous++;
    set_state(s, E_AMBIGUOUS);
  }
  cost[VCC_REC_LINES] += t.n;
}

/* RTPS/RTPT on the register file: PsyCross's GTE_operator (fast_render=0)
 * and gte_inline.h's commands (the mips2c units and the hand renderers'
 * register-file code at fast_render 1 and 2, D89). */
static void rtp_hook(int idx, int sf) {
  const int proj[PROJ_N] = {C2_R11, C2_R12, C2_R13, C2_R21, C2_R22, C2_R23, C2_R31, C2_R32, C2_R33,
                            C2_TRX, C2_TRY, C2_TRZ, C2_OFX, C2_OFY, (unsigned short)C2_H};
  rtp_point(VC_LOGIC, g_vcache_work, proj, V_X(idx), V_Y(idx), V_Z(idx), C2_SXY2,
            (unsigned short)C2_SZ3, sf);
}

/* The hand renderers' native RTPS (gte_native.h, fast_render=2, D80, D89):
 * the same projection, given as values. */
static void rtp_value_hook(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3) {
  rtp_point(VC_LOGIC, g_vcache_work, proj, vx, vy, vz, sxy, sz3, 1);
}

/* The presenter's renderers' native RTPS (render_ctx.c pres_ngte_vcache,
 * on g_pres_rtp_value_hook): into the presenter's own table (#158), on its
 * own thread or inline (#162) */
static void pres_rtp_value_hook(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3) {
  rtp_point(VC_PRES, s_pres_async ? g_vcache_pres_work : g_vcache_work, proj, vx, vy, vz, sxy,
            sz3, 1);
}

/* ratio(a, b) < 4, exactly (depths > 0; times 4 isn't rounded) */
static inline int under4(float a, float b) { return a > b ? a < b * 4.0f : b < a * 4.0f; }
/* ratio(a, b) < 3.999 or so: the quotient, however rounded, is under 4 */
static inline int under3999(float a, float b) { return a > b ? a < b * 3.999f : b < a * 3.999f; }

int psx_vcache_polys;

void psx_vcache_poly(void *vertex_, short *const *xy, int n, VCacheVert *side) {
  GrVertex *const vertex = (GrVertex *)vertex_;
  const Cand *e[4];   /* each vertex's candidates, NULL: not found */
  uint32_t et[4];     /* and its slot's tag */
  unsigned en[4];     /* how many */
  uint32_t ref[4], persp = VREF_PERSP;
  const Table *ptab = NULL; /* the table its vertices were looked up in (the same for each) */
  int i, j, c, best_c = -1, pick[4] = {0, 0, 0, 0}, try_pick[4] = {0, 0, 0, 0};
  int inv = 0, clamp = 0; /* a vertex's key invalid; a clamped vertex found (#178) */
  float best = 4.0f; /* a polygon's depths are never 4x apart */
  /* Whose polygon: the presenter's inline (s_pres), the presenter's on its
   * own thread (a vertex outside the game's walk's buffer while one of its
   * frames is open, #162; the logic draw's walk never reads s_pres_async),
   * or the logic draw's */
  const int async = !s_pres && !(vertex >= psx_gpu_game_walk()->vb &&
                                 vertex < psx_gpu_game_walk()->vb + MAX_VERTEX_BUFFER_SIZE) &&
                    s_pres_async;
  const int pres = s_pres || async;
  VCacheStats *const st = &g_vcache_stats[pres];
  unsigned *const work = async ? g_vcache_pres_work : g_vcache_work;
  unsigned *const cost = COST(work);

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
    const uint32_t tag = k >= 0 ? tab->s[k].tag : 0;
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
      work[3]++;
      if (k < 0)
        st->miss_none++;
      else if (T_STATE(tag) == E_INVALID) {
        st->miss_invalid++;
        inv = 1;
      } else
        st->miss_ambiguous++;
    }
    cost[VCC_LOOK_LINES] += t.n;
  }
  /* each candidate of the first vertex against the closest of every
   * other's; the choice whose worst depth ratio is smallest. Candidates of
   * one point at other scales differ by 4 or 16 times, different points on
   * one pixel by their distance: a polygon's real depths are much closer.
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
  for (c = 0; persp && c < (int)en[0]; c++) {
    const float w0 = e[0][c].w;
    float worst = 1;
    int ok = 1;
    try_pick[0] = c;
    for (i = 1; ok && i < n; i++) {
      float r_best = 1e30f;
      int left = 0, last = 0;
      for (j = 0; j < (int)en[i]; j++)
        if (under4(e[i][j].w, w0)) {
          left++;
          last = j;
        }
      if (!left) {
        ok = 0;
        break;
      }
      if (left == 1 && en[0] == 1) {
        try_pick[i] = last;
        if (!under3999(e[i][last].w, w0)) {
          const float r = ratio(e[i][last].w, w0);
          cost[VCC_LOOK_DIV]++;
          worst = r > worst ? r : worst;
        }
        continue;
      }
      for (j = 0; j < (int)en[i]; j++)
        if (under4(e[i][j].w, w0)) {
          const float r = ratio(e[i][j].w, w0);
          cost[VCC_LOOK_DIV]++;
          if (r < r_best) {
            r_best = r;
            try_pick[i] = j;
          }
        }
      worst = r_best > worst ? r_best : worst;
    }
    if (ok && worst < best) {
      best = worst;
      best_c = c;
      memcpy(pick, try_pick, sizeof pick);
    }
  }
  if (persp && best_c < 0) {
    st->spread++;
    persp = 0;
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
    uint32_t r = ref[i] | persp | (uint32_t)pick[i] << VREF_CAND_SHIFT;
    if (et[i] & T_MULTI)
      r |= VREF_NOPOS;
    if (et[i] & T_CLAMP)
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
      if (e[i]) {
        if (!(et[i] & T_MULTI))
          cand_pos(ptab, &e[i][0], &c[i].dx, &c[i].dy);
        if (persp)
          c[i].w = e[i][pick[i]].w;
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
  r->nfar = 0;
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
  r->count = 0;
  r->nfar = 0;
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
  s_rtab = f ? (const Table *)f->tab : NULL;
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
  psx_vcache_polys = mode != 0;
  s_ot_done = mode != 0;
}

void psx_vcache_ot_done(void) {
  if (s_ot_done)
    done_hook();
}

PsxVcacheValueHook psx_vcache_pres_begin(int walks) {
  Rec *const r = &s_rec[VC_PRES];
  s_pres = 0;
  s_pres_async = 0; /* (the last one ended at its buffer's close, before the join) */
  if (!s_ot_done)
    return NULL;
  if (g_psyxRtpValueHook && !r->tab) { /* recording: the presenter's table, once */
    r->tab = table_new();
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

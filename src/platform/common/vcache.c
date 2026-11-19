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
 * is ambiguous and never used. Vertices whose projection the GTE clamped
 * (IR or SX/SY saturated, divide overflow) are recorded as invalid, so a
 * polygon using them falls back too.
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
 * its done hook cut the logic frame's table short. */
#include "psx_vcache.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "psx/gtereg.h"
#include "psx/libgpu.h"

/* 8192 entries (360 KB) stay in the A9's L2; a frame projects up to ~4100
 * distinct points (logs/run6-host-demo-vcache2), and recording stops at
 * 3/4 full rather than probe far */
#define VC_BITS 13
#define VC_SIZE (1u << VC_BITS)
#define VC_PROBE 32
#define VC_MAX (VC_SIZE * 3 / 4) /* entries per frame before recording stops */
#define VC_CANDS 4
#define VREF_CAND_SHIFT 24 /* which of the entry's candidates (VREF bits 24-25) */

enum { E_VALID = 1, E_INVALID = 2, E_AMBIGUOUS = 3 };

/* One projection recorded under a key: its position (1/256 pixel, from the
 * pixel centre) and depth. */
typedef struct {
  int16_t dx, dy;
  float w;
} Cand;

typedef struct {
  uint32_t key; /* SXY2: (SY << 16) | (SX & 0xFFFF) */
  uint32_t gen; /* frame; an entry from another frame is empty */
  uint8_t state, n;
  uint8_t multi; /* candidates more than a pixel apart: different points */
  uint8_t src;   /* the kind of frame whose projection made it (VC_LOGIC, VC_PRES) */
  Cand c[VC_CANDS];
} Entry;

/* A frame's table: the projections of one kind of frame (VC_LOGIC, VC_PRES)
 * record into their own, so each frame looks up only its own (#158) */
typedef struct {
  Entry *tab;     /* the table recording */
  uint32_t gen;   /* its frame */
  unsigned count; /* entries this frame */
} Rec;

static Entry s_tab0[VC_SIZE];
static Entry *s_tab1;                  /* the logic draw's other table (D71) */
static Rec s_rec[2] = {{s_tab0, 1, 0}, {NULL, 1, 0}}; /* the presenter's: psx_vcache_pres_begin */
static const Entry *s_rtab;            /* the walk's table and frame, NULL: this one */
static uint32_t s_rgen;
VCacheVert g_vcache_side[MAX_VERTEX_BUFFER_SIZE + 6];

/* The vertex buffers the renderer has been given (psx_vcache_set_base):
 * the logic draw's walk's, the presenter's (D125: its own walk) and the
 * check's. A polygon's answer goes to g_vcache_side at its index in its own
 * walk's buffer, whichever buffer the renderer had last (#158: with the two
 * walks taking turns at 60 fps, that was always the other one). */
#define VC_BASES 4
static const GrVertex *s_bases[VC_BASES];
static int s_nbases;

void psx_vcache_set_base(const void *vertex_buffer) {
  const GrVertex *const b = (const GrVertex *)vertex_buffer;
  int i;
  for (i = 0; i < s_nbases; i++)
    if (s_bases[i] == b)
      return;
  /* a buffer first seen: its polygons' answers couldn't be written, so this
   * once it is drawn as not found (integer and affine, as the PS1) */
  for (i = 0; i < MAX_VERTEX_BUFFER_SIZE + 6; i++) {
    g_vcache_side[i].dx = g_vcache_side[i].dy = 0;
    g_vcache_side[i].w = 1;
  }
  if (s_nbases < VC_BASES)
    s_bases[s_nbases++] = b;
}

VCacheStats g_vcache_stats[2];
/* Work since the start, never reset, for the Vita's work counters (issue
 * #160, D131; src/platform/vita/perfcount.c): projections recorded,
 * polygons looked up, their vertices, and those not found. The cache is the
 * game thread's (D126): plain adds. */
unsigned g_vcache_work[4];
static int s_pres; /* the presenter's frame: its DrawOTag hasn't returned (psx_vcache_pres_begin) */

void psx_vcache_stats_reset(void) { memset(g_vcache_stats, 0, sizeof g_vcache_stats); }

void psx_vcache_stats_sum(VCacheStats *out) {
  const unsigned *a = (const unsigned *)&g_vcache_stats[0], *b = (const unsigned *)&g_vcache_stats[1];
  unsigned *o = (unsigned *)out;
  size_t i;
  for (i = 0; i < sizeof *out / sizeof(unsigned); i++)
    o[i] = a[i] + b[i];
  out->peak = g_vcache_stats[0].peak > g_vcache_stats[1].peak ? g_vcache_stats[0].peak
                                                               : g_vcache_stats[1].peak;
}

static inline uint32_t hash(uint32_t key) { return (key * 0x9E3779B1u) >> (32 - VC_BITS); }

/* The entry for key in r's frame, or a free one for it (gen set, state 0),
 * or NULL when the probe is exhausted. */
static Entry *find(Rec *r, int kind, uint32_t key, int add) {
  uint32_t i = hash(key), n;
  for (n = 0; n < VC_PROBE; n++, i = (i + 1) & (VC_SIZE - 1)) {
    Entry *e = &r->tab[i];
    if (e->gen != r->gen) {
      if (!add || r->count >= VC_MAX)
        return NULL;
      e->gen = r->gen;
      e->key = key;
      e->state = 0;
      e->n = 0;
      e->multi = 0;
      e->src = (uint8_t)kind;
      r->count++;
      return e;
    }
    if (e->key == key)
      return e;
  }
  return NULL;
}

/* psx_vcache_poly's lookup: the entry for key in the frame being walked
 * (the presenter's own, in its frame) */
static const Entry *look(uint32_t key) {
  uint32_t i = hash(key), n;
  if (s_pres)
    return find(&s_rec[VC_PRES], VC_PRES, key, 0);
  if (!s_rtab)
    return find(&s_rec[VC_LOGIC], VC_LOGIC, key, 0);
  for (n = 0; n < VC_PROBE; n++, i = (i + 1) & (VC_SIZE - 1)) {
    const Entry *e = &s_rtab[i];
    if (e->gen != s_rgen)
      return NULL;
    if (e->key == key)
      return e;
  }
  return NULL;
}

static inline float ratio(float a, float b) { return a > b ? a / b : b / a; }

static inline int16_t fix8(float d) {
  float f = d * 256.0f;
  return (int16_t)(f > 32767.0f ? 32767 : f < -32767.0f ? -32767 : f);
}

/* One more projection under e's key: 0 = kept (or already there), -1 = no
 * room. The same point at the same depth is kept once. */
static int add_cand(Entry *e, int16_t dx, int16_t dy, float w) {
  int j;
  for (j = 0; j < e->n; j++) {
    int ddx = e->c[j].dx - dx, ddy = e->c[j].dy - dy;
    if (ddx > 32 || ddx < -32 || ddy > 32 || ddy < -32)
      e->multi = 1; /* more than 1/8 pixel apart: not one point */
    else if (ratio(w, e->c[j].w) <= 1.02f)
      return 0;
  }
  if (e->n == VC_CANDS)
    return -1;
  e->c[e->n].dx = dx;
  e->c[e->n].dy = dy;
  e->c[e->n].w = w;
  e->n++;
  return 0;
}

static void record(int kind, uint32_t key, int state, float dx, float dy, float w, float w_alt) {
  Entry *e = find(&s_rec[kind], kind, key, 1);
  int16_t fx = fix8(dx), fy = fix8(dy);
  if (!e) {
    g_vcache_stats[kind].full++;
    return;
  }
  if (e->state == 0) {
    e->state = state;
    if (state == E_INVALID) {
      g_vcache_stats[kind].invalid++;
      return;
    }
  } else if (e->state != state || state != E_VALID) {
    /* a clamped projection and another one under the same key: the prims
     * that use it can't be told apart */
    if (e->state != E_AMBIGUOUS && !(e->state == E_INVALID && state == E_INVALID))
      g_vcache_stats[kind].ambiguous++;
    if (!(e->state == E_INVALID && state == E_INVALID))
      e->state = E_AMBIGUOUS;
    return;
  }
  /* Another projection under this key: the same point again (shared by
   * several polygons), the same point at another scale, or a different
   * point on the same pixel. Each is a candidate; each polygon picks. */
  if (add_cand(e, fx, fy, w) || (w_alt > 0 && add_cand(e, fx, fy, w_alt))) {
    g_vcache_stats[kind].ambiguous++;
    e->state = E_AMBIGUOUS;
  }
}

/* PsyX_GTE.cpp's VX/VY/VZ for idx 0-2 */
#define V_X(n) (gteRegs.CP2D.p[(n) << 1].sw.l)
#define V_Y(n) (gteRegs.CP2D.p[(n) << 1].sw.h)
#define V_Z(n) (gteRegs.CP2D.p[((n) << 1) + 1].sw.l)

/* One projection: the vertex, what the GTE gave (key = SXY2, sz = SZ3) and
 * the registers it read, proj = R11-R33, TRX-TRZ, OFX, OFY, H (PROJ_*). */
enum { PROJ_TR = 9, PROJ_OFX = 12, PROJ_OFY = 13, PROJ_H = 14, PROJ_N = 15 };

static void rtp_point(int kind, const int *proj, int vx, int vy, int vz, uint32_t key, int sz,
                      int sf) {
  const int sx = (short)(key & 0xFFFF), sy = (short)(key >> 16), h = proj[PROJ_H];
  const int *rm = proj, *tr = proj + PROJ_TR;
  int64_t mx, my, mz;
  float x, y, z, r, iz, isz, ox, oy, ex, ey, w_alt = 0;

  g_vcache_stats[kind].records++;
  g_vcache_work[0]++;
  /* the 44-bit sums the GTE shifts into MAC1-3 */
  mx = ((int64_t)tr[0] << 12) + (int64_t)(rm[0] * vx) + (rm[1] * vy) + (rm[2] * vz);
  my = ((int64_t)tr[1] << 12) + (int64_t)(rm[3] * vx) + (rm[4] * vy) + (rm[5] * vz);
  mz = ((int64_t)tr[2] << 12) + (int64_t)(rm[6] * vx) + (rm[7] * vy) + (rm[8] * vz);
  /* Spyro's renderers always set sf; IR1/IR2 saturated (|MAC| > 0x7FFF)
   * or a divide overflow (H >= 2 SZ3) means the GTE's answer isn't the
   * projection of this vertex. */
  if (!sf || sz <= 0 || h >= 2 * sz || (mx >> 12) > 0x7FFF || (mx >> 12) < -0x8000 ||
      (my >> 12) > 0x7FFF || (my >> 12) < -0x8000) {
    record(kind, key, E_INVALID, 0, 0, 0, 0);
    return;
  }
  x = (float)(int32_t)(mx >> 12) + (float)(mx & 0xFFF) * (1.0f / 4096);
  y = (float)(int32_t)(my >> 12) + (float)(my & 0xFFF) * (1.0f / 4096);
  z = (float)(int32_t)(mz >> 12) + (float)(mz & 0xFFF) * (1.0f / 4096);
  ox = (float)proj[PROJ_OFX] * (1.0f / 65536);
  oy = (float)proj[PROJ_OFY] * (1.0f / 65536);
  if (z <= 0) {
    record(kind, key, E_INVALID, 0, 0, 0, 0);
    return;
  }
  r = (float)h / (z * sz); /* one division for h / sz and h / z */
  isz = r * z;
  iz = r * sz;
  /* Check against the GTE's own model (truncated x, y and SZ3): off by more
   * than its rounding means SX/SY saturated. */
  ex = ox + (float)(int32_t)(mx >> 12) * isz - sx;
  ey = oy + (float)(int32_t)(my >> 12) * isz - sy;
  if (ex < -1.5f || ex > 2.5f || ey < -1.5f || ey > 2.5f) {
    record(kind, key, E_INVALID, 0, 0, 0, 0);
    return;
  }
  if (!tr[0] && !tr[1] && !tr[2] && !((vx | vy | vz) & 15) && vx >= -4096 && vx < 4096 &&
      vy >= -4096 && vy < 4096 && vz >= -4096 && vz < 4096)
    w_alt = z * (1.0f / 16); /* maybe r_environment's x16 precision path */
  /* The exact point, relative to the pixel centre the PS1 draws the vertex
   * at: unbiased against vertices drawn at their integer position. */
  record(kind, key, E_VALID, ox + x * iz - sx - 0.5f, oy + y * iz - sy - 0.5f, z, w_alt);
}

/* RTPS/RTPT on the register file: PsyCross's GTE_operator (fast_render=0)
 * and gte_inline.h's commands (the mips2c units and the hand renderers'
 * register-file code at fast_render 1 and 2, D89). */
static void rtp_hook(int idx, int sf) {
  const int proj[PROJ_N] = {C2_R11, C2_R12, C2_R13, C2_R21, C2_R22, C2_R23, C2_R31, C2_R32, C2_R33,
                            C2_TRX, C2_TRY, C2_TRZ, C2_OFX, C2_OFY, (unsigned short)C2_H};
  rtp_point(VC_LOGIC, proj, V_X(idx), V_Y(idx), V_Z(idx), C2_SXY2, (unsigned short)C2_SZ3, sf);
}

/* The hand renderers' native RTPS (gte_native.h, fast_render=2, D80, D89):
 * the same projection, given as values. */
static void rtp_value_hook(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3) {
  rtp_point(VC_LOGIC, proj, vx, vy, vz, sxy, sz3, 1);
}

/* The presenter's renderers' native RTPS (render_ctx.c pres_ngte_vcache,
 * on g_pres_rtp_value_hook): into the presenter's own table (#158) */
static void pres_rtp_value_hook(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3) {
  rtp_point(VC_PRES, proj, vx, vy, vz, sxy, sz3, 1);
}

int psx_vcache_polys;

void psx_vcache_poly(void *vertex_, short *const *xy, int n) {
  GrVertex *const vertex = (GrVertex *)vertex_;
  const Entry *e[4];
  uint32_t ref[4], persp = VREF_PERSP;
  int i, j, c, best_c = -1, pick[4] = {0, 0, 0, 0}, try_pick[4] = {0, 0, 0, 0};
  float best = 4.0f; /* a polygon's depths are never 4x apart */
  VCacheStats *const st = &g_vcache_stats[s_pres];

  st->polys++;
  st->verts += n;
  g_vcache_work[1]++;
  g_vcache_work[2] += (unsigned)n;
  if (!g_psyxRtpHook) { /* tex_filter only: polygons are just marked */
    for (i = 0; i < n; i++) {
      uint32_t r = VREF_POLY;
      memcpy(&vertex[i].tcx, &r, sizeof r);
    }
    return;
  }
  for (i = 0; i < n; i++) {
    uint32_t key = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
    const Entry *f = look(key);
    e[i] = f && f->state == E_VALID && f->n ? f : NULL;
    ref[i] = VREF_POLY;
    if (e[i]) {
      const Entry *tab = s_pres ? s_rec[VC_PRES].tab : s_rtab ? s_rtab : s_rec[VC_LOGIC].tab;
      ref[i] |= (uint32_t)(f - tab) + 1;
      st->hits++;
      if (f->src != s_pres)
        st->other++;
    } else {
      persp = 0;
      g_vcache_work[3]++;
      if (!f)
        st->miss_none++;
      else if (f->state == E_INVALID)
        st->miss_invalid++;
      else
        st->miss_ambiguous++;
    }
  }
  /* each candidate of the first vertex against the closest of every
   * other's; the choice whose worst depth ratio is smallest. Candidates of
   * one point at other scales differ by 4 or 16 times, different points on
   * one pixel by their distance: a polygon's real depths are much closer. */
  for (c = 0; persp && c < e[0]->n; c++) {
    float worst = 1;
    try_pick[0] = c;
    for (i = 1; i < n; i++) {
      float r_best = 1e30f;
      for (j = 0; j < e[i]->n; j++) {
        float r = ratio(e[i]->c[j].w, e[0]->c[c].w);
        if (r < r_best) {
          r_best = r;
          try_pick[i] = j;
        }
      }
      worst = r_best > worst ? r_best : worst;
    }
    if (worst < best) {
      best = worst;
      best_c = c;
      memcpy(pick, try_pick, sizeof pick);
    }
  }
  if (best_c < 0)
    persp = 0;
  if (persp)
    st->persp++;
  for (i = 0; i < n; i++) {
    uint32_t r = ref[i] | persp | (uint32_t)pick[i] << VREF_CAND_SHIFT;
    if (e[i] && e[i]->multi)
      r |= VREF_NOPOS;
    memcpy(&vertex[i].tcx, &r, sizeof r);
  }
  for (i = 0; i < s_nbases; i++)
    if (vertex >= s_bases[i] && vertex - s_bases[i] <= MAX_VERTEX_BUFFER_SIZE - 6)
      break;
  if (i < s_nbases) {
    /* the walk splits a quad 0 1 2 3 into 0 1 3 / 1 3 2 (TriangulateQuad) */
    const GrVertex *const base = s_bases[i];
    static const int k_quad[6] = {0, 1, 3, 1, 3, 2}, k_tri[3] = {0, 1, 2};
    const int *order = n == 4 ? k_quad : k_tri;
    VCacheVert c[4], *side = g_vcache_side + (vertex - base);
    for (i = 0; i < n; i++) {
      c[i].dx = c[i].dy = 0;
      c[i].w = 1;
      if (e[i]) {
        if (!e[i]->multi) {
          c[i].dx = e[i]->c[0].dx;
          c[i].dy = e[i]->c[0].dy;
        }
        if (persp)
          c[i].w = e[i]->c[pick[i]].w;
      }
    }
    for (i = 0; i < (n == 4 ? 6 : 3); i++)
      side[i] = c[order[i]];
  } else if (s_nbases) {
    st->side_lost += n;
  }
}

static int s_ot_done; /* psx_vcache_enable: mode != 0 */

/* the presenter's next frame (psx_vcache_pres_begin): its table's entries
 * from before are old */
static void pres_next(void) {
  Rec *const r = &s_rec[VC_PRES];
  r->count = 0;
  if (++r->gen == 0) {
    memset(r->tab, 0, sizeof(Entry) * VC_SIZE);
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
  if (s_tab1) /* the other table records the next frame (D71) */
    r->tab = r->tab == s_tab0 ? s_tab1 : s_tab0;
  if (++r->gen == 0) { /* wrapped: no old entry may look current */
    memset(s_tab0, 0, sizeof s_tab0);
    if (s_tab1)
      memset(s_tab1, 0, sizeof s_tab0);
    r->gen = 1;
  }
}

int psx_vcache_double(void) {
  if (!s_tab1)
    s_tab1 = calloc(VC_SIZE, sizeof(Entry));
  return s_tab1 ? 0 : -1;
}

void psx_vcache_frame_take(PsxVcacheFrame *f) {
  f->tab = s_rec[VC_LOGIC].tab;
  f->gen = s_rec[VC_LOGIC].gen;
}

void psx_vcache_frame_use(const PsxVcacheFrame *f) {
  s_rtab = f ? (const Entry *)f->tab : NULL;
  s_rgen = f ? f->gen : 0;
}

int psx_vcache_get(uint32_t ref, VCacheVert *out) {
  uint32_t slot = ref & VREF_SLOT, d = (ref >> VREF_CAND_SHIFT) & 3;
  const Entry *e;
  if (!slot || slot > VC_SIZE)
    return 0;
  e = &s_rec[VC_LOGIC].tab[slot - 1];
  if (d >= e->n)
    d = 0;
  out->dx = e->c[0].dx; /* the same for every polygon */
  out->dy = e->c[0].dy;
  out->w = e->c[d].w;
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
  if (!s_ot_done)
    return NULL;
  if (g_psyxRtpValueHook && !r->tab) /* recording: the presenter's table, once */
    r->tab = calloc(VC_SIZE, sizeof(Entry));
  if (!r->tab) /* tex_filter only (polygons just marked), or no memory: as before #158 */
    return NULL;
  pres_next();
  s_pres = walks != 0;
  return s_pres && g_psyxRtpValueHook ? pres_rtp_value_hook : NULL;
}

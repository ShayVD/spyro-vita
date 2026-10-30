/* Vertex cache (psx_vcache.h, decisions D39).
 *
 * RTPS/RTPT: PsyCross's GTE calls rtp_hook after the integer projection.
 * The hook recomputes the same transform from the input registers without
 * the truncations (MAC >> 12, h/SZ3 by UNR division) and records the exact
 * screen position and depth under the SXY2 the game got. It reads gteRegs
 * and writes only this file's table.
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
 * 4096). poly_hook then picks, for each polygon, the candidates whose depths
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
 * DrawOTag: each polygon's vertices are looked up (poly_hook) while the OT
 * is parsed, which happens inside DrawOTag, before the table moves on to the
 * next frame (done_hook). A frame is everything projected between two
 * DrawOTag calls; Spyro builds and draws its OT in the same frame
 * (gamestates/draw.c), and the presentation passes (D30) do too. */
#include "psx_vcache.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <math.h>
#include <string.h>

#include "PsyX/PsyX_render.h"
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
  Cand c[VC_CANDS];
} Entry;

static Entry s_tab[VC_SIZE];
static uint32_t s_gen = 1;
static unsigned s_count; /* entries this frame */

VCacheStats g_vcache_stats;

void psx_vcache_stats_reset(void) { memset(&g_vcache_stats, 0, sizeof g_vcache_stats); }

static inline uint32_t hash(uint32_t key) { return (key * 0x9E3779B1u) >> (32 - VC_BITS); }

/* The entry for key in this frame, or a free one for it (gen set, state 0),
 * or NULL when the probe is exhausted. */
static Entry *find(uint32_t key, int add) {
  uint32_t i = hash(key), n;
  for (n = 0; n < VC_PROBE; n++, i = (i + 1) & (VC_SIZE - 1)) {
    Entry *e = &s_tab[i];
    if (e->gen != s_gen) {
      if (!add || s_count >= VC_MAX)
        return NULL;
      e->gen = s_gen;
      e->key = key;
      e->state = 0;
      e->n = 0;
      e->multi = 0;
      s_count++;
      return e;
    }
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

static void record(uint32_t key, int state, float dx, float dy, float w, float w_alt) {
  Entry *e = find(key, 1);
  int16_t fx = fix8(dx), fy = fix8(dy);
  if (!e) {
    g_vcache_stats.full++;
    return;
  }
  if (e->state == 0) {
    e->state = state;
    if (state == E_INVALID) {
      g_vcache_stats.invalid++;
      return;
    }
  } else if (e->state != state || state != E_VALID) {
    /* a clamped projection and another one under the same key: the prims
     * that use it can't be told apart */
    if (e->state != E_AMBIGUOUS && !(e->state == E_INVALID && state == E_INVALID))
      g_vcache_stats.ambiguous++;
    if (!(e->state == E_INVALID && state == E_INVALID))
      e->state = E_AMBIGUOUS;
    return;
  }
  /* Another projection under this key: the same point again (shared by
   * several polygons), the same point at another scale, or a different
   * point on the same pixel. Each is a candidate; each polygon picks. */
  if (add_cand(e, fx, fy, w) || (w_alt > 0 && add_cand(e, fx, fy, w_alt))) {
    g_vcache_stats.ambiguous++;
    e->state = E_AMBIGUOUS;
  }
}

/* PsyX_GTE.cpp's VX/VY/VZ for idx 0-2 */
#define V_X(n) (gteRegs.CP2D.p[(n) << 1].sw.l)
#define V_Y(n) (gteRegs.CP2D.p[(n) << 1].sw.h)
#define V_Z(n) (gteRegs.CP2D.p[((n) << 1) + 1].sw.l)

static void rtp_hook(int idx, int sf) {
  const int vx = V_X(idx), vy = V_Y(idx), vz = V_Z(idx);
  const uint32_t key = C2_SXY2;
  const int sx = (short)(key & 0xFFFF), sy = (short)(key >> 16);
  const int sz = (unsigned short)C2_SZ3, h = (unsigned short)C2_H;
  int64_t mx, my, mz;
  float x, y, z, r, iz, isz, ox, oy, ex, ey, w_alt = 0;

  g_vcache_stats.records++;
  /* the 44-bit sums the GTE shifts into MAC1-3 */
  mx = ((int64_t)C2_TRX << 12) + (int64_t)(C2_R11 * vx) + (C2_R12 * vy) + (C2_R13 * vz);
  my = ((int64_t)C2_TRY << 12) + (int64_t)(C2_R21 * vx) + (C2_R22 * vy) + (C2_R23 * vz);
  mz = ((int64_t)C2_TRZ << 12) + (int64_t)(C2_R31 * vx) + (C2_R32 * vy) + (C2_R33 * vz);
  /* Spyro's renderers always set sf; IR1/IR2 saturated (|MAC| > 0x7FFF)
   * or a divide overflow (H >= 2 SZ3) means the GTE's answer isn't the
   * projection of this vertex. */
  if (!sf || sz <= 0 || h >= 2 * sz || (mx >> 12) > 0x7FFF || (mx >> 12) < -0x8000 ||
      (my >> 12) > 0x7FFF || (my >> 12) < -0x8000) {
    record(key, E_INVALID, 0, 0, 0, 0);
    return;
  }
  x = (float)(int32_t)(mx >> 12) + (float)(mx & 0xFFF) * (1.0f / 4096);
  y = (float)(int32_t)(my >> 12) + (float)(my & 0xFFF) * (1.0f / 4096);
  z = (float)(int32_t)(mz >> 12) + (float)(mz & 0xFFF) * (1.0f / 4096);
  ox = (float)C2_OFX * (1.0f / 65536);
  oy = (float)C2_OFY * (1.0f / 65536);
  if (z <= 0) {
    record(key, E_INVALID, 0, 0, 0, 0);
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
    record(key, E_INVALID, 0, 0, 0, 0);
    return;
  }
  if (!C2_TRX && !C2_TRY && !C2_TRZ && !((vx | vy | vz) & 15) && vx >= -4096 && vx < 4096 &&
      vy >= -4096 && vy < 4096 && vz >= -4096 && vz < 4096)
    w_alt = z * (1.0f / 16); /* maybe r_environment's x16 precision path */
  /* The exact point, relative to the pixel centre the PS1 draws the vertex
   * at: unbiased against vertices drawn at their integer position. */
  record(key, E_VALID, ox + x * iz - sx - 0.5f, oy + y * iz - sy - 0.5f, z, w_alt);
}

static void poly_hook(GrVertex *vertex, short *const *xy, int n) {
  const Entry *e[4];
  uint32_t ref[4], persp = VREF_PERSP;
  int i, j, c, best_c = -1, pick[4] = {0, 0, 0, 0}, try_pick[4] = {0, 0, 0, 0};
  float best = 4.0f; /* a polygon's depths are never 4x apart */

  g_vcache_stats.polys++;
  g_vcache_stats.verts += n;
  if (!g_psyxRtpHook) { /* tex_filter only: polygons are just marked */
    for (i = 0; i < n; i++) {
      uint32_t r = VREF_POLY;
      memcpy(&vertex[i].tcx, &r, sizeof r);
    }
    return;
  }
  for (i = 0; i < n; i++) {
    uint32_t key = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
    const Entry *f = find(key, 0);
    e[i] = f && f->state == E_VALID && f->n ? f : NULL;
    ref[i] = VREF_POLY;
    if (e[i]) {
      ref[i] |= (uint32_t)(f - s_tab) + 1;
      g_vcache_stats.hits++;
    } else {
      persp = 0;
      if (!f)
        g_vcache_stats.miss_none++;
      else if (f->state == E_INVALID)
        g_vcache_stats.miss_invalid++;
      else
        g_vcache_stats.miss_ambiguous++;
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
    g_vcache_stats.persp++;
  for (i = 0; i < n; i++) {
    uint32_t r = ref[i] | persp | (uint32_t)pick[i] << VREF_CAND_SHIFT;
    if (e[i] && e[i]->multi)
      r |= VREF_NOPOS;
    memcpy(&vertex[i].tcx, &r, sizeof r);
  }
}

static void done_hook(void) {
  g_vcache_stats.frames++;
  if (s_count > g_vcache_stats.peak)
    g_vcache_stats.peak = s_count;
  s_count = 0;
  if (++s_gen == 0) { /* wrapped: no old entry may look current */
    memset(s_tab, 0, sizeof s_tab);
    s_gen = 1;
  }
}

int psx_vcache_get(uint32_t ref, VCacheVert *out) {
  uint32_t slot = ref & VREF_SLOT, d = (ref >> VREF_CAND_SHIFT) & 3;
  const Entry *e;
  if (!slot || slot > VC_SIZE)
    return 0;
  e = &s_tab[slot - 1];
  if (d >= e->n)
    d = 0;
  out->dx = e->c[0].dx; /* the same for every polygon */
  out->dy = e->c[0].dy;
  out->w = e->c[d].w;
  return 1;
}

void psx_vcache_enable(int mode) {
  g_psyxRtpHook = mode >= 2 ? rtp_hook : NULL;
  g_psyxPolyHook = mode ? poly_hook : NULL;
  g_psyxDrawOTagDone = mode ? done_hook : NULL;
}

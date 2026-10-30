/* Vertex cache (psx_vcache.h, decisions D36).
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
 * coordinates find the entry. Two different vertices can land on the same
 * integer pixel in one frame; that key is then ambiguous and never used.
 * Vertices whose projection the GTE clamped (IR or SX/SY saturated, divide
 * overflow) are recorded as invalid, so a polygon using them falls back too.
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

#define VC_BITS 15
#define VC_SIZE (1u << VC_BITS)
#define VC_PROBE 32
#define VC_MAX (VC_SIZE * 3 / 4) /* entries per frame before recording stops */

enum { E_VALID = 1, E_INVALID = 2, E_AMBIGUOUS = 3 };

typedef struct {
  uint32_t key;  /* SXY2: (SY << 16) | (SX & 0xFFFF) */
  uint32_t gen;  /* frame; an entry from another frame is empty */
  uint32_t state;
  VCacheVert v;
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
      s_count++;
      return e;
    }
    if (e->key == key)
      return e;
  }
  return NULL;
}

static void record(uint32_t key, int state, float dx, float dy, float w) {
  Entry *e = find(key, 1);
  if (!e) {
    g_vcache_stats.full++;
    return;
  }
  if (e->state == 0) {
    e->state = state;
    e->v.dx = dx;
    e->v.dy = dy;
    e->v.w = w;
    if (state == E_INVALID)
      g_vcache_stats.invalid++;
    return;
  }
  /* The same key again: the same vertex (shared by several polygons, or a
   * coincident one at the same depth) keeps the entry; anything else makes
   * it unusable. */
  if (e->state == E_VALID && state == E_VALID && fabsf(e->v.dx - dx) <= 0.25f &&
      fabsf(e->v.dy - dy) <= 0.25f && fabsf(e->v.w - w) <= 0.01f * w)
    return;
  if (e->state != E_AMBIGUOUS)
    g_vcache_stats.ambiguous++;
  e->state = E_AMBIGUOUS;
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
  float x, y, z, ox, oy, ex, ey;

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
    record(key, E_INVALID, 0, 0, 0);
    return;
  }
  x = (float)(int32_t)(mx >> 12) + (float)(mx & 0xFFF) * (1.0f / 4096);
  y = (float)(int32_t)(my >> 12) + (float)(my & 0xFFF) * (1.0f / 4096);
  z = (float)(int32_t)(mz >> 12) + (float)(mz & 0xFFF) * (1.0f / 4096);
  ox = (float)C2_OFX * (1.0f / 65536);
  oy = (float)C2_OFY * (1.0f / 65536);
  /* Check against the GTE's own model (truncated x, y and SZ3): off by more
   * than its rounding means SX/SY saturated. */
  ex = ox + (float)(int32_t)(mx >> 12) * h / sz - sx;
  ey = oy + (float)(int32_t)(my >> 12) * h / sz - sy;
  if (z <= 0 || ex < -1.5f || ex > 2.5f || ey < -1.5f || ey > 2.5f) {
    record(key, E_INVALID, 0, 0, 0);
    return;
  }
  /* The exact point, relative to the pixel centre the PS1 draws the vertex
   * at: unbiased against vertices drawn at their integer position. */
  record(key, E_VALID, ox + x * h / z - sx - 0.5f, oy + y * h / z - sy - 0.5f, z);
}

static void poly_hook(GrVertex *vertex, short *const *xy, int n) {
  uint32_t ref[4], all = VREF_PERSP;
  int i;
  g_vcache_stats.polys++;
  for (i = 0; i < n; i++) {
    uint32_t key = (uint16_t)xy[i][0] | (uint32_t)(uint16_t)xy[i][1] << 16;
    Entry *e = find(key, 0);
    ref[i] = VREF_POLY;
    if (e && e->state == E_VALID) {
      ref[i] |= (uint32_t)(e - s_tab) + 1;
      g_vcache_stats.hits++;
    } else {
      all = 0;
      if (!e)
        g_vcache_stats.miss_none++;
      else if (e->state == E_INVALID)
        g_vcache_stats.miss_invalid++;
      else
        g_vcache_stats.miss_ambiguous++;
    }
  }
  g_vcache_stats.verts += n;
  if (all)
    g_vcache_stats.persp++;
  for (i = 0; i < n; i++) {
    uint32_t r = ref[i] | all;
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

const VCacheVert *psx_vcache_get(uint32_t ref) {
  uint32_t slot = ref & VREF_SLOT;
  return slot && slot <= VC_SIZE ? &s_tab[slot - 1].v : NULL;
}

void psx_vcache_enable(int mode) {
  g_psyxRtpHook = mode >= 2 ? rtp_hook : NULL;
  g_psyxPolyHook = mode ? poly_hook : NULL;
  g_psyxDrawOTagDone = mode ? done_hook : NULL;
}

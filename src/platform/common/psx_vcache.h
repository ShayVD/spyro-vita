/* Vertex cache (decisions D39): a side channel from the GTE's RTPS/RTPT to
 * the GPU renderer, like PsyCross's PGXP but without changing the primitive
 * layout. Each projected vertex is recorded under the 16-bit screen
 * coordinates the game got back (SXY2) with its sub-pixel position and view
 * depth. When DrawOTag builds a polygon, each of its vertices is looked up by
 * the coordinates in the prim and the answer is left in the GrVertex's
 * unused trailing bytes (tcx, tcy, _p0, _p1) for the renderer. The cache
 * only reads GTE registers: nothing the game reads changes. */
#ifndef PSX_VCACHE_H
#define PSX_VCACHE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A GrVertex's reference (its last 4 bytes, little-endian). 0: not part of
 * a polygon (sprites, tiles, lines) or no cache. */
#define VREF_POLY 0x80000000u  /* built by MakeVertexTriangle/Quad */
#define VREF_PERSP 0x40000000u /* every vertex of its polygon was found */
#define VREF_NOPOS 0x20000000u /* different points share the key: integer position */
#define VREF_SLOT 0x00FFFFFFu  /* entry + 1, or 0: not found or ambiguous */
/* bits 24-25: which of the entry's depths this polygon uses (vcache.c) */

typedef struct {
  int16_t dx, dy; /* exact screen position - (integer SX/SY + 0.5), 1/256 pixel */
  float w;      /* view-space depth (> 0), at the scale the polygon's other
                   vertices use: only ratios within a polygon mean anything */
} VCacheVert;

/* 0: no hooks. 1: polygons are marked VREF_POLY (nothing is found).
 * 2: projections are recorded and looked up too. */
void psx_vcache_enable(int mode);

/* The walk's (gpu_walk.c, native_draw.c), when psx_vcache_polys (mode 1 or
 * 2): a polygon's vertices (a GrVertex each, 3 or 4, before a quad is split
 * into triangles) with the primitive's own screen coordinates. It fills
 * each vertex's tcx/tcy/_p0/_p1 and nothing else (PsyCross's poly hook,
 * patch 0007, before D117). */
extern int psx_vcache_polys;
void psx_vcache_poly(void *vertex, short *const *xy, int n);

/* DrawOTag (libgpu.c, and the presenter's, present.c) as it returns, drawn
 * or skipped: the next frame's projections begin (nothing with mode 0). */
void psx_vcache_ot_done(void);

/* The vertex a reference names (1), or 0 if none; valid until the next
 * DrawOTag returns. The logic draw's frames only (no caller since D43). */
int psx_vcache_get(uint32_t ref, VCacheVert *out);

/* A walk's vertex buffer, as the renderer's GR_UpdateVertexBuffer gets it.
 * Once known, each polygon's answer is also written to g_vcache_side at the
 * same index as its vertices in its own walk's buffer, after the quad
 * split: dx, dy (0 where the position stays integer) and w (1 unless the
 * whole polygon was found), so the renderer reads them in order instead of
 * looking each one up (D43). Valid for the vertices whose reference has
 * VREF_POLY. Up to four buffers are known (the logic draw's walk, the
 * presenter's; #158); for one seen the first time g_vcache_side is reset
 * to "not found", since its polygons couldn't be written. */
void psx_vcache_set_base(const void *vertex_buffer);
extern VCacheVert g_vcache_side[];

/* The OT walked later or on another thread (walk.c, D71): the frame's
 * table, taken as its list is handed over (before DrawOTag's done hook
 * moves on), and used while it's walked (NULL afterwards: the current
 * one). psx_vcache_double() makes the frames take turns between two
 * tables, so the next frame can record meanwhile. */
typedef struct {
  const void *tab;
  uint32_t gen;
} PsxVcacheFrame;
int psx_vcache_double(void); /* 0 = ok */
void psx_vcache_frame_take(PsxVcacheFrame *f);
void psx_vcache_frame_use(const PsxVcacheFrame *f);

/* The presenter's frame (pres_draw.c, D129: every frame the logic draw
 * doesn't draw; #158): from here to its DrawOTag's psx_vcache_ot_done, the
 * presenter's projections record into a table of its own and its polygons
 * are looked up there, so neither kind of frame finds the other's. walks:
 * 0 when its DrawOTag won't walk or call the done hook (the GPU disabled).
 * Returns the presenter's projection hook (render_ctx.h
 * g_pres_rtp_value_hook), NULL when the cache doesn't record. Inline on
 * the game thread only, after the game's walk is joined (the cache is the
 * game thread's; D126). */
typedef void (*PsxVcacheValueHook)(const int *proj, int vx, int vy, int vz, unsigned int sxy,
                                   int sz3);
PsxVcacheValueHook psx_vcache_pres_begin(int walks);

/* Counters since the last psx_vcache_stats_reset(), by kind of frame:
 * [VC_LOGIC] the logic draw's, [VC_PRES] the presenter's. */
enum { VC_LOGIC = 0, VC_PRES = 1 };
typedef struct {
  unsigned frames;    /* DrawOTag calls */
  unsigned records;   /* vertices projected */
  unsigned invalid;   /* clamped, overflowed or sf=0: never used */
  unsigned ambiguous; /* another vertex with different values had the same SXY */
  unsigned full;      /* not recorded: table full */
  unsigned peak;      /* most entries in one frame */
  unsigned polys;     /* polygons looked up */
  unsigned persp;     /* ... with every vertex found */
  unsigned verts;     /* polygon vertices looked up */
  unsigned hits;      /* ... found */
  unsigned miss_none, miss_invalid, miss_ambiguous; /* ... not found, and why */
  unsigned other;     /* hits on an entry the other kind of frame's projections made */
  unsigned side_lost; /* polygon vertices whose answer couldn't go to g_vcache_side (not in
                         a known vertex buffer): the renderer reads another walk's there */
} VCacheStats;
extern VCacheStats g_vcache_stats[2];
void psx_vcache_stats_reset(void);
/* the two kinds' counters added (peak: the larger) */
void psx_vcache_stats_sum(VCacheStats *out);

#ifdef __cplusplus
}
#endif

#endif

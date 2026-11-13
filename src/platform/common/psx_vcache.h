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

typedef struct VCacheVert {
  int16_t dx, dy; /* exact screen position - (integer SX/SY + 0.5), 1/256 pixel */
  float w;      /* view-space depth (> 0), at the scale the polygon's other
                   vertices use: only ratios within a polygon mean anything */
} VCacheVert;
extern VCacheVert g_vcache_side[]; /* a vertex buffer's (psx_vcache_poly, below) */

/* 0: no hooks. 1: polygons are marked VREF_POLY (nothing is found).
 * 2: projections are recorded and looked up too. */
void psx_vcache_enable(int mode);

/* The walk's (gpu_walk.c, native_draw.c), when psx_vcache_polys (mode 1 or
 * 2): a polygon's vertices (a GrVertex each, 3 or 4, before a quad is split
 * into triangles) with the primitive's own screen coordinates. It fills
 * each vertex's tcx/tcy/_p0/_p1 (PsyCross's poly hook, patch 0007, before
 * D117) and, at side, the polygon's answers for the renderer (D43), one for
 * each vertex after the walk's quad split (3 or 6): dx, dy (0 where the
 * position stays integer) and w (1 unless the whole polygon was found), so
 * the renderer reads them in order instead of looking each one up. NULL:
 * no answers (psx_vcache_poly_ref, the form of PsyCross's hook: walk_check's
 * reference walk).
 *
 * g_vcache_side is the answers of the vertex buffer a walk is filling: the
 * walk gives each polygon g_vcache_side + its first vertex's index
 * (native_draw.c), whichever walk's buffer it is, and the renderer's
 * GR_UpdateVertexBuffer reads them at the same indices, for the vertices
 * whose reference has VREF_POLY. One array is enough because only one walk
 * fills a buffer at a time while the cache is on, and draws it before
 * another starts: the game's (on the parse thread too, D71) is joined
 * before the presenter's runs inline, and the presenter's worker never runs
 * with the cache (D126). Until D152 the answers were placed by the buffer
 * the renderer had drawn last, so with two walks taking turns (the
 * presenter's in-between frame, stage 7f) neither wrote any and both drew
 * with old ones. */
extern int psx_vcache_polys;
void psx_vcache_poly(void *vertex, short *const *xy, int n, VCacheVert *side);
void psx_vcache_poly_ref(void *vertex, short *const *xy, int n);

/* DrawOTag (libgpu.c) as it returns, drawn or skipped: the next frame's
 * projections begin (nothing with mode 0). */
void psx_vcache_ot_done(void);

/* The vertex a reference names (1), or 0 if none; valid until the next
 * DrawOTag returns. */
int psx_vcache_get(uint32_t ref, VCacheVert *out);

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

/* Counters since the last psx_vcache_stats_reset(). */
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
} VCacheStats;
extern VCacheStats g_vcache_stats;
void psx_vcache_stats_reset(void);

#ifdef __cplusplus
}
#endif

#endif

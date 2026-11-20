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
#define VREF_CLAMP 0x10000000u /* the GTE clamped this projection (#178), or the renderer
                                  moved the vertex after it (#183): its exact position is
                                  drawn even without subpixel */
#define VREF_SLOT 0x00FFFFFFu  /* entry + 1, or 0: not found or ambiguous */
/* bits 24-25: which of the entry's depths this polygon uses (vcache.c) */

/* dx, dy: floats since #178. A vertex the GTE clamped (IR1/IR2 or SX/SY
 * saturated, divide overflow) is answered with its exact position, which
 * can lie far outside the screen; an int16 in 1/256 pixel held +-128
 * pixels. Every other answer is still a multiple of 1/256 pixel within
 * that range, the same value as before. 12 bytes a vertex (was 8). */
typedef struct VCacheVert {
  float dx, dy; /* exact screen position - (integer SX/SY + 0.5), pixels */
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
 * whose reference has VREF_POLY. Inline, one array is enough because only
 * one walk fills a buffer at a time and draws it before another starts:
 * the game's (on the parse thread too, D71) is joined before the
 * presenter's runs inline. On its own thread (#162, below) the presenter's
 * answers go to its own array at the same index instead. Until D152 the
 * answers were placed by the buffer the renderer had drawn last, so with
 * two walks taking turns (the presenter's in-between frame, stage 7f)
 * neither wrote any and both drew with old ones. */
extern int psx_vcache_polys;
void psx_vcache_poly(void *vertex, short *const *xy, int n, VCacheVert *side);
void psx_vcache_poly_ref(void *vertex, short *const *xy, int n);

/* DrawOTag (libgpu.c, and the presenter's, present.c) as it returns, drawn
 * or skipped: the next frame's projections begin (nothing with mode 0). */
void psx_vcache_ot_done(void);

/* The vertex a reference names (1), or 0 if none; valid until the next
 * DrawOTag returns. The logic draw's frames only (no caller since D43). */
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

/* The presenter's frame (pres_draw.c, D129: every frame the logic draw
 * doesn't draw; #158): from here to its DrawOTag's psx_vcache_ot_done, the
 * presenter's projections record into a table of its own and its polygons
 * are looked up there, so neither kind of frame finds the other's. walks:
 * 0 when its DrawOTag won't walk or call the done hook (the GPU disabled).
 * Returns the presenter's projection hook (render_ctx.h
 * g_pres_rtp_value_hook), NULL when the cache doesn't record. Called on
 * the game thread, after the game's walk is joined. */
typedef void (*PsxVcacheValueHook)(const int *proj, int vx, int vy, int vz, unsigned int sxy,
                                   int sz3);
PsxVcacheValueHook psx_vcache_pres_begin(int walks);

/* The presenter's frame on its own thread (the Vita's pres_thread=1, D126;
 * #162), beside the logic draw:
 *  - psx_vcache_pres_async_ok: it may run there. The cache records and
 *    the presenter's table and answers' array are there (made by its first
 *    psx_vcache_pres_begin, inline), so its answers can be written from its
 *    first polygon;
 *  - psx_vcache_pres_detach (the game thread, after psx_vcache_pres_begin,
 *    before the job is posted and the logic draw starts): the frame opened
 *    becomes the presenter's thread's. A polygon whose vertices aren't in
 *    the game's walk's buffer is then the presenter's: looked up in its
 *    table, its answers at the index the walk gives (D152) but in
 *    psx_vcache_pres_side (not g_vcache_side), its counters in
 *    g_vcache_pres_work. The logic draw's polygons and table are as
 *    without it;
 *  - psx_vcache_pres_end (the presenter's thread, as its buffer closes,
 *    after its last polygon): the frame ends (DrawOTag's done hook doesn't
 *    run for it there). Nothing if no frame was detached;
 *  - psx_vcache_pres_side: the presenter's answers (NULL until its first
 *    frame with the cache recording), read by its thread as it records its
 *    vertex buffer. */
int psx_vcache_pres_async_ok(void);
void psx_vcache_pres_detach(void);
void psx_vcache_pres_end(void);
const VCacheVert *psx_vcache_pres_side(void);

/* What the cache's work cost (issue #159), never reset, beside vcache.c's
 * work counters (g_vcache_work: projections, polygons, vertices, misses):
 * g_vcache_cost the game thread's, g_vcache_pres_cost the presenter's
 * thread's (#162), one writer each. Counts that don't depend on timing:
 * float divisions, the table's slots probed, and the 32-byte lines of the
 * table touched (a model: the lines the accesses fall in), for the records
 * (projections) and the lookups (polygon vertices). psx_vcache_stats_sum()
 * logs them ("vcache cost" lines) since its last call. */
enum {
  VCC_REC_DIV,
  VCC_REC_PROBES,
  VCC_REC_LINES,
  VCC_LOOK_DIV,
  VCC_LOOK_PROBES,
  VCC_LOOK_LINES,
  VCC_N
};
extern unsigned g_vcache_cost[VCC_N], g_vcache_pres_cost[VCC_N];

/* The cache's work (issue #160, D165), never reset, one writer each:
 * g_vcache_work the game thread's (and its walk's), g_vcache_pres_work the
 * presenter's thread's (#162). The Vita's work counters (perfcount.c, vc.*)
 * are the game thread's. */
enum {
  VCW_PROJ,     /* projections recorded */
  VCW_POLYS,    /* polygons looked up */
  VCW_VERTS,    /* ... their vertices */
  VCW_MISSES,   /* ... of those not found */
  VCW_INVALID,  /* projections recorded as invalid: never used (#178) */
  VCW_FALLBACK, /* polygons drawn affine because a vertex's key was invalid (#178) */
  VCW_CLAMPED,  /* projections the GTE clamped, recorded at their exact position (#178) */
  VCW_N
};
extern unsigned g_vcache_work[VCW_N], g_vcache_pres_work[VCW_N];

/* Counters since the start or the last psx_vcache_stats_reset() (the Vita
 * takes them with psx_vcache_stats_take instead, #181), by kind of frame:
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
  /* #178 */
  unsigned clamped;     /* projections the GTE clamped, recorded at their exact position */
  unsigned fallback;    /* polygons drawn affine because a vertex's key was invalid */
  unsigned clamp_polys; /* polygons with a clamped vertex found */
  unsigned clamp_persp; /* ... drawn with every vertex found (perspective) */
  unsigned spread;      /* polygons with every vertex found, drawn affine: depths 4x apart */
  unsigned inv_depth;   /* projections invalid: a depth of 0 or less */
  unsigned inv_key;     /* ... the key isn't the GTE's answer for the inputs */
  /* #183: a quad's one vertex with no entry, found again from its polygon
   * (vcache.c derive), counted in hits (and its polygon in persp), not in
   * miss_none */
  unsigned derived;  /* ... drawn with the polygon's depth */
  unsigned der_snap; /* ... of those at the projection recorded before the renderer moved it */
  /* #181: vertices whose key is ambiguous (different points on its pixel)
   * drawn with a depth from the projections under it near the polygon's
   * others in the frame's order (vcache.c resolve), counted in hits (and
   * their polygon in persp), not in miss_ambiguous */
  unsigned resolved;
  unsigned res_order; /* ... of those where the order left one point */
} VCacheStats;
extern VCacheStats g_vcache_stats[2];
void psx_vcache_stats_reset(void);
/* the two kinds' counters added (peak: the larger); also logs the cost
 * line (above) since the last call: both reports call it once a report */
void psx_vcache_stats_sum(VCacheStats *out);
/* The Vita's report (#181), from another thread than the walks': each
 * kind's counters since its last call (win) and their sum, with the same
 * lines as psx_vcache_stats_sum. It writes none of them but the peaks: a
 * reset (psx_vcache_stats_reset) from there raced the walk's adds, and a
 * lost one carried a whole report's polygons into the next (the 44% of
 * logs/owner-20261120-1056 at vblank 3,600). */
void psx_vcache_stats_take(VCacheStats win[2], VCacheStats *sum);

#ifdef __cplusplus
}
#endif

#endif

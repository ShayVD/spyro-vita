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

/* The presenter's projections worked out only when a lookup, a hand-over
 * or a derived point first reads their entry (#193, vcache.c; the same
 * answers). 1 (the default) or 0: each one at once, as before #193 (and the
 * logic draw's, below). */
void psx_vcache_set_lazy(int on);
/* The logic draw's too (#179), worked out by its walk: 1 (the default)
 * while the presenter draws no in-between frames (60 Hz logic, or 30 fps)
 * and the last frame's walk has ended, 2 always, 0 never (each one at
 * once on the game thread, as before). 3 (#204): as 1, but a quota of each
 * frame's first projections, sized from the walk's idle time before the
 * game thread's last projections in recent frames (vcache.c LZ_QUOTA); a
 * `vcache quota:` line with the cost lines. The same answers in each. */
void psx_vcache_set_lazy_logic(int mode);

/* Points a renderer makes or moves on screen after the projection (#189):
 * a hand renderer tells the cache "this screen key derives from these
 * keys", and the cache records the derived point's exact place and depths
 * from the sources' entries in the frame being recorded. A key with an
 * entry of its own (a projection under it) keeps that entry: the hand-over
 * is used only for a vertex that would otherwise have none. Forms (how's
 * low 4 bits; p and q as below):
 *  - VCD_ALIAS: sxy is a's point (a moby shadow's corner nudged a line);
 *  - VCD_LERP: sxy is where the side a-b crosses the line through keys p
 *    and q, cut in 2D (the sky's and a portal face's clip at the portal's
 *    outline): the place from the four's exact places (a key with no entry
 *    there is a 2D point at its pixel centre: the face's screen corners),
 *    the depth the side's there (1/w is linear on screen) when a and b have
 *    depths, else the place only (the polygon stays affine: flat, so only
 *    the place shows);
 *  - VCD_OFFSET: sxy = a + (p, q) pixels (a glow's rim round its projected
 *    centre): a's exact place plus the offset, at a's depth;
 *  - VCD_PERSP: the 3D point p / 4096 of the way from a to b (the world's
 *    split midpoints, p 2048; Spyro clipped at the floor): place
 *    ((1 - t) sa za + t sb zb) / ((1 - t) za + t zb), depth (1 - t) za + t zb.
 * how's bits 4-7: the renderer (VCD_SHADOWS...), for the counters. A
 * source with no usable entry (none, ambiguous, invalid, different points
 * under its key), or two hand-overs placing one key differently, leave the
 * key without one, as before; since #202 two cuts at one key within 2
 * pixels share their mean place, and an alias or offset of an ambiguous
 * key is resolved with it from the frame's order (vcache.c). The hook reads only its arguments and writes
 * only the cache's table: nothing the game reads changes. NULL unless the
 * cache records; the presenter's renderers call their own copy
 * (render_ctx_names.h g_pres_rtp_derive_hook, which render_ctx.c hands to
 * psx_vcache_pres_derive_at and psx_vcache_pres_begin sets with its value
 * hook). */
enum { VCD_ALIAS = 0, VCD_LERP = 1, VCD_OFFSET = 2, VCD_PERSP = 3 };
/* (VCD_PARTICLES: the particles' screen-facing quads, reserved by #202;
 * since #209 ParticlesDraw hands over their corners, VCD_OFFSET from the
 * projected centre. A particle corner counts only where no other renderer
 * placed the key, gives way to any other's, and is no source for another
 * hand-over, so every point placed before stays as it was.) */
enum { VCD_SHADOWS, VCD_SKY_CLIP, VCD_GLOWS, VCD_ENV_SPLIT, VCD_PETE_CLIP, VCD_PARTICLES, VCD_WHO_N };
#define VCD(form, who) ((form) | (who) << 4)
/* #215: a VCD_LERP cut's own fraction, or'ed into how: t / 4096 of the way
 * from a to b, as the clipper moved its integer ends (the sky through a
 * portal: sky_clip_hand.c CutSide's IR0). Where the cut through the edge's
 * line leaves its key with no place (more than 8 pixels off, an ambiguous
 * end, two cuts placed apart), the cache places it t of the way between
 * a's and b's exact places instead (an ambiguous end: its last projection
 * in the frame's order), at the depth there; both ends need depths. Such a
 * place is kept apart from the hand-overs: it is no source for another,
 * and a polygon takes it only for a vertex nothing else finds (vcache.c
 * frac_hand). how's bit 8 says t is there, its high half is t. */
#define VCD_T(t) ((int)(0x100u | (unsigned)(unsigned short)(t) << 16))
#define VCD_HAS_T(how) (((unsigned)(how) & 0x100u) != 0)
#define VCD_T_OF(how) ((int)(short)((unsigned)(how) >> 16))
typedef void (*PsxVcacheDeriveHook)(int how, unsigned int sxy, unsigned int a, unsigned int b,
                                    int p, int q);
extern PsxVcacheDeriveHook g_psyxRtpDeriveHook;
void psx_vcache_pres_derive_at(PsxVcacheDeriveHook *hook);
/* A renderer's call, nothing when the cache doesn't record. The call
 * through the pointer is in psx_vc_derive (vcache.c; the presenter's
 * render_ctx.c pres_vc_derive), as gte_native.h's projections call
 * ngte_vcache: the renderers themselves call no function pointer (the
 * presenter's tests/pres/link_check.py). */
void psx_vc_derive(int how, unsigned int sxy, unsigned int a, unsigned int b, int p, int q);
#define PSX_VC_DERIVE(how, sxy, a, b, p, q)                                                    \
  do {                                                                                         \
    if (__builtin_expect(g_psyxRtpDeriveHook != 0, 0))                                         \
      psx_vc_derive((how), (sxy), (a), (b), (p), (q));                                         \
  } while (0)

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

/* The native projections' record (#205; the sky's records too since #213,
 * sky_hand.c): gte_native.h's RTPS and RTPT, while
 * the hook above (or the game's g_psyxRtpValueHook) is set, call the cache
 * by name with the registers by pointer, their generation (NgteProj gen:
 * the same generation, the same registers) and the sums and SZ3 the GTE
 * emulation worked out (NgteRtp), instead of copying 15 registers into the
 * hook's form for each vertex and summing again here. ngte_vcache
 * (declared in gte_native.h) records the game's into the logic draw's
 * table, pres_vcache_ngte (the presenter's build's ngte_vcache,
 * render_ctx_names.h) the presenter's into its own; each is what the hook
 * set then does with the same projection. g_ngte_gen and g_pres_ngte_gen
 * (gte_native.h) count the two builds' generations. */
struct NgteProj;
struct NgteRtp;
void pres_vcache_ngte(const struct NgteProj *p, unsigned int vxy, unsigned int vz,
                      const struct NgteRtp *o);

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
/* Each thread's counters on cache lines of their own (#193): [0] the game
 * thread's (g_vcache_work, g_vcache_cost), [1] the presenter's thread's
 * (g_vcache_pres_work, g_vcache_pres_cost). Until #193 the four arrays were
 * packed together, so the presenter's adds on core 1 and the logic draw's
 * on core 0 wrote the same 32-byte lines while both draws ran. */
typedef struct {
  unsigned work[VCW_N];
  unsigned cost[VCC_N];
} __attribute__((aligned(64))) VCacheCounters;
extern VCacheCounters g_vcache_ctr[2];
#define g_vcache_work (g_vcache_ctr[0].work)
#define g_vcache_pres_work (g_vcache_ctr[1].work)
#define g_vcache_cost (g_vcache_ctr[0].cost)
#define g_vcache_pres_cost (g_vcache_ctr[1].cost)

/* The logic draw's records by thread (#179), never reset: [0] the game
 * thread's, [1] its walk's (the parse thread on the Vita, D71): projections
 * kept for later (the game thread's only), worked out at once as they came
 * (likewise), worked out in all, and the divisions, probes and table lines
 * that cost (the records' part of g_vcache_cost, which has both), and the
 * 32-byte lines of the store the kept ones were written to (a model, as the
 * table's; not in g_vcache_cost, as #193 left the presenter's). Each thread
 * adds its own once a frame. */
enum { VCR_KEPT, VCR_ONCE, VCR_WORKED, VCR_DIV, VCR_PROBES, VCR_LINES, VCR_STORE, VCR_N };
extern unsigned g_vcache_rec[2][VCR_N];

/* Renderer contexts (#211): which renderer wrote a polygon, so a vertex
 * left with no entry can be told apart: missed (a 3D renderer's vertex the
 * cache should have placed) or exempt (drawn in screen space on purpose).
 * A renderer, as it returns, tells the cache the primitives it wrote in the
 * frame being recorded: the primitive buffer's pointer before and after
 * (p0, p1) and native_draw's record cursor before and after (n0, n1; nb the
 * record pool's start after, for a pool begun in the call). The name is its
 * hand C's (render_hand.h HAND_RENDERER's hand: "EnvDraw"...). The walk
 * then finds the polygon's primitive (or record) among the frame's ranges,
 * the smallest that holds it, a copy's (walk.c, D71) by its offset in its
 * 16 MB window. So the class comes from what the frame recorded, not from
 * the walk's timing. Classes:
 *  - a known renderer's (VCX_ENV...VCX_PORTAL_SKY_FOG) or another marked
 *    one's (VCX_OTHER): missed;
 *  - VCX_PORTAL_FACE (a portal's face: screen corners clipped flat at the
 *    portal's outline, place-only hand-overs) and VCX_OUTSIDE (in no
 *    renderer's range: the HUD's text and boxes, fades, borders, drawn by
 *    the game's own C): exempt;
 *  - VCX_UNMARKED: the frame has no range at all (no renderer told the
 *    cache), so nothing says which.
 * psx_vc_ctx is the logic draw's, pres_vc_ctx the presenter build's (its
 * objects may call only pres_ names, tests/render_ctx/link_check.sh); both
 * do nothing when the cache doesn't record. Only for the counters: no
 * answer changes. */
enum {
  VCX_ENV,             /* EnvDraw */
  VCX_MOBY,            /* MobyDraw */
  VCX_LIT,             /* LitDraw */
  VCX_SPEC,            /* SpecDraw */
  VCX_SPYRO,           /* SpyroDraw */
  VCX_SPYRO_SHADOW,    /* SpyroShadow */
  VCX_MOBY_SHADOWS,    /* MobyShadows */
  VCX_FLAME,           /* FlameDraw */
  VCX_PARTICLES,       /* ParticlesDraw */
  VCX_GLOWS,           /* GlowsAndSparkles */
  VCX_RING,            /* DragonRing */
  VCX_SKY,             /* SkyDraw */
  VCX_FOG_SKY,         /* FogSky */
  VCX_PORTAL_SKY,      /* PortalSkyPlain */
  VCX_PORTAL_SKY_FOG,  /* PortalSkyFog */
  VCX_PORTAL_FACE,     /* PortalFace (exempt) */
  VCX_OTHER,           /* a marked renderer with another name */
  VCX_OUTSIDE,         /* in no renderer's range (exempt) */
  VCX_UNMARKED,        /* the frame has no ranges */
  VCX_N
};
void psx_vc_ctx(const char *name, uintptr_t p0, uintptr_t p1, uintptr_t n0, uintptr_t n1,
                uintptr_t nb);
void pres_vc_ctx(const char *name, uintptr_t p0, uintptr_t p1, uintptr_t n0, uintptr_t n1,
                 uintptr_t nb);

/* Counters since the start or the last psx_vcache_stats_reset() (the Vita
 * takes them with psx_vcache_stats_take instead, #181), by kind of frame:
 * [VC_LOGIC] the logic draw's, [VC_PRES] the presenter's. */
enum { VC_LOGIC = 0, VC_PRES = 1 };
/* Why an ambiguous vertex was left ambiguous (#197, VCacheStats res_why):
 * its polygon also has a vertex with no entry that nothing finds (not a
 * quad's only one, or none of its corners' order names it: no hand-over
 * either), an invalid vertex, or a vertex handed over as a place only
 * (flat); an invalid projection under its key near the polygon's others;
 * a clamped point among several near ones; no depth choice under 4
 * (depths 4x apart, or the derived vertex's plane); the frame's log full or
 * too many depths */
enum { RW_NONE, RW_INVALID, RW_FLAT, RW_INV_NEAR, RW_CLAMP_SEV, RW_SPREAD, RW_FULL, RW_N };
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
   * their polygon in persp), not in miss_ambiguous; #192: a clamped point
   * the order names alone at its own place (counted in clamp_polys too),
   * and keys with more than 32 projections */
  unsigned resolved;
  unsigned res_order; /* ... of those where the order left one point */
  /* #189: vertices with no entry found from their renderer's hand-over
   * (PSX_VC_DERIVE), by renderer, counted in hits (and their polygon in
   * persp when every vertex is found), not in miss_none */
  unsigned hooked[VCD_WHO_N];
  /* ... the hand-overs: made, kept (a place for their key), a source not
   * usable, a key placed differently by another (left with none), the
   * frame's table of them full */
  unsigned hk_calls, hk_kept, hk_nosrc, hk_conflict, hk_full;
  /* #197: ambiguous vertices found again together with their polygon's one
   * vertex with no entry (a quad's: resolved, then derived; counted in
   * resolved and derived too), and those left ambiguous (miss_ambiguous) by
   * why (RW_*) */
  unsigned res_der;
  unsigned res_why[RW_N];
  /* #202: hand-overs cut at an edge's pixel another cut had placed
   * differently, merged into one place (their mean) */
  unsigned hk_merged;
  /* #202: derived vertices at the projection the order named whose depth
   * is that projection's own, chosen with the polygon's others, where the
   * plane through those gave none (counted in derived too) */
  unsigned der_own;
  /* #202: hand-overs of one point (an alias, an offset) whose source key is
   * ambiguous, kept as that key's; and the vertices found so, resolved
   * from the frame's order (counted in hits, not in hooked or resolved) */
  unsigned hk_ambsrc, res_alias;
  /* #211: the vertices left with no entry (miss_none), by the renderer
   * context whose primitive their polygon is (VCX_*, above): their sum is
   * miss_none */
  unsigned ne[VCX_N];
  /* #215: vertices with no entry found at their cut's own fraction (VCD_T;
   * counted in hits and hooked too); the cuts placed so, and those with an
   * end not usable or more than 8 pixels from their key */
  unsigned hk_frac, fr_kept, fr_nosrc;
} __attribute__((aligned(64))) VCacheStats; /* each kind's own lines (#193) */
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

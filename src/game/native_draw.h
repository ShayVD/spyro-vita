/* Native draw lists (decisions D82, docs/native-plan.md stage 2).
 *
 * A hand renderer still builds each PS1 primitive where it always did (the
 * same stores at the same addresses, so the primitive buffer's pointer and
 * every bound check are unchanged), but instead of linking it into its OT
 * bucket for PsyCross to parse, it turns it at once into the vertices
 * PsyCross's parse would have made (PsyCross's GrVertex: screen x/y, page,
 * CLUT, u/v, brightness, dither, colour) and appends them, as a record, to
 * the list's record pool. Into the bucket goes a marker in the primitive's
 * own space:
 *
 *   tag     len 2, the 24-bit link (OtAppend's, as for any primitive)
 *   word 1  ND_MAGIC | pool << 11 | the list's serial (11 bits)
 *   word 2  the word offset of its first record in that pool
 *
 * Both words have a zero top byte, GP0 NOPs to anything that doesn't know
 * the marker. A primitive appended to a bucket whose tail is a marker of
 * this list joins that marker's records instead (the last record's link),
 * so a marker stands for every primitive the renderers put in a row into
 * one bucket. When the OT walk reaches it (PsyCross patch 0010's packet
 * hook, nd_packet), it emits the records: the same splits (PsyX_AddSplit
 * with the same flags under the same draw state) and the same vertices, in
 * the OT's own order: buckets, FIFO within a bucket, the HUD chain last.
 *
 * A record: a header word, the next record's offset (ND_END: none), the
 * marker's last record's offset (its first record only), the page and CLUT
 * every vertex has, then 3 or 4 vertices of 3 words (x/y as the primitive
 * had them; u, v, brightness, dither; colour) in the order PsyCross's
 * MakeVertex* builds them (a quad's 0, 1, 3, 2). The walk makes each a
 * GrVertex and adds the draw offset, the draw environment's dither flag,
 * the vertex cache's hook and a quad's split into two triangles, which
 * depend on the state then.
 *
 * The hottest sites (D86) write the record straight from the values they
 * would have stored (nd_open, nd_mk_*, nd_commit), and skip the PS1
 * primitive's stores; the space it would have taken is still taken, so the
 * buffer's pointer and every bound check are the same.
 *
 * The records of everything appended between two OT links (func_80016784,
 * order_table.c, which every DrawOTag in the game is given) are one list's;
 * the two pools take turns per list, so the walk of a list (on the parse
 * thread, D71) reads its pool while the game writes the next list's. A full
 * pool, or native_draw off, leaves the primitive linked as it is. */
#ifndef NATIVE_DRAW_H
#define NATIVE_DRAW_H

#include "mips2c_rt.h"

#include <stddef.h>

#define AT32_ND(a) (*(u32 *)(uintptr_t)(u32)(a))
#define ADDR_ND(sym) ((u32)(uintptr_t)(sym))

/* config: native_draw=1 (host --native-draw). Needs fast_render=2. */
extern int g_native_draw;
/* host --native-draw-check: each converted renderer's call runs both ways */
extern int g_nd_check;
/* the renderer running now emits native records (nd_begin): ND_ON and the
 * variant's bits (g_nd_var) */
extern int g_nd_on;
/* config nd_var (D87): the record path's variant, ND_ON_* bits */
extern int g_nd_var;
/* config nd_ab=1 (D87, diagnostic): the variants take turns by blocks of
 * lists, each timed on its own (nd_ab_slot) */
extern int g_nd_ab;
enum {
  ND_ON = 1,
  ND_ON_SIDE = 2, /* joins found in the side table, not in the arena and the pool */
  ND_ON_PF = 4,   /* the pool's next lines prefetched for writing */
  /* nd_ab's probe: no records; each primitive a site could make one of
   * writes 12 words into the pool and is then built and linked as a PS1
   * primitive (a second write stream alone, timed against native_draw 0) */
  ND_ON_PROBE = 8,
};

enum {
  ND_MAGIC = 0x4E4000u, /* bits 12-23 */
  ND_POOLS = 2,
  ND_SERIAL = 0x7FFu,  /* word 1's serial bits */
  ND_HEAD = 4u,        /* a record's words before its vertices */
  ND_VW = 3u,          /* words per vertex */
};
#define ND_END 0xFFFFFFFFu /* a record's next: none */

/* header bits */
enum {
  ND_TEX = 1,   /* textured: sets the draw state's tpage; 3 words a vertex, else 2 */
  ND_SEMI = 2,  /* semi-transparent */
  ND_QUAD = 4,  /* 4 vertices, two triangles */
  ND_E1 = 8,    /* a draw mode command: tpage, dither and draw-to-display from bits 16-26 */
  ND_NULL = 16, /* an FT3 with every vertex at -1 (SCE's tpage-only hack): tpage only */
  ND_GOUR = 32, /* untextured Gouraud: dither 1 (flat: 0) */
};

typedef struct {
  u32 *w;
  u32 n, cap; /* words (n: what its last list used) */
} NdPool;

extern NdPool g_nd_pool[ND_POOLS];
extern u32 g_nd_cur;  /* the pool being written */
/* the pool being written: its start, the next free word and its end */
extern u32 *g_nd_base, *g_nd_wp, *g_nd_lim;
extern u32 g_nd_id;   /* word 1 of this list's markers: ND_MAGIC, pool, serial */

/* At a converted renderer's start and end. */
void nd_begin(void);
void nd_end(void);
/* func_80016784: the OT is linked and handed to DrawOTag. */
void nd_list_linked(void);
/* At start-up (after the config): installs the walk's packet hook. */
void nd_init(void (*log)(const char *fmt, ...));
/* The walk's packet hook (PsyCross patch 0010). */
int nd_packet(uintptr_t packet);
/* A log line with the counts since the last call (prefix: "native draw N:") */
void nd_report(int vblank);
/* host --native-draw-check: the call both ways (native_draw_check.c) */
typedef u32 (*NdFn)(u32, u32, u32, u32);
u32 nd_check(const char *name, NdFn fn, u32 a0, u32 a1, u32 a2, u32 a3);
void nd_check_began(void); /* nd_begin, for the check */
void nd_check_set_log(void (*log)(const char *fmt, ...));
void nd_check_report(void);

/* The side table (ND_ON_SIDE, D87): this list's markers by their address,
 * each with its last record, so a join needs neither the tail packet (in
 * the arena) nor its first record (in the pool). The key is the marker's
 * 21-bit arena offset and the list's serial; a marker whose entry another
 * took is only not joined again (a new marker, the same triangles). */
enum { ND_SIDE_N = 1024 };
typedef struct {
  u32 key, last;
} NdSide;
extern NdSide g_nd_side[ND_SIDE_N];
#define ND_SIDE_AT(p) (&g_nd_side[((p) >> 2) & (ND_SIDE_N - 1)])
#define ND_SIDE_KEY(p) (((p) & 0x1FFFFFu) | g_nd_id << 21)

/* nd_ab=1: the variants nd_ab takes in turn (native_draw 0, D86's path,
 * ND_ON_SIDE, ND_ON_PF, both; ND_ON_PROBE, with ND_ON_PF) and the one the list being built now is
 * counted under, or -1 (a block's first lists, or nd_ab off). */
enum { ND_AB_VARIANTS = 7 };
int nd_ab_slot(void);
/* the lists counted under each variant since the last call */
void nd_ab_take(unsigned lists[ND_AB_VARIANTS]);

/* counters (nd_report); the primitives are the markers and the joined */
typedef struct {
  unsigned markers, joined, fallback, e1;
} NdStats;
extern NdStats g_nd_stats;

/* OtAppend: the bucket's tail (+0) and head (+4), new primitives on the end */
static inline void nd_ot_append(u32 slot, u32 prim) {
  u32 tail = AT32_ND(slot);
  AT32_ND(slot) = prim;
  if (tail) {
    u32 t = MIPS_KUSEG(tail);
    *(u16 *)(uintptr_t)t = (u16)prim;
    *(u8 *)(uintptr_t)(t + 2) = (u8)(prim >> 16);
  } else {
    AT32_ND(slot + 4) = prim;
  }
}

/* Room for one record of nv vertices in the current pool, or NULL. */
static inline u32 *nd_reserve(u32 nv) {
  u32 *const r = g_nd_wp;
  if (g_nd_on & ND_ON_PF) {
    /* the next record's two lines, for writing (GCC's prefetch is a plain
     * pld on ARMv7; the Cortex-A9 has the MP extensions' pldw) */
#if defined(__arm__)
    __asm__ volatile(".arch_extension mp\n\tpldw [%0, #64]\n\tpldw [%0, #96]" ::"r"(r));
#else
    __builtin_prefetch(r + 16, 1);
    __builtin_prefetch(r + 24, 1);
#endif
  }
  return (u32)(g_nd_lim - r) >= ND_HEAD + nv * ND_VW ? r : NULL;
}

/* ND_ON_PROBE: 12 words written where the next record would go */
static inline void nd_probe(void) {
  volatile u32 *const v = nd_reserve(4); /* scalar stores, as a record's */
  const u32 x = (u32)(uintptr_t)v;
  if (!v)
    return;
  v[0] = x, v[1] = x + 1, v[2] = x + 2, v[3] = x + 3, v[4] = x + 4, v[5] = x + 5;
  v[6] = x + 6, v[7] = x + 7, v[8] = x + 8, v[9] = x + 9, v[10] = x + 10, v[11] = x + 11;
  g_nd_wp = (u32 *)v + 12;
}

/* One textured vertex: xy as the primitive holds it, u/v/brightness/
 * dither, colour (its low 24 bits; the walk adds alpha 0xFF and the vertex
 * cache's four bytes, and takes page/CLUT from the record's word 3). */
static inline u32 *nd_vtx(u32 *o, u32 xy, u32 uvbd, u32 rgb) {
  o[0] = xy;
  o[1] = uvbd;
  o[2] = rgb;
  return o + ND_VW;
}

/* An untextured one: xy and colour (the walk adds the rest, ND_GOUR) */
static inline u32 *nd_vtx2(u32 *o, u32 xy, u32 rgb) {
  o[0] = xy;
  o[1] = rgb;
  return o + 2;
}

/* The primitive kinds the renderers build, by their GP0 layout */
enum { ND_K_F3, ND_K_F4, ND_K_G3, ND_K_G4, ND_K_FT3, ND_K_FT4, ND_K_GT3, ND_K_GT4, ND_K_E1 };
#define ND_NV(kind)                                                            \
  ((kind) == ND_K_E1 ? 0u                                                      \
   : ((kind) == ND_K_F4 || (kind) == ND_K_G4 || (kind) == ND_K_FT4 || (kind) == ND_K_GT4) ? 4u \
                                                                                : 3u)

#define ND_RAW 0x808080u /* an unshaded texture's colour (MakeColourNoShade) */
#define ND_W(o) AT32_ND(prim + (o))
#define ND_SEMI_OF(c) (((c) >> 24 & 2) ? ND_SEMI : 0)
/* u/v from a UV word, brightness 2, dither the tpage's */
#define ND_UV(w, tp) (((w) & 0xFFFFu) | 0x20000u | ((tp) >> 9 & 1) << 24)
#define ND_FLAT 0x10000u      /* an untextured flat vertex: brightness 1, dither 0 */
#define ND_GOURAUD 0x1010000u /* Gouraud: brightness 1, dither 1 */

/* The records PsyCross's Process*Poly would make of a primitive, from the
 * words it holds (c0: the colour word with the code byte; uvN: texture
 * words, uv0's high half the CLUT, uv1's the tpage), vertices given in the
 * primitive's order and written in MakeVertex*'s (a quad's 0, 1, 3, 2).
 * Untextured (xy and colour only, ND_GOUR): page/CLUT/u/v 0, brightness 1,
 * dither 0 flat and 1 Gouraud;
 * textured: brightness 2, dither the tpage's (the walk adds the draw
 * environment's); flat ones with code bit 0 unshaded. r: nd_reserve's. */
static inline void nd_mk_f(u32 *r, int quad, u32 c0, u32 xy0, u32 xy1, u32 xy2, u32 xy3) {
  const u32 c = (c0 >> 24 & 1) ? ND_RAW : c0;
  u32 *o = r + ND_HEAD;
  r[0] = ND_SEMI_OF(c0) | (quad ? ND_QUAD : 0);
  r[1] = ND_END;
  o = nd_vtx2(o, xy0, c);
  o = nd_vtx2(o, xy1, c);
  if (quad) {
    o = nd_vtx2(o, xy3, c);
    nd_vtx2(o, xy2, c);
  } else {
    nd_vtx2(o, xy2, c);
  }
}

static inline void nd_mk_g3(u32 *r, u32 c0, u32 xy0, u32 c1, u32 xy1, u32 c2, u32 xy2) {
  u32 *o = r + ND_HEAD;
  r[0] = ND_SEMI_OF(c0) | ND_GOUR;
  r[1] = ND_END;
  o = nd_vtx2(o, xy0, c0);
  o = nd_vtx2(o, xy1, c1);
  nd_vtx2(o, xy2, c2);
}

static inline void nd_mk_g4(u32 *r, u32 c0, u32 xy0, u32 c1, u32 xy1, u32 c2, u32 xy2, u32 c3,
                            u32 xy3) {
  u32 *o = r + ND_HEAD;
  r[0] = ND_SEMI_OF(c0) | ND_QUAD | ND_GOUR;
  r[1] = ND_END;
  o = nd_vtx2(o, xy0, c0);
  o = nd_vtx2(o, xy1, c1);
  o = nd_vtx2(o, xy3, c3);
  nd_vtx2(o, xy2, c2);
}

/* FT3/FT4. 0 for an FT3 with every vertex at -1 (IsNull, SCE's tpage-only
 * hack): a tpage-only record, no vertices (still nd_reserve(3)'s room). */
static inline void nd_mk_ft(u32 *r, int quad, u32 c0, u32 xy0, u32 uv0, u32 xy1, u32 uv1,
                            u32 xy2, u32 uv2, u32 xy3, u32 uv3) {
  const u32 tp = uv1 >> 16, c = (c0 >> 24 & 1) ? ND_RAW : c0;
  u32 *o = r + ND_HEAD;
  r[0] = ND_SEMI_OF(c0) | ND_TEX | tp << 16 | (quad ? ND_QUAD : 0);
  r[1] = ND_END;
  r[3] = tp | (uv0 & 0xFFFF0000u);
  if (!quad && (xy0 & xy1 & xy2) == 0xFFFFFFFFu) {
    r[0] |= ND_NULL;
    return;
  }
  o = nd_vtx(o, xy0, ND_UV(uv0, tp), c);
  o = nd_vtx(o, xy1, ND_UV(uv1, tp), c);
  if (quad) {
    o = nd_vtx(o, xy3, ND_UV(uv3, tp), c);
    nd_vtx(o, xy2, ND_UV(uv2, tp), c);
  } else {
    nd_vtx(o, xy2, ND_UV(uv2, tp), c);
  }
}

static inline void nd_mk_gt3(u32 *r, u32 c0, u32 xy0, u32 uv0, u32 c1, u32 xy1, u32 uv1, u32 c2,
                             u32 xy2, u32 uv2) {
  const u32 tp = uv1 >> 16;
  u32 *o = r + ND_HEAD;
  r[0] = ND_SEMI_OF(c0) | ND_TEX | tp << 16;
  r[1] = ND_END;
  r[3] = tp | (uv0 & 0xFFFF0000u);
  o = nd_vtx(o, xy0, ND_UV(uv0, tp), c0);
  o = nd_vtx(o, xy1, ND_UV(uv1, tp), c1);
  nd_vtx(o, xy2, ND_UV(uv2, tp), c2);
}

static inline void nd_mk_gt4(u32 *r, u32 c0, u32 xy0, u32 uv0, u32 c1, u32 xy1, u32 uv1, u32 c2,
                             u32 xy2, u32 uv2, u32 c3, u32 xy3, u32 uv3) {
  const u32 tp = uv1 >> 16;
  u32 *o = r + ND_HEAD;
  r[0] = ND_SEMI_OF(c0) | ND_TEX | tp << 16 | ND_QUAD;
  r[1] = ND_END;
  r[3] = tp | (uv0 & 0xFFFF0000u);
  o = nd_vtx(o, xy0, ND_UV(uv0, tp), c0);
  o = nd_vtx(o, xy1, ND_UV(uv1, tp), c1);
  o = nd_vtx(o, xy3, ND_UV(uv3, tp), c3);
  nd_vtx(o, xy2, ND_UV(uv2, tp), c2);
}

/* A draw mode command (tag, E1 word, a NOP word: env's EmitBlendMode) */
static inline void nd_mk_e1(u32 *r, u32 w1) {
  r[0] = ND_E1 | (w1 & 0x7FFu) << 16;
  r[1] = ND_END;
  g_nd_stats.e1++;
}

/* The primitive at prim (as PsyCross's Process*Poly reads it) as a record in
 * the current pool, or NULL if it's full. KIND is a constant at every call,
 * so this folds to one case. */
static inline __attribute__((always_inline)) u32 *nd_rec(int kind, u32 prim) {
  u32 *r = nd_reserve(ND_NV(kind));
  if (!r)
    return NULL;
  switch (kind) {
  case ND_K_E1:
    nd_mk_e1(r, ND_W(4));
    break;
  case ND_K_F3:
    nd_mk_f(r, 0, ND_W(4), ND_W(0x8), ND_W(0xC), ND_W(0x10), 0);
    break;
  case ND_K_F4:
    nd_mk_f(r, 1, ND_W(4), ND_W(0x8), ND_W(0xC), ND_W(0x10), ND_W(0x14));
    break;
  case ND_K_G3:
    nd_mk_g3(r, ND_W(4), ND_W(0x8), ND_W(0xC), ND_W(0x10), ND_W(0x14), ND_W(0x18));
    break;
  case ND_K_G4:
    nd_mk_g4(r, ND_W(4), ND_W(0x8), ND_W(0xC), ND_W(0x10), ND_W(0x14), ND_W(0x18), ND_W(0x1C),
             ND_W(0x20));
    break;
  case ND_K_FT3:
    nd_mk_ft(r, 0, ND_W(4), ND_W(0x8), ND_W(0xC), ND_W(0x10), ND_W(0x14), ND_W(0x18), ND_W(0x1C),
             0, 0);
    break;
  case ND_K_FT4:
    nd_mk_ft(r, 1, ND_W(4), ND_W(0x8), ND_W(0xC), ND_W(0x10), ND_W(0x14), ND_W(0x18), ND_W(0x1C),
             ND_W(0x20), ND_W(0x24));
    break;
  case ND_K_GT3:
    nd_mk_gt3(r, ND_W(4), ND_W(0x8), ND_W(0xC), ND_W(0x10), ND_W(0x14), ND_W(0x18), ND_W(0x1C),
              ND_W(0x20), ND_W(0x24));
    break;
  case ND_K_GT4:
    nd_mk_gt4(r, ND_W(4), ND_W(0x8), ND_W(0xC), ND_W(0x10), ND_W(0x14), ND_W(0x18), ND_W(0x1C),
              ND_W(0x20), ND_W(0x24), ND_W(0x28), ND_W(0x2C), ND_W(0x30));
    break;
  }
  return r;
}

/* The record just written at r (words long) is the current pool's: its
 * word offset. */
static inline u32 nd_used(u32 *r, u32 words) {
  g_nd_wp = r + words;
  return (u32)(r - g_nd_base);
}

/* A record's words: its head, then nv vertices of 3 words textured, else 2 */
static inline u32 nd_words(const u32 *r, u32 nv) {
  return ND_HEAD + nv * ((r[0] & ND_TEX) ? ND_VW : 2u);
}

/* A marker at prim for the record at word `at`, its first and last */
static inline void nd_mark(u32 prim, u32 at) {
  g_nd_base[at + 2] = at;
  if (g_nd_on & ND_ON_SIDE) {
    NdSide *const e = ND_SIDE_AT(prim);
    e->key = ND_SIDE_KEY(prim);
    e->last = at;
  }
  AT32_ND(prim) = 0x2000000u;
  AT32_ND(prim + 4) = g_nd_id;
  AT32_ND(prim + 8) = at;
  g_nd_stats.markers++;
}

/* 1 if the packet at p (a bucket's tail) is a marker of this list */
static inline int nd_is_mark(u32 p) {
  const u32 t = MIPS_KUSEG(p);
  return AT32_ND(t) >> 24 == 2 && AT32_ND(t + 4) == g_nd_id;
}

/* The record at word `at` after the marker at p's last one */
static inline void nd_join(u32 p, u32 at) {
  u32 *const w = g_nd_base;
  u32 *const first = w + AT32_ND(MIPS_KUSEG(p) + 8);
  w[first[2] + 1] = at;
  first[2] = at;
  g_nd_stats.joined++;
}

/* The record just built at r (nv vertices), for the primitive whose space
 * is prim, about to go on the end of a bucket whose tail is `tail`: 1 if it
 * joined the marker at that tail (no link to make); 0 if prim is now a
 * marker of its own, which the caller links as it would the primitive. */
static inline __attribute__((always_inline)) int nd_commit(u32 *r, u32 nv, u32 tail, u32 prim) {
  const u32 at = nd_used(r, nd_words(r, nv));
  if (g_nd_on & ND_ON_SIDE) {
    /* the marker's first record keeps its own offset as the last (word 2):
     * only the walk reads a marker's records, from the first */
    if (tail) {
      NdSide *const e = ND_SIDE_AT(tail);
      if (e->key == ND_SIDE_KEY(tail)) {
        g_nd_base[e->last + 1] = at;
        e->last = at;
        g_nd_stats.joined++;
        return 1;
      }
    }
  } else if (tail && nd_is_mark(tail)) {
    nd_join(tail, at);
    return 1;
  }
  nd_mark(prim, at);
  return 0;
}

/* The primitive just built at prim, about to go on the end of a bucket
 * whose tail is `tail`: nd_commit's answer, or 0 with the primitive left as
 * it is when the pool is full. */
static inline __attribute__((always_inline)) int nd_take(int kind, u32 tail, u32 prim) {
  u32 *r;
  if (g_nd_on & ND_ON_PROBE)
    return 0; /* linked as a primitive (nd_open wrote the probe's words) */
  r = nd_rec(kind, prim);
  if (!r) {
    g_nd_stats.fallback++;
    return 0;
  }
  return nd_commit(r, ND_NV(kind), tail, prim);
}

/* A site that builds its record from values (D86): room for nv vertices
 * when native_draw is on, else NULL, and the caller builds the primitive
 * as it always did (whose link then counts a full pool's fallback). */
static inline __attribute__((always_inline)) u32 *nd_open(u32 nv) {
  const int on = g_nd_on;
  if (on & ND_ON_PROBE) {
    nd_probe();
    return NULL; /* the site builds and links the PS1 primitive */
  }
  return on ? nd_reserve(nv) : NULL;
}

/* nd_commit into an OT slot (OtAppend's: tail +0, head +4) */
static inline __attribute__((always_inline)) void nd_put(u32 *r, u32 nv, u32 slot, u32 prim) {
  if (!nd_commit(r, nv, AT32_ND(slot), prim))
    nd_ot_append(slot, prim);
}

/* OtAppend with the primitive as a record (env's slots) */
static inline __attribute__((always_inline)) void nd_append(int kind, u32 slot, u32 prim) {
  if (!nd_take(kind, AT32_ND(slot), prim))
    nd_ot_append(slot, prim);
}

#define nd_g3(slot, prim) nd_append(ND_K_G3, slot, prim)
#define nd_g4(slot, prim) nd_append(ND_K_G4, slot, prim)
#define nd_gt3(slot, prim) nd_append(ND_K_GT3, slot, prim)
#define nd_gt4(slot, prim) nd_append(ND_K_GT4, slot, prim)
#define nd_e1(slot, prim) nd_append(ND_K_E1, slot, prim)

/* A chain the code links by hand and puts into a bucket whole (the sky):
 * records join the marker at its end. */
typedef struct {
  u32 head, tail; /* 0: empty */
} NdChain;

static inline void nd_chain_link(NdChain *c, u32 prim) {
  if (c->tail) {
    const u32 t = MIPS_KUSEG(c->tail);
    *(u16 *)(uintptr_t)t = (u16)prim;
    *(u8 *)(uintptr_t)(t + 2) = (u8)(prim >> 16);
  } else {
    c->head = prim;
  }
  c->tail = prim;
}

/* The primitive just built at prim (kind), on the chain's end: as a record,
 * or itself (len words after its tag) when the pool is full */
static inline __attribute__((always_inline)) void nd_chain(NdChain *c, int kind, u32 prim, u32 len) {
  if (nd_take(kind, c->tail, prim))
    return;
  if (AT32_ND(prim + 4) != g_nd_id)
    AT32_ND(prim) = len << 24; /* not a marker: the primitive, link 0 */
  nd_chain_link(c, prim);
}

/* nd_chain for a record built from values (r: nd_open's) */
static inline __attribute__((always_inline)) void nd_chain_rec(NdChain *c, u32 *r, u32 nv, u32 prim) {
  if (!nd_commit(r, nv, c->tail, prim))
    nd_chain_link(c, prim);
}

/* A GT3 the code chains by hand (env's last list): its record, and a marker
 * of its own in its place, tag len 2 and link 0 as `AT32(prim) = 0x9000000`
 * would leave it. 0 if the pool is full (the primitive stays as it is). */
static inline int nd_gt3_chained(u32 prim) {
  u32 *r;
  if (g_nd_on & ND_ON_PROBE)
    return 0;
  r = nd_rec(ND_K_GT3, prim);
  if (!r) {
    g_nd_stats.fallback++;
    return 0;
  }
  nd_mark(prim, nd_used(r, nd_words(r, 3)));
  return 1;
}

#endif

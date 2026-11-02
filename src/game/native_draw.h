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
 * marker's last record's offset (its first record only), then 3 or 4
 * GrVertex (5 words each) in the order PsyCross's MakeVertex* builds them
 * (a quad's 0, 1, 3, 2) with the x/y the primitive had. The walk adds the
 * draw offset, the draw environment's dither flag, the vertex cache's hook
 * and a quad's split into two triangles, which depend on the state then.
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
/* the renderer running now emits native records (nd_begin) */
extern int g_nd_on;

enum {
  ND_MAGIC = 0x4E4000u, /* bits 12-23 */
  ND_POOLS = 2,
  ND_SERIAL = 0x7FFu,  /* word 1's serial bits */
  ND_HEAD = 3u,        /* a record's words before its vertices */
};
#define ND_END 0xFFFFFFFFu /* a record's next: none */

/* header bits */
enum {
  ND_TEX = 1,   /* textured: sets the draw state's tpage */
  ND_SEMI = 2,  /* semi-transparent */
  ND_QUAD = 4,  /* 4 vertices, two triangles */
  ND_E1 = 8,    /* a draw mode command: tpage, dither and draw-to-display from bits 16-26 */
  ND_NULL = 16, /* an FT3 with every vertex at -1 (SCE's tpage-only hack): tpage only */
};

typedef struct {
  u32 *w;
  u32 n, cap; /* words */
} NdPool;

extern NdPool g_nd_pool[ND_POOLS];
extern u32 g_nd_cur;  /* the pool being written */
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

/* counters (nd_report) */
typedef struct {
  unsigned prims, markers, joined, fallback, e1;
  unsigned long long words;
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
  NdPool *p = &g_nd_pool[g_nd_cur];
  const u32 need = ND_HEAD + nv * 5;
  if (p->n + need > p->cap)
    return NULL;
  return p->w + p->n;
}

/* One vertex: xy as the primitive holds it, then page/CLUT, u/v/brightness/
 * dither, colour, and the four bytes the vertex cache's hook may fill. */
static inline u32 *nd_vtx(u32 *o, u32 xy, u32 pc, u32 uvbd, u32 rgb) {
  o[0] = xy;
  o[1] = pc;
  o[2] = uvbd;
  o[3] = (rgb & 0xFFFFFFu) | 0xFF000000u;
  o[4] = 0;
  return o + 5;
}

/* The primitive kinds the renderers build, by their GP0 layout */
enum { ND_K_F3, ND_K_F4, ND_K_G3, ND_K_G4, ND_K_FT3, ND_K_FT4, ND_K_GT3, ND_K_GT4, ND_K_E1 };
#define ND_NV(kind)                                                            \
  ((kind) == ND_K_E1 ? 0u                                                      \
   : ((kind) == ND_K_F4 || (kind) == ND_K_G4 || (kind) == ND_K_FT4 || (kind) == ND_K_GT4) ? 4u \
                                                                                : 3u)
#define ND_WORDS(nv) (ND_HEAD + (nv) * 5u)

#define ND_RAW 0xFF808080u /* an unshaded texture's colour (MakeColourNoShade) */
#define ND_W(o) AT32_ND(prim + (o))
#define ND_SEMI_OF(c) (((c) >> 24 & 2) ? ND_SEMI : 0)
/* u/v from a UV word, brightness 2, dither the tpage's */
#define ND_UV(w, tp) (((w) & 0xFFFFu) | 0x20000u | ((tp) >> 9 & 1) << 24)

/* The primitive at prim (as PsyCross's Process*Poly reads it) as a record in
 * the current pool, or NULL if it's full. Vertices in MakeVertex*'s order (a
 * quad's 0, 1, 3, 2). Untextured: page/CLUT/u/v 0, brightness 1, dither 0
 * flat and 1 Gouraud; textured: brightness 2, dither the tpage's (the walk
 * adds the draw environment's); flat ones with code bit 0, unshaded. KIND is
 * a constant at every call, so this folds to one case. */
static inline __attribute__((always_inline)) u32 *nd_rec(int kind, u32 prim) {
  u32 *r = nd_reserve(ND_NV(kind)), *o;
  const u32 c0 = ND_W(4);
  if (!r)
    return NULL;
  r[1] = ND_END;
  o = r + ND_HEAD;
  switch (kind) {
  case ND_K_E1: /* tag, E1 word, a NOP word (env's EmitBlendMode) */
    r[0] = ND_E1 | (c0 & 0x7FFu) << 16;
    g_nd_stats.e1++;
    break;
  case ND_K_F3:
  case ND_K_F4: {
    const u32 c = (c0 >> 24 & 1) ? ND_RAW : c0;
    r[0] = ND_SEMI_OF(c0) | (kind == ND_K_F4 ? ND_QUAD : 0);
    o = nd_vtx(o, ND_W(0x8), 0, 0x10000u, c);
    o = nd_vtx(o, ND_W(0xC), 0, 0x10000u, c);
    if (kind == ND_K_F4) {
      o = nd_vtx(o, ND_W(0x14), 0, 0x10000u, c);
      nd_vtx(o, ND_W(0x10), 0, 0x10000u, c);
    } else {
      nd_vtx(o, ND_W(0x10), 0, 0x10000u, c);
    }
    break;
  }
  case ND_K_G3:
    r[0] = ND_SEMI_OF(c0);
    o = nd_vtx(o, ND_W(0x8), 0, 0x1010000u, c0);
    o = nd_vtx(o, ND_W(0x10), 0, 0x1010000u, ND_W(0xC));
    nd_vtx(o, ND_W(0x18), 0, 0x1010000u, ND_W(0x14));
    break;
  case ND_K_G4:
    r[0] = ND_SEMI_OF(c0) | ND_QUAD;
    o = nd_vtx(o, ND_W(0x8), 0, 0x1010000u, c0);
    o = nd_vtx(o, ND_W(0x10), 0, 0x1010000u, ND_W(0xC));
    o = nd_vtx(o, ND_W(0x20), 0, 0x1010000u, ND_W(0x1C));
    nd_vtx(o, ND_W(0x18), 0, 0x1010000u, ND_W(0x14));
    break;
  case ND_K_FT3:
  case ND_K_FT4: {
    const u32 uv0 = ND_W(0xC), uv1 = ND_W(0x14), tp = uv1 >> 16, pc = tp | (uv0 & 0xFFFF0000u);
    const u32 c = (c0 >> 24 & 1) ? ND_RAW : c0;
    r[0] = ND_SEMI_OF(c0) | ND_TEX | tp << 16 | (kind == ND_K_FT4 ? ND_QUAD : 0);
    if (kind == ND_K_FT3 && (ND_W(0x8) & ND_W(0x10) & ND_W(0x18)) == 0xFFFFFFFFu) {
      r[0] |= ND_NULL; /* IsNull: the tpage, no triangle; its 3 vertices unused */
      return r;
    }
    o = nd_vtx(o, ND_W(0x8), pc, ND_UV(uv0, tp), c);
    o = nd_vtx(o, ND_W(0x10), pc, ND_UV(uv1, tp), c);
    if (kind == ND_K_FT4) {
      o = nd_vtx(o, ND_W(0x20), pc, ND_UV(ND_W(0x24), tp), c);
      nd_vtx(o, ND_W(0x18), pc, ND_UV(ND_W(0x1C), tp), c);
    } else {
      nd_vtx(o, ND_W(0x18), pc, ND_UV(ND_W(0x1C), tp), c);
    }
    break;
  }
  case ND_K_GT3:
  case ND_K_GT4: {
    const u32 uv0 = ND_W(0xC), uv1 = ND_W(0x18), tp = uv1 >> 16, pc = tp | (uv0 & 0xFFFF0000u);
    r[0] = ND_SEMI_OF(c0) | ND_TEX | tp << 16 | (kind == ND_K_GT4 ? ND_QUAD : 0);
    o = nd_vtx(o, ND_W(0x8), pc, ND_UV(uv0, tp), c0);
    o = nd_vtx(o, ND_W(0x14), pc, ND_UV(uv1, tp), ND_W(0x10));
    if (kind == ND_K_GT4) {
      o = nd_vtx(o, ND_W(0x2C), pc, ND_UV(ND_W(0x30), tp), ND_W(0x28));
      nd_vtx(o, ND_W(0x20), pc, ND_UV(ND_W(0x24), tp), ND_W(0x1C));
    } else {
      nd_vtx(o, ND_W(0x20), pc, ND_UV(ND_W(0x24), tp), ND_W(0x1C));
    }
    break;
  }
  }
  return r;
}

/* The record just written at r (need words) is the current pool's. */
static inline u32 nd_used(const u32 *r, u32 need) {
  NdPool *p = &g_nd_pool[g_nd_cur];
  p->n += need;
  g_nd_stats.prims++;
  g_nd_stats.words += need;
  return (u32)(r - p->w);
}

/* A marker at prim for the record at word `at`, its first and last */
static inline void nd_mark(u32 prim, u32 at) {
  g_nd_pool[g_nd_cur].w[at + 2] = at;
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
  u32 *const w = g_nd_pool[g_nd_cur].w;
  u32 *const first = w + AT32_ND(MIPS_KUSEG(p) + 8);
  w[first[2] + 1] = at;
  first[2] = at;
  g_nd_stats.joined++;
}

/* The primitive just built at prim, about to go on the end of a bucket
 * whose tail is `tail`: 1 if its record joined the marker at that tail (no
 * link to make); 0 if the caller links prim as usual, now a marker of its
 * own, or the primitive itself when the pool is full. */
static inline __attribute__((always_inline)) int nd_take(int kind, u32 tail, u32 prim) {
  u32 *r = nd_rec(kind, prim);
  u32 at;
  if (!r) {
    g_nd_stats.fallback++;
    return 0;
  }
  at = nd_used(r, ND_WORDS(ND_NV(kind)));
  if (tail && nd_is_mark(tail)) {
    nd_join(tail, at);
    return 1;
  }
  nd_mark(prim, at);
  return 0;
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

/* A GT3 the code chains by hand (env's last list): its record, and a marker
 * of its own in its place, tag len 2 and link 0 as `AT32(prim) = 0x9000000`
 * would leave it. 0 if the pool is full (the primitive stays as it is). */
static inline int nd_gt3_chained(u32 prim) {
  u32 *r = nd_rec(ND_K_GT3, prim);
  if (!r) {
    g_nd_stats.fallback++;
    return 0;
  }
  nd_mark(prim, nd_used(r, ND_WORDS(3)));
  return 1;
}

#endif

/* Native draw lists (decisions D82, D86, D91; docs/native-plan.md stage 2).
 *
 * A hand renderer still takes each PS1 primitive's space where it always
 * did (so the primitive buffer's pointer and every bound check are
 * unchanged), but instead of linking the primitive into its OT bucket for
 * PsyCross to parse, it writes the primitive's words as a record into the
 * list's record pool. Into the bucket goes a marker in the primitive's own
 * space:
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
 * hook, nd_packet), it emits the records as PsyCross's Process*Poly would
 * have emitted the primitives: the same splits (PsyX_AddSplit with the same
 * flags under the same draw state) and the same vertices, in the OT's own
 * order: buckets, FIFO within a bucket, the HUD chain last.
 *
 * A record (D91): the next record's offset (ND_END: none), the kind
 * (ND_K_*), then the primitive's words after its tag as the PS1 primitive
 * holds them (the colour word with the code byte, then each vertex's
 * colour, x/y and texture word as the kind has them). The renderers store
 * no more than the primitive's own words; the walk, on the parse thread
 * (D71), turns them into PsyCross's GrVertex (MakeVertex*'s order, a quad's
 * 0, 1, 3, 2) and adds the draw offset, the draw environment's dither
 * flag, the vertex cache's hook and a quad's split into two triangles.
 *
 * The hottest sites (D86) write the record straight from the values they
 * would have stored (nd_open, nd_mk_*, then nd_put, nd_chain_rec or the
 * face loops' LinkRec), and skip the PS1 primitive's stores.
 *
 * Joins are found in a side table (D91), not by reading the tail packet and
 * the marker's first record back: nothing on the game thread reads a pool.
 * The records of everything appended between two OT links (func_80016784,
 * order_table.c, which every DrawOTag in the game is given) are one list's;
 * the two pools take turns per list, so the walk of a list reads its pool
 * while the game writes the next list's. A full pool, or native_draw off,
 * leaves the primitive linked as it is. */
#ifndef NATIVE_DRAW_H
#define NATIVE_DRAW_H

#include "mips2c_rt.h"

#include <stddef.h>

#define AT32_ND(a) (*(u32 *)(uintptr_t)(u32)(a))
#define ADDR_ND(sym) ((u32)(uintptr_t)(sym))

/* config: native_draw=1 (host --native-draw): the renderers' records, which
 * need fast_render=2. native_draw=2 (host --native-draw 2, D92): the
 * renderers build their PS1 primitives as with 0, and the walk parses them
 * with the records' converter (nd_packet). With 1 and 2 the walk also parses
 * the other producers' polygon packets so (the particles, shadows, flame,
 * HUD), unless nd_fast=0. */
extern int g_native_draw;
extern int g_nd_fast;
/* the check's reference walk: no packet parsed by nd_packet but markers */
extern int g_nd_fast_off;
/* host --native-draw-check: each converted renderer's call runs both ways */
extern int g_nd_check;
/* config nd_ab=1 (D91, D92, diagnostic): native_draw 0, 1 and 2 take turns
 * by blocks of lists, each timed on its own (nd_ab_slot) */
extern int g_nd_ab;

enum {
  ND_MAGIC = 0x4E4000u, /* bits 12-23 */
  ND_POOLS = 2,
  ND_SERIAL = 0x7FFu, /* word 1's serial bits */
  ND_MAXW = 14u,      /* the longest record (a GT4) */
};
#define ND_END 0xFFFFFFFFu /* a record's next: none */

/* The primitive kinds the renderers build, by their GP0 layout: a record's
 * word 1, and the words after its tag (ND_BODY) */
enum { ND_K_F3, ND_K_F4, ND_K_G3, ND_K_G4, ND_K_FT3, ND_K_FT4, ND_K_GT3, ND_K_GT4, ND_K_E1 };
#define ND_BODY(kind)                                                          \
  ((kind) == ND_K_F3    ? 4u                                                   \
   : (kind) == ND_K_F4  ? 5u                                                   \
   : (kind) == ND_K_G3  ? 6u                                                   \
   : (kind) == ND_K_G4  ? 8u                                                   \
   : (kind) == ND_K_FT3 ? 7u                                                   \
   : (kind) == ND_K_FT4 ? 9u                                                   \
   : (kind) == ND_K_GT3 ? 9u                                                   \
   : (kind) == ND_K_GT4 ? 12u                                                  \
                        : 1u)

typedef struct {
  u32 *w;
  u32 n, cap; /* words (n: what its last list used) */
} NdPool;

extern NdPool g_nd_pool[ND_POOLS];
extern u32 g_nd_cur; /* the pool being written */

/* What a record site touches, in one place (D91: under -fno-strict-aliasing
 * every store makes the compiler reload these; one base address serves
 * them all) */
typedef struct {
  u32 *wp, *lim, *base; /* the pool being written: the next free word, the
                           last start a record fits at, its start */
  u32 key;              /* the side table's high bits: the list's serial << 21 */
  u32 id;               /* word 1 of this list's markers: ND_MAGIC, pool, serial */
  int on;               /* the renderer running now emits records (nd_begin) */
} NdCursor;
extern NdCursor g_nd;

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
/* host --native-draw-parse-check (D92): a packet nd_packet parses, parsed
 * by PsyCross too from the same state and compared; fast's answer */
extern int g_nd_pcheck;
int nd_pcheck_packet(const u32 *m, int (*fast)(const u32 *m));
void nd_pcheck_report(void);

/* The side table (D91): this list's markers by their address, each with
 * its last record, so a join reads neither the tail packet (in the arena)
 * nor the marker's records (in the pool). The key is the marker's 21-bit
 * arena offset and the list's serial; a marker whose entry another took is
 * only not joined again (a new marker, the same triangles). */
enum { ND_SIDE_N = 1024 };
typedef struct {
  u32 key, last;
} NdSide;
extern NdSide g_nd_side[ND_SIDE_N];
#define ND_SIDE_AT(p) (&g_nd_side[((p) >> 2) & (ND_SIDE_N - 1)])
#define ND_SIDE_KEY(p) (((p) & 0x1FFFFFu) | g_nd.key)

/* nd_ab=1: native_draw 0, 1 or 2, the one the list being built now is
 * counted under, or -1 (a block's first lists, or nd_ab off) */
enum { ND_AB_VARIANTS = 3 };
int nd_ab_slot(void);
/* the lists counted under each since the last call */
void nd_ab_take(unsigned lists[ND_AB_VARIANTS]);

/* a pool full: primitives left as they are (nd_report) */
extern unsigned g_nd_fallback;

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

/* Room for a record in the current pool, or NULL (none, or full). */
static inline u32 *nd_reserve(void) {
  u32 *const r = g_nd.wp;
  return r < g_nd.lim ? r : NULL;
}

/* The records of a primitive, from the words it would hold (c0: the colour
 * word with the code byte; uvN: texture words, uv0's high half the CLUT,
 * uv1's the tpage), in the primitive's order. r: nd_reserve's; each takes
 * its room (the game thread never reads a record back). */
#define ND_REC(r, kind) ((r)[0] = ND_END, (r)[1] = (kind))

static inline void nd_mk_f(u32 *r, int quad, u32 c0, u32 xy0, u32 xy1, u32 xy2, u32 xy3) {
  ND_REC(r, quad ? ND_K_F4 : ND_K_F3);
  r[2] = c0;
  r[3] = xy0;
  r[4] = xy1;
  r[5] = xy2;
  if (quad)
    r[6] = xy3;
  g_nd.wp = r + 2 + (quad ? 5 : 4);
}

static inline void nd_mk_g3(u32 *r, u32 c0, u32 xy0, u32 c1, u32 xy1, u32 c2, u32 xy2) {
  ND_REC(r, ND_K_G3);
  r[2] = c0;
  r[3] = xy0;
  r[4] = c1;
  r[5] = xy1;
  r[6] = c2;
  r[7] = xy2;
  g_nd.wp = r + 2 + 6;
}

static inline void nd_mk_g4(u32 *r, u32 c0, u32 xy0, u32 c1, u32 xy1, u32 c2, u32 xy2, u32 c3,
                            u32 xy3) {
  ND_REC(r, ND_K_G4);
  r[2] = c0;
  r[3] = xy0;
  r[4] = c1;
  r[5] = xy1;
  r[6] = c2;
  r[7] = xy2;
  r[8] = c3;
  r[9] = xy3;
  g_nd.wp = r + 2 + 8;
}

/* FT3/FT4; an FT3 with every vertex at -1 (SCE's tpage-only hack) sets the
 * tpage only (the walk sees it) */
static inline void nd_mk_ft(u32 *r, int quad, u32 c0, u32 xy0, u32 uv0, u32 xy1, u32 uv1,
                            u32 xy2, u32 uv2, u32 xy3, u32 uv3) {
  ND_REC(r, quad ? ND_K_FT4 : ND_K_FT3);
  r[2] = c0;
  r[3] = xy0;
  r[4] = uv0;
  r[5] = xy1;
  r[6] = uv1;
  r[7] = xy2;
  r[8] = uv2;
  if (quad) {
    r[9] = xy3;
    r[10] = uv3;
  }
  g_nd.wp = r + 2 + (quad ? 9 : 7);
}

static inline void nd_mk_gt3(u32 *r, u32 c0, u32 xy0, u32 uv0, u32 c1, u32 xy1, u32 uv1, u32 c2,
                             u32 xy2, u32 uv2) {
  ND_REC(r, ND_K_GT3);
  r[2] = c0;
  r[3] = xy0;
  r[4] = uv0;
  r[5] = c1;
  r[6] = xy1;
  r[7] = uv1;
  r[8] = c2;
  r[9] = xy2;
  r[10] = uv2;
  g_nd.wp = r + 2 + 9;
}

static inline void nd_mk_gt4(u32 *r, u32 c0, u32 xy0, u32 uv0, u32 c1, u32 xy1, u32 uv1, u32 c2,
                             u32 xy2, u32 uv2, u32 c3, u32 xy3, u32 uv3) {
  ND_REC(r, ND_K_GT4);
  r[2] = c0;
  r[3] = xy0;
  r[4] = uv0;
  r[5] = c1;
  r[6] = xy1;
  r[7] = uv1;
  r[8] = c2;
  r[9] = xy2;
  r[10] = uv2;
  r[11] = c3;
  r[12] = xy3;
  r[13] = uv3;
  g_nd.wp = r + 2 + 12;
}

/* The primitive at prim (built and about to be linked) as a record in the
 * current pool, or NULL if it's full. KIND is a constant at every call, so
 * the copy unrolls. */
static inline __attribute__((always_inline)) u32 *nd_rec(int kind, u32 prim) {
  u32 *r = nd_reserve(), i;
  if (!r)
    return NULL;
  ND_REC(r, (u32)kind);
  for (i = 0; i < ND_BODY(kind); i++)
    r[2 + i] = AT32_ND(prim + 4 + i * 4);
  g_nd.wp = r + 2 + ND_BODY(kind);
  return r;
}

/* The record just built at r: its word offset in the current pool. */
static inline u32 nd_at(const u32 *r) { return (u32)(r - g_nd.base); }

/* A marker at prim for the record at word `at`, its first and last */
static inline void nd_mark(u32 prim, u32 at) {
  NdSide *const e = ND_SIDE_AT(prim);
  e->key = ND_SIDE_KEY(prim);
  e->last = at;
  AT32_ND(prim) = 0x2000000u;
  AT32_ND(prim + 4) = g_nd.id;
  AT32_ND(prim + 8) = at;
}

/* 1 if the packet at p (a bucket's tail) is a marker of this list (the
 * check's; the renderers ask the side table) */
static inline int nd_is_mark(u32 p) {
  const u32 t = MIPS_KUSEG(p);
  return AT32_ND(t) >> 24 == 2 && AT32_ND(t + 4) == g_nd.id;
}

/* The record just built at r, for the primitive whose space is prim, about
 * to go on the end of a bucket whose tail is `tail`: 1 if it joined the
 * marker at that tail (no link to make); 0 if prim is now a marker of its
 * own, which the caller links as it would the primitive. */
static inline __attribute__((always_inline)) int nd_commit(u32 *r, u32 tail, u32 prim) {
  const u32 at = nd_at(r);
  if (tail) {
    NdSide *const e = ND_SIDE_AT(tail);
    if (e->key == ND_SIDE_KEY(tail)) {
      g_nd.base[e->last] = at; /* the last record's next */
      e->last = at;
      return 1;
    }
  }
  nd_mark(prim, at);
  return 0;
}

/* The primitive just built at prim, about to go on the end of a bucket
 * whose tail is `tail`: nd_commit's answer, or 0 with the primitive left as
 * it is when the pool is full. */
static inline __attribute__((always_inline)) int nd_take(int kind, u32 tail, u32 prim) {
  u32 *r = nd_rec(kind, prim);
  if (!r) {
    g_nd_fallback++;
    return 0;
  }
  return nd_commit(r, tail, prim);
}

/* A site that builds its record from values (D86): room for a record when
 * native_draw is on, else NULL, and the caller builds the primitive as it
 * always did (whose link then counts a full pool's fallback). Then an
 * nd_mk_* and nd_put, nd_chain_rec or the face loops' LinkRec. nv: the
 * vertices (the room is always a GT4's). */
static inline __attribute__((always_inline)) u32 *nd_open(u32 nv) {
  (void)nv;
  return g_nd.on ? nd_reserve() : NULL;
}

/* nd_commit into an OT slot (OtAppend's: tail +0, head +4) */
static inline __attribute__((always_inline)) void nd_put(u32 *r, u32 slot, u32 prim) {
  if (!nd_commit(r, AT32_ND(slot), prim))
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
  if (AT32_ND(prim + 4) != g_nd.id)
    AT32_ND(prim) = len << 24; /* not a marker: the primitive, link 0 */
  nd_chain_link(c, prim);
}

/* nd_chain for a record built from values (r: nd_open's) */
static inline __attribute__((always_inline)) void nd_chain_rec(NdChain *c, u32 *r, u32 prim) {
  if (!nd_commit(r, c->tail, prim))
    nd_chain_link(c, prim);
}

/* A GT3 the code chains by hand (env's last list): its record, and a marker
 * of its own in its place, tag len 2 and link 0 as `AT32(prim) = 0x9000000`
 * would leave it. 0 if the pool is full (the primitive stays as it is). */
static inline int nd_gt3_chained(u32 prim) {
  u32 *r = nd_rec(ND_K_GT3, prim);
  if (!r) {
    g_nd_fallback++;
    return 0;
  }
  nd_mark(prim, nd_at(r));
  return 1;
}

#endif

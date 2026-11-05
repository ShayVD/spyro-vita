/* The sky renderer's other entries (asm/renderers/r_cyclorama.s, hand-written
 * asm in the original): the sky with depth cueing (func_8004F000), the sky
 * seen through a portal (func_8004F4BC, and func_80050240 with depth
 * cueing) and a portal's face (func_8004FEA0), with the clipping helpers
 * they share (func_8004F7E8 and func_8005060C, func_8004FF90,
 * func_8004FDAC). Written by hand from the asm as C (decisions D96, the D72
 * method): the asm's control flow and arithmetic with named locals, its GTE
 * commands on the register file, and the scratchpad, the spill area
 * (D_80077DD8) and the lists it keeps in D_8006FCF4 as it leaves them.
 *
 * The sky renderer itself is sky_hand.c (D72), so the unit stays linked for
 * fast_render=0 (render_hand.h HAND_UNIT_FN): fast_render 1 and 2 run this,
 * --fast-render-check compares each call with the unit (the whole arena,
 * the scratchpad, every GTE register), and the oracle replay against
 * PCSX-Redux (docs/translation-log.md). The polygons stay PS1 primitives;
 * with native_draw the walk parses them (nd_fast, D92).
 *
 * A sky is a list of sectors (sky_hand.c says how one is laid out). Each
 * entry first keeps the sectors whose bounding sphere is in front of the
 * camera, then transforms each one's vertices and emits a flat or a
 * Gouraud triangle for each of its triangles not wholly off one side of
 * the screen (or, through a portal, of the portal's box on screen).
 *
 * Through a portal, each such triangle is then clipped against the
 * portal's outline on screen (D_80077EA0: a list of edges, 0x18 bytes each,
 * ended by a 0 word): for each edge, the corners on its outer side
 * (NCLIP's sign) are cut off where the triangle's sides cross it
 * (func_8004FDAC, which also interpolates the colours with the GTE). Cutting
 * one corner leaves a quad: one half goes on with the next edge, the other
 * waits on a stack (D_80075798's pointer, from D_8006FCF4 + 0x800) until
 * the sector's triangles are done. A portal's face is two triangles
 * clipped the same way, without colours, the halves in a queue. */
#include "render_hand.h"

#include <stdint.h>

extern char g_Camera[];     /* its projection matrix rows at +0, view at +0x14 */
extern char g_Cyclorama[];  /* the level's sky: count, sectors */
extern char g_WorldOT[];
extern char D_8006FCF4[];   /* working lists (below) */
extern char D_8006CB8C[];   /* a portal's face: two triangles' screen points */
extern char D_8007575C[];   /* depth cue: the sky's IR0 */
extern char D_800757D4[];   /* depth cue: the far colour, r | g << 8 | b << 16 */
extern char D_80075780[];   /* the primitive buffer's end */
extern char D_80075798[];   /* the clipped triangles' stack pointer */
extern char D_800757B0[];   /* the primitive buffer's next free byte */
extern char D_800758B0[];   /* set when the buffer filled up */
extern char D_80075934[];   /* the world OT slot the portals draw into */
extern char D_80077DD8[];   /* the asm's spill area */
extern char D_80077EA0[];   /* the portal's outline on screen: its edges */
extern char D_8007AA00[];   /* the portal's box on screen: top, bottom, right, left */

/* D_8006FCF4's lists */
#define SEEN_SECTORS ((u32)(uintptr_t)D_8006FCF4)          /* pass 1, 0-ended */
#define CLIP_STACK ((u32)(uintptr_t)D_8006FCF4 + 0x800u)   /* triangles waiting */
#define FOG_COLOURS ((u32)(uintptr_t)D_8006FCF4 + 0x2800u) /* a sector's depth-cued colours */

/* A sky sector (words; sky_hand.c). */
enum {
  SEC_CENTER_XY = 0,
  SEC_CENTER_Z = 1, /* radius (low half) | centre z << 16 */
  SEC_ORIGIN_XY = 2,
  SEC_ORIGIN_Z = 3, /* vertex count (low half) | origin z << 16 */
  SEC_TRIANGLES = 4,
  SEC_VERTICES = 6,
};

/* A portal's outline edge (words): set, its two screen points, and its
 * line's A, B and C (A * x + B * y + C, func_8004FDAC). */
enum { EDGE_SET, EDGE_XY0, EDGE_XY1, EDGE_A, EDGE_B, EDGE_C, EDGE_WORDS };
#define EDGE_SIZE (EDGE_WORDS * 4u)

#define TAG_F3 MEM_TAG(0x84000000u)
#define TAG_G3 MEM_TAG(0x86000000u)
/* A colour word's command byte: POLY_G3's 0x30 less this is POLY_F3's. */
#define G3_TO_F3 0x10000000u

/* Spill area words the clipper uses (the asm's temporaries) */
enum {
  SPILL_AT = 0x30 / 4, /* the triangle's corners on entry to a cut */
  SPILL_V0,
  SPILL_V1,
  SPILL_A2, /* the box's left, top and bottom, kept across the cuts */
  SPILL_A3,
  SPILL_T0,
  SPILL_RA,             /* the clipper's return address (a PS1 code address) */
  SPILL_LIST = 0x54 / 4 /* the sector list's place, kept across the clipping */
};
#define SPILL ((u32 *)D_80077DD8)

/* ------------------------------------------------------------ passes 1, 2 */

/* The rotation of a matrix of five words (rows of halves) into the GTE. */
static void SetRotation(const u32 *m) {
  CTC2(m[0], 0);
  CTC2(m[1], 1);
  CTC2(m[2], 2);
  CTC2(m[3], 3);
  CTC2(m[4], 4);
}

/* Pass 1: of `count` sectors at `sectors`, the ones whose sphere is in
 * front of the camera (its centre's depth past its radius), into
 * SEEN_SECTORS, 0-ended. The view rotation, no translation. */
static void SectorsInFront(const u32 *view, u32 count, u32 sectors) {
  const u32 *s = HPTR(const u32, sectors), *end = s + count;
  u32 *out = (u32 *)SEEN_SECTORS;
  SetRotation(view);
  CTC2(0u, 5);
  CTC2(0u, 6);
  CTC2(0u, 7);
  for (; s != end; s++) {
    const u32 *sector = HPTR(const u32, *s);
    s32 radius;
    MTC2(sector[SEC_CENTER_XY], 0);
    MTC2((u32)((s32)sector[SEC_CENTER_Z] >> 16), 1);
    radius = (s16)sector[SEC_CENTER_Z];
    gte_rtps();
    (void)MFC2(19); /* the asm reads SZ3 first (a read that normalises it) */
    if ((s32)(MFC2(27) - (u32)radius) > 0)
      *out++ = *s;
  }
  *out = 0;
}

/* The screen's (or a portal's) box on screen, as the clip flags test it:
 * screen x | y << 16 words, x compared in the high half. */
typedef struct {
  u32 top, bottom; /* the y limits, as whole words */
  u32 left, right; /* the x limits, as x << 16 */
} Box;
static const Box SCREEN = {0x10000u, 0x1000000u, 0u, 0x2000000u};

/* A vertex's clip word: its screen x/y shifted left 5, then 1, 2, 4, 8 if
 * above, below, left of, right of the box. */
static u32 ClipFlags(u32 sxy, const Box *b) {
  u32 flags = sxy << 5;
  if ((s32)(sxy - b->top) <= 0)
    flags += 1;
  if ((s32)(sxy - b->bottom) >= 0)
    flags += 2;
  if ((s32)((sxy << 16) - b->left) <= 0)
    flags += 4;
  if ((s32)((sxy << 16) - b->right) >= 0)
    flags += 8;
  return flags;
}

/* A packed vertex (sky_hand.c's VertexWords) into the GTE's V0. */
static void LoadVertex(u32 w, s32 ox, s32 oy, s32 oz) {
  u32 z = (w >> 21) + (u32)oz;
  u32 x = (u32)ox - ((w >> 10) & 0x7FF);
  u32 y = (u32)oy - (w & 0x3FF);
  MTC2(z, 1);
  MTC2(x + (y << 16), 0);
}

/* A sector's vertices projected, each one's clip word into the scratchpad;
 * the flags they all share. As in sky_hand.c, each RTPS overlaps loading
 * the next vertex, so V0 ends up holding vert[n]. */
static u32 ProjectSector(const u32 *sector, const Box *box) {
  const s32 ox = (s16)sector[SEC_ORIGIN_XY], oy = (s32)sector[SEC_ORIGIN_XY] >> 16;
  const s32 oz = (s32)sector[SEC_ORIGIN_Z] >> 16;
  const u32 n = sector[SEC_ORIGIN_Z] & 0xFFFF;
  const u32 *vert = sector + SEC_VERTICES;
  u32 *scratch = (u32 *)SCRATCH_BASE, all = ~0u, i = 0;
  LoadVertex(vert[0], ox, oy, oz);
  do {
    u32 flags;
    gte_rtps();
    LoadVertex(vert[i + 1], ox, oy, oz);
    flags = ClipFlags(MFC2(14), box); /* SXY2 */
    all &= flags;
    scratch[i] = flags;
  } while (++i != n);
  return all;
}

/* Depth cueing: each of a sector's colours (the words from `colours` to its
 * triangle list) moved toward the far colour by IR0 (DPCS), into
 * FOG_COLOURS. The asm loads each next colour into RGB as the DPCS before
 * runs, so RGB ends up holding the word after the last (the first
 * triangle's). */
static void FogColours(const u32 *colours, u32 bytes) {
  const u32 fog = *(const u32 *)D_800757D4;
  const u32 *c = colours, *end = (const u32 *)((const u8 *)colours + bytes);
  u32 *out = (u32 *)FOG_COLOURS;
  CTC2(((fog >> 16) & 0xFF) << 4, 23); /* BFC */
  CTC2((fog & 0xFF) << 4, 21);         /* RFC */
  CTC2(((fog >> 8) & 0xFF) << 4, 22);  /* GFC */
  MTC2(*(const u32 *)D_8007575C, 8);   /* IR0 */
  MTC2(*c++, 6);
  do {
    m2c_gte_0x780010(); /* DPCS */
    *out++ = MFC2(22);  /* RGB2 */
    MTC2(*c++, 6);
  } while (c != end + 1);
}

/* The colour word at a byte offset (the triangles index colours by 4s). */
#define COLOUR(tbl, off) WORD_AT(tbl, off)

/* ------------------------------------------------------------ primitives */

/* The primitives a call builds: one chain, put in an OT slot at the end. */
typedef struct {
  u32 prim;  /* the next primitive (fp) */
  u32 prev;  /* where the last tag goes (sp): the word before the first, then each primitive */
  u32 tag;   /* the last primitive's length tag (t9) */
  u32 first; /* the first primitive (gp) */
} Chain;

static void ChainBegin(Chain *ch) {
  ch->prim = *(u32 *)D_800757B0 + 4; /* the first primitive's tag follows a word */
  ch->first = ch->prim;
  ch->prev = ch->prim - 4;
  ch->tag = 0;
}

/* Links the last primitive to this one (its tag word, made of the last one's
 * length and this one's address) and makes this one the last. */
static void ChainLink(Chain *ch) {
  AT32(MIPS_KUSEG(ch->prev)) = MEM_TAG_LINK(ch->tag, ch->prim);
  ch->prev = ch->prim;
}

/* A triangle as a POLY_F3 if `flat`, else a POLY_G3; xy are the corners'
 * screen words, c their colours. */
static void EmitTriangle(Chain *ch, int flat, u32 xy0, u32 xy1, u32 xy2, u32 c0, u32 c1, u32 c2) {
  const u32 p = MIPS_KUSEG(ch->prim);
  ChainLink(ch);
  if (flat) {
    ch->tag = TAG_F3;
    AT32(p + 0x4) = c0 - G3_TO_F3;
    AT32(p + 0x8) = xy0;
    AT32(p + 0xC) = xy1;
    AT32(p + 0x10) = xy2;
    ch->prim += 0x14;
  } else {
    ch->tag = TAG_G3;
    AT32(p + 0x8) = xy0;
    AT32(p + 0x10) = xy1;
    AT32(p + 0x18) = xy2;
    AT32(p + 0x4) = c0;
    AT32(p + 0xC) = c1;
    AT32(p + 0x14) = c2;
    ch->prim += 0x1C;
  }
}

/* The buffer's next free byte, and the chain (ended) into the OT's slot at
 * `slot` (its tail word, then its head), after what's there. */
static void ChainEnd(Chain *ch, u32 slot) {
  u32 tail;
  *(u32 *)D_800757B0 = ch->prim;
  if (ch->prim == ch->first)
    return;
  tail = AT32(slot);
  AT32(MIPS_KUSEG(ch->prev)) = MEM_TAG_END(ch->tag);
  AT32(slot) = ch->prev;
  if (tail) {
    const u32 t = MIPS_KUSEG(tail);
    AT16(t) = (u16)ch->first;
    AT8(t + 2) = (u8)(ch->first >> 16);
  } else {
    AT32(slot + 4) = ch->first;
  }
}

/* The world OT's slot for the portals' primitives. */
static u32 PortalSlot(void) { return MIPS_KUSEG(*(u32 *)g_WorldOT) + (*(u32 *)D_80075934 << 3); }

/* ------------------------------------------------------------ the sky with depth cueing */

/* func_8004F000: the level's sky (every sector), its colours depth-cued
 * toward the far colour, with the camera's matrices. As sky_hand.c's
 * renderer otherwise, its chain into the world OT's last slot. */
static u32 FogSky(u32 a0, u32 a1, u32 a2, u32 a3) {
  const u32 *cyc = (const u32 *)g_Cyclorama;
  const u32 *visible = (const u32 *)SEEN_SECTORS;
  const u32 limit = *(u32 *)D_80075780 - 0x400;
  u32 *const scratch = (u32 *)SCRATCH_BASE;
  Chain ch;
  (void)a0, (void)a1, (void)a2, (void)a3;

  hand_spill_regs();
  SectorsInFront((const u32 *)(g_Camera + 0x14), cyc[0], cyc[1]);
  SetRotation((const u32 *)g_Camera);
  ChainBegin(&ch);
  for (;;) {
    const u32 *sector, *colours, *tri, *end;
    u32 packed;
    if (!*visible)
      break;
    sector = HPTR(const u32, *visible); /* HPTR reads its argument twice */
    visible++;
    packed = sector[SEC_TRIANGLES];
    colours = sector + SEC_VERTICES + (sector[SEC_ORIGIN_Z] & 0xFFFF);
    FogColours(colours, packed >> 14);
    if (ProjectSector(sector, &SCREEN) & 0xF)
      continue; /* every vertex off the same side */
    tri = (const u32 *)((const u8 *)colours + (packed >> 14));
    end = (const u32 *)((const u8 *)tri + ((packed << 3) & 0xFFF8));
    for (; tri != end; tri += 2) {
      u32 f0, f1, f2;
      if ((s32)(limit - ch.prim) <= 0) {
        *(u32 *)D_800758B0 = 1;
        goto done;
      }
      f0 = WORD_AT(scratch, tri[0] >> 20);
      f1 = WORD_AT(scratch, (tri[0] >> 10) & 0x3FC);
      f2 = WORD_AT(scratch, tri[0] & 0x3FC);
      if ((s32)(f0 & f1 & f2 & 0x1F) > 0)
        continue; /* all three off the same side */
      {
        /* sky_hand.c's order: the Gouraud layout's positions first, which
         * the flat one moves; the same bytes end up in RAM */
        const u32 xy0 = (u32)((s32)f0 >> 5), xy1 = (u32)((s32)f1 >> 5), xy2 = (u32)((s32)f2 >> 5);
        const u32 i0 = tri[1] >> 20, i1 = (tri[1] >> 10) & 0x3FC, i2 = tri[1] & 0x3FC;
        const u8 *fog = (const u8 *)FOG_COLOURS;
        AT32(MIPS_KUSEG(ch.prim) + 0x18) = xy2;
        /* flat when the three colour indices are the same (sky_hand.c) */
        EmitTriangle(&ch, i0 == i1 && i0 == i2, xy0, xy1, xy2, COLOUR(fog, i0), COLOUR(fog, i1), COLOUR(fog, i2));
      }
    }
  }
done:
  ChainEnd(&ch, MIPS_KUSEG(*(u32 *)g_WorldOT) + 0x3FF8);
  return 0; /* v0 is dead at every caller (C statements in draw.c) */
}

/* ------------------------------------------------------------ cutting a side */

/* The registers func_8004FDAC leaves that a caller reads later: the colours
 * it was given come back as the edge's B and C (the face's clipper passes
 * them on to its next cut), t0 as its last product. */
typedef struct {
  u32 a2, a3;
} CutRegs;

static s32 Quotient(s32 n, s32 d) { return n == INT32_MIN && d == -1 ? n : n / d; }

/* func_8004FDAC: the point where the side from `from` to `to` (screen words)
 * crosses the edge's line, and the colour there, from `from`'s colour
 * (r->a2) toward `to`'s (r->a3) by the same fraction: the fraction t of the
 * way (A·from + B·from + C over A·d + B·d, as 1.12 with rounding), then
 * GPL moves `from` by t·d and DPCS moves the colour by t. Returns the point;
 * *rgb gets the colour, with POLY_G3's command byte. A side parallel to the
 * line (a 0 divisor) returns from's x and junk, as the asm does. */
static u32 CutSide(u32 from, u32 to, CutRegs *r, u32 edge, u32 *rgb) {
  const u32 *e = HPTR(const u32, edge);
  const u32 x0 = (u32)(s32)(s16)from, y0 = (u32)((s32)from >> 16);
  const u32 dx = (u32)(s32)(s16)to - x0, dy = (u32)((s32)to >> 16) - y0;
  const u32 fc = r->a3, ea = e[EDGE_A];
  u32 den, num;
  s32 t;
  MTC2(r->a2, 6); /* RGBC: from's colour */
  CTC2((fc & 0xFF) << 4, 21);
  CTC2(((fc >> 8) & 0xFF) << 4, 22);
  CTC2(((fc >> 16) & 0xFF) << 4, 23);
  r->a2 = e[EDGE_B];
  r->a3 = e[EDGE_C];
  MTC2(x0, 25); /* MAC1, MAC2: the start */
  MTC2(y0, 26);
  MTC2(dx, 9); /* IR1, IR2: the side */
  MTC2(dy, 10);
  den = ea * dx + r->a2 * dy;
  if (den == 0) {
    *rgb = y0 | dy;
    return x0;
  }
  num = ((ea * x0 + r->a3 + r->a2 * y0) << 7) + 0x40;
  t = Quotient((s32)num, (s32)den);
  MTC2(((0u - (u32)t) << 5) + 0x10, 8); /* IR0 */
  m2c_gte_0x1A8003E();                 /* GPL, sf=1 */
  {
    const u32 mac1 = MFC2(25), mac2 = MFC2(26);
    m2c_gte_0x780010(); /* DPCS */
    *rgb = 0x30000000u | MFC2(22);
    return (mac2 << 16) | (mac1 & 0xFFFF);
  }
}

/* NCLIP's sign of each corner against an edge: 8, 0x10, 0x20 for the
 * corners on its outer side (or on it). */
static u32 OutsideEdge(const u32 *e, u32 xy0, u32 xy1, u32 xy2, int face) {
  u32 out = 0;
  if (face) {
    MTC2(e[EDGE_XY1], 13);
    MTC2(e[EDGE_XY0], 12);
  } else {
    MTC2(e[EDGE_XY0], 12);
    MTC2(e[EDGE_XY1], 13);
  }
  MTC2(xy0, 14);
  gte_nclip();
  if ((s32)MFC2(24) <= 0)
    out |= 8;
  MTC2(xy1, 14);
  gte_nclip();
  if ((s32)MFC2(24) <= 0)
    out |= 0x10;
  MTC2(xy2, 14);
  gte_nclip();
  if ((s32)MFC2(24) <= 0)
    out |= 0x20;
  return out;
}

/* ------------------------------------------------------------ the sky through a portal */

/* A triangle being clipped: its corners (the asm's at, v0, v1) and colours
 * (t6, t7, s0). On the stack, the same six words and the edge it waited
 * at: 0x1C bytes. */
typedef struct {
  u32 xy[3];
  u32 rgb[3];
} Tri;

typedef struct {
  Chain ch;
  CutRegs box; /* a2, a3 (and t0) as the cuts find them: the box's left, top, bottom */
  u32 bottom;
} Portal;

/* The half of a cut quad that waits: onto the stack, with its edge. */
static void Push(u32 xy0, u32 xy1, u32 xy2, u32 c0, u32 c1, u32 c2, u32 edge) {
  const u32 top = *(u32 *)D_80075798, p = MIPS_KUSEG(top);
  AT32(p + 0x0) = xy0;
  AT32(p + 0xC) = c0;
  AT32(p + 0x10) = c1;
  AT32(p + 0x14) = c2;
  AT32(p + 0x4) = xy1;
  AT32(p + 0x8) = xy2;
  AT32(p + 0x18) = edge;
  *(u32 *)D_80075798 = top + 0x1C;
}

/* func_8004F7E8 (func_8005060C, the same code, for func_80050240): t
 * clipped against the outline's edges from `edge` on, what's left emitted.
 * Each cut first saves the triangle and the box registers in the spill
 * area, and the return address `ra` (the PS1 address the asm's jal left).
 * The two sides from the inner corner(s) are cut (CutSide); one corner off
 * leaves a quad, whose half (the cut points and the far inner corner)
 * waits on the stack. Every corner off: dropped. */
static void ClipSky(Portal *o, Tri t, u32 edge, u32 ra) {
  u32 *const sp = SPILL;
  for (;; edge += EDGE_SIZE) {
    const u32 *e = HPTR(const u32, edge);
    u32 p0, p1, p2, c0, c1, c2, q0, q1, r0, r1, out;
    if (!e[EDGE_SET])
      break;
    out = OutsideEdge(e, t.xy[0], t.xy[1], t.xy[2], 0);
    if (out == 0)
      continue;
    if (out == 0x38)
      return;
    p0 = t.xy[0], p1 = t.xy[1], p2 = t.xy[2];
    c0 = t.rgb[0], c1 = t.rgb[1], c2 = t.rgb[2];
    sp[SPILL_AT] = p0;
    sp[SPILL_V0] = p1;
    sp[SPILL_V1] = p2;
    sp[SPILL_A2] = o->box.a2;
    sp[SPILL_A3] = o->box.a3;
    sp[SPILL_T0] = o->bottom;
    sp[SPILL_RA] = ra;
    {
      CutRegs r;
      switch (out) {
      case 0x08: /* corner 0 off: (q0, p1, p2) goes on, (q1, q0, p2) waits */
        r.a2 = c1, r.a3 = c0;
        q0 = CutSide(p1, p0, &r, edge, &r0);
        sp[SPILL_AT] = q0;
        r.a2 = c2, r.a3 = c0;
        q1 = CutSide(p2, p0, &r, edge, &r1);
        Push(q1, q0, p2, r1, r0, c2, edge);
        t = (Tri){{q0, p1, p2}, {r0, c1, c2}};
        break;
      case 0x10: /* corner 1 off: (p0, q0, p2) goes on, (q1, q0, p0) waits */
        r.a2 = c2, r.a3 = c1;
        q0 = CutSide(p2, p1, &r, edge, &r0);
        sp[SPILL_V0] = q0;
        r.a2 = c0, r.a3 = c1;
        q1 = CutSide(p0, p1, &r, edge, &r1);
        Push(q1, q0, p0, r1, r0, c0, edge);
        t = (Tri){{p0, q0, p2}, {c0, r0, c2}};
        break;
      case 0x18: /* corners 0, 1 off: (q0, q1, p2) */
        r.a2 = c2, r.a3 = c0;
        q0 = CutSide(p2, p0, &r, edge, &r0);
        sp[SPILL_AT] = q0;
        r.a2 = c2, r.a3 = c1;
        q1 = CutSide(p2, p1, &r, edge, &r1);
        t = (Tri){{q0, q1, p2}, {r0, r1, c2}};
        break;
      case 0x20: /* corner 2 off: (p0, p1, q0) goes on, (q1, q0, p1) waits */
        r.a2 = c0, r.a3 = c2;
        q0 = CutSide(p0, p2, &r, edge, &r0);
        sp[SPILL_V1] = q0;
        r.a2 = c1, r.a3 = c2;
        q1 = CutSide(p1, p2, &r, edge, &r1);
        Push(q1, q0, p1, r1, r0, c1, edge);
        t = (Tri){{p0, p1, q0}, {c0, c1, r0}};
        break;
      case 0x28: /* corners 0, 2 off: (q0, p1, q1) */
        r.a2 = c1, r.a3 = c0;
        q0 = CutSide(p1, p0, &r, edge, &r0);
        sp[SPILL_AT] = q0;
        r.a2 = c1, r.a3 = c2;
        q1 = CutSide(p1, p2, &r, edge, &r1);
        t = (Tri){{q0, p1, q1}, {r0, c1, r1}};
        break;
      default: /* 0x30, corners 1, 2 off: (p0, q0, q1) */
        r.a2 = c0, r.a3 = c1;
        q0 = CutSide(p0, p1, &r, edge, &r0);
        sp[SPILL_V0] = q0;
        r.a2 = c0, r.a3 = c2;
        q1 = CutSide(p0, p2, &r, edge, &r1);
        t = (Tri){{p0, q0, q1}, {c0, r0, r1}};
        break;
      }
    }
    /* the box registers come back from the spill area */
    o->box.a2 = sp[SPILL_A2];
    o->box.a3 = sp[SPILL_A3];
    o->bottom = sp[SPILL_T0];
  }
  /* in the outline: emitted, flat when the three colours are the same (the
   * colours here, not their indices; no Gouraud positions first) */
  EmitTriangle(&o->ch, t.rgb[0] == t.rgb[1] && t.rgb[0] == t.rgb[2], t.xy[0], t.xy[1], t.xy[2], t.rgb[0],
               t.rgb[1], t.rgb[2]);
}

/* func_8004F4BC and func_80050240: the sky seen through a portal (pSky's
 * sectors, pView's rotation for pass 1, pSkyView's for pass 2), each
 * triangle clipped to the portal's box and outline; with `fog`, its colours
 * depth-cued first (func_80050240). `ra1` and `ra2` are the return
 * addresses of the clipper's two calls in the asm, which it saves. */
static void PortalSky(u32 sky, u32 view, u32 proj, int fog, u32 ra1, u32 ra2) {
  const u32 *s = HPTR(const u32, sky);
  const u32 *box = (const u32 *)D_8007AA00;
  u32 *const scratch = (u32 *)SCRATCH_BASE;
  u32 list = SEEN_SECTORS;
  Portal o;
  Box b;

  hand_spill_regs();
  SectorsInFront(HPTR(const u32, view), s[0], s[1]);
  SetRotation(HPTR(const u32, proj));
  ChainBegin(&o.ch);
  b.top = box[0], b.bottom = box[1], b.right = box[2], b.left = box[3];
  o.box.a2 = b.left, o.box.a3 = b.top, o.bottom = b.bottom;
  for (;;) {
    const u32 *sector, *colours, *tri, *end;
    const u8 *table;
    u32 packed, at = AT32(list);
    list += 4;
    if (!at)
      break;
    sector = HPTR(const u32, at);
    packed = sector[SEC_TRIANGLES];
    colours = sector + SEC_VERTICES + (sector[SEC_ORIGIN_Z] & 0xFFFF);
    if (fog)
      FogColours(colours, packed >> 14);
    /* the cuts change a2, a3 and t0, but put them back after each one */
    b.left = o.box.a2, b.top = o.box.a3, b.bottom = o.bottom;
    if (ProjectSector(sector, &b) & 0xF)
      continue;
    table = fog ? (const u8 *)FOG_COLOURS : (const u8 *)colours;
    tri = (const u32 *)((const u8 *)colours + (packed >> 14));
    end = (const u32 *)((const u8 *)tri + ((packed << 3) & 0xFFF8));
    SPILL[SPILL_LIST] = list;
    for (; tri != end; tri += 2) {
      const u32 f0 = WORD_AT(scratch, tri[0] >> 20), f1 = WORD_AT(scratch, (tri[0] >> 10) & 0x3FC);
      const u32 f2 = WORD_AT(scratch, tri[0] & 0x3FC);
      Tri t;
      if ((s32)(f0 & f1 & f2 & 0x1F) > 0)
        continue;
      t = (Tri){{(u32)((s32)f0 >> 5), (u32)((s32)f1 >> 5), (u32)((s32)f2 >> 5)},
                {WORD_AT(table, tri[1] >> 20), WORD_AT(table, (tri[1] >> 10) & 0x3FC), WORD_AT(table, tri[1] & 0x3FC)}};
      ClipSky(&o, t, (u32)(uintptr_t)D_80077EA0, ra1);
    }
    /* then the halves that waited, last first, each from the edge after
     * the one it waited at */
    for (;;) {
      u32 top = *(u32 *)D_80075798, p;
      Tri t;
      if (top == CLIP_STACK)
        break;
      top -= 0x1C;
      *(u32 *)D_80075798 = top;
      p = MIPS_KUSEG(top);
      t = (Tri){{AT32(p), AT32(p + 4), AT32(p + 8)}, {AT32(p + 0xC), AT32(p + 0x10), AT32(p + 0x14)}};
      ClipSky(&o, t, AT32(p + 0x18) + EDGE_SIZE, ra2);
    }
    list = SPILL[SPILL_LIST];
    *(u32 *)D_80075798 = CLIP_STACK;
  }
  ChainEnd(&o.ch, PortalSlot());
}

static u32 PortalSkyPlain(u32 a0, u32 a1, u32 a2, u32 a3) {
  (void)a3;
  PortalSky(a0, a1, a2, 0, 0x8004F7E0u, 0x8004FCE0u);
  return 0; /* void at every caller (cyclorama_hand.c) */
}

static u32 PortalSkyFog(u32 a0, u32 a1, u32 a2, u32 a3) {
  (void)a3;
  PortalSky(a0, a1, a2, 1, 0x80050604u, 0x80050B04u);
  return 0;
}

/* ------------------------------------------------------------ a portal's face */

typedef struct {
  u32 prim;   /* s2: the next primitive */
  u32 slot;   /* s3: the OT slot */
  u32 colour; /* t1: the face's colour with POLY_F3's command byte */
  u32 queue;  /* s1: the queue's end */
  CutRegs r;  /* a2, a3: whatever the cuts leave, passed to the next */
} Face;

/* A POLY_F3, linked at the OT slot's tail by itself. */
static void EmitFace(Face *f, u32 xy0, u32 xy1, u32 xy2) {
  const u32 p = MIPS_KUSEG(f->prim);
  u32 tail;
  AT32(p + 0x0) = 0x04000000u; /* length 4, linked below */
  AT32(p + 0x4) = f->colour;
  AT32(p + 0x8) = xy0;
  AT32(p + 0xC) = xy1;
  tail = AT32(f->slot);
  AT32(p + 0x10) = xy2;
  AT32(f->slot) = f->prim;
  if (tail) {
    const u32 t = MIPS_KUSEG(tail);
    AT16(t) = (u16)f->prim;
    AT8(t + 2) = (u8)(f->prim >> 16);
  } else {
    AT32(f->slot + 4) = f->prim;
  }
  f->prim += 0x14;
}

/* The face's half that waits: to the queue's end, with the next edge. */
static void Queue(Face *f, u32 xy0, u32 xy1, u32 xy2, u32 edge) {
  const u32 p = MIPS_KUSEG(f->queue);
  AT32(p + 0x0) = xy0;
  AT32(p + 0x4) = xy1;
  AT32(p + 0x8) = xy2;
  AT32(p + 0xC) = edge;
  f->queue += 0x10;
}

/* func_8004FF90: as ClipSky, for the face (no colours: the cuts' colours,
 * and the colours they're given, are whatever the last cut left). The
 * waiting halves go to the queue with the edge after this one. */
static void ClipFace(Face *f, u32 p0, u32 p1, u32 p2, u32 edge) {
  for (;;) {
    const u32 *e = HPTR(const u32, edge);
    u32 q0, q1, junk;
    if (!e[EDGE_SET])
      break;
    switch (OutsideEdge(e, p0, p1, p2, 1)) {
    case 0x00:
      break;
    case 0x08: /* (q0, p1, p2) goes on, (q0, p2, q1) waits */
      q0 = CutSide(p1, p0, &f->r, edge, &junk);
      q1 = CutSide(p2, p0, &f->r, edge, &junk);
      Queue(f, q0, p2, q1, edge + EDGE_SIZE);
      p0 = q0;
      break;
    case 0x10: /* (p0, q0, p2) goes on, (q0, q1, p2) waits */
      q0 = CutSide(p0, p1, &f->r, edge, &junk);
      q1 = CutSide(p2, p1, &f->r, edge, &junk);
      Queue(f, q0, q1, p2, edge + EDGE_SIZE);
      p1 = q0;
      break;
    case 0x18: /* (q0, q1, p2) */
      q0 = CutSide(p2, p0, &f->r, edge, &junk);
      q1 = CutSide(p2, p1, &f->r, edge, &junk);
      p0 = q0, p1 = q1;
      break;
    case 0x20: /* (p0, p1, q0) goes on, (q0, p1, q1) waits */
      q0 = CutSide(p0, p2, &f->r, edge, &junk);
      q1 = CutSide(p1, p2, &f->r, edge, &junk);
      Queue(f, q0, p1, q1, edge + EDGE_SIZE);
      p2 = q0;
      break;
    case 0x28: /* (q0, p1, q1) */
      q0 = CutSide(p1, p0, &f->r, edge, &junk);
      q1 = CutSide(p1, p2, &f->r, edge, &junk);
      p0 = q0, p2 = q1;
      break;
    case 0x30: /* (p0, q0, q1) */
      q0 = CutSide(p0, p1, &f->r, edge, &junk);
      q1 = CutSide(p0, p2, &f->r, edge, &junk);
      p1 = q0, p2 = q1;
      break;
    default: /* every corner off */
      return;
    }
    edge += EDGE_SIZE;
  }
  EmitFace(f, p0, p1, p2);
}

/* func_8004FEA0: a portal's face, the two triangles at D_8006CB8C in pColor
 * (as a POLY_F3's colour), clipped to the portal's outline, into the OT's
 * portal slot. a2 and a3 come in as the caller left them: the face's first
 * cut takes them as its colours. */
static u32 PortalFace(u32 a0, u32 a1, u32 a2, u32 a3) {
  const u32 *tri = (const u32 *)D_8006CB8C;
  u32 next, k;
  Face f;
  (void)a1;

  hand_spill_regs();
  f.colour = ((*HPTR(const u32, a0) << 8) >> 8) | 0x20000000u;
  f.queue = CLIP_STACK;
  f.prim = *(u32 *)D_800757B0;
  f.slot = PortalSlot();
  f.r.a2 = a2, f.r.a3 = a3;
  for (k = 0; k < 2; k++, tri += 3)
    ClipFace(&f, tri[0], tri[1], tri[2], (u32)(uintptr_t)D_80077EA0);
  for (next = CLIP_STACK; next != f.queue; next += 0x10) {
    const u32 p = MIPS_KUSEG(next);
    ClipFace(&f, AT32(p), AT32(p + 4), AT32(p + 8), AT32(p + 0xC));
  }
  *(u32 *)D_800757B0 = f.prim;
  return 0; /* void at every caller (cyclorama_hand.c) */
}

HAND_UNIT_FN(func_8004F000, FogSky)
HAND_UNIT_FN(func_8004F4BC, PortalSkyPlain)
HAND_UNIT_FN(func_80050240, PortalSkyFog)
HAND_UNIT_FN(func_8004FEA0, PortalFace)

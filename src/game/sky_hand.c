/* The sky (cyclorama) renderer, func_8004EBA8 (asm/renderers/r_cyclorama.s,
 * hand-written asm in the original), written by hand from the asm as C
 * (decisions D72, the D33 method). It replaces the mips2c translation when
 * fast_render=1 (render_hand.h HAND_RENDERER); fast_render=0 runs the
 * translation, which stays linked as func_8004EBA8_gen. Checked against
 * PCSX-Redux by the oracle replay and against the translation by host runs
 * (docs/translation-log.md).
 *
 * The sky is a list of sectors, each a small mesh of vertices packed into
 * one word each, a table of colours and a list of triangles indexing both.
 * Pass 1 keeps the sectors whose bounding sphere is in front of the camera
 * (D_8006FCF4, NULL-terminated); pass 2 transforms each kept sector's
 * vertices, drops a sector whose vertices are all off one side of the
 * screen, and emits a flat or a Gouraud triangle (POLY_F3/POLY_G3) for each
 * of its triangles not wholly off one side, linked into the world OT's
 * last slot. */
#include "render_hand.h"

extern char g_Cyclorama[];  /* Cyclorama: count, sectors, groups count, groups */
extern char D_8006FCF4[];   /* the sectors in front of the camera, NULL-ended */
extern char D_800757B0[];   /* the primitive buffer's next free byte */
extern char D_80075780[];   /* the primitive buffer's end */
extern char D_800758B0[];   /* set when the buffer filled up */
extern char g_WorldOT[];

/* A sky sector (asm offsets). */
enum {
  SEC_CENTER_XY = 0,  /* bounding sphere centre x | y << 16 */
  SEC_CENTER_Z = 1,   /* radius (low half) | centre z << 16 */
  SEC_ORIGIN_XY = 2,  /* vertex origin x | y << 16 */
  SEC_ORIGIN_Z = 3,   /* vertex count (low half) | origin z << 16 */
  SEC_TRIANGLES = 4,  /* triangle list's byte offset << 14 | triangle count */
  SEC_VERTICES = 6,   /* the vertices, then the colours */
};

/* POLY_F3 and POLY_G3 tag lengths, as the asm's OT tag constants (an
 * arena-pointer XOR, MIPS_ARENA_TAG, decisions D25). */
#define TAG_F3 MIPS_ARENA_TAG(0x84000000u)
#define TAG_G3 MIPS_ARENA_TAG(0x86000000u)

/* Clip flags, as the asm builds them below a vertex's screen x/y word
 * shifted left 5: above, below, left of and right of the screen. */
static inline u32 ClipFlags(u32 sxy) {
  u32 flags = sxy << 5;
  if ((s32)(sxy - 0x10000u) <= 0)
    flags += 1;
  if ((s32)(sxy - 0x1000000u) >= 0)
    flags += 2;
  if ((s32)(sxy << 16) <= 0)
    flags += 4;
  if ((s32)((sxy << 16) - 0x2000000u) >= 0)
    flags += 8;
  return flags;
}

/* A packed vertex (z 11 bits, x 11 bits, y 10 bits) relative to the
 * sector's origin, into the GTE's V0. The asm adds x and y << 16 as one
 * word, so a negative x borrows from y's half; so does this. */
static inline void LoadVertex(u32 w, s32 ox, s32 oy, s32 oz) {
  u32 z = (w >> 21) + (u32)oz;
  u32 x = (u32)ox - ((w >> 10) & 0x7FF);
  u32 y = (u32)oy - (w & 0x3FF);
  MTC2(z, 1);           /* VZ0 */
  MTC2(x + (y << 16), 0); /* VXY0 */
}

/* Pass 1: the sectors whose sphere is in front of the camera. */
static void SkyVisibleSectors(s32 group, const u32 *view) {
  const u32 *cyc = (const u32 *)g_Cyclorama;
  const u32 *sectors = HPTR(const u32, cyc[1]);
  u32 *out = (u32 *)D_8006FCF4;
  const u8 *index = 0;
  u32 i = 0;

  CTC2(view[0], 0);
  CTC2(view[1], 1);
  CTC2(view[2], 2);
  CTC2(view[3], 3);
  CTC2(view[4], 4);
  CTC2(0u, 5);
  CTC2(0u, 6);
  CTC2(0u, 7);
  if (group >= 0)
    index = HPTR(const u8, HPTR(const u32, cyc[3])[group]);
  for (;;) {
    u32 sector_addr;
    const u32 *sector;
    s32 radius;
    if (group >= 0) {
      if (*index == 0xFF)
        break;
      sector_addr = sectors[*index++];
    } else {
      if (i == cyc[0])
        break;
      sector_addr = sectors[i++];
    }
    sector = HPTR(const u32, sector_addr);
    MTC2(sector[SEC_CENTER_XY], 0);
    MTC2((u32)((s32)sector[SEC_CENTER_Z] >> 16), 1);
    radius = (s16)sector[SEC_CENTER_Z];
    gte_rtps();
    (void)MFC2(19); /* the asm reads SZ3 first (a read that normalises it) */
    if ((s32)(MFC2(27) - (u32)radius) > 0)
      *out++ = sector_addr;
  }
  *out = 0;
}

static u32 SkyDraw(u32 a0, u32 a1, u32 a2, u32 a3) {
  const s32 group = (s32)a0; /* an occlusion group, or -1 for all sectors */
  const u32 *proj = HPTR(const u32, a2);
  const u32 *visible = (const u32 *)D_8006FCF4;
  u32 *const scratch = (u32 *)SCRATCH_BASE; /* each vertex's clip word */
  const u32 limit = *(u32 *)D_80075780 - 0x400;
  u32 prim, first, prev_tag, tag = 0;
  (void)a3;

  hand_spill_regs();
  SkyVisibleSectors(group, HPTR(const u32, a1));

  CTC2(proj[0], 0);
  CTC2(proj[1], 1);
  CTC2(proj[2], 2);
  CTC2(proj[3], 3);
  CTC2(proj[4], 4);
  prim = *(u32 *)D_800757B0 + 4; /* the first primitive's tag follows a word */
  first = prim;
  prev_tag = prim - 4;

  for (;;) {
    u32 sector_addr = *visible++;
    const u32 *sector, *vert, *colours, *tri, *tri_end;
    s32 ox, oy, oz;
    u32 n, i, all_out = ~0u, packed;
    if (!sector_addr)
      break;
    sector = HPTR(const u32, sector_addr);
    ox = (s16)sector[SEC_ORIGIN_XY];
    oy = (s32)sector[SEC_ORIGIN_XY] >> 16;
    oz = (s32)sector[SEC_ORIGIN_Z] >> 16;
    n = sector[SEC_ORIGIN_Z] & 0xFFFF;
    packed = sector[SEC_TRIANGLES];
    vert = sector + SEC_VERTICES;

    /* Transform the vertices. The asm overlaps each RTPS with loading the
     * next vertex, so the GTE's V0 ends up holding the vertex after the
     * last (vert[n]); this does the same. A sector has at least one. */
    LoadVertex(vert[0], ox, oy, oz);
    i = 0;
    do {
      u32 flags;
      gte_rtps();
      LoadVertex(vert[i + 1], ox, oy, oz);
      flags = ClipFlags(MFC2(14)); /* SXY2 */
      all_out &= flags;
      scratch[i] = flags;
    } while (++i != n);
    if (all_out & 0xF)
      continue; /* every vertex off the same side */

    colours = vert + n;
    tri = (const u32 *)((const u8 *)colours + (packed >> 14));
    tri_end = (const u32 *)((const u8 *)tri + ((packed << 3) & 0xFFF8));
    for (; tri != tri_end; tri += 2) {
      u32 f0, f1, f2, c0, c1, c2, xy0, xy1, xy2;
      if ((s32)(limit - prim) <= 0) {
        *(u32 *)D_800758B0 = 1;
        goto done;
      }
      f0 = WORD_AT(scratch, tri[0] >> 20);
      f1 = WORD_AT(scratch, (tri[0] >> 10) & 0x3FC);
      f2 = WORD_AT(scratch, tri[0] & 0x3FC);
      if ((s32)(f0 & f1 & f2 & 0x1F) > 0)
        continue; /* all three off the same side */
      AT32(prev_tag) = tag ^ prim;
      prev_tag = prim;
      xy0 = (u32)((s32)f0 >> 5);
      xy1 = (u32)((s32)f1 >> 5);
      xy2 = (u32)((s32)f2 >> 5);
      /* the Gouraud layout's positions first; the flat one moves them */
      AT32(prim + 0x8) = xy0;
      AT32(prim + 0x10) = xy1;
      AT32(prim + 0x18) = xy2;
      c0 = tri[1] >> 20;
      c1 = (tri[1] >> 10) & 0x3FC;
      c2 = tri[1] & 0x3FC;
      if (c0 == c1 && c0 == c2) {
        /* one colour: POLY_F3, the colour's code 0x30 made 0x20 */
        tag = TAG_F3;
        AT32(prim + 0x4) = WORD_AT(colours, c0) - 0x10000000u;
        AT32(prim + 0x8) = xy0;
        AT32(prim + 0xC) = xy1;
        AT32(prim + 0x10) = xy2;
        prim += 0x14;
      } else {
        tag = TAG_G3;
        AT32(prim + 0x4) = WORD_AT(colours, c0);
        AT32(prim + 0xC) = WORD_AT(colours, c1);
        AT32(prim + 0x14) = WORD_AT(colours, c2);
        prim += 0x1C;
      }
    }
  }

done:
  *(u32 *)D_800757B0 = prim;
  if (prim != first) {
    /* end the chain and put it in the OT's last slot, after what's there */
    u32 ot = MIPS_KUSEG(*(u32 *)g_WorldOT);
    u32 tail = AT32(ot + 0x3FF8);
    AT32(prev_tag) = tag ^ MIPS_ARENA_TAG(0x80000000u);
    AT32(ot + 0x3FF8) = prev_tag;
    if (tail) {
      u32 t = MIPS_KUSEG(tail);
      AT16(t) = (u16)first;
      AT8(t + 2) = (u8)(first >> 16);
    } else {
      AT32(ot + 0x3FFC) = first;
    }
  }
  return 0; /* v0 is dead at every caller (tools/oracle/ignore_ranges.txt) */
}

HAND_RENDERER(func_8004EBA8, SkyDraw)

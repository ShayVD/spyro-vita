/* The particle renderers (asm/renderers/r_particles.s, hand-written asm in
 * the original), written by hand from the asm as C (docs/native-plan.md
 * stage 2, decisions D95): func_800573C8 draws the particles, func_80058BA8
 * the glows (func_800580F4) and the sparkles (func_800584C4), and
 * func_80058864 the dragon rescue's ring (gamestate 8). They replace the
 * mips2c translation when fast_render is 1 or 2 (render_hand.h
 * HAND_RENDERER_UNIT); fast_render=0 runs the translation, which stays
 * linked as NAME_gen, and the host's --fast-render-check runs it beside
 * every call (docs/translation-log.md). The unit's other entries (the slot
 * allocators func_8005882C, func_80058AE8, func_80058B60, func_80058B68)
 * stay mips2c. The method is D72's, as shadows_hand.c: the asm's control
 * flow with its registers as locals where they carry no other name, its
 * GTE commands on the register file, and what it leaves in RAM, the
 * scratchpad and the GTE exactly.
 *
 * Particles (g_Particles, 0x20 bytes each, ended by a type of -1; a type
 * byte below 0 skips one): by type, a point (LINE_F2 one pixel long, 0), a
 * streak (LINE_G2 between two positions, 1), a rotated or a screen-aligned
 * textured quad (POLY_FT4, 2, 3 and 6 on, from the texture tables
 * D_80076278), a 3D streak (LINE_G2, 4) and a quad turned in the world
 * (POLY_FT4, 5). Byte 3 says it was drawn. A full primitive buffer sets
 * D_800758B0 and ends the list.
 *
 * fast_render=2 with native_draw=1 (particles_hand_ngte.c, decisions D82):
 * every primitive is still built where it was; the polygons go to the walk
 * as native records where the asm links them (Link); the lines (records
 * hold polygons only) are linked as they are. */
#include "render_hand.h"

extern char g_Camera[];
extern char g_Particles[];     /* the particle list's pointer */
extern char g_Glows[];         /* 16 glows of 0x24 bytes */
extern char g_Sparkles[];      /* 8 sparkles of 0x18 bytes */
extern char g_DeltaTime[];
extern char D_8006CBF8[];      /* sine table (cosine at +0x80) */
extern char D_8006CC78[];      /* cosine table */
extern char D_80076248[];      /* the dragon rescue's ring */
extern char D_80076278[];      /* particle texture tables, by subtype */
extern char D_80075780[];      /* the primitive buffer's end */
extern char D_800757B0[];      /* the primitive buffer's next free byte */
extern char D_800758B0[];      /* set when the buffer filled up */
extern char D_80077DD8[];
extern char g_WorldOT[];
extern char g_HudOT[];

#define RTPS() m2c_gte_0x180001()
#define RTPT() m2c_gte_0x280030()
#define GPF0() m2c_gte_0x190003D()   /* sf 0, lm 0 */
#define GPF0LM() m2c_gte_0x198003D() /* sf 0, lm 1 */
#define MVMVA_RT_IR() m2c_gte_0x49E012()

#define S16(a) ((u32)(s32)*(const s16 *)(uintptr_t)(u32)(a))
#define SRA(x, n) ((u32)((s32)(x) >> (n)))
/* a pointer the game keeps in RAM, as an address the code can use */
#define PTR(a) MIPS_KUSEG(a)

/* Into OT slot `slot` (tail +0, head +4): a polygon as a native record
 * when native_draw is on, else as it is. */
static inline void Link(u32 slot, u32 prim, int kind) {
#if NGTE
  if (g_nd.on) {
    nd_append(kind, slot, prim);
    return;
  }
#endif
  (void)kind;
  nd_ot_append(slot, prim);
}

/* The four-way screen test of a quad's corners (the asm's three chains of
 * tests on x, y): some corner below the top, some above the bottom (y
 * 1..255), some right of the left edge, some left of the right (x 1..511) */
static inline int QuadOnScreen(u32 a, u32 b, u32 c, u32 d) {
  const u32 y1 = 0x10000u, y2 = 0x1000000u, x2 = 0x2000000u;
  if (!((s32)(a - y1) > 0 || (s32)(b - y1) > 0 || (s32)(c - y1) > 0 || (s32)(d - y1) > 0))
    return 0;
  if (!((s32)(a - y2) < 0 || (s32)(b - y2) < 0 || (s32)(c - y2) < 0 || (s32)(d - y2) < 0))
    return 0;
  if (!((s32)(a << 16) > 0 || (s32)(b << 16) > 0 || (s32)(c << 16) > 0 || (s32)(d << 16) > 0))
    return 0;
  if (!((s32)((a << 16) - x2) < 0 || (s32)((b << 16) - x2) < 0 || (s32)((c << 16) - x2) < 0 ||
        (s32)((d << 16) - x2) < 0))
    return 0;
  return 1;
}

/* A point's screen test after RTPS (sz the depth, sxy the screen word):
 * depth in (lo, hi), y 1..255 (as above), x 1..511 */
static inline int PointOnScreen(u32 sz, u32 sxy, u32 lo, u32 hi) {
  if ((s32)(sz - lo) <= 0 && lo)
    return 0;
  if ((s32)(sz - hi) >= 0)
    return 0;
  if ((s32)(sxy - 0x10000u) <= 0 || (s32)(sxy - 0x1000000u) >= 0)
    return 0;
  if ((s32)(sxy << 16) <= 0 || (s32)((sxy << 16) - 0x2000000u) >= 0)
    return 0;
  return 1;
}

/* A POLY_FT4's texture words from a texture entry's two (uv0 and its CLUT,
 * uv3 and its tpage): uv1 takes uv3's tpage with uv0's v, uv2 uv0's u
 * with uv3's v */
static inline void QuadUVs(u32 prim, u32 w0, u32 w1) {
  AT32(prim + 0xC) = w0;
  AT32(prim + 0x24) = w1;
  AT32(prim + 0x1C) = (w0 & 0xFFu) | (w1 & 0xFF00u);
  AT32(prim + 0x14) = (w1 ^ (w1 & 0xFF00u)) | (w0 & 0xFF00u);
}

/* Types 2 and 5's turned quad, from MAC1-2 (its half-size turned) on: the
 * four corners round the particle's position (TR, set by the caller), the
 * rotation swapped for a fixed one while RTPT and RTPS run. tex: the
 * subtype's texture table; depth: the bucket before the texture's bias.
 * The asm leaves the swapped rotation in place when the bias puts the quad
 * behind the OT (the next particles see it): so does this. Returns the
 * primitive buffer's next free byte. */
static u32 TurnedQuad(u32 prim, u32 p, u32 tex, u32 depth, u32 ot) {
  u32 a2 = SRA(MFC2(25), 10), a3 = SRA(MFC2(26), 10);
  u32 v0, v1, r0, r1, r2, r3, r4, t3, t4, x0, x1, x2;
  MTC2(0u, 1);
  MTC2(((0u - a2) & 0xFFFFu) + ((0u - a3) << 16), 0);
  MTC2(0u, 3);
  MTC2((a3 & 0xFFFFu) + ((0u - a2) << 16), 2);
  MTC2(0u, 5);
  MTC2(((0u - a3) & 0xFFFFu) + (a2 << 16), 4);
  v0 = a2;
  v1 = a3;
  r0 = CFC2(0);
  r1 = CFC2(1);
  r2 = CFC2(2);
  r3 = CFC2(3);
  r4 = CFC2(4);
  CTC2(0u, 1);
  CTC2(0u, 3);
  CTC2(0x1000u, 0);
  CTC2(0x1000u, 4);
  CTC2(0xA00u, 2);
  v1 <<= 16;
  v0 &= 0xFFFFu;
  RTPT();
  v0 += v1;
  t3 = AT16(p - 0x10); /* the texture: bias << 8 | frame */
  v1 = AT32(p - 0x14); /* the colour */
  t4 = t3 >> 8;
  depth -= t4;
  if ((s32)depth < 0)
    return prim;
  tex += (t3 & 0xFFu) << 3;
  t3 = AT32(tex + 0x4);
  t4 = AT32(tex + 0x8);
  x0 = MFC2(12);
  MTC2(v0, 0);
  MTC2(0u, 1);
  x1 = MFC2(13);
  x2 = MFC2(14);
  RTPS();
  AT32(prim + 0x8) = x0;
  AT32(prim + 0x10) = x1;
  AT32(prim + 0x18) = x2;
  AT32(prim + 0x4) = v1;
  AT32(prim + 0x0) = 0x9000000u; /* POLY_FT4 */
  QuadUVs(prim, t3, t4);
  AT32(prim + 0x20) = MFC2(14);
  CTC2(r0, 0);
  CTC2(r1, 1);
  CTC2(r2, 2);
  CTC2(r3, 3);
  CTC2(r4, 4);
  CTC2(0u, 5);
  CTC2(0u, 6);
  CTC2(0u, 7);
  Link((depth << 3) + ot, prim, ND_K_FT4);
  return prim + 0x28;
}

/* ===================================================================== */
/* func_800573C8: the particles                                           */
/* ===================================================================== */

static u32 ParticlesDraw(u32 a0_, u32 a1_, u32 a2_, u32 a3_) {
  const u32 scr = SCRATCH_BASE, cam = ADDR_ND(g_Camera);
  const u32 sin_tab = ADDR_ND(D_8006CBF8), tex_tab = ADDR_ND(D_80076278);
  u32 cx, cy, cz, lim, p, prim, ot, gp, i;
  (void)a0_, (void)a1_, (void)a2_, (void)a3_;
  /* the register save, into the scratchpad here (as mips2c's units start:
   * s0-s7, gp, fp 0, sp the MIPS stack, ra the sentinel) */
  for (i = 0; i < 8; i++)
    AT32(scr + i * 4) = 0;
  AT32(scr + 0x20) = 0;
  AT32(scr + 0x24) = g_mips_sp;
  AT32(scr + 0x28) = 0;
  AT32(scr + 0x2C) = MIPS_RA_SENTINEL;
#if NGTE
  nd_begin();
#endif
  CTC2(AT32(cam + 0x0), 0);
  CTC2(AT32(cam + 0x4), 1);
  CTC2(AT32(cam + 0x8), 2);
  CTC2(AT32(cam + 0xC), 3);
  CTC2(AT32(cam + 0x10), 4);
  CTC2(0u, 5);
  CTC2(0u, 6);
  CTC2(0u, 7);
  cx = SRA(AT32(cam + 0x28), 2);
  cy = SRA(AT32(cam + 0x2C), 2);
  cz = SRA(AT32(cam + 0x30), 2);
  CTC2(0u, 28);    /* DQB */
  CTC2(0x100u, 27); /* DQA */
  lim = AT32(ADDR_ND(D_80075780)) - 0x400u;
  p = PTR(AT32(ADDR_ND(g_Particles)));
  prim = AT32(ADDR_ND(D_800757B0));
  ot = MIPS_KUSEG(AT32(ADDR_ND(g_WorldOT)));
  gp = S16(p);
  for (;;) {
    const u32 type = SRA(gp, 8);
    u32 at, v0, v1, a0, a1, a2, a3, t0;
    if ((s32)gp < 0) {
      gp = S16(p + 0x20);
      p += 0x20;
      if (type + 1u != 0)
        continue; /* a skipped one */
      break;      /* the end */
    }
    if ((s32)(lim - prim) <= 0) {
      if (type == 4) {
        /* type 4 moves its position into the translation and clears V0
         * before it looks for room */
        const u32 w0 = AT32(p + 0x4), w1 = AT32(p + 0x8);
        CTC2(SRA(w0 << 16, 16), 5);
        CTC2(SRA(w0, 16), 6);
        CTC2(SRA(w1 << 16, 16), 7);
        MTC2(0u, 0);
        MTC2(0u, 1);
      }
      AT32(ADDR_ND(D_800758B0)) = 1;
      break;
    }
    switch (type) {
    case 0: { /* a point: LINE_F2 one pixel long */
      u32 depth_bias, sz, sxy;
      at = AT32(p + 0x4);
      a0 = AT32(p + 0x8);
      v0 = (cy - (at >> 16)) + ((cz - (a0 & 0xFFFFu)) << 16);
      MTC2(v0, 0);
      MTC2((at & 0xFFFFu) - cx, 1);
      depth_bias = (a0 >> 16) & 0xFFu;
      RTPS();
      gp = S16(p + 0x20);
      AT32(prim + 0x4) = AT32(p + 0xC); /* colour */
      AT8(p + 0x3) = 0;
      p += 0x20;
      sz = MFC2(19);
      sxy = MFC2(14);
      if (sz == 0 || !PointOnScreen(sz, sxy, 0, 0x2000u))
        continue;
      AT8(p - 0x1D) = 1;
      AT32(prim + 0x8) = sxy;
      AT32(prim + 0xC) = sxy + 1u;
      AT32(prim + 0x0) = 0x3000000u;
      v1 = (sz >> 5) - depth_bias;
      if ((s32)(v1 - 2u) <= 0)
        continue;
      nd_ot_append((v1 << 3) + ot, prim);
      prim += 0x10;
      continue;
    }
    case 1: { /* a streak: LINE_G2 between two positions */
      u32 sz, sxy, sxy2, w;
      at = AT32(p + 0x4);
      a0 = AT32(p + 0x8);
      v0 = (cy - (at >> 16)) + ((cz - (a0 & 0xFFFFu)) << 16);
      MTC2(v0, 0);
      MTC2((at & 0xFFFFu) - cx, 1);
      at = (a0 >> 16) - cx;
      RTPS();
      w = AT32(p + 0xC);
      gp = S16(p + 0x20);
      p += 0x20;
      v0 = (cy - (w & 0xFFFFu)) + ((cz - (w >> 16)) << 16);
      AT8(p - 0x1D) = 0;
      MTC2(v0, 0);
      MTC2(at, 1);
      sz = MFC2(19);
      sxy = MFC2(14);
      RTPS();
      a0 = AT32(p - 0x10);
      if (sz == 0 || !PointOnScreen(sz, sxy, 0, 0x2000u))
        continue;
      a1 = AT32(p - 0xC);
      sxy2 = MFC2(14);
      AT32(prim + 0x8) = sxy;
      AT32(prim + 0x10) = sxy2;
      AT32(prim + 0x4) = a0;
      AT32(prim + 0xC) = a1;
      v0 = (sz >> 5) - (a1 >> 24);
      if ((s32)(v0 - 2u) <= 0)
        continue;
      AT32(prim + 0x0) = 0x4000000u;
      AT8(p - 0x1D) = 1;
      nd_ot_append((v0 << 3) + ot, prim);
      prim += 0x14;
      continue;
    }
    case 2: { /* a quad turned by its angle, at its position */
      u32 tex, sz, sxy, sn, cs;
      at = AT32(p + 0x4);
      a0 = AT32(p + 0x8);
      MTC2((at & 0xFFFFu) - cx, 1);
      v0 = (cy - (at >> 16)) + ((cz - (a0 & 0xFFFFu)) << 16);
      AT8(p + 0x3) = 0;
      MTC2(v0, 0);
      at = (gp & 0xFFu) << 2;
      RTPS();
      tex = PTR(AT32(at + tex_tab));
      v0 = (a0 >> 16) & 0xFFu;
      a0 = ((a0 >> 23) + 0x40u) & 0x1FEu;
      gp = S16(p + 0x20);
      p += 0x20;
      sz = MFC2(19);
      sxy = MFC2(14);
      sn = S16(a0 + sin_tab);
      if ((s32)(sz - 0x80u) < 0 || (s32)(sz - 0x2000u) >= 0)
        continue;
      if ((s32)(sxy - 0x10000u) <= 0 || (s32)(sxy - 0x1000000u) >= 0)
        continue;
      cs = S16(a0 + sin_tab + 0x80);
      if ((s32)(sxy << 16) <= 0 || (s32)((sxy << 16) - 0x2000000u) >= 0)
        continue;
      MTC2(v0, 8);
      MTC2(sn, 9);
      MTC2(cs, 10);
      {
        const u32 m1 = MFC2(25), m2 = MFC2(26), m3 = MFC2(27);
        GPF0();
        AT8(p - 0x1D) = 1;
        CTC2(m1 << 2, 5);
        CTC2(m2 << 2, 6);
        CTC2(m3 << 2, 7);
      }
      prim = TurnedQuad(prim, p, tex, sz >> 5, ot);
      continue;
    }
    case 3: { /* a quad facing the screen, sized by its depth */
      u32 tex, sz, sxy, t, x0, x1, half_u, half_v;
      at = AT32(p + 0x4);
      a0 = AT32(p + 0x8);
      MTC2((at & 0xFFFFu) - cx, 1);
      v0 = (cy - (at >> 16)) + ((cz - (a0 & 0xFFFFu)) << 16);
      MTC2(v0, 0);
      AT8(p + 0x3) = 0;
      at = gp & 0xFFu;
      RTPS();
      tex = PTR(AT32((at << 2) + tex_tab));
      MTC2((a0 >> 16) & 0xFFu, 9);
      MTC2(a0 >> 24, 10);
      gp = S16(p + 0x20);
      p += 0x20;
      sz = MFC2(19);
      sxy = MFC2(14);
      if ((s32)(sz - 0x80u) < 0 || (s32)(sz - 0x2000u) >= 0)
        continue;
      if ((s32)(sxy - 0x10000u) <= 0 || (s32)(sxy - 0x1000000u) >= 0)
        continue;
      t = AT16(p - 0x10); /* the texture: bias << 8 | frame */
      if ((s32)(sxy << 16) <= 0 || (s32)((sxy << 16) - 0x2000000u) >= 0)
        continue;
      AT8(p - 0x1D) = 1;
      MTC2(MFC2(24) >> 12, 8);
      v1 = (sz >> 5) - (t >> 8);
      if ((s32)v1 < 0)
        continue;
      GPF0();
      tex += (t & 0xFFu) << 3;
      x0 = AT32(tex + 0x4);
      x1 = AT32(tex + 0x8);
      a2 = MFC2(25) >> 12;
      a3 = MFC2(26) >> 12;
      half_u = a2 >> 1;
      half_v = (a3 >> 1) << 16;
      a3 <<= 16;
      v0 = sxy + half_u + half_v;
      AT32(prim + 0x4) = AT32(p - 0x14);
      AT32(prim + 0x20) = v0;
      AT32(prim + 0x18) = v0 - a2;
      AT32(prim + 0x10) = v0 - a3;
      AT32(prim + 0x8) = v0 - a3 - a2;
      QuadUVs(prim, x0, x1);
      AT32(prim + 0x0) = 0x9000000u; /* POLY_FT4 */
      Link((v1 << 3) + ot, prim, ND_K_FT4);
      prim += 0x28;
      continue;
    }
    case 4: { /* a 3D streak: LINE_G2 from the position along its vector */
      u32 sz, sxy, sxy2, w;
      at = AT32(p + 0x4);
      a0 = AT32(p + 0x8);
      CTC2(SRA(at << 16, 16), 5);
      CTC2(SRA(at, 16), 6);
      CTC2(SRA(a0 << 16, 16), 7);
      MTC2(0u, 0);
      MTC2(0u, 1);
      RTPS();
      w = AT32(p + 0xC);
      gp = S16(p + 0x20);
      p += 0x20;
      AT8(p - 0x1D) = 0;
      CTC2(SRA(a0, 16), 5);
      CTC2(SRA(w << 16, 16), 6);
      CTC2(SRA(w, 16), 7);
      sz = MFC2(19);
      sxy = MFC2(14);
      RTPS();
      a0 = AT32(p - 0x10);
      a1 = AT32(p - 0xC);
      CTC2(0u, 5);
      CTC2(0u, 6);
      CTC2(0u, 7);
      if (!PointOnScreen(sz, sxy, 0x40u, 0x800u))
        continue;
      AT8(p - 0x1D) = 1;
      sxy2 = MFC2(14);
      AT32(prim + 0x8) = sxy;
      AT32(prim + 0x10) = sxy2;
      AT32(prim + 0x4) = a0;
      AT32(prim + 0xC) = a1;
      v0 = (sz >> 5) - (a1 >> 24);
      if ((s32)v0 < 0)
        v0 = 0;
      AT32(prim + 0x0) = 0x4000000u;
      nd_ot_append((v0 << 3) + ot, prim);
      prim += 0x14;
      continue;
    }
    case 5: { /* a quad turned in the world (its own matrix) */
      u32 tex, sn, cs, s_v0, s_v1, s_a0;
      a1 = AT32(p + 0x4);
      a2 = AT32(p + 0x8);
      tex = PTR(AT32(((gp & 0xFFu) << 2) + tex_tab));
      v0 = (a2 >> 16) & 0xFFu;
      a0 = ((a2 >> 23) + 0x40u) & 0x1FEu;
      gp = S16(p + 0x20);
      p += 0x20;
      sn = S16(a0 + sin_tab);
      cs = S16(a0 + sin_tab + 0x80);
      MTC2(v0, 8);
      MTC2(sn, 9);
      MTC2(cs, 10);
      s_v0 = SRA(a1 << 16, 14);
      GPF0();
      s_v1 = SRA(a1, 14);
      s_a0 = SRA(a2 << 16, 14);
      CTC2(s_v0, 5);
      CTC2(s_v1, 6);
      CTC2(s_a0, 7);
      prim = TurnedQuad(prim, p, tex, s_a0 >> 7, ot);
      continue;
    }
    default: { /* 6 on: a quad turned by its angle, its size from the texture */
      u32 tex, sn, cs, t, w0, x0, x1, x3, z0, z1, z2, z3, depth;
      at = AT32(p + 0x4);
      a0 = AT32(p + 0x8);
      v0 = at >> 16;
      at = (at & 0xFFFFu) - cx;
      AT8(p + 0x3) = 0;
      v1 = a0 & 0xFFFFu;
      sn = S16(((a0 >> 23) & 0x1FEu) + sin_tab);
      cs = S16(((a0 >> 23) & 0x1FEu) + sin_tab + 0x80);
      a0 = (a0 >> 16) & 0xFFu;
      v0 = cy - v0;
      v1 = (cz - v1) << 16;
      MTC2(a0, 8);
      MTC2(sn, 9);
      MTC2(cs, 10);
      tex = ((gp & 0xFFu) << 2) + tex_tab;
      GPF0();
      tex = PTR(AT32(tex));
      a1 = SRA(MFC2(25), 12);
      a0 = SRA(MFC2(26), 12);
      gp = S16(p + 0x20);
      p += 0x20;
      MTC2(v0 - a1 + v1, 0);
      MTC2(at + a0, 1);
      MTC2(v0 + a0 + v1, 2);
      MTC2(at + a1, 3);
      MTC2(v0 - a0 + v1, 4);
      MTC2(at - a1, 5);
      a3 = at - a0;
      t0 = v0 + a1;
      RTPT();
      t0 += v1;
      t = AT16(p - 0x10);
      w0 = AT32(p - 0x14); /* the colour */
      {
        const u32 e = ((t & 0xFFu) << 3) + tex;
        x0 = AT32(e + 0x4); /* (the texture words, here) */
        x1 = AT32(e + 0x8);
      }
      {
        const u32 s0 = MFC2(12), s1 = MFC2(13), s2 = MFC2(14);
        MTC2(t0, 0);
        MTC2(a3, 1);
        z0 = MFC2(17);
        z1 = MFC2(18);
        z2 = MFC2(19);
        RTPS();
        AT32(prim + 0x4) = w0;
        AT32(prim + 0x0) = 0x9000000u; /* POLY_FT4 */
        QuadUVs(prim, x0, x1);
        x3 = MFC2(14);
        z3 = MFC2(19);
        depth = z0 + z1 + z2 + z3;
        if ((s32)(depth - 0x200u) < 0 || (s32)(depth - 0x8000u) >= 0)
          continue;
        depth = (depth >> 7) - (t >> 8);
        if ((s32)depth < 0)
          continue;
        AT32(prim + 0x8) = s0;
        AT32(prim + 0x10) = s1;
        AT32(prim + 0x18) = s2;
        AT32(prim + 0x20) = x3;
        if (!QuadOnScreen(s0, s1, s2, x3))
          continue;
        AT8(p - 0x1D) = 1;
        Link((depth << 3) + ot, prim, ND_K_FT4);
        prim += 0x28;
      }
      continue;
    }
    }
  }
  AT32(ADDR_ND(D_800757B0)) = prim;
#if NGTE
  nd_end();
#endif
  return 0;
}

/* ===================================================================== */
/* func_80058BA8: the glows (func_800580F4), then the sparkles            */
/* (func_800584C4)                                                        */
/* ===================================================================== */

/* The glows: for each of 16 with a vertex count, its centre projected
 * (scaled down by a power of two far away), a draw mode packet (additive,
 * tpage 0x220), then a fan of Gouraud triangles (G3: the glow's colour at
 * the centre, black at the rim) round it, each kept unless all three
 * corners are off one side of the screen, into the bucket of its depth. */
static void Glows(void) {
  const u32 cam = ADDR_ND(g_Camera), ot = MIPS_KUSEG(AT32(ADDR_ND(g_WorldOT)));
  u32 cx, cy, cz, g, n, prim = AT32(ADDR_ND(D_800757B0)), hi = 0, lo = 0;
  {
    /* the register save as the unit makes it here, called from
     * func_80058BA8: s0-s7, gp, fp 0, sp the MIPS stack less its frame,
     * ra the call's return address in the unit */
    u32 *spill = (u32 *)D_80077DD8, i;
    for (i = 0; i < 8; i++)
      spill[i] = 0;
    spill[8] = 0;
    spill[9] = g_mips_sp - 0x18u;
    spill[10] = 0;
    spill[11] = 0x80058BB8u;
  }
  CTC2(AT32(cam + 0x0), 0);
  CTC2(AT32(cam + 0x4), 1);
  CTC2(AT32(cam + 0x8), 2);
  CTC2(AT32(cam + 0xC), 3);
  CTC2(AT32(cam + 0x10), 4);
  CTC2(0u, 5);
  CTC2(0u, 6);
  CTC2(0u, 7);
  cx = AT32(cam + 0x28);
  cy = AT32(cam + 0x2C);
  cz = AT32(cam + 0x30);
  g = ADDR_ND(g_Glows);
  for (n = 16; (s32)n > 0; n--, g += 0x24) {
    u32 verts = AT32(g + 0x0), pos, dx, dy, dz, sum, shift = 0, centre, sz, slot, f0, vl, colour;
    s32 sx, sy;
    if (verts == 0)
      continue;
    pos = PTR(AT32(g + 0x8));
    dx = AT32(pos + 0x0) + AT32(g + 0x14) - cx;
    dy = cy - (AT32(pos + 0x4) + AT32(g + 0x18));
    dz = cz - (AT32(pos + 0x8) + AT32(g + 0x1C));
    sum = ((s32)dx >= 0 ? dx : 0u - dx) + ((s32)dy >= 0 ? dy : 0u - dy);
    sum += (s32)dz >= 0 ? dz : 0u - dz;
    if ((s32)sum > 0) {
      shift = sum >> 13;
      if ((s32)(shift - 4u) > 0)
        shift = 4;
      dx = SRA(dx, shift & 31);
      dy = SRA(dy, shift & 31);
      dz = SRA(dz, shift & 31);
    }
    MTC2(dx, 1);
    MTC2((dy & 0xFFFFu) | (dz << 16), 0);
    RTPS();
    AT32(prim + 0x0) = 0x2000000u;
    AT32(prim + 0x4) = 0xE1000220u; /* draw mode: additive, tpage 0x220 */
    AT32(prim + 0x8) = 0u;
    centre = MFC2(14);
    sz = MFC2(27);
    sy = (s32)centre >> 16;
    sx = (s32)(centre << 16) >> 16;
    sz <<= shift & 31;
    MIPS_DIV(AT32(g + 0x10) << 12, sz); /* the glow's size at that depth */
    sz = (sz >> 7) + AT32(g + 0x20);
    if ((s32)sz <= 0)
      continue;
    if ((s32)(sz - 0xFFu) > 0)
      sz += 0x40u;
    if ((s32)(sz - 0x7FFu) > 0)
      sz = 0x7FFu;
    slot = (sz << 3) + ot;
    /* the centre's clip flags: above, below, right, left */
    f0 = 0;
    if ((s32)((u32)sy - 1u) <= 0)
      f0 |= 1;
    if ((s32)((u32)sy - 0x100u) >= 0)
      f0 |= 2;
    if ((s32)((u32)sx - 0x200u) >= 0)
      f0 |= 4;
    if (sx <= 0)
      f0 |= 8;
    MTC2(lo, 8);
    Link(slot, prim, ND_K_E1);
    prim += 0xC;
    vl = PTR(AT32(g + 0x4));
    MTC2(AT32(vl + 0x0), 9);
    MTC2(AT32(vl + 0x4), 10);
    vl += 8;
    verts--;
    GPF0LM();
    colour = ((AT32(g + 0xC) << 8) >> 8) | 0x32000000u; /* G3 */
    {
      u32 rx = MFC2(25) + (u32)sx, ry = MFC2(26) + (u32)sy, f1 = 0, rim;
      if ((s32)(ry - 1u) <= 0)
        f1 |= 1;
      if ((s32)(ry - 0x100u) >= 0)
        f1 |= 2;
      if ((s32)(rx - 0x200u) >= 0)
        f1 |= 4;
      if ((s32)rx <= 0)
        f1 |= 8;
      rim = (rx & 0xFFFFu) | (ry << 16);
      do {
        u32 nx, ny, f2, rim2;
        MTC2(AT32(vl + 0x0), 9);
        MTC2(AT32(vl + 0x4), 10);
        AT32(prim + 0x0) = 0x6000000u;
        GPF0LM();
        AT32(prim + 0x8) = centre;
        AT32(prim + 0x4) = colour;
        AT32(prim + 0x10) = rim;
        AT32(prim + 0xC) = 0u;
        AT32(prim + 0x14) = 0u;
        nx = MFC2(25) + (u32)sx;
        ny = MFC2(26) + (u32)sy;
        f2 = 0;
        if ((s32)(ny - 1u) <= 0)
          f2 |= 1;
        if ((s32)(ny - 0x100u) >= 0)
          f2 |= 2;
        if ((s32)(nx - 0x200u) >= 0)
          f2 |= 4;
        if ((s32)nx <= 0)
          f2 |= 8;
        rim2 = (nx & 0xFFFFu) | (ny << 16);
        AT32(prim + 0x18) = rim2;
        if (!((s32)(f0 & f1 & f2) > 0)) {
          Link(slot, prim, ND_K_G3);
          prim += 0x1C;
        }
        verts--;
        vl += 8;
        rim = rim2;
        f1 = f2;
      } while ((s32)verts > 0);
    }
  }
  (void)hi;
  AT32(ADDR_ND(D_800757B0)) = prim;
}

/* The sparkles: 8, each living `dt` vblanks shorter per call; a live one
 * on screen is a cross of two lines (LINE_F2) round its position, turned
 * by its spin and sized by its age, into the bucket of its depth. The
 * camera's rotation is kept in the light matrix while the cross's own
 * rotation is in use. */
static void Sparkles(u32 dt) {
  const u32 cam = ADDR_ND(g_Camera), sin_tab = ADDR_ND(D_8006CBF8);
  const u32 ot = MIPS_KUSEG(AT32(ADDR_ND(g_WorldOT)));
  u32 prim = AT32(ADDR_ND(D_800757B0)), cx, cy, cz, s, end, hi = 0, lo = 0;
  {
    const u32 r0 = AT32(cam + 0x0), r1 = AT32(cam + 0x4), r2 = AT32(cam + 0x8);
    const u32 r3 = AT32(cam + 0xC), r4 = AT32(cam + 0x10);
    CTC2(r0, 0);
    CTC2(r1, 1);
    CTC2(r2, 2);
    CTC2(r3, 3);
    CTC2(r4, 4);
    CTC2(r0, 8);
    CTC2(r1, 9);
    CTC2(r2, 10);
    CTC2(r3, 11);
    CTC2(r4, 12);
  }
  CTC2(0u, 5);
  CTC2(0u, 6);
  CTC2(0u, 7);
  CTC2(0u, 28);
  CTC2(0x100u, 27);
  cx = AT32(cam + 0x28);
  cy = AT32(cam + 0x2C);
  cz = AT32(cam + 0x30);
  s = ADDR_ND(g_Sparkles) - 0x18u;
  end = s + 0xC0u;
  for (;;) {
    u32 w, life, spin, len, age, ang, sz, sxy, col, size, x0, x1, x2, slot;
    u32 m1, m2, m3, scale;
    if (s == end)
      break;
    s += 0x18;
    w = AT32(s + 0xC); /* life | full life << 8 | angle << 16 | spin << 24 */
    life = (w & 0xFFu) - dt;
    spin = SRA(w, 24);
    MIPS_MULT(spin, dt);
    if ((s32)life <= 0) {
      AT8(s + 0xC) = 0;
      continue;
    }
    AT8(s + 0xC) = (u8)life;
    len = (w >> 8) & 0xFFu;
    age = (len - life) << 8;
    if ((s32)(len - life) < 0)
      age = 0u - age;
    ang = (((w >> 16) & 0xFFu) + lo) & 0xFFu;
    AT8(s + 0xE) = (u8)ang;
    MIPS_DIV(age, len);
    {
      const u32 dx = SRA(AT32(s + 0x0) - cx, 2);
      const u32 dy = SRA(cy - AT32(s + 0x4), 2);
      const u32 dz = SRA(cz - AT32(s + 0x8), 2);
      MTC2((dy & 0xFFFFu) | (dz << 16), 0);
      MTC2(dx, 1);
    }
    ang = (ang << 1) + sin_tab;
    RTPS();
    size = AT32(s + 0x14); /* size (low byte), the far limit (bits 8-15) */
    col = AT32(s + 0x10);
    sz = MFC2(19);
    sxy = MFC2(14);
    if ((s32)((size & 0xFF00u) - sz) <= 0 || (s32)(sz - 0x80u) <= 0 ||
        (s32)(sxy - 0x10000u) <= 0 || (s32)(sxy - 0x1000000u) >= 0 || (s32)(sxy << 16) <= 0 ||
        (s32)((sxy << 16) - 0x2000000u) >= 0) {
      AT8(s + 0xC) = 0;
      continue;
    }
    col |= 0x40000000u; /* LINE_F2 */
    AT32(prim + 0x4) = col;
    AT32(prim + 0x14) = col;
    scale = 0x100u - lo;
    MIPS_MULT(scale, size & 0xFFu);
    {
      const u32 sn = S16(ang), cs = S16(ang + 0x80);
      MTC2(lo, 8);
      MTC2(sn, 10);
      MTC2(cs, 9);
    }
    m1 = MFC2(25);
    m2 = MFC2(26);
    m3 = MFC2(27);
    GPF0();
    {
      u32 r = (s32)(m3 - 0x1000u) < 0 ? 0x1000u : m3, ax, ay;
      CTC2(m1 << 1, 5);
      CTC2(m2 << 1, 6);
      CTC2(m3 << 1, 7);
      ax = SRA(MFC2(25), 19);
      ay = SRA(MFC2(26), 19);
      MTC2(0u, 1);
      MTC2(((0u - ax) & 0xFFFFu) + ((0u - ay) << 16), 0);
      MTC2(0u, 3);
      MTC2((ay & 0xFFFFu) + ((0u - ax) << 16), 2);
      MTC2(0u, 5);
      MTC2(((0u - ay) & 0xFFFFu) + (ax << 16), 4);
      CTC2(0u, 1);
      CTC2(0u, 3);
      CTC2(r, 0);
      CTC2(r, 2);
      CTC2(r, 4);
      RTPT();
      x0 = MFC2(12);
      MTC2((ay << 16) + (ax & 0xFFFFu), 0);
      MTC2(0u, 1);
      x1 = MFC2(13);
      x2 = MFC2(14);
      RTPS();
    }
    AT32(prim + 0x8) = x0;
    AT32(prim + 0x18) = x1;
    AT32(prim + 0x1C) = x2;
    {
      u32 d = (sz >> 5) - 6u;
      if ((s32)d < 0)
        d = 0;
      AT32(prim + 0x0) = 0x3000000u;
      if ((s32)(d - 0x100u) > 0)
        d += 0x46u;
      AT32(prim + 0x10) = MIPS_ARENA_TAG(0x83000000u) ^ prim; /* the second line, then the first */
      slot = (d << 3) + ot;
    }
    AT32(prim + 0xC) = MFC2(14);
    {
      const u32 l0 = CFC2(8), l1 = CFC2(9), l2 = CFC2(10), l3 = CFC2(11), l4 = CFC2(12);
      CTC2(l0, 0);
      CTC2(l1, 1);
      CTC2(l2, 2);
      CTC2(l3, 3);
      CTC2(l4, 4);
    }
    CTC2(0u, 5);
    CTC2(0u, 6);
    CTC2(0u, 7);
    {
      const u32 tail = AT32(slot), second = prim + 0x10;
      AT32(slot) = prim;
      if (tail) {
        AT16(PTR(tail) + 0x0) = (u16)second;
        AT8(PTR(tail) + 0x2) = (u8)(second >> 16);
      } else {
        AT32(slot + 0x4) = second;
      }
    }
    prim += 0x20;
  }
  (void)hi;
  AT32(ADDR_ND(D_800757B0)) = prim;
}

static u32 GlowsAndSparkles(u32 a0_, u32 a1_, u32 a2_, u32 a3_) {
  (void)a0_, (void)a1_, (void)a2_, (void)a3_;
  /* the asm's frame: ra (the caller's, the sentinel from C) at sp - 0x8 */
  AT32(g_mips_sp - 0x8u) = MIPS_RA_SENTINEL;
#if NGTE
  nd_begin();
#endif
  Glows();
  Sparkles(AT32(ADDR_ND(g_DeltaTime)));
#if NGTE
  nd_end();
#endif
  return 0;
}

/* ===================================================================== */
/* func_80058864: the dragon rescue's ring (gamestate 8)                  */
/* ===================================================================== */

/* Two rings of 8 points round the ring's centre (its own matrix and
 * position, D_80076248): an inner one at radius / 4096, an outer one at
 * radius / 1024, half a step turned; between them, and from the inner one
 * to the centre, flat semi-transparent triangles (F3, additive), into the
 * HUD OT behind a draw mode packet. */
static u32 DragonRing(u32 a0_, u32 a1_, u32 a2_, u32 a3_) {
  const u32 cam = ADDR_ND(g_Camera), d = ADDR_ND(D_80076248), scr = SCRATCH_BASE;
  const u32 cos_tab = ADDR_ND(D_8006CC78), sin_tab = ADDR_ND(D_8006CBF8);
  u32 centre, ang, o, radius, prim, hud, shade, in, out;
  (void)a0_, (void)a1_, (void)a2_, (void)a3_;
#if NGTE
  nd_begin();
#endif
  CTC2(AT32(cam + 0x0), 0);
  CTC2(AT32(cam + 0x4), 1);
  CTC2(AT32(cam + 0x8), 2);
  CTC2(AT32(cam + 0xC), 3);
  CTC2(AT32(cam + 0x10), 4);
  MTC2(AT32(d + 0x4) - AT32(cam + 0x28), 11);
  MTC2(AT32(cam + 0x2C) - AT32(d + 0x8), 9);
  MTC2(AT32(cam + 0x30) - AT32(d + 0xC), 10);
  MVMVA_RT_IR();
  {
    const u32 m1 = MFC2(25), m2 = MFC2(26), m3 = MFC2(27);
    CTC2(m1, 5);
    CTC2(m2, 6);
    CTC2(m3, 7);
  }
  CTC2(AT32(d + 0x10), 0);
  CTC2(AT32(d + 0x14), 1);
  CTC2(AT32(d + 0x18), 2);
  CTC2(AT32(d + 0x1C), 3);
  CTC2(AT32(d + 0x20), 4);
  MTC2(0u, 0);
  MTC2(0u, 1);
  RTPS();
  centre = MFC2(14);
  radius = AT32(d + 0x24);
  /* the inner ring: scratch +0x04..+0x20, the last again at +0x00 */
  o = scr;
  for (ang = 0x1C0u;;) {
    const u32 c = S16(cos_tab + ang), sv = S16(sin_tab + ang);
    MTC2(SRA(c * radius, 12), 1);
    MTC2(SRA(sv * radius, 12), 0);
    ang -= 0x40u;
    o += 4;
    RTPS();
    AT32(o) = MFC2(14);
    if ((s32)ang < 0)
      break;
  }
  AT32(o - 0x20) = AT32(o);
  /* the outer ring: scratch +0x24..+0x40 */
  o += 4;
  for (ang = 0x1E0u;;) {
    const u32 c = S16(cos_tab + ang), sv = S16(sin_tab + ang);
    MTC2(SRA(c * radius, 10), 1);
    MTC2(SRA(sv * radius, 10), 0);
    ang -= 0x40u;
    o += 4;
    RTPS();
    AT32(o - 4) = MFC2(14);
    if ((s32)ang < 0)
      break;
  }
  prim = AT32(ADDR_ND(D_800757B0));
  hud = MIPS_KUSEG(AT32(ADDR_ND(g_HudOT)));
  AT32(prim + 0x8) = 0u;
  AT32(prim + 0x4) = 0xE1000620u; /* draw mode: additive, tpage 0x620 */
  AT32(prim + 0x0) = 0x2000000u;
  Link(hud, prim, ND_K_E1);
  prim += 0xC;
  shade = AT8(d + 0x2B);
  for (in = scr, out = scr + 0x24; out != scr + 0x44; in += 4, out += 4) {
    const u32 a = AT32(in + 0x0), b = AT32(in + 0x4), c = AT32(out + 0x0);
    AT32(prim + 0x8) = a;
    AT32(prim + 0xC) = b;
    AT32(prim + 0x10) = c;
    AT32(prim + 0x1C) = a;
    AT32(prim + 0x20) = b;
    AT32(prim + 0x24) = centre;
    AT8(prim + 0x4) = (u8)shade;
    AT8(prim + 0x5) = (u8)shade;
    AT8(prim + 0x6) = (u8)shade;
    AT8(prim + 0x18) = (u8)shade;
    AT8(prim + 0x19) = (u8)shade;
    AT8(prim + 0x1A) = (u8)shade;
    AT8(prim + 0x7) = 0x22u; /* F3, semi-transparent */
    AT8(prim + 0x1B) = 0x22u;
    AT32(prim + 0x0) = 0x4000000u;
    AT32(prim + 0x14) = 0x4000000u;
    Link(hud, prim, ND_K_F3);
    prim += 0x14;
    Link(hud, prim, ND_K_F3);
    prim += 0x14;
  }
  AT32(ADDR_ND(D_800757B0)) = prim;
#if NGTE
  nd_end();
#endif
  return 0;
}

HAND_RENDERER_UNIT(func_800573C8, ParticlesDraw)
HAND_RENDERER_UNIT(func_80058BA8, GlowsAndSparkles)
HAND_RENDERER_UNIT(func_80058864, DragonRing)

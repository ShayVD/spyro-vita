/* Spyro's flame, func_80058D64 (asm/renderers/r_flame.s, hand-written asm
 * in the original), written by hand from the asm as C (docs/native-plan.md
 * stage 2, decisions D95). It replaces the mips2c translation when
 * fast_render is 1 or 2 (render_hand.h HAND_RENDERER_UNIT); fast_render=0
 * runs the translation, which stays linked as func_80058D64_gen, and the
 * host's --fast-render-check runs it beside every call
 * (docs/translation-log.md). The method is D72's, as shadows_hand.c.
 *
 * The flame (g_SpyroFlame) is up to 8 segments, each a frame of a ring
 * mesh (D_8006DAA8, from the frame table D_8006D94C). For a segment whose
 * ring is in range, a head of four Gouraud triangles (G3, colours from
 * D_8006E1A8) round its tip, each kept if it faces the camera; then a
 * strip of semi-transparent Gouraud-textured quads (GT4) down the ring,
 * each quad's upper edge the next one's lower edge, fading as it goes. All
 * into the world OT bucket of their depth.
 *
 * fast_render=2 (flame_hand_ngte.c, NGTE 1; decisions D98): the GTE
 * commands as native maths on C values (gte_native.h). The control
 * registers are moved as the asm moves them. The data registers' results
 * stay in locals, with V0-V2 as the code last moved them, and at the end
 * the register file is made what the asm leaves (FlameTail): the last two
 * projections (RTPS or RTPT) run again on the register file. The last is
 * always an RTPT, which rewrites SXY0-2, SZ1-3, MAC, IR, the depth cue and
 * FLAG; SZ0 is the one before's last depth. The head's NCLIPs write only
 * what that RTPT rewrites; the reads that zero a register's top half (SZ1-3)
 * are done again once.
 *
 * fast_render=3 (flame_hand_rc.c, NGTE_RC, D124): the same native maths
 * with the control registers moved as the asm moves them and nothing else:
 * no IR moves, no FlameTail.
 *
 * With native_draw=1 too (decisions D82): each primitive is still built
 * where it was, and where the asm links it it goes to the walk as a
 * native record (nd_append). */
#include "render_hand.h"

extern char g_SpyroFlame[]; /* position (+0x00), per segment: frame (+0x20), first
                               ring point (+0x28); big flame (+0x9C); the flame's
                               rotation (+0xB8); its uv words (+0x10, +0x14, or
                               +0x18, +0x1C when big) */
extern char g_Camera[];
extern char D_8006CBF8[];   /* sine table (cosine at +0x80) */
extern char D_8006D94C[];   /* the frames: a word each (ring points, fade) */
extern char D_8006DAA8[];   /* the ring meshes */
extern char D_8006E1A8[];   /* the head's four colours (+0x10 when big) */
extern char D_800757B0[];
extern char g_WorldOT[];

#define RTPS() m2c_gte_0x180001()
#define RTPT() m2c_gte_0x280030()
#define NCLIP() m2c_gte_0x1400006()
#define MVMVA_RT_IR() m2c_gte_0x49E012() /* sf 1: the rotation times IR1-3 */

#define S16(a) ((u32)(s32)*(const s16 *)(uintptr_t)(u32)(a))
#define SEXT16(x) ((u32)((s32)((x) << 16) >> 16))

/* Link the primitive at prim into world OT slot `slot` (the asm's OtAppend:
 * the bucket's tail at +0, head at +4), or as a native record. */
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

/* A head triangle: the three screen words into SXY0-2 and the primitive,
 * its three colours, then NCLIP; linked if it faces the camera. */
static inline u32 HeadTriangle(u32 prim, u32 slot, u32 xy0, u32 xy1, u32 xy2, u32 c0, u32 c1,
                               u32 c2) {
#if NGTE_MATH
  const s32 mac0 = ngte_nclip(xy0, xy1, xy2); /* the strip's RTPT rewrites what NCLIP writes */
  AT32(prim + 0x8) = xy0;
  AT32(prim + 0x10) = xy1;
#else
  MTC2(xy0, 12);
  MTC2(xy1, 13);
  MTC2(xy2, 14);
  AT32(prim + 0x8) = xy0;
  AT32(prim + 0x10) = xy1;
  NCLIP();
#endif
  AT32(prim + 0x18) = xy2;
  AT32(prim + 0x4) = c0;
  AT32(prim + 0xC) = c1;
  AT32(prim + 0x14) = c2;
  AT32(prim + 0x0) = 0x6000000u; /* G3 */
#if NGTE_MATH
  if (mac0 <= 0)
#else
  if ((s32)MFC2(24) <= 0)
#endif
    return prim; /* facing away: the space is used again */
  Link(slot, prim, ND_K_G3);
  return prim + 0x1C;
}

#if NGTE_MATH
/* The native commands' record (D98): V0-V2 as the code last moved them,
 * the last two projections with V0-V2 as they were then, and which of
 * SZ1-3 the code read (MFC2 17-19 zero their top halves, which no command
 * writes). */
typedef struct {
  u32 v[6];
  u32 ev[2][6];
  u8 ev_rtpt[2];
  u32 nev;
  u8 rd17, rd18, rd19;
  u8 v2_set; /* fast_render=3: V2 moved in this call (the head's RTPT) */
} FlameSync;

#if NGTE_RC
/* fast_render=3 (D124): only V0-V2 as this call moved them; no tail */
static inline void FlameEvent(FlameSync *f, int rtpt) {
  if (rtpt)
    f->v2_set = 1;
}
#else
static inline void FlameEvent(FlameSync *f, int rtpt) {
  const u32 k = f->nev++ & 1;
  memcpy(f->ev[k], f->v, sizeof f->v);
  f->ev_rtpt[k] = (u8)rtpt;
}

/* The register file as the asm leaves it. U: the first MVMVA, whose
 * results stand if nothing was projected. */
static void FlameTail(FlameSync *f, const NgteVec *u) {
  u32 k;
  if (f->nev == 0) {
    C2_MAC1 = u->mac1;
    C2_MAC2 = u->mac2;
    C2_MAC3 = u->mac3;
    C2_IR1 = (short)u->ir1;
    C2_IR2 = (short)u->ir2;
    C2_IR3 = (short)u->ir3;
    C2_FLAG = u->flag;
  }
  for (k = f->nev > 2 ? f->nev - 2 : 0; k < f->nev; k++) {
    const u32 *v = f->ev[k & 1];
    MTC2(v[0], 0);
    MTC2(v[1], 1);
    MTC2(v[2], 2);
    MTC2(v[3], 3);
    MTC2(v[4], 4);
    MTC2(v[5], 5);
    if (f->ev_rtpt[k & 1])
      RTPT();
    else
      RTPS();
  }
  MTC2(f->v[0], 0);
  MTC2(f->v[1], 1);
  MTC2(f->v[2], 2);
  MTC2(f->v[3], 3);
  MTC2(f->v[4], 4);
  MTC2(f->v[5], 5);
  if (f->rd17)
    (void)MFC2(17);
  if (f->rd18)
    (void)MFC2(18);
  if (f->rd19)
    (void)MFC2(19);
}
#endif /* NGTE_RC */
#endif

static u32 FlameDraw(u32 a0_, u32 a1_, u32 a2_, u32 a3_) {
  const u32 fl = ADDR_ND(g_SpyroFlame), cam = ADDR_ND(g_Camera);
  const u32 sin_tab = ADDR_ND(D_8006CBF8);
  const u32 ot = MIPS_KUSEG(AT32(ADDR_ND(g_WorldOT)));
  u32 prim = AT32(ADDR_ND(D_800757B0));
  u32 big, seg;
#if NGTE_MATH
  FlameSync fs;
  NgteProj P;
  NgteVec u;
#endif
  (void)a0_, (void)a1_, (void)a2_, (void)a3_;
  hand_spill_regs();
#if NGTE
  nd_begin();
#endif
  {
    /* the flame's position from the camera through the view matrix, as the
     * translation; then the flame's own rotation */
    const u32 dx = AT32(fl + 0x0) - AT32(cam + 0x28);
    const u32 dy = AT32(cam + 0x2C) - AT32(fl + 0x4);
    const u32 dz = AT32(cam + 0x30) - AT32(fl + 0x8);
    CTC2(AT32(cam + 0x0), 0);
    CTC2(AT32(cam + 0x4), 1);
    CTC2(AT32(cam + 0x8), 2);
    CTC2(AT32(cam + 0xC), 3);
    CTC2(AT32(cam + 0x10), 4);
    CTC2(0u, 5);
    CTC2(0u, 6);
    CTC2(0u, 7);
#if !NGTE_RC
    MTC2(dx, 11);
    MTC2(dy, 9);
    MTC2(dz, 10);
#endif
#if NGTE_MATH
    {
      /* the IR words stay moved (their top halves are what the asm
       * leaves); the MVMVA native */
      const NgteProj P0 = ngte_proj_get();
      const NgteMat m = ngte_rot_of(&P0);
      u = ngte_mvmva(&m, 0, 0, 0, (s16)dy, (s16)dz, (s16)dx);
      CTC2((u32)u.mac3, 7);
      CTC2((u32)u.mac2, 6);
      CTC2((u32)u.mac1, 5);
    }
#else
    MVMVA_RT_IR();
    {
      const u32 m3 = MFC2(27), m2 = MFC2(26), m1 = MFC2(25);
      CTC2(m3, 7);
      CTC2(m2, 6);
      CTC2(m1, 5);
    }
#endif
    CTC2(AT32(fl + 0xB8), 0);
    CTC2(AT32(fl + 0xBC), 1);
    CTC2(AT32(fl + 0xC0), 2);
    CTC2(AT32(fl + 0xC4), 3);
    CTC2(AT32(fl + 0xC8), 4);
  }
#if NGTE_MATH
  P = ngte_proj_get();
#if NGTE_RC
  memset(fs.v, 0, sizeof fs.v);
  fs.v2_set = 0;
#else
  memcpy(fs.v, gteRegs.CP2D.r, sizeof fs.v);
#endif
  fs.nev = 0;
  fs.rd17 = fs.rd18 = fs.rd19 = 0;
#endif
  big = AT32(fl + 0x9C);
  for (seg = 8; seg-- != 0;) {
    u32 frame, step, base, ring, ring_end, first, word, pt, edge1, gp;
    s32 fidx = (s32)(s8)AT8(fl + seg + 0x20);
    const u32 start = (u32)(s32)(s8)AT8(fl + seg + 0x28);
    if (fidx <= 0)
      continue;
    frame = ADDR_ND(D_8006D94C) + ((u32)(fidx - 1) << 2);
    if ((s32)(seg - 4) >= 0) {
      step = 0x100;
      base = (seg - 4) * 0x100u + 0x300u;
      frame += 0x80;
    } else {
      step = 0xC0;
      base = seg * 0xC0u;
    }
    word = AT32(frame);
    ring = ADDR_ND(D_8006DAA8) + base;
    ring_end = ring + step;
    pt = ring + (((word >> 24) << 3) & 0x7F8u);
    first = ring + (start << 3);
    gp = 0;
    if (!((s32)(pt - first) > 0) && (s32)(ring_end - pt) > 0) {
      /* the head: the tip projected, three points round it (RTPT), four
       * triangles */
      u32 tip0 = AT32(pt + 0x0), tip1 = AT32(pt + 0x4);
      u32 nxt0, nxt1, scale, ang, sz, tip, s0x, s1x, s2x, colours, ca, cb, cc, cd, slot;
      s32 vz, vx, vy, cs, sn, a0, a1, a2, a3, b0, b1;
      gp = 1;
#if NGTE_MATH
      fs.v[1] = SEXT16(tip0);
      fs.v[0] = ((0u - (u32)((s32)tip0 >> 16)) & 0xFFFFu) | (0u - (tip1 << 16));
      {
        const NgteRtp q = ngte_rtps_w(&P, fs.v[0], fs.v[1]);
        FlameEvent(&fs, 0);
        sz = q.sz3;
        tip = q.sxy;
        fs.rd19 = 1;
      }
      nxt0 = AT32(pt - 0x10);
      nxt1 = AT32(pt - 0xC);
#else
      MTC2(SEXT16(tip0), 1);
      MTC2(((0u - (u32)((s32)tip0 >> 16)) & 0xFFFFu) | (0u - (tip1 << 16)), 0);
      nxt0 = AT32(pt - 0x10);
      nxt1 = AT32(pt - 0xC);
      RTPS();
#endif
      vz = (s32)SEXT16(nxt0);
      vx = (s32)nxt0 >> 16;
      scale = big ? 0x40u : 0x2Cu;
      ang = (nxt1 >> 15) & 0x1FEu;
      vy = (s32)SEXT16(nxt1);
      cs = (s32)S16(sin_tab + ang + 0x80);
      sn = (s32)S16(sin_tab + ang);
#if !NGTE_MATH
      sz = MFC2(19);
      tip = MFC2(14);
#endif
      cs = (s32)(scale * (u32)cs) >> 11;
      sn = (s32)(scale * (u32)sn) >> 11;
      a0 = cs - vx;
      a1 = sn - vy;
      a2 = a0 - (cs << 1);
      a3 = a1 - (sn << 1);
      cs = -cs;
      if (big) {
        cs >>= 1;
        sn >>= 1;
      }
      b0 = sn - vx;
      b1 = cs - vy;
#if NGTE_MATH
      fs.v[1] = fs.v[3] = fs.v[5] = (u32)vz;
      fs.v[0] = ((u32)b0 & 0xFFFFu) | ((u32)b1 << 16);
      fs.v[2] = ((u32)a0 & 0xFFFFu) | ((u32)a1 << 16);
      fs.v[4] = ((u32)a2 & 0xFFFFu) | ((u32)a3 << 16);
      colours = ADDR_ND(D_8006E1A8) + (big ? 0x10u : 0u);
      {
        const NgteRtpt t = ngte_rtpt_w(&P, fs.v[0], fs.v[1], fs.v[2], fs.v[3], fs.v[4], fs.v[5]);
        FlameEvent(&fs, 1);
        s0x = t.sxy[0];
        s1x = t.sxy[1];
        s2x = t.sxy[2];
      }
#else
      MTC2((u32)vz, 1);
      MTC2((u32)vz, 3);
      MTC2((u32)vz, 5);
      MTC2(((u32)b0 & 0xFFFFu) | ((u32)b1 << 16), 0);
      MTC2(((u32)a0 & 0xFFFFu) | ((u32)a1 << 16), 2);
      MTC2(((u32)a2 & 0xFFFFu) | ((u32)a3 << 16), 4);
      colours = ADDR_ND(D_8006E1A8) + (big ? 0x10u : 0u);
      RTPT();
#endif
      ca = AT32(colours + 0x0);
      cb = AT32(colours + 0x4);
      cc = AT32(colours + 0x8);
      cd = AT32(colours + 0xC);
      sz = (sz >> 7) - 2u;
      if ((s32)sz < 0)
        continue; /* too near: the whole segment goes */
      slot = (sz << 3) + ot;
#if !NGTE_MATH
      s0x = MFC2(12);
      s1x = MFC2(13);
      s2x = MFC2(14);
#endif
      prim = HeadTriangle(prim, slot, tip, s1x, s0x, ca, cc, cb);
      prim = HeadTriangle(prim, slot, tip, s0x, s2x, ca, cb, cd);
      prim = HeadTriangle(prim, slot, tip, s2x, s1x, ca, cd, cc);
      prim = HeadTriangle(prim, slot, s0x, s1x, s2x, cb, cc, cd);
    }
    {
      /* the strip: ring points from pt - 0x10 down to the segment's first,
       * then (as many again as the fade allows) further rings */
      u32 fade, left, colour, grey, uv0, uv1, du, dc, quad_edge;
      pt -= 0x10;
      fade = (word >> 8) & 0xFFu;
      left = (word >> 16) & 0xFFu;
      {
        const u32 d = pt - first;
        if ((s32)d > 0) {
          fade += d >> 3;
          left -= d >> 3;
          pt = first;
        }
      }
      fade *= 5u;
      {
        const u32 g = 0x80u - fade;
        grey = g | (g << 8) | (g << 16);
        colour = 0x3E000000u | grey;
      }
      {
        const u32 uvw = fl + (big ? 0x8u : 0u);
        uv0 = AT32(uvw + 0x10) + (fade << 8);
        uv1 = AT32(uvw + 0x14) + (fade << 8);
      }
      du = 0xA00u;
      dc = 0xA0A0Au;
      /* the last edge's screen words (the asm's at and v0): 0 and the grey
       * word before any, which the first quad's unlinked space keeps */
      quad_edge = 0;
      edge1 = grey;
      for (;;) {
        u32 p0, p1, rad;
        s32 vz, vx, vy, cs, sn, e0, e1, f0, f1;
#if NGTE_MATH
        NgteRtpt st;
#endif
        p0 = AT32(pt + 0x0);
        p1 = AT32(pt + 0x4);
        pt -= 0x10;
        vz = (s32)SEXT16(p0);
        vx = (s32)p0 >> 16;
        {
          const u32 ang = (p1 >> 15) & 0x1FEu;
          cs = (s32)S16(sin_tab + ang + 0x80);
          sn = (s32)S16(sin_tab + ang);
        }
        rad = 0x8u;
        if ((s32)left > 0) {
          gp += quad_edge;
          if (gp != 0)
            rad = big ? 0x40u : 0x2Cu;
        }
        vy = (s32)SEXT16(p1);
        cs = (s32)(rad * (u32)cs) >> 11;
        sn = (s32)(rad * (u32)sn) >> 11;
        e0 = cs - vx;
        e1 = sn - vy;
        f0 = e0 - (cs << 1);
        f1 = e1 - (sn << 1);
#if NGTE_MATH
        fs.v[1] = fs.v[3] = (u32)vz;
        fs.v[0] = ((u32)e0 & 0xFFFFu) | ((u32)e1 << 16);
        fs.v[2] = ((u32)f0 & 0xFFFFu) | ((u32)f1 << 16);
#if NGTE_RC
        if (!fs.v2_set) {
          /* V2 as the code before left it: none of this call's, and only
           * the first two points are read, so two RTPS */
          const NgteRtp q0 = ngte_rtps_w(&P, fs.v[0], fs.v[1]), q1 = ngte_rtps_w(&P, fs.v[2], fs.v[3]);
          st.sxy[0] = q0.sxy, st.sz[0] = q0.sz3;
          st.sxy[1] = q1.sxy, st.sz[1] = q1.sz3;
        } else
#endif
        st = ngte_rtpt_w(&P, fs.v[0], fs.v[1], fs.v[2], fs.v[3], fs.v[4], fs.v[5]);
        FlameEvent(&fs, 1);
        fs.rd17 = fs.rd18 = 1;
#else
        MTC2((u32)vz, 1);
        MTC2((u32)vz, 3);
        MTC2(((u32)e0 & 0xFFFFu) | ((u32)e1 << 16), 0);
        MTC2(((u32)f0 & 0xFFFFu) | ((u32)f1 << 16), 2);
        RTPT();
#endif
        AT32(prim + 0x8) = quad_edge;
        AT32(prim + 0x14) = edge1;
        AT32(prim + 0xC) = uv0;
        AT32(prim + 0x18) = uv1;
        AT32(prim + 0x4) = colour;
        AT32(prim + 0x10) = colour;
        AT32(prim + 0x0) = 0xC000000u; /* GT4 */
        {
#if NGTE_MATH
          const u32 x0 = st.sxy[0], x1 = st.sxy[1];
          u32 depth = st.sz[0] + st.sz[1];
#else
          const u32 x0 = MFC2(12), x1 = MFC2(13);
          u32 depth = MFC2(17) + MFC2(18);
#endif
          if (quad_edge == 0) {
            /* the first edge: nothing drawn yet */
            quad_edge = x0;
            edge1 = x1;
          } else {
            depth >>= 8;
            AT32(prim + 0x20) = x0;
            AT32(prim + 0x2C) = x1;
            quad_edge = x0;
            edge1 = x1;
            uv0 += du;
            AT32(prim + 0x24) = uv0;
            uv1 += du;
            AT32(prim + 0x30) = uv1;
            colour -= dc;
            AT32(prim + 0x1C) = colour;
            AT32(prim + 0x28) = colour;
            depth -= 2u;
            if ((s32)depth >= 0) {
              Link((depth << 3) + ot, prim, ND_K_GT4);
              prim += 0x34;
            }
          }
        }
        /* the next point: while the fade lasts, one ring each; after the
         * segment's first point a smaller step */
        if ((s32)left <= 0)
          break;
        left--;
        if ((s32)left > 0) {
          left--;
          continue;
        }
        left--;
        pt += 0x8;
        du = 0x500u;
        dc = 0x50505u;
      }
    }
  }
  AT32(ADDR_ND(D_800757B0)) = prim;
#if NGTE_MATH && !NGTE_RC
  FlameTail(&fs, &u);
#endif
#if NGTE
  nd_end();
#endif
  return 0;
}

HAND_RENDERER_UNIT(func_80058D64, FlameDraw)

/* The shadows (asm/renderers/r_shadows.s, hand-written asm in the
 * original), written by hand from the asm as C (docs/native-plan.md stage
 * 2, decisions D95): func_80059A48 draws Spyro's shadow, func_80059F8C the
 * mobys'. They replace the mips2c translation when fast_render is 1 or 2
 * (render_hand.h HAND_RENDERER_UNIT); fast_render=0 runs the translation,
 * which stays linked as NAME_gen, and the host's --fast-render-check runs it
 * beside every call (docs/translation-log.md).
 *
 * The method is D72's: the asm's control flow and arithmetic with named
 * locals, its GTE commands on the register file (gte_inline.h), and the
 * scratchpad used as the asm uses it, so RAM, the scratchpad and every GTE
 * register are left as the asm leaves them.
 *
 * Spyro's shadow is a fan of 16 triangles round his screen position: an
 * outline of 16 points from his model's frame (two frames blended by the
 * animation's step), shrunk for a flat floor, each point turned by the
 * floor's slope and projected; then for each edge a packet with a draw
 * mode (subtract, tpage 0x640) and an untextured Gouraud triangle (G3)
 * whose centre vertex is black and whose rim is the shadow's colour, into
 * the world OT bucket of its depth.
 *
 * A moby's shadow is a textured triangle (FT3, subtract) under each moby in
 * g_MobyShadows: its centre projected, tilted by the floor's slope, the
 * triangle's corners from RTPT on the shadow's size, faded with distance.
 *
 * fast_render=2 (shadows_hand_ngte.c, NGTE 1; decisions D98): the GTE
 * commands as native maths on C values (gte_native.h), as D80 did for the
 * six main renderers. The control registers are still moved where the asm
 * moves them (other code reads them, and they are cheap stores). The data
 * registers' results stay in locals, and before the renderer returns the
 * register file is made what the asm leaves: Spyro's shadow runs its last
 * four points on the register file after setting the IR and colour words
 * the native commands left; the mobys' shadows replay, at the end, the last
 * moby that reached its RTPT and the last four that stopped at the centre's
 * RTPS (MobyShadowsTail), which together write every data register the
 * call's commands write.
 *
 * With native_draw=1 too (decisions D82, D86): every primitive is still
 * built where it was (the buffer's pointer and the bytes the game leaves
 * past it don't change), and where the asm links it, it goes to the walk
 * as a native record instead (nd_append), joining the marker already at
 * its bucket's tail. */
#include "render_hand.h"

extern char g_Spyro[];      /* Spyro's moby */
extern char g_Camera[];     /* the view matrix (+0x00), the camera position (+0x28) */
extern char g_Models[];     /* the models, by class */
extern char g_MobyShadows[]; /* the shadow list: start, its end, ... */
extern char D_8007AA10[];   /* Spyro's shadow: floor slope (+0x00), position (+0x10),
                               depth bias (+0x1C), hidden (+0x24) */
extern char D_8006CBF8[];   /* sine table (cosine at +0x80) */
extern char D_8006CC78[];
extern char D_8006E268[];   /* the 16 outline directions, x | z << 16 */
extern char D_8006FCF4[];   /* the moby list: entries from +0x2800 */
extern char D_800757B0[];   /* the primitive buffer's next free byte */
extern char g_WorldOT[];

/* GTE opcodes (gte_inline.h) */
#define RTPS() m2c_gte_0x180001()
#define RTPT() m2c_gte_0x280030()
#define NCLIP() m2c_gte_0x1400006()
#define MVMVA_RT_V0() m2c_gte_0x486012() /* sf 1: the rotation times V0 */
#define GPF0() m2c_gte_0x190003D()       /* sf 0: IR0 times IR1-3 */
#define GPL0() m2c_gte_0x1A0003E()       /* sf 0: MAC1-3 plus IR0 times IR1-3 */

#define S8(a) ((u32)(s32)*(const s8 *)(uintptr_t)(u32)(a))

#if NGTE
/* The primitive at prim, about to go on the end of world OT bucket `slot`:
 * as a record (native_draw), else linked as it is (OtAppend). */
#define SHADOW_APPEND(kind, slot, prim)                                        \
  do {                                                                         \
    if (g_nd.on)                                                               \
      nd_append((kind), (slot), (prim));                                       \
    else                                                                       \
      nd_ot_append((slot), (prim));                                            \
  } while (0)
#endif

/* ===================================================================== */
/* func_80059A48: Spyro's shadow                                          */
/* ===================================================================== */

static u32 SpyroShadow(u32 a0_, u32 a1_, u32 a2_, u32 a3_) {
  const u32 spyro = ADDR_ND(g_Spyro), cam = ADDR_ND(g_Camera), sh = ADDR_ND(D_8007AA10);
  const u32 scr = SCRATCH_BASE;
  u32 r0, r1, r2, r3, r4, sxy, sz, tilted, src, p, end, prim, ot, bias;
  u32 rgb, ctr;
  s32 dx, dy, dz;
#if NGTE_MATH
  /* what the native commands leave in IR0-3 and RGB0-2, for the register
   * file before the last four points */
  u32 irw[3], ir0w = 0, rgbw[3] = {0, 0, 0}, blend = 0;
  NgteProj P;
#endif
  (void)a0_, (void)a1_, (void)a2_, (void)a3_;
  if ((s32)AT32(sh + 0x24) > 0)
    return 0;
#if NGTE
  nd_begin();
#endif
  /* the shadow's position from the camera, through the view matrix */
  dx = (s32)(AT32(sh + 0x10) - AT32(cam + 0x28));
  dy = (s32)(AT32(cam + 0x2C) - AT32(sh + 0x14));
  dz = (s32)(AT32(cam + 0x30) - AT32(sh + 0x18));
  r0 = AT32(cam + 0x0);
  r1 = AT32(cam + 0x4);
  r2 = AT32(cam + 0x8);
  r3 = AT32(cam + 0xC);
  r4 = AT32(cam + 0x10);
  CTC2(r0, 0);
  CTC2(r1, 1);
  CTC2(r2, 2);
  CTC2(r3, 3);
  CTC2(r4, 4);
  CTC2(0u, 5);
  CTC2(0u, 6);
  CTC2(0u, 7);
#if NGTE_MATH
  {
    /* the same, native: the shadow's view position, then the heading's
     * two MVMVA (rotation times V0, no translation) */
    const u32 h = (u32)AT8(spyro + 0xE) << 1;
    const u32 cosv = AT16(ADDR_ND(D_8006CC78) + h);
    const u32 sinv = AT16(ADDR_ND(D_8006CBF8) + h);
    const NgteProj P0 = ngte_proj_get();
    const NgteRtp c = ngte_rtps_w(&P0, ((u32)dy & 0xFFFFu) + ((u32)dz << 16), (u32)dx);
    const NgteMat m = ngte_rot_of(&P0);
    const NgteVec u = ngte_mvmva(&m, 0, 0, 0, (s16)cosv, 0, (s16)sinv);
    const NgteVec v = ngte_mvmva(&m, 0, 0, 0, (s16)(0u - sinv), 0, (s16)cosv);
    sxy = c.sxy;
    sz = c.sz3;
    CTC2((u32)c.mac3, 7);
    CTC2((u32)c.mac2, 6);
    CTC2((u32)c.mac1, 5);
    r0 = (r0 & 0xFFFF0000u) + ((u32)u.ir1 & 0xFFFFu);
    r3 = (r3 & 0xFFFF0000u) + ((u32)u.ir3 & 0xFFFFu);
    r1 = ((u32)v.ir1 & 0xFFFFu) + ((u32)u.ir2 << 16);
    r2 = (r2 & 0xFFFFu) + ((u32)v.ir2 << 16);
    r4 = (u32)v.ir3 & 0xFFFFu;
    /* IR1-3 as the second MVMVA's reads leave them (sign-extended) */
    irw[0] = (u32)v.ir1;
    irw[1] = (u32)v.ir2;
    irw[2] = (u32)v.ir3;
  }
#else
  MTC2((u32)dx, 1); /* VZ0 */
  MTC2(((u32)dy & 0xFFFFu) + ((u32)dz << 16), 0);
  RTPS();
  {
    /* Spyro's heading: the view matrix turned by it becomes the rotation
     * (two MVMVA on its sine and cosine), the shadow's view position the
     * translation */
    const u32 h = (u32)AT8(spyro + 0xE) << 1;
    const u32 cosv = AT16(ADDR_ND(D_8006CC78) + h);
    const u32 sinv = AT16(ADDR_ND(D_8006CBF8) + h);
    u32 ir1, ir2, ir3;
    sxy = MFC2(14);
    sz = MFC2(19);
    {
      const u32 m3 = MFC2(27), m2 = MFC2(26), m1 = MFC2(25);
      CTC2(m3, 7);
      CTC2(m2, 6);
      CTC2(m1, 5);
    }
    MTC2(cosv & 0xFFFFu, 0);
    MTC2(sinv, 1);
    MVMVA_RT_V0();
    ir1 = MFC2(9);
    ir2 = MFC2(10);
    ir3 = MFC2(11);
    MTC2((0u - sinv) & 0xFFFFu, 0);
    MTC2(cosv & 0xFFFFu, 1);
    MVMVA_RT_V0();
    r0 = (r0 & 0xFFFF0000u) + (ir1 & 0xFFFFu);
    r3 = (r3 & 0xFFFF0000u) + (ir3 & 0xFFFFu);
    {
      const u32 j1 = MFC2(9), j2 = MFC2(10), j3 = MFC2(11);
      r1 = (j1 & 0xFFFFu) + (ir2 << 16);
      r2 = (r2 & 0xFFFFu) + (j2 << 16);
      r4 = j3 & 0xFFFFu;
    }
  }
#endif
  CTC2(r0, 0);
  CTC2(r1, 1);
  CTC2(r2, 2);
  CTC2(r3, 3);
  CTC2(r4, 4);

  /* the outline's 16 radii (scratch +0x00..+0x0F) and 16 heights (+0x0C
   * after the copy below) from Spyro's model frame */
  {
    const u32 models = MIPS_KUSEG(MEM_MODEL_AT(AT32(ADDR_ND(g_Models))));
    const u32 step = AT8(spyro + 0x24);
    if (step) {
      /* blending two frames: the anim and frame of each (+0x18, +0x1E) */
      const u32 af0 = AT16(spyro + 0x18), af1 = AT16(spyro + 0x1E);
      u32 ma = MIPS_KUSEG(AT32(models + ((af0 & 0xFF) << 2) + 0x38));
      u32 mb = MIPS_KUSEG(AT32(models + ((af0 >> 8) << 2) + 0x38));
      u32 fa, fb, k, w;
      ma = AT32(ma + ((af1 & 0xFF) << 2) + 0x24);
      mb = AT32(mb + ((af1 >> 8) << 2) + 0x24);
      fa = MEM_ANIM_HALF(ma);
      fb = MEM_ANIM_HALF(mb);
      AT32(scr + 0x0) = AT32(fa + 0x0);
      AT32(scr + 0x4) = AT32(fa + 0x4);
      AT32(scr + 0x8) = AT32(fb + 0x0);
      AT32(scr + 0xC) = AT32(fb + 0x4);
      /* GPF/GPL: (16 - step) * the first frame's bytes + step * the second's, / 16 */
      w = 0x10u - step;
      p = scr;
#if NGTE_MATH
      {
        /* GPF then GPL (sf 0), native; the six colours pushed, the IR
         * words the last GPL leaves */
        const u32 code = gteRegs.CP2D.r[6] >> 24;
        for (k = 3; (s32)k > 0; k--, p += 3) {
          const u32 b0 = AT8(p + 0), b1 = AT8(p + 1), b2 = AT8(p + 2);
          const u32 c0 = AT8(p + 8), c1 = AT8(p + 9), c2 = AT8(p + 10);
          const NgteCol f = ngte_gpf0((s32)b0, (s32)b1, (s32)b2, (s32)w, code);
          const NgteCol l = ngte_gpl0(f.mac1, f.mac2, f.mac3, (s32)c0, (s32)c1, (s32)c2, (s32)step, code);
          rgbw[0] = rgbw[2];
          rgbw[1] = f.rgb2;
          rgbw[2] = l.rgb2;
          AT8(p + 0) = (u8)((u32)l.mac1 >> 4);
          AT8(p + 1) = (u8)((u32)l.mac2 >> 4);
          AT8(p + 2) = (u8)((u32)l.mac3 >> 4);
          irw[0] = (c0 & 0xFFFF0000u) | ((u32)l.ir1 & 0xFFFFu);
          irw[1] = (c1 & 0xFFFF0000u) | ((u32)l.ir2 & 0xFFFFu);
          irw[2] = (c2 & 0xFFFF0000u) | ((u32)l.ir3 & 0xFFFFu);
        }
        ir0w = step;
        blend = 1;
      }
#else
      for (k = 3;;) {
        u32 b0 = AT8(p + 0), b1 = AT8(p + 1), b2 = AT8(p + 2), c0, c1, c2;
        MTC2(w, 8);
        MTC2(b0, 9);
        MTC2(b1, 10);
        MTC2(b2, 11);
        c0 = AT8(p + 8);
        c1 = AT8(p + 9);
        GPF0();
        c2 = AT8(p + 10);
        MTC2(step, 8);
        MTC2(c0, 9);
        MTC2(c1, 10);
        MTC2(c2, 11);
        k--;
        p += 3;
        GPL0();
        {
          const u32 m1 = MFC2(25), m2 = MFC2(26), m3 = MFC2(27);
          AT8(p - 3) = (u8)(m1 >> 4);
          AT8(p - 2) = (u8)(m2 >> 4);
          AT8(p - 1) = (u8)(m3 >> 4);
        }
        if ((s32)k <= 0)
          break;
      }
#endif
      src = scr;
      p = scr + 1;
    } else {
      const u32 af = AT8(spyro + 0x18), fr = AT8(spyro + 0x1E);
      u32 m = MIPS_KUSEG(AT32(models + (af << 2) + 0x38));
      m = AT32(m + (fr << 2) + 0x24);
      src = MEM_ANIM_HALF(m);
      p = scr + 1;
    }
    (void)p;
  }
  /* (both paths: the frame's two words to scratch +0x00 and +0x08, as the
   * asm copies them from wherever they are now) */
  {
    const u32 w1 = AT32(src + 0x4), w0 = AT32(src + 0x0);
    AT32(scr + 0x4) = w1;
    AT32(scr + 0x0) = w0;
    AT32(scr + 0x8) = w0;
  }
  {
    /* the floor's slope (D_8007AA10 +0x00..+0x0C): with any tilt, the
     * outline is drawn at 3/4 */
    const u32 s0 = AT32(sh + 0x0), s1 = AT32(sh + 0x4);
    tilted = (AT32(sh + 0x8) | AT32(sh + 0xC)) != 0;
    AT32(scr + 0x10) = s1;
    AT32(scr + 0xC) = s0;
    AT32(scr + 0x14) = s0;
  }
  {
    /* 16 radii and heights, each with its successor's average between:
     * scratch +0x80 (radii, 32) and +0x90 (heights, 32) */
    u32 q = scr, o = scr + 0x80;
    for (; q != scr + 0x8; q++, o += 2) {
      u32 ra = AT8(q + 0x0), rb = AT8(q + 0x1);
      u32 ha = S8(q + 0xC), hb = S8(q + 0xD);
      if (tilted) {
        ra = (u32)((s32)(ra + (ra << 1)) >> 2);
        rb = (u32)((s32)(rb + (rb << 1)) >> 2);
        ha = (u32)((s32)(ha + (ha << 1)) >> 2);
        hb = (u32)((s32)(hb + (hb << 1)) >> 2);
      }
      AT8(o + 0x0) = (u8)ra;
      AT8(o + 0x1) = (u8)((ra + rb) >> 1);
      AT8(o + 0x10) = (u8)ha;
      AT8(o + 0x11) = (u8)((s32)(ha + hb) >> 1);
    }
  }
  {
    /* the 16 points projected: radius times the direction (x, z), the
     * height as y, to scratch +0x00..+0x7C as SXY, SZ pairs */
    u32 rad = scr + 0x80, hgt = scr + 0x90, dir = ADDR_ND(D_8006E268);
    u32 o = scr;
    u32 d = AT32(dir), r = AT8(rad), dz_, dxz;
    s32 cx = (s32)d >> 16, cz = (s32)(d << 16) >> 16;
    rad++;
    dir += 4;
#if NGTE_MATH
    P = ngte_proj_get();
#endif
    for (;;) {
      const u32 hy = S8(hgt);
      u32 vxy;
#if NGTE_MATH
      dz_ = (u32)((s32)(r * (u32)cz) >> 10);
      dxz = (u32)((s32)(r * (u32)cx) >> 10);
      vxy = (dxz & 0xFFFFu) + (hy << 18);
      hgt++;
      rad++;
      if (o < scr + 0x60) {
        /* the first twelve native; the last four on the register file,
         * which then holds what all sixteen leave */
        const NgteRtp q = ngte_rtps_w(&P, vxy, dz_);
        AT32(o + 0x0) = q.sxy;
        AT32(o + 0x4) = q.sz3;
      } else {
        if (o == scr + 0x60) {
          /* what the native commands left that RTPS doesn't rewrite: the
           * IR words' top halves (IR0's after the blend), the colours */
          MTC2(irw[0], 9);
          MTC2(irw[1], 10);
          MTC2(irw[2], 11);
          if (blend) {
            MTC2(ir0w, 8);
            MTC2(rgbw[0], 20);
            MTC2(rgbw[1], 21);
            MTC2(rgbw[2], 22);
          }
        }
        MTC2(dz_, 1);
        MTC2(vxy, 0);
        RTPS();
        AT32(o + 0x0) = MFC2(14);
        AT32(o + 0x4) = MFC2(19);
      }
      d = AT32(dir);
      r = AT8(rad - 1);
      dir += 4;
      cx = (s32)d >> 16;
      cz = (s32)(d << 16) >> 16;
#else
      dz_ = (u32)((s32)(r * (u32)cz) >> 10);
      MTC2(dz_, 1);
      dxz = (u32)((s32)(r * (u32)cx) >> 10);
      vxy = (dxz & 0xFFFFu) + (hy << 18);
      MTC2(vxy, 0);
      hgt++;
      rad++;
      RTPS();
      d = AT32(dir);
      r = AT8(rad - 1);
      dir += 4;
      cx = (s32)d >> 16;
      cz = (s32)(d << 16) >> 16;
      AT32(o + 0x0) = MFC2(14);
      AT32(o + 0x4) = MFC2(19);
#endif
      if (o == scr + 0x78)
        break;
      o += 8;
    }
  }
  /* the fan: for each edge (point i, point i+1; the last point is the
   * first again), a draw mode packet and a G3 (subtract), into the bucket
   * of the edge's depth */
  p = scr;
  end = scr + 0x80;
  ot = MIPS_KUSEG(AT32(ADDR_ND(g_WorldOT)));
  prim = AT32(ADDR_ND(D_800757B0));
  ctr = sxy;
  bias = AT32(sh + 0x1C);
  rgb = 0x32608080u;
  AT32(end + 0x0) = AT32(p + 0x0);
  AT32(end + 0x4) = AT32(p + 0x4);
  for (;;) {
    u32 xa, za, xb, zb, depth;
    if (p == end)
      break;
    xa = AT32(p + 0x0);
    za = AT32(p + 0x4);
    xb = AT32(p + 0x8);
    zb = AT32(p + 0xC);
    p += 8;
    AT32(prim + 0x10) = rgb; /* G3: colour 0 (centre), code 0x32 */
    AT32(prim + 0x14) = ctr;
    AT32(prim + 0x18) = 0u;  /* colour 1: black */
    AT32(prim + 0x1C) = xa;
    za = za + zb;
    depth = za + (za >> 1) + sz;
    depth >>= 9;
    AT32(prim + 0x20) = 0u;
    AT32(prim + 0x24) = xb;
    AT32(prim + 0x8) = 0u;
    AT32(prim + 0x4) = 0xE1000640u; /* draw mode: subtract, tpage 0x640 */
    AT32(prim + 0x0) = MEM_TAG_LINK_AT(MEM_TAG(0x82000000u), prim, 0xCu);
    AT32(prim + 0xC) = 0x6000000u;
    depth -= bias;
    if ((s32)depth < 0)
      continue; /* not linked: the space is used again */
    {
      const u32 slot = ot + (depth << 3);
#if NGTE
      if (g_nd.on) {
        nd_append(ND_K_E1, slot, prim);
        nd_append(ND_K_G3, slot, prim + 0xC);
        prim += 0x28;
        continue;
      }
#endif
      {
        const u32 tail = AT32(slot);
        AT32(slot) = prim + 0xC;
        if (tail) {
          AT16(tail + 0x0) = (u16)prim;
          AT8(tail + 0x2) = (u8)(prim >> 16);
        } else {
          AT32(slot + 0x4) = prim;
        }
      }
    }
    prim += 0x28;
  }
  AT32(ADDR_ND(D_800757B0)) = prim;
#if NGTE
  nd_end();
#endif
  return 0;
}

/* ===================================================================== */
/* func_80059F8C: the mobys' shadows                                      */
/* ===================================================================== */

#if NGTE_MATH
/* fast_render=2: what the mobys' shadows' native commands must leave in
 * the register file (D98). A moby whose centre is near enough goes on to
 * an RTPT, which rewrites every data register the loop's commands write
 * but IR1-3's top halves (set by the tilt's MFC2s, of this moby or an
 * earlier one) and V0's (rewritten by the next moby's RTPS). So the last
 * such moby, the "anchor", is replayed on the register file, then the last
 * four mobys after it that stopped at the centre's RTPS (four RTPS rewrite
 * both FIFOs), and the control registers are put back as the loop left
 * them. */
typedef struct {
  u32 vxy, vz;   /* the centre's V0 */
  u32 rot[5];    /* the rotation, tilted */
  u32 irw[3];    /* IR1-3's words as the last tilt's reads left them */
  u32 size;
  u8 irw_set, facing, read_corner;
} MsAnchor;

static void MobyShadowsTail(u32 scr, const MsAnchor *a, int anchored, const u32 *nxy, const u32 *nz,
                            u32 nnear) {
  const u32 r0 = AT32(scr + 0xC), r1 = AT32(scr + 0x10), r2 = AT32(scr + 0x14);
  const u32 r3 = AT32(scr + 0x18), r4 = AT32(scr + 0x1C);
  u32 ctl[8], k;
  memcpy(ctl, gteRegs.CP2C.r, sizeof ctl);
  if (anchored) {
    CTC2(r0, 0);
    CTC2(r1, 1);
    CTC2(r2, 2);
    CTC2(r3, 3);
    CTC2(r4, 4);
    CTC2(0u, 5);
    CTC2(0u, 6);
    CTC2(0u, 7);
    MTC2(a->vxy, 0);
    MTC2(a->vz, 1);
    RTPS();
    {
      const u32 m3 = MFC2(27), m2 = MFC2(26), m1 = MFC2(25);
      CTC2(m3, 7);
      CTC2(m2, 6);
      CTC2(m1, 5);
    }
    /* the tilt's MVMVA results go under the RTPT's, but for IR1-3's top
     * halves */
    CTC2(a->rot[0], 0);
    CTC2(a->rot[1], 1);
    CTC2(a->rot[2], 2);
    CTC2(a->rot[3], 3);
    CTC2(a->rot[4], 4);
    if (a->irw_set) {
      MTC2(a->irw[0], 9);
      MTC2(a->irw[1], 10);
      MTC2(a->irw[2], 11);
    }
    (void)MFC2(19);
    MTC2(0u, 0);
    MTC2(a->size, 1);
    MTC2(a->size, 2);
    MTC2(0u, 3);
    MTC2(0u, 4);
    MTC2(0u - a->size, 5);
    RTPT();
    NCLIP();
    (void)MFC2(17);
    (void)MFC2(18);
    (void)MFC2(19);
    if (a->facing) {
      MTC2((0u - a->size) & 0xFFFFu, 0);
      MTC2(0u, 1);
      RTPS();
      if (a->read_corner)
        (void)MFC2(19);
    }
  }
  for (k = nnear > 4 ? nnear - 4 : 0; k < nnear; k++) {
    CTC2(r0, 0);
    CTC2(r1, 1);
    CTC2(r2, 2);
    CTC2(r3, 3);
    CTC2(r4, 4);
    CTC2(0u, 5);
    CTC2(0u, 6);
    CTC2(0u, 7);
    MTC2(nxy[k & 3], 0);
    MTC2(nz[k & 3], 1);
    RTPS();
  }
  memcpy(gteRegs.CP2C.r, ctl, sizeof ctl);
}
#endif

static u32 MobyShadows(u32 a0_, u32 a1_, u32 a2_, u32 a3_) {
  const u32 scr = SCRATCH_BASE, cam = ADDR_ND(g_Camera);
  u32 ent, last;
#if NGTE_MATH
  NgteProj P = ngte_proj_get(); /* OFX, OFY, H, DQA, DQB; R and TR per moby */
  MsAnchor anchor;
  int anchored = 0;
  u32 nxy[4], nz[4], nnear = 0; /* the mobys after the anchor that were too far */
  u32 irw[3] = {0, 0, 0}, irw_set = 0;
#endif
  (void)a0_, (void)a1_, (void)a2_, (void)a3_;
#if NGTE
  nd_begin();
#endif
  /* scratch: the camera position / 4 (+0x00), the view matrix (+0x0C), the
   * shadow texture's uv/CLUT and uv/tpage words (+0x20, +0x24), the
   * primitive buffer (+0x28), the world OT (+0x2C) */
  AT32(scr + 0x0) = AT32(cam + 0x28) >> 2;
  AT32(scr + 0x4) = AT32(cam + 0x2C) >> 2;
  AT32(scr + 0x8) = AT32(cam + 0x30) >> 2;
  AT32(scr + 0xC) = AT32(cam + 0x0);
  AT32(scr + 0x10) = AT32(cam + 0x4);
  AT32(scr + 0x14) = AT32(cam + 0x8);
  AT32(scr + 0x18) = AT32(cam + 0xC);
  AT32(scr + 0x1C) = AT32(cam + 0x10);
  ent = ADDR_ND(D_8006FCF4) + 0x2800u;
  {
    const u32 ms = ADDR_ND(g_MobyShadows);
    const u32 uv0 = AT32(ms + 0x0), uv1 = AT32(ms + 0x4);
    last = AT32(ms + 0x8);
    AT32(scr + 0x20) = uv0;
    AT32(scr + 0x24) = uv1;
  }
  AT32(scr + 0x28) = AT32(ADDR_ND(D_800757B0));
  AT32(scr + 0x2C) = AT32(ADDR_ND(g_WorldOT));
  for (;;) {
    u32 moby, r0, r1, r2, r3, r4, cx, cy, cz, floor, ts, tc, size;
#if NGTE_MATH
    u32 n_csxy, n_csz;
#endif
    const u32 at = ent;
    ent += 8;
    if (at == last)
      break;
    moby = MIPS_KUSEG(AT32(at + 0x0));
    r0 = AT32(scr + 0xC);
    r1 = AT32(scr + 0x10);
    r2 = AT32(scr + 0x14);
    r3 = AT32(scr + 0x18);
    r4 = AT32(scr + 0x1C);
    CTC2(r0, 0);
    CTC2(r1, 1);
    CTC2(r2, 2);
    CTC2(r3, 3);
    CTC2(r4, 4);
    CTC2(0u, 5);
    CTC2(0u, 6);
    CTC2(0u, 7);
    cx = AT32(scr + 0x0);
    cy = AT32(scr + 0x4);
    cz = AT32(scr + 0x8);
    floor = AT32(moby + 0x1C); /* the floor's height (low half), its slope (bits 16-27) */
#if NGTE_MATH
    {
      /* the centre, native */
      const u32 my = AT32(moby + 0x10), mx = AT32(moby + 0xC);
      u32 h = floor & 0xFFFFu, vxy, vz;
      NgteRtp c;
      if (h == 0)
        continue;
      h = cz - h;
      vxy = (h << 16) + (cy - (my >> 2));
      vz = (mx >> 2) - cx;
      tc = (floor >> 16) & 0x3Fu;
      ts = (floor >> 22) & 0x3Fu;
      ngte_proj_rot(&P, r0, r1, r2, r3, r4);
      ngte_proj_tr(&P, 0, 0, 0);
      c = ngte_rtps_w(&P, vxy, vz);
      size = AT32(at + 0x4);
      tc <<= 3;
      ts <<= 3;
      CTC2((u32)c.mac3, 7);
      CTC2((u32)c.mac2, 6);
      CTC2((u32)c.mac1, 5);
      ngte_proj_tr(&P, (u32)c.mac1, (u32)c.mac2, (u32)c.mac3);
      if ((s32)((u32)c.mac3 + 0xFFFFF000u) >= 0) {
        /* too far */
        nxy[nnear & 3] = vxy;
        nz[nnear & 3] = vz;
        nnear++;
        continue;
      }
      anchor.vxy = vxy;
      anchor.vz = vz;
      n_csxy = c.sxy;
      n_csz = c.sz3;
    }
#else
    {
      const u32 my = AT32(moby + 0x10), mx = AT32(moby + 0xC);
      u32 h = floor & 0xFFFFu;
      if (h == 0)
        continue;
      h = cz - h;
      MTC2((h << 16) + (cy - (my >> 2)), 0);
      MTC2((mx >> 2) - cx, 1);
    }
    tc = (floor >> 16) & 0x3Fu; /* the slope's two angles, as sine-table offsets */
    ts = (floor >> 22) & 0x3Fu;
    RTPS();
    size = AT32(at + 0x4); /* the shadow's size */
    tc <<= 3;
    ts <<= 3;
    {
      const u32 m3 = MFC2(27), m2 = MFC2(26), m1 = MFC2(25);
      CTC2(m3, 7);
      CTC2(m2, 6);
      CTC2(m1, 5);
      if ((s32)(m3 + 0xFFFFF000u) >= 0)
        continue; /* too far */
    }
#endif
    /* the rotation tilted by the slope: about one axis, then the other */
    {
      const u32 sin_tab = ADDR_ND(D_8006CBF8);
      u32 t3 = r0, t4 = r1, t5 = r2, t6 = r3, t7 = r4;
#if NGTE_MATH
      /* native: each MVMVA turns V0 by the rotation as it is (the camera's,
       * then the first tilt's) */
      if (tc) {
        const u32 cs = AT16(sin_tab + tc + 0x80), sn = AT16(sin_tab + tc);
        const NgteMat m = ngte_rot_of(&P);
        const NgteVec i = ngte_mvmva(&m, 0, 0, 0, 0, (s16)cs, (s16)sn);
        const NgteVec j = ngte_mvmva(&m, 0, 0, 0, 0, (s16)(0u - sn), (s16)cs);
        t3 = (t3 & 0xFFFFu) + ts; /* the asm's second angle, as above */
        t6 = (t6 & 0xFFFFu) + ((u32)i.ir3 << 16);
        t4 = (t4 & 0xFFFF0000u) + ((u32)j.ir1 & 0xFFFFu);
        t5 = ((u32)j.ir2 << 16) + ((u32)i.ir2 & 0xFFFFu);
        t7 = (u32)j.ir3 & 0xFFFFu;
        CTC2(t3, 0);
        CTC2(t4, 1);
        CTC2(t5, 2);
        CTC2(t6, 3);
        CTC2(t7, 4);
        ngte_proj_rot(&P, t3, t4, t5, t6, t7);
        irw[0] = (u32)j.ir1, irw[1] = (u32)j.ir2, irw[2] = (u32)j.ir3;
        irw_set = 1;
      }
      if (ts) {
        const u32 cs = AT16(sin_tab + ts + 0x80), sn = AT16(sin_tab + ts);
        const NgteMat m = ngte_rot_of(&P);
        const NgteVec i = ngte_mvmva(&m, 0, 0, 0, (s16)cs, (s16)sn, 0);
        const NgteVec j = ngte_mvmva(&m, 0, 0, 0, (s16)(0u - sn), (s16)cs, 0);
        t4 = (t4 & 0xFFFFu) + ((u32)i.ir2 << 16);
        t5 = (t5 & 0xFFFF0000u) + ((u32)j.ir2 & 0xFFFFu);
        t3 = ((u32)j.ir1 << 16) + ((u32)i.ir1 & 0xFFFFu);
        t6 = ((u32)j.ir3 << 16) + ((u32)i.ir3 & 0xFFFFu);
        irw[0] = (u32)j.ir1, irw[1] = (u32)j.ir2, irw[2] = (u32)j.ir3;
        irw_set = 1;
      }
      ngte_proj_rot(&P, t3, t4, t5, t6, t7);
#else
      if (tc) {
        const u32 cs = AT16(sin_tab + tc + 0x80), sn = AT16(sin_tab + tc);
        u32 i1, i2, i3, j1, j2, j3;
        MTC2(cs << 16, 0);
        MTC2(sn, 1);
        MVMVA_RT_V0();
        i1 = MFC2(9);
        i2 = MFC2(10);
        i3 = MFC2(11);
        MTC2(0u - (sn << 16), 0);
        MTC2(cs, 1);
        MVMVA_RT_V0();
        /* the asm adds the second angle's table offset here, not i1 << 16
         * (`add $t3, $t3, $a0` at 0x8005A158 in the original; kept) */
        t3 = (t3 & 0xFFFFu) + ts;
        (void)i1;
        t6 = (t6 & 0xFFFFu) + (i3 << 16);
        j1 = MFC2(9);
        j2 = MFC2(10);
        j3 = MFC2(11);
        t4 = (t4 & 0xFFFF0000u) + (j1 & 0xFFFFu);
        t5 = (j2 << 16) + (i2 & 0xFFFFu);
        t7 = j3 & 0xFFFFu;
        CTC2(t3, 0);
        CTC2(t4, 1);
        CTC2(t5, 2);
        CTC2(t6, 3);
        CTC2(t7, 4);
      }
      if (ts) {
        const u32 cs = AT16(sin_tab + ts + 0x80), sn = AT16(sin_tab + ts);
        u32 i1, i2, i3, j1, j2, j3;
        MTC2(0u, 1);
        MTC2((sn << 16) + cs, 0);
        MVMVA_RT_V0();
        i1 = MFC2(9);
        i2 = MFC2(10);
        i3 = MFC2(11);
        MTC2(((0u - sn) & 0xFFFFu) + (cs << 16), 0);
        MTC2(0u, 1);
        MVMVA_RT_V0();
        t4 = (t4 & 0xFFFFu) + (i2 << 16);
        t5 = t5 & 0xFFFF0000u;
        j1 = MFC2(9);
        j2 = MFC2(10);
        j3 = MFC2(11);
        t3 = (j1 << 16) + (i1 & 0xFFFFu);
        t6 = (j3 << 16) + (i3 & 0xFFFFu);
        t5 = t5 + (j2 & 0xFFFFu);
      }
#endif
      CTC2(t3, 0);
      CTC2(t4, 1);
      CTC2(t5, 2);
      CTC2(t6, 3);
      CTC2(t7, 4);
    }
    {
      /* the triangle's corners on the shadow's size: RTPT; the centre's
       * screen word and depth kept (SXY2, SZ3 of the RTPS above) */
#if NGTE_MATH
      const u32 csxy = n_csxy, csz = n_csz;
      NgteRtpt t;
#else
      const u32 csxy = MFC2(14), csz = MFC2(19);
#endif
      const u32 scr2 = scr;
      u32 prim, ot, uvc, uvt, rgb, dist, x0, x1, x2, z0, z1, z2;
      s32 mac0;
#if NGTE_MATH
      NgteRtp corner;
#endif
      const u32 depth_bias = S8(moby + 0x47) - 1u;
#if NGTE_MATH
      dist = CFC2(7);
      t = ngte_rtpt_w(&P, 0u, size, size, 0u, 0u, 0u - size);
      /* this moby is the anchor now */
      memcpy(anchor.rot, gteRegs.CP2C.r, sizeof anchor.rot);
      memcpy(anchor.irw, irw, sizeof irw);
      anchor.irw_set = (u8)irw_set;
      anchor.size = size;
      anchor.facing = anchor.read_corner = 0;
      anchored = 1;
      nnear = 0;
#else
      MTC2(0u, 0);
      MTC2(size, 1);
      MTC2(size, 2);
      MTC2(0u, 3);
      MTC2(0u, 4);
      MTC2(0u - size, 5);
      dist = CFC2(7);
      RTPT();
#endif
      prim = AT32(scr2 + 0x28);
      ot = MIPS_KUSEG(AT32(scr2 + 0x2C));
      uvc = AT32(scr2 + 0x20);
      uvt = AT32(scr2 + 0x24);
      rgb = 0x26808080u;
      dist += 0xFFFFF400u;
      if ((s32)dist >= 0) {
        /* faded with distance: (0x400 - (dist - 0xC00)) / 8 for each channel */
        u32 f = 0u - dist;
        f = (f + 0x400u) >> 3;
        rgb = 0x26000000u | f | (f << 8) | (f << 16);
      }
      {
        const u32 negsz = (0u - size) & 0xFFFFu;
#if NGTE_MATH
        x0 = t.sxy[0];
        x1 = t.sxy[1];
        x2 = t.sxy[2];
        mac0 = ngte_nclip(x0, x1, x2);
        AT32(scr2 + 0x30) = x0;
        AT32(scr2 + 0x34) = x1;
        AT32(scr2 + 0x38) = x2;
        AT32(scr2 + 0x40) = x0;
        z0 = t.sz[0];
        z1 = t.sz[1];
        z2 = t.sz[2];
#else
        x0 = MFC2(12);
        x1 = MFC2(13);
        x2 = MFC2(14);
        NCLIP();
        AT32(scr2 + 0x30) = x0;
        AT32(scr2 + 0x34) = x1;
        AT32(scr2 + 0x38) = x2;
        AT32(scr2 + 0x40) = x0;
        z0 = MFC2(17);
        z1 = MFC2(18);
        z2 = MFC2(19);
        mac0 = (s32)MFC2(24);
#endif
        AT32(scr2 + 0x44) = z0;
        AT32(scr2 + 0x48) = z1;
        if (mac0 < 0)
          continue; /* facing away */
        AT32(scr2 + 0x4C) = z2;
        AT32(scr2 + 0x54) = z0;
#if NGTE_MATH
        corner = ngte_rtps_w(&P, negsz, 0u);
        anchor.facing = 1;
#else
        MTC2(negsz, 0);
        MTC2(0u, 1);
        RTPS();
#endif
      }
      /* the centre on screen */
      if ((s32)csxy <= 0)
        continue;
      if ((s32)(csxy - 0xF00000u) >= 0)
        continue;
      {
        const s32 cxs = (s32)(csxy << 16) >> 16;
        if (cxs + 8 <= 0 || cxs - 0x208 >= 0)
          continue;
      }
      {
        /* the fourth corner: SXY2, SZ3 (+0x3C, +0x50), so the quad's four
         * corners are +0x30..+0x3C with depths +0x44..+0x50 */
        u32 v, w, q;
#if NGTE_MATH
        AT32(scr2 + 0x3C) = corner.sxy;
        AT32(scr2 + 0x50) = corner.sz3;
        anchor.read_corner = 1;
#else
        AT32(scr2 + 0x3C) = MFC2(14);
        AT32(scr2 + 0x50) = MFC2(19);
#endif
        v = scr2 + 0x30;
        w = v + 0x10;
        /* near the camera, nudge each corner a line up or down by its
         * depth against the centre's */
        q = CFC2(7);
        if ((s32)(q + 0xFFFFFC00u) > 0) {
          u32 c = v;
          for (;;) {
            u32 zc, xy;
            if (c == w + 0x4)
              break;
            zc = AT32(c + 0x14);
            xy = AT32(c + 0x0);
            zc -= csz;
            c += 4;
            if ((s32)zc < 0) {
              AT32(c - 4) = xy + 0x10000u;
            } else if (zc != 0) {
              AT32(c - 4) = xy - 0x10000u;
            }
          }
        }
        /* the four triangles of the fan (corner i, corner i+1, centre) */
        for (;;) {
          u32 xa, xb, za, zb, depth;
          if (v == w)
            break;
          xa = AT32(v + 0x0);
          xb = AT32(v + 0x4);
          za = AT32(v + 0x14);
          zb = AT32(v + 0x18);
          v += 4;
          AT32(prim + 0x4) = rgb;
          AT32(prim + 0x8) = csxy;
          AT32(prim + 0xC) = uvc;
          AT32(prim + 0x14) = uvt;
          AT32(prim + 0x1C) = uvc + 0x1F00u;
          AT32(prim + 0x10) = xa;
          AT32(prim + 0x18) = xb;
          AT32(prim + 0x0) = 0x7000000u;
          za = za + zb;
          depth = za + (za >> 1) + csz;
          depth = (depth >> 7) - depth_bias;
          if ((s32)depth < 0)
            continue; /* not linked: the space is used again */
          {
            const u32 slot = ot + (depth << 3);
#if NGTE
            if (g_nd.on) {
              nd_append(ND_K_FT3, slot, prim);
              prim += 0x20;
              continue;
            }
#endif
            {
              const u32 tail = AT32(slot);
              AT32(slot) = prim;
              if (tail) {
                AT16(tail + 0x0) = (u16)prim;
                AT8(tail + 0x2) = (u8)(prim >> 16);
              } else {
                AT32(slot + 0x4) = prim;
              }
            }
          }
          prim += 0x20;
        }
        AT32(scr + 0x28) = prim;
        (void)x0, (void)x1, (void)x2, (void)z0, (void)z1, (void)z2;
      }
    }
  }
  AT32(ADDR_ND(D_800757B0)) = AT32(scr + 0x28);
#if NGTE_MATH
  MobyShadowsTail(scr, &anchor, anchored, nxy, nz, nnear);
#endif
#if NGTE
  nd_end();
#endif
  return 0;
}

HAND_RENDERER_UNIT(func_80059A48, SpyroShadow)
HAND_RENDERER_UNIT(func_80059F8C, MobyShadows)

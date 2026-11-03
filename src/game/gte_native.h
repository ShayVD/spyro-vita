/* GTE commands as native maths on C values (docs/native-plan.md stage 1,
 * decisions D80), for the hand-written renderers at fast_render=2.
 *
 * gte_inline.h runs each command on the global register file: it reads its
 * inputs from gteRegs, writes every result back (MAC, IR, the SZ/SXY/RGB
 * FIFOs, FLAG with each bit raised as it goes) and the code then reads back
 * the one or two it wanted. The functions here take the inputs as values and
 * return the results as values, and touch nothing else: they are pure and
 * always inlined, so the compiler drops whatever the caller doesn't read
 * (FLAG, IR3, MAC0...). The arithmetic is gte_inline.h's (itself PsyCross's
 * GTE_operator), expression for expression, with the same integer types, so
 * every result is the same: the saturation, the UNR division (gtei_divide),
 * the flags. tests/gte/test_gte_native compares every returned field with
 * the register file after gte_inline.h's command, on PCSX-Redux's vectors
 * and on random states.
 *
 * Only the opcodes the hand renderers issue are here, with their sf and lm
 * fixed as the opcode fixes them.
 *
 * The register file stays the record of the GTE's state between functions
 * (mips2c units, PsyCross's libgte and the decomp C still read and write
 * it). A renderer reads what it needs from it once (ngte_proj_get, ...),
 * runs the native commands, and before anything else looks at the register
 * file it writes back the exact state the register-file commands would have
 * left (render_hand.h's NGTE notes; the renderers' "sync" code). */
#ifndef GTE_NATIVE_H
#define GTE_NATIVE_H

#include "gte_inline.h"

#define NGTE_INL static inline __attribute__((always_inline))
#define NGTE_B31 (1u << 31)

/* The projection: rotation, translation, screen offset, H and the depth
 * cue, as RTPS reads them. */
typedef struct {
  int r11, r12, r13, r21, r22, r23, r31, r32, r33;
  int trx, try_, trz;
  int ofx, ofy;
  unsigned short h;
  int dqa, dqb;
} NgteProj;

NGTE_INL NgteProj ngte_proj_get(void) {
  NgteProj p;
  p.r11 = C2_R11, p.r12 = C2_R12, p.r13 = C2_R13;
  p.r21 = C2_R21, p.r22 = C2_R22, p.r23 = C2_R23;
  p.r31 = C2_R31, p.r32 = C2_R32, p.r33 = C2_R33;
  p.trx = C2_TRX, p.try_ = C2_TRY, p.trz = C2_TRZ;
  p.ofx = C2_OFX, p.ofy = C2_OFY;
  p.h = (unsigned short)C2_H;
  p.dqa = C2_DQA, p.dqb = C2_DQB;
  return p;
}

/* A vertex as V0 holds it: VX0 and VY0 are the halves of VXY0, VZ0 the low
 * half of its word. */
NGTE_INL int ngte_vx(unsigned int vxy) { return (short)vxy; }
NGTE_INL int ngte_vy(unsigned int vxy) { return (short)(vxy >> 16); }
NGTE_INL int ngte_vz(unsigned int vz) { return (short)vz; }

/* gte_inline.h's A1-A3 bounds and shift for sf = 1, with the flags local. */
NGTE_INL int ngte_a_sf1(long long a, unsigned int *flag, unsigned int max_bit, unsigned int min_bit) {
  if (a > (long long)0x7ffffffffff)
    *flag |= NGTE_B31 | max_bit;
  if (a < (long long)-0x8000000000)
    *flag |= NGTE_B31 | min_bit;
  return (int)(a >> 12);
}
NGTE_INL int ngte_a_sf0(long long a, unsigned int *flag, unsigned int max_bit, unsigned int min_bit) {
  if (a > (long long)0x7ffffffffff)
    *flag |= NGTE_B31 | max_bit;
  if (a < (long long)-0x8000000000)
    *flag |= NGTE_B31 | min_bit;
  return (int)a;
}
#define NGTE_A1(a, f, sf) ngte_a_sf##sf(a, f, 1u << 30, 1u << 27)
#define NGTE_A2(a, f, sf) ngte_a_sf##sf(a, f, 1u << 29, 1u << 26)
#define NGTE_A3(a, f, sf) ngte_a_sf##sf(a, f, 1u << 28, 1u << 25)

NGTE_INL int ngte_lim(int value, int max, int min, unsigned int bits, unsigned int *flag) {
  if (value > max) {
    *flag |= bits;
    return max;
  } else if (value < min) {
    *flag |= bits;
    return min;
  }
  return value;
}
/* B1-B3 with lm = 0 (-0x8000..0x7fff) or lm = 1 (0..0x7fff) */
#define NGTE_B1(a, f, lm) ngte_lim(a, 0x7fff, (lm) ? 0 : -0x8000, NGTE_B31 | (1u << 24), f)
#define NGTE_B2(a, f, lm) ngte_lim(a, 0x7fff, (lm) ? 0 : -0x8000, NGTE_B31 | (1u << 23), f)
#define NGTE_B3(a, f, lm) ngte_lim(a, 0x7fff, (lm) ? 0 : -0x8000, 1u << 22, f)
#define NGTE_C1(a, f) ngte_lim(a, 0xff, 0, 1u << 21, f)
#define NGTE_C2(a, f) ngte_lim(a, 0xff, 0, 1u << 20, f)
#define NGTE_C3(a, f) ngte_lim(a, 0xff, 0, 1u << 19, f)

/* gte_inline.h's F: the 32-bit overflow flags of a MAC0 sum. */
NGTE_INL long long ngte_f(long long a, unsigned int *flag) {
  if (a > 0x7fffffffLL)
    *flag |= NGTE_B31 | (1u << 16);
  if (a < -0x80000000LL)
    *flag |= NGTE_B31 | (1u << 15);
  return a;
}
/* G1/G2: a screen coordinate, -0x400..0x3ff */
NGTE_INL int ngte_g(long long a, unsigned int bits, unsigned int *flag) {
  if (a > 0x3ff) {
    *flag |= bits;
    return 0x3ff;
  }
  if (a < -0x400) {
    *flag |= bits;
    return -0x400;
  }
  return (int)a;
}

/* ===================================================================== */
/* RTPS (0x180001: sf 1, lm 0)                                            */
/* ===================================================================== */

typedef struct {
  int mac1, mac2, mac3;
  int ir1, ir2, ir3;
  unsigned int sz3;  /* the new SZ3, 0..0xFFFF */
  unsigned int sxy;  /* the new SXY2: x | y << 16 */
  int mac0, ir0;     /* the depth cue */
  unsigned int flag;
} NgteRtp;

NGTE_INL NgteRtp ngte_rtps(const NgteProj *p, int vx, int vy, int vz) {
  NgteRtp o;
  unsigned int flag = 0, hs;
  long long s3, f;
  int v12, sx, sy;
  o.mac1 = NGTE_A1((long long)((long long)p->trx << 12) + (p->r11 * vx) + (p->r12 * vy) + (p->r13 * vz), &flag, 1);
  o.mac2 = NGTE_A2((long long)((long long)p->try_ << 12) + (p->r21 * vx) + (p->r22 * vy) + (p->r23 * vz), &flag, 1);
  s3 = (long long)((long long)p->trz << 12) + (p->r31 * vx) + (p->r32 * vy) + (p->r33 * vz);
  o.mac3 = NGTE_A3(s3, &flag, 1);
  o.ir1 = NGTE_B1(o.mac1, &flag, 0);
  o.ir2 = NGTE_B2(o.mac2, &flag, 0);
  /* B3 with sf: the flag from the value >> 12, the clamp the same here */
  v12 = (int)(s3 >> 12);
  if (v12 < -0x8000 || v12 > 0x7fff)
    flag |= 1u << 22;
  o.ir3 = v12 > 0x7fff ? 0x7fff : v12 < -0x8000 ? -0x8000 : v12;
  /* D: SZ3 */
  o.sz3 = (unsigned int)ngte_lim(v12, 0xffff, 0, NGTE_B31 | (1u << 18), &flag);
  /* E: H / SZ3 */
  hs = gtei_divide(p->h, (unsigned short)o.sz3);
  if (hs == 0xffffffffu) {
    flag |= NGTE_B31 | (1u << 17);
    hs = 0x1ffff;
  } else if (hs > 0x1ffff) {
    hs = 0x1ffff;
  }
  f = ngte_f((long long)p->ofx + ((long long)o.ir1 * (int)hs), &flag);
  sx = ngte_g(f >> 16, NGTE_B31 | (1u << 14), &flag);
  f = ngte_f((long long)p->ofy + ((long long)o.ir2 * (int)hs), &flag);
  sy = ngte_g(f >> 16, NGTE_B31 | (1u << 13), &flag);
  o.sxy = (unsigned int)(unsigned short)sx | (unsigned int)sy << 16;
  /* MAC0, IR0: the depth cue */
  f = ngte_f((long long)p->dqb + ((long long)p->dqa * (int)hs), &flag);
  o.mac0 = (int)f;
  if ((f >> 12) < 0 || (f >> 12) > 0x1000)
    flag |= 1u << 12;
  v12 = (int)(f >> 12);
  o.ir0 = v12 > 0x1000 ? 0x1000 : v12 < 0 ? 0 : v12;
  o.flag = flag;
  return o;
}

/* The vertex cache (D39) while it records (subpixel, persp): every native
 * RTPS gives it the projection as values, as the register file's RTPS
 * gives it the registers (gte_ops.c, D89). */
void ngte_vcache(const NgteProj *p, unsigned int vxy, unsigned int vz, const NgteRtp *o);

/* RTPS on V0 as the words MTC2 puts there (VXY0, VZ0). */
NGTE_INL NgteRtp ngte_rtps_w(const NgteProj *p, unsigned int vxy, unsigned int vz) {
  const NgteRtp o = ngte_rtps(p, ngte_vx(vxy), ngte_vy(vxy), ngte_vz(vz));
  if (__builtin_expect(g_psyxRtpValueHook != 0, 0))
    ngte_vcache(p, vxy, vz, &o);
  return o;
}

/* ===================================================================== */
/* NCLIP (0x1400006): MAC0, the winding of SXY0-2; FLAG ends 0            */
/* ===================================================================== */

/* gte_inline.h sums the six products as 64 bits and keeps the low 32; the
 * same sum in wrapping 32-bit arithmetic has the same low 32 bits. */
NGTE_INL int ngte_nclip(unsigned int sxy0, unsigned int sxy1, unsigned int sxy2) {
  int x0 = (short)sxy0, y0 = (short)(sxy0 >> 16);
  int x1 = (short)sxy1, y1 = (short)(sxy1 >> 16);
  int x2 = (short)sxy2, y2 = (short)(sxy2 >> 16);
  return (int)((unsigned int)(x0 * y1) + (unsigned int)(x1 * y2) + (unsigned int)(x2 * y0) -
               (unsigned int)(x0 * y2) - (unsigned int)(x1 * y0) - (unsigned int)(x2 * y1));
}

/* ===================================================================== */
/* The colour commands: MAC1-3, IR1-3, the colour pushed as RGB2, FLAG    */
/* ===================================================================== */

typedef struct {
  int mac1, mac2, mac3;
  int ir1, ir2, ir3;
  unsigned int rgb2; /* the colour pushed: R | G << 8 | B << 16 | CODE << 24 */
  unsigned int flag;
} NgteCol;

/* gtei_ir_from_mac (lm 0) and gtei_rgb_push, for CODE */
NGTE_INL void ngte_col_finish(NgteCol *o, unsigned int code, unsigned int *flag) {
  unsigned int r, g, b;
  o->ir1 = NGTE_B1(o->mac1, flag, 0);
  o->ir2 = NGTE_B2(o->mac2, flag, 0);
  o->ir3 = NGTE_B3(o->mac3, flag, 0);
  r = (unsigned int)NGTE_C1(o->mac1 >> 4, flag);
  g = (unsigned int)NGTE_C2(o->mac2 >> 4, flag);
  b = (unsigned int)NGTE_C3(o->mac3 >> 4, flag);
  o->rgb2 = r | g << 8 | b << 16 | (code & 0xff) << 24;
  o->flag = *flag;
}

/* DPCS (0x780010: sf 1, lm 0): RGBC's colour (r, g, b, code) towards the
 * far colour (RFC/GFC/BFC) by IR0. */
NGTE_INL NgteCol ngte_dpcs(unsigned int rgbc, int ir0, int rfc, int gfc, int bfc) {
  NgteCol o;
  unsigned int flag = 0;
  const int r = rgbc & 0xff, g = (rgbc >> 8) & 0xff, b = (rgbc >> 16) & 0xff;
  o.mac1 = NGTE_A1((r << 16) + (ir0 * NGTE_B1(NGTE_A1(((long long)rfc << 12) - (r << 16), &flag, 1), &flag, 0)), &flag, 1);
  o.mac2 = NGTE_A2((g << 16) + (ir0 * NGTE_B2(NGTE_A2(((long long)gfc << 12) - (g << 16), &flag, 1), &flag, 0)), &flag, 1);
  o.mac3 = NGTE_A3((b << 16) + (ir0 * NGTE_B3(NGTE_A3(((long long)bfc << 12) - (b << 16), &flag, 1), &flag, 0)), &flag, 1);
  ngte_col_finish(&o, rgbc >> 24, &flag);
  return o;
}

/* INTPL (0x980011: sf 1, lm 0): IR1-3 towards the far colour by IR0. */
NGTE_INL NgteCol ngte_intpl(int ir1, int ir2, int ir3, int ir0, int rfc, int gfc, int bfc, unsigned int code) {
  NgteCol o;
  unsigned int flag = 0;
  o.mac1 = NGTE_A1((ir1 << 12) + (ir0 * NGTE_B1(NGTE_A1(((long long)rfc << 12) - (ir1 << 12), &flag, 1), &flag, 0)), &flag, 1);
  o.mac2 = NGTE_A2((ir2 << 12) + (ir0 * NGTE_B2(NGTE_A2(((long long)gfc << 12) - (ir2 << 12), &flag, 1), &flag, 0)), &flag, 1);
  o.mac3 = NGTE_A3((ir3 << 12) + (ir0 * NGTE_B3(NGTE_A3(((long long)bfc << 12) - (ir3 << 12), &flag, 1), &flag, 0)), &flag, 1);
  ngte_col_finish(&o, code, &flag);
  return o;
}

/* GPF (0x190003D: sf 0, lm 0): IR1-3 times IR0. */
NGTE_INL NgteCol ngte_gpf0(int ir1, int ir2, int ir3, int ir0, unsigned int code) {
  NgteCol o;
  unsigned int flag = 0;
  o.mac1 = NGTE_A1(ir0 * ir1, &flag, 0);
  o.mac2 = NGTE_A2(ir0 * ir2, &flag, 0);
  o.mac3 = NGTE_A3(ir0 * ir3, &flag, 0);
  ngte_col_finish(&o, code, &flag);
  return o;
}

/* ===================================================================== */
/* MVMVA with sf 1, lm 0: a matrix times a vector, plus a translation      */
/* ===================================================================== */

typedef struct {
  int mac1, mac2, mac3;
  int ir1, ir2, ir3;
  unsigned int flag;
} NgteVec;

/* The rotation matrix and the translation, as MVMVA reads them (mx 0). */
typedef struct {
  int m11, m12, m13, m21, m22, m23, m31, m32, m33;
} NgteMat;

NGTE_INL NgteMat ngte_rot_get(void) {
  NgteMat m;
  m.m11 = C2_R11, m.m12 = C2_R12, m.m13 = C2_R13;
  m.m21 = C2_R21, m.m22 = C2_R22, m.m23 = C2_R23;
  m.m31 = C2_R31, m.m32 = C2_R32, m.m33 = C2_R33;
  return m;
}

/* MVMVA sf 1 lm 0, cv 0..1 (t1-t3 the translation, 0 for none) */
NGTE_INL NgteVec ngte_mvmva(const NgteMat *m, int t1, int t2, int t3, int vx, int vy, int vz) {
  NgteVec o;
  unsigned int flag = 0;
  o.mac1 = NGTE_A1((long long)((long long)t1 << 12) + (m->m11 * vx) + (m->m12 * vy) + (m->m13 * vz), &flag, 1);
  o.mac2 = NGTE_A2((long long)((long long)t2 << 12) + (m->m21 * vx) + (m->m22 * vy) + (m->m23 * vz), &flag, 1);
  o.mac3 = NGTE_A3((long long)((long long)t3 << 12) + (m->m31 * vx) + (m->m32 * vy) + (m->m33 * vz), &flag, 1);
  o.ir1 = NGTE_B1(o.mac1, &flag, 0);
  o.ir2 = NGTE_B2(o.mac2, &flag, 0);
  o.ir3 = NGTE_B3(o.mac3, &flag, 0);
  o.flag = flag;
  return o;
}

#endif

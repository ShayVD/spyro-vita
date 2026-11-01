/* GTE commands with a constant opcode, inline (fps plan C5, decisions D72).
 *
 * The translated renderers and collision code issue every GTE command as
 * GTE_operator(0x...) with a constant instruction word. PsyCross's
 * GTE_operator (src/gte/PsyX_GTE.cpp) decodes sf/lm/mx/v/cv and switches on
 * the function at run time, and calls out-of-line helpers (A1-A3, Lm_*, F,
 * LIM) for every result. m2c_gte_op below is the same arithmetic,
 * expression for expression, as static inline C: with a constant opcode the
 * decode and the switch fold away and the command is straight-line code.
 * Each opcode the translated code uses gets one such function,
 * m2c_gte_<opcode> (gte_ops.c; NCLIP, the smallest, inline here), which
 * mips2c_rt.h's GTE_operator calls by token pasting: inlining every command
 * at every site made GCC run out of memory on the largest units, and one
 * call per command costs little next to the command. The results are
 * bit-identical by construction (same expressions, same integer types and
 * promotions, same FLAG bits); tests/gte checks it against PCSX-Redux's
 * vectors and against GTE_operator itself on random register states.
 *
 * Only the commands' arithmetic is here. The register moves are in
 * mips2c_rt.h (m2c_mtc2 and kin); PsyCross's own state is the global
 * gteRegs, which both paths share, so the two can be mixed freely
 * (PsyCross's file-static m_sf/m_mac0/m_mac3 are set and read within one
 * command, never across commands). */
#ifndef GTE_INLINE_H
#define GTE_INLINE_H

#include <gtereg.h>

#define GTEI_INL static inline __attribute__((always_inline))

/* PsyX_GTE.cpp's LIM: clamp and raise FLAG bits. */
GTEI_INL int gtei_lim(int value, int max, int min, unsigned int flag) {
  if (value > max) {
    C2_FLAG |= flag;
    return max;
  } else if (value < min) {
    C2_FLAG |= flag;
    return min;
  }
  return value;
}

GTEI_INL long long gtei_shift(long long a, int sf) {
  if (sf > 0)
    return a >> 12;
  else if (sf < 0)
    return a << 12;
  return a;
}

GTEI_INL int gtei_bounds(long long value, int max_flag, int min_flag, int sf) {
  if (value > (long long)0x7ffffffffff)
    C2_FLAG |= max_flag;
  if (value < (long long)-0x8000000000)
    C2_FLAG |= min_flag;
  return (int)gtei_shift(value, sf);
}

#define GTEI_B31 ((int)(1u << 31))
GTEI_INL int gtei_a1(long long a, int sf) { return gtei_bounds(a, GTEI_B31 | (1 << 30), GTEI_B31 | (1 << 27), sf); }
GTEI_INL int gtei_a2(long long a, int sf) { return gtei_bounds(a, GTEI_B31 | (1 << 29), GTEI_B31 | (1 << 26), sf); }
/* A3 also keeps the unshifted value (PsyCross's m_mac3) for Lm_B3_sf/Lm_D. */
GTEI_INL int gtei_a3(long long a, int sf, long long *mac3) {
  *mac3 = a;
  return gtei_bounds(a, GTEI_B31 | (1 << 28), GTEI_B31 | (1 << 25), sf);
}
GTEI_INL int gtei_b1(int a, int lm) { return gtei_lim(a, 0x7fff, -0x8000 * !lm, (1u << 31) | (1 << 24)); }
GTEI_INL int gtei_b2(int a, int lm) { return gtei_lim(a, 0x7fff, -0x8000 * !lm, (1u << 31) | (1 << 23)); }
GTEI_INL int gtei_b3(int a, int lm) { return gtei_lim(a, 0x7fff, -0x8000 * !lm, (1 << 22)); }
GTEI_INL int gtei_b3_sf(long long value, int sf, int lm) {
  int value_sf = (int)gtei_shift(value, sf);
  int value_12 = (int)gtei_shift(value, 1);
  int max = 0x7fff;
  int min = 0;
  if (lm == 0)
    min = -0x8000;
  if (value_12 < -0x8000 || value_12 > 0x7fff)
    C2_FLAG |= (1 << 22);
  if (value_sf > max)
    return max;
  else if (value_sf < min)
    return min;
  return value_sf;
}
GTEI_INL int gtei_c1(int a) { return gtei_lim(a, 0x00ff, 0x0000, (1 << 21)); }
GTEI_INL int gtei_c2(int a) { return gtei_lim(a, 0x00ff, 0x0000, (1 << 20)); }
GTEI_INL int gtei_c3(int a) { return gtei_lim(a, 0x00ff, 0x0000, (1 << 19)); }
GTEI_INL int gtei_d(long long a, int sf) {
  return gtei_lim((int)gtei_shift(a, sf), 0xffff, 0x0000, (1u << 31) | (1 << 18));
}
GTEI_INL unsigned int gtei_e(unsigned int result) {
  if (result == 0xffffffff) {
    C2_FLAG |= (1u << 31) | (1 << 17);
    return 0x1ffff;
  }
  if (result > 0x1ffff)
    return 0x1ffff;
  return result;
}
/* F also keeps the value (PsyCross's m_mac0) for Lm_H and AVSZ's Lm_D. */
GTEI_INL long long gtei_f(long long a, long long *mac0) {
  *mac0 = a;
  if (a > 0x7fffffffLL)
    C2_FLAG |= (1u << 31) | (1 << 16);
  if (a < -0x80000000LL)
    C2_FLAG |= (1u << 31) | (1 << 15);
  return a;
}
GTEI_INL int gtei_g1(long long a) {
  if (a > 0x3ff) {
    C2_FLAG |= (1u << 31) | (1 << 14);
    return 0x3ff;
  }
  if (a < -0x400) {
    C2_FLAG |= (1u << 31) | (1 << 14);
    return -0x400;
  }
  return (int)a;
}
GTEI_INL int gtei_g2(long long a) {
  if (a > 0x3ff) {
    C2_FLAG |= (1u << 31) | (1 << 13);
    return 0x3ff;
  }
  if (a < -0x400) {
    C2_FLAG |= (1u << 31) | (1 << 13);
    return -0x400;
  }
  return (int)a;
}
GTEI_INL int gtei_h(long long value, int sf) {
  long long value_sf = gtei_shift(value, sf);
  int value_12 = (int)gtei_shift(value, 1);
  int max = 0x1000;
  int min = 0x0000;
  if (value_sf < min || value_sf > max)
    C2_FLAG |= (1 << 12);
  if (value_12 > max)
    return max;
  if (value_12 < min)
    return min;
  return value_12;
}

/* PsyCross's gte_leadingzerocount for a non-zero value is clz, and
 * gte_divide only uses it on a non-zero denominator (tests/gte checks all
 * 65,535 of them). */
GTEI_INL unsigned int gtei_divide(unsigned short numerator, unsigned short denominator) {
  if (numerator < (denominator * 2)) {
    static const unsigned char table[] = {
        0xff, 0xfd, 0xfb, 0xf9, 0xf7, 0xf5, 0xf3, 0xf1, 0xef, 0xee, 0xec, 0xea, 0xe8, 0xe6, 0xe4, 0xe3,
        0xe1, 0xdf, 0xdd, 0xdc, 0xda, 0xd8, 0xd6, 0xd5, 0xd3, 0xd1, 0xd0, 0xce, 0xcd, 0xcb, 0xc9, 0xc8,
        0xc6, 0xc5, 0xc3, 0xc1, 0xc0, 0xbe, 0xbd, 0xbb, 0xba, 0xb8, 0xb7, 0xb5, 0xb4, 0xb2, 0xb1, 0xb0,
        0xae, 0xad, 0xab, 0xaa, 0xa9, 0xa7, 0xa6, 0xa4, 0xa3, 0xa2, 0xa0, 0x9f, 0x9e, 0x9c, 0x9b, 0x9a,
        0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8f, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x87, 0x86,
        0x85, 0x84, 0x83, 0x82, 0x81, 0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x75, 0x74,
        0x73, 0x72, 0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
        0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x5d, 0x5c, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55,
        0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e, 0x4d, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48, 0x48,
        0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3f, 0x3f, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b,
        0x3a, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2f,
        0x2e, 0x2e, 0x2d, 0x2c, 0x2c, 0x2b, 0x2a, 0x2a, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
        0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a,
        0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
        0x10, 0x0f, 0x0f, 0x0e, 0x0e, 0x0d, 0x0d, 0x0c, 0x0c, 0x0b, 0x0a, 0x0a, 0x09, 0x09, 0x08, 0x08,
        0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
        0x00};
    int shift = (int)__builtin_clz((unsigned int)denominator) - 16;
    int r1 = (denominator << shift) & 0x7fff;
    int r2 = table[((r1 + 0x40) >> 7)] + 0x101;
    int r3 = ((0x80 - (r2 * (r1 + 0x8000))) >> 8) & 0x1ffff;
    unsigned int reciprocal = ((r2 * r3) + 0x80) >> 8;
    return (unsigned int)((((unsigned long long)reciprocal * (numerator << shift)) + 0x8000) >> 16);
  }
  return 0xffffffff;
}

/* PsyX_GTE.cpp's operand selectors, with constant n. */
#define GTEI_VX(n) ((n) < 3 ? gteRegs.CP2D.p[(n) << 1].sw.l : C2_IR1)
#define GTEI_VY(n) ((n) < 3 ? gteRegs.CP2D.p[(n) << 1].sw.h : C2_IR2)
#define GTEI_VZ(n) ((n) < 3 ? gteRegs.CP2D.p[((n) << 1) + 1].sw.l : C2_IR3)
#define GTEI_MX11(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3)].sw.l : -C2_R << 4)
#define GTEI_MX12(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3)].sw.h : C2_R << 4)
#define GTEI_MX13(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 1].sw.l : C2_IR0)
#define GTEI_MX21(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 1].sw.h : C2_R13)
#define GTEI_MX22(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 2].sw.l : C2_R13)
#define GTEI_MX23(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 2].sw.h : C2_R13)
#define GTEI_MX31(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 3].sw.l : C2_R22)
#define GTEI_MX32(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 3].sw.h : C2_R22)
#define GTEI_MX33(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 4].sw.l : C2_R22)
#define GTEI_CV1(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 5].sd : 0)
#define GTEI_CV2(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 6].sd : 0)
#define GTEI_CV3(n) ((n) < 3 ? gteRegs.CP2C.p[((n) << 3) + 7].sd : 0)

/* GTE_RotTransPers: one vertex of RTPS/RTPT. */
GTEI_INL int gtei_rtp(int idx, int sf, int lm, long long *mac0) {
  long long mac3;
  int h_over_sz3;
  C2_MAC1 = gtei_a1((long long)((long long)C2_TRX << 12) + (C2_R11 * GTEI_VX(idx)) + (C2_R12 * GTEI_VY(idx)) + (C2_R13 * GTEI_VZ(idx)), sf);
  C2_MAC2 = gtei_a2((long long)((long long)C2_TRY << 12) + (C2_R21 * GTEI_VX(idx)) + (C2_R22 * GTEI_VY(idx)) + (C2_R23 * GTEI_VZ(idx)), sf);
  C2_MAC3 = gtei_a3((long long)((long long)C2_TRZ << 12) + (C2_R31 * GTEI_VX(idx)) + (C2_R32 * GTEI_VY(idx)) + (C2_R33 * GTEI_VZ(idx)), sf, &mac3);
  C2_IR1 = gtei_b1(C2_MAC1, lm);
  C2_IR2 = gtei_b2(C2_MAC2, lm);
  C2_IR3 = gtei_b3_sf(mac3, sf, lm);
  C2_SZ0 = C2_SZ1;
  C2_SZ1 = C2_SZ2;
  C2_SZ2 = C2_SZ3;
  C2_SZ3 = gtei_d(mac3, 1);
  h_over_sz3 = gtei_e(gtei_divide(C2_H, C2_SZ3));
  C2_SXY0 = C2_SXY1;
  C2_SXY1 = C2_SXY2;
  C2_SX2 = gtei_g1(gtei_f((long long)C2_OFX + ((long long)C2_IR1 * h_over_sz3), mac0) >> 16);
  C2_SY2 = gtei_g2(gtei_f((long long)C2_OFY + ((long long)C2_IR2 * h_over_sz3), mac0) >> 16);
  return h_over_sz3;
}

/* The colour FIFO push at the end of the colour commands. */
GTEI_INL void gtei_rgb_push(void) {
  C2_RGB0 = C2_RGB1;
  C2_RGB1 = C2_RGB2;
  C2_CD2 = C2_CODE;
  C2_R2 = gtei_c1(C2_MAC1 >> 4);
  C2_G2 = gtei_c2(C2_MAC2 >> 4);
  C2_B2 = gtei_c3(C2_MAC3 >> 4);
}

GTEI_INL void gtei_ir_from_mac(int lm) {
  C2_IR1 = gtei_b1(C2_MAC1, lm);
  C2_IR2 = gtei_b2(C2_MAC2, lm);
  C2_IR3 = gtei_b3(C2_MAC3, lm);
}

/* Light matrix times vertex n, then the light colour matrix plus the
 * background colour (the first two stages of NCS/NCDS/NCCS). */
GTEI_INL void gtei_light(int v, int sf, int lm, long long *mac3) {
  C2_MAC1 = gtei_a1((long long)(C2_L11 * GTEI_VX(v)) + (C2_L12 * GTEI_VY(v)) + (C2_L13 * GTEI_VZ(v)), sf);
  C2_MAC2 = gtei_a2((long long)(C2_L21 * GTEI_VX(v)) + (C2_L22 * GTEI_VY(v)) + (C2_L23 * GTEI_VZ(v)), sf);
  C2_MAC3 = gtei_a3((long long)(C2_L31 * GTEI_VX(v)) + (C2_L32 * GTEI_VY(v)) + (C2_L33 * GTEI_VZ(v)), sf, mac3);
  gtei_ir_from_mac(lm);
}
GTEI_INL void gtei_colour_matrix(int sf, int lm, long long *mac3) {
  C2_MAC1 = gtei_a1((long long)((long long)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3), sf);
  C2_MAC2 = gtei_a2((long long)((long long)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3), sf);
  C2_MAC3 = gtei_a3((long long)((long long)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3), sf, mac3);
  gtei_ir_from_mac(lm);
}
/* (R,G,B) << 4 times IR, depth-cued towards the far colour (NCDS, CDP, DCPL). */
GTEI_INL void gtei_depth_cue_rgb_ir(int sf, int lm, long long *mac3) {
  C2_MAC1 = gtei_a1(((C2_R << 4) * C2_IR1) + (C2_IR0 * gtei_b1(gtei_a1(((long long)C2_RFC << 12) - ((C2_R << 4) * C2_IR1), sf), 0)), sf);
  C2_MAC2 = gtei_a2(((C2_G << 4) * C2_IR2) + (C2_IR0 * gtei_b2(gtei_a2(((long long)C2_GFC << 12) - ((C2_G << 4) * C2_IR2), sf), 0)), sf);
  C2_MAC3 = gtei_a3(((C2_B << 4) * C2_IR3) + (C2_IR0 * gtei_b3(gtei_a3(((long long)C2_BFC << 12) - ((C2_B << 4) * C2_IR3), sf, mac3), 0)), sf, mac3);
  gtei_ir_from_mac(lm);
}
/* (R,G,B) << 4 times IR (NCCS, CC). */
GTEI_INL void gtei_rgb_ir(int sf, int lm, long long *mac3) {
  C2_MAC1 = gtei_a1((C2_R << 4) * C2_IR1, sf);
  C2_MAC2 = gtei_a2((C2_G << 4) * C2_IR2, sf);
  C2_MAC3 = gtei_a3((C2_B << 4) * C2_IR3, sf, mac3);
  gtei_ir_from_mac(lm);
}

/* GTE_operator(op) for a constant op: the same results, no decode. */
GTEI_INL int m2c_gte_op(const unsigned int op_) {
  const unsigned int op = op_ & 0x1ffffff;
  const int lm = (op >> 10) & 1;
  const int sf = (op >> 19) & 1;
  const int mx = (op >> 17) & 3;
  const int vv = (op >> 15) & 3;
  const int cv = (op >> 13) & 3;
  long long mac0, mac3;
  int v, h_over_sz3;
  (void)mac3;

  C2_FLAG = 0;

  switch (op & 63) {
  case 0x00:
  case 0x01: /* RTPS */
    h_over_sz3 = gtei_rtp(0, sf, lm, &mac0);
    C2_MAC0 = (int)(gtei_f((long long)C2_DQB + ((long long)C2_DQA * h_over_sz3), &mac0));
    C2_IR0 = gtei_h(mac0, 1);
    return 1;

  case 0x06: /* NCLIP */
    C2_MAC0 = (int)(gtei_f((long long)(C2_SX0 * C2_SY1) + (C2_SX1 * C2_SY2) + (C2_SX2 * C2_SY0) - (C2_SX0 * C2_SY2) - (C2_SX1 * C2_SY0) - (C2_SX2 * C2_SY1), &mac0));
    C2_FLAG = 0;
    return 1;

  case 0x0c: /* OP */
    C2_MAC1 = gtei_a1((long long)(C2_R22 * C2_IR3) - (C2_R33 * C2_IR2), sf);
    C2_MAC2 = gtei_a2((long long)(C2_R33 * C2_IR1) - (C2_R11 * C2_IR3), sf);
    C2_MAC3 = gtei_a3((long long)(C2_R11 * C2_IR2) - (C2_R22 * C2_IR1), sf, &mac3);
    gtei_ir_from_mac(lm);
    return 1;

  case 0x10: /* DPCS */
    C2_MAC1 = gtei_a1((C2_R << 16) + (C2_IR0 * gtei_b1(gtei_a1(((long long)C2_RFC << 12) - (C2_R << 16), sf), 0)), sf);
    C2_MAC2 = gtei_a2((C2_G << 16) + (C2_IR0 * gtei_b2(gtei_a2(((long long)C2_GFC << 12) - (C2_G << 16), sf), 0)), sf);
    C2_MAC3 = gtei_a3((C2_B << 16) + (C2_IR0 * gtei_b3(gtei_a3(((long long)C2_BFC << 12) - (C2_B << 16), sf, &mac3), 0)), sf, &mac3);
    gtei_ir_from_mac(lm);
    gtei_rgb_push();
    return 1;

  case 0x11: /* INTPL */
    C2_MAC1 = gtei_a1((C2_IR1 << 12) + (C2_IR0 * gtei_b1(gtei_a1(((long long)C2_RFC << 12) - (C2_IR1 << 12), sf), 0)), sf);
    C2_MAC2 = gtei_a2((C2_IR2 << 12) + (C2_IR0 * gtei_b2(gtei_a2(((long long)C2_GFC << 12) - (C2_IR2 << 12), sf), 0)), sf);
    C2_MAC3 = gtei_a3((C2_IR3 << 12) + (C2_IR0 * gtei_b3(gtei_a3(((long long)C2_BFC << 12) - (C2_IR3 << 12), sf, &mac3), 0)), sf, &mac3);
    gtei_ir_from_mac(lm);
    gtei_rgb_push();
    return 1;

  case 0x12: /* MVMVA */
    if (cv == 2) {
      C2_MAC1 = gtei_a1((long long)(GTEI_MX12(mx) * GTEI_VY(vv)) + (GTEI_MX13(mx) * GTEI_VZ(vv)), sf);
      C2_MAC2 = gtei_a2((long long)(GTEI_MX22(mx) * GTEI_VY(vv)) + (GTEI_MX23(mx) * GTEI_VZ(vv)), sf);
      C2_MAC3 = gtei_a3((long long)(GTEI_MX32(mx) * GTEI_VY(vv)) + (GTEI_MX33(mx) * GTEI_VZ(vv)), sf, &mac3);
      gtei_b1(gtei_a1(((long long)GTEI_CV1(cv) << 12) + (GTEI_MX11(mx) * GTEI_VX(vv)), sf), 0);
      gtei_b2(gtei_a2(((long long)GTEI_CV2(cv) << 12) + (GTEI_MX21(mx) * GTEI_VX(vv)), sf), 0);
      gtei_b3(gtei_a3(((long long)GTEI_CV3(cv) << 12) + (GTEI_MX31(mx) * GTEI_VX(vv)), sf, &mac3), 0);
    } else {
      C2_MAC1 = gtei_a1((long long)((long long)GTEI_CV1(cv) << 12) + (GTEI_MX11(mx) * GTEI_VX(vv)) + (GTEI_MX12(mx) * GTEI_VY(vv)) + (GTEI_MX13(mx) * GTEI_VZ(vv)), sf);
      C2_MAC2 = gtei_a2((long long)((long long)GTEI_CV2(cv) << 12) + (GTEI_MX21(mx) * GTEI_VX(vv)) + (GTEI_MX22(mx) * GTEI_VY(vv)) + (GTEI_MX23(mx) * GTEI_VZ(vv)), sf);
      C2_MAC3 = gtei_a3((long long)((long long)GTEI_CV3(cv) << 12) + (GTEI_MX31(mx) * GTEI_VX(vv)) + (GTEI_MX32(mx) * GTEI_VY(vv)) + (GTEI_MX33(mx) * GTEI_VZ(vv)), sf, &mac3);
    }
    gtei_ir_from_mac(lm);
    return 1;

  case 0x13: /* NCDS */
    gtei_light(0, sf, lm, &mac3);
    gtei_colour_matrix(sf, lm, &mac3);
    gtei_depth_cue_rgb_ir(sf, lm, &mac3);
    gtei_rgb_push();
    return 1;

  case 0x14: /* CDP */
    gtei_colour_matrix(sf, lm, &mac3);
    gtei_depth_cue_rgb_ir(sf, lm, &mac3);
    gtei_rgb_push();
    return 1;

  case 0x16: /* NCDT */
    for (v = 0; v < 3; v++) {
      gtei_light(v, sf, lm, &mac3);
      gtei_colour_matrix(sf, lm, &mac3);
      gtei_depth_cue_rgb_ir(sf, lm, &mac3);
      gtei_rgb_push();
    }
    return 1;

  case 0x1b: /* NCCS */
    gtei_light(0, sf, lm, &mac3);
    gtei_colour_matrix(sf, lm, &mac3);
    gtei_rgb_ir(sf, lm, &mac3);
    gtei_rgb_push();
    return 1;

  case 0x1c: /* CC */
    gtei_colour_matrix(sf, lm, &mac3);
    gtei_rgb_ir(sf, lm, &mac3);
    gtei_rgb_push();
    return 1;

  case 0x1e: /* NCS */
    gtei_light(0, sf, lm, &mac3);
    gtei_colour_matrix(sf, lm, &mac3);
    gtei_rgb_push();
    return 1;

  case 0x20: /* NCT */
    for (v = 0; v < 3; v++) {
      gtei_light(v, sf, lm, &mac3);
      gtei_colour_matrix(sf, lm, &mac3);
      gtei_rgb_push();
    }
    return 1;

  case 0x28: /* SQR */
    C2_MAC1 = gtei_a1(C2_IR1 * C2_IR1, sf);
    C2_MAC2 = gtei_a2(C2_IR2 * C2_IR2, sf);
    C2_MAC3 = gtei_a3(C2_IR3 * C2_IR3, sf, &mac3);
    gtei_ir_from_mac(lm);
    return 1;

  case 0x29: /* DCPL */
    gtei_depth_cue_rgb_ir(sf, lm, &mac3);
    gtei_rgb_push();
    return 1;

  case 0x2a: /* DPCT */
    for (v = 0; v < 3; v++) {
      C2_MAC1 = gtei_a1((C2_R0 << 16) + (C2_IR0 * gtei_b1(gtei_a1(((long long)C2_RFC << 12) - (C2_R0 << 16), sf), 0)), sf);
      C2_MAC2 = gtei_a2((C2_G0 << 16) + (C2_IR0 * gtei_b2(gtei_a2(((long long)C2_GFC << 12) - (C2_G0 << 16), sf), 0)), sf);
      C2_MAC3 = gtei_a3((C2_B0 << 16) + (C2_IR0 * gtei_b3(gtei_a3(((long long)C2_BFC << 12) - (C2_B0 << 16), sf, &mac3), 0)), sf, &mac3);
      gtei_ir_from_mac(lm);
      gtei_rgb_push();
    }
    return 1;

  case 0x2d: /* AVSZ3 */
    C2_MAC0 = (int)(gtei_f((long long)(C2_ZSF3 * C2_SZ1) + (C2_ZSF3 * C2_SZ2) + (C2_ZSF3 * C2_SZ3), &mac0));
    C2_OTZ = gtei_d(mac0, 1);
    return 1;

  case 0x2e: /* AVSZ4 */
    C2_MAC0 = (int)(gtei_f((long long)(C2_ZSF4 * C2_SZ0) + (C2_ZSF4 * C2_SZ1) + (C2_ZSF4 * C2_SZ2) + (C2_ZSF4 * C2_SZ3), &mac0));
    C2_OTZ = gtei_d(mac0, 1);
    return 1;

  case 0x30: /* RTPT */
    h_over_sz3 = gtei_rtp(0, sf, lm, &mac0);
    h_over_sz3 = gtei_rtp(1, sf, lm, &mac0);
    h_over_sz3 = gtei_rtp(2, sf, lm, &mac0);
    C2_MAC0 = (int)(gtei_f((long long)C2_DQB + ((long long)C2_DQA * h_over_sz3), &mac0));
    C2_IR0 = gtei_h(mac0, 1);
    return 1;

  case 0x3d: /* GPF */
    C2_MAC1 = gtei_a1(C2_IR0 * C2_IR1, sf);
    C2_MAC2 = gtei_a2(C2_IR0 * C2_IR2, sf);
    C2_MAC3 = gtei_a3(C2_IR0 * C2_IR3, sf, &mac3);
    gtei_ir_from_mac(lm);
    gtei_rgb_push();
    return 1;

  case 0x3e: /* GPL */
    C2_MAC1 = gtei_a1(gtei_shift(C2_MAC1, -sf) + (C2_IR0 * C2_IR1), sf);
    C2_MAC2 = gtei_a2(gtei_shift(C2_MAC2, -sf) + (C2_IR0 * C2_IR2), sf);
    C2_MAC3 = gtei_a3(gtei_shift(C2_MAC3, -sf) + (C2_IR0 * C2_IR3), sf, &mac3);
    gtei_ir_from_mac(lm);
    gtei_rgb_push();
    return 1;

  case 0x3f: /* NCCT */
    for (v = 0; v < 3; v++) {
      gtei_light(v, sf, lm, &mac3);
      gtei_colour_matrix(sf, lm, &mac3);
      gtei_rgb_ir(sf, lm, &mac3);
      gtei_rgb_push();
    }
    return 1;
  }
  return 0;
}

/* Every constant opcode in the translated units (grep GTE_operator in the
 * build's gen/mips2c/*.c): a missing one is a link error, never a silent
 * fallback. */
#define M2C_GTE_OPS(X)                                                         \
  X(0x138041C) X(0x170000C) X(0x180001) X(0x190003D) X(0x198003D)             \
  X(0x1A0003E) X(0x1A8003E) X(0x280030) X(0x41E012) X(0x43E012) X(0x45E012)  \
  X(0x480012) X(0x486012) X(0x49E012) X(0x780010) X(0x980011) X(0xA00428)
#define M2C_GTE_DECLARE(op) void m2c_gte_##op(void);
M2C_GTE_OPS(M2C_GTE_DECLARE)
/* NCLIP (sf 1, lm 0), the most frequent and smallest: inline */
GTEI_INL void m2c_gte_0x1400006(void) { m2c_gte_op(0x1400006); }

#endif

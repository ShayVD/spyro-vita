/* The GTE register file and its moves (docs/native-plan.md stage 8, step 8c,
 * decisions D116): the machine model the B2 hand C, the mips2c units and
 * libgte.c share. Derived from PsyCross (MIT, THIRD_PARTY_NOTICES.md):
 * gteRegs is PsyX_GTE.cpp's, and MTC2, MFC2, CTC2 and CFC2 do what
 * src/psx/INLINE_C.C's do, register for register, including the raw
 * storage they leave (an MFC2 of a 16-bit register sign- or zero-extends it
 * in place). tests/gte/test_libgte.c compares every register's moves with
 * PsyCross's on random states. mips2c_rt.h inlines the plain registers'
 * moves and calls these for the rest (IRGB/ORGB, LZCS/LZCR, FLAG). */
#include <gtereg.h>

GTERegisters gteRegs;

/* PsyX_GTE.cpp's gte_leadingzerocount: the leading zeros (not the leading
 * sign bits a real GTE counts), 32 for 0. */
static unsigned int leading_zeros(unsigned int v) {
#if LIBGTE_MUTATE == 2 /* tests/gte's mutant: the real GTE's leading sign bits */
  if (v & 0x80000000u)
    v = ~v;
#endif
  return v ? (unsigned int)__builtin_clz(v) : 32u;
}

/* IR >> 7 clamped to 0..0x1F, as INLINE_C.C's MFC2 28/29 (LIM with no flag). */
static unsigned int ir_to_5(int ir) {
  ir >>= 7;
  return ir > 0x1F ? 0x1Fu : ir < 0 ? 0u : (unsigned int)ir;
}

unsigned int MFC2(int reg) {
  switch (reg) {
  case 1: case 3: case 5: case 8: case 9: case 10: case 11:
    gteRegs.CP2D.p[reg].d = (int)gteRegs.CP2D.p[reg].sw.l;
    break;
  case 7: case 16: case 17: case 18: case 19:
    gteRegs.CP2D.p[reg].d = (unsigned int)gteRegs.CP2D.p[reg].w.l;
    break;
  case 15:
    gteRegs.CP2D.p[reg].d = C2_SXY2;
    break;
  case 28: case 29:
    gteRegs.CP2D.p[reg].d = ir_to_5(C2_IR1) | ir_to_5(C2_IR2) << 5 | ir_to_5(C2_IR3) << 10;
    break;
  }
  return gteRegs.CP2D.p[reg].d;
}

void MTC2(unsigned int value, int reg) {
  switch (reg) {
  case 15:
    C2_SXY0 = C2_SXY1;
    C2_SXY1 = C2_SXY2;
    C2_SXY2 = value;
    break;
  case 28:
    C2_IR1 = (value & 0x1f) << 7;
    C2_IR2 = (value & 0x3e0) << 2;
    C2_IR3 = (value & 0x7c00) >> 3;
    break;
  case 30:
    C2_LZCR = leading_zeros(value);
    break;
  case 31:
    return;
  }
  gteRegs.CP2D.p[reg].d = value;
}

void CTC2(unsigned int value, int reg) {
  switch (reg) {
  case 4: case 12: case 20: case 26: case 27: case 29: case 30:
    value = (int)(short)value;
    break;
  case 31:
    value = value & 0x7ffff000;
    if ((value & 0x7f87e000) != 0)
      value |= 0x80000000;
    break;
  }
  gteRegs.CP2C.p[reg].d = value;
}

unsigned int CFC2(int reg) { return gteRegs.CP2C.p[reg].d; }

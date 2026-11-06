/* The GTE's arithmetic as plain integer C, for the hand-written C that
 * replaces mips2c units (collision_hand.c, math_hand.c; docs/native-plan.md
 * stage 3, decisions D83): what each command the asm issues computes, on C
 * values, exactly. A register holds 16 bits (S16), products are 32-bit,
 * sums 64-bit and the MAC their low 32 bits. Plus the MIPS details the asm
 * relies on: div by zero, shift amounts mod 32, LZCS/LZCR, and PsyQ's
 * table square root. */
#ifndef GTE_INT_H
#define GTE_INT_H

#include "vector.h"

#define GTE_INT_INL static inline __attribute__((always_inline))

extern short D_80074B84[]; /* PsyQ's square root table (SquareRoot0) */

/* A value as a 16-bit GTE register holds it (IR0-3, the matrix halves,
 * V0's coordinates). */
GTE_INT_INL int S16(int v) { return (short)v; }

/* OP (sf 0) with D on the diagonal (R11, R22, R33) and E in IR1-3: D x E,
 * as MAC1-3. */
GTE_INT_INL Vector3D Cross(const Vector3D *d, const Vector3D *e) {
  Vector3D r;
  r.x = (int)((long long)(S16(d->y) * S16(e->z)) - S16(d->z) * S16(e->y));
  r.y = (int)((long long)(S16(d->z) * S16(e->x)) - S16(d->x) * S16(e->z));
  r.z = (int)((long long)(S16(d->x) * S16(e->y)) - S16(d->y) * S16(e->x));
  return r;
}

/* A row of MVMVA (sf 0, no translation) with the row and IR1-3 both
 * 16-bit: the dot product as that row's MAC. */
GTE_INT_INL int Dot(const Vector3D *r, const Vector3D *v) {
  return (int)((long long)(S16(r->x) * S16(v->x)) + S16(r->y) * S16(v->y) + S16(r->z) * S16(v->z));
}

/* A row of MVMVA or RTPS (sf 1, no translation) read back as MAC: the row
 * times V0, >> 12. */
GTE_INT_INL int RotMac(const short *row, int vx, int vy, int vz) {
  return (int)(((long long)(row[0] * vx) + row[1] * vy + row[2] * vz) >> 12);
}

/* The same row (lm 0) read back as IR: the MAC clamped to 16 bits. */
GTE_INT_INL int RotClamp(const short *row, int vx, int vy, int vz) {
  int v = RotMac(row, vx, vy, vz);
  return v > 0x7FFF ? 0x7FFF : v < -0x8000 ? -0x8000 : v;
}

/* SQR (sf 0), then the asm's sum of MAC1-3. */
GTE_INT_INL int LengthSq(const Vector3D *v) {
  return (int)((unsigned int)(S16(v->x) * S16(v->x)) + (unsigned int)(S16(v->y) * S16(v->y)) +
               (unsigned int)(S16(v->z) * S16(v->z)));
}

/* GPL (sf 0) after MAC1-3 are cleared, or GPF (sf 0): IR0 x IR, shifted. */
GTE_INT_INL Vector3D Scale(int ir0, const Vector3D *v, int shift) {
  Vector3D r;
  r.x = (S16(ir0) * S16(v->x)) >> shift;
  r.y = (S16(ir0) * S16(v->y)) >> shift;
  r.z = (S16(ir0) * S16(v->z)) >> shift;
  return r;
}

GTE_INT_INL Vector3D Sub(const Vector3D *a, const Vector3D *b) {
  Vector3D r = {a->x - b->x, a->y - b->y, a->z - b->z};
  return r;
}

GTE_INT_INL Vector3D Add(const Vector3D *a, const Vector3D *b) {
  Vector3D r = {a->x + b->x, a->y + b->y, a->z + b->z};
  return r;
}

GTE_INT_INL Vector3D Shr(const Vector3D *a, int shift) {
  Vector3D r = {a->x >> shift, a->y >> shift, a->z >> shift};
  return r;
}

/* MIPS div's quotient, division by zero as the R3000A gives it. */
GTE_INT_INL int MipsDiv(int a, int b) {
  if (b == 0)
    return a >= 0 ? -1 : 1;
  if (a == (int)0x80000000 && b == -1)
    return a;
  return a / b;
}

/* sllv/srav: the shift amount's low 5 bits. */
GTE_INT_INL int Shl(int v, int n) { return (int)((unsigned int)v << (n & 31)); }
GTE_INT_INL int Sar(int v, int n) { return v >> (n & 31); }

/* LZCR after LZCS = v: the leading bits equal to the sign bit. */
GTE_INT_INL int LeadingSignBits(int v) {
  unsigned int u = v < 0 ? ~(unsigned int)v : (unsigned int)v;
  return u ? __builtin_clz(u) : 32;
}

/* The square root of v > 0 by PsyQ's table, as the asm inlines it
 * (SquareRoot0): v normalised to 0x40..0xFF by an even shift, looked up,
 * and shifted back by half. */
GTE_INT_INL int TableSqrt(int v) {
  int k = LeadingSignBits(v) & ~1;
  int half = (0x1F - k) >> 1;
  int m = k - 0x18 >= 0 ? Shl(v, k - 0x18) : Sar(v, 0x18 - k);
  return (int)((unsigned int)Shl(D_80074B84[m - 0x40], half) >> 12);
}

#endif

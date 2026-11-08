/* libgte: the six functions the game calls (docs/native-plan.md stage 8,
 * step 8c, decisions D116), on our own register file (gte_regs.c) and the
 * fixed-point maths of gte_int.h. Derived from PsyCross (MIT,
 * THIRD_PARTY_NOTICES.md): src/psx/LIBGTE.C, its gtemac.h gte_MulMatrix0,
 * and the MVMVA of src/gte/PsyX_GTE.cpp it runs.
 *
 * Each leaves gteRegs as PsyCross's does, so whatever reads the register
 * file afterwards (the B2 hand C, the mips2c units, a save state) sees the
 * same bytes; tests/gte/test_libgte.c compares the whole register file after
 * each call, on random inputs and register states. Two things PsyCross
 * leaves to its stack are left alone here (no caller reads them: camera.c,
 * gamestates/draw.c, pete.c and cyclorama_hand.c take the 3x3 only, and
 * VZ0 is a 16-bit register):
 * - MulMatrix copies its whole uninitialised temporary over m0, t[] and the
 *   pad too; this one writes the 3x3, as PsyQ's does;
 * - MulMatrix0 loads V0 from a local SVECTOR whose pad it never sets, so
 *   VZ0's high half is whatever the stack held; here it's 0. */
#include <libgte.h>
#include <gtereg.h>
#include <string.h>

#include "gte_int.h"

/* FLAG's bits for IR1-3 saturating (PsyX_GTE.cpp Lm_B1-3: IR3's has no
 * bit 31). */
static const unsigned int k_ir_flag[3] = {(1u << 31) | (1u << 24), (1u << 31) | (1u << 23), 1u << 22};

/* MVMVA with sf 1, lm 0, the rotation and no translation (RTIR and RTV0
 * alike) on the column v: MAC1-3 and IR1-3 into mac and ir, FLAG returned
 * (A1-A3 never flag here: three 16x16 products stay far inside 44 bits). */
static unsigned int rot_column(const MATRIX *r, const short v[3], int mac[3], short ir[3]) {
  unsigned int flag = 0;
  for (int i = 0; i < 3; i++) {
    int m = RotMac(r->m[i], v[0], v[1], v[2]);
    int c = m > 0x7FFF ? 0x7FFF : m < -0x8000 ? -0x8000 : m;
    if (c != m)
      flag |= k_ir_flag[i];
    mac[i] = m;
    ir[i] = (short)c;
  }
  return flag;
}

/* m0 times m1 into p, column by column, and the register file as the
 * last column's command leaves it: the rotation m0 (gte_SetRotMatrix, R33
 * sign-extended as CTC2 4 leaves it), MAC1-3, FLAG, and IR1-3
 * sign-extended (gte_stclmv's and gte_stsv's MFC2s leave them so). */
static void mul_columns(const MATRIX *m0, const MATRIX *m1, short p[3][3]) {
  int mac[3];
  short ir[3];
  unsigned int flag = 0;
  for (int j = 0; j < 3; j++) {
    const short v[3] = {m1->m[0][j], m1->m[1][j], m1->m[2][j]};
#if LIBGTE_MUTATE == 1 /* tests/gte's mutant: FLAG from every column, not the last */
    flag |= rot_column(m0, v, mac, ir);
#else
    flag = rot_column(m0, v, mac, ir);
#endif
    for (int i = 0; i < 3; i++)
      p[i][j] = ir[i];
  }
  memcpy(&gteRegs.CP2C.r[0], m0->m, 16);
  gteRegs.CP2C.p[4].sd = m0->m[2][2];
#if LIBGTE_CONTROL_ONLY
  /* the presenter's build on fast_render=3's renderers (stage 7e, D124):
   * they read the rotation, never the data registers or FLAG */
  (void)mac, (void)ir, (void)flag;
#else
  for (int i = 0; i < 3; i++) {
    gteRegs.CP2D.p[25 + i].sd = mac[i];
    gteRegs.CP2D.p[9 + i].sd = ir[i];
  }
  C2_FLAG = flag;
#endif
}

void InitGeom(void) {
  C2_ZSF3 = 341;
  C2_ZSF4 = 256;
  C2_H = 1000;
  C2_DQA = -98;
  C2_DQB = 340;
  C2_OFX = 0;
  C2_OFY = 0;
}

void SetGeomOffset(int ofx, int ofy) {
  C2_OFX = ofx << 16;
  C2_OFY = ofy << 16;
}

void SetGeomScreen(int h) { C2_H = h; }

void SetTransMatrix(MATRIX *m) {
  gteRegs.CP2C.p[5].sd = m->t[0];
  gteRegs.CP2C.p[6].sd = m->t[1];
  gteRegs.CP2C.p[7].sd = m->t[2];
}

/* m0 = m0 x m1 (gtemac.h's gte_MulMatrix0: RTIR on each column, loaded
 * into IR1-3 by gte_ldclmv). */
MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1) {
  short p[3][3];
  mul_columns(m0, m1, p);
  memcpy(m0->m, p, sizeof p);
  return m0;
}

/* m2 = m0 x m1 (LIBGTE.C's own: RTV0 on each column, loaded into V0 by
 * gte_ldv0, which stays as the last column). */
MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2) {
  short p[3][3];
  const short vx = m1->m[0][2], vy = m1->m[1][2], vz = m1->m[2][2];
  mul_columns(m0, m1, p);
#if LIBGTE_CONTROL_ONLY
  (void)vx, (void)vy, (void)vz;
#else
  gteRegs.CP2D.p[0].d = (unsigned short)vx | (unsigned int)(unsigned short)vy << 16;
  gteRegs.CP2D.p[1].d = (unsigned short)vz;
#endif
  memcpy(m2->m, p, sizeof p);
  return m2;
}

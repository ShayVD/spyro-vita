/* The game's maths library (asm/math.s): angles, square roots, vectors and
 * matrices, written by hand from the asm so it reads as C
 * (docs/native-plan.md stage 3, decisions D87). Replaces the mips2c unit
 * (cmake/mips2c.cmake M2C_HAND_MULTI); checked against PCSX-Redux with the
 * oracle replay and against the unit with differential sweeps
 * (docs/translation-log.md).
 *
 * The asm does its vector maths on the GTE. Here it is integer C with the
 * GTE's arithmetic (gte_int.h). One piece of the GTE's state is the
 * library's own, read across calls: the rotation matrix (and translation)
 * that RotVec8ToMatrix, VecRotateByMatrix, VecRotateByCam and the camera
 * projections load and VecRotateByLastMatrix and the projections use. The
 * hand C keeps those registers as the asm leaves them; the rest of what
 * the asm leaves in the GTE (MAC, IR, FLAG, LZCR, the FIFOs) nothing reads,
 * and goes (D87, as collision's did in D83). The projections read the screen offset, H and the depth
 * cue from the GTE, as the asm does.
 *
 * M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) takes that function from the mips2c
 * unit instead, linked as <name>_gen, for A/B tests. */
#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <string.h>

#include "common.h"

#include "camera.h"
#include "environment.h"
#include "gte_int.h"
#include "gte_native.h"
#include "math.h"
#include "math_hand.h"
#include "vector.h"

extern u_char D_8006D908[];  /* atan by ratio x 64, 65 entries (Atan2Fast) */
extern int D_8006CE7C[];     /* tan by angle, scaled 0x10000 (Atan2) */
extern u_short D_8006CF04[]; /* the steps between D_8006CE7C's entries */

/* The mips2c unit's entries, linked as <name>_gen for A/B tests and the
 * shadow below (cmake M2C_HAND_MULTI). */
u_int Atan2Fast_gen(), Atan2_gen(), Sin_gen(), Cos_gen(), func_80016D08_gen();
u_int RotVec8ToMatrix_gen(), func_80016FD0_gen(), VecRotateByMatrix_gen();
u_int VecRotateByLastMatrix_gen(), VecRotateByCam_gen(), func_80017188_gen();
u_int VecMagnitude_gen(), VecRefineMagnitude_gen(), func_80017330_gen();
u_int func_80017428_gen(), func_80017548_gen(), VecScaleToLength_gen();
u_int func_80017614_gen(), VecShiftLeft_gen(), VecShiftRight_gen();
u_int VecNull_gen(), VecCopy_gen(), func_8001771C_gen(), VecAdd_gen();
u_int VecSub_gen(), VecMult_gen(), func_800177F8_gen(), func_80017854_gen();
u_int func_80017894_gen(), func_80017908_gen(), func_80017928_gen();
u_int func_80017948_gen(), func_8001796C_gen(), OctDistance_gen();
u_int func_800179F0_gen(), func_80017A38_gen(), func_80017AA4_gen();
u_int func_80017B48_gen(), func_80017BFC_gen(), func_80017C24_gen();
u_int func_80017C4C_gen(), VecToShortVec_gen(), func_80017C84_gen();
u_int ColTriUnpack_gen(), func_80017D7C_gen();

/* Oracle replays and differential sweeps (host builds with the mips2c
 * unit linked, cmake SPYRO_M2C_REF; decisions D83): with g_math_shadow
 * set, a call runs the unit from the same state first, then the hand C.
 * The output it writes, the result and the GTE's rotation and translation
 * (the state the hand C keeps) must match; mismatches are printed and
 * counted in g_math_shadow_fail. The call then leaves the GTE as the unit
 * left it, so a sample's whole state still compares with the emulator's.
 * The library is leaf code that writes only its output and the GTE, so
 * only those are saved and compared (it runs thousands of times a
 * sample; collision_hand.c's shadow compares all of RAM). */
#define MATH_SHADOW (MIPS2C_REPLAY && M2C_REF)
#if MATH_SHADOW
#include <gtereg.h>
#include <stdio.h>
#include <stdlib.h>

int g_math_shadow, g_math_shadow_fail;
static int s_shadowing;
static GTERegisters s_gte_before, s_gte_gen;
static unsigned char s_out_before[40], s_out_gen[40];
static void *s_out;
static int s_out_size;

/* Differential fuzzing (SPYRO_MATH_FUZZ=N with the shadow on): at a
 * sample's first math call, N rounds of every entry with random arguments
 * (MathFuzz, at the end of this file), each through the shadow. */
static int s_fuzz = -1;
static void MathFuzz(int rounds);

static int ShadowBegin(void *out, int size) {
  if (!g_math_shadow || s_shadowing)
    return 0;
  if (s_fuzz < 0) {
    s_fuzz = getenv("SPYRO_MATH_FUZZ") ? atoi(getenv("SPYRO_MATH_FUZZ")) : 0;
    if (s_fuzz > 0)
      MathFuzz(s_fuzz);
  }
  s_shadowing = 1;
  s_out = out, s_out_size = size;
  s_gte_before = gteRegs;
  if (size)
    memcpy(s_out_before, out, size);
  return 1;
}

static void ShadowSwap(void) {
  s_gte_gen = gteRegs;
  if (s_out_size) {
    memcpy(s_out_gen, s_out, s_out_size);
    memcpy(s_out, s_out_before, s_out_size);
  }
  gteRegs = s_gte_before;
}

static int ShadowEnd(const char *name, int gen, int hand) {
  int i;
  if (s_out_size && memcmp(s_out, s_out_gen, s_out_size)) {
    g_math_shadow_fail++;
    printf("  mathshadow %s output differs\n", name);
  }
  if (gen != hand) {
    g_math_shadow_fail++;
    printf("  mathshadow %s result unit %08x hand %08x\n", name, gen, hand);
  }
  for (i = 0; i < 8; i++) /* R11R12..R33, TRX..TRZ */
    if (gteRegs.CP2C.p[i].d != s_gte_gen.CP2C.p[i].d) {
      g_math_shadow_fail++;
      printf("  mathshadow %s gte c%d unit %08x hand %08x\n", name, i, s_gte_gen.CP2C.p[i].d, gteRegs.CP2C.p[i].d);
    }
  gteRegs = s_gte_gen;
  s_shadowing = 0;
  return hand;
}

/* SHADOW(name, output, output size, unit call, hand call) for a function
 * returning int; SHADOW_VOID for one returning nothing. */
#define SHADOW(name, out, size, gen_call, hand_call)                           \
  do {                                                                         \
    if (ShadowBegin((out), (size))) {                                          \
      int gen_ = (int)(gen_call);                                              \
      ShadowSwap();                                                            \
      return ShadowEnd(#name, gen_, (int)(hand_call));                         \
    }                                                                          \
  } while (0)
#define SHADOW_VOID(name, out, size, gen_call, hand_call)                      \
  do {                                                                         \
    if (ShadowBegin((out), (size))) {                                          \
      (void)(gen_call);                                                        \
      ShadowSwap();                                                            \
      hand_call;                                                               \
      ShadowEnd(#name, 0, 0);                                                  \
      return;                                                                  \
    }                                                                          \
  } while (0)
#else
#define SHADOW(name, out, size, gen_call, hand_call) ((void)0)
#define SHADOW_VOID(name, out, size, gen_call, hand_call) ((void)0)
#endif


/* ===================================================================== */
/* The GTE's rotation matrix and translation: the library's own state    */
/* ===================================================================== */

/* The GTE's rotation matrix as CTC2 loads it (R11R12, R13R21, R22R23,
 * R31R32 whole words, R33 the low half of the fifth) and its translation;
 * gtereg.h's storage, as mips2c_rt.h's moves use it. */
static void GteSetRotationWords(u_int w0, u_int w1, u_int w2, u_int w3, u_int w4) {
  gteRegs.CP2C.p[0].d = w0;
  gteRegs.CP2C.p[1].d = w1;
  gteRegs.CP2C.p[2].d = w2;
  gteRegs.CP2C.p[3].d = w3;
  gteRegs.CP2C.p[4].d = (int)(short)w4;
}

static void GteSetTranslation(int x, int y, int z) {
  gteRegs.CP2C.p[5].d = x;
  gteRegs.CP2C.p[6].d = y;
  gteRegs.CP2C.p[7].d = z;
}

static void GteGetRotation(short m[3][3]) {
  m[0][0] = C2_R11, m[0][1] = C2_R12, m[0][2] = C2_R13;
  m[1][0] = C2_R21, m[1][1] = C2_R22, m[1][2] = C2_R23;
  m[2][0] = C2_R31, m[2][1] = C2_R32, m[2][2] = C2_R33;
}

/* func_80017428's two moves: R11 and R12 from x and y, R13 and R21 from
 * z's word. */
static void GteSetRotationRow1(int x, int y, int z) {
  gteRegs.CP2C.p[0].d = (u_int)(x & 0xFFFF) + ((u_int)y << 16);
  gteRegs.CP2C.p[1].d = z;
}

/* OP's diagonal from three whole words: R11R12, R22R23, R33. */
static void GteSetRotationDiagonal(int x, int y, int z) {
  gteRegs.CP2C.p[0].d = x;
  gteRegs.CP2C.p[2].d = y;
  gteRegs.CP2C.p[4].d = (int)(short)z;
}

/* A matrix's first five words, as CTC2 loads them into R11R12..R33 (the
 * 3x3 shorts and the pad after them). */
static inline void LoadRotation(const void *m) {
  const u_int *w = (const u_int *)m;
  GteSetRotationWords(w[0], w[1], w[2], w[3], w[4]);
}

/* MVMVA (sf 1) on the rotation matrix with IR set from a game vector as the
 * library maps it (IR1 = -y, IR2 = -z, IR3 = x), read back as MAC1-3. */
static inline void RotateGameVector(const short m[3][3], const Vector3D *v, int mac[3]) {
  int ir1 = S16(-v->y), ir2 = S16(-v->z), ir3 = S16(v->x), i;
  for (i = 0; i < 3; i++)
    mac[i] = (int)(((long long)(m[i][0] * ir1) + m[i][1] * ir2 + m[i][2] * ir3) >> 12);
}

/* ===================================================================== */
/* Angles                                                                 */
/* ===================================================================== */

/* The octant of (y, x): a base angle (256ths of a turn) and whether the
 * table's angle (of the smaller over the larger) is taken off it. */
static void Octant(int y, int x, int *base, int *neg) {
  if (y >= 0) {
    if (x >= 0) {
      *neg = y - x < 0, *base = y - x < 0 ? 0x40 : 0;
    } else {
      x = -x;
      *neg = !(y - x < 0), *base = y - x < 0 ? 0xC0 : 0x100;
    }
  } else {
    y = -y;
    if (x >= 0) {
      *neg = !(y - x < 0), *base = y - x < 0 ? 0x40 : 0x80;
    } else {
      x = -x;
      *neg = y - x < 0, *base = y - x < 0 ? 0xC0 : 0x80;
    }
  }
}

#if !M2C_Atan2Fast
static int Atan2FastHand(int pY, int pX) {
  int y = pY < 0 ? -pY : pY, x = pX < 0 ? -pX : pX, lo = y, hi = x, base, neg, t;
  if (y - x > 0)
    lo = x, hi = y;
  if (!hi)
    hi = 1;
  t = D_8006D908[MipsDiv(Shl(lo, 6), hi)];
  Octant(pY, pX, &base, &neg);
  return neg ? base - t : base + t;
}

/* The angle of (pX, pY) in 256ths of a turn, from the ratio of the smaller
 * to the larger in 64ths. */
int Atan2Fast(int pY, int pX) {
  SHADOW(Atan2Fast, NULL, 0, Atan2Fast_gen(pY, pX), Atan2FastHand(pY, pX));
  return Atan2FastHand(pY, pX);
}
#else
int Atan2Fast(int pY, int pX) { return Atan2Fast_gen(pY, pX); }
#endif

#if !M2C_Atan2
static int Atan2Hand(int pY, int pX, int output16Bit) {
  int y = pY < 0 ? -pY : pY, x = pX < 0 ? -pX : pX, lo, hi, shift, q, k, base, neg, r;
  shift = 17 - LeadingSignBits(y | x);
  if (shift > 0)
    y = Sar(y, shift), x = Sar(x, shift);
  lo = y, hi = x;
  if (y - x > 0)
    lo = x, hi = y;
  if (!hi)
    return 0;
  q = MipsDiv(Shl(lo, 16), hi);
  Octant(pY, pX, &base, &neg);
  /* The first entry from q's 2048ths on that reaches q; k the one before. */
  for (k = (int)((u_int)q >> 11) + 1; D_8006CE7C[k] - q < 0; k++)
    ;
  k--;
  if (!output16Bit) {
    r = k;
  } else {
    int step = (int)((u_int)MipsDiv(Shl(D_8006CE7C[k + 1] - q, 16), D_8006CF04[k]) >> 12);
    r = (k == 32 ? k << 4 : (k << 4) + 8) - step;
    base <<= 4;
  }
  return base + (neg ? -r : r);
}

/* The angle of (pX, pY) in 256ths of a turn, or 4096ths with
 * output16Bit, from a table of tangents (after shifting both down to 17
 * bits). (0, 0) is 0. */
int Atan2(int pY, int pX, int output16Bit) {
  SHADOW(Atan2, NULL, 0, Atan2_gen(pY, pX, output16Bit), Atan2Hand(pY, pX, output16Bit));
  return Atan2Hand(pY, pX, output16Bit);
}
#else
int Atan2(int pY, int pX, int output16Bit) { return Atan2_gen(pY, pX, output16Bit); }
#endif

/* Sin and Cos: the table entry of the angle's 16ths (4096ths of a turn),
 * interpolated in steps of 16. */
static int TableWave(const short *t, int angle) {
  int a = angle & 0xFFF, frac = a & 0xF, i = a >> 4;
  if (!frac)
    return t[i];
  return ((frac * (t[i + 1] - t[i])) >> 4) + t[i];
}

#if !M2C_Sin
int Sin(int pAngle) {
  SHADOW(Sin, NULL, 0, Sin_gen(pAngle), TableWave(D_8006CBF8, pAngle));
  return TableWave(D_8006CBF8, pAngle);
}
#else
int Sin(int pAngle) { return Sin_gen(pAngle); }
#endif

#if !M2C_Cos
int Cos(int pAngle) {
  SHADOW(Cos, NULL, 0, Cos_gen(pAngle), TableWave(D_8006CC78, pAngle));
  return TableWave(D_8006CC78, pAngle);
}
#else
int Cos(int pAngle) { return Cos_gen(pAngle); }
#endif

#if !M2C_func_80016D08
static int HighestBit(int v) { return v ? 31 - LeadingSignBits(v) : 0; }

/* The index of v's highest set bit (0 for 0). */
int func_80016D08(int v) {
  SHADOW(func_80016D08, NULL, 0, func_80016D08_gen(v), HighestBit(v));
  return HighestBit(v);
}
#else
int func_80016D08(int v) { return func_80016D08_gen(v); }
#endif

/* The difference of two angles, its size up to half a turn (func_80017908
 * in 256ths, func_80017928 in 4096ths), or signed (func_80017948,
 * func_8001796C). */
static int AngleSize(int a, int b, int mask) {
  int d = (a - b) & mask;
  return d - (mask + 1) / 2 >= 0 ? mask + 1 - d : d;
}
static int AngleSigned(int a, int b, int mask) {
  int d = (a - b) & mask;
  return d - (mask + 1) / 2 > 0 ? d - (mask + 1) : d;
}

#if !M2C_func_80017908
int func_80017908(int pAngle1, int pAngle2) {
  SHADOW(func_80017908, NULL, 0, func_80017908_gen(pAngle1, pAngle2), AngleSize(pAngle1, pAngle2, 0xFF));
  return AngleSize(pAngle1, pAngle2, 0xFF);
}
#else
int func_80017908(int pAngle1, int pAngle2) { return func_80017908_gen(pAngle1, pAngle2); }
#endif

#if !M2C_func_80017928
int func_80017928(int pAngle1, int pAngle2) {
  SHADOW(func_80017928, NULL, 0, func_80017928_gen(pAngle1, pAngle2), AngleSize(pAngle1, pAngle2, 0xFFF));
  return AngleSize(pAngle1, pAngle2, 0xFFF);
}
#else
int func_80017928(int pAngle1, int pAngle2) { return func_80017928_gen(pAngle1, pAngle2); }
#endif

#if !M2C_func_80017948
int func_80017948(int pAngle1, int pAngle2) {
  SHADOW(func_80017948, NULL, 0, func_80017948_gen(pAngle1, pAngle2), AngleSigned(pAngle1, pAngle2, 0xFF));
  return AngleSigned(pAngle1, pAngle2, 0xFF);
}
#else
int func_80017948(int pAngle1, int pAngle2) { return func_80017948_gen(pAngle1, pAngle2); }
#endif

#if !M2C_func_8001796C
int func_8001796C(int pAngle1, int pAngle2) {
  SHADOW(func_8001796C, NULL, 0, func_8001796C_gen(pAngle1, pAngle2), AngleSigned(pAngle1, pAngle2, 0xFFF));
  return AngleSigned(pAngle1, pAngle2, 0xFFF);
}
#else
int func_8001796C(int pAngle1, int pAngle2) { return func_8001796C_gen(pAngle1, pAngle2); }
#endif

#if !M2C_func_800179F0
static int StepAngle(int target, int angle, int speed, int minStep) {
  int d = (target - angle) & 0xFF;
  if (d - 0x80 > 0)
    return d - 0x100 + minStep > 0 ? angle & 0xFF : (angle - speed) & 0xFF;
  return d - minStep < 0 ? angle & 0xFF : (angle + speed) & 0xFF;
}

/* An 8-bit angle one step of pSpeed towards pTargetAngle, the short way
 * round; unchanged when within pMinStep (upwards) or within -pMinStep
 * ... of it (downwards: 0x100 - pMinStep). */
int func_800179F0(int pTargetAngle, int pAngle, int pSpeed, int pMinStep) {
  SHADOW(func_800179F0, NULL, 0, func_800179F0_gen(pTargetAngle, pAngle, pSpeed, pMinStep),
         StepAngle(pTargetAngle, pAngle, pSpeed, pMinStep));
  return StepAngle(pTargetAngle, pAngle, pSpeed, pMinStep);
}
#else
int func_800179F0(int pTargetAngle, int pAngle, int pSpeed, int pMinStep) {
  return func_800179F0_gen(pTargetAngle, pAngle, pSpeed, pMinStep);
}
#endif

/* ===================================================================== */
/* Square roots and lengths                                               */
/* ===================================================================== */

#if !M2C_func_80017A38
static int Root(int v) { return v ? TableSqrt(v) : 0; }

/* The square root of v by PsyQ's table (0 for 0). */
int func_80017A38(int pVal) {
  SHADOW(func_80017A38, NULL, 0, func_80017A38_gen(pVal), Root(pVal));
  return Root(pVal);
}
#else
int func_80017A38(int pVal) { return func_80017A38_gen(pVal); }
#endif

/* SQR of a vector as the asm loads it (each component a 16-bit IR), z
 * left 0 without pIncludeZ, summed. */
static int LengthSqXY(const Vector3D *v, int includeZ) {
  Vector3D w = *v;
  if (!includeZ)
    w.z = 0;
  return LengthSq(&w);
}

#if !M2C_VecMagnitude
static int Magnitude(Vector3D *pVec, int pIncludeZAxis) { return Root(LengthSqXY(pVec, pIncludeZAxis)); }

int VecMagnitude(Vector3D *pVec, int pIncludeZAxis) {
  SHADOW(VecMagnitude, NULL, 0, VecMagnitude_gen(pVec, pIncludeZAxis), Magnitude(pVec, pIncludeZAxis));
  return Magnitude(pVec, pIncludeZAxis);
}
#else
int VecMagnitude(Vector3D *pVec, int pIncludeZAxis) { return VecMagnitude_gen(pVec, pIncludeZAxis); }
#endif

#if !M2C_VecRefineMagnitude
/* One Newton step: guess + (len2 / (guess2 >> s) - (1 << s)) / 2 x guess
 * >> s, with s half guess2's bit length; the guess itself if s <= 6. */
static int RefineMagnitude(Vector3D *pIn, int guess, int includeZ) {
  int len2 = LengthSqXY(pIn, includeZ), g2 = guess * guess;
  int s = (int)((u_int)(32 - LeadingSignBits(g2)) >> 1), step;
  if (s - 6 <= 0)
    return guess;
  step = (MipsDiv(len2, (int)((u_int)g2 >> (s & 31))) - Shl(1, s)) >> 1;
  return guess + Sar(step * guess, s);
}

int VecRefineMagnitude(Vector3D *pIn, int pInitialGuess, int pIncludeZAxis) {
  SHADOW(VecRefineMagnitude, NULL, 0, VecRefineMagnitude_gen(pIn, pInitialGuess, pIncludeZAxis),
         RefineMagnitude(pIn, pInitialGuess, pIncludeZAxis));
  return RefineMagnitude(pIn, pInitialGuess, pIncludeZAxis);
}
#else
int VecRefineMagnitude(Vector3D *pIn, int pInitialGuess, int pIncludeZAxis) {
  return VecRefineMagnitude_gen(pIn, pInitialGuess, pIncludeZAxis);
}
#endif

#if !M2C_OctDistance
/* |dx| + 3/8 |dy| with dx the larger of the two in x and y. */
static int Octagonal(const Vector3D *a, const Vector3D *b) {
  int dx = b->x - a->x, dy = b->y - a->y;
  if (dx < 0)
    dx = -dx;
  if (dy < 0)
    dy = -dy;
  if (dx - dy >= 0)
    return dx + (int)((u_int)(dy * 3) >> 3);
  return (int)((u_int)(dx * 3) >> 3) + dy;
}

int OctDistance(Vector3D *pPos1, Vector3D *pPos2) {
  SHADOW(OctDistance, NULL, 0, OctDistance_gen(pPos1, pPos2), Octagonal(pPos1, pPos2));
  return Octagonal(pPos1, pPos2);
}
#else
int OctDistance(Vector3D *pPos1, Vector3D *pPos2) { return OctDistance_gen(pPos1, pPos2); }
#endif

/* ===================================================================== */
/* Vectors                                                                */
/* ===================================================================== */

#if !M2C_VecNull
void VecNull(Vector3D *pVec) {
  SHADOW_VOID(VecNull, pVec, 12, VecNull_gen(pVec), (pVec->x = pVec->y = pVec->z = 0));
  pVec->x = pVec->y = pVec->z = 0;
}
#else
void VecNull(Vector3D *pVec) { VecNull_gen(pVec); }
#endif

#if !M2C_VecCopy
void VecCopy(Vector3D *pOut, Vector3D *pIn) {
  Vector3D v = *pIn;
  SHADOW_VOID(VecCopy, pOut, 12, VecCopy_gen(pOut, pIn), (*pOut = v));
  *pOut = v;
}
#else
void VecCopy(Vector3D *pOut, Vector3D *pIn) { VecCopy_gen(pOut, pIn); }
#endif

#if !M2C_VecAdd
void VecAdd(Vector3D *pOut, Vector3D *pIn1, Vector3D *pIn2) {
  SHADOW_VOID(VecAdd, pOut, 12, VecAdd_gen(pOut, pIn1, pIn2), (*pOut = Add(pIn1, pIn2)));
  *pOut = Add(pIn1, pIn2);
}
#else
void VecAdd(Vector3D *pOut, Vector3D *pIn1, Vector3D *pIn2) { VecAdd_gen(pOut, pIn1, pIn2); }
#endif

#if !M2C_VecSub
void VecSub(Vector3D *pOut, Vector3D *pIn1, Vector3D *pIn2) {
  SHADOW_VOID(VecSub, pOut, 12, VecSub_gen(pOut, pIn1, pIn2), (*pOut = Sub(pIn1, pIn2)));
  *pOut = Sub(pIn1, pIn2);
}
#else
void VecSub(Vector3D *pOut, Vector3D *pIn1, Vector3D *pIn2) { VecSub_gen(pOut, pIn1, pIn2); }
#endif

/* sllv/srav by n mod 32 on each component. */
static void ShiftVector(Vector3D *v, int n, int left) {
  if (left)
    v->x = Shl(v->x, n), v->y = Shl(v->y, n), v->z = Shl(v->z, n);
  else
    v->x = Sar(v->x, n), v->y = Sar(v->y, n), v->z = Sar(v->z, n);
}

#if !M2C_VecShiftLeft
void VecShiftLeft(Vector3D *pVec, int pNumBits) {
  SHADOW_VOID(VecShiftLeft, pVec, 12, VecShiftLeft_gen(pVec, pNumBits), ShiftVector(pVec, pNumBits, 1));
  ShiftVector(pVec, pNumBits, 1);
}
#else
void VecShiftLeft(Vector3D *pVec, int pNumBits) { VecShiftLeft_gen(pVec, pNumBits); }
#endif

#if !M2C_VecShiftRight
void VecShiftRight(Vector3D *pVec, int pNumBits) {
  SHADOW_VOID(VecShiftRight, pVec, 12, VecShiftRight_gen(pVec, pNumBits), ShiftVector(pVec, pNumBits, 0));
  ShiftVector(pVec, pNumBits, 0);
}
#else
void VecShiftRight(Vector3D *pVec, int pNumBits) { VecShiftRight_gen(pVec, pNumBits); }
#endif

/* GPL (sf 0) after MAC1-3 are cleared: pVal x each component, both as
 * 16-bit registers. */
#if !M2C_VecMult
void VecMult(Vector3D *pOut, Vector3D *pIn, int pVal) {
  SHADOW_VOID(VecMult, pOut, 12, VecMult_gen(pOut, pIn, pVal), (*pOut = Scale(pVal, pIn, 0)));
  *pOut = Scale(pVal, pIn, 0);
}
#else
void VecMult(Vector3D *pOut, Vector3D *pIn, int pVal) { VecMult_gen(pOut, pIn, pVal); }
#endif

/* Each component x (0x10000 / pVal), >> 16: divided by pVal. */
#if !M2C_func_800177F8
void func_800177F8(Vector3D *pOut, Vector3D *pIn, int pVal) {
  SHADOW_VOID(func_800177F8, pOut, 12, func_800177F8_gen(pOut, pIn, pVal),
              (*pOut = Scale(MipsDiv(0x10000, pVal), pIn, 16)));
  *pOut = Scale(MipsDiv(0x10000, pVal), pIn, 16);
}
#else
void func_800177F8(Vector3D *pOut, Vector3D *pIn, int pVal) { func_800177F8_gen(pOut, pIn, pVal); }
#endif

/* ===================================================================== */
/* Matrices                                                               */
/* ===================================================================== */

/* A matrix as five words (the 3x3 shorts, then the pad half after m[2][2])
 * and back: what the asm moves through CTC2 and stores. */
typedef struct {
  short m[3][3];
  short pad;
} MatWords;

/* MVMVA (sf 1, lm 0) of the matrix with V0 = (vx, vy, vz), as IR1-3. */
static void RotColumn(const MatWords *r, int vx, int vy, int vz, int ir[3]) {
  int i;
  for (i = 0; i < 3; i++)
    ir[i] = RotClamp(r->m[i], vx, vy, vz);
}

/* pIn (or the identity) turned by the three 8-bit angles packed in
 * angles (bits 16-23 about one axis, then bits 8-15, then bits 0-7; 256ths
 * of a turn, the tables as unsigned halves): each turn replaces two
 * columns with the matrix times (cos, sin) and (-sin, cos) in those axes,
 * clamped to 16 bits. The matrix goes to pOut and to the GTE (translation
 * 0), as each turn leaves it; with loadLast 0 the last turn isn't loaded
 * (42CC4.s's copies of this code). The asm's angles of 0 are no turn. */
void RotateByAngles8(u_int angles, const MATRIX *pIn, MATRIX *pOut, int loadLast) {
  static const MatWords identity = {{{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}}, 0};
  MatWords r;
  int a, c, s, i1[3], i2[3], k;
  if (pIn)
    memcpy(&r, pIn, sizeof r);
  else
    r = identity;
  GteSetTranslation(0, 0, 0);
  LoadRotation(&r);
  a = (angles >> 16) & 0xFF;
  if (a) {
    c = S16((u_short)D_8006CC78[a]), s = S16((u_short)D_8006CBF8[a]);
    RotColumn(&r, c, 0, s, i1);
    RotColumn(&r, S16(-s), 0, c, i2);
    for (k = 0; k < 3; k++)
      r.m[k][0] = i1[k], r.m[k][2] = i2[k];
    r.pad = 0; /* the asm rebuilds the last word from IR3 alone */
    LoadRotation(&r);
  }
  a = (angles >> 8) & 0xFF;
  if (a) {
    c = S16((u_short)D_8006CC78[a]), s = S16((u_short)D_8006CBF8[a]);
    RotColumn(&r, 0, c, s, i1);
    RotColumn(&r, 0, S16(-s), c, i2);
    for (k = 0; k < 3; k++)
      r.m[k][1] = i1[k], r.m[k][2] = i2[k];
    r.pad = 0;
    LoadRotation(&r);
  }
  a = angles & 0xFF;
  if (a) {
    c = S16((u_short)D_8006CC78[a]), s = S16((u_short)D_8006CBF8[a]);
    RotColumn(&r, c, s, 0, i1);
    RotColumn(&r, S16(-s), c, 0, i2);
    for (k = 0; k < 3; k++)
      r.m[k][0] = i1[k], r.m[k][1] = i2[k];
    if (loadLast)
      LoadRotation(&r);
  }
  memcpy(pOut, &r, sizeof r);
}

#if !M2C_RotVec8ToMatrix
/* The matrix stays in the GTE, where VecRotateByLastMatrix finds it. */
static void Vec8ToMatrix(Vector3D8 *pRotation, MATRIX *pOut, MATRIX *pIn) {
  RotateByAngles8(*(const u_int *)pRotation, pIn, pOut, 1);
}

void RotVec8ToMatrix(Vector3D8 *pRotation, MATRIX *pOut, MATRIX *pIn) {
  SHADOW_VOID(RotVec8ToMatrix, pOut, 20, RotVec8ToMatrix_gen(pRotation, pOut, pIn), Vec8ToMatrix(pRotation, pOut, pIn));
  Vec8ToMatrix(pRotation, pOut, pIn);
}
#else
void RotVec8ToMatrix(Vector3D8 *pRotation, MATRIX *pOut, MATRIX *pIn) { RotVec8ToMatrix_gen(pRotation, pOut, pIn); }
#endif

#if !M2C_func_80016FD0
/* The transpose, the pad half after m[2][2] copied with it. */
static void Transpose(MATRIX *pOut, MATRIX *pIn) {
  MatWords i, o;
  memcpy(&i, pIn, sizeof i);
  o = i;
  o.m[0][1] = i.m[1][0], o.m[1][0] = i.m[0][1];
  o.m[0][2] = i.m[2][0], o.m[2][0] = i.m[0][2];
  o.m[1][2] = i.m[2][1], o.m[2][1] = i.m[1][2];
  memcpy(pOut, &o, sizeof o);
}

void func_80016FD0(MATRIX *pOut, MATRIX *pIn) {
  SHADOW_VOID(func_80016FD0, pOut, 20, func_80016FD0_gen(pOut, pIn), Transpose(pOut, pIn));
  Transpose(pOut, pIn);
}
#else
void func_80016FD0(MATRIX *pOut, MATRIX *pIn) { func_80016FD0_gen(pOut, pIn); }
#endif

/* The library's vector rotation: the game's (x, y, z) are the GTE's
 * (z, -x, -y), so the matrix rows give (row 3, -row 1, -row 2). */
static void RotateByMatrix(const MatWords *r, Vector3D *pIn, Vector3D *pOut) {
  int mac[3];
  RotateGameVector(r->m, pIn, mac);
  pOut->x = mac[2], pOut->y = -mac[0], pOut->z = -mac[1];
}

#if !M2C_VecRotateByMatrix
static void RotateLoad(MATRIX *pMatrix, Vector3D *pVector, Vector3D *pOut) {
  MatWords r;
  memcpy(&r, pMatrix, sizeof r);
  LoadRotation(&r);
  RotateByMatrix(&r, pVector, pOut);
}

/* pVector rotated by pMatrix, which stays in the GTE for
 * VecRotateByLastMatrix. */
void VecRotateByMatrix(MATRIX *pMatrix, Vector3D *pVector, Vector3D *pOut) {
  SHADOW_VOID(VecRotateByMatrix, pOut, 12, VecRotateByMatrix_gen(pMatrix, pVector, pOut), RotateLoad(pMatrix, pVector, pOut));
  RotateLoad(pMatrix, pVector, pOut);
}
#else
void VecRotateByMatrix(MATRIX *pMatrix, Vector3D *pVector, Vector3D *pOut) {
  VecRotateByMatrix_gen(pMatrix, pVector, pOut);
}
#endif

#if !M2C_VecRotateByLastMatrix
static void RotateLast(Vector3D *pIn, Vector3D *pOut) {
  MatWords r;
  GteGetRotation(r.m);
  RotateByMatrix(&r, pIn, pOut);
}

/* pIn rotated by the matrix in the GTE: the last one loaded. */
void VecRotateByLastMatrix(Vector3D *pIn, Vector3D *pOut) {
  SHADOW_VOID(VecRotateByLastMatrix, pOut, 12, VecRotateByLastMatrix_gen(pIn, pOut), RotateLast(pIn, pOut));
  RotateLast(pIn, pOut);
}
#else
void VecRotateByLastMatrix(Vector3D *pIn, Vector3D *pOut) { VecRotateByLastMatrix_gen(pIn, pOut); }
#endif

#if !M2C_VecRotateByCam
/* By the camera's view matrix, which stays in the GTE; the result in the
 * GTE's axes (MAC1-3), not the game's. */
static void RotateCam(Vector3D *pIn, Vector3D *pOut) {
  MatWords r;
  int mac[3];
  memcpy(&r, &g_Camera.m_ViewMatrix, sizeof r);
  LoadRotation(&r);
  RotateGameVector(r.m, pIn, mac);
  pOut->x = mac[0], pOut->y = mac[1], pOut->z = mac[2];
}

void VecRotateByCam(Vector3D *pIn, Vector3D *pOut) {
  SHADOW_VOID(VecRotateByCam, pOut, 12, VecRotateByCam_gen(pIn, pOut), RotateCam(pIn, pOut));
  RotateCam(pIn, pOut);
}
#else
void VecRotateByCam(Vector3D *pIn, Vector3D *pOut) { VecRotateByCam_gen(pIn, pOut); }
#endif

/* ===================================================================== */
/* Scaling and blending                                                   */
/* ===================================================================== */

#if !M2C_VecScaleToLength
static void ScaleToLength(Vector3D *pVec, int pCurrentLength, int pTargetLength) {
  *pVec = Scale(MipsDiv(Shl(pTargetLength, 12), pCurrentLength), pVec, 12);
}

void VecScaleToLength(Vector3D *pVec, int pCurrentLength, int pTargetLength) {
  SHADOW_VOID(VecScaleToLength, pVec, 12, VecScaleToLength_gen(pVec, pCurrentLength, pTargetLength),
              ScaleToLength(pVec, pCurrentLength, pTargetLength));
  ScaleToLength(pVec, pCurrentLength, pTargetLength);
}
#else
void VecScaleToLength(Vector3D *pVec, int pCurrentLength, int pTargetLength) {
  VecScaleToLength_gen(pVec, pCurrentLength, pTargetLength);
}
#endif

#if !M2C_func_80017330
/* pVec scaled to pNormalizedDistance (by the table root of its length
 * squared); 0 stays 0. Unlike the others the scaling multiplies the full
 * components. */
static void ScaleToDistance(Vector3D *pVec, int dist) {
  int len2 = LengthSq(pVec), f;
  if (!len2) {
    pVec->x = pVec->y = pVec->z = 0;
    return;
  }
  f = MipsDiv(Shl(dist, 12), TableSqrt(len2));
  pVec->x = (int)((u_int)pVec->x * (u_int)f) >> 12;
  pVec->y = (int)((u_int)pVec->y * (u_int)f) >> 12;
  pVec->z = (int)((u_int)pVec->z * (u_int)f) >> 12;
}

void func_80017330(Vector3D *pVec, int pNormalizedDistance) {
  SHADOW_VOID(func_80017330, pVec, 12, func_80017330_gen(pVec, pNormalizedDistance), ScaleToDistance(pVec, pNormalizedDistance));
  ScaleToDistance(pVec, pNormalizedDistance);
}
#else
void func_80017330(Vector3D *pVec, int pNormalizedDistance) { func_80017330_gen(pVec, pNormalizedDistance); }
#endif

#if !M2C_func_80017614
/* pVec x pScale (16-bit, GPL), each shifted right by pShift as a size:
 * negative components round towards 0. */
static void ScaleShift(Vector3D *pVec, int pScale, int pShift) {
  Vector3D v = Scale(pScale, pVec, 0);
  int *c[3] = {&v.x, &v.y, &v.z}, i;
  for (i = 0; i < 3; i++)
    *c[i] = *c[i] < 0 ? -(int)((0u - (u_int)*c[i]) >> (pShift & 31)) : (int)((u_int)*c[i] >> (pShift & 31));
  *pVec = v;
}

void func_80017614(Vector3D *pVec, int pScale, int pShift) {
  SHADOW_VOID(func_80017614, pVec, 12, func_80017614_gen(pVec, pScale, pShift), ScaleShift(pVec, pScale, pShift));
  ScaleShift(pVec, pScale, pShift);
}
#else
void func_80017614(Vector3D *pVec, int pScale, int pShift) { func_80017614_gen(pVec, pScale, pShift); }
#endif

#if !M2C_func_80017894
/* pA towards pB by pT (4096ths): GPF then GPL accumulate pT x pB and
 * (0x1000 - pT) x pA, each as 16-bit registers, >> 12. */
static void Blend(Vector3D *pOut, Vector3D *pA, Vector3D *pB, int pT) {
  int t = S16(pT), u = S16(0x1000 - pT);
  pOut->x = (int)((long long)(t * S16(pB->x)) + u * S16(pA->x)) >> 12;
  pOut->y = (int)((long long)(t * S16(pB->y)) + u * S16(pA->y)) >> 12;
  pOut->z = (int)((long long)(t * S16(pB->z)) + u * S16(pA->z)) >> 12;
}

void func_80017894(Vector3D *pOut, Vector3D *pA, Vector3D *pB, int pT) {
  SHADOW_VOID(func_80017894, pOut, 12, func_80017894_gen(pOut, pA, pB, pT), Blend(pOut, pA, pB, pT));
  Blend(pOut, pA, pB, pT);
}
#else
void func_80017894(Vector3D *pOut, Vector3D *pA, Vector3D *pB, int pT) { func_80017894_gen(pOut, pA, pB, pT); }
#endif

/* ===================================================================== */
/* Short vectors, triangles                                               */
/* ===================================================================== */

#if !M2C_VecToShortVec
void VecToShortVec(Vector3D16 *pOut, Vector3D *pVec) {
  Vector3D v = *pVec;
  SHADOW_VOID(VecToShortVec, pOut, 6, VecToShortVec_gen(pOut, pVec), (pOut->x = v.x, pOut->y = v.y, pOut->z = v.z));
  pOut->x = v.x, pOut->y = v.y, pOut->z = v.z;
}
#else
void VecToShortVec(Vector3D16 *pOut, Vector3D *pVec) { VecToShortVec_gen(pOut, pVec); }
#endif

#if !M2C_func_80017BFC
void func_80017BFC(Vector3D16 *pOut, Vector3D *pVec) {
  Vector3D v = *pVec;
  SHADOW_VOID(func_80017BFC, pOut, 6, func_80017BFC_gen(pOut, pVec),
              (pOut->x = v.x >> 2, pOut->y = v.y >> 2, pOut->z = v.z >> 2));
  pOut->x = v.x >> 2, pOut->y = v.y >> 2, pOut->z = v.z >> 2;
}
#else
void func_80017BFC(Vector3D16 *pOut, Vector3D *pVec) { func_80017BFC_gen(pOut, pVec); }
#endif

#if !M2C_func_80017C24
/* The halves read unsigned (lhu), then x 4. */
void func_80017C24(Vector3D *pOut, Vector3D16 *pIn) {
  int x = (u_short)pIn->x << 2, y = (u_short)pIn->y << 2, z = (u_short)pIn->z << 2;
  SHADOW_VOID(func_80017C24, pOut, 12, func_80017C24_gen(pOut, pIn), (pOut->x = x, pOut->y = y, pOut->z = z));
  pOut->x = x, pOut->y = y, pOut->z = z;
}
#else
void func_80017C24(Vector3D *pOut, Vector3D16 *pIn) { func_80017C24_gen(pOut, pIn); }
#endif

#if !M2C_func_80017C4C
void func_80017C4C(Vector3D *pOut, Vector3D16 *pIn) {
  int x = pIn->x, y = pIn->y, z = pIn->z;
  SHADOW_VOID(func_80017C4C, pOut, 12, func_80017C4C_gen(pOut, pIn), (pOut->x = x, pOut->y = y, pOut->z = z));
  pOut->x = x, pOut->y = y, pOut->z = z;
}
#else
void func_80017C4C(Vector3D *pOut, Vector3D16 *pIn) { func_80017C4C_gen(pOut, pIn); }
#endif

#if !M2C_func_80017C84
void func_80017C84(Vector3D16 *pOut, Vector3D16 *pIn1, Vector3D16 *pIn2) {
  int x = pIn1->x + pIn2->x, y = pIn1->y + pIn2->y, z = pIn1->z + pIn2->z;
  SHADOW_VOID(func_80017C84, pOut, 6, func_80017C84_gen(pOut, pIn1, pIn2), (pOut->x = x, pOut->y = y, pOut->z = z));
  pOut->x = x, pOut->y = y, pOut->z = z;
}
#else
void func_80017C84(Vector3D16 *pOut, Vector3D16 *pIn1, Vector3D16 *pIn2) { func_80017C84_gen(pOut, pIn1, pIn2); }
#endif

#if !M2C_ColTriUnpack
/* A terrain triangle's three corners in world units (A, then B and C from
 * its offsets: collision_hand.c's TRI_ fields), from m_Triangles. */
static void TriUnpack(int pIndex, Vector3D *pOut) {
  const u_int *w = (const u_int *)g_Environment.m_TerrainCollision->m_Triangles + pIndex * 3;
  int i;
  for (i = 0; i < 3; i++) {
    int a = w[i] & 0x3FFF;
    int b = a + (i < 2 ? (int)(w[i] << 9) >> 23 : (int)((w[i] << 8) >> 24));
    int c = a + (i < 2 ? (int)w[i] >> 23 : (int)(w[i] >> 24));
    (&pOut[0].x)[i] = a << 4, (&pOut[1].x)[i] = b << 4, (&pOut[2].x)[i] = c << 4;
  }
}

void ColTriUnpack(int pIndex, Vector3D *pOut) {
  SHADOW_VOID(ColTriUnpack, pOut, 36, ColTriUnpack_gen(pIndex, pOut), TriUnpack(pIndex, pOut));
  TriUnpack(pIndex, pOut);
}
#else
void ColTriUnpack(int pIndex, Vector3D *pOut) { ColTriUnpack_gen(pIndex, pOut); }
#endif

#if !M2C_func_80017428
/* Reflect pIn off a plane with normal pNormal (taken in 16ths) when pIn
 * goes into it (-pIn . n >= 0, the asm's MVMVA on -pIn): pOut = pIn + 2 x
 * the part of -pIn along n, returning 1; else pOut = pIn and 0. The asm
 * reuses its y register for the result before the scaling, so y's part is
 * always 0: pOut.y = pIn.y. Leaves R11-R13 and R21 as the asm does (-pIn,
 * and -pIn.z's high half). */
static int Reflect(Vector3D *pIn, Vector3D *pNormal, Vector3D *pOut) {
  Vector3D n = Shr(pNormal, 4), neg = {-pIn->x, -pIn->y, -pIn->z}, part;
  int d, q;
  GteSetRotationRow1(neg.x, neg.y, neg.z);
  d = Dot(&neg, &n);
  if (d < 0) {
    *pOut = *pIn;
    return 0;
  }
  q = MipsDiv(Shl(d, 6), LengthSq(&n));
  n.y = 0;
  part = Scale(q, &n, 6);
  pOut->x = part.x + (part.x - neg.x);
  pOut->y = part.y + (part.y - neg.y);
  pOut->z = part.z + (part.z - neg.z);
  return 1;
}

int func_80017428(Vector3D *pIn, Vector3D *pNormal, Vector3D *pOut) {
  SHADOW(func_80017428, pOut, 12, func_80017428_gen(pIn, pNormal, pOut), Reflect(pIn, pNormal, pOut));
  return Reflect(pIn, pNormal, pOut);
}
#else
int func_80017428(Vector3D *pIn, Vector3D *pNormal, Vector3D *pOut) { return func_80017428_gen(pIn, pNormal, pOut); }
#endif

#if !M2C_func_80017D7C
/* pOut = (pB - pA) x 0x1000 / (the largest of |pB - pA|'s components
 * >> (bit length of pLength / 2)), >> 12; returns that factor. With
 * pLength / 2 = 0 the asm stores pA's y in pOut's z and returns 32. */
static int ScaleBetween(Vector3D *pA, Vector3D *pB, Vector3D *pOut, int pLength) {
  int bits = 32 - LeadingSignBits((int)((u_int)pLength >> 1)), big, q;
  Vector3D d = Sub(pB, pA), m;
  if (bits == 0) {
    pOut->z = pA->y;
    return 32;
  }
  m.x = d.x < 0 ? -d.x : d.x, m.y = d.y < 0 ? -d.y : d.y, m.z = d.z < 0 ? -d.z : d.z;
  big = m.x - m.y >= 0 ? m.x : m.y;
  if (big - m.z < 0)
    big = m.z;
  q = MipsDiv(0x1000, (int)((u_int)big >> (bits & 31)));
  *pOut = Scale(q, &d, 12);
  return q;
}

int func_80017D7C(Vector3D *pA, Vector3D *pB, Vector3D *pOut, int pLength) {
  SHADOW(func_80017D7C, pOut, 12, func_80017D7C_gen(pA, pB, pOut, pLength), ScaleBetween(pA, pB, pOut, pLength));
  return ScaleBetween(pA, pB, pOut, pLength);
}
#else
int func_80017D7C(Vector3D *pA, Vector3D *pB, Vector3D *pOut, int pLength) {
  return func_80017D7C_gen(pA, pB, pOut, pLength);
}
#endif

/* ===================================================================== */
/* The camera projection                                                  */
/* ===================================================================== */

/* pIn on screen: RTPS with the camera's projection matrix (left in the
 * GTE, translation 0) on (pIn - camera) in the GTE's axes, shifted down by
 * pShift (each part a 16-bit V0 coordinate), with the screen offset, H and
 * the depth cue from the GTE. pOut = the screen x and y, and the depth
 * (MAC3) shifted back up. */
static void Project(Vector3D *pOut, Vector3D *pIn, int pShift) {
  NgteProj p = ngte_proj_get();
  MatWords r;
  NgteRtp o;
  memcpy(&r, &g_Camera.m_ProjectionMatrix, sizeof r);
  LoadRotation(&r);
  GteSetTranslation(0, 0, 0);
  p.r11 = r.m[0][0], p.r12 = r.m[0][1], p.r13 = r.m[0][2];
  p.r21 = r.m[1][0], p.r22 = r.m[1][1], p.r23 = r.m[1][2];
  p.r31 = r.m[2][0], p.r32 = r.m[2][1], p.r33 = r.m[2][2];
  p.trx = p.try_ = p.trz = 0;
  o = ngte_rtps(&p, S16(Sar(g_Camera.m_Position.y - pIn->y, pShift)), S16(Sar(g_Camera.m_Position.z - pIn->z, pShift)),
                S16(Sar(pIn->x - g_Camera.m_Position.x, pShift)));
  pOut->x = (short)o.sxy;
  pOut->y = (int)o.sxy >> 16;
  pOut->z = Shl(o.mac3, pShift);
}

#if !M2C_func_80017AA4
void func_80017AA4(Vector3D *pOut, Vector3D *pIn) {
  SHADOW_VOID(func_80017AA4, pOut, 12, func_80017AA4_gen(pOut, pIn), Project(pOut, pIn, 0));
  Project(pOut, pIn, 0);
}
#else
void func_80017AA4(Vector3D *pOut, Vector3D *pIn) { func_80017AA4_gen(pOut, pIn); }
#endif

#if !M2C_func_80017B48
void func_80017B48(Vector3D *pOut, Vector3D *pIn, int pShift) {
  SHADOW_VOID(func_80017B48, pOut, 12, func_80017B48_gen(pOut, pIn, pShift), Project(pOut, pIn, pShift));
  Project(pOut, pIn, pShift);
}
#else
void func_80017B48(Vector3D *pOut, Vector3D *pIn, int pShift) { func_80017B48_gen(pOut, pIn, pShift); }
#endif

/* ===================================================================== */
/* No callers (no jal, j or address of them anywhere): kept as the asm    */
/* ===================================================================== */

#if !M2C_func_80017188
/* pIn shifted down so its largest component fits 17 bits. */
static void Fit17(Vector3D *pOut, Vector3D *pIn) {
  Vector3D v = *pIn;
  int ax = v.x < 0 ? -v.x : v.x, ay = v.y < 0 ? -v.y : v.y, az = v.z < 0 ? -v.z : v.z;
  int s = 0x11 - LeadingSignBits(ax | ay | az);
  if (s > 0)
    v.x = Sar(v.x, s), v.y = Sar(v.y, s), v.z = Sar(v.z, s);
  *pOut = v;
}

void func_80017188(Vector3D *pOut, Vector3D *pIn) {
  SHADOW_VOID(func_80017188, pOut, 12, func_80017188_gen(pOut, pIn), Fit17(pOut, pIn));
  Fit17(pOut, pIn);
}
#else
void func_80017188(Vector3D *pOut, Vector3D *pIn) { func_80017188_gen(pOut, pIn); }
#endif

#if !M2C_func_8001771C
static int SameVector(const Vector3D *a, const Vector3D *b) { return a->x == b->x && a->y == b->y && a->z == b->z; }

int func_8001771C(Vector3D *pA, Vector3D *pB) {
  SHADOW(func_8001771C, NULL, 0, func_8001771C_gen(pA, pB), SameVector(pA, pB));
  return SameVector(pA, pB);
}
#else
int func_8001771C(Vector3D *pA, Vector3D *pB) { return func_8001771C_gen(pA, pB); }
#endif

/* OP (sf 0) of two vectors, leaving the first on the GTE's diagonal (each
 * as CTC2 loads a whole word: R11R12, R22R23, R33). */
static void OuterProduct(Vector3D *pOut, const Vector3D *d, const Vector3D *e) {
  GteSetRotationDiagonal(d->x, d->y, d->z);
  *pOut = Cross(d, e);
}

#if !M2C_func_80017548
/* (pB - pA) x (pC - pA). */
static void TriangleCross(Vector3D *pA, Vector3D *pB, Vector3D *pC, Vector3D *pOut) {
  Vector3D d = Sub(pB, pA), e = Sub(pC, pA);
  OuterProduct(pOut, &d, &e);
}

void func_80017548(Vector3D *pA, Vector3D *pB, Vector3D *pC, Vector3D *pOut) {
  SHADOW_VOID(func_80017548, pOut, 12, func_80017548_gen(pA, pB, pC, pOut), TriangleCross(pA, pB, pC, pOut));
  TriangleCross(pA, pB, pC, pOut);
}
#else
void func_80017548(Vector3D *pA, Vector3D *pB, Vector3D *pC, Vector3D *pOut) { func_80017548_gen(pA, pB, pC, pOut); }
#endif

#if !M2C_func_80017854
static void CrossOf(Vector3D *pOut, Vector3D *pA, Vector3D *pB) {
  Vector3D a = *pA, b = *pB;
  OuterProduct(pOut, &a, &b);
}

void func_80017854(Vector3D *pOut, Vector3D *pA, Vector3D *pB) {
  SHADOW_VOID(func_80017854, pOut, 12, func_80017854_gen(pOut, pA, pB), CrossOf(pOut, pA, pB));
  CrossOf(pOut, pA, pB);
}
#else
void func_80017854(Vector3D *pOut, Vector3D *pA, Vector3D *pB) { func_80017854_gen(pOut, pA, pB); }
#endif

#if MATH_SHADOW
/* ===================================================================== */
/* Differential fuzzing                                                   */
/* ===================================================================== */

/* Each round calls every entry (the four without callers too) with random
 * arguments in host buffers, which both the unit and the hand C can
 * address; the shadow in each compares them. The GTE's rotation and
 * translation are random too, and so is the camera for the calls that read
 * it; both are put back afterwards. Seeded from the sample's state. */
static u_int s_rng;

static u_int FuzzRand(void) {
  s_rng = s_rng * 1103515245u + 12345u;
  return s_rng >> 4;
}

/* Small, 16-bit (edges included), world-sized or wide values. */
static int FuzzInt(void) {
  static const int edges[] = {0, 1, -1, 0x7FFF, -0x8000, 0x8000, -0x8001, 0xFFFF, 0x10000};
  switch (FuzzRand() % 6) {
  case 0:
    return (int)(FuzzRand() % 129) - 64;
  case 1:
    return (short)FuzzRand();
  case 2:
    return edges[FuzzRand() % (sizeof edges / sizeof *edges)];
  case 3:
    return (int)(FuzzRand() % 0x100001) - 0x80000;
  case 4:
    return (int)(FuzzRand() % 0x40000);
  default:
    return (int)(FuzzRand() << 4) >> (FuzzRand() % 8);
  }
}

static Vector3D FuzzVec(void) {
  Vector3D v;
  v.x = FuzzInt(), v.y = FuzzInt(), v.z = FuzzInt();
  if (FuzzRand() % 4 == 0)
    v.x = v.y = v.z = (short)FuzzRand() >> (FuzzRand() % 12); /* all one size */
  return v;
}

static void FuzzMatrix(MATRIX *m) {
  short *h = (short *)m;
  int i;
  for (i = 0; i < 10; i++)
    h[i] = FuzzRand() % 3 ? (int)(FuzzRand() % 0x2001) - 0x1000 : (short)FuzzRand();
}

static void MathFuzz(int rounds) {
  GTERegisters gte = gteRegs;
  Camera cam = g_Camera;
  int i, k;
  s_rng = (u_int)g_Camera.m_Position.x * 31u + (u_int)g_Camera.m_Position.y * 17u + (u_int)gteRegs.CP2C.p[0].d;
  for (i = 0; i < rounds; i++) {
    Vector3D a = FuzzVec(), b = FuzzVec(), c = FuzzVec(), o = FuzzVec(), tri[3];
    Vector3D16 a16, b16, o16;
    MATRIX m, mi, mo;
    int x = FuzzInt(), y = FuzzInt(), n = FuzzInt();
    u_int rot = FuzzRand();
    a16.x = FuzzRand(), a16.y = FuzzRand(), a16.z = FuzzRand();
    b16.x = FuzzRand(), b16.y = FuzzRand(), b16.z = FuzzRand();
    FuzzMatrix(&m), FuzzMatrix(&mi), FuzzMatrix(&mo);
    for (k = 0; k < 8; k++)
      gteRegs.CP2C.p[k].d = k == 4 ? (short)FuzzRand() : k < 4 ? FuzzRand() * 0x10001u : FuzzInt();
    if (FuzzRand() % 2) {
      FuzzMatrix((MATRIX *)&g_Camera.m_ViewMatrix);
      FuzzMatrix((MATRIX *)&g_Camera.m_ProjectionMatrix);
      g_Camera.m_Position = FuzzVec();
    }
    /* angles and roots: |y|, |x| below 2^30 (the tables are indexed by
     * their ratio; the game never passes more) */
    Atan2Fast(y >> 2, x >> 2);
    Atan2(y >> 2, x >> 2, FuzzRand() % 2);
    Atan2((int)(FuzzRand() % 0x10000) - 0x8000, (int)(FuzzRand() % 0x10000) - 0x8000, 1);
    Sin(n), Cos(n), func_80016D08(n);
    func_80017908(x, y), func_80017928(x, y), func_80017948(x, y), func_8001796C(x, y);
    func_800179F0(x & 0xFF, y & 0xFF, FuzzRand() % 0x40, FuzzRand() % 0x40);
    func_80017A38(n < 0 ? -n : n);
    /* matrices */
    RotVec8ToMatrix((Vector3D8 *)&rot, &mo, FuzzRand() % 2 ? &mi : NULL);
    func_80016FD0(&mo, &mi);
    VecRotateByMatrix(&m, &a, &o);
    VecRotateByLastMatrix(&b, &o);
    VecRotateByCam(&c, &o);
    /* lengths and scaling */
    VecMagnitude(&a, FuzzRand() % 2);
    VecRefineMagnitude(&a, FuzzRand() % 0x8000, FuzzRand() % 2);
    OctDistance(&a, &b);
    b = a, func_80017330(&b, FuzzInt());
    b = a, VecScaleToLength(&b, FuzzRand() % 4 ? FuzzInt() : 0, FuzzInt());
    b = a, func_80017614(&b, FuzzInt(), FuzzRand() % 40);
    b = a, VecShiftLeft(&b, FuzzRand() % 40);
    b = a, VecShiftRight(&b, FuzzRand() % 40);
    VecMult(&o, &a, FuzzInt());
    func_800177F8(&o, &a, FuzzRand() % 4 ? FuzzInt() : 0);
    func_80017894(&o, &a, &c, FuzzInt());
    func_80017D7C(&a, &c, &o, FuzzInt());
    /* vectors */
    VecNull(&o), VecCopy(&o, &a), VecAdd(&o, &a, &c), VecSub(&o, &a, &c);
    func_8001771C(&a, FuzzRand() % 2 ? &a : &c);
    func_80017188(&o, &a);
    func_80017428(&a, &c, &o);
    func_80017548(&a, &b, &c, &o);
    func_80017854(&o, &a, &c);
    func_80017BFC(&o16, &a), VecToShortVec(&o16, &a), func_80017C84(&o16, &a16, &b16);
    func_80017C24(&o, &a16), func_80017C4C(&o, &b16);
    /* the camera's projection (reads OFX, OFY, H and the depth cue as the
     * sample left them) */
    c = g_Camera.m_Position, c.x += x >> 6, c.y += y >> 6, c.z += n >> 6;
    func_80017AA4(&o, &c);
    func_80017B48(&o, &c, FuzzRand() % 13);
    if (g_Environment.m_TerrainCollision && g_Environment.m_TerrainCollision->m_TriangleCount > 0)
      ColTriUnpack(FuzzRand() % g_Environment.m_TerrainCollision->m_TriangleCount, tri);
  }
  gteRegs = gte;
  g_Camera = cam;
  printf("  mathfuzz %d rounds, %d mismatches\n", rounds, g_math_shadow_fail);
}
#endif

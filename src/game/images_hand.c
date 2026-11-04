/* Colours and images (asm/images.s): the colour blend, the grey copy of the
 * screen and the logos' brightness steps, written by hand from the asm so
 * it reads as C (docs/native-plan.md stage 3, decisions D94). Replaces the
 * mips2c unit (cmake/mips2c.cmake M2C_HAND_MULTI); checked against the unit
 * with the shadows of hand_shadow.h and against PCSX-Redux with the oracle
 * replay (docs/translation-log.md).
 *
 * The GTE: ColorLerp is the GTE's DPCS. It sets the far colour (RFC, GFC,
 * BFC, control registers) and leaves it; the hand C sets it the same, as
 * state other code may read. The colour, IR0 and the result it reads back
 * are data registers, a calculator's leftovers (D87): the hand C computes
 * in locals.
 *
 * M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) takes that function from the mips2c
 * unit instead, linked as <name>_gen, for A/B tests. */
#include "common.h"

#include "hand_shadow.h"
#include "images.h"

#include <gtereg.h>

/* The mips2c unit's entries, linked as <name>_gen for A/B tests and the
 * shadows (cmake M2C_HAND_MULTI). */
u_int ColorLerp_gen(), func_80017E98_gen(), func_80017F24_gen();

/* One channel of DPCS (sf 1, lm 0): the colour moved towards the far
 * colour by t/4096, in steps of 1/65536 rounded down, then 0 to 255. Both
 * channels are 8 bits, so IR1-3 never saturate; only t, a 16-bit IR0, can
 * take the result out of range. */
static int Lerp8(int from, int to, int t) {
  int v = (from * 4096 + t * (to - from)) >> 12;
  return v < 0 ? 0 : v > 255 ? 255 : v;
}

#if M2C_ColorLerp
int ColorLerp(int pColor1, int pColor2, int pAmount) { return ColorLerp_gen(pColor1, pColor2, pAmount); }
#else
static int Blend(u_int c1, u_int c2, int t) {
  t = (short)t; /* IR0 */
  gteRegs.CP2C.p[21].d = (c2 << 4) & 0xFF0;  /* RFC */
  gteRegs.CP2C.p[22].d = (c2 >> 4) & 0xFF0;  /* GFC */
  gteRegs.CP2C.p[23].d = (c2 >> 12) & 0xFF0; /* BFC */
  return (int)((c1 & 0xFF000000u) | (u_int)Lerp8((c1 >> 16) & 0xFF, (c2 >> 16) & 0xFF, t) << 16 |
               (u_int)Lerp8((c1 >> 8) & 0xFF, (c2 >> 8) & 0xFF, t) << 8 | (u_int)Lerp8(c1 & 0xFF, c2 & 0xFF, t));
}

#if HAND_SHADOW
/* Fuzzing (SPYRO_HAND_FUZZ=N): random colours, and amounts over IR0's
 * whole range and past it (the low 16 bits count). */
static void FuzzBlend(u_int seed) {
  int n = HandFuzzBegin("ColorLerp", seed), k;
  for (k = 0; k < n; k++) {
    u_int c1 = HandFuzzRand() << 8 ^ HandFuzzRand(), c2 = HandFuzzRand() << 8 ^ HandFuzzRand();
    int t = k & 1 ? (int)(HandFuzzRand() % 0x1001) : (int)(HandFuzzRand() << 8 ^ HandFuzzRand());
    HAND_FUZZ_ONE(ColorLerp, ColorLerp_gen(c1, c2, t), Blend(c1, c2, t));
  }
}
#endif

/* pColor1 towards pColor2 by pAmount/4096, channel by channel; the code
 * byte (bits 24-31) is pColor1's. */
int ColorLerp(int pColor1, int pColor2, int pAmount) {
#if HAND_SHADOW
  FuzzBlend(pColor1 ^ pColor2 ^ pAmount);
#endif
  HAND_SHADOW_CALL(ColorLerp, ColorLerp_gen(pColor1, pColor2, pAmount), Blend(pColor1, pColor2, pAmount));
  return Blend(pColor1, pColor2, pAmount);
}
#endif

#if M2C_func_80017E98
void func_80017E98(void *buf, int size) { func_80017E98_gen(buf, size); }
#else
/* A 15-bit pixel's grey: (4 red + 3 green + blue) / 8, 0 to 31. */
static inline u_char Grey(u_int p) { return (u_char)((((p & 0x1F) << 2) + 3 * ((p >> 5) & 0x1F) + ((p >> 10) & 0x1F)) >> 3); }

static void GreyCopy(u_char *buf, int size) {
  const u_int *in = (const u_int *)buf;
  u_char *out = buf, *end = buf + size;
  /* Two pixels in, two bytes out, over the buffer itself: the output
   * trails the input. The asm stops when the output reaches the end
   * (exactly: an odd size would run on). */
  do {
    u_int two = *in++;
    out[0] = Grey(two);
    out[1] = Grey(two >> 16);
    out += 2;
  } while (out != end);
}

/* The screen's BGR555 pixels as greys, a byte each, in place: size bytes
 * out from size*2 in. */
void func_80017E98(void *buf, int size) {
  HAND_SHADOW_VOID(func_80017E98, func_80017E98_gen(buf, size), GreyCopy(buf, size));
  GreyCopy(buf, size);
}
#endif

#if M2C_func_80017F24
void func_80017F24(void *in, void *out, int valueOffset) { func_80017F24_gen(in, out, valueOffset); }
#else
/* A byte plus the offset, kept only when above 0; there is no upper clamp,
 * so a sum past 255 carries into the next byte up, as the asm's adds do. */
static inline u_int Step(u_int byte, int offset, int shift) {
  int v = (int)byte + offset;
  return v > 0 ? (u_int)v << shift : 0;
}

/* in: runs of words, each after a header word (bits 0-16: words of out to
 * skip first; 17-31: words in the run), until a negative header. Each word
 * goes to out with offset added to each of its four bytes. */
static void Brighten(const u_int *in, u_int *out, int offset) {
  int header = (int)*in++;
  u_int word = *in++;
  while (header >= 0) {
    u_int *end;
    out += header & 0x1FFFF;
    end = out + ((u_int)header >> 17);
    /* At least one word: a run of 0 would never end, as in the asm. */
    do {
      u_int w = word;
      word = *in++;
      *out++ = Step(w >> 24, offset, 24) + Step((w >> 16) & 0xFF, offset, 16) + Step((w >> 8) & 0xFF, offset, 8) +
               Step(w & 0xFF, offset, 0);
    } while (out != end);
    header = (int)word;
    word = *in++;
  }
}

/* The logos fading in and out: the image in, brightened by valueOffset
 * (negative darkens), to out. */
void func_80017F24(void *in, void *out, int valueOffset) {
  HAND_SHADOW_VOID(func_80017F24, func_80017F24_gen(in, out, valueOffset), Brighten(in, out, valueOffset));
  Brighten(in, out, valueOffset);
}
#endif

/* src/game/gte_inline.h (fps plan C5, decisions D70) against PsyCross's
 * GTE_operator, which test_gte checks against PCSX-Redux.
 * usage: test_gte_inline [gte.bin]
 * 1. Every PCSX-Redux vector (gte.bin, if given and present) through the
 *    inline path, compared with PCSX-Redux's result as test_gte compares.
 * 2. Every opcode the translated code issues, as a compile-time constant
 *    (the way mips2c_rt.h uses it), on random register states: all 64
 *    registers' raw storage must equal GTE_operator's.
 * 3. Every GTE function with every sf/lm/mx/v/cv, decoded at run time, on
 *    random states, the same comparison.
 * 4. gte_divide's leading-zero count (clz here) for every denominator. */
#include "gte_inline.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t addr, word, vblank;
  uint32_t before[64], after[64];
} Record;

/* PsyX_GTE.cpp's gte_leadingzerocount (for part 4). */
unsigned int gte_leadingzerocount(unsigned int lzcs);

/* The functions mips2c_rt.h calls for the translated code's constant
 * opcodes (src/game/gte_ops.c), and NCLIP's inline one. */
#define AS_ENTRY(op) {op, m2c_gte_##op},
static const struct {
  unsigned op;
  void (*fn)(void);
} k_const[] = {M2C_GTE_OPS(AS_ENTRY) {0x1400006, m2c_gte_0x1400006}};

static uint64_t s_rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 7;
  s_rng ^= s_rng << 17;
  return (uint32_t)s_rng;
}

/* A register value: half fully random, half small or edge values, so both
 * the saturating and the in-range paths run. */
static uint32_t rnd_reg(void) {
  switch (rnd() % 8) {
  case 0: case 1: case 2: case 3:
    return rnd();
  case 4:
    return rnd() & 0x00FF00FFu;
  case 5:
    return (rnd() & 0x0FFF0FFFu) | ((rnd() & 1) ? 0xF000F000u : 0);
  case 6: {
    static const uint32_t edge[] = {0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x7FFF7FFF, 0x80008000u,
                                    0xFFFFFFFFu, 0x1000, 0x10001000, 0x7FFFFFFF, 0x80000000u};
    return edge[rnd() % (sizeof edge / sizeof edge[0])];
  }
  default:
    return rnd() & 0xFFFu;
  }
}

static uint32_t s_state[64];
static void random_state(void) {
  for (int i = 0; i < 64; i++)
    s_state[i] = rnd_reg();
}
static void load_state(const uint32_t *s) {
  memcpy(gteRegs.CP2D.r, s, 32 * 4);
  memcpy(gteRegs.CP2C.r, s + 32, 32 * 4);
}
static void save_state(uint32_t *s) {
  memcpy(s, gteRegs.CP2D.r, 32 * 4);
  memcpy(s + 32, gteRegs.CP2C.r, 32 * 4);
}

static unsigned s_fail;
static void compare(const char *what, unsigned op, const uint32_t *want, const uint32_t *have) {
  for (int i = 0; i < 64; i++) {
    if (want[i] == have[i])
      continue;
    if (s_fail++ < 20)
      printf("  %s op %07X: reg %d want %08X got %08X\n", what, op, i, want[i], have[i]);
  }
}

/* Same register view as test_gte's cpu_view, for the PCSX-Redux vectors. */
static uint32_t clamp5(int32_t ir) {
  ir >>= 7;
  return ir < 0 ? 0 : ir > 0x1F ? 0x1F : (uint32_t)ir;
}
static uint32_t lzc(uint32_t v) {
  uint32_t n = 0;
  if (v & 0x80000000u)
    v = ~v;
  while (n < 32 && !(v & 0x80000000u)) {
    v <<= 1;
    n++;
  }
  return n;
}
static uint32_t cpu_view(const uint32_t *r, int i) {
  switch (i) {
  case 1: case 3: case 5: case 8: case 9: case 10: case 11:
    return (uint32_t)(int32_t)(int16_t)r[i];
  case 7: case 16: case 17: case 18: case 19:
    return r[i] & 0xFFFF;
  case 15:
    return r[14];
  case 28: case 29:
    return clamp5((int16_t)r[9]) | clamp5((int16_t)r[10]) << 5 | clamp5((int16_t)r[11]) << 10;
  case 31:
    return lzc(r[30]);
  case 32 + 4: case 32 + 12: case 32 + 20: case 32 + 26: case 32 + 27:
  case 32 + 29: case 32 + 30:
    return (uint32_t)(int32_t)(int16_t)r[i];
  }
  return r[i];
}

int main(int argc, char **argv) {
  uint32_t want[64], have[64];
  unsigned n;

  /* 1. PCSX-Redux vectors */
  FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
  if (f) {
    Record rec;
    unsigned nv = 0, bad = 0;
    while (fread(&rec, sizeof rec, 1, f) == 1) {
      load_state(rec.before);
      m2c_gte_op(rec.word & 0x1FFFFFF);
      save_state(have);
      int diff = 0;
      for (int i = 0; i < 64; i++)
        if (cpu_view(rec.after, i) != cpu_view(have, i))
          diff = 1;
      bad += diff;
      nv++;
    }
    fclose(f);
    printf("test_gte_inline: %u PCSX-Redux vectors, %u mismatched\n", nv, bad);
    s_fail += bad;
  } else {
    printf("test_gte_inline: no PCSX-Redux vectors (part 1 skipped)\n");
  }

  /* 2. the constant opcodes */
  n = 0;
  for (unsigned k = 0; k < sizeof k_const / sizeof k_const[0]; k++)
    for (int t = 0; t < 200000; t++, n++) {
      random_state();
      load_state(s_state);
      GTE_operator(k_const[k].op);
      save_state(want);
      load_state(s_state);
      k_const[k].fn();
      save_state(have);
      compare("const", k_const[k].op, want, have);
    }
  printf("test_gte_inline: %u random states for %u constant opcodes\n", n,
         (unsigned)(sizeof k_const / sizeof k_const[0]));

  /* 3. every function and field combination, decoded at run time */
  static const unsigned functs[] = {0x01, 0x06, 0x0C, 0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x1B, 0x1C,
                                    0x1E, 0x20, 0x28, 0x29, 0x2A, 0x2D, 0x2E, 0x30, 0x3D, 0x3E, 0x3F};
  n = 0;
  for (unsigned k = 0; k < sizeof functs / sizeof functs[0]; k++)
    for (unsigned fields = 0; fields < 0x80; fields++) {
      unsigned op = functs[k] | (fields & 1) << 10 | ((fields >> 1) & 1) << 19 |
                    ((fields >> 2) & 3) << 13 | ((fields >> 4) & 3) << 15;
      for (int mx = 0; mx < 4; mx++) {
        if (functs[k] != 0x12 && mx)
          break;
        op = (op & ~(3u << 17)) | (unsigned)mx << 17;
        for (int t = 0; t < 400; t++, n++) {
          random_state();
          load_state(s_state);
          GTE_operator((int)op);
          save_state(want);
          load_state(s_state);
          m2c_gte_op(op);
          save_state(have);
          compare("decoded", op, want, have);
        }
      }
    }
  printf("test_gte_inline: %u random states over every function and field\n", n);

  /* 4. clz for gte_divide */
  for (unsigned d = 1; d <= 0xFFFF; d++)
    if ((unsigned)__builtin_clz(d) != gte_leadingzerocount(d) && s_fail++ < 20)
      printf("  clz(%u) %u, PsyCross %u\n", d, (unsigned)__builtin_clz(d), gte_leadingzerocount(d));

  printf("test_gte_inline: %s\n", s_fail ? "FAILED" : "all match");
  return s_fail ? 1 : 0;
}

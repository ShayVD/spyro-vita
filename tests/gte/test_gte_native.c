/* src/game/gte_native.h (fast_render=2, decisions D79) against
 * src/game/gte_inline.h's register-file commands, which test_gte_inline
 * checks against PsyCross and PCSX-Redux.
 * usage: test_gte_native [gte.bin]
 * 1. Every PCSX-Redux vector (gte.bin, if given and present) of an opcode the
 *    native functions cover: the native function on the vector's input
 *    registers, every field it returns against PCSX-Redux's result.
 * 2. Random register states (half random words, half small or edge values):
 *    each native function against the inline command on the same state,
 *    every returned field against the register it stands for.
 * Built a second time with NGTE_MUTATE (one deliberate off-by-one in
 * gte_native.h's arithmetic), which must fail (ctest WILL_FAIL). */
#ifdef NGTE_MUTATE
/* The mutation: RTPS's screen offset one pixel off. Checked by building
 * this file with -DNGTE_MUTATE. */
#define ngte_proj_get ngte_proj_get_real
#endif
#include "gte_native.h"
#ifdef NGTE_MUTATE
#undef ngte_proj_get
static NgteProj ngte_proj_get(void) {
  NgteProj p = ngte_proj_get_real();
  p.ofx += 0x10000; /* one pixel */
  return p;
}
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t addr, word, vblank;
  uint32_t before[64], after[64];
} Record;

static uint64_t s_rng = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 7;
  s_rng ^= s_rng << 17;
  return (uint32_t)s_rng;
}
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

static void load_state(const uint32_t *s) {
  memcpy(gteRegs.CP2D.r, s, 32 * 4);
  memcpy(gteRegs.CP2C.r, s + 32, 32 * 4);
}

static unsigned s_fail, s_checked;
static void check(const char *op, const char *field, uint32_t want, uint32_t have) {
  s_checked++;
  if (want == have)
    return;
  if (s_fail++ < 20)
    printf("  %s %s: want %08X got %08X\n", op, field, want, have);
}

/* The register view MFC2/CFC2 would give, from raw storage (test_gte's). */
static uint32_t d_reg(int i) { return gteRegs.CP2D.r[i]; }
static uint32_t ir_view(int i) { return (uint32_t)(int32_t)(int16_t)gteRegs.CP2D.r[i]; }

/* Run one opcode both ways on the state in S and compare. FROM_REDUX: the
 * want side is PCSX-Redux's result (AFTER) instead of gte_inline.h's. */
static void run(unsigned op, const uint32_t *s, const uint32_t *after) {
  uint32_t want[64];
  int i;
  load_state(s);
  if (after) {
    for (i = 0; i < 64; i++)
      want[i] = after[i];
  } else {
    m2c_gte_op(op);
    for (i = 0; i < 32; i++)
      want[i] = gteRegs.CP2D.r[i], want[32 + i] = gteRegs.CP2C.r[i];
    load_state(s);
  }
#define W_IR(i) ((uint32_t)(int32_t)(int16_t)want[i])
#define W_U16(i) (want[i] & 0xFFFF)
  switch (op) {
  case 0x180001: { /* RTPS */
    NgteProj p = ngte_proj_get();
    NgteRtp o = ngte_rtps_w(&p, d_reg(0), d_reg(1));
    check("RTPS", "MAC1", want[25], (uint32_t)o.mac1);
    check("RTPS", "MAC2", want[26], (uint32_t)o.mac2);
    check("RTPS", "MAC3", want[27], (uint32_t)o.mac3);
    check("RTPS", "IR1", W_IR(9), (uint32_t)o.ir1);
    check("RTPS", "IR2", W_IR(10), (uint32_t)o.ir2);
    check("RTPS", "IR3", W_IR(11), (uint32_t)o.ir3);
    check("RTPS", "SZ3", W_U16(19), o.sz3);
    check("RTPS", "SXY2", want[14], o.sxy);
    check("RTPS", "MAC0", want[24], (uint32_t)o.mac0);
    check("RTPS", "IR0", W_IR(8), (uint32_t)o.ir0);
    check("RTPS", "FLAG", want[63], o.flag);
    break;
  }
  case 0x1400006: /* NCLIP */
    check("NCLIP", "MAC0", want[24], (uint32_t)ngte_nclip(d_reg(12), d_reg(13), d_reg(14)));
    check("NCLIP", "FLAG", want[63], 0);
    break;
  case 0x780010: { /* DPCS */
    NgteCol o = ngte_dpcs(d_reg(6), (int16_t)d_reg(8), C2_RFC, C2_GFC, C2_BFC);
    goto col;
  case 0x980011: /* INTPL */
    o = ngte_intpl((int16_t)d_reg(9), (int16_t)d_reg(10), (int16_t)d_reg(11), (int16_t)d_reg(8),
                   C2_RFC, C2_GFC, C2_BFC, d_reg(6) >> 24);
    goto col;
  case 0x190003D: /* GPF sf 0 */
    o = ngte_gpf0((int16_t)d_reg(9), (int16_t)d_reg(10), (int16_t)d_reg(11), (int16_t)d_reg(8),
                  d_reg(6) >> 24);
  col:
    check("col", "MAC1", want[25], (uint32_t)o.mac1);
    check("col", "MAC2", want[26], (uint32_t)o.mac2);
    check("col", "MAC3", want[27], (uint32_t)o.mac3);
    check("col", "IR1", W_IR(9), (uint32_t)o.ir1);
    check("col", "IR2", W_IR(10), (uint32_t)o.ir2);
    check("col", "IR3", W_IR(11), (uint32_t)o.ir3);
    check("col", "RGB2", want[22], o.rgb2);
    check("col", "FLAG", want[63], o.flag);
    break;
  }
  case 0x49E012: /* MVMVA R x IR */
  case 0x486012: /* MVMVA R x V0 */
  case 0x480012: { /* MVMVA R x V0 + TR */
    NgteMat m = ngte_rot_get();
    NgteVec o;
    if (op == 0x49E012)
      o = ngte_mvmva(&m, 0, 0, 0, ir_view(9), ir_view(10), ir_view(11));
    else if (op == 0x486012)
      o = ngte_mvmva(&m, 0, 0, 0, ngte_vx(d_reg(0)), ngte_vy(d_reg(0)), ngte_vz(d_reg(1)));
    else
      o = ngte_mvmva(&m, C2_TRX, C2_TRY, C2_TRZ, ngte_vx(d_reg(0)), ngte_vy(d_reg(0)), ngte_vz(d_reg(1)));
    check("MVMVA", "MAC1", want[25], (uint32_t)o.mac1);
    check("MVMVA", "MAC2", want[26], (uint32_t)o.mac2);
    check("MVMVA", "MAC3", want[27], (uint32_t)o.mac3);
    check("MVMVA", "IR1", W_IR(9), (uint32_t)o.ir1);
    check("MVMVA", "IR2", W_IR(10), (uint32_t)o.ir2);
    check("MVMVA", "IR3", W_IR(11), (uint32_t)o.ir3);
    check("MVMVA", "FLAG", want[63], o.flag);
    break;
  }
  }
}

static const unsigned k_ops[] = {0x180001, 0x1400006, 0x780010, 0x980011, 0x190003D,
                                 0x49E012, 0x486012, 0x480012};

int main(int argc, char **argv) {
  uint32_t s[64];
  unsigned n = 0, k;

  /* 1. PCSX-Redux vectors */
  FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
  if (f) {
    Record rec;
    unsigned nv = 0, used = 0, fail0 = s_fail;
    while (fread(&rec, sizeof rec, 1, f) == 1) {
      unsigned op = rec.word & 0x1FFFFFF;
      nv++;
      for (k = 0; k < sizeof k_ops / sizeof k_ops[0]; k++)
        if (op == k_ops[k]) {
          run(op, rec.before, rec.after);
          used++;
        }
    }
    fclose(f);
    printf("test_gte_native: %u PCSX-Redux vectors, %u of the native opcodes, %u mismatched fields\n",
           nv, used, s_fail - fail0);
  } else {
    printf("test_gte_native: no PCSX-Redux vectors (part 1 skipped)\n");
  }

  /* 2. random states against gte_inline.h */
  for (k = 0; k < sizeof k_ops / sizeof k_ops[0]; k++)
    for (int t = 0; t < 1000000; t++, n++) {
      for (int i = 0; i < 64; i++)
        s[i] = rnd_reg();
      run(k_ops[k], s, NULL);
    }
  printf("test_gte_native: %u random states over %u opcodes, %u fields compared\n", n,
         (unsigned)(sizeof k_ops / sizeof k_ops[0]), s_checked);
  printf("test_gte_native: %s\n", s_fail ? "FAILED" : "all match");
  return s_fail ? 1 : 0;
}

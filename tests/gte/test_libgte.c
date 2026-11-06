/* Our libgte and register moves (src/game/libgte.c, gte_regs.c; stage 8c,
 * decisions D116) against PsyCross's (src/psx/LIBGTE.C, INLINE_C.C, built
 * as the reference library psycross_ref with their names prefixed psyx_).
 * usage: test_libgte
 * Each function runs on the same random register state and inputs both
 * ways; the whole register file (CP0, the 32 data and 32 control registers'
 * raw storage), the return value and the matrices must be identical after
 * every call. Two exceptions, both PsyCross reading its own uninitialised
 * stack (libgte.c's header): MulMatrix's m0->t and pad, which callers never
 * read and PsyQ leaves alone, aren't compared; nor is VZ0's high half after
 * MulMatrix0 (its count of differing calls is printed). The mutant builds
 * (LIBGTE_MUTATE 1: MulMatrix's FLAG; 2: MTC2 30's count) must fail. */
#include <libgte.h>
#include <gtereg.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void psyx_InitGeom(void);
void psyx_SetGeomOffset(int ofx, int ofy);
void psyx_SetGeomScreen(int h);
void psyx_SetTransMatrix(MATRIX *m);
MATRIX *psyx_MulMatrix(MATRIX *m0, MATRIX *m1);
MATRIX *psyx_MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2);
void psyx_MTC2(unsigned int value, int reg);
unsigned int psyx_MFC2(int reg);
void psyx_CTC2(unsigned int value, int reg);
unsigned int psyx_CFC2(int reg);
/* ours (gte_regs.c), as mips2c_rt.h declares them */
void MTC2(unsigned int value, int reg);
unsigned int MFC2(int reg);
void CTC2(unsigned int value, int reg);
unsigned int CFC2(int reg);

static uint64_t s_rng = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 7;
  s_rng ^= s_rng << 17;
  return (uint32_t)s_rng;
}

/* A register word: random, small, or an edge value (tests/gte/test_gte_inline.c's). */
static uint32_t rnd_reg(void) {
  static const uint32_t edge[] = {0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x7FFF7FFF, 0x80008000u,
                                  0xFFFFFFFFu, 0x1000, 0x10001000, 0x7FFFFFFF, 0x80000000u};
  switch (rnd() % 8) {
  case 0: case 1: case 2: case 3:
    return rnd();
  case 4:
    return rnd() & 0x00FF00FFu;
  case 5:
    return (rnd() & 0x0FFF0FFFu) | ((rnd() & 1) ? 0xF000F000u : 0);
  case 6:
    return edge[rnd() % (sizeof edge / sizeof edge[0])];
  default:
    return rnd() & 0xFFFu;
  }
}

/* A matrix element: a rotation's (|v| <= 4096, the game's), any 16-bit
 * value (IR saturates and flags), or an edge. */
static short rnd_elem(int kind) {
  static const short edge[] = {0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000};
  switch (kind) {
  case 0:
    return (short)((int)(rnd() % 8193) - 4096);
  case 1:
    return (short)rnd();
  default:
    return rnd() % 4 ? (short)((int)(rnd() % 8193) - 4096) : edge[rnd() % 7];
  }
}
static void rnd_matrix(MATRIX *m) {
  int kind = (int)(rnd() % 3);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      m->m[i][j] = rnd_elem(kind);
  *((short *)m + 9) = (short)rnd(); /* the pad */
  for (int i = 0; i < 3; i++)
    m->t[i] = (int)rnd_reg();
}

static GTERegisters s_state, s_want;
static void random_state(void) {
  uint32_t *w = (uint32_t *)&s_state;
  for (size_t i = 0; i < sizeof s_state / 4; i++)
    w[i] = rnd_reg();
}

static unsigned s_fail;
static void compare_regs(const char *what, const GTERegisters *want, const GTERegisters *have) {
  const uint32_t *a = (const uint32_t *)want, *b = (const uint32_t *)have;
  static const char *const bank[] = {"CP0", "CP2D", "CP2C"};
  for (size_t i = 0; i < sizeof *want / 4; i++) {
    if (a[i] == b[i])
      continue;
    if (s_fail++ < 20)
      printf("  %s: %s %u want %08X got %08X\n", what, bank[i / 32], (unsigned)(i % 32), a[i], b[i]);
  }
}
static void check(int ok, const char *what) {
  if (!ok && s_fail++ < 20)
    printf("  %s: differs\n", what);
}

static unsigned long s_vz0_pad; /* MulMatrix0 calls whose VZ0 high half differed (masked) */

/* MulMatrix0 on (m0, m1, m2) as given, aliases included: a, b, c index the
 * three matrices of the set (0, 1, 2; equal indices alias). */
static void test_mulmatrix0(int a, int b, int c) {
  MATRIX ref[3], our[3];
  for (int k = 0; k < 3; k++) {
    rnd_matrix(&ref[k]);
    our[k] = ref[k];
  }
  random_state();
  gteRegs = s_state;
  MATRIX *rr = psyx_MulMatrix0(&ref[a], &ref[b], &ref[c]);
  s_want = gteRegs;
  gteRegs = s_state;
  MATRIX *ro = MulMatrix0(&our[a], &our[b], &our[c]);
  if (s_want.CP2D.p[1].w.h != gteRegs.CP2D.p[1].w.h)
    s_vz0_pad++;
  s_want.CP2D.p[1].w.h = gteRegs.CP2D.p[1].w.h;
  compare_regs("MulMatrix0", &s_want, &gteRegs);
  check(rr == &ref[c] && ro == &our[c], "MulMatrix0 return");
  check(!memcmp(ref, our, sizeof ref), "MulMatrix0 matrices");
}

/* MulMatrix on (m0, m1), m1 == m0 if alias. */
static void test_mulmatrix(int alias) {
  MATRIX ref[2], our[2];
  for (int k = 0; k < 2; k++) {
    rnd_matrix(&ref[k]);
    our[k] = ref[k];
  }
  random_state();
  gteRegs = s_state;
  MATRIX *rr = psyx_MulMatrix(&ref[0], &ref[alias ? 0 : 1]);
  s_want = gteRegs;
  gteRegs = s_state;
  MATRIX *ro = MulMatrix(&our[0], &our[alias ? 0 : 1]);
  compare_regs("MulMatrix", &s_want, &gteRegs);
  check(rr == &ref[0] && ro == &our[0], "MulMatrix return");
  check(!memcmp(ref[0].m, our[0].m, sizeof ref[0].m), "MulMatrix m0");
  check(!memcmp(&ref[1], &our[1], sizeof ref[1]), "MulMatrix m1");
}

int main(void) {
  const int n = 200000;
  unsigned long calls = 0;

  for (int t = 0; t < n; t++) {
    /* the moves, every register */
    int reg = (int)(rnd() % 32);
    uint32_t v = rnd_reg(), rw, rh;
    random_state();
    gteRegs = s_state;
    psyx_MTC2(v, reg);
    s_want = gteRegs;
    gteRegs = s_state;
    MTC2(v, reg);
    compare_regs("MTC2", &s_want, &gteRegs);

    gteRegs = s_state;
    rw = psyx_MFC2(reg);
    s_want = gteRegs;
    gteRegs = s_state;
    rh = MFC2(reg);
    compare_regs("MFC2", &s_want, &gteRegs);
    check(rw == rh, "MFC2 value");

    gteRegs = s_state;
    psyx_CTC2(v, reg);
    s_want = gteRegs;
    gteRegs = s_state;
    CTC2(v, reg);
    compare_regs("CTC2", &s_want, &gteRegs);

    gteRegs = s_state;
    rw = psyx_CFC2(reg);
    rh = CFC2(reg);
    check(rw == rh, "CFC2 value");
    compare_regs("CFC2", &s_state, &gteRegs);
    calls += 4;

    /* libgte's six */
    random_state();
    gteRegs = s_state;
    psyx_InitGeom();
    s_want = gteRegs;
    gteRegs = s_state;
    InitGeom();
    compare_regs("InitGeom", &s_want, &gteRegs);

    int x = (int)(rnd() % 65536) - 32768, y = (int)(rnd() % 65536) - 32768;
    gteRegs = s_state;
    psyx_SetGeomOffset(x, y);
    s_want = gteRegs;
    gteRegs = s_state;
    SetGeomOffset(x, y);
    compare_regs("SetGeomOffset", &s_want, &gteRegs);

    int h = (int)rnd_reg();
    gteRegs = s_state;
    psyx_SetGeomScreen(h);
    s_want = gteRegs;
    gteRegs = s_state;
    SetGeomScreen(h);
    compare_regs("SetGeomScreen", &s_want, &gteRegs);

    MATRIX m, mo;
    rnd_matrix(&m);
    mo = m;
    gteRegs = s_state;
    psyx_SetTransMatrix(&m);
    s_want = gteRegs;
    gteRegs = s_state;
    SetTransMatrix(&mo);
    compare_regs("SetTransMatrix", &s_want, &gteRegs);
    check(!memcmp(&m, &mo, sizeof m), "SetTransMatrix m");

    test_mulmatrix(t % 16 == 0);
    static const int set[][3] = {{0, 1, 2}, {0, 1, 0}, {0, 1, 1}, {0, 0, 0}, {0, 0, 1}};
    const int *s = set[t % 16 ? 0 : 1 + (t / 16) % 4];
    test_mulmatrix0(s[0], s[1], s[2]);
    calls += 6;
  }

  printf("test_libgte: %lu calls on %d random register states\n", calls, n);
  printf("test_libgte: MulMatrix0 left VZ0's high half different in %lu of %d calls (PsyCross's stack pad, masked)\n",
         s_vz0_pad, n);
  printf("test_libgte: %s\n", s_fail ? "FAILED" : "all match");
  return s_fail ? 1 : 0;
}

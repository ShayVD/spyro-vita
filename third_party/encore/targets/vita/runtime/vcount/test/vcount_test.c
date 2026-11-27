/* vpk_vcount against a simulated 16-bit sceDisplayGetVcount: the widening by itself, then a game loop that waits for
 * "vblank target reached" across several wraps (frames taking 0-3 vblanks, pauses counted afresh), started where a
 * real freeze started. Every wait must end within a few vblanks and the count must keep up with the display's.
 * Built with -DVCOUNT_MUTANT=1 it compares the raw counter, as the port did before its fix, and must fail.
 * After spyro-vita tests/vcount/test_vcount.c @ bb62f38 (its decision D90), without its clock module. */
#include "vpk_vcount.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned s_disp; /* the display's vblanks since boot */
static VpkVcount s_vc;
static int s_fail;

static unsigned vcount(void) {
  unsigned raw = s_disp & 0xffffu; /* sceDisplayGetVcount on hardware */
#if VCOUNT_MUTANT
  return raw;
#else
  return vpk_vcount_widen(&s_vc, raw);
#endif
}

static void check(int ok, const char *what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    s_fail = 1;
  }
}

static void widen_cases(void) {
  VpkVcount v = {0, 0, 0};
  check(vpk_vcount_widen(&v, 0xfffe) == 0xfffe, "first read");
  check(vpk_vcount_widen(&v, 0xffff) == 0xffff, "step to 0xffff");
  check(vpk_vcount_widen(&v, 0) == 0x10000, "0xffff -> 0 is one step");
  check(vpk_vcount_widen(&v, 5) == 0x10005, "steps after the wrap");
  check(vpk_vcount_widen(&v, 5) == 0x10005, "no step");
  vpk_vcount_reset(&v);
  vpk_vcount_widen(&v, 0x12345678u); /* a wider counter: the same steps */
  check(vpk_vcount_widen(&v, 0x12345678u + 1000) == 0x5678 + 1000, "wider counter");
}

/* A game loop: each frame does 0-3 vblanks of work, then waits until the display reaches the frame's target vblank
 * (the previous target + 1 or 2), as a VSync(n) does. Returns -1 if a wait never ends. */
static int play(unsigned start, int frames, unsigned seed) {
  unsigned target, base;
  int i;
  s_disp = start;
  vpk_vcount_reset(&s_vc);
  srand(seed);
  base = vcount();
  target = base;
  for (i = 0; i < frames; i++) {
    int r = rand(), waits = 0;
    s_disp += (unsigned)(r % 4); /* the frame's own work */
    if (r % 20000 == 7) {        /* a pause: counted afresh, no catch-up */
      s_disp += 3000;
      vpk_vcount_reset(&s_vc);
      base = vcount();
      target = base;
    }
    target += 1 + (unsigned)(r >> 8) % 2;
    if (vcount() > target + 4)
      target = vcount(); /* late: drop the missed vblanks */
    while (vcount() < target) {
      s_disp++; /* sceDisplayWaitVblankStart */
      if (++waits > 8) {
        printf("FAIL: start %u: the wait never ends at frame %d (target %u, vcount 0x%04x)\n", start, i, target,
               s_disp & 0xffffu);
        return -1;
      }
    }
  }
  printf("ok: start %u, %d frames, %u wraps of the 16-bit counter\n", start, frames, (s_disp >> 16) - (start >> 16));
  return 0;
}

int main(void) {
  widen_cases();
  if (play(2539, 200000, 1) < 0) /* a real freeze's start: the wrap came at the port's vblank 62997 */
    s_fail = 1;
  if (play(65530, 200000, 2) < 0) /* a wrap in the first frames */
    s_fail = 1;
  if (play(0, 200000, 3) < 0)
    s_fail = 1;
  printf(s_fail ? "vcount_test: FAIL\n" : "vcount_test: ok\n");
  return s_fail;
}

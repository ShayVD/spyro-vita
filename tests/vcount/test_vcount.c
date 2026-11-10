/* The Vita's widened vblank counter (vita-port-kit's vpk_vcount.h, D90).
 *
 * The display's sceDisplayGetVcount counts in 16 bits on hardware. Before
 * D90 vblank.c's PsyX_WaitForTimestep compared it with a 32-bit target, so
 * 65536 display vblanks after start-up (18 min 13 s) VSync's wait never
 * ended: the owner's freeze going into Cliff Town (crash dump 2026-11-03:
 * target 62997, base 2539, 2539 + 62996 = 0xffff).
 *
 * This runs that wait's arithmetic (the realtime path's psx_clock_due, in
 * src/platform/common/clock.c since D104, linked here as it is) against a
 * simulated 16-bit counter: frames that take 0-3 vblanks, pauses
 * that start the count again, a save state's larger vblank count, across
 * several wraps, starting from the dump's count. Each wait must end within a
 * few vblanks and our count must keep up with the display's. The mutant
 * build (VCOUNT_MUTANT=1) uses the raw counter, as before D90, and must
 * fail. */
#include "psx_clock.h"
#include "vpk_vcount.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned s_disp; /* the display's vblanks since boot */
static VpkVcount s_vc;
static int s_waits;
static int s_fail;

static int vcount(void) {
  unsigned raw = s_disp & 0xffffu; /* sceDisplayGetVcount on hardware */
#if VCOUNT_MUTANT
  return (int)raw;
#else
  return (int)vpk_vcount_widen(&s_vc, raw);
#endif
}

/* sceDisplayWaitVblankStart; a wait that doesn't end gives up (exit 1)
 * rather than hang the test. */
static void wait_display(void) {
  s_disp++;
  if (++s_waits > 1000) {
    printf("FAIL: VSync's wait never ends: our vblank %d, display vcount 0x%04x\n",
           psx_clock_count(), s_disp & 0xffffu);
    printf("test_vcount: FAIL\n");
    exit(1);
  }
}

/* The Vita's VSync(0) with realtime=1 (vblank.c psx_platform_vsync):
 * psx_clock_due, then the vblanks due, each a psx_clock_tick; -1 if the
 * wait took more vblanks than a frame can ask for. */
static int wait_for_timestep(int count) {
  int late, n, i;
  s_waits = 0;
  n = psx_clock_due(count, vcount, wait_display, &late);
  if (s_waits > 2 * count + 2)
    return -1;
  for (i = 0; i < n; i++)
    psx_clock_tick();
  return n;
}

static void check(int ok, const char *what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    s_fail = 1;
  }
}

/* The widener alone. */
static void widen_cases(void) {
  VpkVcount v = {0};
  check(vpk_vcount_widen(&v, 0xfffe) == 0xfffe, "first read");
  check(vpk_vcount_widen(&v, 0xffff) == 0xffff, "step to 0xffff");
  check(vpk_vcount_widen(&v, 0) == 0x10000, "0xffff -> 0 is one step");
  check(vpk_vcount_widen(&v, 5) == 0x10005, "steps after the wrap");
  check(vpk_vcount_widen(&v, 5) == 0x10005, "no step");
  v.valid = 0;
  vpk_vcount_widen(&v, 0x12345678u); /* a wider counter: the same steps */
  check(vpk_vcount_widen(&v, 0x12345678u + 1000) == 0x5678 + 1000, "wider counter");
}

/* A long play from the dump's start, frames 0-3 vblanks, VSync(1) or (2). */
static int play(unsigned start, int frames, unsigned seed) {
  int i, jumped = 0, resyncs = 0, lost;
  unsigned disp0, paused = 0;
  s_disp = disp0 = start;
  s_vc.valid = 0;
  psx_clock_set(0);
  srand(seed);
  for (i = 0; i < frames; i++) {
    int r = rand();
    s_disp += (unsigned)(r % 4); /* the frame's own work */
    if (r % 20000 == 7) {        /* the options menu: paused, counted afresh */
      s_disp += 3000;
      paused += 3000;
      psx_clock_resync();
      resyncs++;
    }
    if (r % 30011 == 11) { /* a save state's vblank count (vita_set_vblank) */
      psx_clock_set(psx_clock_count() + 200000);
      jumped += 200000;
      resyncs++;
    }
    if (wait_for_timestep(1 + (r >> 8) % 2) < 0) {
      printf("FAIL: VSync's wait never ends at frame %d, vblank %d, display %u (vcount 0x%04x)\n",
             i, psx_clock_count(), s_disp - disp0, s_disp & 0xffffu);
      return -1;
    }
  }
  /* every display vblank but the pauses' counted, less a frame's work at
   * each fresh count */
  lost = (int)(s_disp - disp0 - paused) - (psx_clock_count() - jumped);
  if (lost < 0 || lost > 3 * (resyncs + 1)) {
    printf("FAIL: start %u: our count %d behind the display's\n", start, lost);
    return -1;
  }
  printf("ok: start %u, %d frames, %u display vblanks (%u wraps), our vblank %d\n", start,
         frames, s_disp - disp0, (s_disp >> 16) - (start >> 16), psx_clock_count());
  return 0;
}

int main(void) {
  widen_cases();
  if (play(2539, 200000, 1) < 0) /* the dump's: wraps at our vblank 62997 */
    s_fail = 1;
  if (play(65530, 200000, 2) < 0) /* a wrap in the first frames */
    s_fail = 1;
  if (play(0, 200000, 3) < 0)
    s_fail = 1;
  printf(s_fail ? "test_vcount: FAIL\n" : "test_vcount: ok\n");
  return s_fail;
}

/* The game's vblank clock (psx_clock.h, stage 7b, D104): the count both
 * platforms keep, and the Vita's real-time arithmetic (D28; the display's
 * vcount widened by the caller, D90). No PsyCross here: tests/vcount links
 * this file alone. */
#include "psx_clock.h"

static int s_vblank;
/* Written by the game thread only; read by the Vita's render thread too
 * (psx_clock_display_base, stage 7c), a word at a time. */
static volatile int s_vcount_base;   /* the display's vcount at our vblank 0 */
static volatile int s_vcount_synced; /* 0: s_vcount_base is taken afresh at the next wait */

int psx_clock_count(void) { return s_vblank; }

int psx_clock_tick(void) { return ++s_vblank; }

void psx_clock_set(int vblank) {
  s_vblank = vblank;
  s_vcount_synced = 0;
}

void psx_clock_resync(void) { s_vcount_synced = 0; }

int psx_clock_display_base(int *base) {
  int synced = s_vcount_synced;
  *base = s_vcount_base;
  return synced;
}

int psx_clock_due(int count, int (*vcount)(void), void (*wait)(void), int *late) {
  int target, n;
  if (!s_vcount_synced) {
    s_vcount_base = vcount() - s_vblank;
    s_vcount_synced = 1;
  }
  target = s_vblank + count;
  while (vcount() - s_vcount_base < target)
    wait();
  n = vcount() - s_vcount_base - s_vblank;
  *late = n > count ? n - count : 0;
  if (n > PSX_CLOCK_MAX_CATCH_UP) { /* a load or a stall: don't replay all of it */
    s_vcount_base += n - PSX_CLOCK_MAX_CATCH_UP;
    n = PSX_CLOCK_MAX_CATCH_UP;
  }
  return n;
}

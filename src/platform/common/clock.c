/* The game's vblank clock (psx_clock.h, stage 7b, D104): the count both
 * platforms keep, and the Vita's real-time arithmetic (D28; the display's
 * vcount widened by the caller, D90). No PsyCross here: tests/vcount links
 * this file alone. */
#include "psx_clock.h"

static int s_vblank;
/* The game's vblank rate against the display's 60 (D138): 50 for PAL at its
 * own timing. psx_clock_due counts the display's vblanks as hz/60 of ours. */
static int s_hz = 60;
/* Written by the game thread only; read by the Vita's render thread too
 * (psx_clock_display_base, stage 7c), a word at a time. */
static volatile int s_vcount_base;   /* the display's vcount at our vblank 0 */
static volatile int s_vcount_synced; /* 0: s_vcount_base is taken afresh at the next wait */

int psx_clock_count(void) { return s_vblank; }

void psx_clock_set_hz(int hz) {
  s_hz = hz == 50 ? 50 : 60;
  s_vcount_synced = 0;
}

int psx_clock_hz(void) { return s_hz; }

/* The display's vblanks as ours: the same count at 60 Hz, 5 of 6 at 50. */
static int scaled(int display) {
  return s_hz == 60 ? display : (int)((long long)display * s_hz / 60);
}

int psx_clock_display_due(int base, int due) {
  /* the first display vblank at or after our vblank `due`: ceil(due * 60 / hz) */
  return s_hz == 60 ? base + due : base + (int)(((long long)due * 60 + s_hz - 1) / s_hz);
}

int psx_clock_display_to_ours(int n) {
  /* display vblanks past a due one, as ours (floor, either sign) */
  long long x = (long long)n * s_hz;
  return s_hz == 60 ? n : (int)(x >= 0 ? x / 60 : -((-x + 59) / 60));
}

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
    /* at 50 Hz the base stays in the display's vblanks (psx_clock_display_due) */
    s_vcount_base = s_hz == 60 ? vcount() - s_vblank : vcount() - (s_vblank * 60 + s_hz - 1) / s_hz;
    s_vcount_synced = 1;
  }
  target = s_vblank + count;
  while (scaled(vcount() - s_vcount_base) < target)
    wait();
  n = scaled(vcount() - s_vcount_base) - s_vblank;
  *late = n > count ? n - count : 0;
  if (n > PSX_CLOCK_MAX_CATCH_UP) { /* a load or a stall: don't replay all of it */
    if (s_hz == 60)
      s_vcount_base += n - PSX_CLOCK_MAX_CATCH_UP;
    else /* count afresh from where the display is, less the vblanks run now */
      s_vcount_base = vcount() - ((s_vblank + PSX_CLOCK_MAX_CATCH_UP) * 60 + s_hz - 1) / s_hz;
    n = PSX_CLOCK_MAX_CATCH_UP;
  }
  return n;
}

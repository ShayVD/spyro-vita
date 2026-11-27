/* Synthetic decomp code for lp64/tools/test (no game's code). */
#include "game.h"

int walk(void) {
  Moby **w;
  int n = 0;
  for (w = g_List; *w; w++)
    n += (*w)->m_Type;
  return n;
}

int names(void) { return g_Names[2][2]; }

void store(void *p) { ((u_long *)p)[1] = 7; }

int first_set(void *p) { return *(Moby **)p != 0; }

int addr(Moby *m) { return (int)m; }

/* A stand-in port's own C for the offset model's tools (no game's code). */
#include "game.h"

typedef __UINTPTR_TYPE__ uintptr_t;
typedef unsigned int u32;

static unsigned char s_copy[64]; /* a host copy the port keeps */

int port_count(void) {
  int n = 0;
  Moby *m;
  for (m = g_Head; m; m = m->m_Next)
    n++;
  return n;
}

/* a host address kept in an integer and cut to 32 bits: the narrowing check lists it */
u32 copy_addr(int i) {
  uintptr_t at = (uintptr_t)s_copy + (uintptr_t)i;
  return (u32)at;
}

/* a slot read raw as a word and widened: the --raw check lists the first, not the decoded second */
uintptr_t raw_head(void) { return *(u32 *)&g_Head; }
uintptr_t decoded_head(void) { return (uintptr_t)P32_DEC(*(u32 *)&g_Head); }

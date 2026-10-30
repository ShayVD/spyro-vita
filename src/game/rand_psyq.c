/* PsyQ libc rand/srand (asm/psyq.s `rand`, `srand`; seed D_80075AC0 in
 * BSS, so 0 at boot). The game calls rand() hundreds of places; the host's
 * would change every random choice and break trace comparison. With the PS1
 * layout (decisions D23) the seed lives at its PS1 address in the arena, so
 * the oracle replay (oracle_replay.c) and RAM comparisons see it there. */
#if SPYRO_PSX_LAYOUT
#include "psx_arena.h"
#define s_seed (*(unsigned int *)PSX_ARENA(0x80075AC0u))
#else
static unsigned int s_seed;
#endif

int rand(void) {
  s_seed = s_seed * 0x41C64E6Du + 0x3039u;
  return (int)((s_seed >> 16) & 0x7FFFu);
}

void srand(unsigned int seed) { s_seed = seed; }

unsigned int psyq_rand_seed(void) { return s_seed; } /* for trace.c */

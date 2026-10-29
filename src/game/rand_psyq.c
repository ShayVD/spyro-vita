/* PsyQ libc rand/srand (asm/psyq.s `rand`, `srand`; seed D_80075AC0 in
 * BSS, so 0 at boot). The game calls rand() hundreds of places; the host's
 * would change every random choice and break trace comparison. */
static unsigned int s_seed;

int rand(void) {
  s_seed = s_seed * 0x41C64E6Du + 0x3039u;
  return (int)((s_seed >> 16) & 0x7FFFu);
}

void srand(unsigned int seed) { s_seed = seed; }

unsigned int psyq_rand_seed(void) { return s_seed; } /* for trace.c */

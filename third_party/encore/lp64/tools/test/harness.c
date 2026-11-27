/* lp64/tools/test's program over the synthetic decomp (fixture/): built from the patched tree for x86-64 (P32 forced
 * in) and for i686, it checks the layout the decomp declares and runs the decomp's functions over it. HostState is the
 * host's own (8-byte pointers on LP64): layout64.py must report it unless it is listed as host-only. */
#include "game.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(offsetof(Moby, m_Bytes) == 12 && offsetof(Moby, m_Flags) == 20, "Moby's 32-bit offsets");
_Static_assert(sizeof(Moby) == 24, "Moby's 32-bit size");

struct HostState {
  void *m_Where;
  int m_Count;
};
struct HostState g_Host;
void (*g_Update)(Moby *);
static Moby s_mobys[3];
static unsigned int s_words[4];

int main(void) {
  int i;
  for (i = 0; i < 3; i++) {
    s_mobys[i].m_Type = (short)(i + 1);
    g_List[i] = &s_mobys[i];
  }
  g_Player = &s_mobys[0];
  store(s_words);
  if (walk() != 6 || names() != 'C' || s_words[1] != 7 || s_words[2] != 0 || !first_set(g_List) ||
      g_Player->m_Type != 1) {
    printf("harness: FAILED (walk %d, names %c, words %u %u)\n", walk(), names(), s_words[1], s_words[2]);
    return 1;
  }
  printf("harness: ok (Moby 24 bytes, a T ** walk over P32 pointers, P32_INIT names, a 4-byte u_long store)\n");
  return 0;
}

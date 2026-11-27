/* A stand-in game's main.c for lp64/cmake/test (no game's code): built unchanged but for -Dmain=lp64_game_main, with
 * lp64_p32.h forced in by the toolchain. Its memory and its P32 table are in the low image; its own main runs on the
 * low stack. */
#include "lp64_low_image.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct Node Node;
struct Node {
  int m_Value;
  Node *P32 m_Next;
};
_Static_assert(sizeof(Node) == 8, "a P32 field: the 32-bit layout");

static Node s_nodes[8];

int main(int argc, char **argv) {
  int i, n = argc > 1 ? atoi(argv[1]) : 8, sum = 0, here;
  Node *p;
  for (i = 0; i < n; i++) {
    s_nodes[i].m_Value = i + 1;
    s_nodes[i].m_Next = i + 1 < n ? &s_nodes[i + 1] : NULL;
  }
  for (p = &s_nodes[0]; p; p = p->m_Next)
    sum += p->m_Value;
  if (!LP64_P32_ON || sum != n * (n + 1) / 2 || !lp64_on_game_stack(&here)) {
    printf("game: FAILED (P32 %d, sum %d)\n", LP64_P32_ON, sum);
    return 1;
  }
  printf("game: ok (P32 on, the list summed to %d on the low stack)\n", sum);
  return 0;
}

/* The offset model as a completeness check on x86-64 (spyro-vita D258): compiled with -DLP64_P32_OFFSET=1 a slot is a
 * union, so every use that skips an accessor, and every accessor on something that isn't a slot, is a compile error;
 * the same text compiles in the low image (-fms-extensions) and on a 32-bit target. Syntax only, no headers:
 *   clang --target=x86_64-linux-gnu -ffreestanding -fsyntax-only -DLP64_P32_OFFSET=1 -I.. p32_complete.c   (compiles)
 *   ... -DLP64_TEST_MISSED=1   a bare read of a slot          (must fail)
 *   ... -DLP64_TEST_EXTRA=1    P32_GET on a plain pointer      (must fail)
 *   ... -DLP64_TEST_BARE=1     the bare `T *P32` qualifier     (must fail, in the offset model only)
 * Written for the kit, after spyro-vita's tools/ps4/p32_offset_check.py @ 36d9e07 (D258), on synthetic data. */
#include "lp64_p32.h"

typedef struct Moby Moby;
typedef P32F(Moby *) MobySlot;
struct Moby {
  short m_Type;
  P32F(Moby *) m_Next;
  P32F(MobySlot *) m_List;
  Moby *m_Host; /* a plain pointer: the port's own, never in the game's memory */
};
_Static_assert(__builtin_offsetof(Moby, m_List) == 8 && sizeof(MobySlot) == 4, "the slots are 4 bytes");
MobySlot g_Table[4];
extern __typeof__(g_Table) g_Table; /* a redeclaration borrows the first's type */

int count(Moby *m) {
  int n = 0;
  MobySlot *w;
  for (; m; m = P32_GET(m->m_Next))
    n += m->m_Type;
  for (w = P32_GET(g_Table[0]) ? g_Table : 0; w && w < g_Table + 4; w++)
    n += P32_GET(*w)->m_Type;
  P32_SET(g_Table[1], P32_GET(g_Table[0]));
  P32_SET(m->m_List, g_Table);
  n += (int)P32_ENC(P32_GET(g_Table[2]));
#if LP64_TEST_MISSED
  n += m->m_Next->m_Type;
#endif
#if LP64_TEST_EXTRA
  n += P32_GET(m->m_Host)->m_Type;
#endif
  return n;
}

#if LP64_TEST_BARE
Moby *P32 g_Bare;
#endif

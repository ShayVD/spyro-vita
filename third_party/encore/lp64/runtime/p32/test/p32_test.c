/* lp64_p32 on x86-64 with clang: a synthetic game struct keeps its 32-bit layout with P32 pointer fields, a pointer
 * read from one is zero-extended (a word at 0xF0000000 reads back as 0x00000000F0000000, not sign-extended), a
 * `T *P32 *` walks an array of P32 pointers four bytes a step, a P32 data label holds a .bss address, and the
 * P32_INIT tables are filled by their constructors before main(). Built static and non-PIE so .bss is low; run under
 * qemu-x86_64 on a non-x86 machine. The mutant (LP64_P32_MUTANT: __sptr instead of __uptr) must fail.
 *   p32_test
 * From spyro-vita src/compat/p32.h @ c25a3ea (MIT): the cases it was written for (D174), on synthetic data. */
#include "lp64_p32.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !LP64_P32_ON
#error "p32_test is the x86-64 clang test: build it with clang --target=x86_64-linux-gnu -fms-extensions"
#endif

#ifdef LP64_P32_MUTANT
#undef LP64_P32
#define LP64_P32 __ptr32 __sptr
#endif

/* a "game" struct as a 32-bit decomp declares it: offsets are the original's */
typedef struct Thing Thing;
struct Thing {
  short m_Type;
  short m_Flags;
  Thing *P32 m_Next;
  int *P32 m_Data;
  unsigned char *P32 m_Bytes[3];
  Thing *P32 *P32 m_List;
  int m_Pos[3];
};
_Static_assert(sizeof(Thing) == 40, "Thing keeps its 32-bit size");
_Static_assert(__builtin_offsetof(Thing, m_Data) == 8, "m_Data at its 32-bit offset");
_Static_assert(__builtin_offsetof(Thing, m_List) == 24, "m_List at its 32-bit offset");
_Static_assert(sizeof(Thing *P32) == 4, "a P32 pointer is 4 bytes");

static Thing s_things[4];        /* the game's memory: .bss, below 2 GB */
Thing *P32 g_FirstThing;         /* a data label, 4 bytes as on the original */
Thing *P32 g_ThingTable[4];      /* a table of them */
static const char s_text[] = "lp64";

/* address-initialised tables: clang's backend can't take these as P32 static initializers. The game's header
 * declares them as usual; the definitions (P32_INIT_ARRAY, P32_INIT_SCALAR) are at the file's end. */
extern char *P32 g_Names[3];
extern Thing *P32 g_Second;
char *P32_INIT(g_Names)[3] = {"A", "BB", "CCC"};
Thing *P32_INIT(g_Second) = &s_things[1];

static int s_fails;
#define CHECK(c)                                                                                                       \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      printf("p32_test: FAILED %s (line %d)\n", #c, __LINE__);                                                         \
      s_fails++;                                                                                                       \
    }                                                                                                                  \
  } while (0)

int main(void) {
  int i, n = 0;
  uint32_t raw;
  Thing *t = &s_things[0];
  Thing *P32 *walk;
  int *data;

  CHECK((uintptr_t)&s_things[3] + sizeof(Thing) < 0x80000000u);
  CHECK(sizeof g_FirstThing == 4 && sizeof g_ThingTable == 16);

  /* a pointer written into a field and read back */
  t->m_Next = &s_things[2];
  CHECK(t->m_Next == &s_things[2]);
  memcpy(&raw, (char *)t + 4, 4);
  CHECK(raw == (uint32_t)(uintptr_t)&s_things[2]);

  /* the disc's data fixed up in place: a 32-bit word written raw, read as a pointer, zero-extended */
  raw = 0xF0000000u;
  memcpy((char *)t + 8, &raw, 4);
  data = t->m_Data; /* read as a pointer: (uintptr_t)t->m_Data would be a 32-bit integer read, never extended */
  CHECK((uintptr_t)data == 0xF0000000u);
  raw = (uint32_t)(uintptr_t)s_text;
  memcpy((char *)t + 12 + 4, &raw, 4);
  CHECK(t->m_Bytes[1] == (const unsigned char *)s_text && t->m_Bytes[1][3] == '4');

  /* a T *P32 * walks an array of P32 pointers 4 bytes a step (a plain T ** would read 8) */
  for (i = 0; i < 4; i++)
    g_ThingTable[i] = &s_things[3 - i];
  t->m_List = g_ThingTable;
  for (walk = t->m_List; walk < g_ThingTable + 4; walk++)
    n += (int)(*walk - s_things);
  CHECK(n == 6);
  CHECK((char *)(walk) - (char *)t->m_List == 16);
  g_FirstThing = t;
  CHECK(g_FirstThing->m_Next->m_Type == s_things[2].m_Type);

  /* P32_INIT tables: filled by constructors before main() */
  CHECK(sizeof g_Names == 12);
  CHECK(strcmp(g_Names[0], "A") == 0 && strcmp(g_Names[2], "CCC") == 0);
  CHECK(g_Second == &s_things[1]);

  if (s_fails) {
    printf("p32_test: %d failed\n", s_fails);
    return 1;
  }
  printf("p32_test: ok (struct 40 bytes, zero-extended reads, T *P32 * walk, P32_INIT tables)\n");
  return 0;
}

P32_INIT_ARRAY(char, g_Names)
P32_INIT_SCALAR(Thing, g_Second)

/* lp64_p32 on Windows (x86_64-w64-mingw32 clang): a P32 field stays 4 bytes after a system header, which pulls in
 * _mingw.h and its empty __ptr32 macro. Compiled only (-fsyntax-only is enough: the asserts are static). The mutant
 * (LP64_P32_MUTANT_MINGW: the fix left out) must fail to compile. Learned in spyro-vita's Windows build (its D241). */
#include "lp64_p32.h"

#include <stdio.h> /* _mingw.h, as any game unit's first system header */

struct with_p32 {
  int a;
  char *P32 p;
  short s;
};
_Static_assert(sizeof(char *P32) == 4, "P32 is 4 bytes after a system header");
_Static_assert(sizeof(struct with_p32) == 12, "a struct with a P32 field keeps its 32-bit layout");
_Static_assert(__builtin_offsetof(struct with_p32, s) == 8, "the field after the P32 one at its 32-bit offset");

int main(void) {
  printf("p32_mingw: ok\n");
  return 0;
}

/* clang bug 4: with AVX (-march=btver2, the PS4's Jaguar, or any -mavx) at -O2, the loop vectorizer turns a copy of
 * normal pointers into a __ptr32 array (lp64_p32.h's P32_INIT constructors are such loops) into a vector addrspacecast
 * the backend can't select ("Cannot select: v4i32 = addrspacecast[0 -> 271]"). Plain SSE2 doesn't vectorize it. The
 * workaround: build with -fno-vectorize (check.sh compiles it so); -DWORKAROUND here also turns the loop's
 * vectorizing off with a pragma, for a unit that must keep the vectorizer.
 *   clang --target=x86_64-linux-gnu -fms-extensions -O2 -march=btver2 -c vector_cast.c
 * Found by spyro-vita's PS4 build (clang 18.1.8; D193); also in Debian clang 19.1.7 and Apple clang 16 (checked
 * 2026-11-22). From spyro-vita tools/ps4/clang_bugs/vector_addrspacecast.c @ 5c6021c (MIT). */
#include "lp64_p32.h"
extern char *names_init[64]; /* filled elsewhere: nothing to fold */
char *P32 names[64];

__attribute__((constructor)) static void fill(void) {
#ifdef WORKAROUND
#pragma clang loop vectorize(disable)
#endif
  for (unsigned long i = 0; i < 64; i++)
    names[i] = names_init[i];
}

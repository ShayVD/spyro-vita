/* clang 18.1.8, x86_64-pc-freebsd12-elf, -fms-extensions -O2 -march=btver2
 * (the PS4 build, D193): the loop vectorizer turns a copy of normal pointers
 * into a __ptr32 array (src/compat/p32.h's P32_INIT constructors) into a
 * vector addrspacecast the backend can't select:
 *   fatal error: error in backend: Cannot select: v4i32 = addrspacecast[0 -> 271]
 * -fno-vectorize avoids it (the PS4 builds with it); plain SSE2 (no -march)
 * doesn't vectorize it and compiles. */
extern char *names_init[64]; /* filled elsewhere: nothing to fold */
char *__ptr32 __uptr names[64];

__attribute__((constructor)) static void fill(void) {
  for (unsigned long i = 0; i < 64; i++)
    names[i] = names_init[i];
}

/* clang 19, x86-64, -fms-extensions (D174): a __ptr32 static initializer
 * holding an address crashes the backend ("Unsupported expression in static
 * initializer: addrspacecast"). src/compat/p32.h's P32_INIT fills such
 * tables from a normal one in a constructor instead.
 *   tools/tc clang-19 --target=x86_64-linux-gnu -fms-extensions -O1 -c static_init.c */
#define P32 __ptr32 __uptr
char *P32 g_Names[3] = {"A", "BB", "CCC"};

/* clang bug 2: a ?: with a __ptr32 pointer on one side and a normal pointer on the other never finishes compiling
 * (clang 19.1.7, -O1); Apple clang 16 crashes instead ("Cannot select: i32 = bitcast"). The workaround (-DWORKAROUND):
 * a cast on the __ptr32 side. Build under `timeout`.
 *   timeout 20 clang --target=x86_64-linux-gnu -fms-extensions -O1 -c ternary_hang.c
 * From spyro-vita tools/ps4/clang_bugs/ternary_hang.c @ c25a3ea (MIT; D173, D174). */
#define P32 __ptr32 __uptr
struct S { int a; void *P32 p; };
#ifndef WORKAROUND
void *pick(struct S *s, int c, void *q) { return c ? s->p : q; }
#else
void *pick(struct S *s, int c, void *q) { return c ? (void *)s->p : q; }
#endif

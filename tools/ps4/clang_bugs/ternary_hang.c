/* clang 19, x86-64, -fms-extensions -O1 (D174): a ?: with a __ptr32 and a
 * normal pointer never finishes compiling (Apple clang 16 crashes:
 * "Cannot select: i32 = bitcast"). A cast on the __ptr32 side avoids it.
 *   tools/tc timeout 20 clang-19 --target=x86_64-linux-gnu -fms-extensions -O1 -c ternary_hang.c */
#define P32 __ptr32 __uptr
struct S { int a; void *P32 p; };
void *pick(struct S *s, int c, void *q) { return c ? s->p : q; }
void *pick_ok(struct S *s, int c, void *q) { return c ? (void *)s->p : q; }

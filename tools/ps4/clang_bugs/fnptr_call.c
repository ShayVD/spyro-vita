/* clang 19 (Debian 19.1.7), x86-64, -fms-extensions -O1 (D174): a call
 * through a __ptr32 function pointer. call() compiles to `jmp *g_fn(%rip)`,
 * an 8-byte load from a 4-byte slot; call2() crashes the backend ("Cannot
 * select: X86ISD::CALL ... load (s32)"). Casting to a normal function
 * pointer first is correct.
 *   tools/tc clang-19 --target=x86_64-linux-gnu -fms-extensions -O1 -c fnptr_call.c */
#define P32 __ptr32 __uptr
void (*P32 g_fn)(int);
int (*P32 g_tab[4])(int);
int other(int);
void call(void) { g_fn(3); }
int call2(int i) { g_fn(3); return g_tab[i](5) + other(1); }
typedef void (*fn_t)(int);
void call_ok(void) { ((fn_t)g_fn)(3); }

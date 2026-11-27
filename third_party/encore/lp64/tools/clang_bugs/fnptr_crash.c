/* clang bug 1b: the same bug as fnptr_call.c, where the call isn't a tail call: the x86 backend crashes ("Cannot
 * select: X86ISD::CALL ... load (s32)"). The workaround (-DWORKAROUND): cast to a normal function pointer first.
 *   clang --target=x86_64-linux-gnu -fms-extensions -O1 -c fnptr_crash.c
 * From spyro-vita tools/ps4/clang_bugs/fnptr_call.c @ c25a3ea (MIT; D174): its call2(). */
#define P32 __ptr32 __uptr
void (*P32 g_fn)(int);
int (*P32 g_tab[4])(int);
int other(int);
#ifndef WORKAROUND
int call2(int i) { g_fn(3); return g_tab[i](5) + other(1); }
#else
typedef void (*fn_t)(int);
typedef int (*ifn_t)(int);
int call2(int i) { ((fn_t)g_fn)(3); return ((ifn_t)g_tab[i])(5) + other(1); }
#endif

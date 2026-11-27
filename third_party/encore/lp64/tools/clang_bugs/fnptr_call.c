/* clang bug 1a: a call through a __ptr32 function pointer is compiled as an 8-byte load from the 4-byte slot (clang 19:
 * `jmpq *g_fn(%rip)`). LLVM 18 and 19 lack the 2026-11-03 fix (llvm-project PR #209078). The workaround
 * (-DWORKAROUND): cast to a normal function pointer first (`movl g_fn(%rip), %eax; jmpq *%rax`), or keep function
 * pointers as the port's own 8-byte globals. check.sh reads the assembly.
 *   clang --target=x86_64-linux-gnu -fms-extensions -O1 -S fnptr_call.c
 * From spyro-vita tools/ps4/clang_bugs/fnptr_call.c @ c25a3ea (MIT; D174). */
#define P32 __ptr32 __uptr
void (*P32 g_fn)(int);
#ifndef WORKAROUND
void call(void) { g_fn(3); }
#else
typedef void (*fn_t)(int);
void call(void) { ((fn_t)g_fn)(3); }
#endif

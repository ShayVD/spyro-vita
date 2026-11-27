/* lp64_p32: 32-bit pointer fields on a 64-bit target, the low-image model (lp64/docs/low-image.md).
 *
 * A decomp of a 4-byte-pointer game (PS1, PS2, PSP) lays its structs and data out as the original did: data from the
 * disc is fixed up in place, hand-written C reads fields at raw offsets, addresses sit in 32-bit integers. On x86-64
 * with clang (-fms-extensions) LP64_P32 makes such a pointer 32 bits wide, zero-extended when read (__uptr). Nothing
 * is lost as long as everything the game can point at lies in the executable's image below 2 GB
 * (lp64/runtime/low_image). On every other target LP64_P32 is empty and the text compiles as it always has: the
 * 32-bit builds (the Vita, an i686 host) don't change.
 *
 * Names: LP64_P32, LP64_P32F, LP64_P32_INIT, LP64_P32_INIT_ARRAY, LP64_P32_INIT_SCALAR, the accessors LP64_P32_GET
 * and the rest (below), LP64_P32_ON (1 when P32 means something), LP64_P32_OFFSET (1 in the offset model),
 * LP64_ASM_LIT. The short spellings without LP64_ (P32, P32F, P32_GET, ...) are aliases, because they sit on hundreds
 * of fields and thousands of uses in a decomp and a patch reads better with them; define LP64_P32_NO_SHORT_NAMES to
 * leave them out.
 *
 * clang 19's three __ptr32 bugs (lp64/docs/clang-bugs.md; lp64/tools/clang_bugs/check.sh tells which a clang has):
 *  - Never call through a P32 function pointer: clang loads 8 bytes from the 4-byte slot, or crashes in its backend.
 *    Keep function pointers in normal 8-byte globals of the port's, or cast to a normal function pointer first.
 *  - Never mix a P32 pointer and a normal one in `?:` (cast one side): clang 19 hangs, Apple clang 16 crashes. Build
 *    under `timeout` (lp64/cmake/linux64.toolchain.cmake).
 *  - Never initialise a P32 pointer with an address in a static initializer: the backend crashes ("addrspacecast in
 *    static initializer"). Write `char *P32_INIT(g_Names)[3] = {"A", ...};` and, at the file's end,
 *    `P32_INIT_ARRAY(char, g_Names)` (P32_INIT_SCALAR for one pointer): on x86-64 the initializer is a normal table,
 *    g_Names_p32init, and a constructor copies it into the real P32 g_Names before main(). Elsewhere P32_INIT(n) is n
 *    and the rest nothing.
 *
 * Windows (x86_64-w64-mingw32 clang, -fms-extensions; with -mno-ms-bitfields for the GNU struct layout): the same,
 * once _mingw.h's empty __ptr32 macro is undone (below).
 *
 * The offset model (lp64/docs/low-image.md, "The offset model: ARM64"): AArch64 has no __ptr32 (clang accepts it and
 * does nothing), so there every slot is a 32-bit offset from one base, lp64_p32_base (the image's start; macOS's
 * Mach-O header), 0 for NULL. It is the default on AArch64 LP64; LP64_P32_OFFSET=1 selects it anywhere, which on
 * x86-64 is the completeness check (a slot is a union, so a use that skips the accessors is a compile error).
 * A slot is spelled LP64_P32F(T *) (short: P32F(T *)) in every model: `P32F(Moby *) m_Next;`, two levels
 * `P32F(P32F(Moby *) *) m_List;`. Every use goes through an accessor, which is the code's own text on the other
 * models, so their binaries don't change:
 *   P32_GET(x)          read                       (x)            | x ? base + x : NULL
 *   P32_SET(x, v)       write, gives v             ((x) = (v))    | x = v ? v - base : 0
 *   P32_ADD/SUB(x, n), P32_PREINC/PREDEC/POSTINC/POSTDEC(x)   the compound forms
 *   P32_PATCH(x, base)  a loader's fix-up: the slot holds an offset into a buffer just read, becomes base + it
 *   P32_ENC(p)          a pointer the code turns into a 32-bit integer: (p) | its offset (a non-pointer passes)
 *   P32_DEC(i)          a 32-bit integer the code turns into a pointer:  (i) | base + i (a pointer passes)
 *   P32_ARG_T, P32_ARG(a)   an entry point that takes the caller's pointers as 32-bit words
 * The bare qualifier (`T *P32 name`) can't be an offset: in the offset model it is a compile error.
 * LP64_P32_CHECK=1 is the checking build: an encode of a value above 4 GB outside the base's window (a host pointer
 * stored as a game address) stops with its file and line (lp64/runtime/p32_image).
 *
 * A 64-bit target that is neither (gcc on x86-64, a non-x86 clang with LP64_P32_OFFSET=0) gets an #error: there the
 * fields would silently become 8 bytes. Define LP64_P32_WIDE_OK where that is meant (a 64-bit tool that includes the
 * game's headers but never reads its memory; it also keeps the offset model off by default).
 *
 * From spyro-vita src/compat/p32.h @ c25a3ea (MIT; its decisions D173, D174); P32F, the accessors and the offset model
 * from it @ 36d9e07 (D249, D258, D262, D265). */
#ifndef LP64_P32_H
#define LP64_P32_H

/* A C name's assembly name, for __asm__ labels: LP64_ASM_LIT("name"). Mach-O puts an underscore before C's names;
 * elsewhere the prefix is empty and this is the literal (spyro-vita D265). */
#define LP64_ASM_STR_(x) #x
#define LP64_ASM_STR(x) LP64_ASM_STR_(x)
#define LP64_ASM_LIT(s) LP64_ASM_STR(__USER_LABEL_PREFIX__) s

#ifndef LP64_P32_OFFSET
#if defined(__aarch64__) && defined(__LP64__) && !defined(LP64_P32_WIDE_OK)
#define LP64_P32_OFFSET 1
#else
#define LP64_P32_OFFSET 0
#endif
#endif

#if LP64_P32_OFFSET
#define LP64_P32_ON 1
/* the bare qualifier can't be an offset: spell the slot LP64_P32F(T *) */
#define LP64_P32 _Pragma("GCC error \"lp64_p32.h: the offset model has no bare P32 qualifier; write P32F(T *)\"")
#if defined(__APPLE__)
/* macOS: the executable's Mach-O header, where the kernel slid it (above __PAGEZERO's 4 GB) */
extern char lp64_p32_base[] __asm__("__mh_execute_header");
#else
/* the image's start: link with --defsym=lp64_p32_base=__executable_start (lp64/cmake/linuxa64.toolchain.cmake) */
extern char lp64_p32_base[];
#endif
/* Macros, not functions: a decomp's overlays may be preprocessed and then compiled with this header forced in again,
 * where a function would be defined twice. */
#define lp64_p32_dec_(o)                                                                                               \
  (__extension__({                                                                                                     \
    unsigned lp64_d_ = (o);                                                                                            \
    lp64_d_ ? (void *)(lp64_p32_base + lp64_d_) : (void *)0;                                                           \
  }))
#if LP64_P32_CHECK
void lp64_p32_check_fail(unsigned long long p, const char *file, int line) __attribute__((noreturn));
#define LP64_P32_CHECK_(r) ((r) >= 0x100000000ull ? lp64_p32_check_fail((r), __FILE__, __LINE__) : (void)0)
#else
#define LP64_P32_CHECK_(r) ((void)0)
#endif
/* A pointer into the 4 GB above the base becomes its offset; anything else (NULL, a small constant, a sentinel such
 * as 0xffffffff) keeps its low 32 bits, as the 32-bit targets keep it. */
#define lp64_p32_enc_(p)                                                                                               \
  (__extension__({                                                                                                     \
    __UINTPTR_TYPE__ lp64_r_ = (__UINTPTR_TYPE__)(p), lp64_e_ = lp64_r_ - (__UINTPTR_TYPE__)lp64_p32_base;             \
    lp64_e_ < 0x100000000ull ? (unsigned)lp64_e_ : (LP64_P32_CHECK_(lp64_r_), (unsigned)lp64_r_);                      \
  }))
/* the same, unchecked: an entry's argument may be any integer (P32_ARG) */
#define lp64_p32_arg_(a)                                                                                               \
  (__extension__({                                                                                                     \
    __UINTPTR_TYPE__ lp64_r_ = (__UINTPTR_TYPE__)(a), lp64_e_ = lp64_r_ - (__UINTPTR_TYPE__)lp64_p32_base;             \
    lp64_e_ < 0x100000000ull ? (unsigned)lp64_e_ : (unsigned)lp64_r_;                                                  \
  }))
/* A slot: 4 bytes, 4-aligned; the zero-length array carries the pointer type at no size, so the accessors need no
 * type argument. Two spellings are two distinct unions: a redeclaration borrows the first's type
 * (`extern __typeof__(g_X) g_X;`) or both share a typedef. */
#define LP64_P32F(T)                                                                                                   \
  union __attribute__((packed, aligned(4))) {                                                                          \
    unsigned p32_off;                                                                                                  \
    T p32_ty[0];                                                                                                       \
  }
#define LP64_P32_TYPE_(x) __typeof__((x).p32_ty[0])
#define LP64_P32_GET(x) ((LP64_P32_TYPE_(x))lp64_p32_dec_((x).p32_off))
#define LP64_P32_SET(x, v)                                                                                             \
  (__extension__({                                                                                                     \
    __typeof__(&(x)) lp64_s_ = &(x);                                                                                   \
    LP64_P32_TYPE_(x) lp64_v_ = (v);                                                                                   \
    lp64_s_->p32_off = lp64_p32_enc_(lp64_v_);                                                                         \
    lp64_v_;                                                                                                           \
  }))
#define LP64_P32_STEP_(x, n, post)                                                                                     \
  (__extension__({                                                                                                     \
    __typeof__(&(x)) lp64_s_ = &(x);                                                                                   \
    LP64_P32_TYPE_(x) lp64_o_ = LP64_P32_GET(*lp64_s_), lp64_n_ = lp64_o_ + (n);                                       \
    lp64_s_->p32_off = lp64_p32_enc_(lp64_n_);                                                                         \
    post ? lp64_o_ : lp64_n_;                                                                                          \
  }))
#define LP64_P32_ADD(x, n) LP64_P32_STEP_(x, n, 0)
#define LP64_P32_SUB(x, n) LP64_P32_STEP_(x, -(n), 0)
#define LP64_P32_PREINC(x) LP64_P32_STEP_(x, 1, 0)
#define LP64_P32_PREDEC(x) LP64_P32_STEP_(x, -1, 0)
#define LP64_P32_POSTINC(x) LP64_P32_STEP_(x, 1, 1)
#define LP64_P32_POSTDEC(x) LP64_P32_STEP_(x, -1, 1)
#define LP64_P32_PATCH(x, base) LP64_P32_SET(x, (void *)((char *)(base) + (x).p32_off))
#define LP64_P32_PATCH2(x, base) LP64_P32_PATCH(x, base)
/* each passes the other kind through, chosen by type, so a macro that takes a pointer or an address can have them in
 * its body */
#define LP64_P32_IS_PTR_(x) (__builtin_classify_type(x) == 5) /* arrays and functions too */
#define LP64_P32_ENC(p)                                                                                                \
  __builtin_choose_expr(LP64_P32_IS_PTR_(p), lp64_p32_enc_(p), (unsigned)(__UINTPTR_TYPE__)(p))
#define LP64_P32_DEC(i)                                                                                                \
  __builtin_choose_expr(LP64_P32_IS_PTR_(i), (void *)(__UINTPTR_TYPE__)(i),                                            \
                        lp64_p32_dec_((unsigned)(__UINTPTR_TYPE__)(i)))
/* an entry point the caller passes pointers to as 32-bit words: a 64-bit register here, encoded on entry */
#define LP64_P32_ARG_T __UINTPTR_TYPE__
#define LP64_P32_ARG(a) ((unsigned)lp64_p32_arg_(a))
/* address-initialised tables: filled at start-up, as the low image's are */
#define LP64_P32_INIT(name) name##_p32init
#define LP64_P32_INIT_ARRAY(type, name)                                                                                \
  __typeof__(name[0]) name[sizeof name##_p32init / sizeof name##_p32init[0]];                                          \
  __attribute__((constructor)) static void name##_p32fill(void) {                                                      \
    for (unsigned long i = 0; i < sizeof name / sizeof name[0]; i++)                                                   \
      LP64_P32_SET(name[i], name##_p32init[i]);                                                                        \
  }
#define LP64_P32_INIT_SCALAR(type, name)                                                                               \
  __typeof__(name) name;                                                                                               \
  __attribute__((constructor)) static void name##_p32fill(void) { LP64_P32_SET(name, name##_p32init); }

#else /* the low image and the 32-bit targets: a slot is a pointer, each accessor the code's own text */
#define LP64_P32_GET(x) (x)
#define LP64_P32_SET(x, v) ((x) = (v))
#define LP64_P32_ADD(x, n) ((x) += (n))
#define LP64_P32_SUB(x, n) ((x) -= (n))
#define LP64_P32_PREINC(x) (++(x))
#define LP64_P32_PREDEC(x) (--(x))
#define LP64_P32_POSTINC(x) ((x)++)
#define LP64_P32_POSTDEC(x) ((x)--)
#define LP64_P32_PATCH(x, base) x = (__typeof__(x))((unsigned int)(base) + (unsigned int)(x))
#define LP64_P32_PATCH2(x, base) x = (__typeof__(x))((unsigned int)(x) + (unsigned int)(base))
#define LP64_P32_ENC(p) (p)
#define LP64_P32_DEC(i) (i)
#define LP64_P32_ARG_T unsigned int
#define LP64_P32_ARG(a) (a)

#if defined(__x86_64__) && defined(__clang__)
#if defined(_WIN32) && !defined(LP64_P32_MUTANT_MINGW)
/* mingw's _mingw.h defines __ptr32 and __ptr64 as empty macros (MSVC's keywords, for compilers without them): once
 * any system header is in, every P32 field would silently be 8 bytes. Take _mingw.h now (it is guarded: it never
 * defines them again) and drop the two; clang's keywords remain. Only _mingw.h: a header forced into units that are
 * preprocessed and then compiled again with it forced in (a decomp's overlays) must not bring structs, or they are
 * defined twice. Learned in spyro-vita's Windows build (its D241). */
#include <_mingw.h>
#undef __ptr32
#undef __ptr64
#endif
#define LP64_P32_ON 1
#define LP64_P32 __ptr32 __uptr
#define LP64_P32F(T) T LP64_P32
#define LP64_P32_INIT(name) name##_p32init
#define LP64_P32_INIT_ARRAY(type, name)                                                                                \
  type *LP64_P32 name[sizeof name##_p32init / sizeof name##_p32init[0]];                                               \
  __attribute__((constructor)) static void name##_p32fill(void) {                                                      \
    for (unsigned long i = 0; i < sizeof name / sizeof name[0]; i++)                                                   \
      name[i] = name##_p32init[i];                                                                                     \
  }
#define LP64_P32_INIT_SCALAR(type, name)                                                                               \
  type *LP64_P32 name;                                                                                                 \
  __attribute__((constructor)) static void name##_p32fill(void) { name = name##_p32init; }
#else
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ > 4 && !defined(LP64_P32_WIDE_OK)
#error "lp64_p32.h: 8-byte pointers but no __ptr32 here (x86-64 clang with -fms-extensions; else LP64_P32_OFFSET=1, the offset model); see LP64_P32_WIDE_OK"
#endif
#define LP64_P32_ON 0
#define LP64_P32
#define LP64_P32F(T) T
#define LP64_P32_INIT(name) name
#define LP64_P32_INIT_ARRAY(type, name)
#define LP64_P32_INIT_SCALAR(type, name)
#endif
#endif

#ifndef LP64_P32_NO_SHORT_NAMES
#define P32 LP64_P32
#define P32F(T) LP64_P32F(T)
#define P32_INIT(name) LP64_P32_INIT(name)
#define P32_INIT_ARRAY(type, name) LP64_P32_INIT_ARRAY(type, name)
#define P32_INIT_SCALAR(type, name) LP64_P32_INIT_SCALAR(type, name)
#define P32_GET(x) LP64_P32_GET(x)
#define P32_SET(x, v) LP64_P32_SET(x, v)
#define P32_ADD(x, n) LP64_P32_ADD(x, n)
#define P32_SUB(x, n) LP64_P32_SUB(x, n)
#define P32_PREINC(x) LP64_P32_PREINC(x)
#define P32_PREDEC(x) LP64_P32_PREDEC(x)
#define P32_POSTINC(x) LP64_P32_POSTINC(x)
#define P32_POSTDEC(x) LP64_P32_POSTDEC(x)
#define P32_PATCH(x, base) LP64_P32_PATCH(x, base)
#define P32_PATCH2(x, base) LP64_P32_PATCH2(x, base)
#define P32_ENC(p) LP64_P32_ENC(p)
#define P32_DEC(i) LP64_P32_DEC(i)
#define P32_ARG_T LP64_P32_ARG_T
#define P32_ARG(a) LP64_P32_ARG(a)
#endif

#endif

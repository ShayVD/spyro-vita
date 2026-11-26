/* 32-bit pointer fields on a 64-bit host: the low-image model (D173, D174;
 * docs/ps4-plan.md). Forced into every C and C++ unit (CMakeLists.txt).
 *
 * The game's structs and data labels are laid out as on the PS1, with 4-byte
 * pointers: the disc's data is fixed up in place (loaders.c PATCH_POINTER)
 * and the hand C reads fields at raw offsets. On x86-64 with clang
 * (-fms-extensions) P32 makes such a pointer 32 bits wide, zero-extended
 * when read (__uptr); everything the game can point at lives in the
 * executable's image, below 2 GB (src/platform/linux32/low_image.c), so
 * nothing is lost. On every other target P32F(T *) is T * and the text
 * compiles as it always has: linux32 and the Vita don't change.
 *
 * A P32 pointer is spelled P32F(T *) (D250 O1): `P32F(Moby *) m_Next;`,
 * `P32F(P32F(Moby *) *) m_List;` (two levels), `(P32F(Moby *) *)p` (a
 * pointer to such a slot). The type is the macro's argument so that an
 * ARM64 build, where __ptr32 does nothing, can make each one a 32-bit
 * offset (the offset model, D250). There is no bare P32 any more: a
 * declaration in the old form, `T *P32 name`, fails to compile everywhere.
 *
 * Function pointers stored in the game's memory can't be P32: clang 19
 * loads 8 bytes for a call through a __ptr32 function pointer, or crashes
 * in its backend (D174). The decomp has 13, the overlays' (overlay_pointers.h);
 * on x86-64 they are normal 8-byte globals of the port's
 * (src/game/p32_fnptrs.c), their PS1 slots renamed (cmake/game.cmake).
 *
 * Never mix a P32 pointer and a normal one in `?:` (cast one side): clang
 * 19 hangs on it and Apple clang 16 crashes (D173, D174).
 *
 * Nor initialise a P32 pointer with an address in a static initializer:
 * clang 19's backend crashes ("addrspacecast in static initializer"). Such a
 * definition (`char *g_Names[3] = {"A", ...};`) is written
 * `char *P32_INIT(g_Names)[3] = {...};` and, at the file's end,
 * `P32_INIT_ARRAY(char, g_Names)` (or P32_INIT_SCALAR for one pointer): on
 * x86-64 the initializer is a normal table, g_Names_p32init, and a
 * constructor copies it into the real P32 g_Names before main(). Elsewhere
 * P32_INIT(n) is n and the rest nothing. */
#ifndef SPYRO_P32_H
#define SPYRO_P32_H

/* A C name's assembly name, for __asm__ labels: SPYRO_ASM_LIT("name").
 * Mach-O puts an underscore before C's names (macOS, D250 O4); elsewhere
 * the prefix is empty and this is the literal. */
#define SPYRO_ASM_STR_(x) #x
#define SPYRO_ASM_STR(x) SPYRO_ASM_STR_(x)
#define SPYRO_ASM_LIT(s) SPYRO_ASM_STR(__USER_LABEL_PREFIX__) s

/* The offset model (D250, D258): every P32 slot is a 32-bit offset from
 * spyro_p32_base, 0 for NULL. The default on AArch64; SPYRO_P32_OFFSET=1
 * selects it on x86-64 too, where it is O2's completeness check: the slot is
 * a union, so a read or write that doesn't go through the accessors below is
 * a type error, and so is an accessor on something that isn't a slot. The
 * union's zero-length array carries the pointer type without adding size. */
#ifndef SPYRO_P32_OFFSET
#if defined(__aarch64__) && defined(__LP64__)
#define SPYRO_P32_OFFSET 1
#else
#define SPYRO_P32_OFFSET 0
#endif
#endif

#if SPYRO_P32_OFFSET
#define SPYRO_P32 1
#if defined(__APPLE__)
/* macOS (D250 O4): the executable's Mach-O header, where the kernel slid it */
extern char spyro_p32_base[] __asm__("__mh_execute_header");
#else
extern char spyro_p32_base[]; /* the image's start (CMakeLists.txt) */
#endif
/* Macros, not functions: the overlays' units are preprocessed and then
 * compiled with this header forced in again (cmake/overlays.cmake), where
 * a function would be defined twice. */
#define p32_dec_(o)                                                            \
  (__extension__({ unsigned p32_d_ = (o); p32_d_ ? (void *)(spyro_p32_base + p32_d_) : (void *)0; }))
/* A pointer into the 4 GB above the base becomes its offset; anything else
 * (NULL, a small constant, a sentinel such as termPrim's 0xffffffff) is
 * kept as its low 32 bits, as the 32-bit targets keep it. The checking
 * build (cmake -DSPYRO_P32_CHECK=ON, D250 O3) stops at a value above 4 GB
 * outside that window: a host pointer stored into the game's memory, which
 * no 32-bit word can hold (the low 4 GB is reserved, low_image.c, so
 * nothing below it is the host's). */
#if SPYRO_P32_CHECK
void p32_check_fail_(unsigned long long p, const char *file, int line) __attribute__((noreturn));
#define P32_CHECK_(r) ((r) >= 0x100000000ull ? p32_check_fail_((r), __FILE__, __LINE__) : (void)0)
#else
#define P32_CHECK_(r) ((void)0)
#endif
#define p32_enc_(p)                                                            \
  (__extension__({ uintptr_t p32_r_ = (uintptr_t)(p), p32_e_ = p32_r_ - (uintptr_t)spyro_p32_base; \
     p32_e_ < 0x100000000ull ? (unsigned)p32_e_ : (P32_CHECK_(p32_r_), (unsigned)p32_r_); }))
/* the same, unchecked: an entry's argument may be any integer (P32_ARG) */
#define p32_arg_(a)                                                            \
  (__extension__({ uintptr_t p32_r_ = (uintptr_t)(a), p32_e_ = p32_r_ - (uintptr_t)spyro_p32_base; \
     p32_e_ < 0x100000000ull ? (unsigned)p32_e_ : (unsigned)p32_r_; }))
#define P32F(T) union __attribute__((packed, aligned(4))) { unsigned p32_off; T p32_ty[0]; }
#define P32_TYPE_(x) __typeof__((x).p32_ty[0])
#define P32_GET(x) ((P32_TYPE_(x))p32_dec_((x).p32_off))
#define P32_SET(x, v)                                                          \
  (__extension__({ __typeof__(&(x)) p32_s_ = &(x); P32_TYPE_(x) p32_v_ = (v);  \
     p32_s_->p32_off = p32_enc_(p32_v_); p32_v_; }))
#define P32_STEP_(x, n, post)                                                  \
  (__extension__({ __typeof__(&(x)) p32_s_ = &(x);                            \
     P32_TYPE_(x) p32_o_ = P32_GET(*p32_s_), p32_n_ = p32_o_ + (n);           \
     p32_s_->p32_off = p32_enc_(p32_n_); post ? p32_o_ : p32_n_; }))
#define P32_ADD(x, n) P32_STEP_(x, n, 0)
#define P32_SUB(x, n) P32_STEP_(x, -(n), 0)
#define P32_PREINC(x) P32_STEP_(x, 1, 0)
#define P32_PREDEC(x) P32_STEP_(x, -1, 0)
#define P32_POSTINC(x) P32_STEP_(x, 1, 1)
#define P32_POSTDEC(x) P32_STEP_(x, -1, 1)
/* the disc loaders' fix-up (common.h PATCH_POINTER): the slot holds an
 * offset into the file just read; it becomes that file's base plus it */
#define P32_PATCH(x, base) P32_SET(x, (void *)((char *)(base) + (x).p32_off))
#define P32_PATCH2(x, base) P32_PATCH(x, base)
/* A pointer the code turns into a 32-bit integer, and back (D250 O3): the
 * integer holds the offset, so arithmetic, round trips and alignment work
 * as with an address (the base is 16 MB-aligned on Linux). Written on the
 * cast's operand: `(int)P32_ENC(p)`, `(T *)P32_DEC(i)`. A macro that takes
 * either a pointer or an address (`WORD_AT`, `PATCH_POINTER`) has them in
 * its body: each passes the other kind through, chosen by type. */
#define P32_IS_PTR_(x) (__builtin_classify_type(x) == 5) /* arrays, functions too */
#define P32_ENC(p) __builtin_choose_expr(P32_IS_PTR_(p), p32_enc_(p), (unsigned)(uintptr_t)(p))
/* The hand C's entries (render_hand.h) take the decomp's arguments as
 * 32-bit words, where the decomp passes pointers: in the offset model each
 * is a 64-bit register, encoded on entry (an integer argument arrives
 * zero-extended, below the image, and is kept). */
#define P32_ARG_T uintptr_t
#define P32_ARG(a) ((unsigned)p32_arg_(a))
#define P32_DEC(i)                                                             \
  __builtin_choose_expr(P32_IS_PTR_(i), (void *)(uintptr_t)(i), p32_dec_((unsigned)(uintptr_t)(i)))
/* static tables need constant initialisers: an offset model's are made at
 * start-up, as the low image's are (below) */
#define P32_INIT(name) name##_p32init
#define P32_INIT_ARRAY(type, name)                                             \
  __typeof__(name[0]) name[sizeof name##_p32init / sizeof name##_p32init[0]];  \
  __attribute__((constructor)) static void name##_p32fill(void) {              \
    for (unsigned long i = 0; i < sizeof name / sizeof name[0]; i++)           \
      P32_SET(name[i], name##_p32init[i]);                                     \
  }
#define P32_INIT_SCALAR(type, name)                                            \
  __typeof__(name) name;                                                       \
  __attribute__((constructor)) static void name##_p32fill(void) { P32_SET(name, name##_p32init); }
#else
/* Elsewhere a slot is a pointer (__ptr32 or plain) and the accessors are the
 * text the code had before O2, so the 32-bit and x86-64 builds don't change */
#define P32_GET(x) (x)
#define P32_SET(x, v) ((x) = (v))
#define P32_ADD(x, n) ((x) += (n))
#define P32_SUB(x, n) ((x) -= (n))
#define P32_PREINC(x) (++(x))
#define P32_PREDEC(x) (--(x))
#define P32_POSTINC(x) ((x)++)
#define P32_POSTDEC(x) ((x)--)
#define P32_PATCH(x, base) x = (__typeof__(x))((u_int)(base) + (u_int)(x))
#define P32_PATCH2(x, base) x = (__typeof__(x))((u_int)(x) + (u_int)(base))
#define P32_ENC(p) (p)
#define P32_DEC(i) (i)
#define P32_ARG_T unsigned int
#define P32_ARG(a) (a)
#endif

#if SPYRO_P32_OFFSET
/* (above) */
#elif defined(__x86_64__) && defined(__clang__)
#if defined(_WIN32)
/* mingw's _mingw.h defines __ptr32 and __ptr64 as empty macros (MSVC's
 * keywords, for other compilers); once any system header is in, every P32
 * field would silently be 8 bytes (the PC's Windows build, D241). Take
 * _mingw.h now (it is guarded, so it never defines them again) and drop the
 * two: clang's own keywords remain. */
#include <_mingw.h>
#undef __ptr32
#undef __ptr64
#include "pc_win.h" /* the BSD type names glibc has and mingw lacks */
#endif
#define SPYRO_P32 1
#define P32F(T) T __ptr32 __uptr
#define P32_INIT(name) name##_p32init
#define P32_INIT_ARRAY(type, name)                                             \
  P32F(type *) name[sizeof name##_p32init / sizeof name##_p32init[0]];         \
  __attribute__((constructor)) static void name##_p32fill(void) {              \
    for (unsigned long i = 0; i < sizeof name / sizeof name[0]; i++)           \
      name[i] = name##_p32init[i];                                             \
  }
#define P32_INIT_SCALAR(type, name)                                            \
  P32F(type *) name;                                                           \
  __attribute__((constructor)) static void name##_p32fill(void) { name = name##_p32init; }
#else
#define SPYRO_P32 0
#define P32F(T) T
#define P32_INIT(name) name
#define P32_INIT_ARRAY(type, name)
#define P32_INIT_SCALAR(type, name)
#endif

#endif

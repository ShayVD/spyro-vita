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

#if defined(__x86_64__) && defined(__clang__)
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

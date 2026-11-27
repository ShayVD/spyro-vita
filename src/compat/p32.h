/* 32-bit pointer fields on a 64-bit host (D173, D174, D250; docs/ps4-plan.md):
 * Encore's lp64_p32.h (third_party/encore/lp64/runtime/p32, D279), with
 * spyro's names mapped onto the kit's. Forced into every C and C++ unit
 * (CMakeLists.txt).
 *
 * The model, the accessors (P32F, P32_GET, P32_SET, ..., P32_ENC, P32_DEC,
 * P32_ARG, P32_INIT*) and their traps are documented there. The accessors
 * keep their names; the old spyro names below are aliases:
 *   SPYRO_P32_OFFSET  LP64_P32_OFFSET  the offset model (default on AArch64;
 *                                      =1 on x86-64 is O2's completeness check)
 *   SPYRO_P32_CHECK   LP64_P32_CHECK   the checking build (cmake -DSPYRO_P32_CHECK=ON)
 *   SPYRO_P32         LP64_P32_ON      1 where P32 means something
 *   spyro_p32_base    lp64_p32_base    the offset model's base (CMakeLists.txt's
 *                                      --defsym; macOS's Mach-O header)
 *   SPYRO_ASM_LIT     LP64_ASM_LIT     a C name's assembly name, for __asm__ labels
 * The 13 function pointers stored in the game's memory are normal 8-byte
 * globals of the port's on x86-64 (src/game/p32_fnptrs.c, cmake/game.cmake). */
#ifndef SPYRO_P32_H
#define SPYRO_P32_H

#if defined(SPYRO_P32_OFFSET) && !defined(LP64_P32_OFFSET)
#define LP64_P32_OFFSET SPYRO_P32_OFFSET
#endif
#if defined(SPYRO_P32_CHECK) && !defined(LP64_P32_CHECK)
#define LP64_P32_CHECK SPYRO_P32_CHECK
#endif

#include "../../third_party/encore/lp64/runtime/p32/lp64_p32.h"

#ifndef SPYRO_P32_OFFSET
#define SPYRO_P32_OFFSET LP64_P32_OFFSET
#endif
#define SPYRO_P32 LP64_P32_ON
#define spyro_p32_base lp64_p32_base
#define SPYRO_ASM_LIT(s) LP64_ASM_LIT(s)

#if !LP64_P32_OFFSET && defined(__x86_64__) && defined(__clang__) && defined(_WIN32)
#include "pc_win.h" /* the BSD type names glibc has and mingw lacks (the PC's Windows build, D241) */
#endif

#endif

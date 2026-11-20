/* Forced include for every decomp C file in the port build (cmake/game.cmake).
 * Turns the decomp's INCLUDE_ASM / INCLUDE_RODATA into nothing: the asm they
 * name comes from tools/mips2c output or hand-written C in src/game/. */
#ifndef SPYRO_PORT_PREFIX_H
#define SPYRO_PORT_PREFIX_H

#define INCLUDE_ASM(FOLDER, NAME)
#define INCLUDE_ASM_REORDER_HACK(FOLDER, NAME)
#define INCLUDE_RODATA(FOLDER, NAME)
#define INCLUDE_ASM_MACRO

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdint.h>

#include "tick60_sites.h" /* 60 Hz logic's sites: macros only (D130) */

/* PS1 RAM address -> host address in the arena (src/platform/common/arena.c).
 * Used by patches/spyro-1-port for the decomp's hard-coded RAM addresses. */
extern uintptr_t g_psx_arena_base;
#if SPYRO_NATIVE_MEM
/* The native build (decisions D106): the game's memory is a block from the
 * heap starting at main_BSS_END's page, 0x8007A000 (psx_arena.h
 * PSX_MEM_PSX); the patches name only overlay space and its end. */
#define PSX_ADDR(a) (g_psx_arena_base + ((uint32_t)(a) - 0x8007A000u))
#else
#define PSX_ADDR(a) (g_psx_arena_base + ((uint32_t)(a) - 0x80000000u))
#endif

/* A pointer the game may hold as a KUSEG RAM alias (0x00000000-0x001FFFFF,
 * the same RAM as 0x80000000): on the PS1 an access through it lands in
 * RAM, here it goes to the same place. For original-game bugs that store
 * through small integers (patches/spyro-1-port/0006, 0007; decisions D42).
 * Below 0x10000 is the PS1 kernel's RAM, which the game never uses; the port
 * keeps the scratchpad at the arena's base, so those addresses go to
 * g_psx_kernel_ram (src/platform/common/arena.c) instead. NULL stays NULL. */
extern unsigned char g_psx_kernel_ram[0x10000];
#if SPYRO_NATIVE_MEM
/* the rest of RAM by its PS1 address (src/game/port.c, D106) */
uintptr_t psx_kuseg_host(uint32_t a);
#define PSX_KUSEG_RAM_(a) psx_kuseg_host(a)
#else
#define PSX_KUSEG_RAM_(a) (g_psx_arena_base + (a))
#endif
#define PSX_KUSEG(p)                                                           \
  ((__typeof__(p))(                                                            \
      (uint32_t)(uintptr_t)(p) - 1u < 0xFFFFu                                  \
          ? (uintptr_t)g_psx_kernel_ram + (uint32_t)(uintptr_t)(p)             \
      : (uint32_t)(uintptr_t)(p) - 0x10000u < 0x1F0000u                        \
          ? PSX_KUSEG_RAM_((uint32_t)(uintptr_t)(p))                           \
          : (uintptr_t)(p)))

/* Packed and tagged addresses (decisions D97, D100): the native build's
 * patches decode them through these (patches/spyro-1-port/0011). */
#include "../game/psx_mem.h"

/* Log and abort on a path the port doesn't support (src/game/port.c). */
void PortFatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#endif

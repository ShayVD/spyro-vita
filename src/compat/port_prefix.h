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

/* PS1 RAM address -> host address in the arena (src/platform/common/arena.c).
 * Used by patches/spyro-1-port for the decomp's hard-coded RAM addresses. */
extern uintptr_t g_psx_arena_base;
#define PSX_ADDR(a) (g_psx_arena_base + ((uint32_t)(a) - 0x80000000u))

/* A pointer the game may hold as a KUSEG RAM alias (0x00000000-0x001FFFFF,
 * the same RAM as 0x80000000): on the PS1 an access through it lands in
 * RAM, here it goes to the same place. For original-game bugs that store
 * through small integers (patches/spyro-1-port/0006, 0007; decisions D42).
 * Below 0x10000 is the PS1 kernel's RAM, which the game never uses; the port
 * keeps the scratchpad at the arena's base, so those addresses go to
 * g_psx_kernel_ram (src/platform/common/arena.c) instead. NULL stays NULL. */
extern unsigned char g_psx_kernel_ram[0x10000];
#define PSX_KUSEG(p)                                                           \
  ((__typeof__(p))(                                                            \
      (uint32_t)(uintptr_t)(p) - 1u < 0xFFFFu                                  \
          ? (uintptr_t)g_psx_kernel_ram + (uint32_t)(uintptr_t)(p)             \
      : (uint32_t)(uintptr_t)(p) - 0x10000u < 0x1F0000u                        \
          ? g_psx_arena_base + (uint32_t)(uintptr_t)(p)                        \
          : (uintptr_t)(p)))

/* Log and abort on a path the port doesn't support (src/game/port.c). */
void PortFatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#endif

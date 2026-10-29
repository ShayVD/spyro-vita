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

/* Log and abort on a path the port doesn't support (src/game/port.c). */
void PortFatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#endif

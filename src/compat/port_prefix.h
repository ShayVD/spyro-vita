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

#endif

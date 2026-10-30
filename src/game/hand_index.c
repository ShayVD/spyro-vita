/* Hand-written replacements for mips2c units (cmake/mips2c.cmake M2C_HAND),
 * by PS1 address, so the oracle replay (oracle_replay.c) can call them as it
 * calls translated entry points. */
#include "mips2c_rt.h"

#define HAND(addr, name)                                                       \
  { addr, #name, (u32(*)(u32, u32, u32, u32))name }

/* M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) puts the mips2c unit back for A/B
 * tests; m2c_index.c then lists it instead. */
u32 func_80033F08(); /* camera_hand.c */
u32 func_80034480(); /* camera_hand.c */
u32 func_80034CE8(); /* camera_hand.c */
u32 func_800357A4(); /* camera_hand.c */
u32 func_80048D10(); /* pete_hand.c */
u32 func_80049FAC(); /* pete_hand.c */

const M2cEntry m2c_hand_index[] = {
#if !M2C_func_80033F08
    HAND(0x80033F08u, func_80033F08),
#endif
#if !M2C_func_80034480
    HAND(0x80034480u, func_80034480),
#endif
#if !M2C_func_80034CE8
    HAND(0x80034CE8u, func_80034CE8),
#endif
#if !M2C_func_800357A4
    HAND(0x800357A4u, func_800357A4),
#endif
#if !M2C_func_80048D10
    HAND(0x80048D10u, func_80048D10),
#endif
#if !M2C_func_80049FAC
    HAND(0x80049FACu, func_80049FAC),
#endif
    {0, 0, 0},
};

u32 func_80043FE4__m2c(); /* pete_hand.c, partly hand-written */
u32 func_80043FE4_gen__m2c(); /* its mips2c unit, renamed */

/* The mips2c units of fully hand-written functions, linked as <name>_gen
 * only with cmake SPYRO_M2C_REF=ON (M2C_REF=1), for differential sweeps. */
#define GEN(addr, name)                                                        \
  { addr, #name, (u32(*)(u32, u32, u32, u32))name##_gen__m2c }
u32 func_80033F08_gen__m2c();
u32 func_80034480_gen__m2c();
u32 func_80034CE8_gen__m2c();
u32 func_800357A4_gen__m2c();
u32 func_80048D10_gen__m2c();
u32 func_80049FAC_gen__m2c();

const M2cEntry m2c_gen_index[] = {
#if !M2C_func_80043FE4
    {0x80043FE4u, "func_80043FE4", (u32(*)(u32, u32, u32, u32))func_80043FE4_gen__m2c},
#endif
#if M2C_REF && !M2C_func_80033F08
    GEN(0x80033F08u, func_80033F08),
#endif
#if M2C_REF && !M2C_func_80034480
    GEN(0x80034480u, func_80034480),
#endif
#if M2C_REF && !M2C_func_80034CE8
    GEN(0x80034CE8u, func_80034CE8),
#endif
#if M2C_REF && !M2C_func_800357A4
    GEN(0x800357A4u, func_800357A4),
#endif
#if M2C_REF && !M2C_func_80048D10
    GEN(0x80048D10u, func_80048D10),
#endif
#if M2C_REF && !M2C_func_80049FAC
    GEN(0x80049FACu, func_80049FAC),
#endif
    {0, 0, 0},
};

/* Hand-written replacements for mips2c units (cmake/mips2c.cmake M2C_HAND),
 * by PS1 address, so the oracle replay (oracle_replay.c) can call them as it
 * calls translated entry points. */
#include "mips2c_rt.h"

#define HAND(addr, name)                                                       \
  { addr, #name, (u32(*)(u32, u32, u32, u32))name }

/* M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) puts the mips2c unit back for A/B
 * tests; m2c_index.c then lists it instead. */
u32 func_80033F08(); /* camera_hand.c */
u32 func_80048D10(); /* pete_hand.c */
u32 func_80049FAC(); /* pete_hand.c */

const M2cEntry m2c_hand_index[] = {
#if !M2C_func_80033F08
    HAND(0x80033F08u, func_80033F08),
#endif
#if !M2C_func_80048D10
    HAND(0x80048D10u, func_80048D10),
#endif
#if !M2C_func_80049FAC
    HAND(0x80049FACu, func_80049FAC),
#endif
    {0, 0, 0},
};

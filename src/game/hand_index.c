/* Hand-written replacements for mips2c units (cmake/mips2c.cmake M2C_HAND),
 * by PS1 address, so the oracle replay (oracle_replay.c) can call them as it
 * calls translated entry points. */
#include "mips2c_rt.h"

#define HAND(addr, name)                                                       \
  { addr, #name, (u32(*)(u32, u32, u32, u32))name }

u32 func_80033F08(); /* camera_hand.c */
u32 func_80049FAC(); /* pete_hand.c */

const M2cEntry m2c_hand_index[] = {
    HAND(0x80033F08u, func_80033F08),
    HAND(0x80049FACu, func_80049FAC),
    {0, 0, 0},
};

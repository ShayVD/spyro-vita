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
u32 func_8003EA68(); /* pete_state_hand.c */
u32 func_8003FE40(); /* pete_state_hand.c */
u32 func_80035FB4(); /* camera_hand.c */
u32 func_80043FE4(); /* pete_hand.c */
u32 func_80047B60(); /* pete_dispatch_hand.c */
u32 func_80041670(); /* pete_transition_hand.c */
u32 func_80048D10(); /* pete_hand.c */
u32 func_80049FAC(); /* pete_hand.c */
u32 ColTraceRay();      /* collision_hand.c */
u32 ColSphere();        /* collision_hand.c */
u32 ColFindFloor();     /* collision_hand.c */
u32 func_8004DF24();    /* collision_hand.c */
u32 ColSpyroInHitbox(); /* collision_hand.c */
u32 ColMoby();          /* collision_hand.c */

/* ColMoby's fifth and sixth arguments are on the stack, as the asm reads
 * them (0x10 and 0x14 above $sp). */
static u32 ColMoby_regs(u32 a0, u32 a1, u32 a2, u32 a3) {
  return ColMoby(a0, a1, a2, a3, M_U32(MIPS_KUSEG(g_mips_sp + 0x10u)), M_U32(MIPS_KUSEG(g_mips_sp + 0x14u)));
}

/* collision.s's entries taken from mips2c (M2C_<name>=1, cmake
 * M2C_HAND_MULTI): the unit is linked with each renamed <name>_gen. */
u32 ColTraceRay_gen__m2c();
u32 ColSphere_gen__m2c();
u32 ColFindFloor_gen__m2c();
u32 func_8004DF24_gen__m2c();
u32 ColSpyroInHitbox_gen__m2c();
u32 ColMoby_gen__m2c();
#define GEN_AS(addr, name)                                                     \
  { addr, #name, (u32(*)(u32, u32, u32, u32))name##_gen__m2c }

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
#if !M2C_func_8003EA68
    HAND(0x8003EA68u, func_8003EA68),
#endif
#if !M2C_func_8003FE40
    HAND(0x8003FE40u, func_8003FE40),
#endif
#if !M2C_func_80035FB4
    HAND(0x80035FB4u, func_80035FB4),
#endif
#if !M2C_func_80043FE4
    HAND(0x80043FE4u, func_80043FE4),
#endif
#if !M2C_func_80047B60
    HAND(0x80047B60u, func_80047B60),
#endif
#if !M2C_func_80041670
    HAND(0x80041670u, func_80041670),
#endif
#if !M2C_func_80048D10
    HAND(0x80048D10u, func_80048D10),
#endif
#if !M2C_func_80049FAC
    HAND(0x80049FACu, func_80049FAC),
#endif
#if M2C_ColTraceRay
    GEN_AS(0x8004AE38u, ColTraceRay),
#else
    HAND(0x8004AE38u, ColTraceRay),
#endif
#if M2C_ColSphere
    GEN_AS(0x8004BE4Cu, ColSphere),
#else
    HAND(0x8004BE4Cu, ColSphere),
#endif
#if M2C_ColFindFloor
    GEN_AS(0x8004D5ECu, ColFindFloor),
#else
    HAND(0x8004D5ECu, ColFindFloor),
#endif
#if M2C_func_8004DF24
    GEN_AS(0x8004DF24u, func_8004DF24),
#else
    HAND(0x8004DF24u, func_8004DF24),
#endif
#if M2C_ColSpyroInHitbox
    GEN_AS(0x8004E2E8u, ColSpyroInHitbox),
#else
    HAND(0x8004E2E8u, ColSpyroInHitbox),
#endif
#if M2C_ColMoby
    GEN_AS(0x8004E3C8u, ColMoby),
#else
    {0x8004E3C8u, "ColMoby", (u32(*)(u32, u32, u32, u32))ColMoby_regs},
#endif
    {0, 0, 0},
};

/* The mips2c units of fully hand-written functions, linked as <name>_gen
 * only with cmake SPYRO_M2C_REF=ON (M2C_REF=1), for differential sweeps. */
#define GEN(addr, name)                                                        \
  { addr, #name, (u32(*)(u32, u32, u32, u32))name##_gen__m2c }
u32 func_80033F08_gen__m2c();
u32 func_80034480_gen__m2c();
u32 func_80034CE8_gen__m2c();
u32 func_800357A4_gen__m2c();
u32 func_8003EA68_gen__m2c();
u32 func_8003FE40_gen__m2c();
u32 func_80035FB4_gen__m2c();
u32 func_80043FE4_gen__m2c();
u32 func_80047B60_gen__m2c();
u32 func_80041670_gen__m2c();
u32 func_80048D10_gen__m2c();
u32 func_80049FAC_gen__m2c();
/* The hand-written renderers' units are always linked (render_hand.h). */
u32 func_8004EBA8_gen__m2c();
u32 func_800258F0_gen__m2c();
u32 func_80023AC4_gen__m2c();
u32 func_8001F798_gen__m2c();
u32 func_80022A2C_gen__m2c();
u32 func_80020F34_gen__m2c();

const M2cEntry m2c_gen_index[] = {
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
#if M2C_REF && !M2C_func_8003EA68
    GEN(0x8003EA68u, func_8003EA68),
#endif
#if M2C_REF && !M2C_func_8003FE40
    GEN(0x8003FE40u, func_8003FE40),
#endif
#if M2C_REF && !M2C_func_80035FB4
    GEN(0x80035FB4u, func_80035FB4),
#endif
#if M2C_REF && !M2C_func_80043FE4
    GEN(0x80043FE4u, func_80043FE4),
#endif
#if M2C_REF && !M2C_func_80047B60
    GEN(0x80047B60u, func_80047B60),
#endif
#if M2C_REF && !M2C_func_80041670
    GEN(0x80041670u, func_80041670),
#endif
#if M2C_REF && !M2C_func_80048D10
    GEN(0x80048D10u, func_80048D10),
#endif
#if M2C_REF && !M2C_func_80049FAC
    GEN(0x80049FACu, func_80049FAC),
#endif
#if M2C_REF
    GEN(0x8004AE38u, ColTraceRay),
    GEN(0x8004BE4Cu, ColSphere),
    GEN(0x8004D5ECu, ColFindFloor),
    GEN(0x8004DF24u, func_8004DF24),
    GEN(0x8004E2E8u, ColSpyroInHitbox),
    GEN(0x8004E3C8u, ColMoby),
#endif
    GEN(0x8004EBA8u, func_8004EBA8),
    GEN(0x800258F0u, func_800258F0),
    GEN(0x80023AC4u, func_80023AC4),
    GEN(0x8001F798u, func_8001F798),
    GEN(0x80022A2Cu, func_80022A2C),
    GEN(0x80020F34u, func_80020F34),
    {0, 0, 0},
};

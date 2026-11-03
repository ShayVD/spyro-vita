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
u32 func_8002F3E4(); /* gamestates_hand.c */
u32 func_800314B4(); /* gamestates_hand.c */
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

/* math.s and 42CC4.s (math_hand.c, 42CC4_hand.c, decisions D87): each
 * entry the hand C, or with M2C_<name>=1 the unit's, linked as <name>_gen
 * (cmake M2C_HAND_MULTI). GCC folds the constant choice, so the other
 * symbol isn't referenced. */
#define MULTI(addr, name)                                                      \
  { addr, #name, (u32(*)(u32, u32, u32, u32))(M2C_##name ? (void *)name##_gen__m2c : (void *)name) }
u32 Atan2Fast();
u32 Atan2();
u32 Sin();
u32 Cos();
u32 func_80016D08();
u32 RotVec8ToMatrix();
u32 func_80016FD0();
u32 VecRotateByMatrix();
u32 VecRotateByLastMatrix();
u32 VecRotateByCam();
u32 func_80017188();
u32 VecMagnitude();
u32 VecRefineMagnitude();
u32 func_80017330();
u32 func_80017428();
u32 func_80017548();
u32 VecScaleToLength();
u32 func_80017614();
u32 VecShiftLeft();
u32 VecShiftRight();
u32 VecNull();
u32 VecCopy();
u32 func_8001771C();
u32 VecAdd();
u32 VecSub();
u32 VecMult();
u32 func_800177F8();
u32 func_80017854();
u32 func_80017894();
u32 func_80017908();
u32 func_80017928();
u32 func_80017948();
u32 func_8001796C();
u32 OctDistance();
u32 func_800179F0();
u32 func_80017A38();
u32 func_80017AA4();
u32 func_80017B48();
u32 func_80017BFC();
u32 func_80017C24();
u32 func_80017C4C();
u32 VecToShortVec();
u32 func_80017C84();
u32 ColTriUnpack();
u32 func_80017D7C();
u32 Atan2Fast_gen__m2c();
u32 Atan2_gen__m2c();
u32 Sin_gen__m2c();
u32 Cos_gen__m2c();
u32 func_80016D08_gen__m2c();
u32 RotVec8ToMatrix_gen__m2c();
u32 func_80016FD0_gen__m2c();
u32 VecRotateByMatrix_gen__m2c();
u32 VecRotateByLastMatrix_gen__m2c();
u32 VecRotateByCam_gen__m2c();
u32 func_80017188_gen__m2c();
u32 VecMagnitude_gen__m2c();
u32 VecRefineMagnitude_gen__m2c();
u32 func_80017330_gen__m2c();
u32 func_80017428_gen__m2c();
u32 func_80017548_gen__m2c();
u32 VecScaleToLength_gen__m2c();
u32 func_80017614_gen__m2c();
u32 VecShiftLeft_gen__m2c();
u32 VecShiftRight_gen__m2c();
u32 VecNull_gen__m2c();
u32 VecCopy_gen__m2c();
u32 func_8001771C_gen__m2c();
u32 VecAdd_gen__m2c();
u32 VecSub_gen__m2c();
u32 VecMult_gen__m2c();
u32 func_800177F8_gen__m2c();
u32 func_80017854_gen__m2c();
u32 func_80017894_gen__m2c();
u32 func_80017908_gen__m2c();
u32 func_80017928_gen__m2c();
u32 func_80017948_gen__m2c();
u32 func_8001796C_gen__m2c();
u32 OctDistance_gen__m2c();
u32 func_800179F0_gen__m2c();
u32 func_80017A38_gen__m2c();
u32 func_80017AA4_gen__m2c();
u32 func_80017B48_gen__m2c();
u32 func_80017BFC_gen__m2c();
u32 func_80017C24_gen__m2c();
u32 func_80017C4C_gen__m2c();
u32 VecToShortVec_gen__m2c();
u32 func_80017C84_gen__m2c();
u32 ColTriUnpack_gen__m2c();
u32 func_80017D7C_gen__m2c();
u32 MobyAlloc();
u32 MobyFree();
u32 MobyUpdateCollision();
u32 MobySetShaded();
u32 func_800529E4();
u32 MobyGetAnimatedVertex();
u32 func_80052F38();
u32 func_800530C0();
u32 MobyUpdateShadow();
u32 MobyAlloc_gen__m2c();
u32 MobyFree_gen__m2c();
u32 MobyUpdateCollision_gen__m2c();
u32 MobySetShaded_gen__m2c();
u32 func_800529E4_gen__m2c();
u32 MobyGetAnimatedVertex_gen__m2c();
u32 func_80052F38_gen__m2c();
u32 func_800530C0_gen__m2c();
u32 MobyUpdateShadow_gen__m2c();

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
#if !M2C_func_8002F3E4
    HAND(0x8002F3E4u, func_8002F3E4),
#endif
#if !M2C_func_800314B4
    HAND(0x800314B4u, func_800314B4),
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
    MULTI(0x800169ACu, Atan2Fast),
    MULTI(0x80016AB4u, Atan2),
    MULTI(0x80016C58u, Sin),
    MULTI(0x80016CB0u, Cos),
    MULTI(0x80016D08u, func_80016D08),
    MULTI(0x80016D2Cu, RotVec8ToMatrix),
    MULTI(0x80016FD0u, func_80016FD0),
    MULTI(0x80017048u, VecRotateByMatrix),
    MULTI(0x800170C0u, VecRotateByLastMatrix),
    MULTI(0x80017110u, VecRotateByCam),
    MULTI(0x80017188u, func_80017188),
    MULTI(0x800171FCu, VecMagnitude),
    MULTI(0x8001729Cu, VecRefineMagnitude),
    MULTI(0x80017330u, func_80017330),
    MULTI(0x80017428u, func_80017428),
    MULTI(0x80017548u, func_80017548),
    MULTI(0x800175B8u, VecScaleToLength),
    MULTI(0x80017614u, func_80017614),
    MULTI(0x800176A0u, VecShiftLeft),
    MULTI(0x800176C8u, VecShiftRight),
    MULTI(0x800176F0u, VecNull),
    MULTI(0x80017700u, VecCopy),
    MULTI(0x8001771Cu, func_8001771C),
    MULTI(0x80017758u, VecAdd),
    MULTI(0x8001778Cu, VecSub),
    MULTI(0x800177C0u, VecMult),
    MULTI(0x800177F8u, func_800177F8),
    MULTI(0x80017854u, func_80017854),
    MULTI(0x80017894u, func_80017894),
    MULTI(0x80017908u, func_80017908),
    MULTI(0x80017928u, func_80017928),
    MULTI(0x80017948u, func_80017948),
    MULTI(0x8001796Cu, func_8001796C),
    MULTI(0x80017990u, OctDistance),
    MULTI(0x800179F0u, func_800179F0),
    MULTI(0x80017A38u, func_80017A38),
    MULTI(0x80017AA4u, func_80017AA4),
    MULTI(0x80017B48u, func_80017B48),
    MULTI(0x80017BFCu, func_80017BFC),
    MULTI(0x80017C24u, func_80017C24),
    MULTI(0x80017C4Cu, func_80017C4C),
    MULTI(0x80017C68u, VecToShortVec),
    MULTI(0x80017C84u, func_80017C84),
    MULTI(0x80017CB8u, ColTriUnpack),
    MULTI(0x80017D7Cu, func_80017D7C),
    MULTI(0x800524C4u, MobyAlloc),
    MULTI(0x80052568u, MobyFree),
    MULTI(0x800526A8u, MobyUpdateCollision),
    MULTI(0x800529CCu, MobySetShaded),
    MULTI(0x800529E4u, func_800529E4),
    MULTI(0x80052D64u, MobyGetAnimatedVertex),
    MULTI(0x80052F38u, func_80052F38),
    MULTI(0x800530C0u, func_800530C0),
    MULTI(0x800533D0u, MobyUpdateShadow),
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
u32 func_8002F3E4_gen__m2c();
u32 func_800314B4_gen__m2c();
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
    GEN(0x8002F3E4u, func_8002F3E4),
    GEN(0x800314B4u, func_800314B4),
#endif
#if M2C_REF
    GEN(0x8004AE38u, ColTraceRay),
    GEN(0x8004BE4Cu, ColSphere),
    GEN(0x8004D5ECu, ColFindFloor),
    GEN(0x8004DF24u, func_8004DF24),
    GEN(0x8004E2E8u, ColSpyroInHitbox),
    GEN(0x8004E3C8u, ColMoby),
    GEN(0x800169ACu, Atan2Fast),
    GEN(0x80016AB4u, Atan2),
    GEN(0x80016C58u, Sin),
    GEN(0x80016CB0u, Cos),
    GEN(0x80016D08u, func_80016D08),
    GEN(0x80016D2Cu, RotVec8ToMatrix),
    GEN(0x80016FD0u, func_80016FD0),
    GEN(0x80017048u, VecRotateByMatrix),
    GEN(0x800170C0u, VecRotateByLastMatrix),
    GEN(0x80017110u, VecRotateByCam),
    GEN(0x80017188u, func_80017188),
    GEN(0x800171FCu, VecMagnitude),
    GEN(0x8001729Cu, VecRefineMagnitude),
    GEN(0x80017330u, func_80017330),
    GEN(0x80017428u, func_80017428),
    GEN(0x80017548u, func_80017548),
    GEN(0x800175B8u, VecScaleToLength),
    GEN(0x80017614u, func_80017614),
    GEN(0x800176A0u, VecShiftLeft),
    GEN(0x800176C8u, VecShiftRight),
    GEN(0x800176F0u, VecNull),
    GEN(0x80017700u, VecCopy),
    GEN(0x8001771Cu, func_8001771C),
    GEN(0x80017758u, VecAdd),
    GEN(0x8001778Cu, VecSub),
    GEN(0x800177C0u, VecMult),
    GEN(0x800177F8u, func_800177F8),
    GEN(0x80017854u, func_80017854),
    GEN(0x80017894u, func_80017894),
    GEN(0x80017908u, func_80017908),
    GEN(0x80017928u, func_80017928),
    GEN(0x80017948u, func_80017948),
    GEN(0x8001796Cu, func_8001796C),
    GEN(0x80017990u, OctDistance),
    GEN(0x800179F0u, func_800179F0),
    GEN(0x80017A38u, func_80017A38),
    GEN(0x80017AA4u, func_80017AA4),
    GEN(0x80017B48u, func_80017B48),
    GEN(0x80017BFCu, func_80017BFC),
    GEN(0x80017C24u, func_80017C24),
    GEN(0x80017C4Cu, func_80017C4C),
    GEN(0x80017C68u, VecToShortVec),
    GEN(0x80017C84u, func_80017C84),
    GEN(0x80017CB8u, ColTriUnpack),
    GEN(0x80017D7Cu, func_80017D7C),
    GEN(0x800524C4u, MobyAlloc),
    GEN(0x80052568u, MobyFree),
    GEN(0x800526A8u, MobyUpdateCollision),
    GEN(0x800529CCu, MobySetShaded),
    GEN(0x800529E4u, func_800529E4),
    GEN(0x80052D64u, MobyGetAnimatedVertex),
    GEN(0x80052F38u, func_80052F38),
    GEN(0x800530C0u, func_800530C0),
    GEN(0x800533D0u, MobyUpdateShadow),
#endif
    GEN(0x8004EBA8u, func_8004EBA8),
    GEN(0x800258F0u, func_800258F0),
    GEN(0x80023AC4u, func_80023AC4),
    GEN(0x8001F798u, func_8001F798),
    GEN(0x80022A2Cu, func_80022A2C),
    GEN(0x80020F34u, func_80020F34),
    {0, 0, 0},
};

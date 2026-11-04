/* Shadows for the hand-written C of decisions D94 (cyclorama_hand.c,
 * moby_lists_hand.c, particle_alloc_hand.c, images_hand.c,
 * moby_helpers_hand.c, credits_hand.c): in host builds with the mips2c
 * units linked (cmake SPYRO_M2C_REF), each call also runs its unit from the
 * same state, and the two must leave the same RAM (but the scratchpad, the
 * asm's spill area and the emulated stack below the call), the same result
 * and the same GTE control registers (but FLAG), as collision_hand.c's
 * (D83) and 42CC4_hand.c's and gamestates_hand.c's (D87). On by default in
 * the oracle replay (SPYRO_REPLAY_HANDSHADOW=0 turns it off), and over whole
 * host runs with SPYRO_SHADOW=hand. Mismatches are printed ("handshadow"
 * lines) and counted in g_hand_shadow_fail.
 *
 * Two ways to run the unit:
 * - in this process (HAND_SHADOW_CALL): the unit runs, its state is kept
 *   aside, RAM and the GTE are put back, the hand C runs and the two are
 *   compared. For functions that act only on RAM and the GTE.
 * - in a forked child (HAND_SHADOW_FORK): the child runs the unit and hands
 *   back its RAM and GTE; the hand C then runs here. For functions that act
 *   outside RAM too (sounds, the CD, the GPU, the vblank clock), which a
 *   second run in this process would repeat.
 * Either way the call leaves the unit's scratchpad and GTE behind, so an
 * oracle replay sample still compares whole with the emulator's (D83). */
#ifndef HAND_SHADOW_H
#define HAND_SHADOW_H

#define HAND_SHADOW (MIPS2C_REPLAY && M2C_REF)

#include <stdint.h>

#if HAND_SHADOW
extern int g_hand_shadow, g_hand_shadow_fail;

/* In this process. Begin: 0 when off or inside another shadowed call. */
int HandShadowBegin(const char *name);
void HandShadowSwap(void);
unsigned int HandShadowEnd(const char *name, unsigned int gen, unsigned int hand);

/* In a child: 0 when off, inside another shadowed call, or the fork
 * failed. Then the hand C runs, then HandShadowForkEnd. */
int HandShadowFork(const char *name, unsigned int (*unit)(), unsigned int a0, unsigned int a1);
void HandShadowForkEnd(const char *name);

/* The calls compared, by function (SPYRO_SHADOW's report at exit). */
#include <stdio.h>
void HandShadowReport(FILE *f);

/* Fuzzing (SPYRO_HAND_FUZZ=N): N variants of a shadowed call, each from the
 * same state through the unit and the hand C, compared, then the state put
 * back. FuzzBegin returns N (0 when off) and saves the state.
 * SPYRO_HAND_ONLY=NAME,... limits both to those functions. */
int HandFuzzBegin(const char *name, unsigned int seed);
unsigned int HandFuzzRand(void);
void HandFuzzRestore(void);

#define HAND_SHADOW_CALL(name, gen_call, hand_call)                            \
  do {                                                                         \
    if (HandShadowBegin(#name)) {                                              \
      unsigned int gen_ = (unsigned int)(gen_call);                            \
      __typeof__(hand_call) hand_;                                             \
      HandShadowSwap();                                                        \
      hand_ = hand_call;                                                       \
      HandShadowEnd(#name, gen_, (unsigned int)(uintptr_t)hand_);              \
      return hand_;                                                            \
    }                                                                          \
  } while (0)
#define HAND_SHADOW_VOID(name, gen_call, hand_call)                            \
  do {                                                                         \
    if (HandShadowBegin(#name)) {                                              \
      (void)(gen_call);                                                        \
      HandShadowSwap();                                                        \
      hand_call;                                                               \
      HandShadowEnd(#name, 0, 0);                                              \
      return;                                                                  \
    }                                                                          \
  } while (0)
#define HAND_SHADOW_FORK(name, a0, a1, hand_call)                              \
  do {                                                                         \
    if (HandShadowFork(#name, name##_gen, (unsigned int)(a0), (unsigned int)(a1))) { \
      hand_call;                                                               \
      HandShadowForkEnd(#name);                                                \
      return;                                                                  \
    }                                                                          \
  } while (0)
/* One fuzz variant, after the caller changed the state. */
#define HAND_FUZZ_ONE(name, gen_call, hand_call)                               \
  do {                                                                         \
    unsigned int gen_;                                                         \
    HandShadowBegin(#name);                                                    \
    gen_ = (unsigned int)(gen_call);                                           \
    HandShadowSwap();                                                          \
    HandShadowEnd("fuzz " #name, gen_, (unsigned int)(uintptr_t)(hand_call));  \
    HandFuzzRestore();                                                         \
  } while (0)
/* The same for a void function: no result to compare. */
#define HAND_FUZZ_VOID(name, gen_call, hand_call)                              \
  do {                                                                         \
    HandShadowBegin(#name);                                                    \
    (void)(gen_call);                                                          \
    HandShadowSwap();                                                          \
    hand_call;                                                                 \
    HandShadowEnd("fuzz " #name, 0, 0);                                        \
    HandFuzzRestore();                                                         \
  } while (0)
#else
#define HAND_SHADOW_CALL(name, gen_call, hand_call) ((void)0)
#define HAND_SHADOW_VOID(name, gen_call, hand_call) ((void)0)
#define HAND_SHADOW_FORK(name, a0, a1, hand_call) ((void)0)
#endif

#endif

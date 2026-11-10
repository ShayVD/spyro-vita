/* The 60 Hz rules' own C (decisions D130; docs/tick60.md): built only into
 * cmake/tick60.cmake's object, with the update path's second compile, so
 * the names below that the set defines (UpdateMoby_LEVEL_10 and the rest,
 * func_800522C0) are the set's 60 Hz copies.
 *
 * In a 60 Hz update:
 *   - the pilot's levels' mobys run the second compile of their overlay's
 *     UpdateMoby: a class judged for 60 Hz (t60_classes.c) every vblank,
 *     any other one every other vblank, as at 30 Hz;
 *   - every other level's mobys, the particles and the HUD run the game's
 *     own code at 30 Hz: on the vblank that starts a 30 Hz tick, with the
 *     vblanks since the last one started as g_DeltaTime. */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"
#include "hud.h"
#include "moby.h"
#include "moby_lists.h"
#include "overlay_pointers.h"
#include "sony_image.h"

#include "tick60_rules.h"

/* the game's 30 Hz pointers of the pilot's levels' UpdateMoby (tick60.c):
 * the names here are this set's copies */
void *tick60_game_update_moby(int pIndex);

void UpdateMoby_LEVEL_10(void), UpdateMoby_LEVEL_11(void), UpdateMoby_LEVEL_12(void);
void UpdateMoby_LEVEL_13(void), UpdateMoby_LEVEL_14(void), UpdateMoby_LEVEL_15(void);
static void (*const k_update_60[])(void) = {UpdateMoby_LEVEL_10, UpdateMoby_LEVEL_11,
                                            UpdateMoby_LEVEL_12, UpdateMoby_LEVEL_13,
                                            UpdateMoby_LEVEL_14, UpdateMoby_LEVEL_15};
#define N_LEVELS_60 (int)(sizeof k_update_60 / sizeof k_update_60[0])

/* this update's g_DeltaTime, for the 60 Hz classes; the lists split by rate
 * (rebuilt every update: scratch, not state) */
static int s_t60_delta;
static Moby *s_t60_list60[1024], *s_t60_list30[1024];

/* A 30 Hz part, on the vblank that starts a tick. */
#define AT_30(call)                                                            \
  do {                                                                         \
    if (g_t60.tick) {                                                          \
      int d_ = g_DeltaTime;                                                    \
      g_DeltaTime = g_t60.tick_delta;                                          \
      call;                                                                    \
      g_DeltaTime = d_;                                                        \
    }                                                                          \
  } while (0)

void t60_update_moby(void) {
  int i, d = g_DeltaTime;
  s_t60_delta = d;
  for (i = 0; i < N_LEVELS_60; i++)
    if ((void *)g_UpdateMoby == tick60_game_update_moby(i)) {
      k_update_60[i]();
      g_DeltaTime = d;
      return;
    }
  AT_30((*g_UpdateMoby)());
}

void t60_update_particles(void) { AT_30(g_UpdateParticle(g_DeltaTime)); }

void t60_hud_tick(void) { AT_30(HudTick()); }

/* UpdateMoby's loop, per moby: its class's rate. 0 skips a 30 Hz class
 * between ticks. The loop sets D_800756C4 from g_DeltaTime after this. */
int t60_moby_begin(Moby *pMoby) {
  if (t60_class_60(pMoby->m_Class)) {
    g_DeltaTime = s_t60_delta;
    return 1;
  }
  if (!g_t60.tick)
    return 0;
  g_DeltaTime = g_t60.tick_delta;
  return 1;
}

/* UpdateMoby's animation step (the original: one step, and a catch-up
 * step at delta 3 or 4). The 60 Hz classes take half a step each vblank
 * (moby_lists_hand.c's Animate: T60_ANIM_LOWER, then T60_ANIM_UPPER; at
 * most one frame a tick, as the original's step); the others the
 * original's step, on the vblank that starts a tick. */
void t60_moby_animate(Moby **pList) {
  Moby **a = s_t60_list60, **b = s_t60_list30, *m;
  while ((m = *pList++)) {
    if (t60_class_60(m->m_Class))
      *a++ = m;
    else
      *b++ = m;
  }
  *a = NULL;
  *b = NULL;
  if (s_t60_delta == 1) {
    func_800522C0(s_t60_list60, g_t60.upper ? T60_ANIM_UPPER : T60_ANIM_LOWER);
  } else { /* late: a whole tick's step, the original's way */
    func_800522C0(s_t60_list60, 0);
    if (s_t60_delta > 2)
      func_800522C0(s_t60_list60, s_t60_delta == 3 ? 0x80000001 : 0x80000000);
  }
  if (g_t60.tick) {
    func_800522C0(s_t60_list30, 0);
    if (g_t60.tick_delta > 2)
      func_800522C0(s_t60_list30, g_t60.tick_delta == 3 ? 0x80000001 : 0x80000000);
  }
}

/* func_8003FE40's move: the vblanks' velocities (in 64ths) >> 6. The
 * original shifts a tick's sum, two vblanks', once; here each vblank is
 * shifted, so the first half's low bits are carried into the second's and
 * a tick moves Spyro exactly what the original's shift of the sum does. */
void t60_spyro_shift(void *pVelocity) {
  int *v = (int *)pVelocity, *c = g_t60.spyro_carry, k;
  for (k = 0; k < 3; k++) {
    if (g_DeltaTime != 1) { /* a late update: a whole tick, as the original */
      v[k] >>= 6;
      c[k] = 0;
    } else if (!g_t60.upper) {
      int x = v[k];
      v[k] = x >> 6;
      c[k] = x - (v[k] << 6);
    } else {
      v[k] = (v[k] + c[k]) >> 6;
      c[k] = 0;
    }
  }
}

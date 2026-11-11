/* The 60 Hz rules' own definitions (decisions D130; docs/tick60.md):
 * forced into every file cmake/tick60.cmake compiles a second time, after
 * port_prefix.h. Only the 60 Hz rules see it. */
#ifndef SPYRO_TICK60_RULES_H
#define SPYRO_TICK60_RULES_H

#include "../tick60.h"
#include "../../compat/tick60_sites.h"

#if !SPYRO_TICK60
#error "tick60_rules.h is the 60 Hz rules' (cmake/tick60.cmake)"
#endif

extern int g_DeltaTime;

/* This vblank's share of an amount per 30 Hz tick: its lower half on a
 * tick's first vblank, the rest on the second, so the two sum to it
 * (floor and ceiling for negative amounts too). A late update (2+
 * vblanks) takes the whole amount, as the original's per-tick code does at
 * any delta. */
static inline int t60_share(int x) {
  if (g_DeltaTime != 1)
    return x;
  return g_t60.upper ? x - (x >> 1) : x >> 1;
}

/* t60_rules.c */
struct Moby;
void t60_update_moby(void);       /* the level's mobys: 60 Hz classes and the rest at 30 Hz */
void t60_update_particles(void);  /* the particles, at 30 Hz */
int t60_moby_begin(struct Moby *pMoby); /* UpdateMoby's loop: 0 skips a 30 Hz class between ticks */
void t60_moby_animate(struct Moby **pList); /* UpdateMoby's animation step, by class rate */
int t60_class_60(int pClass);     /* a class judged for 60 Hz (t60_classes.c) */
void t60_hud_tick(void);          /* the HUD, at 30 Hz */
struct Vector3D;
void t60_spyro_shift(void *pVelocity); /* func_8003FE40's >> 6, its low bits carried */
void t60_spyro_damage_end(void);  /* func_8004A200: a vortex's lift (0x800) lasts a 30 Hz tick */
void t60_spyro_release(void);     /* func_8004A200's end: a script's hold lasts a 30 Hz tick */

/* func_8003BFC0, a moby's frame-counted path step (moby_helpers_hand.c), in
 * the 60 Hz rules: its frame a tick, split over the tick's two vblanks
 * (t60_rules.c, D144). Every call in the set goes to t60_path_step, which
 * moby_helpers.h's declaration declares. */
#define func_8003BFC0 t60_path_step

/* moby_lists_hand.c's animation step (its speedDiv): half a 30 Hz tick's
 * step, the lower or the upper half. The original's step advances at most
 * one frame a tick (a speed of 64 or more gives one frame a tick), so the
 * halves split min(speed, 64). */
#define T60_ANIM_LOWER 0x20
#define T60_ANIM_UPPER 0x40
static inline unsigned int t60_anim_step(unsigned int pPer, int pSpeedDiv) {
  if (pSpeedDiv & (T60_ANIM_LOWER | T60_ANIM_UPPER)) {
    if (pPer > 64)
      pPer = 64;
    return pSpeedDiv & T60_ANIM_UPPER ? pPer - (pPer >> 1) : pPer >> 1;
  }
  return pPer >> (pSpeedDiv & 31);
}

#endif

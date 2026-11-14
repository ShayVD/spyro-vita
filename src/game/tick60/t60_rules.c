/* The 60 Hz rules' own C (decisions D130; docs/tick60.md): built only into
 * cmake/tick60.cmake's object, with the update path's second compile, so
 * the names below that the set defines (UpdateMoby_LEVEL_10 and the rest,
 * func_800522C0) are the set's 60 Hz copies.
 *
 * In a 60 Hz update:
 *   - every level's mobys run the second compile of their overlay's
 *     UpdateMoby (t60_levels.h). A class judged for 60 Hz (t60_classes.c)
 *     runs every vblank (in every level since D144); any other class
 *     every other vblank, as at 30 Hz;
 *   - the particles and the HUD run the game's own code at 30 Hz: on the
 *     vblank that starts a 30 Hz tick, with the vblanks since the last one
 *     started as g_DeltaTime. */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"
#include "gamepad.h"
#include "hud.h"
#include "moby.h"
#include "moby_helpers.h"
#include "moby_lists.h"
#include "overlay_pointers.h"
#include "sony_image.h"
#include "spyro.h"

#include "tick60_rules.h"

/* the game's 30 Hz pointers of every level's UpdateMoby (tick60.c):
 * the names here are this set's copies */
void *tick60_game_update_moby(int pIndex);

#define T60_LEVEL(name) void UpdateMoby_##name(void);
#include "t60_levels.h"
#undef T60_LEVEL
#define T60_LEVEL(name) UpdateMoby_##name,
static void (*const k_update_60[])(void) = {
#include "t60_levels.h"
};
#undef T60_LEVEL
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
  if (g_t60.tick)
    g_t60.path_n = 0; /* a new tick: no path step taken yet (t60_path_step) */
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
static void path_rest(Moby *pMoby);

int t60_moby_begin(Moby *pMoby) {
  if (t60_class_60(pMoby->m_Class)) {
    g_DeltaTime = s_t60_delta;
    if (g_t60.upper && g_t60.path_n)
      path_rest(pMoby);
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

/* func_8004A200's end (0015): the original lets go of a script's hold on
 * Spyro (m_ControlFlags bit 31) after each of his updates, and clears his
 * m_DamageFlags; the moby whose script it is sets them again in its next
 * update, before his. A moby that runs at 30 Hz sets them once a tick, so
 * in the 60 Hz rules a hold lasts the tick's two vblanks (D141):
 *   - bit 31 is let go on the vblank that ends a tick (or after a late
 *     update, which is a whole one). Otherwise the second vblank found it
 *     clear, func_8003FE40 zeroed the flags and Spyro was the player's
 *     again for half of every tick: a portal's path (class 398) never
 *     carried him in, and its camera left its path mode and started over;
 *   - of the damage flags, a vortex's or whirlwind's lift (0x800, classes
 *     9 and 300) is kept for the second half: HandleSpyroDamage drops him
 *     from state 17 to 15 when it is missing. The one-shot hits are taken
 *     once a tick, as before.
 * A tick cut short by a gamestate change lets go in PortGamestateUpdate. */
void t60_spyro_damage_end(void) {
  if (T60_TICK_ENDS)
    g_Spyro.m_DamageFlags = 0;
  else
    g_Spyro.m_DamageFlags &= 0x800;
}

void t60_spyro_release(void) {
  if (T60_TICK_ENDS) {
    g_Spyro.m_ControlFlags &= 0x7FFFFFFF;
    g_t60.spyro_held = 0;
  } else {
    g_t60.spyro_held = 1;
  }
}

/* func_8003BFC0 (moby_helpers_hand.c), the frame-counted path step: one
 * frame a call, the segment to the next node taking pSegmentDuration
 * frames, each a share of the way left, curved from the last segment's
 * direction in its first frames. At 60 Hz a frame still is a 30 Hz tick's
 * (units stay the original's): on a tick's first vblank the step is taken
 * whole (its frame counter, the node it reaches, its return, the moby's
 * heading), the moby moved by half of it, and the rest kept; the moby's
 * update on the second vblank moves it by the rest (t60_moby_begin, before
 * its class's code), and a call then returns 0 and moves nothing more. So
 * at the end of every tick the moby is where the original's tick puts it,
 * whether or not its class calls the step again in the second half. A
 * class at 30 Hz takes the original's step; a late update takes the
 * tick's step whole, unless the tick's first half took it. A save state
 * taken between a tick's halves drops the rest (half a frame's move). */
#undef func_8003BFC0
int func_8003BFC0(Moby *pMoby, PathData *pPath, Vector3D *pPreviousMovement, int *pSegmentFrame,
                  int pSegmentDuration, int pFlags);

/* one step a tick a moby, even if its class moves it onto another path in
 * the tick's first half */
static int path_find(Moby *pMoby) {
  int i;
  for (i = 0; i < g_t60.path_n; i++)
    if (g_t60.path[i].moby == pMoby)
      return i;
  return -1;
}

static void path_rest(Moby *pMoby) {
  int i;
  for (i = 0; i < g_t60.path_n; i++)
    if (g_t60.path[i].moby == pMoby) {
      int *r = g_t60.path[i].rest;
      pMoby->m_Position.x += r[0];
      pMoby->m_Position.y += r[1];
      pMoby->m_Position.z += r[2];
      r[0] = r[1] = r[2] = 0;
    }
}

int t60_path_step(Moby *pMoby, PathData *pPath, Vector3D *pPreviousMovement, int *pSegmentFrame,
                  int pSegmentDuration, int pFlags) {
  Vector3D from;
  int r, k, *rest;
  if (!t60_class_60(pMoby->m_Class)) /* a 30 Hz class: the original's step */
    return func_8003BFC0(pMoby, pPath, pPreviousMovement, pSegmentFrame, pSegmentDuration, pFlags);
  if (path_find(pMoby) >= 0)
    return 0; /* this tick's step is taken (its rest moved in t60_moby_begin) */
  from = pMoby->m_Position;
  r = func_8003BFC0(pMoby, pPath, pPreviousMovement, pSegmentFrame, pSegmentDuration, pFlags);
  if (g_t60.path_n >= (int)(sizeof g_t60.path / sizeof g_t60.path[0]))
    return r; /* the table is full: the step whole */
  k = g_t60.path_n++;
  g_t60.path[k].moby = pMoby;
  rest = g_t60.path[k].rest;
  rest[0] = rest[1] = rest[2] = 0;
  if (t60_first_half()) {
    /* the tick's first half: move by half the step, keep the rest */
    rest[0] = (pMoby->m_Position.x - from.x) - ((pMoby->m_Position.x - from.x) >> 1);
    rest[1] = (pMoby->m_Position.y - from.y) - ((pMoby->m_Position.y - from.y) >> 1);
    rest[2] = (pMoby->m_Position.z - from.z) - ((pMoby->m_Position.z - from.z) >> 1);
    pMoby->m_Position.x -= rest[0];
    pMoby->m_Position.y -= rest[1];
    pMoby->m_Position.z -= rest[2];
  }
  /* else: first taken in the second half, or a late update: whole, once */
  return r;
}

/* The wall contact (m_againstWall, m_wallAgainstSpyro; D160). The original
 * clears it at func_8003FE40's start, its one collision pass over the
 * tick's move sets it, and the next tick's two physics substeps both read
 * it. At 60 Hz each vblank has its own pass: a tick's first vblank starts
 * the contact fresh and, at its end, keeps what it found for the second
 * vblank and puts the last tick's back, which the second vblank's physics
 * then reads as the original's second substep did; the second vblank goes
 * on from the first's contact and leaves the tick's for the next. A late
 * update is a whole tick: the original's clear. */
void t60_wall_begin(void) {
  if (g_DeltaTime == 1 && g_t60.upper) { /* the tick's second vblank */
    g_Spyro.m_againstWall = g_t60.wall_acc;
    g_Spyro.m_wallAgainstSpyro.x = g_t60.wall_acc_vec[0];
    g_Spyro.m_wallAgainstSpyro.y = g_t60.wall_acc_vec[1];
    g_Spyro.m_wallAgainstSpyro.z = g_t60.wall_acc_vec[2];
    return;
  }
  if (t60_first_half()) {
    g_t60.wall_pub = g_Spyro.m_againstWall;
    g_t60.wall_pub_vec[0] = g_Spyro.m_wallAgainstSpyro.x;
    g_t60.wall_pub_vec[1] = g_Spyro.m_wallAgainstSpyro.y;
    g_t60.wall_pub_vec[2] = g_Spyro.m_wallAgainstSpyro.z;
  }
  g_Spyro.m_againstWall = 0;
  g_Spyro.m_wallAgainstSpyro.x = g_Spyro.m_wallAgainstSpyro.y = g_Spyro.m_wallAgainstSpyro.z = 0;
}

void t60_wall_end(void) {
  if (!t60_first_half())
    return;
  g_t60.wall_acc = g_Spyro.m_againstWall;
  g_t60.wall_acc_vec[0] = g_Spyro.m_wallAgainstSpyro.x;
  g_t60.wall_acc_vec[1] = g_Spyro.m_wallAgainstSpyro.y;
  g_t60.wall_acc_vec[2] = g_Spyro.m_wallAgainstSpyro.z;
  g_Spyro.m_againstWall = g_t60.wall_pub;
  g_Spyro.m_wallAgainstSpyro.x = g_t60.wall_pub_vec[0];
  g_Spyro.m_wallAgainstSpyro.y = g_t60.wall_pub_vec[1];
  g_Spyro.m_wallAgainstSpyro.z = g_t60.wall_pub_vec[2];
}

/* Spyro's state transitions once a tick (T60_SPYRO_TRANSITIONS, D160). The
 * original's PadVSync ORs the presses and releases of a tick's two vblanks
 * into g_Pad.m_Down and m_Released (g_UnprocessedFrames != 0), so the
 * transitions at the tick's end see a press on either. At 60 Hz each vblank
 * is sampled on its own; tick60.c keeps the first vblank's, and they are
 * ORed in for the transitions only, then the live values put back. */
void t60_pad_tick_begin(void) {
  g_t60.pad_down_saved = g_Pad.m_Down;
  g_t60.pad_released_saved = g_Pad.m_Released;
  g_Pad.m_Down |= g_t60.pad_down_acc;
  g_Pad.m_Released |= g_t60.pad_released_acc;
}

void t60_pad_tick_end(void) {
  g_Pad.m_Down = g_t60.pad_down_saved;
  g_Pad.m_Released = g_t60.pad_released_saved;
}

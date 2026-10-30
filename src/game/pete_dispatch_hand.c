/* Spyro's per-frame collision dispatch (asm/nonmatchings/pete/func_80047B60),
 * written by hand from the asm so it reads as C (docs/porting-plan.md §7,
 * M2). After the physics ticks (func_80043FE4) have set Spyro's velocity,
 * this moves him against the world and runs the floor, wall and slope
 * collision his state needs, plus a few per-state extras: the walking speed
 * on slopes, dust and vapour particles, the flight ceiling, drowning.
 * Replaces its mips2c unit (cmake/mips2c.cmake M2C_HAND); checked against
 * PCSX-Redux with the oracle replay (docs/translation-log.md).
 * M2C_func_80047B60=1 compiles it out for the mips2c unit (cmake
 * SPYRO_M2C_FUNCS, A/B tests). */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "camera.h"
#include "collision.h"
#include "gamepad.h"
#include "math.h"
#include "overlay_pointers.h"
#include "rand.h"
#include "spyro.h"
#include "variables.h"
#include "vector.h"

#if !M2C_func_80047B60
/* pete.c */
void func_8003E628(void);
void func_8003E1AC(void);
void UpdateSlopeFloorCollision(void);
void AdjustAirCollision(void);
void CheckWallCollision(void);
void ApplySlopeGravity(void);

/* Moves Spyro by his velocity, against the world (asm/nonmatchings/pete,
 * not matched). Its asm takes no arguments (it sets a0-a3 before reading
 * them), and nothing here reads a result from it. */
void func_8003FE40(void);

/* Walking speed limits on a slope: along it (uphill, downhill) and across
 * it, on flat ground and at the steepest slope (m_slopeAngle 22). */
extern int D_8006C5B8; /* Flat, along (and the limit off slopes): 0x1200 */
extern int D_8006C5BC; /* Flat, across: 0x1200 */
extern int D_8006C5C0; /* Flat, facing downhill: 0x1200 */
extern int D_8006C5C4; /* Steepest, along: 0x1440 */
extern int D_8006C5C8; /* Steepest, across: 0x1200 */
extern int D_8006C5CC; /* Steepest, facing downhill: 0x3C0 */

static inline int AngleDelta(int delta) {
  delta &= 0xFFF;
  if (delta > 0x800)
    delta -= 0x1000;
  return delta;
}

static inline int Lerp(int pFrom, int pTo, int pT) { return pFrom + (pT * (pTo - pFrom) >> 12); }

/* Running states: the speed limit (unk_0x144) on a slope, from its
 * steepness and the angle between Spyro's heading and the slope's direction
 * (m_floorPositonOnSlope's x and y here): an ellipse with one radius along
 * the slope and the other across. */
static int SlopeWalkSpeed(void) {
  int cross, t, along, across, x, y;

  if (g_Spyro.m_slopeAngle == 0)
    return D_8006C5B8;

  cross = AngleDelta(g_Spyro.m_Physics.m_SpeedAngle.m_RotZ -
                     Atan2(g_Spyro.m_floorPositonOnSlope.x, g_Spyro.m_floorPositonOnSlope.y, 1));
  t = (g_Spyro.m_slopeAngle << 12) / 22;
  if (cross < 0)
    cross = -cross;
  if (cross > 0x400) {
    along = Lerp(D_8006C5C0, D_8006C5CC, t);
    cross = 0x800 - cross;
  } else {
    along = Lerp(D_8006C5B8, D_8006C5C4, t);
  }
  across = Lerp(D_8006C5BC, D_8006C5C8, t);
  x = along * Cos(cross) >> 12;
  y = across * Sin(cross) >> 12;
  return func_80017A38(x * x + y * y);
}

/* Particle 9, the flying vapour trail, around pPos, drifting with Spyro's
 * true velocity / 64. */
static void SpawnTrail(Vector3D *pPos) {
  Vector3D drift;

  pPos->x += (rand() & 0xFE) - 0x7F;
  pPos->y += (rand() & 0xFE) - 0x7F;
  VecCopy(&drift, &g_Spyro.m_Physics.m_TrueVelocity);
  VecShiftRight(&drift, 6);
  g_SpawnParticle(1, 9, pPos, (int)&drift);
}

/* m_highestFlightPoint, the height flying speed is reckoned from, follows
 * the ceiling D_800758C0 by at most 0x40 up and 0x10 down a frame. */
static void FollowFlightCeiling(void) {
  int step = D_800758C0 - g_Spyro.m_highestFlightPoint;

  if (step > 0x40)
    step = 0x40;
  if (step < -0x10)
    step = -0x10;
  g_Spyro.m_highestFlightPoint += step;
}

/* The flying ability outside the flight levels (Gnasty's Loot): follow the
 * ceiling once there is one; standing above it raises it, in Gnasty's Loot
 * to at most D_80075678. */
static void UpdateFlightCeiling(void) {
  if (D_800758C0 != 0)
    FollowFlightCeiling();
  if (D_800758C0 < g_Spyro.m_highestFlightPoint && g_Spyro.m_floorIdleTime != 0)
    D_800758C0 = g_Spyro.m_highestFlightPoint;
  if (g_LevelId == 64 && D_800758C0 > D_80075678)
    D_800758C0 = D_80075678;
}

/* Air and flying states: without the flying ability the highest point is
 * where Spyro is before he moves. */
static void ResetHighestPointUnlessFlying(void) {
  if (!g_Spyro.m_flyingAbility)
    g_Spyro.m_highestFlightPoint = g_Spyro.m_Position.z;
}

static void CheckWallsUnlessAgainstOne(void) {
  if (!g_Spyro.m_againstWall)
    CheckWallCollision();
}

/* Drowning (state 29): once started, sink 8 a frame, to 0x96 below the
 * water while alive, to 0x240 when dead (pete.c restarts the level there).
 * m_damagingFloorIndex holds the water's height (special_surfaces.c). */
static void Sink(void) {
  int offset = g_Spyro.m_drowningOffset;

  if (offset == 0)
    return;
  offset += 8;
  g_Spyro.m_drowningOffset = offset;
  g_Spyro.m_Position.z = g_Spyro.m_damagingFloorIndex + 0x164;
  if (g_Spyro.m_health < 0) {
    if (offset > 0x240)
      g_Spyro.m_drowningOffset = 0x240;
  } else if (offset > 0x96) {
    g_Spyro.m_drowningOffset = 0x96;
  }
}

/// @brief Per-frame state dispatch for Spyro: after the physics ticks, moves
/// him against the world and runs the floor, wall and slope collision his
/// state needs, with each state's extras.
/// @note asm/nonmatchings/pete/func_80047B60.s, by hand.
void func_80047B60(void) {
  Vector3D v;

  g_Camera.m_OnMovingPlatform = 0;

  switch (g_Spyro.m_State) {
  case 0: case 12: case 13: case 18: case 35: case 36: case 37: case 38:
  case 39: case 40: case 41: case 42: case 43:
    /* Standing still and holding triangle looks around. */
    if (g_Pad.m_NoMovementButtonPressed && (g_Pad.m_Held & PAD_TRIANGLE))
      g_Camera.unk_0xC0 = 0x80000009;
    func_8003E628();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    if (g_Spyro.m_ControlFlags == 0)
      AdjustAirCollision();
    break;

  case 1: case 2: case 3: case 21:
    /* Walking and running; state 3 kicks up dust. */
    func_8003E628();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    func_8003E1AC();
    g_Spyro.m_Physics.unk_0x144 = SlopeWalkSpeed();
    if (g_Spyro.m_State == 3) {
      VecCopy(&v, &g_Spyro.m_Physics.m_TrueVelocity);
      VecShiftRight(&v, 6);
      g_SpawnParticle(1, 0x21, &v, 0);
    }
    break;

  case 4:
    /* Turning on the spot: dust. */
    func_8003E628();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    func_8003E1AC();
    v.x = 0x20;
    v.y = 0;
    v.z = 0;
    VecRotateByMatrix(&g_Spyro.m_RotationMatrix, &v, &v);
    g_SpawnParticle(1, 0x21, &v, 0);
    break;

  case 7: {
    /* Hurt: flash white and grey over the animation's first frames. */
    int frame;

    func_8003FE40();
    UpdateSlopeFloorCollision();
    func_8003E1AC();
    frame = g_Spyro.m_nextBodyAnimationFrame;
    if (frame < 10) {
      int shade = (frame & 1) ? 0xFF : 0x40;
      g_Spyro.m_colorFilter.m_interpolation = 0xFF;
      g_Spyro.m_colorFilter.m_blue = shade;
      g_Spyro.m_colorFilter.m_green = shade;
      g_Spyro.m_colorFilter.m_red = shade;
    } else {
      g_Spyro.m_colorFilter.m_interpolation = 0;
    }
    break;
  }

  case 25: case 27:
    func_8003FE40();
    UpdateSlopeFloorCollision();
    break;

  case 5: case 17:
    func_8003FE40();
    UpdateSlopeFloorCollision();
    CheckWallsUnlessAgainstOne();
    break;

  case 6: case 16:
    /* In the air; afterwards he accelerates by what collision left of his
     * velocity. */
    ResetHighestPointUnlessFlying();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    if (g_Spyro.m_flyingAbility) {
      if (g_IsFlightLevel)
        FollowFlightCeiling();
      else
        UpdateFlightCeiling();
    }
    CheckWallsUnlessAgainstOne();
    VecCopy(&g_Spyro.m_Physics.m_Acceleration, &g_Spyro.m_Physics.m_TrueVelocity);
    break;

  case 23:
    ResetHighestPointUnlessFlying();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    if (g_Spyro.m_flyingAbility) {
      if (g_IsFlightLevel)
        FollowFlightCeiling();
      else
        UpdateFlightCeiling();
    }
    CheckWallsUnlessAgainstOne();
    break;

  case 32: case 33: case 34:
    /* Flying: in a flight level's m_walkingState 3, touching anything runs
     * the level's crash code (D_80075694). */
    ResetHighestPointUnlessFlying();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    if (g_Spyro.m_flyingAbility) {
      if (g_IsFlightLevel) {
        FollowFlightCeiling();
        if (g_Spyro.m_walkingState == 3 && ColSphere(&g_Spyro.m_Position, 0x164, 0x164))
          (*D_80075694)();
      } else {
        UpdateFlightCeiling();
      }
    }
    CheckWallsUnlessAgainstOne();
    break;

  case 15:
    /* Flying through a portal or into the level. Flying to the portal's
     * end (phase 9) with surface checks off, the floor is looked for from
     * high up. */
    ResetHighestPointUnlessFlying();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    if (g_Spyro.m_flyingAbility)
      UpdateFlightCeiling();
    CheckWallsUnlessAgainstOne();
    if ((g_Spyro.m_ControlFlags & CTRL_SKIP_SURFACE_CHECK) && g_Spyro.m_walkingState == 9)
      g_Spyro.m_surfaceBelowSpyro = ColFindFloor(&g_Spyro.m_Position, 0x10000);
    break;

  case 8:
    func_8003E628();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    AdjustAirCollision();
    break;

  case 11:
    /* Charging. */
    func_8003FE40();
    UpdateSlopeFloorCollision();
    func_8003E1AC();
    ApplySlopeGravity();
    AdjustAirCollision();
    CheckWallsUnlessAgainstOne();
    break;

  case 19:
    /* Sliding: the momentum is what collision left of the velocity. */
    func_8003FE40();
    VecCopy(&g_Spyro.m_Physics.unk_0xe8, &g_Spyro.m_Physics.m_TrueVelocity);
    UpdateSlopeFloorCollision();
    func_8003E1AC();
    ApplySlopeGravity();
    break;

  case 20: case 24:
    /* Flying ability and gliding: a trail while diving (bit 0x40). */
    func_8003FE40();
    UpdateSlopeFloorCollision();
    if (g_Spyro.m_walkingState & 0x40) {
      VecCopy(&v, &g_Spyro.m_Position);
      SpawnTrail(&v);
    }
    break;

  case 9: case 10: case 14: case 22: case 26: case 28: case 30: case 31:
    func_8003FE40();
    UpdateSlopeFloorCollision();
    func_8003E1AC();
    break;

  case 29:
    Sink();
    func_8003FE40();
    UpdateSlopeFloorCollision();
    AdjustAirCollision();
    break;

  case 44:
    /* Flight levels: the trail from under his belly. */
    func_8003FE40();
    UpdateSlopeFloorCollision();
    func_8003E1AC();
    ApplySlopeGravity();
    VecCopy(&v, &g_Spyro.m_Position);
    v.z -= 0x164;
    SpawnTrail(&v);
    break;
  }

  /* Drowning: Spyro is drawn the sinking offset under the water line, and
   * his floor is the water's surface, or what's below him. */
  if (g_Spyro.m_State == 29) {
    g_Spyro.m_Position.z -= g_Spyro.m_drowningOffset;
    if (g_Spyro.m_surfaceBelowSpyro != 0) {
      g_Spyro.m_FloorDistance = g_Spyro.m_surfaceBelowSpyro;
    } else {
      VecCopy(&v, &g_Spyro.m_Position);
      v.z += 0x800;
      g_Spyro.m_FloorDistance = ColFindFloor(&v, 0xC00);
    }
  } else {
    g_Spyro.m_FloorDistance = 0;
  }
}
#endif

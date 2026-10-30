/* Spyro's state transitions (pete func_80041670), written by hand from the
 * asm so it reads as C (docs/porting-plan.md §7, M2). It replaces the mips2c
 * unit (cmake/mips2c.cmake M2C_HAND) and is checked against PCSX-Redux with
 * the oracle replay (docs/translation-log.md). M2C_func_80041670=1 compiles
 * it out for the mips2c unit (cmake SPYRO_M2C_FUNCS, A/B tests).
 *
 * Kept apart from pete_hand.c (the physics, func_80043FE4) because it does
 * a different job: once a frame, after the physics and before the
 * animations (func_8004A200), it looks at the pad, the ground and the
 * damage flags and picks Spyro's next state with func_8003EA68. */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "camera.h"
#include "gamepad.h"
#include "hand_structs.h"
#include "loaders.h"
#include "math.h"
#include "overlay_pointers.h"
#include "spyro.h"
#include "variables.h"
#include "vector.h"

#if !M2C_func_80041670

/* pete.c */
int HandleSpyroDamage(int pFlags);
int func_80041270(void);
void CycleSpyroIdleAnimation(void);
void func_8003D978(void);
void func_8002C85C(void);

extern struct {
  u_char m_StartFrame, m_EndFrame;
  u_char m_TransitionLastFrame, m_FrameRate;
} spyro_AnimationDetails[46];                  // pete.c
extern u_char spyro_StateDefaultAnimation[48]; // pete.c
extern int D_8006E9A4[]; // Per-level height under which Spyro is lost (by g_LevelIndex)
extern int D_800756B4;   // Jump held without the button (cutscenes)
extern int D_80075728;   // Surface Spyro sank into (special_surfaces.c)
extern int D_8006EA40[]; // Moby class spawned when he sinks, by D_80075728

#define NONE (-1) /* No state change */

/* HandleSpyroDamage's flags: everything but the bonk bits (1, 2) that
 * func_80041270 handles; the same without charge damage (0x400); and the
 * supercharge's (charge damage and knockback only). */
#define DAMAGE_ALL 0xFFF9
#define DAMAGE_NO_CHARGE 0xFBF9
#define DAMAGE_SUPERCHARGE 0x8400

#define STEEP_SLOPE 23 /* m_slopeAngle Spyro can't stand on */

/* A jump out of a charge or supercharge: one kick of upward acceleration. */
#define JUMP_BOOST 0xDC0

#define ph (&g_Spyro.m_Physics)

/* The camera's scripted look-around (0x80000009) keeps Spyro standing. */
static int CameraHoldsSpyro(void) {
  return (u_int)g_Camera.unk_0xC0 == 0x80000009u;
}

static int IsFalling(void) {
  return g_Spyro.m_airTime >= 4 || g_Spyro.m_slopeAngle >= STEEP_SLOPE;
}

/* What the standing and running states check first: jump, fall, charge
 * (held), sidestep (pressed). */
static int GroundAction(void) {
  int down = g_Pad.m_Down;

  if (down & PAD_CROSS)
    return 5;
  if (IsFalling())
    return 6;
  if (g_Pad.m_Held & PAD_SQUARE)
    return 11;
  if (down & PAD_L1)
    return 9;
  if (down & PAD_R1)
    return 10;
  return NONE;
}

/* Walk (1), trot (21) or run (2), by speed. */
static int MovingState(void) {
  int speed = ph->m_SpeedAngle.m_Speed;

  if (speed > 0xF00)
    return 2;
  if (speed > 0x780)
    return 21;
  return 1;
}

/* Touching down: on in the gait for the speed if there's a target speed,
 * else stopping (3), or standing (0, dead stop) below 0x780. */
static int LandingState(void) {
  if (ph->m_TargetSpeedAngle.m_Speed > 0)
    return MovingState();
  if (ph->m_SpeedAngle.m_Speed < 0x780) {
    func_8003DFA4();
    return 0;
  }
  return 3;
}

/* A glide: flight levels and Gnasty's Loot (64) have their own (32). */
static int GlideState(void) {
  return g_IsFlightLevel || g_LevelId == 64 ? 32 : 15;
}

/* Fast, but not getting anywhere: running into something. */
static int IsBlocked(void) {
  int speed = ph->m_SpeedAngle.m_Speed;

  return speed > 0xC80 && ph->m_TrueSpeed * 2 < speed;
}

/* Dot products with the floor's normal (m_floorPositonOnSlope); the asm's
 * mult keeps the low 32 bits. */
static int FloorDot2(Vector3D *pVec) {
  Vector3D *n = &g_Spyro.m_floorPositonOnSlope;

  return (int)((u_int)n->x * (u_int)pVec->x + (u_int)n->y * (u_int)pVec->y);
}

static int FloorDot3(Vector3D *pVec) {
  return (int)((u_int)FloorDot2(pVec) + (u_int)g_Spyro.m_floorPositonOnSlope.z * (u_int)pVec->z);
}

/* On a steep slope, accelerating into it: knocked back (12). */
static int PushingIntoSlope(void) {
  return g_Spyro.m_slopeAngle >= STEEP_SLOPE && !g_HasLevelTransition && FloorDot2(&ph->m_Acceleration) < 0;
}

/* Out of a hurt state after pTicks: dying (30), or back to standing or
 * falling with the pad locked for 12 frames. */
static void EndHurt(int pTicks) {
  if (g_Spyro.m_idleTimer < pTicks)
    return;
  if (g_Spyro.m_health < 0) {
    func_8003EA68(30);
    return;
  }
  func_8003EA68(g_Spyro.m_airTime != 0 ? 6 : 0);
  if (g_Spyro.m_noGamepadUpdateFrames < 12)
    g_Spyro.m_noGamepadUpdateFrames = 12;
}

/* Falling for ten seconds: in a flight level the level's own handler, else
 * a free life and the death respawn (which takes it again). */
static void FellTooLong(void) {
  if (g_Spyro.m_idleTimer <= 300)
    return;
  if (g_IsFlightLevel) {
    (*D_80075694)();
    (*g_UpdateMoby)();
  } else {
    g_SpyroLifeCount++;
    func_8002C85C();
  }
}

/* Four puffs of dust, a quarter turn apart around Spyro's heading. */
static void SpawnLandingDust(void) {
  Vector3D dir;
  u_char angle = g_Spyro.m_bodyRotation.z;
  int i;

  dir.z = 0;
  for (i = 0; i < 4; i++) {
    dir.x = COSINE_8(angle) >> 7;
    dir.y = SINE_8(angle) >> 7;
    g_SpawnParticle(1, 0x21, &dir, 0);
    angle += 0x40;
  }
}

/* Gliding with the flying ability, m_walkingState 0: too far above the
 * highest point falls (6); R1 climbs (1), L1 dives (2) from this height. */
static void StartClimbOrDive(void) {
  int z = g_Spyro.m_Position.z;
  int held;

  if (z - g_Spyro.m_highestFlightPoint > 0x200) {
    func_8003EA68(6);
    return;
  }
  held = g_PadFrame->m_Held;
  if (held & PAD_R1) {
    g_Spyro.m_walkingState = 1;
  } else if (held & PAD_L1) {
    g_Spyro.m_walkingState = 2;
    D_80075700 = z;
  }
}

/* States 0 (standing) and 13 (teetering on an edge). */
static int FromStanding(void) {
  int next;

  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if ((next = GroundAction()) != NONE)
    return next;
  if (g_Spyro.m_State == 13) {
    if (CameraHoldsSpyro())
      return 0;
    if (!g_Pad.m_NoMovementButtonPressed)
      return 1;
    if (g_Spyro.m_noGamepadUpdateFrames)
      return 0;
    if (g_Spyro.m_idleTimer < 31)
      return NONE;
    return g_Spyro.m_onEdge ? NONE : 0;
  }

  /* Pushed along (a hit, a moving floor): stopping (3), phase 7. */
  if (ph->m_TrueSpeed > 0x400) {
    func_8003EA68(3);
    g_Spyro.m_walkingState = 7;
    return NONE;
  }
  if (CameraHoldsSpyro())
    return NONE;
  if (!g_Pad.m_NoMovementButtonPressed)
    return 1;
  if (g_Spyro.m_noGamepadUpdateFrames)
    return NONE;
  if (g_Spyro.m_onEdge)
    return 13;
  if (g_Spyro.m_idleTimer >= D_80075788 && g_Spyro.unk_0x198 != 1)
    CycleSpyroIdleAnimation();
  return NONE;
}

/* State 1: walking. */
static int FromWalking(void) {
  int next;

  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if ((next = GroundAction()) != NONE)
    return next;
  if (ph->m_SpeedAngle.m_Speed == 0 && ph->m_TargetSpeedAngle.m_Speed == 0 && g_Spyro.m_idleTimer >= 16)
    return 0;
  next = MovingState();
  return next == 1 ? NONE : next;
}

/* State 2: running. A sharp turn (over 0x500) turns on the spot (4),
 * unless the stick is pushed sideways only (stopping, 3). */
static int FromRunning(void) {
  int next, speed;

  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if ((next = GroundAction()) != NONE)
    return next;
  if (ph->m_TargetSpeedAngle.m_Speed == 0)
    return 3;
  if (func_80017928(ph->m_TargetSpeedAngle.m_RotZ, ph->m_SpeedAngle.m_RotZ) > 0x500) {
    if (g_Pad.m_LeftStickMoved) {
      int y = g_PadFrame->m_Sticks.m_LeftY - 0x7F;

      if ((y < 0 ? -y : y) <= 0x30)
        return 3;
    }
    return 4;
  }
  /* (The asm tests the target speed for 0 again here, which can't be.) */
  speed = ph->m_SpeedAngle.m_Speed;
  if (speed < 0x640)
    return 1;
  if (speed < 0xC80)
    return 21;
  return NONE;
}

/* State 3: stopping. Stands once still; a target speed sets off again (a
 * sharp turn early on turns on the spot). */
static int FromStopping(void) {
  int next, speed, target;

  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if ((next = GroundAction()) != NONE)
    return next;
  speed = ph->m_SpeedAngle.m_Speed;
  target = ph->m_TargetSpeedAngle.m_Speed;
  if (speed == 0 && target == 0 && ph->m_TrueSpeed < 0x100)
    return 0;
  if (target <= 0)
    return NONE;
  if (g_Spyro.m_idleTimer < 18 &&
      func_80017928(ph->m_TargetSpeedAngle.m_RotZ, ph->m_SpeedAngle.m_RotZ) > 0x500)
    return 4;
  return speed > 0 ? MovingState() : NONE;
}

/* State 4: turning on the spot. */
static int FromTurning(void) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if (IsFalling())
    return 6;
  if (g_Spyro.m_idleTimer < 37)
    return NONE;
  if (g_Pad.m_NoMovementButtonPressed)
    return 0;
  ph->m_SpeedAngle.m_Speed = 0xA00;
  return 1;
}

/* State 5: jumping. m_walkingState becomes 1 once falling, 2 when the
 * button is let go early (a short jump). Cross again sets m_isGliding:
 * glide from the top. */
static int FromJumping(void) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if (!g_Spyro.m_onSlope && (g_Pad.m_Down & PAD_CROSS))
    g_Spyro.m_isGliding = 1;
  if (ph->m_TrueVelocity.z < 0)
    g_Spyro.m_walkingState = 1;
  if (g_Spyro.m_walkingState == 0 && !(g_Pad.m_Held & PAD_CROSS) && !D_800756B4)
    g_Spyro.m_walkingState = 2;

  if (g_Spyro.m_airTime == 0 && g_Spyro.m_idleTimer >= 16) {
    if (g_Spyro.m_slopeAngle >= STEEP_SLOPE)
      return 19;
    if (FloorDot3(&ph->m_Acceleration) >= 0)
      return NONE;
    return LandingState();
  }
  if (g_Pad.m_Held & PAD_SQUARE) {
    func_8003EA68(20);
    ph->m_SpeedAngle.m_Speed = 0x1F80;
    return NONE;
  }
  if (!g_Spyro.m_onSlope && g_Spyro.m_walkingState == 1 && g_Spyro.m_isGliding)
    return GlideState();
  if (ph->m_TrueVelocity.z < -0x1900 || g_Spyro.m_idleTimer > 120)
    return 6;
  return NONE;
}

/* State 6: in the air. Cross glides (just off a ledge, unk_0x15c: jumps);
 * on a slope only after a while. Landing raises dust. */
static int FromAirborne(void) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if (g_Pad.m_Down & PAD_CROSS) {
    if (g_Spyro.unk_0x15c != 0 && g_Spyro.m_idleTimer < 4)
      return 5;
    if (g_Spyro.m_walkingState == 0) {
      int next = GlideState();

      if (!g_Spyro.m_onSlope || g_Spyro.m_idleTimer >= (next == 32 ? 16 : 31))
        return next;
      return NONE;
    }
  }
  if (g_Spyro.m_Position.z < D_8006E9A4[g_LevelIndex] || g_Spyro.m_airTime != 0) {
    FellTooLong();
    return NONE;
  }
  if (g_Spyro.m_slopeAngle >= STEEP_SLOPE)
    return g_Spyro.m_idleTimer < 9 ? NONE : 19;
  func_8003EA68(LandingState());
  SpawnLandingDust();
  return NONE;
}

/* State 8: heading reset to the target, pitch and roll mirrored, then on
 * to walking (at speed 0x15) or standing. Damage (not charge) mirrors
 * earlier. */
static void MirrorPitchAndRoll(void) {
  ph->m_SpeedAngle.m_RotZ = ph->m_TargetSpeedAngle.m_RotZ;
  ph->m_SpeedAngle.m_RotX = -ph->m_SpeedAngle.m_RotX & 0xFFF;
  ph->m_SpeedAngle.m_RotY = -ph->m_SpeedAngle.m_RotY & 0xFFF;
}

static int FromState8(void) {
  if (HandleSpyroDamage(DAMAGE_NO_CHARGE)) {
    if (g_Spyro.m_idleTimer >= 13)
      MirrorPitchAndRoll();
    return NONE;
  }
  if (g_Spyro.m_idleTimer < 20)
    return NONE;
  MirrorPitchAndRoll();
  if (g_Pad.m_NoMovementButtonPressed)
    return 0;
  func_8003EA68(1);
  ph->m_SpeedAngle.m_Speed = 0x15;
  return NONE;
}

/* States 9 and 10: sidestepping while L1 / R1 is held. */
static int FromSidestep(int pButton) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if (IsFalling())
    return 6;
  if ((g_Pad.m_Held & pButton) || g_Spyro.m_idleTimer <= 20)
    return NONE;
  VecCopy(&ph->m_Acceleration, &ph->unk_0xe8);
  ph->m_SpeedAngle.m_Speed = 0;
  return 0;
}

/* State 11: charging while square is held. Cross jumps (20); blocked at
 * speed knocks back (12). The supercharge (m_doingSupercharge) goes into
 * state 44 over 0x3000; on its first tick it records the height and aims
 * for at least 0x1F80 (m_walkingState bit 0x80: done). */
static int FromCharging(void) {
  int speed;

  if (func_80041270())
    return NONE;
  if ((g_Pad.m_Held & (PAD_CROSS | PAD_SQUARE)) == (PAD_CROSS | PAD_SQUARE) && g_Spyro.m_slopeAngle < 12) {
    func_8003EA68(20);
    ph->m_Acceleration.z += JUMP_BOOST;
    g_Spyro.m_airTime++;
    return NONE;
  }
  if (g_Spyro.m_airTime >= 4)
    return 20;
  if (g_Spyro.m_slopeAngle >= STEEP_SLOPE)
    return 6;
  if (!(g_Pad.m_Held & PAD_SQUARE)) {
    func_8003EA68(2);
    g_Spyro.m_idleTimer = g_Spyro.unk_0x84 * 2;
    return NONE;
  }
  if (IsBlocked() && !g_HasLevelTransition)
    return 12;
  if (!g_Spyro.m_doingSupercharge) {
    g_Spyro.m_walkingState &= ~0x80;
    return NONE;
  }
  if (ph->m_SpeedAngle.m_Speed > 0x3000)
    return 44;
  if (g_Spyro.m_walkingState & 0x80)
    return NONE;
  g_Spyro.m_highestFlightPoint = g_Spyro.m_Position.z;
  speed = VecMagnitude(&ph->unk_0xe8, 1);
  ph->m_TargetSpeedAngle.m_Speed = speed < 0x1F80 ? 0x1F80 : speed;
  g_Spyro.m_walkingState |= 0x80;
  return NONE;
}

/* State 12: knocked back. */
static int FromKnockback(void) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if (g_Spyro.m_idleTimer > 16 && (g_Spyro.m_airTime != 0 || g_Spyro.m_slopeAngle >= STEEP_SLOPE))
    return 6;
  return g_Spyro.m_idleTimer > 24 ? 0 : NONE;
}

/* States 15 (gliding) and 23 (gliding slowly). Lands, falls when it loses
 * its floor or speed, square: charge jump (20), triangle: drop (16). In
 * 15, the flying ability climbs and dives on R1/L1 instead of slowing. */
static int FromGlide(void) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if (g_Spyro.m_airTime == 0)
    return g_Spyro.m_slopeAngle >= STEEP_SLOPE ? 6 : LandingState();
  if (!g_Spyro.m_floorIdleTime || ph->m_TrueSpeed < 0x400)
    return 6;
  if (g_Pad.m_Held & PAD_SQUARE)
    return 20;
  if (g_Spyro.m_State == 15) {
    if (g_Spyro.m_idleTimer > 16 && (g_Pad.m_Down & PAD_TRIANGLE))
      return 16;
    if (g_Spyro.m_flyingAbility) {
      if (g_Spyro.m_walkingState == 0)
        StartClimbOrDive();
      return NONE;
    }
    if (g_Spyro.m_walkingState == 0 && g_Spyro.m_highestFlightPoint + 0x18 < g_Spyro.m_Position.z)
      return 6;
    if (ph->m_SpeedAngle.m_Speed >= 0xC80 || g_Spyro.m_idleTimer <= 16)
      return NONE;
    return 23;
  }
  /* 23: speeding up again glides. */
  if (g_Pad.m_Down & PAD_TRIANGLE)
    return 16;
  if (g_Spyro.m_highestFlightPoint + 0x18 < g_Spyro.m_Position.z)
    return 6;
  return ph->m_SpeedAngle.m_Speed > 0xC80 ? GlideState() : NONE;
}

/* State 16: dropping out of a glide. Landing stands (early, on flat
 * ground) or falls, with the pad locked for 8 frames; cross glides. */
static int FromDrop(void) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if (g_Spyro.m_Position.z >= D_8006E9A4[g_LevelIndex] && g_Spyro.m_airTime == 0) {
    if (g_Spyro.m_idleTimer < 24 && g_Spyro.m_slopeAngle < STEEP_SLOPE) {
      func_8003EA68(0);
    } else {
      func_8003EA68(6);
      g_Spyro.m_idleTimer = g_Spyro.unk_0x84;
    }
    if (g_Spyro.m_noGamepadUpdateFrames < 8)
      g_Spyro.m_noGamepadUpdateFrames = 8;
    func_8003DFA4();
  }
  if (!(g_Pad.m_Down & PAD_CROSS)) {
    FellTooLong();
    return NONE;
  }
  if (!g_Spyro.m_onSlope || g_Spyro.m_idleTimer >= 16)
    return 15;
  return NONE;
}

/* States 18 and 36-43: idle animations. Like standing, but they end with
 * their animation (or unk_0x198) and reset it (func_8003FDC8). */
static int FromIdleAnimation(void) {
  int next;

  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if ((next = GroundAction()) != NONE)
    return next;
  if (ph->m_TrueSpeed > 0x400)
    return 3;
  if (!CameraHoldsSpyro()) {
    if (!g_Pad.m_NoMovementButtonPressed)
      return 1;
    if (g_Spyro.m_onEdge) {
      func_8003FDC8(13);
      return NONE;
    }
    if (!g_Spyro.m_noGamepadUpdateFrames) {
      int anim = g_Spyro.m_nextBodyAnimation;

      if (anim == spyro_StateDefaultAnimation[g_Spyro.m_State] &&
          g_Spyro.m_nextBodyAnimationFrame >= spyro_AnimationDetails[anim].m_EndFrame - 1)
        return 0;
      if (g_Spyro.unk_0x198 != 1)
        return NONE;
    }
  }
  func_8003FDC8(0);
  return NONE;
}

/* State 19: sliding down a slope. Jump early (5), charge jump at speed
 * (20), fall (6); on flat ground at the slide animation's end, lands. */
static int FromSliding(void) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if ((g_Pad.m_Held & PAD_CROSS) && g_Spyro.m_idleTimer < 4)
    return 5;
  if ((g_Pad.m_Held & PAD_SQUARE) && ph->m_SpeedAngle.m_Speed > 0x1F00)
    return 20;
  if (g_Spyro.m_airTime >= 8)
    return 6;
  if (g_Spyro.m_slopeAngle < STEEP_SLOPE &&
      g_Spyro.m_nextBodyAnimationFrame == spyro_AnimationDetails[20].m_EndFrame - 1 &&
      ph->m_TrueSpeed < 0x900)
    return LandingState();
  FellTooLong();
  return NONE;
}

/* State 20: charge jump (m_walkingState bit 0x40: out of a supercharge,
 * which bonks don't stop). Lands into a charge (11) or supercharge (44). */
static int FromChargeJump(void) {
  if (!g_Spyro.m_onSlope && (g_Pad.m_Down & PAD_CROSS))
    g_Spyro.m_isGliding = 1;
  if (!(g_Spyro.m_walkingState & 0x40) && func_80041270())
    return NONE;
  if (PushingIntoSlope())
    return 12;
  if (!g_Spyro.m_touchingMoby && IsBlocked() && !g_HasLevelTransition) {
    func_8003D978();
    return 12;
  }
  if (g_Spyro.m_airTime == 0)
    return g_Spyro.m_walkingState & 0x40 ? 44 : 11;
  if (ph->m_Acceleration.z >= 0 || !g_Spyro.m_isGliding)
    return NONE;
  return GlideState();
}

/* State 21: trotting, between walking and running. */
static int FromTrotting(void) {
  int next;

  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if ((next = GroundAction()) != NONE)
    return next;
  if (ph->m_SpeedAngle.m_Speed > 0xF00)
    return 2;
  return ph->m_SpeedAngle.m_Speed < 0x640 ? 1 : NONE;
}

/* State 24: the supercharge's jump. Cross held in the first ticks
 * (unk_0x15c) boosts it once; blocked or stalled knocks back (12). */
static int FromSuperchargeJump(void) {
  int length;

  if (g_Spyro.m_idleTimer >= 4)
    g_Spyro.unk_0x15c = 0;
  if ((g_Pad.m_Held & PAD_CROSS) && g_Spyro.unk_0x15c) {
    g_Spyro.unk_0x15c = 0;
    ph->m_Acceleration.z += JUMP_BOOST;
  } else if ((g_Pad.m_Down & PAD_CROSS) && !g_Spyro.m_onSlope) {
    g_Spyro.m_isGliding = 1;
  }
  if (PushingIntoSlope())
    return 12;
  length = VecMagnitude(&ph->m_Acceleration, 1);
  if (length == 0)
    length = 1;
  /* True speed under half the acceleration's length. */
  if (!g_Spyro.m_touchingMoby && (int)((u_int)ph->m_TrueSpeed << 12) / length < 0x800 && !g_HasLevelTransition)
    return 12;
  if (g_Spyro.m_airTime == 0)
    return 11;
  if (ph->m_Acceleration.z >= 0)
    return NONE;
  return g_Spyro.m_isGliding ? GlideState() : 20;
}

/* State 26: circling a moby: walks while moving or in the first 16 ticks,
 * else stands. */
static int FromCirclingMoby(void) {
  if (HandleSpyroDamage(DAMAGE_ALL))
    return NONE;
  if (g_Pad.m_Down & PAD_CROSS)
    return 5;
  if (IsFalling())
    return 6;
  if (ph->m_SpeedAngle.m_Speed != 0 || ph->m_TargetSpeedAngle.m_Speed != 0 || g_Spyro.m_idleTimer < 16)
    return 1;
  return 0;
}

/* State 29: sinking (water, lava). Cross jumps out between ticks 16 and
 * 60; after that Spyro dies and the surface's moby spawns. */
static int FromSinking(void) {
  if (HandleSpyroDamage(DAMAGE_NO_CHARGE))
    return NONE;
  if (g_Spyro.m_health < 0)
    return NONE;
  if (g_Spyro.m_idleTimer > 60) {
    g_Spyro.m_health = -1;
    g_SpawnMoby(D_8006EA40[D_80075728], 0);
    return NONE;
  }
  if (g_Spyro.m_idleTimer >= 16 && (g_Pad.m_Down & PAD_CROSS))
    return 5;
  return NONE;
}

/* States 32-34: gliding in a flight level or with Gnasty's Loot's flying
 * ability: 32 level, 33 climbing (cross, or rising fast), 34 diving
 * (falling fast). Outside flight levels square then charge-jumps and
 * triangle drops, even right after another change. */
static void FromFlightGlide(int pState) {
  int vz, next = NONE;

  if (HandleSpyroDamage(DAMAGE_ALL))
    return;
  vz = ph->m_TrueVelocity.z;
  if (g_Spyro.m_airTime == 0) {
    next = g_Spyro.m_slopeAngle >= STEEP_SLOPE ? 6 : LandingState();
  } else if (!g_Spyro.m_floorIdleTime || ph->m_TrueSpeed < 0x400) {
    next = 6;
  } else if (pState == 32) {
    if (vz > 0x800 || (g_Pad.m_Down & PAD_CROSS))
      next = 33;
    else if (vz < -0x1000)
      next = 34;
  } else if (pState == 34 && (g_Pad.m_Down & PAD_CROSS)) {
    next = 33;
  } else if (g_Spyro.m_idleTimer >= 31 && (pState == 33 ? vz < 0x600 : vz >= -0xBFF)) {
    next = 32;
  }
  if (next != NONE)
    func_8003EA68(next);
  else if (g_Spyro.m_walkingState == 0)
    StartClimbOrDive();

  if (g_IsFlightLevel)
    return;
  if (g_Pad.m_Held & PAD_SQUARE) {
    func_8003EA68(20);
    return;
  }
  if (g_Spyro.m_idleTimer > 16 && (g_Pad.m_Down & PAD_TRIANGLE))
    func_8003EA68(16);
}

/* State 44: supercharging. Cross jumps (24) with a boost when let go,
 * after ten ticks held (D_80075700 counts them) or in the air; airborne a
 * while, it jumps without one (unk_0x15c: cross may still boost). Only
 * charge damage and knockback. */
static int FromSupercharge(void) {
  int jump = 0;

  if (HandleSpyroDamage(DAMAGE_SUPERCHARGE))
    return NONE;
  if (g_Pad.m_Held & PAD_CROSS) {
    if (D_80075700 >= 10 || g_Spyro.m_airTime != 0)
      jump = 1;
    else
      D_80075700 += g_DeltaTime;
  }
  if (jump || (g_Pad.m_Released & PAD_CROSS)) {
    func_8003EA68(24);
    g_Spyro.unk_0x15c = 0;
    ph->m_Acceleration.z += JUMP_BOOST;
    g_Spyro.m_airTime++;
    return NONE;
  }
  if (g_Spyro.m_airTime >= 4) {
    func_8003EA68(24);
    g_Spyro.unk_0x15c = 1;
    return NONE;
  }
  if (g_Spyro.m_slopeAngle >= STEEP_SLOPE)
    return 6;
  if (g_Spyro.m_idleTimer >= 15 && !(g_Pad.m_Held & PAD_SQUARE))
    return 11;
  if (!g_Spyro.m_touchingMoby && IsBlocked() && !g_HasLevelTransition)
    return 12;
  return ph->m_SpeedAngle.m_Speed < 0x1E00 ? 11 : NONE;
}

/// @brief Spyro's state machine: after the physics, picks his next state
/// (func_8003EA68) from the pad, the ground and the damage flags.
/// @note asm/nonmatchings/pete/func_80041670.s, by hand.
void func_80041670(void) {
  int next = NONE;

  switch (g_Spyro.m_State) {
  case 0: case 13: next = FromStanding(); break;
  case 1: next = FromWalking(); break;
  case 2: next = FromRunning(); break;
  case 3: next = FromStopping(); break;
  case 4: next = FromTurning(); break;
  case 5: next = FromJumping(); break;
  case 6: next = FromAirborne(); break;
  case 8: next = FromState8(); break;
  case 9: next = FromSidestep(PAD_L1); break;
  case 10: next = FromSidestep(PAD_R1); break;
  case 11: next = FromCharging(); break;
  case 12: next = FromKnockback(); break;
  case 15: case 23: next = FromGlide(); break;
  case 16: next = FromDrop(); break;
  case 17: HandleSpyroDamage(DAMAGE_ALL); break; /* the whirlwind */
  case 18: case 36: case 37: case 38: case 39: case 40: case 41: case 42:
  case 43:
    next = FromIdleAnimation();
    break;
  case 19: next = FromSliding(); break;
  case 20: next = FromChargeJump(); break;
  case 21: next = FromTrotting(); break;
  case 24: next = FromSuperchargeJump(); break;
  case 26: next = FromCirclingMoby(); break;
  case 29: next = FromSinking(); break;
  case 32: case 33: case 34: FromFlightGlide(g_Spyro.m_State); break;
  case 35: /* scripted: stands once the script lets go */
    if (!HandleSpyroDamage(DAMAGE_ALL) && !g_Spyro.m_ControlFlags)
      next = 0;
    break;
  case 44: next = FromSupercharge(); break;

  /* Hurt: no damage handling; over after 24 ticks (27: 48), unless the
   * hit is still on (7: 0x20, 25: 0x10). */
  case 7:
    if (!(g_Spyro.m_DamageFlags & 0x20))
      EndHurt(24);
    break;
  case 25:
    if (!(g_Spyro.m_DamageFlags & 0x10))
      EndHurt(24);
    break;
  case 14: case 22: case 28: EndHurt(24); break;
  case 27: EndHurt(48); break;

  default: /* 30, 31: dying */
    break;
  }
  if (next != NONE)
    func_8003EA68(next);
}

#undef ph
#endif

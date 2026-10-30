/* Spyro's state change (func_8003EA68) and movement with collision
 * (func_8003FE40), two player-controller (pete) functions the decomp hasn't
 * matched, written by hand from the asm so they read as C (docs/porting-
 * plan.md §7, M2). Each one replaces its mips2c unit (cmake/mips2c.cmake
 * M2C_HAND) and is checked against PCSX-Redux with the oracle replay
 * (docs/translation-log.md). M2C_<name>=1 compiles one out for the mips2c
 * unit (cmake SPYRO_M2C_FUNCS, A/B tests).
 *
 * Spyro fields spyro.h still calls unk_, as these functions use them:
 *   unk_0x84        ticks Spyro spent in the state before this one
 *   unk_0x15c       Spyro may jump (func_80041670 checks it with cross)
 *   unk_0x194       a cutscene is walking Spyro (forced flame, pete.c)
 *   unk_0x208       a scripted velocity per tick (m_ControlFlags 0x40)
 *   unk_0x21c/220   camera focus and preset for m_ControlFlags 0x200
 *   unk_0x254       entered a glide climbing steeply (m_RotY > 0x400)
 *   unk_0x17c       portal flight's target when a step first hit terrain
 *   m_Physics.unk_0x144  ground speed limit (func_8003D3B8)
 *   m_Physics.unk_0xe8   horizontal momentum (jump, charge, flight) */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "camera.h"
#include "collision.h"
#include "environment.h"
#include "gamepad.h"
#include "math.h"
#include "moby.h"
#include "overlay_pointers.h"
#include "rand.h"
#include "spu.h"
#include "spyro.h"
#include "variables.h"
#include "vector.h"

/* Wrap an angle difference (12-bit angles, 0x1000 a turn) into
 * -0x7FF..0x800, as the asm does: `andi 0xFFF; slti 0x801; addiu -0x1000`. */
static inline int AngleDelta(int delta) {
  delta &= 0xFFF;
  if (delta > 0x800)
    delta -= 0x1000;
  return delta;
}

static inline int Abs(int v) { return v < 0 ? -v : v; }

/* pete.c */
void func_8003E1AC(void);
void ApplySlopeGravity(void);
void func_8003E90C(void);
void func_8003D3B8(int speed);
void func_8003EA68(int pNewState);
void func_8003FE40(void);

static_assert(offsetof(Spyro, m_State) == 0x78, "Spyro.m_State");
static_assert(offsetof(Spyro, m_Physics.m_TrueVelocity) == 0x10C, "Spyro.m_TrueVelocity");
static_assert(offsetof(Spyro, unk_0x15c) == 0x15C, "Spyro.unk_0x15c");
static_assert(offsetof(Spyro, m_ControlFlags) == 0x1F4, "Spyro.m_ControlFlags");
static_assert(offsetof(Spyro, m_mobyInUseBySpyro) == 0x224, "Spyro.m_mobyInUseBySpyro");
static_assert(offsetof(Spyro, m_damageSoundChannel) == 0x2A0, "Spyro.m_damageSoundChannel");
static_assert(sizeof(Moby) == 0x58, "Moby size");
static_assert(offsetof(Moby, m_Rotation.z) == 0x46, "Moby.m_Rotation.z");
static_assert(offsetof(Moby, m_State) == 0x48, "Moby.m_State");
static_assert(offsetof(Camera, m_Rotation.z) == 0x50, "Camera.m_Rotation.z");

#define SPYRO_MOBY ((Moby *)&g_Spyro) /* Spyro as a sound source */

static void Rumble(int *pTimer, int pMin) {
  if (*pTimer < pMin)
    *pTimer = pMin;
}

/* ------------------------------------------------------------------------ */
#if !M2C_func_8003EA68
extern struct {
  u_char m_StartFrame, m_EndFrame;
  u_char m_TransitionLastFrame, m_FrameRate;
} spyro_AnimationDetails[46];                 // pete.c
extern u_char spyro_StateDefaultAnimation[48]; // pete.c

extern u_char D_80075274[8]; // Idle: wait multiplier by rand() & 7 (with D_80075278)
extern int D_800756B4;       // Jump held without the button (cutscenes)
extern Moby *D_80075790;     // Moby a charge homes in on
extern int D_80075728;       // Special surface's param 1 (special_surfaces.c)
extern int D_8006EA34[];     // Splash moby class by D_80075728
extern int D_80075668;       // Whirlwind: bottom height
extern int D_8007578C;       // Whirlwind: spiral angle
extern int D_800757F0;       // Whirlwind: spiral turns ...
extern int D_80075724;       // ... over this height

/* Ground speed limit on a slope (unk_0x144), lerped from flat (B8, BC, C0)
 * to steepest (C4, C8, CC): the first column is uphill or downhill along
 * the heading (the third when facing down the slope), the middle across. */
extern int D_8006C5B8, D_8006C5BC, D_8006C5C0;
extern int D_8006C5C4, D_8006C5C8, D_8006C5CC;

static_assert(offsetof(Spyro, unk_0x84) == 0x84, "Spyro.unk_0x84");

static void PlaySpyroSound(int pSound, u_char *pChannel) {
  PlaySound(pSound, SPYRO_MOBY, 4, pChannel);
}

/* Leaving the old state: stop his looped sounds, or the stars of a hurt. */
static void LeaveState(int pOldState) {
  switch (pOldState) {
  case 11: case 15: case 32: case 44:
    func_800562A4(SPYRO_MOBY, 2);
    break;
  case 7:
    g_Spyro.m_colorFilter.m_interpolation = 0;
    /* fall through */
  case 14: case 22: case 27: case 28:
    PlaySpyroSound(g_Spu.m_SoundTable->spyroStars, &g_Spyro.m_damageSoundChannel);
    break;
  case 25:
    PlaySpyroSound(g_Spu.m_SoundTable->spyroUnsquish, &g_Spyro.m_damageSoundChannel);
    break;
  }
}

/* Keep the speed along the true velocity, unless it points backwards
 * (more than a quarter turn off the heading). */
static void KeepForwardSpeed(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  u_int off = (Atan2(ph->m_TrueVelocity.x, ph->m_TrueVelocity.y, 1) - ph->m_SpeedAngle.m_RotZ) & 0xFFF;

  if (off - 0x401 < 0x7FF)
    ph->m_SpeedAngle.m_Speed = 0;
  else
    ph->m_SpeedAngle.m_Speed = VecMagnitude(&ph->m_TrueVelocity, 0);
}

/* Ground speed limit on a slope: interpolate the D_8006C5B8 table by the
 * slope's steepness, then combine the along- and across-slope limits by the
 * heading. */
static int SlopeSpeedLimit(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int off, t, along, across;

  off = Abs(AngleDelta(ph->m_SpeedAngle.m_RotZ -
                       Atan2(g_Spyro.m_floorPositonOnSlope.x, g_Spyro.m_floorPositonOnSlope.y, 1)));
  t = (g_Spyro.m_slopeAngle << 12) / 22;
  if (off > 0x400) {
    along = D_8006C5C0 + ((t * (D_8006C5CC - D_8006C5C0)) >> 12);
    off = 0x800 - off;
  } else {
    along = D_8006C5B8 + ((t * (D_8006C5C4 - D_8006C5B8)) >> 12);
  }
  across = D_8006C5BC + ((t * (D_8006C5C8 - D_8006C5BC)) >> 12);
  along = (along * Cos(off)) >> 12;
  across = (across * Sin(off)) >> 12;
  return func_80017A38(along * along + across * across);
}

/* States 1, 2, 3 and 21: walking and running. */
static void EnterRunning(int pNewState) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  g_Spyro.m_walkingState = 0;
  KeepForwardSpeed();
  if (pNewState == 3 && ph->m_SpeedAngle.m_Speed > 0x1400)
    ph->m_SpeedAngle.m_Speed = 0x1400;
  ph->m_gravity = -0xC00;
  func_8003E1AC();
  ph->unk_0x144 = g_Spyro.m_slopeAngle ? SlopeSpeedLimit() : D_8006C5B8;
  g_Spyro.unk_0x15c = 1;
  if (g_Spyro.unk_0x194) { /* cutscene walk: from standing, to the portal's heading */
    ph->m_TargetSpeedAngle.m_Speed = 0;
    ph->m_TargetSpeedAngle.m_RotZ = g_Spyro.m_portalAngle.z << 4;
  }
}

/* The charge locks on to the nearest moby with collision that is ahead of
 * Spyro (within 0x200 of his heading, closer than 0x1800, nearer heading
 * first), but only when he charges more than 0x200 off the camera's
 * direction. */
static void FindChargeTarget(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D toMoby;
  Moby *moby;
  int best = 30000, off, dist;

  D_80075790 = NULL;
  if (Abs(AngleDelta(ph->m_SpeedAngle.m_RotZ - g_Camera.m_Rotation.z)) <= 0x200)
    return;
  for (moby = g_LevelMobys; moby < g_DynMobys; moby++) {
    if (moby->m_State >= 0x7F || moby->m_CollisionGroup == NULL)
      continue;
    VecSub(&toMoby, &moby->m_Position, &g_Spyro.m_Position);
    off = Abs(AngleDelta(Atan2(toMoby.x, toMoby.y, 1) - ph->m_SpeedAngle.m_RotZ));
    if (off >= 0x200 || Abs(toMoby.x) + Abs(toMoby.y) + Abs(toMoby.z) >= 0x4000)
      continue;
    dist = VecMagnitude(&toMoby, 1);
    if (dist >= 0x1800)
      continue;
    dist += off * 4;
    if (dist < best) {
      best = dist;
      D_80075790 = moby;
    }
  }
}

/* Supercharge or flying: remember the height and keep at least 0x1F80. */
static void StartFastFlight(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int speed;

  g_Spyro.m_highestFlightPoint = g_Spyro.m_Position.z;
  speed = VecMagnitude(&ph->unk_0xe8, 1);
  ph->m_TargetSpeedAngle.m_Speed = speed < 0x1F80 ? 0x1F80 : speed;
}

/* State 11: charging (m_walkingState 1, or 0x81 supercharged). */
static void EnterCharge(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  VecCopy(&ph->unk_0xe8, &ph->m_TrueVelocity);
  if (g_Spyro.m_doingSupercharge) {
    if (!(g_Spyro.m_walkingState & 0x80))
      StartFastFlight();
    g_Spyro.m_walkingState = 0x81;
  } else {
    g_Spyro.m_walkingState = 1;
  }
  ph->m_TurnMomentum = 0;
  ph->m_gravity = -0x240;
  func_8003E1AC();
  ApplySlopeGravity();
  g_Spyro.unk_0x15c = 1;
  PlaySpyroSound(g_Spu.m_SoundTable->spyroCharge, &g_Spyro.m_chargeSoundChannel);
  FindChargeTarget();
}

/* State 12: knocked back, with four stars around his head. */
static void EnterKnockback(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D star;
  int i, angle = rand() & 0x3F;

  for (i = 0; i < 4; i++) {
    star.x = 0;
    star.y = COSINE_8(angle) >> 7;
    star.z = SINE_8(angle) >> 7;
    VecRotateByMatrix(&g_Spyro.m_RotationMatrix, &star, &star);
    g_SpawnParticle(1, 0x21, &star, 1);
    angle = (angle + 0x40) & 0xFF;
  }
  g_Spyro.m_walkingState = 0;
  VecNull(&ph->unk_0xe8);
  VecSub(&ph->m_Acceleration, &ph->unk_0xe8, &ph->m_Acceleration);
  VecShiftRight(&ph->m_Acceleration, 2);
  ph->m_gravity = -0x300;
  g_Spyro.unk_0x15c = 1;
  Rumble(&D_8007584C, 0x78);
  Rumble(&D_800757D0, 0xF);
}

/* States 7 and 14: hurt. Pushed back against his motion, at most 0x1E00. */
static void EnterHurt(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int len;

  g_Spyro.m_walkingState = 0;
  VecNull(&ph->m_Acceleration);
  VecSub(&ph->m_Acceleration, &ph->m_Acceleration, &ph->m_TrueVelocity);
  len = VecMagnitude(&ph->m_Acceleration, 1);
  if (len > 0x1E00)
    VecScaleToLength(&ph->m_Acceleration, len, 0x1E00);
  ph->m_gravity = -0x300;
  if (g_Spyro.m_airTime == 0)
    func_8003E1AC();
  g_Spyro.unk_0x15c = 1;
  g_SpawnParticle(5, 10, NULL, 0);
}

/* States 15, 23, 32, 33 and 34: gliding and portal flight. Entering from
 * another of them changes nothing but the jump. */
static void EnterGlide(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int old = g_Spyro.m_State;

  if (old == 15 || old == 23 || old == 32 || old == 33 || old == 34) {
    g_Spyro.unk_0x15c = 0;
    return;
  }
  g_Spyro.m_walkingState = 0;
  g_Spyro.m_onSlope = 1;
  ph->m_TurnMomentum = 0;
  D_80075960 = 0;
  D_800758A0 = 0;
  D_80075700 = 0;
  if (ph->m_TrueVelocity.z > 0)
    ph->m_TrueVelocity.z = 0;
  ph->m_SpeedAngle.m_Speed = VecMagnitude(&ph->m_Acceleration, 0);
  if (ph->m_SpeedAngle.m_Speed > 0x1900)
    ph->m_SpeedAngle.m_Speed = 0x1900;
  if (ph->m_SpeedAngle.m_Speed < 0x780)
    ph->m_SpeedAngle.m_Speed = 0x780;
  if (old != 6 || g_IsFlightLevel || g_Spyro.m_highestFlightPoint < g_Spyro.m_Position.z)
    g_Spyro.m_highestFlightPoint = g_Spyro.m_Position.z;
  g_Spyro.unk_0x254 = ph->m_SpeedAngle.m_RotY > 0x400;
  ph->m_gravity = -0x80;
  g_Spyro.unk_0x15c = 0;
}

/* State 17: caught by the whirlwind m_mobyInUseBySpyro; set up its spiral
 * (UpdateWhirlwind in pete_hand.c). Without one, keep the momentum. */
static void EnterWhirlwind(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int turns;

  if (g_Spyro.m_mobyInUseBySpyro) {
    VecNull(&ph->m_Acceleration);
    g_Spyro.m_highestFlightPoint = g_Spyro.m_portalEndPos.z;
    D_80075724 = g_Spyro.m_portalEndPos.z - g_Spyro.m_Position.z - 0x800;
    D_80075668 = g_Spyro.m_Position.z;
    D_8007578C = ph->m_SpeedAngle.m_RotZ;
    turns = ((D_80075724 << 10) >> 10) + 0x400; /* low 22 bits, signed */
    D_800757F0 = AngleDelta((g_Spyro.m_mobyInUseBySpyro->m_Rotation.z << 4) - ph->m_SpeedAngle.m_RotZ - turns);
  } else {
    VecCopy(&ph->m_Acceleration, &ph->m_TrueVelocity);
  }
  ph->m_gravity = 0x80;
  g_Spyro.unk_0x15c = 0;
}

/* States 6 and 16: falling. A push off a ledge he is stuck on. */
static void EnterFall(void) {
  if (g_Spyro.m_floorIdleTime == 0 && g_Spyro.m_slopeAngle < 0x20)
    func_8003E90C();
  g_Spyro.m_Physics.m_gravity = -0xC0;
  g_Spyro.m_isGliding = 0;
  g_Spyro.unk_0x15c = 0;
}

/* State 20: the flying ability (Gnasty's Loot), keeping the momentum. */
static void EnterFlyingAbility(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  VecCopy(&ph->m_Acceleration, &ph->m_TrueVelocity);
  VecCopy(&ph->unk_0xe8, &ph->m_TrueVelocity);
  ph->m_TurnMomentum = 0;
  if (g_Spyro.m_doingSupercharge) {
    if (!(g_Spyro.m_walkingState & 0x80))
      StartFastFlight();
    g_Spyro.m_walkingState |= 0x80;
  } else {
    g_Spyro.m_walkingState = 0;
    ph->m_Acceleration.z = 0;
    ph->unk_0xe8.z = 0;
  }
  ph->m_gravity = -0x240;
  g_Spyro.m_isGliding = 0;
  g_Spyro.unk_0x15c = 0;
}

/* State 44: flying (flight levels). */
static void EnterFlying(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  VecCopy(&ph->unk_0xe8, &ph->m_TrueVelocity);
  if (!(g_Spyro.m_walkingState & 0x80))
    StartFastFlight();
  g_Spyro.m_walkingState = 0xC0;
  ph->m_TurnMomentum = 0;
  ph->m_gravity = -0x240;
  func_8003E1AC();
  ApplySlopeGravity();
  g_Spyro.unk_0x15c = 1;
  D_80075700 = 0;
  PlaySpyroSound(g_Spu.m_SoundTable->superCharge, &g_Spyro.m_chargeSoundChannel);
}

/* States 22 and 28: thrown by a scripted velocity (unk_0x208, per tick;
 * z 0 keeps his own). */
static void EnterThrown(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  ph->m_Acceleration.x = g_Spyro.unk_0x208.x << 6;
  g_Spyro.m_walkingState = 0;
  ph->m_Acceleration.y = g_Spyro.unk_0x208.y << 6;
  ph->m_Acceleration.z = g_Spyro.unk_0x208.z ? g_Spyro.unk_0x208.z << 6 : ph->m_TrueVelocity.z;
  (void)Atan2(ph->m_Acceleration.x, ph->m_Acceleration.y, 1); /* result unused in the asm */
  ph->m_gravity = -0x300;
  if (g_Spyro.m_airTime == 0)
    func_8003E1AC();
  g_Spyro.unk_0x15c = 1;
  g_SpawnParticle(5, 10, NULL, 0);
  PlaySpyroSound(g_Spu.m_SoundTable->flipImpact, &g_Spyro.m_damageSoundChannel);
  Rumble(&D_80075904, 0xF);
}

/* State 29: in water (a splash moby by the surface's param). */
static void EnterSplash(void) {
  g_Spyro.m_walkingState = 0;
  g_Spyro.m_Physics.m_gravity = -0x80;
  g_Spyro.unk_0x15c = 1;
  if (g_Spyro.m_invulverabilityTimer == 0)
    g_SpawnParticle(5, 10, NULL, 0);
  g_SpawnMoby(D_8006EA34[D_80075728], NULL);
  PlaySpyroSound(g_Spu.m_SoundTable->waterSplash, &g_Spyro.m_damageSoundChannel);
  PlaySpyroSound(g_Spu.m_SoundTable->spyroStars, &g_Spyro.m_damageSoundChannel);
  Rumble(&D_80075904, 0xF);
}

/* States 30 and 31 (31 with a puff). */
static void EnterDying(int pPuff) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  g_Spyro.m_walkingState = 0;
  VecCopy(&ph->unk_0xe8, &ph->m_TrueVelocity);
  ph->m_gravity = -0x300;
  if (g_Spyro.m_airTime == 0)
    func_8003E1AC();
  g_Spyro.unk_0x15c = 1;
  if (pPuff)
    g_SpawnParticle(5, 10, NULL, 0);
}

/// @brief Changes Spyro's state: leaves the old one (sounds), sets up the
/// new one's physics, then records the change and the new state's
/// animation speed.
/// @param pNewState The state to enter (0..44; others only record it).
/// @note asm/nonmatchings/pete/func_8003EA68.s, by hand. The asm leaves the
/// new state in v1 and the frame rate in v0; no caller reads them (the
/// decomp declares it void; asm callers: docs/translation-log.md).
void func_8003EA68(int pNewState) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int wait, ticks;

  LeaveState(g_Spyro.m_State);

  switch (pNewState) {
  case 0: /* idle: the first idle animation after a random wait */
    g_Spyro.m_walkingState = 0;
    wait = D_80075274[rand() & 7] * spyro_AnimationDetails[0].m_TransitionLastFrame;
    ph->m_gravity = -0x80;
    g_Spyro.unk_0x15c = 1;
    D_80075788 = wait - 2;
    break;
  case 1: case 2: case 3: case 21:
    EnterRunning(pNewState);
    break;
  case 4: /* turning on the spot */
    g_Spyro.m_walkingState = 0;
    ph->m_gravity = -0xC00;
    func_8003E1AC();
    g_Spyro.unk_0x15c = 1;
    break;
  case 5: /* jumping */
    VecCopy(&ph->m_Acceleration, &ph->m_TrueVelocity);
    ph->unk_0xe8.z = 0xDC0;
    ph->m_gravity = -0xC0;
    g_Spyro.m_walkingState = 0;
    g_Spyro.m_isGliding = 0;
    g_Spyro.unk_0x15c = 0;
    g_Spyro.m_highestFlightPoint = g_Spyro.m_Position.z + (ph->m_Acceleration.z >> 6);
    D_800756B4 = g_Spyro.m_State == 29;
    break;
  case 6:
    g_Spyro.m_walkingState = 0;
    VecCopy(&ph->m_Acceleration, &ph->m_TrueVelocity);
    EnterFall();
    break;
  case 16:
    g_Spyro.m_walkingState = 0;
    ph->m_Acceleration.x = 0;
    ph->m_Acceleration.y = 0;
    ph->m_Acceleration.z = ph->m_TrueVelocity.z;
    EnterFall();
    break;
  case 7:
    EnterHurt();
    Rumble(&D_80075764, 0x2D);
    break;
  case 14:
    EnterHurt();
    Rumble(&D_80075904, 0xF);
    PlaySpyroSound(g_Spu.m_SoundTable->flipImpact, &g_Spyro.m_damageSoundChannel);
    break;
  case 8:
    g_Spyro.m_walkingState = 0;
    g_Spyro.unk_0x15c = 1;
    break;
  case 9: case 10: /* sidestepping */
    g_Spyro.m_walkingState = 0;
    VecCopy(&ph->unk_0xe8, &ph->m_TrueVelocity);
    ph->m_SpeedAngle.m_Speed = 0;
    ph->m_gravity = -0xC00;
    func_8003E1AC();
    g_Spyro.unk_0x15c = 1;
    break;
  case 11:
    EnterCharge();
    break;
  case 12:
    EnterKnockback();
    break;
  case 13:
    ph->m_gravity = -0xC00;
    g_Spyro.m_walkingState = 0;
    g_Spyro.unk_0x15c = 1;
    break;
  case 15: case 23: case 32: case 33: case 34:
    EnterGlide();
    break;
  case 17:
    EnterWhirlwind();
    break;
  case 18: case 36: case 37: case 38: case 39: case 40: case 41: case 42: case 43:
    /* idle animations; the asm indexes the frame table by state here */
    g_Spyro.m_walkingState = 0;
    ph->m_gravity = -0xC00;
    D_80075788 = spyro_AnimationDetails[pNewState].m_TransitionLastFrame << 4;
    break;
  case 19: /* sliding: no upward momentum */
    g_Spyro.m_walkingState = 0;
    VecCopy(&ph->unk_0xe8, &ph->m_TrueVelocity);
    if (ph->unk_0xe8.z > 0)
      ph->unk_0xe8.z = 0;
    ph->m_gravity = -0x200;
    func_8003E1AC();
    ApplySlopeGravity();
    g_Spyro.unk_0x15c = 0;
    break;
  case 20:
    EnterFlyingAbility();
    break;
  case 22: case 28:
    EnterThrown();
    break;
  case 24: /* gliding */
    VecCopy(&ph->m_Acceleration, &ph->m_TrueVelocity);
    VecCopy(&ph->unk_0xe8, &ph->m_TrueVelocity);
    ph->m_TurnMomentum = 0;
    g_Spyro.m_walkingState = 0xC0;
    ph->m_gravity = 0;
    g_Spyro.m_isGliding = 0;
    break;
  case 25: /* squashed */
    g_Spyro.m_walkingState = 0;
    VecNull(&ph->m_Acceleration);
    ph->m_Acceleration.z = -0xC00;
    g_Spyro.unk_0x15c = 1;
    g_SpawnParticle(5, 10, NULL, 0);
    Rumble(&D_80075904, 0xF);
    PlaySpyroSound(g_Spu.m_SoundTable->splort, &g_Spyro.m_damageSoundChannel);
    break;
  case 26: /* circling m_mobyInUseBySpyro */
    g_Spyro.m_walkingState = 0;
    KeepForwardSpeed();
    ph->m_gravity = -0xC00;
    func_8003E1AC();
    ph->unk_0x144 = 0x280;
    D_800758A0 = 0;
    g_Spyro.unk_0x15c = 1;
    break;
  case 27:
    g_Spyro.m_walkingState = 0;
    VecNull(&ph->m_Acceleration);
    ph->m_gravity = -0x300;
    g_Spyro.unk_0x15c = 1;
    g_SpawnParticle(5, 10, NULL, 0);
    Rumble(&D_80075904, 0xF);
    break;
  case 29:
    EnterSplash();
    break;
  case 30:
    EnterDying(0);
    break;
  case 31:
    EnterDying(1);
    break;
  case 35:
    ph->m_gravity = -0x80;
    g_Spyro.m_walkingState = 0;
    g_Spyro.unk_0x15c = 1;
    break;
  case 44:
    EnterFlying();
    break;
  }

  ticks = g_Spyro.m_idleTimer;
  g_Spyro.m_State = pNewState;
  g_Spyro.m_idleTimer = 0;
  g_Spyro.unk_0x84 = ticks;
  g_Spyro.m_bodyAnimationSpeed =
      spyro_AnimationDetails[spyro_StateDefaultAnimation[g_Spyro.m_State]].m_FrameRate;
}
#endif

/* ------------------------------------------------------------------------ */
#if !M2C_func_8003FE40
extern Moby *D_80075804;                 // Moby Spyro last touched (pete.c)
extern int D_800758A0;                   // Circling speed around m_mobyInUseBySpyro
extern int D_80075948;                   // Ticks the camera has been behind the moby (0x1000)
extern Vector3D D_80077798;              // Camera focus above the moby (0x1000)
extern SphericalCoordsOffset D_8006CB2C; // Camera preset for it
extern Vector3D D_80077858;              // Portal flight: the point flown towards

#define SPYRO_RADIUS 0x164 /* ColSphere and ColMoby */
#define MAX_PUSH 0x140     /* a collision moves him at most this from the step's start */
#define SUBSTEP 0x154      /* longest movement per collision step */

/* m_ControlFlags (spyro.h ControlFlags), the bits used here. */
enum {
  CTRL_MOVE_X = 0x1, CTRL_MOVE_Y = 0x2, CTRL_MOVE_Z = 0x4, /* velocity set by script */
  CTRL_ANGLE_X = 0x8, CTRL_ANGLE_Y = 0x10, CTRL_ANGLE_Z = 0x20, /* m_portalAngle */
  CTRL_MOVE_BY_VELOCITY = 0x40, /* unk_0x208 per tick, else to m_portalEndPos */
  CTRL_FALLING_STATE = 0x100,   /* hold m_fallingState */
  CTRL_CAMERA_FOCUS = 0x200,    /* camera on unk_0x21c with preset unk_0x220 */
  CTRL_FOLLOW_MOBY = 0x1000,    /* camera behind m_mobyInUseBySpyro */
  CTRL_PAD_LOCKED = 0x2000,
  CTRL_FORCE_RUN = 0x20000,     /* cutscene walk (state 1) */
};

/* One scripted axis: the velocity for this tick, and the acceleration that
 * gives it over g_DeltaTime. */
static void ScriptAxis(int *pVelocity, int *pAcceleration, int pDistance) {
  int v = pDistance << 6;
  *pVelocity = v;
  *pAcceleration = v / g_DeltaTime;
}

/* m_ControlFlags 0x1000: Spyro rides or circles m_mobyInUseBySpyro with
 * the camera behind it. Once the camera has been behind for 30 ticks,
 * walking at the moby from the side (within 0x28..0x38 of its heading)
 * puts Spyro into state 26 to circle it (m_walkingState 1 or 2 for the
 * direction). */
static void FollowMoby(int pFlags) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D d;
  int targetRotZ = ph->m_TargetSpeedAngle.m_RotZ;
  int rel, limit, left, heading;

  rel = (g_Spyro.m_mobyInUseBySpyro->m_Rotation.z - (g_Camera.m_Rotation.z >> 4)) & 0xFF;
  if (rel > 0x80)
    rel -= 0x100;

  if (Abs(rel) >= 0x10) {
    D_80075948 = 0;
    g_Spyro.m_ControlFlags = pFlags | CTRL_PAD_LOCKED;
    if (g_Spyro.m_noGamepadUpdateFrames < 2)
      g_Spyro.m_noGamepadUpdateFrames = 2;
  } else {
    if (D_80075948 < 30) {
      D_800758A0 = 0;
      g_Spyro.m_ControlFlags = pFlags | CTRL_PAD_LOCKED;
      if (g_Spyro.m_noGamepadUpdateFrames < 2)
        g_Spyro.m_noGamepadUpdateFrames = 2;
    } else {
      func_8003D3B8(ph->unk_0x144);
      if (ph->m_TargetSpeedAngle.m_Speed == 0) {
        D_800758A0 = 0;
      } else {
        VecSub(&d, &g_Spyro.m_Position, &g_Spyro.m_mobyInUseBySpyro->m_Position);
        rel = (Atan2(d.x, d.y, 0) - g_Spyro.m_mobyInUseBySpyro->m_Rotation.z + 0x80) & 0xFF;
        if (rel > 0x80)
          rel -= 0x100;
        if (g_LevelId == 20)
          limit = g_Spyro.m_State == 26 ? 0x32 : 0x28;
        else
          limit = g_Spyro.m_State == 26 ? 0x38 : 0x30;
        if (Abs(rel) >= limit) {
          D_800758A0 = 0;
        } else {
          VecSub(&d, &g_Spyro.m_mobyInUseBySpyro->m_Position, &g_Spyro.m_Position);
          heading = g_Spyro.m_mobyInUseBySpyro->m_Rotation.z;
          left = ((Atan2(d.x, d.y, 1) - (heading << 4)) & 0xFFF) < 0x800;
          heading += left ? 0x40 : -0x40;
          if (Abs(AngleDelta((heading << 4) - ph->m_TargetSpeedAngle.m_RotZ)) < 0x200) {
            if (g_Spyro.m_State != 26)
              func_8003EA68(26);
            g_Spyro.m_ControlFlags |= CTRL_FALLING_STATE;
            g_Spyro.m_walkingState = left ? 1 : 2;
          }
        }
      }
    }
    D_80075948 += g_DeltaTime;
  }

  g_Camera.m_Focus = &D_80077798;
  VecCopy(&D_80077798, &g_Spyro.m_mobyInUseBySpyro->m_Position);
  D_80077798.z += 0x200;
  g_Camera.m_SphericalPreset = &D_8006CB2C;
  g_Camera.unk_0xC0 = 0x8000000A;
  ph->m_TargetSpeedAngle.m_RotZ = targetRotZ;
  g_Spyro.m_ControlFlags |= CTRL_CAMERA_FOCUS;
  g_Camera.m_FocusRotation = g_Spyro.m_mobyInUseBySpyro->m_Rotation.z << 4;
}

/* m_ControlFlags bit 31: a script moves Spyro (portals, cutscenes, mobys
 * he rides). */
static void ScriptedControl(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int flags = g_Spyro.m_ControlFlags;

  if ((flags & CTRL_FALLING_STATE) && g_Spyro.m_health >= 0 && g_Spyro.m_State != g_Spyro.m_fallingState) {
    func_8003EA68(g_Spyro.m_fallingState);
    g_Spyro.m_airTime = 1;
  }

  flags = g_Spyro.m_ControlFlags;
  if (flags & (CTRL_MOVE_X | CTRL_MOVE_Y | CTRL_MOVE_Z)) {
    if (flags & CTRL_MOVE_BY_VELOCITY) {
      if (flags & CTRL_MOVE_X)
        ScriptAxis(&ph->m_Velocity.x, &ph->m_Acceleration.x, g_Spyro.unk_0x208.x);
      if (flags & CTRL_MOVE_Y)
        ScriptAxis(&ph->m_Velocity.y, &ph->m_Acceleration.y, g_Spyro.unk_0x208.y);
      if (flags & CTRL_MOVE_Z)
        ScriptAxis(&ph->m_Velocity.z, &ph->m_Acceleration.z, g_Spyro.unk_0x208.z);
    } else {
      if (flags & CTRL_MOVE_X)
        ScriptAxis(&ph->m_Velocity.x, &ph->m_Acceleration.x, g_Spyro.m_portalEndPos.x - g_Spyro.m_Position.x);
      if (flags & CTRL_MOVE_Y)
        ScriptAxis(&ph->m_Velocity.y, &ph->m_Acceleration.y, g_Spyro.m_portalEndPos.y - g_Spyro.m_Position.y);
      if (flags & CTRL_MOVE_Z)
        ScriptAxis(&ph->m_Velocity.z, &ph->m_Acceleration.z, g_Spyro.m_portalEndPos.z - g_Spyro.m_Position.z);
    }
    ph->m_SpeedAngle.m_Speed = VecMagnitude(&ph->m_Acceleration, 0);
  }

  flags = g_Spyro.m_ControlFlags;
  if (flags & CTRL_ANGLE_X) {
    g_Spyro.m_bodyRotation.x = g_Spyro.m_portalAngle.x;
    ph->m_SpeedAngle.m_RotX = g_Spyro.m_portalAngle.x << 4;
  }
  if (flags & CTRL_ANGLE_Y) {
    g_Spyro.m_bodyRotation.y = g_Spyro.m_portalAngle.y;
    ph->m_SpeedAngle.m_RotY = g_Spyro.m_portalAngle.y << 4;
  }
  if (flags & CTRL_ANGLE_Z) {
    g_Spyro.m_bodyRotation.z = g_Spyro.m_portalAngle.z;
    ph->m_SpeedAngle.m_RotZ = g_Spyro.m_portalAngle.z << 4;
  }

  /* Camera modes (camera.c unk_0xC0), unless it's already in 0x80000009. */
  if (g_Camera.unk_0xC0 != (int)0x80000009) {
    if (flags & CTRL_CAMERA_FOCUS) {
      g_Camera.m_FocusRotation = 0;
      g_Camera.unk_0xC0 = 0x8000000A;
      g_Camera.m_Focus = g_Spyro.unk_0x21c;
      g_Camera.m_SphericalPreset = g_Spyro.unk_0x220;
    } else if (flags & 0x400) {
      g_Camera.unk_0xC0 = 0x8000000B;
    } else if (flags & CTRL_FOLLOW_MOBY) {
      FollowMoby(flags);
    } else if (flags & 0x8000) {
      g_Camera.unk_0xC0 = 0x80000007;
    } else if (flags & 0x10000) {
      g_Camera.unk_0xC0 = 0x8000000C;
    }
  }

  if (g_Spyro.m_ControlFlags & CTRL_FORCE_RUN) {
    g_Spyro.unk_0x194 = 1;
    func_8003EA68(1);
  }
}

static Moby *ColMobyAt(int pApplyingDamage, int pCollide) {
  return (Moby *)ColMoby(&g_Spyro.m_Position, SPYRO_RADIUS, (int *)&g_Spyro.m_DamageFlags, pApplyingDamage,
                         NULL, pCollide);
}

/* Moby collision at Spyro's position for this step, by state. Returns the
 * moby hit, or NULL. */
static Moby *CollideMobys(void) {
  Moby *hit;
  int state = g_Spyro.m_State;

  if (state == 11 || (state == 20 && !(g_Spyro.m_walkingState & 0x40))) {
    /* charging damages what it hits, with a rumble */
    hit = ColMobyAt(0x20000, 1);
    if (hit) {
      Rumble(&D_8007584C, 0x64);
      Rumble(&D_800757D0, 0xC);
    }
    return hit;
  }
  if (state == 20 || state == 24 || state == 44) {
    /* flying and gliding: test first, collide only if it can hurt Spyro */
    hit = ColMobyAt(0x80000, 0);
    if (hit && (g_Spyro.m_DamageFlags & 0x4000))
      hit = ColMobyAt(0x80000, 1);
    return hit;
  }
  return ColMobyAt(0, 1);
}

/* Move Spyro back towards pFrom so he is at most MAX_PUSH from it. */
static void LimitPush(Vector3D *pFrom) {
  Vector3D push;
  int len;

  VecSub(&push, &g_Spyro.m_Position, pFrom);
  len = VecMagnitude(&push, 1);
  if (len > MAX_PUSH) {
    VecScaleToLength(&push, len, MAX_PUSH);
    VecAdd(&g_Spyro.m_Position, pFrom, &push);
  }
}

/* ColSphere hit terrain: Spyro is pushed out to g_CollisionPoint. Records
 * a wall (steeper than 0x17, not surface type 6), the first surface touched
 * this tick, and that he is on something (m_floorIdleTime 0; in portal
 * flight only when not flying on towards D_80077858). */
static void HitTerrain(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D toTarget, dir;
  int state = g_Spyro.m_State, steep, surface, type;

  if (state == 15 || state == 32 || state == 33 || state == 34) {
    VecSub(&toTarget, &D_80077858, &g_Spyro.m_Position);
    func_80017330(&toTarget, 0x100);
    if (toTarget.z < -0xC8) {
      g_Spyro.m_floorIdleTime = 0;
    } else {
      VecCopy(&dir, &ph->m_Velocity);
      func_80017330(&dir, 0x100);
      if ((toTarget.x * dir.x + toTarget.y * dir.y + toTarget.z * dir.z) >> 8 >= 0xE1)
        g_Spyro.m_floorIdleTime = 0;
    }
  } else {
    g_Spyro.m_floorIdleTime = 0;
  }

  steep = (signed char)Atan2(g_CollisionNormal.z, VecMagnitude(&g_CollisionNormal, 0), 0);
  if (steep >= 0x17) {
    surface = g_SurfaceBelowFlags & 0x3F;
    type = surface == 0x3F ? 0 : g_Environment.m_SurfaceData[surface]->m_Type;
    if (type != 6) {
      g_Spyro.m_againstWall = 1;
      func_80017330(&g_CollisionNormal, 0x400);
      VecAdd(&g_Spyro.m_wallAgainstSpyro, &g_Spyro.m_wallAgainstSpyro, &g_CollisionNormal);
    }
  }

  VecCopy(&g_Spyro.m_Position, &g_CollisionPoint);
  if (g_Spyro.m_touchingSurface == 0xFF) {
    g_Spyro.m_touchingSurface = g_SurfaceBelowFlags;
    VecCopy(&g_Spyro.unk_0x17c, &D_80077858);
    VecCopy(&g_Spyro.m_KnockbackDirection, &g_CollisionNormal);
  }
}

/* One collision step: move by the (sub-step) velocity, then up to six
 * rounds of moby and terrain collision; if Spyro is still in terrain after
 * that, up to six more of terrain only. The asm keeps the step's start in
 * m_TrueVelocity (rewritten at the end of the tick) for the first rounds
 * and in a local copy for the others. */
static void CollisionStep(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D start;
  Moby *hit;
  int tries;

  VecCopy(&ph->m_TrueVelocity, &g_Spyro.m_Position);
  VecCopy(&start, &g_Spyro.m_Position);
  VecAdd(&g_Spyro.m_Position, &g_Spyro.m_Position, &ph->m_Velocity);

  for (tries = 0; tries < 6; tries++) {
    hit = CollideMobys();
    if (hit) {
      D_80075804 = hit;
      g_Spyro.m_touchingMoby = 1;
      LimitPush(&ph->m_TrueVelocity);
    }
    if (ColSphere(&g_Spyro.m_Position, SPYRO_RADIUS, SPYRO_RADIUS) == 0) {
      if (!hit)
        return;
      continue;
    }
    HitTerrain();
    LimitPush(&ph->m_TrueVelocity);
  }

  for (tries = 0; tries < 6; tries++) {
    if (ColSphere(&g_Spyro.m_Position, SPYRO_RADIUS, SPYRO_RADIUS) == 0)
      return;
    HitTerrain();
    LimitPush(&start);
  }
}

/// @brief Moves Spyro by this tick's velocity with collision: scripted
/// control first (m_ControlFlags bit 31), then sub-steps of at most
/// SUBSTEP against mobys and terrain, and finally his true velocity from
/// where he ended up.
/// @note asm/nonmatchings/pete/func_8003FE40.s, by hand. The asm returns 1
/// in v0; its only caller, func_80047B60 (16 asm call sites), reads neither
/// v0 nor v1 after any of them, so this returns nothing.
void func_8003FE40(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int steps, i;

  g_Spyro.m_touchingMoby = 0;
  g_Spyro.m_touchingSurface = 0xFF;
  g_Spyro.m_againstWall = 0;
  g_Spyro.m_floorIdleTime++;
  VecNull(&g_Spyro.m_wallAgainstSpyro);
  VecAdd(&g_Spyro.m_Position, &g_Spyro.m_Position, &ph->m_CollisionMovement);
  VecNull(&ph->m_CollisionMovement);
  VecCopy(&g_Spyro.m_previousPosition, &g_Spyro.m_Position);

  if (g_Spyro.m_ControlFlags < 0)
    ScriptedControl();
  else
    g_Spyro.m_ControlFlags = 0;

  VecShiftRight(&ph->m_Velocity, 6);
  if (g_Spyro.m_ControlFlags & CTRL_SKIP_SURFACE_CHECK) {
    VecAdd(&g_Spyro.m_Position, &g_Spyro.m_Position, &ph->m_Velocity);
  } else {
    steps = VecMagnitude(&ph->m_Velocity, 1) / SUBSTEP + 1;
    if (steps >= 2) {
      ph->m_Velocity.x /= steps;
      ph->m_Velocity.y /= steps;
      ph->m_Velocity.z /= steps;
    }
    for (i = 0; i < steps; i++)
      CollisionStep();
  }

  VecSub(&ph->m_TrueVelocity, &g_Spyro.m_Position, &g_Spyro.m_previousPosition);
  VecShiftLeft(&ph->m_TrueVelocity, 6);
  ph->m_TrueVelocity.x /= g_DeltaTime;
  ph->m_TrueVelocity.y /= g_DeltaTime;
  ph->m_TrueVelocity.z /= g_DeltaTime;
  ph->m_TrueSpeed = VecMagnitude(&ph->m_TrueVelocity, 1);
}
#endif

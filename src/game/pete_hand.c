/* Player-controller (pete) functions the decomp hasn't matched
 * (asm/nonmatchings/pete), written by hand from the asm so they read as C
 * (docs/porting-plan.md §7, M2): M4's 60 fps presentation needs to see what
 * Spyro's update does. Each one replaces its mips2c unit (cmake/mips2c.cmake
 * M2C_HAND) and is checked against PCSX-Redux with the oracle replay
 * (docs/translation-log.md). M2C_<name>=1 compiles one out for the mips2c
 * unit (cmake SPYRO_M2C_FUNCS, A/B tests). */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "collision.h"
#include "hand_structs.h"
#include "math.h"
#include "memory.h"
#include "moby.h"
#include "overlay_pointers.h"
#include "psx_arena.h"
#include "rand.h"
#include "spu.h"
#include "spyro.h"
#include "variables.h"
#include "vector.h"

#if !M2C_func_80049FAC
extern Vector3D D_8006E2A8[8]; // Ground probe points around Spyro, one per frame
extern int D_8006E9A4[];       // Per-level surface-height threshold (by g_LevelIndex)

/// @brief Places Spyro's shadow and probes the ground around him: each call
/// tests one of eight points (D_8006E2A8, in turn) for floor within reach,
/// so the shadow can be drawn over ledges.
/// @param pTestFloor 0 skips the probe and clears this point's results.
/// @note asm/nonmatchings/pete/func_80049FAC.s, by hand.
void func_80049FAC(int pTestFloor) {
  /* unk_00..unk_04 and unk_08..unk_0c hold eight bytes each, per point:
   * the shadow's drop to the floor there, and "no floor in reach". */
  u_char *drop = (u_char *)&D_8007AA10.unk_00;
  u_char *noFloor = (u_char *)&D_8007AA10.unk_08;
  Vector3D point;
  int i, floor;

  /* The shadow sits under Spyro's centre, 0x164 back along his body. */
  point.x = 0;
  point.y = 0;
  point.z = -0x164;
  VecRotateByMatrix(&g_Spyro.m_RotationMatrix, &point, &point);
  VecAdd(&point, &point, &g_Spyro.m_Position);
  VecCopy(&D_8007AA10.m_Position, &point);
  D_8007AA10.m_Position.z = g_Spyro.m_surfaceBelowSpyro;

  if (g_Spyro.m_sortingDepth < 0x7F)
    g_Spyro.m_sortingDepth = 5;

  D_8007AA10.unk_1c = 3;
  if (g_Spyro.m_Position.z - g_Spyro.m_surfaceBelowSpyro > 0x364)
    D_8007AA10.unk_1c = 5;

  D_8007AA10.m_IsSpyroOverWater = 0;
  if (D_8006E9A4[g_LevelIndex] >= g_Spyro.m_surfaceBelowSpyro || g_Spyro.m_SurfaceProximityState != 0)
    D_8007AA10.m_IsSpyroOverWater = 1;

  i = D_8007AA10.unk_20 = (D_8007AA10.unk_20 + 1) & 7;

  if (pTestFloor == 0) {
    noFloor[i] = 0;
    drop[i] = 0;
    return;
  }

  VecRotateByMatrix(&g_Spyro.m_RotationMatrix, &D_8006E2A8[i], &point);
  VecAdd(&point, &point, &g_Spyro.m_Position);
  point.z += 0x200;
  floor = ColFindFloor(&point, 0x400);
  point.z -= 0x200;

  if ((u_int)(point.z - floor + 0x80) >= 0x100) {
    noFloor[i] = 1; /* nothing within 0x80 of the probe */
    drop[i] = 0;
  } else {
    noFloor[i] = 0;
    drop[i] = (D_8007AA10.m_Position.z - floor) * 194 / 512;
  }
}
#endif

#if !M2C_func_80048D10
/* One point of a puff's path, in head space: 8-byte records whose last two
 * bytes only r_flame.s reads. */
typedef struct {
  Vector3D16 m_Pos;
  u_char unk_6[2];
} FlamePathPoint;

extern u_char D_8006E1C8[2][8];         // Puff order for each m_Pattern
extern Vector3D D_8006DA6C[4];          // Mouth offset by puffs emitted / 2
extern Vector3D D_8006DA9C;             // ... from the 8th puff on
extern FlamePathPoint D_8006DAA8[4][24]; // Paths of puffs 0-3
extern FlamePathPoint D_8006DDA8[4][32]; // Paths of puffs 4-7 (longer)
extern Vector3D D_8006E1D8[8];          // Spark direction where a path ends

/// @brief The fairy kiss glow: sparkles around Spyro and a pulsing red
/// colour filter that blinks out over the last two seconds.
static void UpdateFairyKiss(void) {
  Vector3D sparkle;
  int timer;

  sparkle.x = (rand() & 0x1FF) - 0x160;
  sparkle.y = (rand() & 0x7F) - 0x40;
  sparkle.z = (sparkle.x >> 1) + (rand() & 0x3F) - 0x60;
  VecRotateByMatrix(&g_Spyro.m_RotationMatrix, &sparkle, &sparkle);
  VecAdd(&sparkle, &sparkle, &g_Spyro.m_Position);
  g_SpawnParticle(1, 0x1E, &sparkle, 0);

  g_Spyro.m_colorFilter.m_red = 0x90;
  g_Spyro.m_colorFilter.m_green = 0x20;
  g_Spyro.m_colorFilter.m_blue = 0x10;

  timer = g_Flame.m_FairyKissTimer;
  if (timer < 120 && timer % 24 >= 14)
    g_Spyro.m_colorFilter.m_interpolation = 0;
  else
    g_Spyro.m_colorFilter.m_interpolation = Sin(timer << 7) / 256 - 40;

  g_Flame.m_FairyKissTimer -= g_DeltaTime;
  if (g_Flame.m_FairyKissTimer <= 0) {
    g_Flame.m_FairyKissTimer = 0;
    g_Spyro.m_colorFilter.m_interpolation = 0;
  }
}

/// @brief While the flame is emitting it follows Spyro's head: the mouth
/// position comes from the body animation frame, pushed forward the more
/// puffs are out. Afterwards it keeps its offset from Spyro.
static void UpdateFlameOrigin(void) {
  Vector3D offset;
  SpyroAnimationFrame *frames;
  u_int packed;

  if (!g_Flame.m_Emitting) {
    VecAdd(&g_Flame.m_Position, &g_Spyro.m_Position, &g_Flame.m_OffsetFromSpyro);
    return;
  }

  g_Flame.m_Rotation = g_Spyro.m_bodyRotation;
  Memcpy(&g_Flame.m_HeadRotation, &g_Spyro.m_headRotationMatrix, sizeof(SHORTMATRIX));

  /* The frame's data holds the head's position, packed 11:11:10 (z in
   * units of 2), 0x10 bytes in. Frame data offsets are KUSEG addresses / 2. */
  frames = (SpyroAnimationFrame *)SPYRO_MODEL->m_Animations[g_Spyro.m_bodyAnimation]->m_Frames;
  packed = *(u_int *)((u_char *)PSX_ARENA(PSX_RAM_BASE + ((frames[g_Spyro.m_bodyAnimationFrame].m_Data & 0x1FFFFF) << 1)) + 0x10);
  offset.x = (int)packed >> 21;
  offset.y = (int)(packed << 11) >> 21;
  offset.z = (int)(packed << 22) >> 21;
  VecRotateByMatrix(&g_Spyro.m_RotationMatrix, &offset, &offset);
  VecAdd(&g_Flame.m_Position, &offset, &g_Spyro.m_Position);

  VecCopy(&offset, g_Flame.m_PuffsEmitted < 8 ? &D_8006DA6C[g_Flame.m_PuffsEmitted >> 1] : &D_8006DA9C);
  VecRotateByMatrix((MATRIX *)&g_Flame.m_HeadRotation, &offset, &offset);
  VecAdd(&g_Flame.m_Position, &g_Flame.m_Position, &offset);
  VecSub(&g_Flame.m_OffsetFromSpyro, &g_Flame.m_Position, &g_Spyro.m_Position);
}

/// @brief The point of a puff's path at pIndex, in world space.
static void FlamePathToWorld(Vector3D *pOut, FlamePathPoint *pPath, int pIndex) {
  func_80017C4C(pOut, &pPath[pIndex].m_Pos);
  VecRotateByMatrix((MATRIX *)&g_Flame.m_HeadRotation, pOut, pOut);
  VecAdd(pOut, pOut, &g_Flame.m_Position);
}

/// @brief Particles where a puff stops: the flame itself (pFlameDir), smoke
/// unless it's the fairy kiss flame, and sparks along pSparkDir rotated by
/// the GTE's current matrix (pFlameDir is overwritten with that).
static void SpawnPuffEnd(Vector3D *pPos, Vector3D *pFlameDir, int pFlameClass, Vector3D *pSparkDir) {
  g_SpawnParticle(1, pFlameClass, pPos, (int)pFlameDir);
  if (!g_Flame.m_IsSuperFlame)
    g_SpawnParticle(4, 0x4E, pPos, 0);
  VecRotateByLastMatrix(pSparkDir, pFlameDir);
  g_SpawnParticle(g_Flame.m_IsSuperFlame * 5 + 5, 0x4F, pPos, (int)pFlameDir);
}

/// @brief Updates the flame breath: the fairy kiss glow, new puffs, and each
/// puff along its path, with a moby/world hit test per step.
/// @param pDeltaTime Ticks since the last update (g_DeltaTime).
/// @note asm/nonmatchings/pete/func_80048D10.s, by hand.
void func_80048D10(int pDeltaTime) {
  Vector3D pos, from, spark;
  int i, deadPuffs = 0, playedSound = 0;

  if (g_Flame.m_FairyKissTimer != 0)
    UpdateFairyKiss();

  if (!g_Flame.m_IsFlameActive)
    return;

  /* On the ground and pitched nose-down, the head looks up to compensate. */
  if (g_Spyro.m_airTime == 0 && g_Spyro.m_bodyRotation.y > 0x80)
    g_Spyro.m_HeadLookTarget.y = (0x100 - g_Spyro.m_bodyRotation.y) * 8;
  else
    g_Spyro.m_HeadLookTarget.y = 0;

  /* One new puff per tick, in the pattern's order; a puff started i ticks
   * into this update has aged the rest of it. */
  for (i = 0; i < pDeltaTime; i++) {
    if (g_Flame.m_PuffsEmitted < 8) {
      int puff = D_8006E1C8[g_Flame.m_Pattern][g_Flame.m_PuffsEmitted];
      g_Flame.m_PuffAge[puff] = pDeltaTime - i;
      g_Flame.m_PuffEndAge[puff] = 0x20;
      g_Flame.m_PuffState[puff] = FLAME_PUFF_NEW;
    }
    g_Flame.m_PuffsEmitted++;
  }

  /* Hold the breath animation's open-mouth frame while puffs come out. */
  if (g_Flame.m_PuffsEmitted < 30 && g_Spyro.m_headAnimation == 7 &&
      g_Spyro.m_nextHeadAnimation == 7 && g_Spyro.m_headAnimationFrame == 4)
    g_Spyro.m_headFrameProgress = 0;

  UpdateFlameOrigin();

  for (i = 0; i < 8; i++) {
    FlamePathPoint *path;
    int pathLen, maxAge;

    if (g_Flame.m_PuffAge[i] == 0) {
      deadPuffs++;
      continue;
    }

    if (i < 4) {
      pathLen = 24;
      maxAge = 32;
    } else {
      pathLen = 32;
      maxAge = 40;
    }

    if (g_Flame.m_PuffState[i] != FLAME_PUFF_NEW)
      g_Flame.m_PuffAge[i] += pDeltaTime;
    if (g_Flame.m_PuffAge[i] >= maxAge) {
      g_Flame.m_PuffAge[i] = 0;
      continue;
    }
    if (g_Flame.m_PuffState[i] == FLAME_PUFF_HIT)
      continue;

    path = i < 4 ? D_8006DAA8[i] : D_8006DDA8[i - 4];

    if (g_Flame.m_PuffAge[i] < pathLen - 1) {
      /* Step along the path and test the ray from the last position. */
      FlamePathToWorld(&pos, path, g_Flame.m_PuffAge[i] + 1);
      if (g_Flame.m_PuffState[i] == FLAME_PUFF_NEW) {
        VecCopy(&from, &g_Spyro.m_Position);
        g_Flame.m_PuffState[i] = FLAME_PUFF_FLYING;
      } else {
        VecCopy(&from, &g_Flame.m_PuffLastPos[i]);
      }
      VecCopy(&g_Flame.m_PuffLastPos[i], &pos);

      if (ColMoby(&pos, 0xA0, NULL, g_Flame.m_IsSuperFlame ? 0x90000 : 0x10000, NULL, 0) == 0 &&
          ColTraceRay(&from, &pos) == 0)
        continue;

      g_Flame.m_PuffState[i] = FLAME_PUFF_HIT;
      g_Flame.m_PuffEndAge[i] = g_Flame.m_PuffAge[i];

      /* from is reused as the flame particle's direction: back and to a
       * random side, flat in head space. */
      from.x = -0x10;
      from.y = (rand() & 0x1F) - 0x10;
      from.z = 0;
      VecRotateByMatrix((MATRIX *)&g_Flame.m_HeadRotation, &from, &from);
      from.z = 0;
      spark.x = 0x30;
      spark.y = 0;
      spark.z = 0;
      SpawnPuffEnd(&pos, &from, 1, &spark);

      if (!playedSound) {
        playedSound = 1;
        PlaySound(g_Spu.m_SoundTable->spyroFlame, (Moby *)&g_Spyro, 4, NULL);
      }
    } else if (g_Flame.m_PuffAge[i] > pathLen && g_Flame.m_PuffAge[i] <= pathLen + pDeltaTime) {
      /* Just ran off the end of its path, this update: burst there. */
      FlamePathToWorld(&pos, path, pathLen - 1);
      VecNull(&from);
      SpawnPuffEnd(&pos, &from, g_Flame.m_IsSuperFlame ? 1 : 0, &D_8006E1D8[i]);
    }
  }

  if (deadPuffs == 8)
    g_Flame.m_IsFlameActive = 0;
}
#endif

#if !M2C_func_80043FE4
/* pete.c */
void func_8003D3B8(int speed);
void func_8003D52C(int pTurn);
void func_8003D978(void);

extern signed char D_8006C704[13]; // Flight turn momentum -6..6 -> turn rate
#define FLIGHT_TURN_RATE(momentum) (D_8006C704[(momentum) + 6])

static void TickInvulnerability(void) {
  if (--g_Spyro.m_invulverabilityTimer < 0)
    g_Spyro.m_invulverabilityTimer = 0;
}

/* pete.c. Both end by calling func_80017928 and the asm reads what it left
 * in v0 (the decomp declares them void): see TurnLeft. */
void ApplySpyroSoftTurn(void);
void UpdateSpyroTurnMomentum(int pTableIndex);
void func_8003EA68(int pNewState);
void func_8003E90C(void);
void TurnBodyToVelocity(int pMaxTurnSpeed, int pDeadzone);
void func_8003D92C(int pAddSpeed, int pSubtractSpeed);

extern Moby *D_80075804;           // Moby Spyro turns to face (pete.c)
extern int D_800756B4;             // Jump held without the button (cutscenes)
extern u_char D_8006C6F8[12];      // Turn-on-the-spot rate by ticks in state

extern Moby *D_80075790;           // Moby a charge homes in on

/* The slope's gravity, written by ApplySlopeGravity (pete.c) over
 * m_TargetSpeedAngle.m_RotY, m_RotX and m_SlopeGravityZ. */
#define SLOPE_GRAVITY(ph) ((Vector3D *)&(ph)->m_TargetSpeedAngle.m_RotY)

static inline int AngleDelta(int delta) {
  delta &= 0xFFF;
  if (delta > 0x800)
    delta -= 0x1000;
  return delta;
}

static inline int Abs(int v) { return v < 0 ? -v : v; }

/* What's left of Spyro's turn (0..0x800). UpdateSpyroTurnMomentum and
 * ApplySpyroSoftTurn end with this same call, and the asm uses its v0 as
 * their result; func_80017928 is pure, so calling it again here gives the
 * same value without relying on the host ABI. */
static int TurnLeft(void) {
  return func_80017928(g_Spyro.m_Physics.m_TargetSpeedAngle.m_RotZ, g_Spyro.m_Physics.m_SpeedAngle.m_RotZ);
}

/* Turn towards the target heading: gently at low speed, else with momentum
 * from turn-rate row 1 (under 0xC80) or 2. Returns TurnLeft(). */
static int TurnWhileMoving(void) {
  int speed = g_Spyro.m_Physics.m_SpeedAngle.m_Speed;

  if (speed < 0x400)
    ApplySpyroSoftTurn();
  else
    UpdateSpyroTurnMomentum(speed < 0xC80 ? 1 : 2);
  return TurnLeft();
}

/* Up to speed while the turn is small (0x140 a tick), down by 0xC0. */
static void AccelerateUnlessTurning(int pTurnLeft) {
  func_8003D92C(pTurnLeft <= 0x200 ? 0x140 : 0, 0xC0);
}

static void SlowDown(int pAmount) {
  g_Spyro.m_Physics.m_SpeedAngle.m_Speed -= pAmount;
  if (g_Spyro.m_Physics.m_SpeedAngle.m_Speed < 0)
    g_Spyro.m_Physics.m_SpeedAngle.m_Speed = 0;
}

/* Against a wall: cancel the part of the acceleration that goes into it
 * (the wall normal, flattened). Returns 1 if anything was cancelled. */
static int PushOffWall(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D *wall = &g_Spyro.m_wallAgainstSpyro;
  int into;

  wall->z = 0;
  func_80017330(wall, 0x1000);
  into = (-ph->m_Acceleration.x * wall->x - ph->m_Acceleration.y * wall->y) >> 12;
  if (into <= 0)
    return 0;
  VecScaleToLength(wall, 0x1000, into);
  VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, wall);
  return 1;
}

/* Falling: a fixed pull relative to the true velocity; on the ground the
 * slope's gravity vector (unk_0xdc). */
static void ApplyFall(void) {
  g_Spyro.m_Physics.m_Acceleration.z = g_Spyro.m_Physics.m_TrueVelocity.z - 0xC0;
}

static void ApplyGroundGravity(void) {
  VecAdd(&g_Spyro.m_Physics.m_Acceleration, &g_Spyro.m_Physics.m_Acceleration, &g_Spyro.m_Physics.unk_0xdc);
}

static void ApplyGravity(void) {
  if (g_Spyro.m_onEdge || g_Spyro.m_airTime)
    ApplyFall();
  else
    ApplyGroundGravity();
}

/* Spyro's acceleration decays: unk_0xe8 loses 1/16 per tick and becomes the
 * acceleration, plus gravity (unk_0xdc) on the ground, or a fixed fall
 * relative to his true velocity on an edge or in the air. */
static void DecayAcceleration(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  ph->unk_0xe8.x -= ph->unk_0xe8.x >> 4;
  ph->unk_0xe8.y -= ph->unk_0xe8.y >> 4;
  ph->unk_0xe8.z -= ph->unk_0xe8.z >> 4;
  VecCopy(&ph->m_Acceleration, &ph->unk_0xe8);
  ApplyGravity();
}

/* Standing states: steer from the stick at speed 0, or, while a cutscene
 * makes Spyro flame (unk_0x194), press circle for him once he's been idle
 * 45 ticks. */
static void UpdateIdle(void) {
  TickInvulnerability();
  if (g_Spyro.unk_0x194 == 0) {
    func_8003D3B8(0);
  } else if (g_Spyro.m_idleTimer >= 45 && !g_SpyroFlame.m_IsFlameActive) {
    if (g_Spyro.m_walkingState != 0) {
      g_Spyro.unk_0x194 = 0;
    } else {
      g_Spyro.m_walkingState = 1;
      g_Pad.m_Down |= PAD_CIRCLE;
    }
  }
  VecNull(&g_Spyro.m_Physics.m_Acceleration);
}

/* Walking and running (states 1, 2, 21). In a cutscene (unk_0x194) Spyro
 * walks to his target heading and stops once facing it. */
static void UpdateRunning(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int turnLeft;

  TickInvulnerability();
  if (g_Spyro.unk_0x194 == 0)
    func_8003D3B8(ph->unk_0x144);
  else if (Abs(AngleDelta(ph->m_TargetSpeedAngle.m_RotZ - ph->m_SpeedAngle.m_RotZ)) < 0x10)
    func_8003EA68(0);

  g_Spyro.m_walkingState = 0;
  turnLeft = TurnWhileMoving();
  if (g_Spyro.m_walkingState) /* A sharp soft turn: brake */
    SlowDown(0xC0);
  else
    AccelerateUnlessTurning(turnLeft);

  func_8003D978();
  if (g_Spyro.m_againstWall && PushOffWall())
    ph->m_SpeedAngle.m_Speed = VecMagnitude(&ph->m_Acceleration, 1);
  ApplyGravity();
}

/* State 3: moving, or (m_walkingState 7, no direction pressed) standing and
 * turning to face D_80075804. */
static void UpdateState3(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int turnLeft;

  TickInvulnerability();
  if (g_Pad.m_NoMovementButtonPressed && g_Spyro.m_walkingState == 7) {
    ph->m_TargetSpeedAngle.m_Speed = 0;
    if (D_80075804 != NULL)
      ph->m_TargetSpeedAngle.m_RotZ = Atan2(D_80075804->m_Position.x - g_Spyro.m_Position.x,
                                            D_80075804->m_Position.y - g_Spyro.m_Position.y, 1);
  } else {
    func_8003D3B8(ph->unk_0x144);
  }
  if (g_Spyro.m_walkingState != 7)
    g_Spyro.m_walkingState = 0;

  turnLeft = TurnWhileMoving();
  if (g_Spyro.m_onEdge)
    ph->m_SpeedAngle.m_Speed = 0;
  else if (g_Spyro.m_walkingState)
    SlowDown(0xC0);
  else
    AccelerateUnlessTurning(turnLeft);

  func_8003D978();
  if (g_Spyro.m_againstWall && PushOffWall())
    ph->m_SpeedAngle.m_Speed = VecMagnitude(&ph->m_Acceleration, 1);
  if (g_Spyro.m_airTime)
    ApplyFall();
  else
    ApplyGroundGravity();
}

/* State 4: turning on the spot, slowing to a stop. */
static void UpdateTurnOnSpot(int pTicksInState) {
  int turnLeft;

  TickInvulnerability();
  turnLeft = TurnLeft();
  if (turnLeft < 0x80)
    func_8003D52C(turnLeft >> 3);
  else
    func_8003D52C(D_8006C6F8[pTicksInState < 12 ? pTicksInState : 11]);

  if (g_Spyro.m_onEdge)
    g_Spyro.m_Physics.m_SpeedAngle.m_Speed = 0;
  else
    SlowDown(0x100);
  func_8003D978();
  ApplyGroundGravity();
}

/* State 5: jumping. Steering pulls the horizontal momentum (unk_0xe8)
 * towards full speed along the stick, at most 0x280 a tick; holding cross
 * for the first 13 ticks keeps lifting him. */
static void UpdateJump(int pTicksInState) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D pull;
  int length;

  TickInvulnerability();
  func_8003D3B8(0xC80);
  UpdateSpyroTurnMomentum(2);
  func_8003D92C(0x140, 0xC0);

  ph->unk_0xe8.x = ph->m_SpeedAngle.m_Speed * Cos(ph->m_TargetSpeedAngle.m_RotZ) >> 12;
  ph->unk_0xe8.y = ph->m_SpeedAngle.m_Speed * Sin(ph->m_TargetSpeedAngle.m_RotZ) >> 12;
  VecSub(&pull, &ph->unk_0xe8, &ph->m_Acceleration);
  pull.z = 0;
  length = VecMagnitude(&pull, 0);
  if (length > 0x280)
    VecScaleToLength(&pull, length, 0x280);
  VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, &pull);
  if (g_Spyro.m_againstWall)
    PushOffWall();

  if (pTicksInState < 13 && g_Spyro.m_walkingState == 0 &&
      ((g_PadFrame->m_Held & PAD_CROSS) || D_800756B4)) {
    /* Hold the jump: aim the vertical acceleration at the jump's target
     * height (m_highestFlightPoint) and move that target up. */
    int rise;
    ph->unk_0xe8.z += 0xD7 + ph->m_gravity;
    rise = (g_Spyro.m_highestFlightPoint - g_Spyro.m_Position.z) * 64 - ph->m_Velocity.z + ph->unk_0xe8.z;
    ph->m_Acceleration.z = rise;
    g_Spyro.m_highestFlightPoint = (g_Spyro.m_Position.z * 64 + rise + ph->m_Velocity.z) >> 6;
  } else {
    ph->unk_0xe8.z += ph->m_gravity;
    ph->m_Acceleration.z = ph->unk_0xe8.z;
  }
}

/* State 6: in the air. Gnasty's Loot's flying ability turns at most 0x20 a
 * tick; air drag of up to 0x40 against the horizontal acceleration, gravity,
 * a nudge off a wall Spyro is stuck against. */
static void UpdateAirborne(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D drag;
  int length;

  TickInvulnerability();
  if (g_Spyro.m_flyingAbility) {
    int turn;
    func_8003D3B8(ph->unk_0x144);
    turn = AngleDelta(ph->m_TargetSpeedAngle.m_RotZ - ph->m_SpeedAngle.m_RotZ);
    if (turn > 0x20)
      turn = 0x20;
    if (turn < -0x20)
      turn = -0x20;
    func_8003D52C(turn);
  }

  if (ph->m_TrueSpeed < 0x80) {
    VecCopy(&drag, &g_Spyro.m_wallAgainstSpyro);
    VecShiftRight(&drag, 3);
    VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, &drag);
  }

  VecNull(&drag);
  VecSub(&drag, &drag, &ph->m_Acceleration);
  drag.z = 0;
  length = VecMagnitude(&drag, 0);
  if (length > 0x40)
    VecScaleToLength(&drag, length, 0x40);
  drag.z = ph->m_gravity;
  VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, &drag);

  if (g_Spyro.m_floorIdleTime == 0 && g_Spyro.m_slopeAngle < 0x20)
    func_8003E90C();

  if (ph->m_Acceleration.z < -0x2300)
    ph->m_Acceleration.z = -0x2300;
  if (g_Spyro.m_againstWall)
    PushOffWall();
}

/* States 9 and 10: sidestepping, left and right of Spyro's heading. */
static void UpdateSidestep(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  TickInvulnerability();
  func_8003D3B8(0x1000);
  UpdateSpyroTurnMomentum(3);
  ph->m_TargetSpeedAngle.m_Speed = 0x1000;
  func_8003D92C(0x140, 0xC0);

  ph->m_Acceleration.x = 0;
  ph->m_Acceleration.z = 0;
  ph->m_Acceleration.y = g_Spyro.m_State == 10 ? -ph->m_SpeedAngle.m_Speed : ph->m_SpeedAngle.m_Speed;
  VecRotateByMatrix(&g_Spyro.m_RotationMatrix, &ph->m_Acceleration, &ph->m_Acceleration);
  ph->unk_0xe8.x -= ph->unk_0xe8.x >> 4;
  ph->unk_0xe8.y -= ph->unk_0xe8.y >> 4;
  ph->unk_0xe8.z -= ph->unk_0xe8.z >> 4;
  VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, &ph->unk_0xe8);
  ApplyGravity();
}

/* Clamp to the flight turn-rate table's ends (D_8006C704[0], [12]). */
static int ClampTurn(int pTurn) {
  if (pTurn < D_8006C704[0])
    pTurn = D_8006C704[0];
  if (pTurn > D_8006C704[12])
    pTurn = D_8006C704[12];
  return pTurn;
}

/* Supercharge, and flight-style speed from height: speed from the drop
 * below the highest point, 0x1F80 on the level. */
static void UpdateSuperchargeSpeed(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int drop = g_Spyro.m_highestFlightPoint - g_Spyro.m_Position.z;

  if (drop >= 0)
    ph->m_SpeedAngle.m_Speed = func_80017A38(drop << 13) + 0x1F80;
  else
    ph->m_SpeedAngle.m_Speed = 0x1F80 - func_80017A38(-drop << 13);
  if (ph->m_SpeedAngle.m_Speed > 0x5900)
    ph->m_SpeedAngle.m_Speed = 0x5900;
  if (ph->m_SpeedAngle.m_Speed < 0x1400) {
    ph->m_TargetSpeedAngle.m_Speed = 0x1400;
    g_Spyro.m_highestFlightPoint = g_Spyro.m_Position.z;
  }

  func_8003D978();
  VecCopy(&ph->unk_0xe8, &ph->m_Acceleration);
  if (g_Spyro.m_onEdge)
    ph->m_Acceleration.z -= 0xC0;
  else
    ApplyGroundGravity();
}

/* The stick, or the d-pad as a full stick: x right, y up. */
static void ReadStick(int *pX, int *pY) {
  if (g_PadFrame->m_LeftStickMoved) {
    *pX = g_PadFrame->m_Sticks.m_LeftX - 0x7F;
    *pY = 0x7F - g_PadFrame->m_Sticks.m_LeftY;
  } else {
    int held = g_PadFrame->m_Held;
    *pY = (held & PAD_UP) ? 0x7F : (held & PAD_DOWN) ? -0x7F : 0;
    *pX = (held & PAD_RIGHT) ? 0x7F : (held & PAD_LEFT) ? -0x7F : 0;
  }
}

/* Steer like a vehicle: the stick's sideways part in Spyro's frame (the
 * camera's view) sets the turn momentum. */
static void SteerFromStick(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int stickX, stickY, heading, cosH, sinH;

  ReadStick(&stickX, &stickY);
  heading = (ph->m_SpeedAngle.m_RotZ - g_Camera.m_Rotation.z) & 0xFFF;
  cosH = Cos(heading);
  sinH = Sin(heading);
  ph->m_TurnMomentum = ClampTurn((-stickX * cosH - stickY * sinH) >> 13);
  func_8003D52C(ph->m_TurnMomentum);
}

/* Horizontal acceleration at m_Speed along the heading. */
static void AccelerateAlongHeading(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  ph->m_Acceleration.x = ph->m_SpeedAngle.m_Speed * Cos(ph->m_SpeedAngle.m_RotZ) >> 12;
  ph->m_Acceleration.y = ph->m_SpeedAngle.m_Speed * Sin(ph->m_SpeedAngle.m_RotZ) >> 12;
}

/* State 11: charging. While charge is held (m_walkingState bit 0) the
 * stick or d-pad, taken relative to the camera, steers; the charge homes in
 * on D_80075790 for its first 120 ticks if it's ahead. Released, Spyro slows
 * at 0x180 a tick, capped at 0x2300. */
static void UpdateCharge(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  TickInvulnerability();
  if (g_Spyro.m_walkingState & 1) {
    SteerFromStick();

    if (D_80075790 != NULL && g_Spyro.m_idleTimer < 120) {
      Vector3D toTarget;
      int distance, turn;

      if (Abs(AngleDelta(ph->m_SpeedAngle.m_RotZ - g_Camera.m_Rotation.z)) >= 0x200) {
        D_80075790 = NULL;
      } else {
        VecSub(&toTarget, &D_80075790->m_Position, &g_Spyro.m_Position);
        distance = VecMagnitude(&toTarget, 1);
        turn = AngleDelta(Atan2(toTarget.x, toTarget.y, 1) - ph->m_SpeedAngle.m_RotZ);
        if (Abs(turn) < 0x200 && distance < 0x1800)
          func_8003D52C(ClampTurn(turn >> 2));
        else
          D_80075790 = NULL;
      }
    }

    if (g_Spyro.m_doingSupercharge) {
      UpdateSuperchargeSpeed();
    } else {
      /* Momentum (unk_0xe8) loses 300/4096 a tick and takes the
       * acceleration; speed is fixed at 0x240 on top. */
      Vector3D decay;
      decay.x = -ph->unk_0xe8.x * 300 >> 12;
      decay.y = -ph->unk_0xe8.y * 300 >> 12;
      decay.z = -ph->unk_0xe8.z * 300 >> 12;
      VecAdd(&ph->unk_0xe8, &ph->unk_0xe8, &decay);
      ph->m_SpeedAngle.m_Speed = 0x240;
      func_8003D978();
      VecAdd(&ph->unk_0xe8, &ph->unk_0xe8, &ph->m_Acceleration);
      VecAdd(&ph->unk_0xe8, &ph->unk_0xe8, SLOPE_GRAVITY(ph));
      ph->m_SpeedAngle.m_Speed = VecMagnitude(&ph->unk_0xe8, 1);
      VecAdd(&ph->m_Acceleration, &ph->unk_0xe8, &ph->unk_0xdc);
    }
  } else {
    int speed;

    ph->m_TargetSpeedAngle.m_Speed = 0;
    func_8003D92C(0, 0x180);
    func_8003D978();
    VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, SLOPE_GRAVITY(ph));
    speed = ph->m_SpeedAngle.m_Speed = VecMagnitude(&ph->m_Acceleration, 1);
    if (speed > 0x2300) {
      VecScaleToLength(&ph->m_Acceleration, speed, 0x2300);
      ph->m_SpeedAngle.m_Speed = 0x2300;
    }
    if (g_Spyro.m_onEdge)
      ph->m_Acceleration.z -= 0xC0;
    else
      ApplyGroundGravity();
  }
}

/* math.s: pVec = pVec * pMul >> pShift, rounded towards zero. */
void func_80017614(Vector3D *pVec, int pMul, int pShift);

static void ClampFall(void) {
  if (g_Spyro.m_Physics.m_Acceleration.z < -0x2300)
    g_Spyro.m_Physics.m_Acceleration.z = -0x2300;
}

/* States 7 and 14: hurt. Speed runs down; falls under gravity. */
static void UpdateHurt(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  ph->m_TargetSpeedAngle.m_Speed = 0;
  func_8003D92C(0, 0xC0);
  func_8003D978();
  ApplyFall();
  ClampFall();
}

/* State 12: knocked back. No upward acceleration; after 19 ticks the
 * horizontal push fades to 7/8. */
static void UpdateKnockback(int pTicksInState) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  TickInvulnerability();
  if (ph->m_Acceleration.z < 0)
    ph->m_Acceleration.z = 0;
  if (pTicksInState >= 19)
    func_80017614(&ph->m_Acceleration, 7, 3);
  ph->m_Acceleration.z += ph->m_gravity;
}

/* States 25 and 27: thrown about (27 spins). Falls fast in the air, stops on
 * the ground. */
static void UpdateTumble(int pSpin) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  if (g_Spyro.m_airTime) {
    ph->m_Acceleration.z -= 0x600;
    ClampFall();
  } else {
    VecNull(&ph->m_Acceleration);
  }
  if (pSpin)
    ph->m_SpeedAngle.m_RotZ += 0x55;
}

/* State 16: falling with gravity only. */
static void UpdateFall(void) {
  TickInvulnerability();
  g_Spyro.m_Physics.m_Acceleration.z += g_Spyro.m_Physics.m_gravity;
  ClampFall();
}

extern int D_80075668; // Whirlwind: bottom height
extern int D_8007578C; // Whirlwind: spiral angle
extern int D_800757F0; // Whirlwind: spiral turns ...
extern int D_80075724; // ... over this height

static_assert(offsetof(Moby, m_Class) == 0x36, "Moby.m_Class");
static_assert(offsetof(Moby, m_Rotation.z) == 0x46, "Moby.m_Rotation.z");

/* State 17: carried by a whirlwind (m_mobyInUseBySpyro, centre unk_0x17c):
 * pulled in at the bottom, spiralled up, thrown out along the moby's
 * heading at the top. The spiral's radius is 0x500, or the class-300
 * whirlwind's props word 1. */
static void UpdateWhirlwind(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Moby *wind = g_Spyro.m_mobyInUseBySpyro;
  Vector3D pull;
  int radius, rise, heading, along;

  TickInvulnerability();
  if (wind == NULL)
    return;

  radius = wind->m_Class == 300 ? ((int *)wind->m_Props)[1] : 0x500;

  if (g_Spyro.m_Position.z < D_80075668 + 0x400) {
    /* Bottom: rise gathers speed; aim at the centre. */
    int height;
    rise = ph->unk_0xe8.z + 0x80;
    if (rise > 0x12C0)
      rise = 0x12C0;
    VecCopy(&pull, &g_Spyro.unk_0x17c);
    height = g_Spyro.m_Position.z - D_80075668;
    g_Spyro.m_walkingState = 0;
    ph->unk_0x144 = 0x400;
    heading = (height * height >> 11) + D_8007578C;
  } else if (g_Spyro.m_Position.z < g_Spyro.m_highestFlightPoint - 0x400) {
    /* Middle: circle the centre, wider with height up to the radius. */
    if (g_Spyro.m_walkingState <= 0) {
      g_Spyro.m_walkingState = 1;
      D_8007578C += 0x200;
    }
    along = g_Spyro.m_Position.z - D_80075668 - 0x400;
    ph->unk_0x144 = 0xC80;
    heading = (D_8007578C + D_800757F0 * along / D_80075724 + ((int)((u_int)along << 10) >> 10)) & 0xFFF;
    rise = 0x12C0;
    if (radius < along)
      along = radius;
    pull.x = along * Cos(ph->m_SpeedAngle.m_RotZ) >> 12;
    pull.y = along * Sin(ph->m_SpeedAngle.m_RotZ) >> 12;
    pull.z = 0; /* (the asm leaves it unset; it's zeroed before use) */
    VecAdd(&pull, &pull, &g_Spyro.unk_0x17c);
  } else {
    /* Top: out past the rim, further when facing the wind's way. */
    int toThrow = AngleDelta(ph->m_SpeedAngle.m_RotZ - (wind->m_Rotation.z << 4));
    int below;
    along = radius;
    if (Abs(toThrow) < 0x100)
      along += 0x800;
    pull.x = along * Cos(ph->m_SpeedAngle.m_RotZ) >> 12;
    pull.y = along * Sin(ph->m_SpeedAngle.m_RotZ) >> 12;
    pull.z = 0;
    VecAdd(&pull, &pull, &g_Spyro.unk_0x17c);
    below = g_Spyro.m_highestFlightPoint - g_Spyro.m_Position.z;
    rise = below * 4800 >> 10;
    ph->unk_0x144 = 0xC80;
    heading = (wind->m_Rotation.z << 4) - (below * below >> 11);
  }

  /* Move towards pull, horizontally, at most unk_0x144 a tick. */
  pull.z = 0;
  VecSub(&pull, &pull, &g_Spyro.m_Position);
  along = VecMagnitude(&pull, 0);
  if (along > ph->unk_0x144)
    VecScaleToLength(&pull, along, ph->unk_0x144);
  VecCopy(&ph->unk_0xe8, &pull);
  VecCopy(&ph->m_Acceleration, &ph->unk_0xe8);
  ph->unk_0xe8.z = rise;
  ph->m_Acceleration.z = rise;
  ph->m_SpeedAngle.m_RotZ = heading & 0xFFF;
}

/* State 19: sliding down a slope. Friction of up to 0x100 against the
 * momentum on gentle slopes; the slope's gravity; capped at 0x2300. */
static void UpdateSlide(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D friction;
  int length;

  TickInvulnerability();
  TurnBodyToVelocity(8, 4);
  if (g_Spyro.m_slopeAngle < 0x17) {
    VecNull(&friction);
    VecSub(&friction, &friction, &ph->unk_0xe8);
    length = VecMagnitude(&friction, 1);
    if (length > 0x100)
      VecScaleToLength(&friction, length, 0x100);
    VecAdd(&ph->unk_0xe8, &ph->unk_0xe8, &friction);
  }
  VecAdd(&ph->unk_0xe8, &ph->unk_0xe8, SLOPE_GRAVITY(ph));
  length = ph->m_SpeedAngle.m_Speed = VecMagnitude(&ph->unk_0xe8, 1);
  if (length > 0x2300) {
    VecScaleToLength(&ph->unk_0xe8, length, 0x2300);
    ph->m_SpeedAngle.m_Speed = 0x2300;
  }
  VecAdd(&ph->m_Acceleration, &ph->unk_0xe8, &ph->unk_0xdc);
}

/* State 20: flying with the flying ability (Gnasty's Loot), steered like a
 * vehicle. With m_walkingState bit 0x40, speed comes from the height below
 * the highest point; else it eases to 0x1E00. */
static void UpdateFlyingAbility(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  TickInvulnerability();
  SteerFromStick();
  if (g_Spyro.m_walkingState & 0x40) {
    int climb = g_Spyro.m_highestFlightPoint - g_Spyro.m_Position.z;
    if (climb >= 0)
      ph->m_SpeedAngle.m_Speed = ph->m_TargetSpeedAngle.m_Speed + func_80017A38(climb << 13);
    else
      ph->m_SpeedAngle.m_Speed = ph->m_TargetSpeedAngle.m_Speed - func_80017A38(-climb << 13);
    if (ph->m_SpeedAngle.m_Speed > 0x5900)
      ph->m_SpeedAngle.m_Speed = 0x5900;
    AccelerateAlongHeading();
    VecCopy(&ph->unk_0xe8, &ph->m_Acceleration);
  } else {
    ph->m_TargetSpeedAngle.m_Speed = 0x1E00;
    func_8003D92C(0x240, 0x180);
    AccelerateAlongHeading();
  }
  if (g_Spyro.m_againstWall)
    PushOffWall();
  ph->m_Acceleration.z -= 0xC0;
  ClampFall();
}

/* States 22 and 28: stopping. Drag against the acceleration, up to 0x40 in
 * the air (then a fixed fall) or 0x100 on the ground (then gravity). */
static void UpdateStop(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D drag;
  int length;

  VecNull(&drag);
  VecSub(&drag, &drag, &ph->m_Acceleration);
  if (g_Spyro.m_airTime) {
    drag.z = 0;
    length = VecMagnitude(&drag, 0);
    if (length > 0x40)
      VecScaleToLength(&drag, length, 0x40);
    drag.z = -0xC0;
    VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, &drag);
  } else {
    length = VecMagnitude(&drag, 1);
    if (length > 0x100)
      VecScaleToLength(&drag, length, 0x100);
    VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, &drag);
    ApplyGroundGravity();
  }
  ClampFall();
}

/* State 24: gliding, steered like a vehicle at a steady speed; after 5
 * ticks gravity starts to win. */
static void UpdateGlide(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  TickInvulnerability();
  SteerFromStick();
  AccelerateAlongHeading();
  VecCopy(&ph->unk_0xe8, &ph->m_Acceleration);
  if (g_Spyro.m_idleTimer >= 5)
    ph->m_Acceleration.z -= ph->m_gravity;
  ClampFall();
}

extern int D_800758A0; // Circling speed around m_mobyInUseBySpyro
extern int g_PortalLevelId;

/* State 26: circling a moby (m_mobyInUseBySpyro) at the same distance,
 * turning it with Spyro; m_walkingState 1 goes the other way. */
static void UpdateCircleMoby(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Moby *moby = g_Spyro.m_mobyInUseBySpyro;
  int *props = (int *)moby->m_Props;
  Vector3D offset;
  int distance, around, step;

  TickInvulnerability();
  VecSub(&offset, &moby->m_Position, &g_Spyro.m_Position);
  distance = VecMagnitude(&offset, 0);
  around = Atan2(offset.x, offset.y, 1);
  step = (g_Spyro.m_walkingState == 1 ? -D_800758A0 : D_800758A0) >> 6;

  func_8003D3B8(0x280);
  UpdateSpyroTurnMomentum(0);
  D_800758A0 += 0x20;
  if (ph->m_TargetSpeedAngle.m_Speed < D_800758A0)
    D_800758A0 = ph->m_TargetSpeedAngle.m_Speed;
  around = (around + step + 0x800) & 0xFFF;

  /* The moby's props angle at +0x14 turns with Spyro (the asm has the same
   * code for class 255 and the rest). */
  props[5] = (props[5] + step) & 0xFFF;
  moby->m_Rotation.z = props[5] >> 4;

  offset.x = distance * Cos(around) >> 12;
  offset.y = distance * Sin(around) >> 12;
  VecAdd(&offset, &offset, &moby->m_Position);
  VecSub(&offset, &offset, &g_Spyro.m_Position);
  offset.z = 0;
  VecShiftLeft(&offset, 6);
  VecCopy(&ph->m_Acceleration, &offset);
  ph->m_SpeedAngle.m_RotZ = (ph->m_SpeedAngle.m_RotZ + step) & 0xFFF;
  if (g_Spyro.m_onEdge)
    ph->m_Acceleration.z -= 0xC0;
  else
    ApplyGroundGravity();
}

static inline int Clamp(int v, int pMin, int pMax) { return v < pMin ? pMin : v > pMax ? pMax : v; }

/* As the asm computes it: shifted left first, so the top bits drop. */
static inline int ShiftUpDown(int v, int pUp, int pDown) { return (int)((u_int)v << pUp) >> pDown; }



/* Ease the speed to pTarget by 0x80 a tick, at least 0x780. */
static void EaseSpeedTo(int pTarget) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  if (ph->m_SpeedAngle.m_Speed < pTarget) {
    ph->m_SpeedAngle.m_Speed += 0x80;
    if (pTarget < ph->m_SpeedAngle.m_Speed)
      ph->m_SpeedAngle.m_Speed = pTarget;
  } else if (pTarget < ph->m_SpeedAngle.m_Speed) {
    ph->m_SpeedAngle.m_Speed -= 0x80;
    if (ph->m_SpeedAngle.m_Speed < pTarget)
      ph->m_SpeedAngle.m_Speed = pTarget;
  }
  if (ph->m_SpeedAngle.m_Speed < 0x780)
    ph->m_SpeedAngle.m_Speed = 0x780;
}

/* Roll towards wings level, at most 0x10 a tick: the step. */
static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), -0x10, 0x10); }

static void AddRoll(int pStep) {
  g_Spyro.m_Physics.m_SpeedAngle.m_RotX = (g_Spyro.m_Physics.m_SpeedAngle.m_RotX + pStep) & 0xFFF;
}

/* Bank into the turn (m_TurnMomentum), at most 0x10 a tick. */
static void BankIntoTurn(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), -0x10, 0x10));
}

/* Head for the camera state Spyro's state asks for, drawn in front. */
static void EndPortalFlight(void) {
  g_Spyro.m_walkingState = 0;
  g_Spyro.m_sortingDepth = 4;
  g_Camera.unk_0xC0 = D_8006C588[g_Spyro.m_State];
}

static void KeepGamepadLocked(void) {
  if (g_Spyro.m_noGamepadUpdateFrames <= 0)
    g_Spyro.m_noGamepadUpdateFrames = 1;
}

/* Gravity on top of the true velocity, falling at most 0x780. */
static void PortalFall(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  ph->m_Acceleration.z = ph->m_TrueVelocity.z + ph->m_gravity;
  if (ph->m_Acceleration.z < -0x780)
    ph->m_Acceleration.z = -0x780;
}

/* Pitch while flying out of a portal (m_walkingState 10): climb at first,
 * then level off over the floor and land. Returns the pitch step. */
static int PortalPitchStep(void) {
  int pitch = g_Spyro.m_Physics.m_SpeedAngle.m_RotY, step;

  if (g_Spyro.m_idleTimer < 16)
    return 0;

  step = ColFindFloor(&g_Spyro.m_Position, 0x10000);
  if (g_Spyro.unk_0x254) {
    int height = g_Spyro.m_Position.z - step;
    if (height < 0x200) {
      EndPortalFlight();
      return step; /* sic: the asm adds the floor height to the pitch */
    }
    if (height < 0x800) {
      step = AngleDelta(-0x100 - pitch) >> 2;
      return step < -0x3C ? -0x3C : step > 0x3C ? 0x3C : step;
    }
  }

  if ((u_int)(pitch + 0x100) <= 0x500) {
    g_Spyro.unk_0x254 = 0;
    return 0x1E;
  }
  g_Spyro.unk_0x254 = 1;
  step = AngleDelta(-0x400 - pitch);
  if (g_LevelId == 22 && Abs(step) < 0x100)
    g_Camera.unk_0xC0 = D_8006C588[g_Spyro.m_State];
  step >>= 4;
  return step < -0x1E ? -0x1E : step > 0x1E ? 0x1E : step;
}

/* States 15, 23, 32, 33 and 34 without the flying ability: flying through
 * a portal or into a level. The stick steers and sets the speed; the
 * m_walkingState phases are 8 turn to the portal's angle, 9 fly to its end,
 * 10 fly out and land, 11 fall. */
static void UpdatePortalFlight(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int stickX, stickY, heading, cosH, sinH, turn, ahead;

  ReadStick(&stickX, &stickY);
  heading = (ph->m_SpeedAngle.m_RotZ - g_Camera.m_Rotation.z) & 0xFFF;
  cosH = Cos(heading);
  sinH = Sin(heading);
  turn = (-stickX * cosH - stickY * sinH) >> 12;
  cosH = Cos(heading);
  sinH = Sin(heading);
  ahead = (stickY * cosH - stickX * sinH) >> 12;

  /* Back on the stick slows down to as little as 0x780. */
  ph->m_TargetSpeedAngle.m_Speed = ahead >= 0 ? 0x1900 : ((int)((u_int)(ahead * 35) << 7) >> 7) + 0x1900;
  EaseSpeedTo(ph->m_TargetSpeedAngle.m_Speed);

  ph->m_TurnMomentum = turn * 20 >> 7;
  ph->m_SpeedAngle.m_RotZ += ph->m_TurnMomentum; /* not wrapped here */
  g_Spyro.m_bodyRotation.z = ph->m_SpeedAngle.m_RotZ >> 4;

  switch (g_Spyro.m_walkingState) {
  case 8:
    if (g_PortalLevelId == 0) {
      int toPortal = AngleDelta((g_Spyro.m_portalAngle.z << 4) - ph->m_SpeedAngle.m_RotZ) * 16;
      ph->m_TurnMomentum = toPortal < -0x10 ? -0x10 : toPortal > 0x10 ? 0x10 : toPortal;
      ph->m_SpeedAngle.m_RotZ += ph->m_TurnMomentum;
    }
    VecNull(&ph->m_Acceleration);
    ph->m_SpeedAngle.m_RotY = 0;
    break;
  case 9:
    func_8003D978();
    ph->m_Acceleration.z = 0;
    g_Spyro.m_ControlFlags = 0x80004000;
    KeepGamepadLocked();
    if (OctDistance(&g_Spyro.m_Position, &g_Spyro.m_portalEndPos) < 0x300)
      EndPortalFlight();
    ph->m_SpeedAngle.m_RotY = 0;
    break;
  case 11:
    func_8003D978();
    PortalFall();
    ph->m_SpeedAngle.m_RotY = 0;
    KeepGamepadLocked();
    break;
  case 10: {
    int pitch = (ph->m_SpeedAngle.m_RotY + PortalPitchStep()) & 0xFFF;
    ph->m_SpeedAngle.m_RotY = pitch;
    g_Spyro.m_bodyRotation.y = pitch >> 4;
    if (pitch > 0x800)
      ph->m_SpeedAngle.m_RotY = pitch - 0x1000;
    func_8003D978();
    KeepGamepadLocked();
    break;
  }
  default:
    func_8003D978();
    PortalFall();
    ph->m_SpeedAngle.m_RotY = 0;
    break;
  }

  BankIntoTurn();
}

/* The flying ability's flight (flight levels, Gnasty's Loot): states 15,
 * 23, 32, 33 and 34 with m_flyingAbility set. */

/* The d-pad as a stick axis (-0x7F..0x7F): held, it moves 0x10 a tick
 * towards pPlus or pMinus; released, it eases back to 0 by up to 0x20. */
static void DpadAxis(int *pAxis, int pPlus, int pMinus) {
  int held = g_PadFrame->m_Held;

  if (held & pPlus) {
    *pAxis += 0x10;
    if (*pAxis >= 0x80)
      *pAxis = 0x7F;
  } else if (held & pMinus) {
    *pAxis -= 0x10;
    if (*pAxis < -0x7F)
      *pAxis = -0x7F;
  } else {
    *pAxis += Clamp(-*pAxis, -0x20, 0x20);
  }
}

/* Flying speed from the height below the highest point: 0x1040 level,
 * gaining with the square root of the drop, losing faster climbing. Returns
 * that height. */
static int SpeedFromHeight(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int below = g_Spyro.m_highestFlightPoint - g_Spyro.m_Position.z;

  if (below < 0)
    ph->m_SpeedAngle.m_Speed = 0x1040 - (func_80017A38(-below) << 7);
  else
    ph->m_SpeedAngle.m_Speed = (func_80017A38(below) << 6) + 0x1040;
  return below;
}

static void CapSpeed(void) {
  if (g_Spyro.m_Physics.m_SpeedAngle.m_Speed > 0x5900)
    g_Spyro.m_Physics.m_SpeedAngle.m_Speed = 0x5900;
}

/* A manoeuvre (loop, turn-around) is over: back to free flight with the
 * d-pad's stick centred. */
static void EndManoeuvre(void) {
  g_Spyro.m_walkingState = 0;
  D_800758A0 = 0;
  D_80075700 = 0;
}

extern Vector3D D_8006E7DC[]; // Flight levels, by world: the area's centre (x, y) and radius (z)

/* Flight levels: past the edge of the level's circle Spyro is pushed back
 * in, and turned towards the centre, the harder the further out. */
static void KeepInsideFlightArea(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D *area = &D_8006E7DC[g_LevelId / 10 - 1];
  Vector3D back;
  int distance, radius, over, turn;

  VecSub(&back, area, &g_Spyro.m_Position);
  VecShiftRight(&back, 4);
  distance = VecMagnitude(&back, 0);
  radius = area->z >> 4;
  if (distance <= radius)
    return;

  over = distance - radius;
  VecScaleToLength(&back, distance, over);
  VecShiftLeft(&back, 5);
  back.z = 0;
  VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, &back);
  turn = AngleDelta(Atan2(back.x, back.y, 1) - ph->m_SpeedAngle.m_RotZ) * over >> 15;
  ph->m_SpeedAngle.m_RotZ += Clamp(turn, -0x20, 0x20); /* not wrapped here */
}

/* m_walkingState 0: free flight. The stick (or the d-pad, eased) pitches
 * and turns; centred, Spyro levels out, nose down when slow. Below the
 * highest point, ground ahead pulls the nose up. Roll follows the turn on a
 * spring (D_80075960). */
static void FlyFree(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int stickX, stickY, pitchIn, cosPitch, pitchStep, turnStep, below, ceiling;

  if (g_PadFrame->m_LeftStickMoved) {
    stickX = g_PadFrame->m_Sticks.m_LeftX - 0x7F;
    stickY = 0x7F - g_PadFrame->m_Sticks.m_LeftY;
  } else {
    DpadAxis(&D_80075700, PAD_UP, PAD_DOWN);
    stickY = D_80075700;
    DpadAxis(&D_800758A0, PAD_RIGHT, PAD_LEFT);
    stickX = D_800758A0;
  }

  if (stickY != 0) {
    pitchIn = stickY;
  } else {
    pitchIn = ph->m_SpeedAngle.m_RotY >> 3;
    if (ph->m_SpeedAngle.m_Speed < 0x1040)
      pitchIn += (0x1040 - ph->m_SpeedAngle.m_Speed) >> 6;
    pitchIn = Clamp(pitchIn, -0x7F, 0x7F);
  }

  cosPitch = Cos(ph->m_SpeedAngle.m_RotY);
  pitchStep = pitchIn * -15 >> 7;
  turnStep = -stickX * 20 >> 7;

  if (g_Spyro.m_Position.z < g_Spyro.m_highestFlightPoint) {
    Vector3D ahead;
    ahead.z = (ph->m_Acceleration.z >> 3) - 0x400;
    if (ahead.z < 0) {
      ahead.x = ph->m_Acceleration.x >> 3;
      ahead.y = ph->m_Acceleration.y >> 3;
      VecAdd(&ahead, &ahead, &g_Spyro.m_Position);
      if (func_80033E40(&g_Spyro.m_Position, &ahead) == 0) { /* blocked */
        int lift = (g_Spyro.m_Position.z - g_CollisionPoint.z) >> 3;
        if (lift >= 8)
          lift = 7;
        pitchStep += lift;
      }
    }
  }

  ph->m_SpeedAngle.m_RotZ = (ph->m_SpeedAngle.m_RotZ + turnStep) & 0xFFF;
  ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + pitchStep);
  if (ph->m_SpeedAngle.m_RotY > 0x320)
    ph->m_SpeedAngle.m_RotY = 0x320;

  below = SpeedFromHeight();
  if (!g_IsFlightLevel)
    ph->m_SpeedAngle.m_Speed += 0x41E;
  CapSpeed();
  /* Can't dive steeper than the height below the highest point allows. */
  ceiling = (below + 0x132) >> 2;
  if (ceiling < 0)
    ceiling = 0;
  if (ceiling < ph->m_SpeedAngle.m_RotY)
    ph->m_SpeedAngle.m_RotY = ceiling;
  if (ph->m_SpeedAngle.m_RotY < -0x320)
    ph->m_SpeedAngle.m_RotY = -0x320;
  if (ph->m_SpeedAngle.m_Speed < 0x780)
    ph->m_SpeedAngle.m_Speed = 0x780;

  /* Roll spring: D_80075960 is the roll rate x 64, driven by the turn
   * (less when pitched), damped, and pulled back to level. */
  D_80075960 += (turnStep * -(cosPitch << 5) >> 12) - ShiftUpDown(D_80075960, 4, 6) -
                ShiftUpDown(ph->m_SpeedAngle.m_RotX, 6, 6);
  ph->m_SpeedAngle.m_RotX = AngleDelta(ph->m_SpeedAngle.m_RotX + (D_80075960 >> 6));
  ph->m_SpeedAngle.m_RotX = Clamp(ph->m_SpeedAngle.m_RotX, -0x300, 0x300);

  func_8003D978();
  if (g_IsFlightLevel)
    KeepInsideFlightArea();
}

/* m_walkingState 1: a loop, pitching up 0x1E a tick. unk_0x254 is set once
 * he's past vertical; nose level again, the loop is done. Too slow, it ends
 * early. */
static void FlyLoop(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  ph->m_SpeedAngle.m_RotZ &= 0xFFF;
  ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + 0x1E);
  if (ph->m_SpeedAngle.m_RotY > 0x400) {
    g_Spyro.unk_0x254 = 1;
  } else if (ph->m_SpeedAngle.m_RotY > 0) {
    if (g_Spyro.unk_0x254)
      g_Spyro.m_walkingState = 0;
    g_Spyro.unk_0x254 = 0;
  }
  AddRoll(RollToLevel());

  SpeedFromHeight();
  CapSpeed();
  if (ph->m_SpeedAngle.m_Speed < 0x1E00 && !g_Spyro.unk_0x254)
    EndManoeuvre();
  func_8003D978();
}

/* m_walkingState 2: turn around (half a loop, then roll upright). Pitching
 * up 0x1E a tick until upside down past vertical, where heading, pitch and
 * roll flip round (unk_0x254 set); then pitch back to level at the height
 * D_80075700 while rolling upright, D_800758A0 the roll rate. */
static void FlyTurnAround(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;

  if (g_Spyro.unk_0x254) {
    int toHeight = D_80075700 - g_Spyro.m_Position.z;
    int pitchStep = AngleDelta(-ph->m_SpeedAngle.m_RotY) + ((toHeight - (ph->m_TrueVelocity.z >> 5)) >> 2);

    ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(pitchStep, -0x1E, 0x1E));
    ph->m_SpeedAngle.m_RotZ &= 0xFFF;
    if (Abs(ph->m_SpeedAngle.m_RotX) < 0x40) {
      if (Abs(toHeight) < 0x100 && Abs(ph->m_SpeedAngle.m_RotY) < 0x80) {
        g_Spyro.unk_0x254 = 0;
        EndManoeuvre();
      }
      ph->m_SpeedAngle.m_RotX = 0;
    } else {
      D_800758A0 = Clamp(D_800758A0 + RollToLevel(), -0x40, 0x40);
      AddRoll(D_800758A0);
    }
    if (ph->m_SpeedAngle.m_RotX > 0x800)
      ph->m_SpeedAngle.m_RotX -= 0x1000;
  } else {
    ph->m_SpeedAngle.m_RotZ &= 0xFFF;
    ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + 0x1E);
    if (ph->m_SpeedAngle.m_RotY < -0x400) {
      g_Spyro.unk_0x254 = 1;
      ph->m_SpeedAngle.m_RotZ = (ph->m_SpeedAngle.m_RotZ + 0x800) & 0xFFF;
      ph->m_SpeedAngle.m_RotY = (0x800 - ph->m_SpeedAngle.m_RotY) & 0xFFF;
      D_800758A0 = 0;
      ph->m_SpeedAngle.m_RotX = (ph->m_SpeedAngle.m_RotX + 0x800) & 0xFFF;
    }
    AddRoll(RollToLevel());
  }

  SpeedFromHeight();
  CapSpeed();
  if (ph->m_SpeedAngle.m_Speed < 0x1E00 && !g_Spyro.unk_0x254 && ph->m_SpeedAngle.m_RotY < 0x400)
    EndManoeuvre();
  func_8003D978();
}

/* m_walkingState 3: climb out of the level, straight up, spinning, at speed
 * 0x1040; the camera stays where it is (state 0x8000000E). */
static void FlyClimbOut(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int roll;

  if (g_Camera.m_State != 0x8000000E) {
    VecCopy(&D_8006EBCC.m_CameraPosition, &g_Camera.m_Position);
    func_80037714(&D_8006EBCC);
  }

  ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(AngleDelta(0x400 - ph->m_SpeedAngle.m_RotY), -0x1E, 0x1E));
  ph->m_SpeedAngle.m_RotZ &= 0xFFF;
  roll = Abs(ph->m_SpeedAngle.m_RotY - 0x400) < 0x80 ? 0x40 : RollToLevel();
  AddRoll(roll);
  ph->m_SpeedAngle.m_Speed += Clamp(0x1040 - ph->m_SpeedAngle.m_Speed, -0x80, 0x80);
  func_8003D978();
}

/* m_walkingState 10: flying in (the level start, out of a portal), at speed
 * 0x1900, pitching up 0x1E a tick until a dive from above levels out
 * (unk_0x254 set once pitched down). */
static void FlyIn(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int pitch;

  ph->m_TargetSpeedAngle.m_Speed = 0x1900;
  EaseSpeedTo(0x1900);
  g_Spyro.m_bodyRotation.z = ph->m_SpeedAngle.m_RotZ >> 4;
  if (g_Spyro.unk_0x254) {
    if (ph->m_SpeedAngle.m_RotY >= -0xFF)
      EndPortalFlight();
  } else if (ph->m_SpeedAngle.m_RotY < 0) {
    g_Spyro.unk_0x254 = 1;
  }

  pitch = (ph->m_SpeedAngle.m_RotY + 0x1E) & 0xFFF;
  ph->m_SpeedAngle.m_RotY = pitch;
  g_Spyro.m_bodyRotation.y = pitch >> 4;
  if (pitch > 0x800)
    ph->m_SpeedAngle.m_RotY = pitch - 0x1000;
  func_8003D978();
  KeepGamepadLocked();
  BankIntoTurn();
}

/* Low over water (m_SurfaceProximityState): a wake on its surface,
 * particle 0x1B, bigger with speed (0x10000: Spyro isn't moving). */
static void SpawnWake(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  Vector3D at;
  int size;

  if (g_Spyro.m_SurfaceProximityState == 0 || g_Spyro.m_Position.z - g_Spyro.m_surfaceBelowSpyro > 0xC00)
    return;
  size = (ph->m_SpeedAngle.m_Speed >> 11) + 2;
  if (ph->m_Velocity.x == 0 && ph->m_Velocity.y == 0)
    size += 0x10000;
  VecCopy(&at, &ph->m_Velocity);
  VecShiftRight(&at, 6);
  at.x += g_Spyro.m_Position.x;
  at.y += g_Spyro.m_Position.y;
  at.z = g_Spyro.m_surfaceBelowSpyro;
  g_SpawnParticle(1, 0x1B, &at, size);
}

/* 42CC4.s: model vertex pVertex of Spyro's body in an animation frame. */
void func_80052F38(int pVertex, Vector3D *pOut, int pAnimation, int pFrame);
/* math.s: pOut = pA + (pB - pA) * pT / 0x1000, through the GTE. */
void func_80017894(Vector3D *pOut, Vector3D *pA, Vector3D *pB, int pT);

/* Vapour trails from both wing tips (model vertices 9 and 0x64),
 * particle 0x1A, lasting longer the faster he flies. */
static void SpawnWingTrails(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int moving = ph->m_Velocity.x != 0 || ph->m_Velocity.y != 0;
  int life, blend, vertex, i;

  life = (ph->m_SpeedAngle.m_Speed >> 11) - (moving - (g_Spyro.m_State == 32 ? 3 : 1));
  if (life <= 0)
    life = 1;
  blend = g_Spyro.m_bodyFrameProgress;
  if (moving && blend < 13)
    blend += 4;

  for (i = 0, vertex = 9; i < 2; i++, vertex += 0x5B) {
    Vector3D tip, other;

    func_80052F38(vertex, &tip, g_Spyro.m_bodyAnimation, g_Spyro.m_bodyAnimationFrame);
    if (blend != 0) {
      func_80052F38(vertex, &other, g_Spyro.m_nextBodyAnimation, g_Spyro.m_nextBodyAnimationFrame);
      func_80017894(&tip, &tip, &other, blend << 8);
    }
    VecRotateByMatrix(&g_Spyro.m_RotationMatrix, &tip, &tip);
    VecAdd(&tip, &tip, &g_Spyro.m_Position);
    VecCopy(&other, &ph->m_Velocity);
    VecShiftRight(&other, 6);
    VecAdd(&tip, &tip, &other);
    g_SpawnParticle(1, 0x1A, &tip, life);
  }
}

/* States 15, 23, 32, 33 and 34 with the flying ability: the phase by
 * m_walkingState (others just coast), then the wake and wing trails. */
static void UpdateAbilityFlight(void) {
  switch (g_Spyro.m_walkingState) {
  case 0:
    FlyFree();
    break;
  case 1:
    FlyLoop();
    break;
  case 2:
    FlyTurnAround();
    break;
  case 3:
    FlyClimbOut();
    break;
  case 10:
    FlyIn();
    break;
  }
  SpawnWake();
  SpawnWingTrails();
}

/* Flying (flight levels): turn from the stick, or ramp the turn momentum
 * with the d-pad; climbing trades speed, diving gains it. */
static void UpdateFlying(void) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int climb;

  TickInvulnerability();

  if (g_PadFrame->m_LeftStickMoved) {
    ph->m_TurnMomentum = ClampTurn((0x7F - g_PadFrame->m_Sticks.m_LeftX) >> 1);
    func_8003D52C(ph->m_TurnMomentum);
  } else {
    if (g_PadFrame->m_Held & PAD_LEFT) {
      if (ph->m_TurnMomentum < 0)
        ph->m_TurnMomentum = 0;
      if (ph->m_TurnMomentum < 6)
        ph->m_TurnMomentum++;
    } else if (g_PadFrame->m_Held & PAD_RIGHT) {
      if (ph->m_TurnMomentum > 0)
        ph->m_TurnMomentum = 0;
      if (ph->m_TurnMomentum >= -5)
        ph->m_TurnMomentum--;
    } else {
      ph->m_TurnMomentum = 0;
    }
    func_8003D52C(FLIGHT_TURN_RATE(ph->m_TurnMomentum));
  }

  /* Speed from the height below the highest point reached: v^2 = v0^2 + 2gh
   * style, through the square root func_80017A38. */
  climb = g_Spyro.m_highestFlightPoint - g_Spyro.m_Position.z;
  if (climb >= 0)
    ph->m_SpeedAngle.m_Speed = ph->m_TargetSpeedAngle.m_Speed + func_80017A38(climb << 13);
  else
    ph->m_SpeedAngle.m_Speed = ph->m_TargetSpeedAngle.m_Speed - func_80017A38(-climb << 13);
  if (ph->m_SpeedAngle.m_Speed > 0x5900)
    ph->m_SpeedAngle.m_Speed = 0x5900;
  if (ph->m_SpeedAngle.m_Speed < 0x1400) {
    ph->m_TargetSpeedAngle.m_Speed = 0x1400;
    g_Spyro.m_highestFlightPoint = g_Spyro.m_Position.z;
  }

  func_8003D978();
  VecCopy(&ph->unk_0xe8, &ph->m_Acceleration);
  if (g_Spyro.m_airTime == 0)
    VecAdd(&ph->m_Acceleration, &ph->m_Acceleration, &ph->unk_0xdc);

  /* Rumble with speed. */
  D_8007584C = ((ph->m_SpeedAngle.m_Speed - 0x1400) >> 7) + 20;
  if (D_8007584C < 0x50)
    D_8007584C = 0x50;
  if (D_8007584C > 0xA0)
    D_8007584C = 0xA0;
  if (D_800757D0 < 4)
    D_800757D0 = 4;
}

/// @brief Spyro's physics for one tick of input: sets his acceleration (and
/// for some states speed and heading) by state, then integrates velocity
/// and writes his body rotation.
/// @param pDeltaTimeIndex This tick's frame in the pad input buffer.
/// @note asm/nonmatchings/pete/func_80043FE4.s, by hand, state by state.
void func_80043FE4(int pDeltaTimeIndex) {
  SpyroPhysics *ph = &g_Spyro.m_Physics;
  int ticksInState;

  g_ActivePad = (Gamepad *)&g_Pad.m_BufferedInputs[pDeltaTimeIndex];
  ticksInState = g_Spyro.m_idleTimer + pDeltaTimeIndex;
  if (g_Gamestate == 0 && g_LevelId == 64 && g_Spyro.m_airTime == 0)
    g_Spyro.m_flyingAbility = 1; /* Gnasty's Loot */

  switch (g_Spyro.m_State) {
  case 1: case 2: case 21:
    UpdateRunning();
    break;
  case 3:
    UpdateState3();
    break;
  case 4:
    UpdateTurnOnSpot(ticksInState);
    break;
  case 5:
    UpdateJump(ticksInState);
    break;
  case 6:
    UpdateAirborne();
    break;
  case 9: case 10:
    UpdateSidestep();
    break;
  case 11:
    UpdateCharge();
    break;
  case 7: case 14:
    UpdateHurt();
    break;
  case 12:
    UpdateKnockback(ticksInState);
    break;
  case 25:
    UpdateTumble(0);
    break;
  case 27:
    UpdateTumble(1);
    break;
  case 16:
    UpdateFall();
    break;
  case 17:
    UpdateWhirlwind();
    break;
  case 19:
    UpdateSlide();
    break;
  case 20:
    UpdateFlyingAbility();
    break;
  case 22: case 28:
    UpdateStop();
    break;
  case 24:
    UpdateGlide();
    break;
  case 26:
    UpdateCircleMoby();
    break;
  case 15: case 23: case 32: case 33: case 34:
    TickInvulnerability();
    if (g_Spyro.m_flyingAbility)
      UpdateAbilityFlight();
    else
      UpdatePortalFlight();
    break;
  case 0: case 13: case 18: case 36: case 37: case 38: case 39: case 40:
  case 41: case 42: case 43:
    UpdateIdle();
    break;
  case 8: case 35:
    TickInvulnerability();
    break;
  case 29:
    VecNull(&ph->m_Acceleration);
    break;
  case 30: case 31:
    DecayAcceleration();
    break;
  case 44:
    UpdateFlying();
    break;
  }

  VecAdd(&ph->m_Velocity, &ph->m_Velocity, &ph->m_Acceleration);
  g_Spyro.m_bodyRotation.x = ph->m_SpeedAngle.m_RotX >> 4;
  g_Spyro.m_bodyRotation.z = ph->m_SpeedAngle.m_RotZ >> 4;
  g_Spyro.m_bodyRotation.y = ph->m_SpeedAngle.m_RotY >> 4;
}
#endif

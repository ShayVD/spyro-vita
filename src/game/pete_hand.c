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

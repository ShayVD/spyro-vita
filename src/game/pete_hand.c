/* Player-controller (pete) functions the decomp hasn't matched
 * (asm/nonmatchings/pete), written by hand from the asm so they read as C
 * (docs/porting-plan.md §7, M2): M4's 60 fps presentation needs to see what
 * Spyro's update does. Each one replaces its mips2c unit (cmake/mips2c.cmake
 * M2C_HAND) and is checked against PCSX-Redux with the oracle replay
 * (docs/translation-log.md). */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "collision.h"
#include "math.h"
#include "spyro.h"
#include "vector.h"

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

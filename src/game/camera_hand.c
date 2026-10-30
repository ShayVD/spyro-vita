/* Camera functions the decomp hasn't matched (asm/nonmatchings/camera),
 * written by hand from the asm so they read as C (docs/porting-plan.md §7,
 * M2): M4's 60 fps presentation needs to see what the camera does. Each one
 * replaces its mips2c unit (cmake/mips2c.cmake M2C_HAND) and is checked
 * against PCSX-Redux with the oracle replay (docs/translation-log.md).
 * M2C_<name>=1 compiles one out for the mips2c unit (cmake SPYRO_M2C_FUNCS,
 * A/B tests). */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "camera.h"
#include "collision.h"
#include "math.h"
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

#if !M2C_func_80033F08
/// @brief Updates the camera's simulated spherical coordinates for a camera
/// at pVec looking at the focus. Returns 1 when the azimuth was flipped by
/// half a turn (the camera passed over the focus), else 0.
/// @note asm/nonmatchings/camera/func_80033F08.s, by hand.
int func_80033F08(Vector3D *pVec) {
  Vector3D fromFocus;
  SphericalCoordinates *sim = &g_Camera.m_Simulation.m_Coords;
  int groundDistance, azimuth, turn, turnFlipped, flipped;

  VecSub(&fromFocus, pVec, g_Camera.m_Focus);

  sim->radius = VecMagnitude(&fromFocus, 1);
  sim->radius = VecRefineMagnitude(&fromFocus, sim->radius, 1);

  groundDistance = VecMagnitude(&fromFocus, 0);
  groundDistance = VecRefineMagnitude(&fromFocus, groundDistance, 0);
  sim->elevation = Atan2(groundDistance, fromFocus.z, 1);

  azimuth = Atan2(fromFocus.x, -fromFocus.y, 1);

  /* Closer to the old azimuth turned half a turn than to the old azimuth:
   * the camera went over the top of the focus. */
  turn = AngleDelta(azimuth - sim->azimuth);
  turnFlipped = AngleDelta(azimuth - (sim->azimuth - 0x800));
  flipped = 0;

  if (Abs(turnFlipped) < Abs(turn) &&
      (g_Camera.m_State == 0x80000009 || g_Camera.unk_0xE8 != 0)) {
    /* Right above the focus the azimuth means nothing: keep the sphere's. */
    if (Abs(fromFocus.x) <= 0x80 && Abs(fromFocus.y) <= 0x80)
      sim->azimuth = g_Camera.m_Sphere.m_Coords.azimuth;
    else
      sim->azimuth = (azimuth + 0x800) & 0xFFF;
    sim->elevation = (0x800 - sim->elevation) & 0xFFF;
    flipped = 1;
  } else if (Abs(fromFocus.x) <= 0x80 && Abs(fromFocus.y) <= 0x80) {
    sim->azimuth = g_Camera.m_Sphere.m_Coords.azimuth;
  } else {
    sim->azimuth = azimuth;
  }

  /* The camera's rotation relative to the simulated position. */
  g_Camera.m_Simulation.m_Offset.azimuth = g_Camera.m_Rotation.x;
  g_Camera.m_Simulation.m_Offset.elevation = AngleDelta(g_Camera.m_Rotation.y - sim->elevation);
  g_Camera.m_Simulation.m_Offset.radius = AngleDelta(0x800 - sim->azimuth - g_Camera.m_Rotation.z);

  return flipped;
}
#endif

#if !M2C_func_80034480
extern int D_800756B8;                  // Spyro visible from the camera (camera.c)
extern SphericalCoordsOffset D_8006CAB4[5]; // Presets tried when Spyro stays hidden
extern SphericalCoordsOffset D_8006C82C[5]; // Sphere nudges tried when Spyro is hidden

/* Spyro's sight point for the line-of-sight tests, 0x134 below his origin. */
#define SPYRO_SIGHT_DROP 0x134

/* Two rays, both ways (func_80033E40 is false when the ray hits). */
static int CanSee(Vector3D *pA, Vector3D *pB) {
  return func_80033E40(pA, pB) && func_80033E40(pB, pA);
}

/* A free camera spot at pPos that sees pTarget. */
static int IsGoodSpot(Vector3D *pPos, Vector3D *pTarget) {
  return ColSphere(pPos, 0x100, 0x100) == 0 && CanSee(pPos, pTarget);
}

/// @brief Moves the camera towards its destination in steps of at most 0x100,
/// pushed out of geometry; then, if Spyro is out of sight, looks for a
/// sphere position near the current one that sees him (nudged by
/// D_8006C82C at 1x, 2x and 4x), and after 30 hidden ticks resets to the
/// first preset that does.
/// @note asm/nonmatchings/camera/func_80034480.s, by hand.
void func_80034480(void) {
  Vector3D step, original, target;
  SphericalCoordsOffset savedSim;
  int isFar, steps, i, pushes = 0, pushed = 0, scale, freeNudges;

  VecSub(&step, &g_Camera.m_DestinationPosition, &g_Spyro.m_Position);
  isFar = VecMagnitude(&step, 1) > 0x2000;
  VecCopy(&original, &g_Camera.m_DestinationPosition);

  VecSub(&step, &g_Camera.m_DestinationPosition, &g_Camera.m_Position);
  steps = VecMagnitude(&step, 1) / 256 + 1;
  if (steps >= 2) {
    step.x /= steps;
    step.y /= steps;
    step.z /= steps;
  }

  /* March m_DestinationPosition from the camera to the destination, pushing
   * it out of anything it runs into; six pushes in a row give up. */
  VecCopy(&g_Camera.m_DestinationPosition, &g_Camera.m_Position);
  for (i = 0; i < steps; i++) {
    VecAdd(&g_Camera.m_DestinationPosition, &g_Camera.m_DestinationPosition, &step);
    for (pushes = 0; pushes < 6; pushes++) {
      if (!ColSphere(&g_Camera.m_DestinationPosition, 0x100, 0x100))
        break;
      VecCopy(&g_Camera.m_DestinationPosition, &g_CollisionPoint);
      pushed = 1;
    }
    if (pushes == 6) {
      VecCopy(&g_Camera.m_DestinationPosition, &original);
      break;
    }
  }

  g_Camera.unk_0xCC = 0;
  if (pushes < 6) {
    if (pushed) {
      /* Move by what's left, ignoring jitter under 0x20 on any axis. */
      VecSub(&original, &g_Camera.m_DestinationPosition, &g_Camera.m_Position);
      if (Abs(original.x) < 0x20)
        original.x = 0;
      if (Abs(original.y) < 0x20)
        original.y = 0;
      if (Abs(original.z) < 0x20)
        original.z = 0;
      VecAdd(&g_Camera.m_Position, &g_Camera.m_Position, &original);
    } else {
      VecCopy(&g_Camera.m_Position, &g_Camera.m_DestinationPosition);
      g_Camera.unk_0xCC = 1;
    }
  }

  g_Camera.unk_0xE8 = func_80033F08(&g_Camera.m_Position);

  VecCopy(&target, &g_Spyro.m_Position);
  target.z -= SPYRO_SIGHT_DROP;
  if (CanSee(&g_Camera.m_Position, &target)) {
    D_800756B8 = 1;
    if (g_Camera.unk_0xC4 != 0) {
      if (g_Camera.m_State == 0 || g_Camera.m_State == 0x80000009) {
        if (g_Camera.unk_0xE8 != 0)
          g_Camera.unk_0xC4--;
      } else {
        g_Camera.unk_0xC4--;
      }
    }

    /* Count the ticks Spyro is off the middle of the screen or far away. */
    if ((int)g_Camera.m_State >= 0) {
      func_80017AA4(&target, &g_Spyro.m_Position);
      if ((u_int)(target.x - 0x21) >= 0x1BF || (u_int)(target.y - 0x19) >= 0xBF || isFar) {
        g_Camera.m_SpyroOffCenterFrames++;
        return;
      }
    }
    g_Camera.m_SpyroOffCenterFrames = 0;
    return;
  }

  /* Spyro is hidden: draw him over the scenery, no shadow. */
  D_800756B8 = 0;
  if (g_Spyro.m_sortingDepth < 0x7F)
    g_Spyro.m_sortingDepth = 2;
  D_8007AA10.unk_1c = 0;

  if (++g_Camera.m_SpyroOffCenterFrames > 30) {
    for (i = 0; i < 5; i++) {
      g_Camera.m_SphericalPreset = &D_8006CAB4[i];
      func_80034358();
      if (IsGoodSpot(&g_Camera.m_DestinationPosition, &target))
        return;
    }
  }

  /* Try the nudges around the simulated sphere, wider each round, only
   * those whose spot was free in the first round. */
  savedSim = g_Camera.m_Simulation;
  freeNudges = 0;
  for (scale = 1; scale <= 4; scale *= 2) {
    for (i = 0; i < 5; i++) {
      if (scale > 1 && !((freeNudges >> i) & 1))
        continue;
      g_Camera.m_Sphere.m_Coords.azimuth =
          (g_Camera.m_Simulation.m_Coords.azimuth + D_8006C82C[i].m_Coords.azimuth * scale) & 0xFFF;
      g_Camera.m_Sphere.m_Coords.elevation =
          (g_Camera.m_Simulation.m_Coords.elevation + D_8006C82C[i].m_Coords.elevation * scale) & 0xFFF;
      g_Camera.m_Sphere.m_Coords.radius =
          g_Camera.m_Simulation.m_Coords.radius + D_8006C82C[i].m_Coords.radius * scale;
      func_80034204(&g_Camera.m_DestinationPosition);
      VecAdd(&g_Camera.m_DestinationPosition, &g_Camera.m_DestinationPosition, g_Camera.m_Focus);
      if (ColSphere(&g_Camera.m_DestinationPosition, 0x100, 0x100))
        continue;
      if (scale == 1)
        freeNudges |= 1 << i;
      if (CanSee(&g_Camera.m_DestinationPosition, &target)) {
        g_Camera.unk_0xC4 = 5;
        func_80033F08(&g_Camera.m_DestinationPosition);
        g_Camera.m_LastSimulation.m_Coords = g_Camera.m_Simulation.m_Coords;
        g_Camera.m_Simulation = savedSim;
        return;
      }
    }
    if (freeNudges == 0)
      return;
  }
}
#endif

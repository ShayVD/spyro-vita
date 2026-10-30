/* Camera functions the decomp hasn't matched (asm/nonmatchings/camera),
 * written by hand from the asm so they read as C (docs/porting-plan.md §7,
 * M2): M4's 60 fps presentation needs to see what the camera does. Each one
 * replaces its mips2c unit (cmake/mips2c.cmake M2C_HAND) and is checked
 * against PCSX-Redux with the oracle replay (docs/translation-log.md). */
#include "common.h"

#include "camera.h"
#include "math.h"
#include "vector.h"

/* Wrap an angle difference (12-bit angles, 0x1000 a turn) into
 * -0x7FF..0x800, as the asm does: `andi 0xFFF; slti 0x801; addiu -0x1000`. */
static int AngleDelta(int delta) {
  delta &= 0xFFF;
  if (delta > 0x800)
    delta -= 0x1000;
  return delta;
}

static int Abs(int v) { return v < 0 ? -v : v; }

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

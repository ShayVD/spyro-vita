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
#include "gamepad.h"
#include "hand_structs.h"
#include "math.h"
#include "moby.h"
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

extern int D_800756B8; // Spyro visible from the camera (camera.c)
extern int D_80075894; // Passive camera's hold timer (camera.c)
extern int D_80075924; // L2/R2 turn speed (camera.c, func_80035F58)
extern int D_80075938; // Ticks before an auto-centre may end

/* D_80075914 is the camera mode: 2 active, 0x52 passive. Both of its bits
 * 0x10 and 0x40 are set only in passive mode; the asm tests them
 * separately. */
#define CAMERA_PASSIVE() (D_80075914 & 0x10)
#define CAMERA_PASSIVE_40() (D_80075914 & 0x40)

/* The passive camera's behaviour, which D_8007592C (Spyro in state 0x1B,
 * CameraUpdate) also turns on. */
#define CAMERA_AUTO() (CAMERA_PASSIVE() || D_8007592C)

/* Camera states in m_State / unk_0xC0 (the requested next state). */
#define CAM_FOLLOW 0
#define CAM_STATE_1 1
#define CAM_STATE_2 2
#define CAM_STATE_3 3
#define CAM_STATE_4 4
#define CAM_STATE_5 5
#define CAM_STATE_6 6               /* Held: the focus frozen, springs off */
#define CAM_STATE_15 15
#define CAM_SPEED_CHECK 0x80000000u /* Follow, recentring once Spyro slows */
#define CAM_LOOK 0x80000009u        /* Triangle look mode */
#define CAM_SCRIPT_0200 0x8000000Au /* Held while CTRL_CAMERA_0200 is set */
#define CAM_PATH 0x8000000Bu        /* Held while CTRL_CAMERA_0400 is set */
#define CAM_SCRIPT_8000 0x80000007u /* Held while CTRL_CAMERA_8000 is set */
#define CAM_SCRIPT_10000 0x8000000Cu /* Held while CTRL_CAMERA_10000 is set */
#define CAM_FLY_IN 0x8000000Eu
#define CAM_RECENTRE 0x80000010u
#define CAM_RECENTRE_SLOW 0x80000011u
#define CAM_LEVEL_TRANSITION 0x80000012u

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

#if !M2C_func_80034CE8
/* Spring constants, decomp data: one table per component of the sphere
 * (azimuth, elevation, radius, then the offset's three), each with six
 * rows by stiffness, 0 the softest. The pull towards the target and the
 * damping are in 1/256; a speed under the rest value stops the spring. */
extern short D_8006C754[6], D_8006C760[6], D_8006C76C[6], D_8006C778[6], D_8006C784[6], D_8006C790[6];
extern short D_8006C79C[6], D_8006C7A8[6], D_8006C7B4[6], D_8006C7C0[6], D_8006C7CC[6], D_8006C7D8[6];
extern short D_8006C7E4[6], D_8006C7F0[6], D_8006C7FC[6], D_8006C808[6], D_8006C814[6], D_8006C820[6];

static short *const s_SpringPull[6] = {D_8006C754, D_8006C760, D_8006C76C,
                                       D_8006C778, D_8006C784, D_8006C790};
static short *const s_SpringDamping[6] = {D_8006C79C, D_8006C7A8, D_8006C7B4,
                                          D_8006C7C0, D_8006C7CC, D_8006C7D8};
static short *const s_SpringRest[6] = {D_8006C7E4, D_8006C7F0, D_8006C7FC,
                                       D_8006C808, D_8006C814, D_8006C820};

/* The six components of a SphericalCoordsOffset, in memory order. */
enum { SPH_AZIMUTH, SPH_ELEVATION, SPH_RADIUS, SPH_OFF_AZIMUTH, SPH_OFF_ELEVATION, SPH_OFF_RADIUS };
static_assert(sizeof(SphericalCoordsOffset) == 6 * sizeof(int), "SphericalCoordsOffset");

static inline int *Components(SphericalCoordsOffset *pCoords) { return &pCoords->m_Coords.azimuth; }

/* The error the spring of one component works on: target - sphere,
 * wrapped for the angles. */
static int SphereError(int pComp) {
  int delta = Components(&g_Cam.m_LastSimulation)[pComp] - Components(&g_Cam.m_Sphere)[pComp];
  return pComp == SPH_RADIUS ? delta : AngleDelta(delta);
}

/* One tick of a component's spring: speed += (pull * error - damping *
 * speed) / 256, in the asm's wrapping 32-bit arithmetic. Under its rest
 * speed the spring stops, except that a camera at its destination keeps
 * creeping at 0x100 towards the target. */
static void SpringStep(int pComp, int pRow, int pPull, int pError) {
  int *speed = &Components(&g_Cam.m_SphereSpeed)[pComp];
  u_int force = (u_int)pPull * (u_int)pError - (u_int)s_SpringDamping[pComp][pRow] * (u_int)*speed;

  *speed = (int)((u_int)*speed + (u_int)((int)force >> 8));
  if (Abs(*speed) < s_SpringRest[pComp][pRow]) {
    *speed = 0;
    if (g_Cam.m_AtDestination) {
      if (pError > 0)
        *speed = 0x100;
      else if (pError < 0)
        *speed = -0x100;
    }
  }
}

static void ClampSpeed(int pComp, int pLimit) {
  int *speed = &Components(&g_Cam.m_SphereSpeed)[pComp];

  if (*speed < -pLimit)
    *speed = -pLimit;
  if (*speed > pLimit)
    *speed = pLimit;
}

/* Move the component by its speed. Angles aren't wrapped here. */
static void SphereMove(int pComp) {
  Components(&g_Cam.m_Sphere)[pComp] += Components(&g_Cam.m_SphereSpeed)[pComp] >> 8;
}

/* All but the azimuth stop pulling during the passive camera's slow
 * recentre and in state 6. */
static int IsSphereHeld(void) {
  return (CAMERA_AUTO() && g_Cam.m_State == CAM_RECENTRE_SLOW) || g_Cam.m_State == CAM_STATE_6;
}

/// @brief Moves the camera's sphere (m_Sphere) one tick towards its target
/// (m_LastSimulation), each of the six components on a damped spring.
/// @param pRow Stiffness row of the spring tables, 0 (softest) to 5.
/// @note asm/nonmatchings/camera/func_80034CE8.s, by hand.
void func_80034CE8(int pRow) {
  int pull, error, comp;

  /* Azimuth: L2/R2 turn it directly; the passive camera lets a slow Spyro
   * jump or fall without swinging round. */
  pull = s_SpringPull[SPH_AZIMUTH][pRow];
  if (g_Cam.m_State == CAM_STATE_3 && pull < 0x400)
    pull = 0x400;
  if (D_80075924 != 0) {
    error = D_80075924;
  } else if (CAMERA_AUTO() && g_Cam.m_State == CAM_RECENTRE_SLOW && D_800756B8) {
    error = 0;
  } else if (((CAMERA_PASSIVE() && (g_Spyro.m_State == 5 || g_Spyro.m_State == 6)) || D_8007592C) &&
             g_Spyro.m_Physics.m_SpeedAngle.m_Speed < 0x400 && !g_Cam.m_NudgeTicks &&
             !g_Cam.m_OnMovingPlatform) {
    g_Cam.m_SphereSpeed.m_Coords.azimuth = 0;
    error = 0;
  } else if (g_Cam.m_State == CAM_STATE_6) {
    error = 0;
  } else {
    error = SphereError(SPH_AZIMUTH);
  }
  SpringStep(SPH_AZIMUTH, pRow, pull, error);

  if (g_Spyro.m_State == 15 && g_Spyro.m_walkingState == 11)
    ClampSpeed(SPH_AZIMUTH, 0x800);
  if (CAMERA_PASSIVE_40() && !g_Cam.m_NudgeTicks &&
      (g_Cam.m_State == CAM_LOOK || g_Cam.m_State == CAM_STATE_3))
    ClampSpeed(SPH_AZIMUTH, 0x2000);
  if (CAMERA_AUTO() && D_80075894 == 0 && D_800756B8 && !g_Cam.m_OnMovingPlatform &&
      (g_Cam.m_State == CAM_FOLLOW || g_Cam.m_State == CAM_SPEED_CHECK))
    ClampSpeed(SPH_AZIMUTH, 0x800);
  SphereMove(SPH_AZIMUTH);

  /* Elevation: the passive camera holds it while Spyro is in the air. */
  if (CAMERA_AUTO() && g_Cam.m_State == CAM_RECENTRE_SLOW) {
    error = 0;
  } else if (CAMERA_PASSIVE() && g_Spyro.m_State == 6 && !g_Cam.m_NudgeTicks) {
    g_Cam.m_SphereSpeed.m_Coords.elevation = 0;
    error = 0;
  } else if (g_Cam.m_State == CAM_STATE_6) {
    error = 0;
  } else {
    error = SphereError(SPH_ELEVATION);
  }
  SpringStep(SPH_ELEVATION, pRow, s_SpringPull[SPH_ELEVATION][pRow], error);
  if (CAMERA_PASSIVE_40() && g_Cam.m_State == CAM_LOOK && !g_Cam.m_NudgeTicks)
    ClampSpeed(SPH_ELEVATION, 0x2000);
  SphereMove(SPH_ELEVATION);

  /* Radius and the offset angles. */
  for (comp = SPH_RADIUS; comp <= SPH_OFF_RADIUS; comp++) {
    error = IsSphereHeld() ? 0 : SphereError(comp);
    SpringStep(comp, pRow, s_SpringPull[comp][pRow], error);
    SphereMove(comp);
  }
}
#endif

#if !M2C_func_800357A4
/* The camera state Spyro's state asks for. */
#define SPYRO_CAMERA_STATE() (D_8006C588[g_Spyro.m_State])

/* Spyro is near the middle of the screen and not too deep. */
static int IsSpyroCentred(void) {
  Vector3D screen;

  func_80017AA4(&screen, &g_Spyro.m_Position);
  screen.x -= 0x100;
  screen.y -= 0x78;
  return Abs(screen.x) <= 0x40 && Abs(screen.y) <= 0x28 && (u_int)screen.z <= 0x1400;
}

/* Re-derive the simulated sphere from where the camera is now and make it
 * the sphere the camera follows. */
static void ResyncSphere(void) {
  g_Camera.unk_0xE8 = func_80033F08(&g_Camera.m_Position);
  g_Camera.m_Sphere = g_Camera.m_Simulation;
}

static void StartRecentre(void) {
  g_Camera.unk_0xC0 = CAM_RECENTRE;
  D_80075938 = 45;
}

/* Leave a scripted state once its control flag is cleared; while it's set,
 * resync once on entering. */
static void UpdateScripted(u_int pState, int pFlag) {
  if (!(g_Spyro.m_ControlFlags & pFlag)) {
    g_Camera.unk_0xC0 = SPYRO_CAMERA_STATE();
    g_Camera.m_Focus = &g_Spyro.m_Position;
  } else if (g_Camera.m_State == pState) {
    return;
  }
  ResyncSphere();
}

/// @brief Picks the camera's next state (unk_0xC0) from Spyro's state, the
/// look button, scripted control flags and the passive camera's
/// auto-centring, then switches m_State to it.
/// @note asm/nonmatchings/camera/func_800357A4.s, by hand.
void func_800357A4(void) {
  u_int next = g_Camera.unk_0xC0;

  if (next == CAM_LOOK) {
    if (CAMERA_PASSIVE() && g_Spyro.m_State != 0 && g_Spyro.m_State != 0xD) {
      /* Passive camera, Spyro busy: drop look mode. */
    } else if (CAMERA_PASSIVE()) {
      if (!(g_Pad.m_Held & PAD_TRIANGLE)) {
        /* Triangle let go while standing: recentre. */
        StartRecentre();
        VecNull(&g_Spyro.m_HeadLookTarget);
      }
      goto switchState;
    } else if ((g_Pad.m_Held & PAD_TRIANGLE) && g_Spyro.m_State == 0) {
      goto switchState;
    }
    g_Camera.unk_0xC0 = SPYRO_CAMERA_STATE();
    VecNull(&g_Spyro.m_HeadLookTarget);
  } else if (next == CAM_SCRIPT_0200) {
    if (!(g_Spyro.m_ControlFlags & CTRL_CAMERA_0200)) {
      g_Camera.unk_0xC0 = SPYRO_CAMERA_STATE();
      g_Camera.m_Focus = &g_Spyro.m_Position;
      ResyncSphere();
      if ((CAMERA_PASSIVE() || D_8007592C) && g_Camera.unk_0xC0 == CAM_FOLLOW &&
          !g_Camera.m_OnMovingPlatform)
        StartRecentre();
    } else if (g_Camera.m_State != CAM_SCRIPT_0200) {
      ResyncSphere();
    }
  } else if (next == CAM_SCRIPT_8000) {
    UpdateScripted(CAM_SCRIPT_8000, CTRL_CAMERA_8000);
  } else if (next == CAM_SCRIPT_10000) {
    UpdateScripted(CAM_SCRIPT_10000, CTRL_CAMERA_10000);
  } else if (next == CAM_PATH) {
    if (!(g_Spyro.m_ControlFlags & CTRL_CAMERA_0400)) {
      g_Camera.unk_0xC0 = SPYRO_CAMERA_STATE();
      g_Camera.m_Focus = &g_Spyro.m_Position;
    } else if (g_Camera.m_State != CAM_PATH) {
      /* Entering: start the path at the end Spyro travels from. */
      g_Spyro.unk_0x240->m_CurrentNode = g_Spyro.unk_0x248 ? g_Spyro.unk_0x240->m_NodeCount - 1 : 0;
    }
  } else if (next == CAM_RECENTRE || next == CAM_RECENTRE_SLOW) {
    /* Recentring runs out its time unless Spyro asks for another state. */
    u_int wanted = SPYRO_CAMERA_STATE();
    if (!((wanted == CAM_FOLLOW || wanted == 4) && D_80075938 > 0))
      g_Camera.unk_0xC0 = wanted;
  } else if (next == CAM_SPEED_CHECK) {
    if (g_Spyro.m_Physics.m_SpeedAngle.m_Speed > 0x400 || !IsSpyroCentred())
      g_Camera.unk_0xC0 = SPYRO_CAMERA_STATE();
  } else if (next == CAM_FOLLOW && g_Spyro.unk_0x194 == 0 && (CAMERA_PASSIVE() || D_8007592C) &&
             !g_Camera.m_OnMovingPlatform) {
    /* Passive camera: recentre when Spyro drifts off the middle. */
    if (!IsSpyroCentred())
      StartRecentre();
  } else if (g_Camera.m_Focus != &g_Spyro.m_Position && g_Camera.m_State != CAM_STATE_6) {
    g_Camera.m_Focus = &g_Spyro.m_Position;
    ResyncSphere();
  }

switchState:
  if (g_Camera.unk_0xC0 == g_Camera.m_State)
    return;

  if ((CAMERA_PASSIVE() || D_8007592C) && g_Spyro.unk_0x194 == 0 && !g_Camera.m_OnMovingPlatform &&
      (g_Camera.unk_0xC0 == CAM_FOLLOW || g_Camera.unk_0xC0 == CAM_SPEED_CHECK) &&
      g_Camera.m_State != CAM_RECENTRE_SLOW && g_Camera.m_Focus == &g_Spyro.m_Position) {
    /* Passive camera returning to follow: recentre slowly first. */
    D_80075938 = 8;
    g_Camera.unk_0xC0 = CAM_RECENTRE_SLOW;
    D_80075894 = 90;
  } else if (g_Camera.m_State == CAM_STATE_2 || g_Camera.m_State == CAM_FLY_IN) {
    ResyncSphere();
  }

  g_Camera.m_State = g_Camera.unk_0xC0;
  if (g_Camera.m_State == CAM_LOOK) {
    if (D_800756B8)
      g_Camera.unk_0xC4 = 0;
    g_Camera.unk_0xDC = 0;
    g_Camera.unk_0xE0 = 0;
  }
}
#endif

#if !M2C_func_80035FB4
/* camera.c functions without a prototype in camera.h. */
void ApplySphericalPreset(void);
void CameraForceToDestination(void);
void func_80034480(void);
void func_80034CE8(int pRow);
void func_80035F58(void);

/* Sphere presets (decomp data). */
extern SphericalCoordsOffset D_8006C8D4; // Follow
extern SphericalCoordsOffset D_8006C934; // Behind Spyro
extern SphericalCoordsOffset D_8006C94C; // Flying ability, turning one way
extern SphericalCoordsOffset D_8006C964; // Spyro turned towards the camera
extern SphericalCoordsOffset D_8006C97C; // Flying ability, turning the other way
extern SphericalCoordsOffset D_8006C994; // State 3
extern SphericalCoordsOffset D_8006C9AC; // State 4
extern SphericalCoordsOffset D_8006C9DC; // State 4, Spyro turned towards the camera
extern SphericalCoordsOffset D_8006CA0C; // Look mode
extern SphericalCoordsOffset D_8006CB5C; // Beside a moby (CTRL_CAMERA_8000)
extern SphericalCoordsOffset D_8006CB74; // Facing a moby (CTRL_CAMERA_10000)

extern u_char D_8007527C;  // How far ahead of Spyro's velocity the camera leads
extern Vector3D D_80077798; // State 6's frozen focus point

static_assert(offsetof(Spyro, m_State) == 0x78, "Spyro.m_State");
static_assert(offsetof(Spyro, m_walkingState) == 0x7C, "Spyro.m_walkingState");
static_assert(offsetof(Spyro, m_Physics.m_TrueVelocity) == 0x10C, "Spyro.m_TrueVelocity");
static_assert(offsetof(Spyro, m_Physics.m_SpeedAngle.m_RotZ) == 0x11C, "Spyro.m_RotZ");
static_assert(offsetof(Spyro, m_Physics.m_TurnMomentum) == 0x148, "Spyro.m_TurnMomentum");
static_assert(offsetof(Spyro, m_HeadLookTarget.y) == 0x1A8, "Spyro.m_HeadLookTarget");
static_assert(offsetof(Spyro, m_ControlFlags) == 0x1F4, "Spyro.m_ControlFlags");
static_assert(offsetof(Spyro, m_mobyInUseBySpyro) == 0x224, "Spyro.m_mobyInUseBySpyro");
static_assert(offsetof(Spyro, m_flyingAbility) == 0x24C, "Spyro.m_flyingAbility");
static_assert(offsetof(Moby, m_Position) == 0xC, "Moby.m_Position");
static_assert(offsetof(Moby, m_Rotation.z) == 0x46, "Moby.m_Rotation.z");
static_assert(offsetof(Gamepad, m_Held) == 0x8, "Gamepad.m_Held");
static_assert(offsetof(Gamepad, m_NoButtonsDown) == 0x18, "Gamepad.m_NoButtonsDown");

/* Which components of m_Simulation SyncSphere copies into m_Sphere. */
enum { SYNC_AZIMUTH = 1, SYNC_ELEVATION = 2, SYNC_RADIUS = 4 };

static inline int Clamp(int pValue, int pMin, int pMax) {
  if (pValue < pMin)
    pValue = pMin;
  if (pValue > pMax)
    pValue = pMax;
  return pValue;
}

/* Re-derive the simulated sphere from where the camera is, and take the
 * chosen components of it into the sphere the camera follows. */
static void SyncSphere(int pWhich) {
  g_Cam.m_SimFlipped = func_80033F08(&g_Cam.m_Position);
  if (pWhich & SYNC_AZIMUTH)
    g_Cam.m_Sphere.m_Coords.azimuth = g_Cam.m_Simulation.m_Coords.azimuth;
  if (pWhich & SYNC_ELEVATION)
    g_Cam.m_Sphere.m_Coords.elevation = g_Cam.m_Simulation.m_Coords.elevation;
  if (pWhich & SYNC_RADIUS)
    g_Cam.m_Sphere.m_Coords.radius = g_Cam.m_Simulation.m_Coords.radius;
}

/* The whole sphere becomes the simulated one, so the camera stays where it
 * is: what most states do while a nudge found by func_80034480 holds
 * (m_NudgeTicks). */
static void TakeSimulatedSphere(void) {
  g_Cam.m_SimFlipped = func_80033F08(&g_Cam.m_Position);
  g_Cam.m_Sphere = g_Cam.m_Simulation;
}

static void FocusOnSpyro(void) {
  g_Cam.m_Focus = &g_Spyro.m_Position;
  g_Cam.m_FocusRotation = g_Spyro.m_Physics.m_SpeedAngle.m_RotZ;
}

static void StopSphere(void) {
  g_Cam.m_SphereSpeed.m_Coords.azimuth = 0;
  g_Cam.m_SphereSpeed.m_Coords.elevation = 0;
  g_Cam.m_SphereSpeed.m_Coords.radius = 0;
  g_Cam.m_SphereSpeed.m_Offset.azimuth = 0;
  g_Cam.m_SphereSpeed.m_Offset.elevation = 0;
  g_Cam.m_SphereSpeed.m_Offset.radius = 0;
}

/* Spyro's heading is more than 0x578 (123 degrees) off the camera's. */
static int IsSpyroTurnedToCamera(void) {
  return func_80017928(g_Spyro.m_Physics.m_SpeedAngle.m_RotZ, g_Cam.m_Rotation.z) >= 0x579;
}

/* Spyro's heading relative to the camera's, -0x7FF..0x800. */
static int SpyroTurn(void) {
  return AngleDelta(g_Spyro.m_Physics.m_SpeedAngle.m_RotZ - g_Cam.m_Rotation.z);
}

/* Turn the target's yaw offset to lead Spyro's movement: the angle, seen
 * from the camera, between Spyro and where his velocity (times
 * D_8007527C >> pShift) takes him. */
static void LeadSpyro(int pShift) {
  Vector3D toSpyro, ahead;

  VecSub(&toSpyro, &g_Spyro.m_Position, &g_Cam.m_Position);
  VecMult(&ahead, &g_Spyro.m_Physics.m_TrueVelocity, D_8007527C);
  VecShiftRight(&ahead, pShift);
  VecAdd(&ahead, &toSpyro, &ahead);
  g_Cam.m_LastSimulation.m_Offset.radius =
      (Atan2(toSpyro.x, toSpyro.y, 1) - Atan2(ahead.x, ahead.y, 1)) & 0xFFF;
}

/* Follow (states 0 and 0x80000000): the follow preset behind Spyro. The
 * passive camera instead holds still while D_80075894 runs (see the end of
 * func_80035FB4); the timer restarts at 90 whenever a button is down, Spyro
 * is hidden or the camera rides a platform, and only counts down while the
 * simulated sphere is off the follow preset. */
static void UpdateFollow(void) {
  int azimuth, elevation;

  if (!(CAMERA_PASSIVE() && !g_Cam.m_OnMovingPlatform && D_80075924 == 0 && !g_Cam.m_NudgeTicks &&
        (D_80075894 != 0 || g_Spyro.m_State == 27))) {
    g_Cam.m_SphericalPreset = &D_8006C8D4;
    FocusOnSpyro();
    SyncSphere(SYNC_AZIMUTH);
    if (!g_Cam.m_NudgeTicks || g_GameTick < 120)
      ApplySphericalPreset();
  }

  if (!(CAMERA_AUTO() && g_Pad.m_NoButtonsDown && D_800756B8) || g_Cam.m_OnMovingPlatform) {
    D_80075894 = 90;
    return;
  }
  if (D_80075894 == 0)
    return;

  azimuth = AngleDelta(g_Cam.m_Simulation.m_Coords.azimuth + g_Spyro.m_Physics.m_SpeedAngle.m_RotZ - 0x800);
  elevation = AngleDelta(g_Cam.m_Simulation.m_Coords.elevation - D_8006C8D4.m_Coords.elevation);
  if (Abs(azimuth) <= 0x190 && Abs(elevation) <= 0x100)
    return;

  D_80075894 -= g_DeltaTime;
  if (D_80075894 <= 0) {
    D_80075894 = 0;
    StopSphere();
  }
}

/* 0x80000010: recentre behind Spyro with the follow preset. */
static void UpdateRecentre(void) {
  g_Cam.m_SphericalPreset = &D_8006C8D4;
  FocusOnSpyro();
  SyncSphere(SYNC_AZIMUTH);
  if (!g_Cam.m_NudgeTicks)
    ApplySphericalPreset();
}

static void UpdateState1(void) {
  FocusOnSpyro();
  if (g_Cam.m_NudgeTicks)
    return;
  if (IsSpyroTurnedToCamera()) {
    g_Cam.m_SphericalPreset = &D_8006C964;
  } else {
    g_Cam.m_SphericalPreset = &D_8006C934;
    if (!CAMERA_PASSIVE_40())
      SyncSphere(SYNC_AZIMUTH);
  }
  ApplySphericalPreset();
  LeadSpyro(6);
}

static void UpdateState2(void) {
  FocusOnSpyro();
  if (g_Cam.m_NudgeTicks) {
    TakeSimulatedSphere();
    return;
  }
  g_Cam.m_SphericalPreset = IsSpyroTurnedToCamera() ? &D_8006C964 : &D_8006C934;
  ApplySphericalPreset();
}

/* State 3: the yaw offset leans with Spyro's turn. */
static void UpdateState3(void) {
  g_Cam.m_SphericalPreset = &D_8006C994;
  FocusOnSpyro();
  SyncSphere(SYNC_AZIMUTH);
  if (g_Cam.m_NudgeTicks) {
    TakeSimulatedSphere();
    return;
  }
  ApplySphericalPreset();
  g_Cam.m_LastSimulation.m_Offset.radius = (-g_Spyro.m_Physics.m_TurnMomentum * 8) & 0xFFF;
}

static void UpdateState4(void) {
  FocusOnSpyro();
  if (g_Cam.m_NudgeTicks) {
    TakeSimulatedSphere();
    return;
  }
  if (IsSpyroTurnedToCamera()) {
    g_Cam.m_SphericalPreset = &D_8006C9DC;
  } else {
    g_Cam.m_SphericalPreset = &D_8006C9AC;
    if (!CAMERA_PASSIVE_40())
      SyncSphere(SYNC_AZIMUTH);
  }
  ApplySphericalPreset();
}

/* State 5. With the flying ability (flight levels), m_walkingState 2 and 1
 * swing the camera to the side Spyro turns to. */
static void UpdateState5(void) {
  int turn;

  FocusOnSpyro();
  if (g_Cam.m_NudgeTicks) {
    TakeSimulatedSphere();
    return;
  }

  if (g_Spyro.m_flyingAbility) {
    if (g_Spyro.m_walkingState == 2) {
      g_Cam.m_SphericalPreset = SpyroTurn() > 0 ? &D_8006C94C : &D_8006C97C;
      g_Cam.m_FocusRotation = (g_Spyro.m_Physics.m_SpeedAngle.m_RotZ + 0x800) & 0xFFF;
      SyncSphere(SYNC_AZIMUTH);
      ApplySphericalPreset();
      return;
    }
    if (g_Spyro.m_walkingState == 1) {
      /* Only the preset changes; the target stays as it is. */
      turn = SpyroTurn();
      if (turn > 0x100)
        g_Cam.m_SphericalPreset = &D_8006C94C;
      else if (turn < -0x100)
        g_Cam.m_SphericalPreset = &D_8006C97C;
      else
        g_Cam.m_SphericalPreset = &D_8006C934;
      SyncSphere(SYNC_AZIMUTH);
      return;
    }
    g_Cam.m_SphericalPreset = &D_8006C934;
    SyncSphere(SYNC_AZIMUTH | SYNC_ELEVATION);
  } else if (g_Spyro.m_walkingState != 11 && IsSpyroTurnedToCamera()) {
    g_Cam.m_SphericalPreset = &D_8006C964;
  } else {
    g_Cam.m_SphericalPreset = &D_8006C934;
    SyncSphere(SYNC_AZIMUTH | SYNC_ELEVATION);
  }
  ApplySphericalPreset();
  LeadSpyro(5);
}

/* State 6: the focus point freezes where it is (copied into D_80077798)
 * and the springs run with no pull, so the camera coasts to a stop. Then
 * the sphere is whatever the camera's position gives. */
static void UpdateHeld(void) {
  SphericalCoordsOffset *speed = &g_Cam.m_SphereSpeed;
  int i;

  if (g_Cam.m_Focus != &D_80077798) {
    VecCopy(&D_80077798, g_Cam.m_Focus);
    g_Cam.m_Focus = &D_80077798;
  }
  D_80075924 = 0;

  if (Abs(speed->m_Coords.azimuth) + Abs(speed->m_Coords.elevation) + Abs(speed->m_Coords.radius) +
          Abs(speed->m_Offset.azimuth) + Abs(speed->m_Offset.elevation) + Abs(speed->m_Offset.radius) >
      0x20) {
    for (i = 0; i < g_DeltaTime; i++)
      func_80034CE8(0);
    func_80034204(&g_Cam.m_DestinationPosition);
    VecAdd(&g_Cam.m_DestinationPosition, &g_Cam.m_DestinationPosition, g_Cam.m_Focus);
    CameraForceToDestination();
    func_800342F8();
  }

  TakeSimulatedSphere();
}

/* State 15 (Spyro in a portal): the follow preset; the elevation stays
 * the sphere's. */
static void UpdateState15(void) {
  g_Cam.m_SphericalPreset = &D_8006C8D4;
  FocusOnSpyro();
  SyncSphere(SYNC_AZIMUTH | SYNC_RADIUS);
  if (g_Cam.m_NudgeTicks)
    TakeSimulatedSphere();
  else
    ApplySphericalPreset();
}

/* Look mode (triangle): the stick or the d-pad moves the target around the
 * look preset, and Spyro's head looks the opposite way. */
static void UpdateLook(void) {
  PadFrame *pad;

  g_Cam.m_SphericalPreset = &D_8006CA0C;
  FocusOnSpyro();
  SyncSphere(SYNC_AZIMUTH | SYNC_ELEVATION | SYNC_RADIUS);
  if (g_Cam.m_NudgeTicks)
    return;
  ApplySphericalPreset();

  pad = g_PadFrame;
  if (pad->m_Type == CONTROLLER_TYPE_STICK &&
      !(pad->m_Sticks.m_LeftX == 0x7F && pad->m_Sticks.m_LeftY == 0x7F)) {
    g_Cam.m_LookYaw += (pad->m_Sticks.m_LeftX - 0x7F) >> 1;
    g_Cam.m_LookPitch += (0x7F - pad->m_Sticks.m_LeftY) >> 1;
  } else {
    if (g_Pad.m_Held & PAD_LEFT)
      g_Cam.m_LookYaw -= 0x30;
    else if (g_Pad.m_Held & PAD_RIGHT)
      g_Cam.m_LookYaw += 0x30;
    if (g_Pad.m_Held & PAD_UP)
      g_Cam.m_LookPitch += 0x30;
    else if (g_Pad.m_Held & PAD_DOWN)
      g_Cam.m_LookPitch -= 0x30;
  }
  g_Cam.m_LookYaw = Clamp(g_Cam.m_LookYaw, -0x400, 0x400);
  g_Cam.m_LookPitch = Clamp(g_Cam.m_LookPitch, -0x3D0, 0x500);

  /* Looking down further than 0x140 tilts the view instead. */
  g_Cam.m_LastSimulation.m_Coords.azimuth += g_Cam.m_LookYaw;
  if (g_Cam.m_LookPitch < -0x140) {
    g_Cam.m_LastSimulation.m_Coords.elevation -= 0x140;
    g_Cam.m_LastSimulation.m_Offset.elevation += 0x140 + g_Cam.m_LookPitch;
  } else {
    g_Cam.m_LastSimulation.m_Coords.elevation += g_Cam.m_LookPitch;
  }

  g_Spyro.m_HeadLookTarget.z = Clamp(-g_Cam.m_LookYaw, -0x300, 0x300);
  g_Spyro.m_HeadLookTarget.y = Clamp(-g_Cam.m_LookPitch, -0x200, 0x200);
}

/* 0x80000007 (CTRL_CAMERA_8000): beside the moby Spyro uses, at Spyro's
 * height, facing the way the moby does. */
static void UpdateBesideMoby(void) {
  Moby *moby = g_Spyro.m_mobyInUseBySpyro;

  g_Cam.m_SphericalPreset = &D_8006CB5C;
  g_Cam.m_Focus = &g_Cam.m_MobyFocus;
  g_Cam.m_MobyFocus.x = moby->m_Position.x;
  g_Cam.m_MobyFocus.z = g_Spyro.m_Position.z;
  g_Cam.m_MobyFocus.y = moby->m_Position.y;
  g_Cam.m_FocusRotation = moby->m_Rotation.z << 4;
  SyncSphere(SYNC_AZIMUTH | SYNC_ELEVATION | SYNC_RADIUS);
  ApplySphericalPreset();
}

/* 0x8000000C (CTRL_CAMERA_10000): on Spyro, turned to face from him
 * towards the moby he uses. */
static void UpdateFacingMoby(void) {
  Moby *moby = g_Spyro.m_mobyInUseBySpyro;

  g_Cam.m_Focus = &g_Spyro.m_Position;
  g_Cam.m_FocusRotation = Atan2(moby->m_Position.x - g_Spyro.m_Position.x,
                                moby->m_Position.y - g_Spyro.m_Position.y, 1);
  if (g_Cam.m_NudgeTicks)
    return;
  g_Cam.m_SphericalPreset = &D_8006CB74;
  SyncSphere(0);
  ApplySphericalPreset();
}

/* 0x8000000A (CTRL_CAMERA_0200): the preset set by whoever set the flag. */
static void UpdateScripted0200(void) {
  SyncSphere(SYNC_AZIMUTH | SYNC_ELEVATION | SYNC_RADIUS);
  if (g_Cam.m_NudgeTicks)
    TakeSimulatedSphere();
  else
    ApplySphericalPreset();
}

/* No L2/R2 turning in these states. */
static int IsTurnLocked(u_int pState) {
  return pState == CAM_LOOK || pState == CAM_SCRIPT_8000 || pState == CAM_LEVEL_TRANSITION ||
         pState == CAM_PATH || pState == CAM_FLY_IN || pState == CAM_STATE_3;
}

/// @brief Camera update for every state but the path (0x8000000B), the
/// fly-in (0x8000000E) and the level transition (0x80000012): sets the
/// focus and the target sphere for the state, runs the springs
/// (func_80034CE8) g_DeltaTime times, moves the camera with collision
/// (func_80034480) and re-derives the sphere from where it ended up.
/// @note asm/nonmatchings/camera/func_80035FB4.s, by hand.
void func_80035FB4(void) {
  int row, i;

  if (CAMERA_AUTO() && g_Cam.m_State == CAM_STATE_15)
    g_Cam.m_State = CAM_FOLLOW;

  /* The springs stiffen the longer Spyro is off the middle of the screen. */
  row = (g_Cam.m_SpyroOffCenterFrames + 1) >> 1;
  if (row >= 5)
    row = 4;

  switch (g_Cam.m_State) {
  case CAM_FOLLOW:
  case CAM_SPEED_CHECK:
    UpdateFollow();
    break;
  case CAM_RECENTRE:
    UpdateRecentre();
    D_80075938--;
    break;
  case CAM_RECENTRE_SLOW:
    D_80075938--;
    break;
  case CAM_STATE_1:
    UpdateState1();
    break;
  case CAM_STATE_2:
    UpdateState2();
    break;
  case CAM_STATE_3:
    UpdateState3();
    break;
  case CAM_STATE_4:
    UpdateState4();
    break;
  case CAM_STATE_5:
    UpdateState5();
    break;
  case CAM_STATE_6:
    UpdateHeld();
    return;
  case CAM_STATE_15:
    UpdateState15();
    break;
  case CAM_LOOK:
    UpdateLook();
    row = 5;
    break;
  case CAM_SCRIPT_8000:
    UpdateBesideMoby();
    break;
  case CAM_SCRIPT_10000:
    UpdateFacingMoby();
    break;
  case CAM_SCRIPT_0200:
    UpdateScripted0200();
    if (g_Spyro.m_ControlFlags & CTRL_SKIP_CAMERA_CENTER)
      row = 5;
    break;
  }

  if (IsTurnLocked(g_Cam.m_State))
    D_80075924 = 0;
  else
    func_80035F58(); /* L2/R2 */

  if (CAMERA_AUTO() && D_800756B8 && D_80075894 != 0 && !g_Cam.m_OnMovingPlatform &&
      D_80075924 == 0 && (g_Cam.m_State == CAM_FOLLOW || g_Cam.m_State == CAM_SPEED_CHECK)) {
    /* The passive camera holds still. */
    VecCopy(&g_Cam.m_DestinationPosition, &g_Cam.m_Position);
    StopSphere();
  } else {
    for (i = 0; i < g_DeltaTime; i++)
      func_80034CE8(row);
    func_80034204(&g_Cam.m_DestinationPosition);
    VecAdd(&g_Cam.m_DestinationPosition, &g_Cam.m_DestinationPosition, g_Cam.m_Focus);
    func_80034480();
    func_800342F8();
  }

  /* The sphere becomes what the camera's position gives, except the
   * passive camera's azimuth while a slow Spyro jumps. */
  g_Cam.m_SimFlipped = func_80033F08(&g_Cam.m_Position);
  if (!(CAMERA_PASSIVE() && g_Spyro.m_State == 5 && !g_Cam.m_OnMovingPlatform &&
        g_Spyro.m_Physics.m_SpeedAngle.m_Speed < 0x400))
    g_Cam.m_Sphere.m_Coords.azimuth = g_Cam.m_Simulation.m_Coords.azimuth;
  g_Cam.m_Sphere.m_Coords.elevation = g_Cam.m_Simulation.m_Coords.elevation;
  g_Cam.m_Sphere.m_Coords.radius = g_Cam.m_Simulation.m_Coords.radius;
  g_Cam.m_Sphere.m_Offset = g_Cam.m_Simulation.m_Offset;
}
#endif

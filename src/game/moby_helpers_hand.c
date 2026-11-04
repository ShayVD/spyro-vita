/* The one function of src/moby_helpers.c the decomp hasn't matched
 * (asm/nonmatchings/moby_helpers/func_8003BFC0.s): a moby's step along its
 * path, written by hand from the asm so it reads as C (docs/native-plan.md
 * stage 3, decisions D94). Compiled C in the original, so this is
 * decompiling. Replaces the mips2c unit (cmake/mips2c.cmake M2C_HAND);
 * checked against the unit with the shadows of hand_shadow.h and against
 * PCSX-Redux with the oracle replay (docs/translation-log.md).
 * M2C_func_8003BFC0=1 (cmake SPYRO_M2C_FUNCS) compiles it out for the unit. */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "42CC4.h"
#include "hand_shadow.h"
#include "math.h"
#include "moby.h"
#include "moby_helpers.h"
#include "vector.h"

#include <stddef.h>

static_assert(offsetof(Moby, m_Position) == 0xC && offsetof(Moby, m_Rotation) == 0x44, "Moby");
static_assert(offsetof(PathData, m_CurrentNode) == 1 && offsetof(PathData, m_Nodes) == 8 && sizeof(PathData) == 0x18,
              "PathData");

/* The mips2c unit, linked as func_8003BFC0_gen with SPYRO_M2C_REF or
 * SPYRO_M2C_FUNCS. */
u_int func_8003BFC0_gen();

#if !M2C_func_8003BFC0
/* A segment's curves, by its length in frames: a (new, previous) weight
 * pair in 1024ths per frame of its first frames; past them the moby goes
 * straight. */
static const short *Curve(int duration, int *curveFrames) {
  if (duration >= 9) {
    *curveFrames = 7;
    return D_8006CBCC;
  }
  if (duration >= 7) {
    *curveFrames = 5;
    return D_8006CBB4;
  }
  if (duration >= 5) {
    *curveFrames = 3;
    return D_8006CBA4;
  }
  *curveFrames = 1;
  return D_80075280;
}

static int PathStep(Moby *moby, PathData *path, Vector3D *prev, int *frame, int duration, int flags) {
  Vector3D move, turn;
  int curveFrames, passed = 0, left;
  const short *curve = Curve(duration, &curveFrames);
  Vector3D *node = &path->m_Nodes[path->m_CurrentNode].m_Position;
  VecSub(&move, node, &moby->m_Position);
  if (*frame >= duration) {
    /* The segment's last frame: onto the node, and on to the next (the
     * first again after the last). */
    VecCopy(prev, &move);
    passed++;
    if (++path->m_CurrentNode >= path->m_NodeCount) {
      path->m_CurrentNode = 0;
      passed++;
    }
    *frame = 0;
  } else {
    /* An even share of the way left... */
    left = duration - (*frame - 1);
    move.x /= left;
    move.y /= left;
    move.z /= left;
    if (*frame <= curveFrames) {
      /* ...turned from the last segment's direction in its first frames. */
      VecMult(&move, &move, curve[*frame * 2]);
      VecMult(&turn, prev, curve[*frame * 2 + 1]);
      VecAdd(&move, &move, &turn);
      VecShiftRight(&move, 10);
    }
    ++*frame;
  }
  /* Flag 4: the path climbs; otherwise the moby keeps its height. It faces
   * where it goes. */
  if (!(flags & 4))
    move.z = 0;
  moby->m_Rotation.z = Atan2Fast(move.x, move.y);
  moby->m_Rotation.y = 0x40 - Atan2Fast(move.z, VecMagnitude(&move, 0));
  VecAdd(&moby->m_Position, &moby->m_Position, &move);
  func_800529E4(moby, 2);
  return passed;
}

#if HAND_SHADOW
/* Fuzzing (SPYRO_HAND_FUZZ=N): the segment's frame (0 to past its end),
 * its length (1 to 12: all four curves), the flags, the current node and
 * the last segment's direction, at random. */
static void FuzzPath(Moby *moby, PathData *path, Vector3D *prev, int *frame, int duration, int flags) {
  int n = HandFuzzBegin("func_8003BFC0", (u_int)*frame ^ (u_int)duration << 8 ^ path->m_CurrentNode << 16), k;
  for (k = 0; k < n; k++) {
    int d = k & 1 ? duration : (int)(HandFuzzRand() % 12) + 1, f = HandFuzzRand() & 4;
    *frame = (int)(HandFuzzRand() % (d + 2));
    if (path->m_NodeCount)
      path->m_CurrentNode = HandFuzzRand() % path->m_NodeCount;
    prev->x = (int)(HandFuzzRand() % 0x2001) - 0x1000;
    prev->y = (int)(HandFuzzRand() % 0x2001) - 0x1000;
    prev->z = (int)(HandFuzzRand() % 0x801) - 0x400;
    HAND_FUZZ_ONE(func_8003BFC0, func_8003BFC0_gen(moby, path, prev, frame, d, f), PathStep(moby, path, prev, frame, d, f));
  }
  (void)flags;
}
#endif

/* Moves pMoby one frame along pPath, the segment to the current node
 * taking pSegmentDuration frames (*pSegmentFrame counts them). Returns 1
 * when it reached the node, 2 when that was the path's last. */
int func_8003BFC0(Moby *pMoby, PathData *pPath, Vector3D *pPreviousMovement, int *pSegmentFrame, int pSegmentDuration,
                  int pFlags) {
#if HAND_SHADOW
  FuzzPath(pMoby, pPath, pPreviousMovement, pSegmentFrame, pSegmentDuration, pFlags);
#endif
  HAND_SHADOW_CALL(func_8003BFC0,
                   func_8003BFC0_gen(pMoby, pPath, pPreviousMovement, pSegmentFrame, pSegmentDuration, pFlags),
                   PathStep(pMoby, pPath, pPreviousMovement, pSegmentFrame, pSegmentDuration, pFlags));
  return PathStep(pMoby, pPath, pPreviousMovement, pSegmentFrame, pSegmentDuration, pFlags);
}
#endif

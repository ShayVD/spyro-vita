/* The moby slots and a moby's derived state (asm/42CC4.s): allocation,
 * the collision grid, the rotation matrix, animated vertices, Spyro's
 * compressed vertices, the death sparks and the shadow, written by hand
 * from the asm so it reads as C (docs/native-plan.md stage 3, decisions
 * D87). Replaces the mips2c unit (cmake/mips2c.cmake M2C_HAND_MULTI);
 * checked against PCSX-Redux with the oracle replay and against the unit
 * with the shadow below (docs/translation-log.md).
 *
 * The GTE: the asm sets its control registers (the rotation, the
 * translation, and RBK, which func_800530C0 borrows as a spare register)
 * and leaves them; the hand C sets them the same, as state other calls may
 * read. Its data registers (V0, IR, MAC, the FIFOs) and FLAG it uses as a
 * calculator, and nothing reads what it leaves there: the hand C computes
 * in locals instead (D87, as D83 for collision). So does func_800530C0's
 * vertex list, which the asm keeps in the scratchpad.
 *
 * M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) takes that function from the mips2c
 * unit instead, linked as <name>_gen, for A/B tests. */
#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <string.h>

#include "common.h"

#include "42CC4.h"
#include "gte_int.h"
#include "math.h"
#include "math_hand.h"
#include "moby.h"
#include "psx_arena.h"
#include "psx_mem.h"
#include "rand.h"
#include "renderers.h"
#include "variables.h"
#include "vector.h"

#include <gtereg.h>
#include <stddef.h>

/* The asm's offsets. */
static_assert(sizeof(Moby) == 0x58, "Moby");
static_assert(offsetof(Moby, m_ShadowDistance) == 0x1C && offsetof(Moby, m_RotationMatrix) == 0x20, "Moby");
static_assert(offsetof(Moby, m_CollisionRegion) == 0x34 && offsetof(Moby, m_AnimationState) == 0x3C, "Moby");
static_assert(offsetof(Moby, m_Rotation) == 0x44 && offsetof(Moby, m_State) == 0x48, "Moby");
static_assert(offsetof(Moby, m_Renderer) == 0x4B, "Moby");
static_assert(offsetof(AnimationHeader, m_Frames) == 0x24 && offsetof(Model, m_Animations) == 0x38, "Model");

extern int D_8006D614[];             /* Spyro's 7-bit vertex deltas, packed as frame words */

/* The mips2c unit's entries, linked as <name>_gen for A/B tests and the
 * shadow below (cmake M2C_HAND_MULTI). */
u_int MobyAlloc_gen(), MobyFree_gen(), MobyUpdateCollision_gen(), MobySetShaded_gen(), func_800529E4_gen();
u_int MobyGetAnimatedVertex_gen(), func_80052F38_gen(), func_800530C0_gen(), MobyUpdateShadow_gen();

/* Oracle replays and differential sweeps (host builds with the mips2c
 * unit linked, cmake SPYRO_M2C_REF): with g_mobys_shadow set, each call
 * also runs the unit from the same state first, then the hand C. RAM (but
 * the scratchpad), the result and the GTE's control registers (but FLAG)
 * must match
 * (mismatches are printed and counted in g_mobys_shadow_fail), and the call
 * leaves the scratchpad and the GTE as the unit left them, so a sample's
 * whole state still compares with the emulator's (as collision_hand.c's,
 * decisions D83). */
#define MOBYS_SHADOW (MIPS2C_REPLAY && M2C_REF)
#if MOBYS_SHADOW
#include "mips2c_rt.h"

#include <stdio.h>

int g_mobys_shadow, g_mobys_shadow_fail;
static int s_in_shadow;
static u_char s_before[PSX_RAM_SIZE], s_gen[PSX_RAM_SIZE];
static GTERegisters s_gte_before, s_gte_gen;
static u32 s_sp;

static int ShadowBegin(void) {
  if (!g_mobys_shadow || s_in_shadow)
    return 0;
  s_in_shadow = 1;
  s_sp = g_mips_sp;
  memcpy(s_before, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  s_gte_before = gteRegs;
  return 1;
}

static void ShadowSwap(void) {
  memcpy(s_gen, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  s_gte_gen = gteRegs;
  memcpy((void *)g_psx_arena_base, s_before, PSX_RAM_SIZE);
  gteRegs = s_gte_before;
  g_mips_sp = s_sp;
}

/* RAM the comparison skips: the scratchpad (the arena's first KB, which
 * the unit's state replaces), the asm's register spill area, and the
 * emulated stack below the call. */
static int ShadowSkip(u32 off) {
  return off < 0x10000u || (off >= 0x77DD8u && off < 0x77EA0u) ||
         (off < s_sp - PSX_RAM_BASE && off >= s_sp - PSX_RAM_BASE - 0x10000u);
}

static int ShadowEnd(const char *name, int gen, int hand) {
  const u32 *h = (const u32 *)g_psx_arena_base, *g = (const u32 *)s_gen;
  u32 i;
  int shown = 0;
  for (i = 0; i < PSX_RAM_SIZE / 4; i++)
    if (h[i] != g[i] && !ShadowSkip(i * 4)) {
      g_mobys_shadow_fail++;
      if (shown++ < 8)
        printf("  mobyshadow %s ram %08x unit %08x hand %08x\n", name, PSX_RAM_BASE + i * 4, g[i], h[i]);
    }
  if (gen != hand) {
    g_mobys_shadow_fail++;
    printf("  mobyshadow %s result unit %08x hand %08x\n", name, gen, hand);
  }
  for (i = 0; i < 31; i++) /* not FLAG (31): a command's result, like the data registers */
    if (gteRegs.CP2C.p[i].d != s_gte_gen.CP2C.p[i].d) {
      g_mobys_shadow_fail++;
      printf("  mobyshadow %s gte c%u unit %08x hand %08x\n", name, i, s_gte_gen.CP2C.p[i].d, gteRegs.CP2C.p[i].d);
    }
  memcpy((void *)g_psx_arena_base, s_gen, 0x400);
  gteRegs = s_gte_gen;
  s_in_shadow = 0;
  return hand;
}

#define SHADOW(name, gen_call, hand_call)                                      \
  do {                                                                         \
    if (ShadowBegin()) {                                                       \
      int gen_ = (int)(gen_call);                                              \
      ShadowSwap();                                                            \
      return ShadowEnd(#name, gen_, (int)(hand_call));                         \
    }                                                                          \
  } while (0)
#define SHADOW_VOID(name, gen_call, hand_call)                                 \
  do {                                                                         \
    if (ShadowBegin()) {                                                       \
      (void)(gen_call);                                                        \
      ShadowSwap();                                                            \
      hand_call;                                                               \
      ShadowEnd(#name, 0, 0);                                                  \
      return;                                                                  \
    }                                                                          \
  } while (0)

/* Differential fuzzing (SPYRO_MOBY_FUZZ=N with the shadow on): before a
 * shadowed call of MobyUpdateShadow, func_800529E4, MobyGetAnimatedVertex
 * or func_80052F38, N variants of it (the collision normal and point, the
 * flags (subsets of the call's) and a moved position, the vertex and the frame progress, the
 * vertex count), each from the same state through the unit and the hand C
 * and compared, then the state put back. Seeded from the arguments. */
#include <stdlib.h>

static int s_fuzz = -1;
static u_int s_rng;
static u_char s_fuzz_save[PSX_RAM_SIZE];
static GTERegisters s_fuzz_gte;

static int FuzzBegin(u_int seed) {
  if (s_fuzz < 0)
    s_fuzz = getenv("SPYRO_MOBY_FUZZ") ? atoi(getenv("SPYRO_MOBY_FUZZ")) : 0;
  if (!g_mobys_shadow || s_in_shadow || !s_fuzz)
    return 0;
  s_rng = seed * 2654435761u + 1;
  memcpy(s_fuzz_save, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  s_fuzz_gte = gteRegs;
  return s_fuzz;
}

static u_int FuzzRand(void) {
  s_rng = s_rng * 1103515245u + 12345u;
  return s_rng >> 8;
}

/* One variant, after the caller changed the state: unit, then hand C,
 * compared; then the state from before the variants. */
#define FUZZ_ONE(name, gen_call, hand_call)                                    \
  do {                                                                         \
    int gen_;                                                                  \
    ShadowBegin();                                                             \
    gen_ = (int)(gen_call);                                                    \
    ShadowSwap();                                                              \
    hand_call;                                                                 \
    ShadowEnd("fuzz " #name, gen_, gen_);                                      \
    memcpy((void *)g_psx_arena_base, s_fuzz_save, PSX_RAM_SIZE);               \
    gteRegs = s_fuzz_gte;                                                      \
  } while (0)
#else
#define SHADOW(name, gen_call, hand_call) ((void)0)
#define SHADOW_VOID(name, gen_call, hand_call) ((void)0)
#endif

/* ===================================================================== */
/* Shared pieces                                                          */
/* ===================================================================== */

/* The moby collision grid: g_MobyCollisionChain points at its heads, one
 * per 8192-unit cell (32 per row), then a second grid 0x400 entries on;
 * each list runs through m_CollisionChainNext. */
static inline Moby **MobyChain(int region) { return (Moby **)g_MobyCollisionChain + region; }

/* A moby's cell: x and y >> 13 (as unsigned), 32 cells to a row. */
static inline int MobyCell(const Moby *m) {
  return (int)((u_int)m->m_Position.x >> 13) + (int)(((u_int)m->m_Position.y >> 13) << 5);
}

/* Takes m out of its cell's list (the asm walks it to m's link; m must be
 * on it). */
static void ChainUnlink(Moby *m, int region) {
  Moby **link = MobyChain(region);
  while (*link != m)
    link = &(*link)->m_CollisionChainNext;
  *link = m->m_CollisionChainNext;
}

/* Puts m at the head of a cell's list. */
static void ChainLink(Moby *m, int region) {
  Moby **head = MobyChain(region);
  Moby *next = *head;
  *head = m;
  m->m_CollisionChainNext = next;
}

/* The collision model of a moby's animation frame: the frame word's bits
 * 21-23 pick one of the model's eight. */
static void *FrameCollisionModel(const Model *model, int anim, int frame) {
  u_int data = model->m_Animations[anim]->m_Frames[frame].m.m_Data;
  return model->m_CollisionModels[(data >> 21) & 7];
}

/* The moby's rotation matrix from its three 8-bit angles: RotVec8ToMatrix
 * from the identity, with the last turn left out of the GTE. */
static void MobyRotationMatrix(Moby *m) {
  RotateByAngles8(*(const u_int *)&m->m_Rotation, NULL, (MATRIX *)&m->m_RotationMatrix, 0);
}

/* ===================================================================== */
/* Moby slots                                                             */
/* ===================================================================== */

/* g_MobyAllocPtr's array of mobys and g_PropsAllocPtr's of 0x18-byte prop
 * blocks (the props grow down from it) mark each slot with a signed state
 * byte: m_State for a moby (read signed), a block's last byte for props. 0 or
 * more: in use; -1: free, and so is everything after it; -2: a free slot
 * among used ones; -3: a level moby freed (MobyFree). */
#define SLOT_END -1
#define SLOT_HOLE -2
#define SLOT_DEAD -3
#define PROPS_SIZE 0x18

static inline signed char *MobyState(Moby *m) { return (signed char *)&m->m_State; }
/* A props block's state byte, by the block's end (the next block's start). */
static inline signed char *PropsState(u_char *end) { return (signed char *)end - 1; }

#if !M2C_MobyAlloc
/* Takes the slot at g_MobyAllocPtr and the props block below
 * g_PropsAllocPtr, and moves both on to the next free slot. */
static Moby *Alloc(void) {
  Moby *m = g_MobyAllocPtr, *next = m + 1;
  u_char *props = (u_char *)g_PropsAllocPtr, *end;
  int state;
  g_DynMobyCount++;
  state = *MobyState(m);
  *MobyState(m) = 0;
  if (state == SLOT_END) {
    *MobyState(next) = SLOT_END;
  } else {
    while (*MobyState(next) >= 0)
      next++;
  }
  g_MobyAllocPtr = next;
  state = *PropsState(props);
  *PropsState(props) = 0;
  end = props - PROPS_SIZE;
  if (state == SLOT_END) {
    *PropsState(end) = SLOT_END;
  } else {
    while (*PropsState(end) >= 0)
      end -= PROPS_SIZE;
  }
  m->m_Props = props - PROPS_SIZE;
  g_PropsAllocPtr = end;
  return m;
}

Moby *MobyAlloc(void) {
#if MOBYS_SHADOW
  if (ShadowBegin()) {
    int gen = (int)MobyAlloc_gen();
    ShadowSwap();
    return (Moby *)ShadowEnd("MobyAlloc", gen, (int)Alloc());
  }
#endif
  return Alloc();
}
#else
Moby *MobyAlloc(void) { return (Moby *)MobyAlloc_gen(); }
#endif

#if !M2C_MobyFree
/* Marks a freed slot by its state byte (step: the distance to the next
 * slot in allocation order): SLOT_HOLE when the next one isn't SLOT_END,
 * else SLOT_END on the first slot of the free run it now starts (the asm
 * walks back over the holes before it). Returns the byte it marked. */
static signed char *FreeSlot(signed char *state, int step) {
  if (state[step] != SLOT_END) {
    *state = SLOT_HOLE;
    return state;
  }
  while (state[-step] == SLOT_HOLE)
    state -= step;
  *state = SLOT_END;
  return state;
}

/* Takes pMoby out of its collision cell; a level moby (below g_DynMobys)
 * is marked dead, a spawned one's slot and props block go back to the
 * free lists, the allocation pointers moving back to them if they're
 * before. */
static void Free(Moby *pMoby) {
  Moby *slot;
  u_char *end;
  if (pMoby->m_CollisionRegion >= 0)
    ChainUnlink(pMoby, pMoby->m_CollisionRegion);
  if (g_DynMobys - pMoby > 0) {
    *MobyState(pMoby) = SLOT_DEAD;
    return;
  }
  g_DynMobyCount--;
  slot = (Moby *)((u_char *)FreeSlot(MobyState(pMoby), sizeof(Moby)) - offsetof(Moby, m_State));
  if (g_MobyAllocPtr - slot >= 0)
    g_MobyAllocPtr = slot;
  end = (u_char *)FreeSlot(PropsState((u_char *)pMoby->m_Props + PROPS_SIZE), -PROPS_SIZE) + 1;
  if ((u_char *)g_PropsAllocPtr - end <= 0)
    g_PropsAllocPtr = end;
}

void MobyFree(Moby *pMoby) {
  SHADOW_VOID(MobyFree, MobyFree_gen(pMoby), Free(pMoby));
  Free(pMoby);
}
#else
void MobyFree(Moby *pMoby) { MobyFree_gen(pMoby); }
#endif

/* ===================================================================== */
/* A moby's collision state and rotation                                  */
/* ===================================================================== */

#if !M2C_MobyUpdateCollision
/* From the moby's animation: its per-frame progress and its frame's
 * collision model; it goes into the collision grid's list for its cell
 * (the second grid when the model's first collision model isn't a
 * pointer), or none (region -1) when the model has none; then its rotation
 * matrix. */
static void UpdateCollision(Moby *pMoby) {
  const Model *model = g_Models[(u_short)pMoby->m_Class];
  const AnimationState *a = &pMoby->m_AnimationState;
  void *first = model->m_CollisionModels[0];
  pMoby->m_AnimationState.m_PerFrameProgress = model->m_Animations[a->m_Animation]->m_ProgressPerTick;
  pMoby->m_CollisionGroup = FrameCollisionModel(model, a->m_Animation, a->m_Frame);
  pMoby->m_CollisionRegion = -1;
  if (first) {
    int region = MobyCell(pMoby) + (MEM_COLL_TAGGED(first) ? 0 : 0x400);
    pMoby->m_CollisionRegion = region;
    ChainLink(pMoby, region);
  }
  MobyRotationMatrix(pMoby);
}

void MobyUpdateCollision(Moby *pMoby) {
  SHADOW_VOID(MobyUpdateCollision, MobyUpdateCollision_gen(pMoby), UpdateCollision(pMoby));
  UpdateCollision(pMoby);
}
#else
void MobyUpdateCollision(Moby *pMoby) { MobyUpdateCollision_gen(pMoby); }
#endif

#if !M2C_MobySetShaded
static void SetShaded(Moby *pMoby) {
  pMoby->m_Renderer.raw = 0;
  pMoby->m_AnimationState.m_PerFrameProgress = 0;
  pMoby->m_CollisionGroup = NULL;
  pMoby->m_CollisionRegion = -1;
}

void MobySetShaded(Moby *pMoby) {
  SHADOW_VOID(MobySetShaded, MobySetShaded_gen(pMoby), SetShaded(pMoby));
  SetShaded(pMoby);
}
#else
void MobySetShaded(Moby *pMoby) { MobySetShaded_gen(pMoby); }
#endif

#if !M2C_func_800529E4
/* UPDATE_PROP_COLLISION: the collision model of the frame it's in (the
 * next frame once it has progress). UPDATE_PROP_CHAIN: if it's in the
 * grid and its cell changed, moves it to the new cell's list (keeping
 * which of the two grids). UPDATE_PROP_ROTMATRIX: its rotation matrix. */
static void UpdateProps(Moby *pMoby, int pFlags) {
  if (pFlags & UPDATE_PROP_COLLISION) {
    const AnimationState *a = &pMoby->m_AnimationState;
    const Model *model = g_Models[(u_short)pMoby->m_Class];
    if (a->m_FrameProgress > 0)
      pMoby->m_CollisionGroup = FrameCollisionModel(model, a->m_NextAnimation, a->m_NextFrame);
    else
      pMoby->m_CollisionGroup = FrameCollisionModel(model, a->m_Animation, a->m_Frame);
  }
  if (pFlags & UPDATE_PROP_CHAIN) {
    int region = pMoby->m_CollisionRegion, cell = MobyCell(pMoby) | (region & 0x400);
    if (cell != region && region >= 0) {
      ChainUnlink(pMoby, region);
      pMoby->m_CollisionRegion = cell;
      ChainLink(pMoby, cell);
    }
  }
  if (pFlags & UPDATE_PROP_ROTMATRIX)
    MobyRotationMatrix(pMoby);
}

void func_800529E4(Moby *pMoby, int pFlags) {
#if MOBYS_SHADOW
  {
    int i, n = FuzzBegin((u_int)pMoby->m_Position.x ^ (u_int)pFlags);
    for (i = 0; i < n; i++) {
      static const int ranges[3] = {0x100, 0x2000, 0x8000};
      /* Subsets of the call's own flags (the moby's state is valid for
       * those), with the rotation always allowed. */
      int f = FuzzRand() % 8 & (pFlags | UPDATE_PROP_ROTMATRIX), r = ranges[FuzzRand() % 3];
      pMoby->m_Position.x += (int)(FuzzRand() % (2 * r + 1)) - r;
      pMoby->m_Position.y += (int)(FuzzRand() % (2 * r + 1)) - r;
      pMoby->m_Rotation.x = FuzzRand(), pMoby->m_Rotation.y = FuzzRand(), pMoby->m_Rotation.z = FuzzRand();
      FUZZ_ONE(func_800529E4, func_800529E4_gen(pMoby, f), UpdateProps(pMoby, f));
    }
  }
#endif
  SHADOW_VOID(func_800529E4, func_800529E4_gen(pMoby, pFlags), UpdateProps(pMoby, pFlags));
  UpdateProps(pMoby, pFlags);
}
#else
void func_800529E4(Moby *pMoby, int pFlags) { func_800529E4_gen(pMoby, pFlags); }
#endif

/* ===================================================================== */
/* Vertices                                                               */
/* ===================================================================== */

/* A frame's vertex words: the frame word's 21-bit offset into RAM. */
static const u_int *FrameVertices(const AnimationHeader *anim, int frame) {
  return (const u_int *)MEM_ANIM21(anim->m_Frames[frame].m.m_Data);
}

/* A vertex word: z in bits 21-31, x in 11-21, y in 0-11 doubled, each
 * signed, shifted up by the animation's scale (sllv: mod 32). */
static void UnpackVertex(u_int w, int scale, int *x, int *y, int *z) {
  *z = Shl((int)w >> 21, scale);
  *x = Shl((int)(w << 10) >> 21, scale);
  *y = Shl((int)(w << 20) >> 19, scale);
}

#if !M2C_MobyGetAnimatedVertex
/* The vertex in world space: from the current frame, or blended towards
 * the next one by the frame progress (64ths: GPF then GPL on 16-bit
 * registers, >> 6), rotated by the moby's matrix (RTPS, read back as IR;
 * the asm loads VXY0 = x + (y << 16) without masking x, so a negative x
 * borrows one from y) and placed at its position, the axes as
 * (IR3, -IR1, -IR2). The animation's scale is a signed byte here. */
static void AnimatedVertex(Moby *pMoby, int pVertexIdx, Vector3D *pOut) {
  const Model *model = g_Models[(u_short)pMoby->m_Class];
  const AnimationState *a = &pMoby->m_AnimationState;
  const AnimationHeader *anim = model->m_Animations[a->m_Animation];
  const short *r = &pMoby->m_RotationMatrix.m[0][0];
  int scale = (signed char)anim->m_Scale, t = a->m_FrameProgress, x, y, z, vxy, vx, vy;
  gteRegs.CP2C.p[0].d = ((const u_int *)r)[0];
  gteRegs.CP2C.p[1].d = ((const u_int *)r)[1];
  gteRegs.CP2C.p[2].d = ((const u_int *)r)[2];
  gteRegs.CP2C.p[3].d = ((const u_int *)r)[3];
  gteRegs.CP2C.p[4].d = (short)((const u_int *)r)[4];
  gteRegs.CP2C.p[5].d = gteRegs.CP2C.p[6].d = gteRegs.CP2C.p[7].d = 0;
  UnpackVertex(FrameVertices(anim, a->m_Frame)[pVertexIdx], scale, &x, &y, &z);
  if (t > 0) {
    const AnimationHeader *next = model->m_Animations[a->m_NextAnimation];
    int nx, ny, nz;
    UnpackVertex(FrameVertices(next, a->m_NextFrame)[pVertexIdx], scale, &nx, &ny, &nz);
    z = ((0x40 - t) * S16(z) + t * S16(nz)) >> 6;
    x = ((0x40 - t) * S16(x) + t * S16(nx)) >> 6;
    y = ((0x40 - t) * S16(y) + t * S16(ny)) >> 6;
  }
  vxy = (int)((u_int)x + ((u_int)y << 16));
  vx = S16(vxy), vy = S16(vxy >> 16), z = S16(z);
  pOut->x = pMoby->m_Position.x + RotClamp(r + 6, vx, vy, z);
  pOut->y = pMoby->m_Position.y - RotClamp(r + 0, vx, vy, z);
  pOut->z = pMoby->m_Position.z - RotClamp(r + 3, vx, vy, z);
}

void MobyGetAnimatedVertex(Moby *pMoby, int pVertexIdx, Vector3D *pOutVector) {
#if MOBYS_SHADOW
  {
    int i, n = FuzzBegin((u_int)pMoby->m_Position.x ^ (u_int)pVertexIdx);
    AnimationState *a = &pMoby->m_AnimationState;
    /* The next frame is only valid to read when the call's was in use. */
    int blend = a->m_FrameProgress > 0;
    for (i = 0; i < n; i++) {
      int v = FuzzRand() % 64;
      a->m_FrameProgress = blend ? FuzzRand() % 64 : 0;
      pMoby->m_RotationMatrix.m[FuzzRand() % 3][FuzzRand() % 3] = (short)FuzzRand();
      FUZZ_ONE(MobyGetAnimatedVertex, MobyGetAnimatedVertex_gen(pMoby, v, pOutVector),
               AnimatedVertex(pMoby, v, pOutVector));
    }
  }
#endif
  SHADOW_VOID(MobyGetAnimatedVertex, MobyGetAnimatedVertex_gen(pMoby, pVertexIdx, pOutVector),
              AnimatedVertex(pMoby, pVertexIdx, pOutVector));
  AnimatedVertex(pMoby, pVertexIdx, pOutVector);
}
#else
void MobyGetAnimatedVertex(Moby *pMoby, int pVertexIdx, Vector3D *pOutVector) {
  MobyGetAnimatedVertex_gen(pMoby, pVertexIdx, pOutVector);
}
#endif

#if !M2C_func_80052F38
/* Vertex pVertex - 1 of one of Spyro's animation frames, in the model's
 * units (x, y, z), by summing the frame's deltas from the first vertex
 * (pVertex of them). A frame is a list of base words (the animation's
 * m_AnimationVertices, one per vertex) and, from its frame word's 21-bit
 * offset x 2, a header whose third word's top 12 bits lead to a byte list
 * and whose data from 0x18 is a list of 16-bit entries. Each step's
 * encoding is chosen by the step before (the frame word's bit 21 for the
 * first): 8-bit (D_8006D614's packed delta by bits 1-7, plus the base word;
 * bit 0 picks the next step's encoding) or 16-bit (a 7-bit delta per axis
 * plus the base word, or with bit 15 set an absolute position from two
 * entries, no base word; bit 14 picks the next step's). */
static void SpyroVertex(int pVertex, Vector3D *pOut, int pAnimation, int pFrame) {
  const AnimationHeader *anim = SPYRO_MODEL->m_Animations[pAnimation];
  u_int frame = ((const u_int *)anim->m_Frames)[pFrame];
  int wide = (frame >> 21) & 1, x = 0, y = 0, z = 0;
  const u_char *data = (const u_char *)MEM_ANIM_HALF(frame);
  const int *base = anim->m_AnimationVertices;
  const short *entries = (const short *)(data + 0x18);
  const u_char *bytes = data + 0x18 + (((const u_int *)data)[2] >> 20);
  do {
    if (!wide) {
      int b = *bytes++, w = D_8006D614[(b >> 1) & 0x7F] + *base++;
      wide = b & 1;
      x += w >> 21, y += Shl(w, 11) >> 21, z += Shl(w, 22) >> 21;
    } else {
      int e = *entries++;
      wide = e & 0x4000;
      if (e >= 0) {
        int w = *base++;
        x += Shl(e, 18) >> 25, y += Shl(e, 23) >> 25, z += Shl(e, 28) >> 25;
        x += w >> 21, y += Shl(w, 11) >> 21, z += Shl(w, 22) >> 21;
      } else {
        int e2 = *(const u_short *)entries++;
        base++;
        x = Shl(e, 18) >> 21;
        y = (Shl(e2, 12) | Shl(e, 28)) >> 21;
        z = Shl(e2, 22) >> 21;
      }
    }
  } while (--pVertex);
  pOut->x = x, pOut->y = y, pOut->z = z;
}

void func_80052F38(int pVertex, Vector3D *pOut, int pAnimation, int pFrame) {
#if MOBYS_SHADOW
  {
    int i, n = FuzzBegin((u_int)pVertex ^ ((u_int)pAnimation << 8) ^ ((u_int)pFrame << 16));
    for (i = 0; i < n; i++) {
      int v = 1 + FuzzRand() % pVertex;
      FUZZ_ONE(func_80052F38, func_80052F38_gen(v, pOut, pAnimation, pFrame), SpyroVertex(v, pOut, pAnimation, pFrame));
    }
  }
#endif
  SHADOW_VOID(func_80052F38, func_80052F38_gen(pVertex, pOut, pAnimation, pFrame),
              SpyroVertex(pVertex, pOut, pAnimation, pFrame));
  SpyroVertex(pVertex, pOut, pAnimation, pFrame);
}
#else
void func_80052F38(int pVertex, Vector3D *pOut, int pAnimation, int pFrame) {
  func_80052F38_gen(pVertex, pOut, pAnimation, pFrame);
}
#endif

/* ===================================================================== */
/* Death sparks, shadow                                                   */
/* ===================================================================== */

#if !M2C_func_800530C0
#define MAX_SPARKS 128 /* the asm's scratchpad holds 128 eight-byte entries */

/* One particle per second vertex of the moby's current frame (half its
 * vertex count): the vertex, decoded from the frame's words and 16-bit
 * deltas (m_ShortOffset), rotated by the moby's matrix (MVMVA on IR, read
 * back as MAC, axes (MAC3, -MAC1, -MAC2) kept as shorts), in quarter units
 * from its position. Three in four are smoke puffs of size pSize + 0-3,
 * the rest sparks with a random end point; rand() picks each. The asm
 * leaves the rotation, the quarter position (TR) and pSize (in RBK, its
 * spare register) in the GTE. */
static void DeathSparks(Moby *pMoby, int pSize) {
  const Model *model = g_Models[(u_short)pMoby->m_Class];
  const AnimationState *a = &pMoby->m_AnimationState;
  const AnimationHeader *anim = model->m_Animations[a->m_Animation];
  const AnimationFrame *frame = &anim->m_Frames[a->m_Frame];
  const short *r = &pMoby->m_RotationMatrix.m[0][0];
  const u_int *words = FrameVertices(anim, a->m_Frame);
  const short *deltas = (const short *)((const u_char *)words + frame->m_ShortOffset * 4);
  int scale = anim->m_Scale, dshift = anim->m_ShortEncodeShift + 1 + scale;
  int n = anim->m_VertCountHigh >> 1, i, k, more = 0, x = 0, y = 0, z = 0;
  int tx = (int)((u_int)pMoby->m_Position.x >> 2), ty = (int)((u_int)pMoby->m_Position.y >> 2),
      tz = (int)((u_int)pMoby->m_Position.z >> 2);
  short pts[MAX_SPARKS][3];
  gteRegs.CP2C.p[13].d = pSize; /* RBK */
  for (k = 0; k < 5; k++)
    gteRegs.CP2C.p[k].d = k < 4 ? ((const u_int *)r)[k] : (short)((const u_int *)r)[4];
  gteRegs.CP2C.p[5].d = tx, gteRegs.CP2C.p[6].d = ty, gteRegs.CP2C.p[7].d = tz;
  for (i = 0; i < 2 * n - 1; i++) {
    if (!more) {
      u_int w = *words++;
      more = w & 1;
      UnpackVertex(w, scale, &x, &y, &z);
    } else {
      int d = *deltas++;
      z += Shl(d >> 11, dshift);
      x -= Shl(Shl(d, 21) >> 27, dshift);
      y -= Shl(Shl(d, 26) >> 27, dshift);
      more = d & 1;
    }
    if (i & 1)
      continue;
    {
      int ix = S16(x), iy = S16(y), iz = S16(z);
      /* MVMVA (sf 1, no translation) of IR = (x, y, z), as MAC1-3. */
      int m1 = (int)(((long long)(r[0] * ix) + r[1] * iy + r[2] * iz) >> 12);
      int m2 = (int)(((long long)(r[3] * ix) + r[4] * iy + r[5] * iz) >> 12);
      int m3 = (int)(((long long)(r[6] * ix) + r[7] * iy + r[8] * iz) >> 12);
      pts[i >> 1][0] = m3, pts[i >> 1][1] = -m1, pts[i >> 1][2] = -m2;
    }
  }
  for (i = 0; i < n; i++) {
    int rnd = rand();
    Particle *p = func_80053570(2);
    int px = (pts[i][0] >> 2) + tx, py = (pts[i][1] >> 2) + ty, pz = (pts[i][2] >> 2) + tz;
    p->m_Data.m_Position.x = px, p->m_Data.m_Position.y = py, p->m_Data.m_Position.z = pz;
    if (rnd & 3) {
      ParticleRotatedQuadData *q = &p->m_Data.m_RotatedQuad;
      short *drift = (short *)&p->m_Data.m_Raw[14];
      int grey = ((rnd & 7) << 3) + 0x30;
      q->m_Size = (rnd & 3) + pSize;
      q->m_Rotation = rnd;
      q->m_Color.r = q->m_Color.g = q->m_Color.b = grey;
      q->m_Color.s = 0x2E;
      q->m_TextureIndex = 0;
      q->m_OtOffset = 4;
      drift[0] = Shl(rnd, 17) >> 29;
      drift[1] = Shl(rnd, 14) >> 29;
      drift[2] = (Shl(rnd, 11) >> 29) + 2;
      p->m_Class = 0xB;
      p->m_Timer = 0;
      p->m_WasRendered = 1;
    } else {
      ParticleLineData *l = &p->m_Data.m_Line;
      l->m_End.x = px + (Shl(rnd, 17) >> 27);
      l->m_End.y = py + (Shl(rnd, 20) >> 27);
      l->m_End.z = pz - ((int)((u_int)Shl(rnd, 23) >> 29) + 6);
      p->m_Class = 0x48;
      p->m_RenderType = 1;
      p->m_Timer = (rnd >> 10) & 0x1F;
      p->m_WasRendered = 1;
      *(u_int *)&l->m_Color0 = 0x5080F0F0;
      *(u_int *)&l->m_Color1 = 0x024080C0;
    }
  }
}

void func_800530C0(Moby *pMoby, int pSize) {
  SHADOW_VOID(func_800530C0, func_800530C0_gen(pMoby, pSize), DeathSparks(pMoby, pSize));
  DeathSparks(pMoby, pSize);
}
#else
void func_800530C0(Moby *pMoby, int pSize) { func_800530C0_gen(pMoby, pSize); }
#endif

#if !M2C_MobyUpdateShadow
/* An angle from the octant of (a, b): (b << 5) / a or (a << 5) / -b, plus
 * 0, 0x40, 0x80 or 0xC0 (64ths of a quarter turn). */
static int ShadowAngle(int a, int b) {
  if (a - b < 0)
    return a + b < 0 ? MipsDiv(Shl(b, 5), a) + 0x80 : MipsDiv(Shl(a, 5), -b) + 0x40;
  return a + b < 0 ? MipsDiv(Shl(a, 5), -b) + 0xC0 : MipsDiv(Shl(b, 5), a);
}

/* A moby whose shadow word is negative (it has a shadow) gets the floor's
 * slope under it: from the last collision normal (ColFindFloor's), the
 * angles of (|xz|, y) and (z, x) in 6 bits each, and the floor's height
 * / 4 below them, with bit 31 kept. */
static void UpdateShadow(Moby *pMoby) {
  const Vector3D *n = &g_CollisionNormal;
  int xz = (int)((u_int)(S16(n->x) * S16(n->x)) + (u_int)(S16(n->z) * S16(n->z))), tilt, turn;
  if (pMoby->m_ShadowDistance >= 0)
    return;
  if (xz)
    xz = TableSqrt(xz);
  tilt = ShadowAngle(xz, n->y);
  turn = ShadowAngle(n->z, n->x);
  pMoby->m_ShadowDistance = (int)(0x80000000u | ((0u - (u_int)turn) >> 2 & 0x3F) << 16 |
                                  ((0u - (u_int)tilt) >> 2 & 0x3F) << 22 | (u_int)g_CollisionPoint.z >> 2);
}

void MobyUpdateShadow(Moby *pMoby) {
#if MOBYS_SHADOW
  {
    int i, n = FuzzBegin((u_int)pMoby->m_ShadowDistance ^ (u_int)g_CollisionNormal.x);
    static const int edges[] = {0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000, 0x8000};
    for (i = 0; i < n; i++) {
      int *c = &g_CollisionNormal.x, k;
      for (k = 0; k < 3; k++)
        c[k] = FuzzRand() % 4 ? (int)(FuzzRand() % 0x2001) - 0x1000 : edges[FuzzRand() % 8];
      g_CollisionPoint.z = (int)FuzzRand() - 0x400000;
      if (FuzzRand() % 4)
        pMoby->m_ShadowDistance |= (int)0x80000000;
      FUZZ_ONE(MobyUpdateShadow, MobyUpdateShadow_gen(pMoby), UpdateShadow(pMoby));
    }
  }
#endif
  SHADOW_VOID(MobyUpdateShadow, MobyUpdateShadow_gen(pMoby), UpdateShadow(pMoby));
  UpdateShadow(pMoby);
}
#else
void MobyUpdateShadow(Moby *pMoby) { MobyUpdateShadow_gen(pMoby); }
#endif

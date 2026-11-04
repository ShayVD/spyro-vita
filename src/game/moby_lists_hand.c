/* The moby lists (asm/moby_lists.s): which mobys update, which are drawn
 * by which renderer, and their animation step, written by hand from the
 * asm so it reads as C (docs/native-plan.md stage 3, decisions D94).
 * Replaces the mips2c unit (cmake/mips2c.cmake M2C_HAND_MULTI); checked
 * against the unit with the shadows of hand_shadow.h and against
 * PCSX-Redux with the oracle replay (docs/translation-log.md).
 *
 * What the asm leaves elsewhere, the hand C doesn't (D83, D87): the update
 * list's distance test squares in the GTE (SQR), whose data registers
 * nothing reads; the animation step works on a moby's two state words in
 * the scratchpad (0x1F800000-7) and keeps its sound queue's ends in the
 * spill area (D_80077DD8), which nothing reads after it. The lists' slots
 * past their ends hold what the asm wrote there before it decided not to
 * keep a moby; each is overwritten by a later entry or the list's 0, so
 * the hand C doesn't write them. The sound queue's one such store is kept:
 * the slot after its last entry is read by nothing, but stays in RAM.
 *
 * M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) takes that function from the mips2c
 * unit instead, linked as <name>_gen, for A/B tests. */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "hand_shadow.h"
#include "moby.h"
#include "moby_lists.h"
#include "spu.h"
#include "spyro.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static_assert(sizeof(Moby) == 0x58, "Moby");
static_assert(offsetof(Moby, m_AnimationState) == 0x3C && offsetof(Moby, m_Pod) == 0x43, "Moby");
static_assert(offsetof(Moby, m_State) == 0x48 && offsetof(Moby, m_SectorIndex) == 0x4A, "Moby");
static_assert(offsetof(Moby, m_Renderer) == 0x4B && offsetof(Moby, m_WasDrawn) == 0x51, "Moby");
static_assert(offsetof(Moby, m_UpdateDistance) == 0x52 && offsetof(Moby, m_SoundChannel) == 0x54, "Moby");
static_assert(offsetof(Model, m_Sounds) == 4 && offsetof(Model, m_Animations) == 0x38, "Model");
static_assert(offsetof(AnimationHeader, m_ProgressPerTick) == 0xC && offsetof(AnimationHeader, m_Frames) == 0x24,
              "AnimationHeader");

extern char D_8006FCF4[];   /* g_SonyImage's buffer: the lists */
extern char D_80077868[32]; /* a byte per pod: one of its mobys updates */
extern u_char D_800771C8[]; /* a byte per sector, not 0 when it was visible */

/* The mips2c unit's entries, linked as <name>_gen for A/B tests and the
 * shadows (cmake M2C_HAND_MULTI). */
u_int func_80051FEC_gen(), func_800521C0_gen(), func_800522C0_gen();

/* The lists in D_8006FCF4, each ended by 0. */
#define UPDATE_LIST ((Moby **)(D_8006FCF4 + 0x400))
#define DRAW_LIST ((Moby **)(D_8006FCF4))          /* the shaded renderer */
#define SHINY_LIST ((Moby **)(D_8006FCF4 + 0x2200)) /* the shiny renderer */
#define LIT_LIST ((Moby **)(D_8006FCF4 + 0x2400))   /* renderer byte 0 (func_80022A2C) */
/* The animation step's sound queue: (sound, moby) pairs. */
#define SOUND_QUEUE ((u_int *)(D_8006FCF4))

/* A moby's state byte, read signed: 0 or more in use, -1 the end of the
 * level's mobys, -2 and -3 free. */
static inline int MobyState(const Moby *m) { return (signed char)m->m_State; }

#if M2C_func_80051FEC
void func_80051FEC(void) { func_80051FEC_gen(); }
#else
/* Whether a moby is near enough Spyro to update: within its update
 * distance (a signed byte, in 1024ths; 0 never, negative always) in x and
 * y on a circle and in z on the box. The asm squares the eighths in the
 * GTE (SQR); they fit its 16-bit IR, since each is within the distance. */
static int NearSpyro(const Moby *m) {
  int dist = (signed char)m->m_UpdateDistance, r, dx, dy, dz, r8;
  if (dist == 0)
    return 0;
  r = dist << 10;
  if (r < 0)
    return 1;
  dx = m->m_Position.x - g_Spyro.m_Position.x;
  if (dx + r <= 0 || dx - r >= 0)
    return 0;
  dy = m->m_Position.y - g_Spyro.m_Position.y;
  if (dy + r <= 0 || dy - r >= 0)
    return 0;
  dz = m->m_Position.z - g_Spyro.m_Position.z;
  if (dz + r <= 0 || dz - r >= 0)
    return 0;
  r8 = r >> 3;
  dx >>= 3;
  dy >>= 3;
  return r8 * r8 - dx * dx - dy * dy > 0;
}

static void UpdateList(void) {
  Moby **out = UPDATE_LIST, *m;
  int pod;
  memset(D_80077868, 0, sizeof D_80077868);
  /* The level's mobys drawn last frame or near Spyro. One in a pod isn't
   * listed: its pod is marked, and all the pod's mobys in use follow. */
  for (m = g_LevelMobys; MobyState(m) != -1; m++) {
    if (MobyState(m) < 0)
      continue;
    if (!m->m_WasDrawn && !NearSpyro(m))
      continue;
    pod = (signed char)m->m_Pod;
    if (pod >= 0)
      D_80077868[pod] = 1;
    else
      *out++ = m;
  }
  /* The marked pods' mobys: indices into the level's mobys, the last with
   * its top bit set. */
  for (pod = 0; pod < (int)sizeof D_80077868; pod++) {
    const short *index;
    int i;
    if (!D_80077868[pod])
      continue;
    index = (const short *)g_MobyPods[pod];
    do {
      i = *index++;
      m = &g_LevelMobys[i & 0x7FFF];
      if (MobyState(m) >= 0)
        *out++ = m;
    } while (i >= 0);
  }
  *out = NULL;
}

#if HAND_SHADOW
/* Fuzzing (SPYRO_HAND_FUZZ=N): Spyro moved by up to 0x8000 per axis, and
 * a random quarter of the level's mobys given random drawn flags and
 * update distances (positive, negative and 0). */
static void FuzzUpdateList(void) {
  int n = HandFuzzBegin("func_80051FEC", (u_int)g_Spyro.m_Position.x ^ (u_int)g_Spyro.m_Position.y << 7), k;
  Moby *m;
  for (k = 0; k < n; k++) {
    g_Spyro.m_Position.x += (int)(HandFuzzRand() & 0xFFFF) - 0x8000;
    g_Spyro.m_Position.y += (int)(HandFuzzRand() & 0xFFFF) - 0x8000;
    g_Spyro.m_Position.z += (int)(HandFuzzRand() & 0x3FFF) - 0x2000;
    for (m = g_LevelMobys; MobyState(m) != -1; m++)
      if (!(HandFuzzRand() & 3)) {
        m->m_WasDrawn = HandFuzzRand() & 1;
        m->m_UpdateDistance = HandFuzzRand() & 7 ? HandFuzzRand() % 24 : HandFuzzRand();
      }
    HAND_FUZZ_VOID(func_80051FEC, func_80051FEC_gen(), UpdateList());
  }
}
#endif

/* Lists the mobys to update this tick, at D_8006FCF4+0x400. */
void func_80051FEC(void) {
#if HAND_SHADOW
  FuzzUpdateList();
#endif
  HAND_SHADOW_VOID(func_80051FEC, func_80051FEC_gen(), UpdateList());
  UpdateList();
}
#endif

#if M2C_func_800521C0
void func_800521C0(void) { func_800521C0_gen(); }
#else
static void DrawLists(void) {
  Moby **shaded = DRAW_LIST, **shiny = SHINY_LIST, **lit = LIT_LIST, ***list, *m;
  for (m = g_LevelMobys;; m++) {
    int sector;
    if (MobyState(m) < 0) {
      if (MobyState(m) == -1)
        break;
      continue;
    }
    /* By the renderer byte: 0 lit, the shiny bit (7) shiny, else shaded. */
    list = !m->m_Renderer.raw ? &lit : (m->m_Renderer.raw & 0x80) ? &shiny : &shaded;
    /* A moby in a sector that wasn't visible isn't drawn, and no longer
     * counts as drawn; 0xFF is no sector. */
    sector = m->m_SectorIndex;
    if (sector != 0xFF && !D_800771C8[sector])
      m->m_WasDrawn = 0;
    else
      *(*list)++ = m;
  }
  *shaded = NULL;
  *shiny = NULL;
  *lit = NULL;
}

#if HAND_SHADOW
/* Fuzzing (SPYRO_HAND_FUZZ=N): the sectors' visibility bytes, and a
 * random quarter of the mobys given random renderer bytes and sectors. */
static void FuzzDrawLists(void) {
  int n = HandFuzzBegin("func_800521C0", (u_int)(uintptr_t)g_LevelMobys ^ D_800771C8[0]), k, i;
  Moby *m;
  for (k = 0; k < n; k++) {
    for (i = 0; i < 256; i++)
      D_800771C8[i] = HandFuzzRand() & 1 ? 0 : (u_char)HandFuzzRand();
    for (m = g_LevelMobys; MobyState(m) != -1; m++)
      if (!(HandFuzzRand() & 3)) {
        m->m_Renderer.raw = HandFuzzRand() & 1 ? 0 : (u_char)HandFuzzRand();
        m->m_SectorIndex = HandFuzzRand() & 3 ? (u_char)HandFuzzRand() : 0xFF;
      }
    HAND_FUZZ_VOID(func_800521C0, func_800521C0_gen(), DrawLists());
  }
}
#endif

/* Lists the level's mobys in use by renderer: D_8006FCF4 (shaded), +0x2200
 * (shiny), +0x2400 (lit). */
void func_800521C0(void) {
#if HAND_SHADOW
  FuzzDrawLists();
#endif
  HAND_SHADOW_VOID(func_800521C0, func_800521C0_gen(), DrawLists());
  DrawLists();
}
#endif

#if M2C_func_800522C0
void func_800522C0(Moby **pMobyList, u_int pSpeedDiv) { func_800522C0_gen(pMobyList, pSpeedDiv); }
#else
/* One moby's animation step. pSpeedDiv's low 5 bits shift the frame speed
 * down; negative is a catch-up step (a tick of more than two vblanks): it
 * keeps the flags of the step before, and a finished animation stays
 * finished. The new frame's sound, if it has one, goes on the queue. */
static u_int *Animate(Moby *m, int speedDiv, u_int *queue) {
  AnimationState *a = &m->m_AnimationState;
  u_int progress = a->m_FrameProgress + ((u_int)a->m_PerFrameProgress >> (speedDiv & 31));
  const Model *model;
  const AnimationHeader *anim;
  int frame, sound;
  if (!(progress >> 6)) {
    /* No new frame. */
    if (speedDiv >= 0)
      a->m_AnimationFlags = 0;
    a->m_FrameProgress = progress;
    return queue;
  }
  frame = a->m_NextFrame;
  if (a->m_Animation == a->m_NextAnimation) {
    /* On to the next frame, back to the first after the last (flag 2). */
    int flags = 0;
    if (speedDiv < 0 && (signed char)a->m_AnimationFlags != 0) {
      if (a->m_AnimationFlags & 2)
        return queue;
      flags = a->m_AnimationFlags | 4;
    }
    model = g_Models[(u_short)m->m_Class];
    anim = model->m_Animations[a->m_NextAnimation];
    a->m_FrameProgress = progress & 0x3F;
    a->m_Frame = frame;
    a->m_NextFrame = frame + 1;
    a->m_AnimationFlags = flags | 1;
    if (frame + 1 - (int)(u_short)anim->m_NumFrames >= 0) {
      a->m_NextFrame = 0;
      a->m_AnimationFlags = flags | 3;
    }
  } else {
    /* The next animation starts, at its own speed; at speed 0 it holds
     * its frame. */
    model = g_Models[(u_short)m->m_Class];
    anim = model->m_Animations[a->m_NextAnimation];
    a->m_FrameProgress = 0;
    a->m_Animation = a->m_NextAnimation;
    a->m_Frame = frame;
    a->m_NextFrame = frame + 1;
    a->m_AnimationFlags = 1;
    a->m_PerFrameProgress = anim->m_ProgressPerTick;
    if (!anim->m_ProgressPerTick)
      a->m_NextFrame = frame;
  }
  /* The asm stores the moby in the queue's next slot before it knows
   * whether the frame has a sound. */
  queue[1] = (u_int)(uintptr_t)m;
  sound = (signed char)(anim->m_Frames[frame].m.m_Data >> 24);
  if (sound >= 0) {
    queue[0] = model->m_Sounds[sound];
    queue += 2;
  }
  return queue;
}

static void AnimateList(Moby **list, int speedDiv) {
  u_int *queue = SOUND_QUEUE, *q;
  Moby *m;
  while ((m = *list++))
    queue = Animate(m, speedDiv, queue);
  /* The frames' sounds, from each moby. */
  for (q = SOUND_QUEUE; q != queue; q += 2) {
    Moby *from = (Moby *)(uintptr_t)q[1];
    PlaySound(q[0], from, 8, &from->m_SoundChannel);
  }
}

#if HAND_SHADOW
/* Fuzzing (SPYRO_HAND_FUZZ=N): each variant a speed shift of 0 to 2,
 * half of them catch-up steps, and each listed moby's frame progress,
 * speed, flags and next frame (within its animation) at random, the next
 * animation a random one of its model's a third of the time. The unit
 * runs in a child, as for the shadow. */
static void FuzzAnimate(Moby **list) {
  int n = HandFuzzBegin("func_800522C0", (u_int)(uintptr_t)list[0] ^ g_DeltaTime), k;
  Moby **l, *m;
  for (k = 0; k < n; k++) {
    u_int div = HandFuzzRand() % 3 | (k & 1 ? 0x80000000u : 0);
    for (l = list; (m = *l); l++) {
      AnimationState *a = &m->m_AnimationState;
      const Model *model = g_Models[(u_short)m->m_Class];
      if (model->m_NumAnimations > 0 && !(HandFuzzRand() % 3))
        a->m_NextAnimation = HandFuzzRand() % model->m_NumAnimations;
      a->m_FrameProgress = HandFuzzRand();
      a->m_PerFrameProgress = HandFuzzRand();
      a->m_AnimationFlags = HandFuzzRand() & 7;
      if (model->m_Animations[a->m_NextAnimation]->m_NumFrames > 0)
        a->m_NextFrame = HandFuzzRand() % (u_short)model->m_Animations[a->m_NextAnimation]->m_NumFrames;
    }
    if (HandShadowFork("func_800522C0", func_800522C0_gen, (u_int)(uintptr_t)list, div)) {
      AnimateList(list, (int)div);
      HandShadowForkEnd("fuzz func_800522C0");
    }
    HandFuzzRestore();
  }
}
#endif

/* The animation step of every moby on pMobyList. */
void func_800522C0(Moby **pMobyList, u_int pSpeedDiv) {
#if HAND_SHADOW
  FuzzAnimate(pMobyList);
#endif
  HAND_SHADOW_FORK(func_800522C0, pMobyList, pSpeedDiv, AnimateList(pMobyList, (int)pSpeedDiv));
  AnimateList(pMobyList, (int)pSpeedDiv);
}
#endif

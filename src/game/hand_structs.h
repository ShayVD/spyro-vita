/* Named views of decomp structs whose fields are still unk_ in the decomp's
 * headers, for the hand-written C in src/game (docs/translation-log.md).
 * Each view has the decomp struct's exact layout (checked below) and is read
 * through the decomp's own global, so nothing in the submodule changes. */
#ifndef SPYRO_HAND_STRUCTS_H
#define SPYRO_HAND_STRUCTS_H

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stddef.h>

#include "common.h"

#include "gamepad.h"
#include "matrix.h"
#include "spyro.h"
#include "vector.h"

/* SpyroFlame (spyro.h), the flame breath: eight puffs, each flying along a
 * path from Spyro's mouth until it ages out or hits something. */
typedef struct {
  Vector3D m_Position; /* The mouth, in world space */
  Vector3D8 m_Rotation;
  u_char pad1;
  Tiledef m_FlameTexture;
  Tiledef m_SuperFlameTexture;
  u_char m_PuffAge[8];    /* Ticks along its path; 0 = no puff */
  u_char m_PuffEndAge[8]; /* Age it hit something at (0x20 = none) */
  u_char m_PuffState[8];  /* FLAME_PUFF_* */
  int unk_38[24];
  u_char m_IsFlameActive;
  u_char m_PuffsEmitted;  /* Puffs started this breath (unk_99[0]) */
  u_char m_Emitting;      /* Still following the head (unk_99[1]) */
  u_char m_Pattern;       /* Row of D_8006E1C8, the puff order (unk_99[2]) */
  int m_IsSuperFlame;     /* Fairy kiss flame (unk_9c) */
  int m_FairyKissTimer;
  SHORTMATRIX m_HeadRotation;   /* Head matrix at the last emitting tick */
  SHORTMATRIX m_RenderRotation; /* Read by r_flame.s */
  Vector3D m_OffsetFromSpyro; /* m_Position - Spyro, once emission ends */
  Vector3D m_PuffLastPos[8];  /* Each puff's previous position (ray start) */
} SpyroFlameView;

enum {
  FLAME_PUFF_NEW = 0,    /* Not moved yet: its first ray starts at Spyro */
  FLAME_PUFF_FLYING = 1,
  FLAME_PUFF_HIT = 2,    /* Stopped at m_PuffEndAge, still ageing out */
};

#define g_Flame (*(SpyroFlameView *)&g_SpyroFlame)

/* One frame of g_Pad.m_BufferedInputs, which g_ActivePad points at. The
 * decomp types g_ActivePad as Gamepad *, whose first fields are in another
 * order (its "m_Released" at +4 is this m_Held). */
typedef struct {
  int m_Type;
  int m_Held;
  int m_Down;
  int m_Released;
  int m_LeftStickMoved;
  GamepadSticks m_Sticks;
} PadFrame;

#define g_PadFrame ((PadFrame *)g_ActivePad)

#define HAND_SAME_OFFSET(view, field, orig, origField)                         \
  static_assert(offsetof(view, field) == offsetof(orig, origField),            \
                #view "." #field " is not " #orig "." #origField)

static_assert(sizeof(SpyroFlameView) == sizeof(SpyroFlame), "SpyroFlameView size");
HAND_SAME_OFFSET(SpyroFlameView, m_PuffAge, SpyroFlame, unk_20);
HAND_SAME_OFFSET(SpyroFlameView, m_PuffEndAge, SpyroFlame, unk_28);
HAND_SAME_OFFSET(SpyroFlameView, m_PuffState, SpyroFlame, unk_30);
HAND_SAME_OFFSET(SpyroFlameView, m_IsFlameActive, SpyroFlame, m_IsFlameActive);
HAND_SAME_OFFSET(SpyroFlameView, m_PuffsEmitted, SpyroFlame, unk_99);
HAND_SAME_OFFSET(SpyroFlameView, m_IsSuperFlame, SpyroFlame, unk_9c);
HAND_SAME_OFFSET(SpyroFlameView, m_FairyKissTimer, SpyroFlame, m_FairyKissTimer);
HAND_SAME_OFFSET(SpyroFlameView, m_HeadRotation, SpyroFlame, unk_a4);
HAND_SAME_OFFSET(SpyroFlameView, m_RenderRotation, SpyroFlame, unk_b8);
HAND_SAME_OFFSET(SpyroFlameView, m_OffsetFromSpyro, SpyroFlame, unk_cc);
HAND_SAME_OFFSET(SpyroFlameView, m_PuffLastPos, SpyroFlame, unk_d8);

static_assert(sizeof(PadFrame) == sizeof(g_Pad.m_BufferedInputs[0]), "PadFrame size");
static_assert(offsetof(PadFrame, m_LeftStickMoved) == offsetof(Gamepad, m_LeftStickMoved), "PadFrame stick flag");
static_assert(offsetof(PadFrame, m_Sticks) == offsetof(Gamepad, m_Sticks), "PadFrame sticks");

#endif

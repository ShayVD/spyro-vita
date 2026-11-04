/* The environment animations' set-up and state (asm/renderers/r_environment.s:
 * func_8002B4AC, EnvAnimSetFlags, EnvAnimGetState, EnvAnimSetFrame,
 * hand-written asm in the original), written by hand from the asm so it
 * reads as C (docs/native-plan.md stage 3, decisions D96). The asm uses
 * neither the scratchpad nor the GTE.
 *
 * The environment's renderer and its animations' update are hand C on the
 * GTE register file (env_hand.c, env_update_hand.c), so the unit stays
 * linked for fast_render=0 (render_hand.h HAND_UNIT_FN): fast_render 1 and
 * 2 run this, --fast-render-check compares each call with the unit
 * (docs/translation-log.md). */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

/* the decomp's prototypes, out of the way of the entries' own */
#define func_8002B4AC func_8002B4AC_decl
#define func_8002A6FC func_8002A6FC_decl
#define EnvAnimSetFlags EnvAnimSetFlags_decl
#define EnvAnimGetState EnvAnimGetState_decl
#define EnvAnimSetFrame EnvAnimSetFrame_decl
#include "environment.h"
#undef func_8002B4AC
#undef func_8002A6FC
#undef EnvAnimSetFlags
#undef EnvAnimGetState
#undef EnvAnimSetFrame

#include "render_hand.h"

#include "env_anim_hand.h"

/* Every animation starts with the same four bytes: its index (what the
 * game's mobys name it by), its flags, its frame and the frame's delay
 * (EnvAnimGetState returns the last three). */
typedef struct {
  u8 m_Index, m_Flags, m_Frame, m_Delay;
} EnvAnim;

static const EnvAnimList *Lists(void) { return (const EnvAnimList *)&g_EnvironmentAnimations; }
static EnvAnim *Anim(const EnvAnimList *list, int i) { return HPTR(EnvAnim, HPTR(const u32, list->m_List)[i]); }

/* An animation's flags, in every animation of that index in every list:
 * kept where pMask has them, then pSet's added (EnvAnimSetFlags). */
static u32 SetFlags(u32 index, u32 mask, u32 set, u32 a3) {
  const EnvAnimList *list = Lists();
  int l, i;
  (void)a3;
  for (l = 0; l < LIST_COUNT; l++, list++)
    for (i = 0; i < list->m_Count; i++) {
      EnvAnim *anim = Anim(list, i);
      if (anim->m_Index == index)
        anim->m_Flags = (u8)((anim->m_Flags & mask) | set);
    }
  return 0;
}

/* The first animation of that index's flags, frame and delay, a byte each
 * (EnvAnimGetState). Only the index's low byte is compared. With none, the
 * asm returns what it last read (the last animation's state), or v0 as it
 * came in when there's no animation at all (0 in the translation). */
static u32 GetState(u32 index, u32 a1, u32 a2, u32 a3) {
  const EnvAnimList *list = Lists();
  u32 state = 0;
  int l, i;
  (void)a1, (void)a2, (void)a3;
  for (l = 0; l < LIST_COUNT; l++, list++)
    for (i = 0; i < list->m_Count; i++) {
      const u32 word = *(const u32 *)Anim(list, i);
      state = word >> 8;
      if ((u8)word == (u8)index)
        return state;
    }
  return state;
}

/* Every animation of that index goes to pFrame; with pDelay, the frame's
 * delay is 1 (EnvAnimSetFrame). */
static u32 SetFrame(u32 index, u32 frame, u32 delay, u32 a3) {
  const EnvAnimList *list = Lists();
  int l, i;
  (void)a3;
  for (l = 0; l < LIST_COUNT; l++, list++)
    for (i = 0; i < list->m_Count; i++) {
      EnvAnim *anim = Anim(list, i);
      if (anim->m_Index != index)
        continue;
      anim->m_Frame = (u8)frame;
      if (delay)
        anim->m_Delay = 1;
    }
  return 0;
}

/* The animations' state after a level loads (func_8002B4AC): each animated
 * texture shows its frame's texture, each scrolling texture its scroll;
 * each sector learns its place in the four geometry and colour lists
 * (sector bytes 0x18-0x1B); each collision animation's frame goes into the
 * level's collision triangles. */
static u32 SetUp(u32 a0, u32 a1, u32 a2, u32 a3) {
  const EnvAnimList *lists = Lists();
  int l, i;
  (void)a0, (void)a1, (void)a2, (void)a3;

  /* An animated texture: its frames at +8, 4 bytes each, the texture to
   * show in the last; the texture it replaces at +4. */
  for (i = 0; i < lists[LIST_TEXTURES].m_Count; i++) {
    const u8 *anim = (const u8 *)Anim(&lists[LIST_TEXTURES], i);
    CopyTexture(*(const u32 *)(anim + 4), anim[8 + anim[2] * 4 + 3]);
  }
  /* A scrolling texture: the texture (+4) and its scroll (+6), halves. */
  for (i = 0; i < lists[LIST_SCROLLING].m_Count; i++) {
    const u8 *anim = (const u8 *)Anim(&lists[LIST_SCROLLING], i);
    ScrollTexture(*(const u16 *)(anim + 4), *(const u16 *)(anim + 6));
  }
  /* Geometry and colour animations: their sector (+4, a half). */
  for (l = LIST_LOW_POLY; l <= LIST_HIGH_COLOR; l++)
    for (i = 0; i < lists[l].m_Count; i++) {
      const u8 *anim = (const u8 *)Anim(&lists[l], i);
      const u32 *sectors = HPTR(const u32, PSX_ADDR(g_Environment.m_SectorPointer));
      HPTR(u8, sectors[*(const u16 *)(anim + 4)])[0x18 + (l - LIST_LOW_POLY)] = (u8)i;
    }
  /* A collision animation: its first triangle (+6) and count (+4), halves;
   * its frames' data at the offset at +8; its keyframes at +0xC, 8 bytes
   * each, the data frame in the second word's second byte. */
  for (i = 0; i < lists[LIST_COLLISION].m_Count; i++) {
    const u8 *anim = (const u8 *)Anim(&lists[LIST_COLLISION], i);
    const u32 tris = *(const u32 *)(anim + 4), count = tris & 0xFFFF;
    const u32 frame = (*(const u32 *)(anim + 0xC + anim[2] * 8 + 4) >> 8) & 0xFF;
    const u32 data = PSX_ADDR(anim) + *(const u32 *)(anim + 8) + frame * count * 12;
    const TerrainCollision *coll = HPTR(const TerrainCollision, PSX_ADDR(g_Environment.m_TerrainCollision));
    CopyCollision((tris >> 16) * 12 + PSX_ADDR(coll->m_Triangles), data, data + count * 12);
  }
  return 0;
}

/* All four are void at every caller but EnvAnimGetState, whose v1 is dead
 * (all callers are C; tools/oracle/ignore_ranges.txt). */
HAND_UNIT_FN(EnvAnimSetFlags, SetFlags)
HAND_UNIT_FN_V0(EnvAnimGetState, GetState)
HAND_UNIT_FN(EnvAnimSetFrame, SetFrame)
HAND_UNIT_FN(func_8002B4AC, SetUp)

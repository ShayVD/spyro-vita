/* The environment animations' per-frame update, func_8002A6FC
 * (asm/renderers/r_environment.s, hand-written asm in the original), written
 * by hand from the asm so it reads as C (docs/native-plan.md stage 3,
 * decisions D96). The game calls it once a frame with the frame's time
 * (g_DeltaTime, src/gamestates/update.c).
 *
 * Each animation (the lists of g_EnvironmentAnimations, env_anim_hand.c)
 * steps through its frames: a paused one stays put; otherwise its frame's
 * delay counts down by the frame's time, and when it runs out the
 * animation moves on through as many frames as the time left over covers.
 * When the frame changed:
 *  - an animated texture copies its new frame's texture over its own;
 *  - a scrolling texture adds the frames' scroll steps to its scroll and
 *    rewrites its texture's V coordinates;
 *  - a geometry or colour animation tells its sector (sector bytes
 *    0x18-0x1B) where it is in its list, so the renderer applies it;
 *  - a collision animation writes its frame, or a blend of two frames (GPF
 *    and GPL on the GTE, through the scratchpad's first six words), into
 *    the level's collision triangles, each turned to start at its lowest
 *    vertex.
 *
 * The GTE moves and commands, the scratchpad's stores and every RAM store
 * are the asm's; registers the asm leaves aside. fast_render=3
 * (env_update_hand_rc.c, NGTE_RC, D124): the two commands as native maths,
 * no GTE data register or scratchpad. Checked per call by
 * --fast-render-check against the mips2c unit and by the oracle replay
 * (docs/translation-log.md). */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

/* the decomp's prototype, out of the way of the entry's own */
#define func_8002A6FC func_8002A6FC_decl
#include "environment.h"
#undef func_8002A6FC

#include "render_hand.h"

#include "env_anim_hand.h"

/* Every animation starts with its index, flags, frame and the frame's delay
 * left, a byte each. Flag 2: paused. Flag 1: take each frame's second
 * "next frame" instead of its first. */
enum { A_FLAGS = 1, A_FRAME = 2, A_DELAY = 3 };
#define FLAG_ALT 1u
#define FLAG_PAUSED 2u

/* A texture or scrolling texture's frames, 4 bytes each from +8: the
 * frame's delay << 2 | its flags, the next frame, the other next frame, and
 * the texture to show (a scrolling texture: a signed scroll step). The
 * flags go into the animation's flags byte as the frame starts. Returns the
 * new frame, or NULL when the frame didn't change. SUM, when given, adds up
 * the last byte of every frame passed through. */
static const u8 *StepSmall(u8 *anim, u32 dt, s32 *sum) {
  const u8 *const frames = anim + 8;
  const u32 flags = anim[A_FLAGS];
  const u8 *e;
  u32 frame, delay, over, f;
  if (flags & FLAG_PAUSED)
    return NULL;
  delay = anim[A_DELAY] - dt;
  if ((s32)delay > 0) {
    anim[A_DELAY] = (u8)delay;
    return NULL;
  }
  over = 0u - delay; /* the time past the frame's end */
  frame = frames[anim[A_FRAME] * 4 + (flags & FLAG_ALT) + 1];
  for (;;) {
    e = frames + frame * 4;
    if (sum)
      *sum += (s32)(s8)e[3];
    f = e[0] & 3;
    anim[A_FLAGS] = (u8)f;
    delay = (u32)e[0] >> 2;
    if (f & FLAG_PAUSED)
      break; /* stops here, its delay whole */
    delay -= over;
    if ((s32)delay > 0)
      break;
    over = 0u - delay;
    frame = e[(f & FLAG_ALT) + 1];
  }
  anim[A_DELAY] = (u8)delay;
  anim[A_FRAME] = (u8)frame;
  return e;
}

/* A geometry, colour or collision animation's frames, 8 bytes each from
 * +0xC: the frame's flags (all of the byte goes into the animation's), its
 * delay, the next frame, the other next frame, then a word of the frame's
 * own. Otherwise as StepSmall. */
static const u8 *StepBig(u8 *anim, u32 dt) {
  const u8 *const frames = anim + 0xC;
  const u32 flags = anim[A_FLAGS];
  const u8 *e;
  u32 frame, delay, over, f;
  if (flags & FLAG_PAUSED)
    return NULL;
  delay = anim[A_DELAY] - dt;
  if ((s32)delay > 0) {
    anim[A_DELAY] = (u8)delay;
    return NULL;
  }
  over = 0u - delay;
  frame = frames[anim[A_FRAME] * 8 + (flags & FLAG_ALT) + 2];
  for (;;) {
    e = frames + frame * 8;
    f = e[0];
    delay = e[1];
    anim[A_FLAGS] = (u8)f;
    if (f & FLAG_PAUSED)
      break;
    delay -= over;
    if ((s32)delay > 0)
      break;
    over = 0u - delay;
    frame = e[(f & FLAG_ALT) + 2];
  }
  anim[A_DELAY] = (u8)delay;
  anim[A_FRAME] = (u8)frame;
  return e;
}

/* One axis of a triangle blended between two frames' (A's weight WA, B's
 * WB, out of 0x1000): GPF puts A's three coordinates times WA into MAC1-3,
 * GPL adds B's times WB. */
#if NGTE_RC
/* fast_render=3 (D124): the same two commands as native maths, MAC1-3 as
 * values (an IR register holds 16 bits of the word moved there). */
static NgteCol BlendAxis(u32 a, u32 b, u32 wa, u32 wb) {
  const NgteCol f = ngte_gpf0((s16)(a & 0x3FFF), (s16)Vertex1(a), (s16)Vertex2(a), (s16)wa, 0);
  return ngte_gpl0(f.mac1, f.mac2, f.mac3, (s16)(b & 0x3FFF), (s16)Vertex1(b), (s16)Vertex2(b), (s16)wb,
                   0);
}
#else
static void BlendAxis(u32 a, u32 b, u32 wa, u32 wb) {
  MTC2(wa, 8); /* IR0 */
  MTC2(a & 0x3FFF, 9);
  MTC2(Vertex1(a), 10);
  MTC2(Vertex2(a), 11);
  m2c_gte_0x190003D(); /* GPF, sf 0 */
  MTC2(wb, 8);
  MTC2(b & 0x3FFF, 9);
  MTC2(Vertex1(b), 10);
  MTC2(Vertex2(b), 11);
  m2c_gte_0x1A0003E(); /* GPL, sf 0 */
}
#endif

/* Two frames' triangles blended into the level's, each turned to start at
 * its lowest vertex (its height offsets as 8 bits, unchecked). The x and y
 * coordinates go through the scratchpad's first six words, as in the asm
 * (not at fast_render=3: nothing reads them, D124). */
static void BlendCollision(u32 dst, u32 src_a, u32 src_b, u32 end, u32 wa, u32 wb) {
#if !NGTE_RC
  u32 *const scratch = (u32 *)SCRATCH_BASE;
#endif
  do {
    const u32 *a = HPTR(const u32, src_a), *b = HPTR(const u32, src_b);
    u32 *d = HPTR(u32, dst);
    u32 x0, x1, x2, y0, y1, y2, z0, z1, z2;
#if NGTE_RC
    NgteCol c = BlendAxis(a[0], b[0], wa, wb);
    x0 = (u32)(c.mac1 >> 12);
    x1 = (u32)(c.mac2 >> 12);
    x2 = (u32)(c.mac3 >> 12);
    c = BlendAxis(a[1], b[1], wa, wb);
    y0 = (u32)(c.mac1 >> 12);
    y1 = (u32)(c.mac2 >> 12);
    y2 = (u32)(c.mac3 >> 12);
    c = BlendAxis(a[2], b[2], wa, wb);
    z0 = (u32)c.mac1 >> 12; /* the heights shifted unsigned (srl) */
    z1 = (u32)c.mac2 >> 12;
    z2 = (u32)c.mac3 >> 12;
#else
    BlendAxis(a[0], b[0], wa, wb);
    scratch[0] = x0 = (u32)((s32)MFC2(25) >> 12);
    scratch[1] = x1 = (u32)((s32)MFC2(26) >> 12);
    scratch[2] = x2 = (u32)((s32)MFC2(27) >> 12);
    BlendAxis(a[1], b[1], wa, wb);
    scratch[3] = y0 = (u32)((s32)MFC2(25) >> 12);
    scratch[4] = y1 = (u32)((s32)MFC2(26) >> 12);
    scratch[5] = y2 = (u32)((s32)MFC2(27) >> 12);
    BlendAxis(a[2], b[2], wa, wb);
    z0 = MFC2(25) >> 12; /* the heights shifted unsigned (srl) */
    z1 = MFC2(26) >> 12;
    z2 = MFC2(27) >> 12;
#endif
    if ((s32)(z1 - z0) >= 0 && (s32)(z2 - z0) >= 0) {
      d[2] = Pack(z0, z1, z2, 0xFF, 16, 24);
      d[1] = PACK_XY(y0, y1, y2);
      d[0] = PACK_XY(x0, x1, x2);
    } else if ((s32)(z2 - z1) >= 0) {
      d[2] = Pack(z1, z2, z0, 0xFF, 16, 24);
      d[1] = PACK_XY(y1, y2, y0);
      d[0] = PACK_XY(x1, x2, x0);
    } else {
      d[2] = Pack(z2, z0, z1, 0xFF, 16, 24);
      d[1] = PACK_XY(y2, y0, y1);
      d[0] = PACK_XY(x2, x0, x1);
    }
    src_a += 12;
    src_b += 12;
    dst += 12;
  } while (src_a != end);
}

/* A collision animation: its first triangle (+6) and count (+4), halves;
 * its frames' data at the offset at +8, `count` triangles of 12 bytes a
 * frame. Its keyframe's word: the blend weight towards the second frame (in
 * 16ths of 0x1000, 0 for none) and the two data frames. */
static void UpdateCollision(u32 anim_addr, const u8 *anim, const u8 *e) {
  const u32 key = *(const u32 *)(e + 4);
  const u32 weight = key & 0xFF, frame_a = (key >> 8) & 0xFF, frame_b = (key >> 16) & 0xFF;
  const u32 tris = *(const u32 *)(anim + 4);
  const u32 size = (tris & 0xFFFF) * 12;
  const u32 data = *(const u32 *)(anim + 8) + anim_addr;
  const TerrainCollision *coll = HPTR(const TerrainCollision, PSX_ADDR(g_Environment.m_TerrainCollision));
  const u32 dst = (tris >> 16) * 12 + PSX_ADDR(coll->m_Triangles);
  const u32 src_a = data + size * frame_a;
  if (weight > 0)
    BlendCollision(dst, src_a, data + size * frame_b, src_a + size, 0x1000 - (weight << 4), weight << 4);
  else
    CopyCollision(dst, src_a, src_a + size);
}

/* Animation I of a list: its address as the list holds it, and its bytes. */
static u8 *Anim(const EnvAnimList *list, u32 i, u32 *addr) {
  *addr = HPTR(const u32, list->m_List)[i];
  return HPTR(u8, *addr);
}

static u32 UpdateAnimations(u32 a0, u32 a1, u32 a2, u32 a3) {
  const EnvAnimList *const lists = (const EnvAnimList *)&g_EnvironmentAnimations;
  const u32 dt = a0; /* the frame's time */
  const EnvAnimList *list;
  u32 addr, i;
  (void)a1, (void)a2, (void)a3;

  /* Animated textures: the new frame's texture over the animation's (the
   * word at +4). */
  list = &lists[LIST_TEXTURES];
  for (i = 0; i != (u32)list->m_Count; i++) {
    u8 *anim = Anim(list, i, &addr);
    const u8 *e = StepSmall(anim, dt, NULL);
    if (e)
      CopyTexture(*(const u32 *)(anim + 4), e[3]);
  }

  /* Scrolling textures: the texture (+4) and its scroll (+6), halves; the
   * scroll moves by the steps of the frames passed through, mod 0x80. */
  list = &lists[LIST_SCROLLING];
  for (i = 0; i != (u32)list->m_Count; i++) {
    u8 *anim = Anim(list, i, &addr);
    s32 step = 0;
    const u8 *e = StepSmall(anim, dt, &step);
    if (e) {
      const u32 tex = *(const u16 *)(anim + 4);
      const u32 scroll = (*(const u16 *)(anim + 6) + (u32)step) & 0x7F;
      *(u16 *)(anim + 6) = (u16)scroll;
      ScrollTexture(tex, scroll);
    }
  }

  /* Geometry and colour animations: on a new frame, the sector (+4, a
   * half) is told the animation's place in its list again (the renderer,
   * env_hand.c, applies the frame). */
  for (list = &lists[LIST_LOW_POLY]; list <= &lists[LIST_HIGH_COLOR]; list++)
    for (i = 0; i != (u32)list->m_Count; i++) {
      u8 *anim = Anim(list, i, &addr);
      if (StepBig(anim, dt)) {
        const u32 *sectors = HPTR(const u32, PSX_ADDR(g_Environment.m_SectorPointer));
        HPTR(u8, sectors[*(const u16 *)(anim + 4)])[0x18 + (list - &lists[LIST_LOW_POLY])] = (u8)i;
      }
    }

  /* Collision animations: the new frame into the level's triangles. */
  list = &lists[LIST_COLLISION];
  for (i = 0; i != (u32)list->m_Count; i++) {
    u8 *anim = Anim(list, i, &addr);
    const u8 *e = StepBig(anim, dt);
    if (e)
      UpdateCollision(addr, anim, e);
  }
  /* The asm's v0 is whatever it last computed; the caller is C and ignores
   * it. */
  return 0;
}

HAND_UNIT_FN_RC(func_8002A6FC, UpdateAnimations)

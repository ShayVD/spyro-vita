/* What the environment animations' hand C shares (decisions D96): the
 * lists' layout, the textures' two copies and the collision triangles'
 * packing, as func_8002B4AC (env_anim_hand.c, the set-up after a level
 * loads) and func_8002A6FC (env_update_hand.c, the update each frame) both
 * write them. Include after environment.h and render_hand.h. */
#ifndef ENV_ANIM_HAND_H
#define ENV_ANIM_HAND_H

#include <stddef.h>

/* g_EnvironmentAnimations is nine lists, each a count and an array of
 * pointers to animations: the textures', the scrolling textures', the
 * low- and high-detail geometry's and colours', two unused, the
 * collision's. */
typedef struct {
  int m_Count;
  u32 m_List;
} EnvAnimList;
enum {
  LIST_TEXTURES,
  LIST_SCROLLING,
  LIST_LOW_POLY, /* LIST_LOW_POLY to LIST_HIGH_COLOR: on sectors */
  LIST_LOW_COLOR,
  LIST_HIGH_POLY,
  LIST_HIGH_COLOR,
  LIST_COLLISION = 8,
  LIST_COUNT
};
static_assert(sizeof(EnvironmentAnimations) == LIST_COUNT * sizeof(EnvAnimList), "EnvironmentAnimations");
static_assert(offsetof(EnvironmentAnimations, m_CollisionAnimations) == 0x44, "EnvironmentAnimations");
static_assert(offsetof(Environment, m_SectorPointer) == 0 && offsetof(Environment, m_LQTexturePointer) == 0x18 &&
                  offsetof(Environment, m_HQTexturePointer) == 0x1C &&
                  offsetof(Environment, m_TerrainCollision) == 0x2C,
              "Environment");
static_assert(offsetof(TerrainCollision, m_Triangles) == 0x10, "TerrainCollision");

/* A level's textures, in two copies: the low-detail one 16 bytes each
 * (four corners' UVs and the page and CLUT), the high-detail one 0xA8.
 * Addresses as the asm computes them, from the pointers in g_Environment. */
#define LQ_TEXTURE_SIZE 0x10
#define HQ_TEXTURE_SIZE 0xA8
static inline u8 *LQTexture(u32 i) {
  return HPTR(u8, PSX_ADDR(g_Environment.m_LQTexturePointer) + i * LQ_TEXTURE_SIZE);
}
static inline u8 *HQTexture(u32 i) {
  return HPTR(u8, PSX_ADDR(g_Environment.m_HQTexturePointer) + i * HQ_TEXTURE_SIZE);
}

/* An animated texture's frame: texture FROM's two copies over TO's, the
 * low-detail one four words at once, the high-detail one two at a time, as
 * the asm does. */
static inline void CopyTexture(u32 to, u32 from) {
  const u32 *src = (const u32 *)LQTexture(from);
  u32 *dst = (u32 *)LQTexture(to);
  u32 w0 = src[0], w1 = src[1], w2 = src[2], w3 = src[3];
  int k;
  dst[0] = w0, dst[1] = w1, dst[2] = w2, dst[3] = w3;
  src = (const u32 *)HQTexture(from);
  dst = (u32 *)HQTexture(to);
  for (k = 0; k < HQ_TEXTURE_SIZE / 4; k += 2) {
    w0 = src[k], w1 = src[k + 1];
    dst[k] = w0, dst[k + 1] = w1;
  }
}

/* A scrolling texture's V coordinates for a scroll of `scroll`: the
 * low-detail corners and the high-detail copy's first two at scroll / 4;
 * the high-detail copy's next four at scroll / 2, the four after them 0x20
 * further on (mod 0x40), the eight after those at scroll / 2 again, and its
 * last 24 in three rows of eight, each 0x10 further on (mod 0x40) than the
 * one before. A byte each (the V of a 4-byte UV word). */
static inline void ScrollTexture(u32 tex, u32 scroll) {
  u8 *lq = LQTexture(tex), *hq = HQTexture(tex);
  u32 v = scroll >> 2;
  int k;
  for (k = 0x1; k <= 0xD; k += 4)
    lq[k] = (u8)v;
  hq[0x1] = hq[0x5] = (u8)v;
  v = scroll >> 1;
  for (k = 0x09; k <= 0x15; k += 4)
    hq[k] = (u8)v;
  v = (v + 0x20) & 0x3F;
  for (k = 0x19; k <= 0x25; k += 4)
    hq[k] = (u8)v;
  v = scroll >> 1;
  for (k = 0x29; k <= 0x45; k += 4)
    hq[k] = (u8)v;
  for (; k <= 0xA5; k += 0x20) {
    int j;
    v = (v + 0x10) & 0x3F;
    for (j = 0; j < 0x20; j += 4)
      hq[k + j] = (u8)v;
  }
}

/* A collision triangle is three words, one per axis: the first vertex's
 * coordinate in the low 14 bits and the other two's as 9-bit signed
 * offsets from it (bits 14-22 and 23-31). The level's own triangles keep
 * the height word's offsets at bits 16 and 24, both non-negative: the
 * lowest vertex first. */
static inline u32 Vertex1(u32 w) { return (w & 0x3FFF) + (u32)((s32)(w << 9) >> 23); }
static inline u32 Vertex2(u32 w) { return (w & 0x3FFF) + (u32)((s32)w >> 23); }
/* A word from three coordinates: FIRST's, then NEXT's offset (masked) at
 * AT_NEXT and LAST's at AT_LAST. */
static inline u32 Pack(u32 first, u32 next, u32 last, u32 mask, int at_next, int at_last) {
  return (first & 0x3FFF) | ((next - first) & mask) << at_next | (last - first) << at_last;
}
#define PACK_XY(a, b, c) Pack(a, b, c, 0x1FF, 14, 23)

/* One frame's triangles (addresses from SRC to END) into the level's at
 * DST, each turned (keeping its winding) to start at its lowest vertex. The
 * asm runs the loop at least once (a count of 0 never ends). */
static inline void CopyCollision(u32 dst, u32 src, u32 end) {
  do {
    const u32 *s = HPTR(const u32, src);
    u32 *d = HPTR(u32, dst);
    const u32 x = s[0], y = s[1], z = s[2];
    const s32 dz1 = (s32)(z << 9) >> 23, dz2 = (s32)z >> 23;
    if (dz1 >= 0 && dz2 >= 0) {
      d[0] = x;
      d[1] = y;
      d[2] = (z & 0x3FFF) | (u32)dz1 << 16 | (u32)dz2 << 24;
    } else if (dz2 - dz1 >= 0) {
      /* the second vertex is the lowest: (1, 2, 0) */
      d[0] = PACK_XY(Vertex1(x), Vertex2(x), x & 0x3FFF);
      d[1] = PACK_XY(Vertex1(y), Vertex2(y), y & 0x3FFF);
      d[2] = Pack(Vertex1(z), Vertex2(z), z & 0x3FFF, 0x1FF, 16, 24);
    } else {
      /* the third: (2, 0, 1) */
      d[0] = PACK_XY(Vertex2(x), x & 0x3FFF, Vertex1(x));
      d[1] = PACK_XY(Vertex2(y), y & 0x3FFF, Vertex1(y));
      d[2] = Pack(Vertex2(z), z & 0x3FFF, Vertex1(z), 0x1FF, 16, 24);
    }
    src += 12;
    dst += 12;
  } while (src != end);
}

#endif

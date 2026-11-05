/* The GPU renderer's lean vertex, made where the triangle is made (D99).
 *
 * gpu_gl.c's lean vertex (gpu_lean, D70; gpu_fused's one pass, D92) is
 * PsyCross's GrVertex with what the shaders read after it: the triangle's
 * UV box (the page texels the filter may read), its atlas offset, and two
 * flags (filtered, wraps within its tile). Of those only the box is a
 * function of the triangle alone; the offset and the flags depend on the
 * atlas and the options, which the render thread owns.
 *
 * With nd_direct=1 the OT walk (native_draw.c's em_prim, which makes every
 * triangle of the records and of the polygon packets it parses) writes the
 * box into the GrVertex's last 4 bytes, where the vertex cache's reference
 * goes (psx_vcache.h; the lean renderer runs only without it, D70). Those
 * vertices are then the renderer's vertex buffer as they are: it adds the
 * offset and the flags per triangle in a side stream, and copies nothing.
 * The software rasterizer doesn't read those bytes, so the target logs it
 * replays (gpu_gl.c) take either form.
 *
 * Both sides use these functions, so the box is computed by one rule. Words
 * of a GrVertex (5 a vertex, little-endian): x | y << 16, page | clut << 16,
 * u | v << 8 | bright << 16 | dither << 24, r | g << 8 | b << 16 | a << 24,
 * then the 4 spare bytes. A box word: u0 | v0 << 8 | u1 << 16 | v1 << 24. */
#ifndef PSX_LEAN_H
#define PSX_LEAN_H

#include <stdint.h>

#define LEAN_VW 5u /* words a vertex */

/* 1 if the PS1 GPU skips the triangle t (three vertices): an edge spanning
 * >= 1024 x or >= 512 y (gpu_soft.c's rule, gl_vertices' collapse) */
static inline int lean_skipped(const uint32_t *t) {
  int k;
  for (k = 0; k < 3; k++) {
    const uint32_t a = t[k * LEAN_VW], b = t[(k + 1) % 3 * LEAN_VW];
    const int dx = (int16_t)a - (int16_t)b, dy = (int16_t)(a >> 16) - (int16_t)(b >> 16);
    if (dx >= 1024 || dx <= -1024 || dy >= 512 || dy <= -512)
      return 1;
  }
  return 0;
}

/* The UV box of the first n (1..3) vertices at t: lowest to highest u and
 * v; snapped to the whole tile when they lie in one aligned 32x32 tile and
 * span at least half of it each way (D39: such a triangle's filter wraps
 * within the tile) */
static inline uint32_t lean_box(const uint32_t *t, int n) {
  uint32_t u0 = 255, v0 = 255, u1 = 0, v1 = 0;
  int k;
  for (k = 0; k < n; k++) {
    const uint32_t w = t[k * LEAN_VW + 2], u = w & 0xFF, v = w >> 8 & 0xFF;
    u0 = u < u0 ? u : u0;
    v0 = v < v0 ? v : v0;
    u1 = u > u1 ? u : u1;
    v1 = v > v1 ? v : v1;
  }
  if (u0 >> 5 == u1 >> 5 && v0 >> 5 == v1 >> 5 && u1 - u0 >= 15 && v1 - v0 >= 15) {
    u0 &= ~31u;
    v0 &= ~31u;
    u1 = u0 + 31;
    v1 = v0 + 31;
  }
  return u0 | v0 << 8 | u1 << 16 | v1 << 24;
}

/* 1 if lean_box snapped to a tile: a box that is one whole aligned tile is
 * one it snapped, or one it would have (span 31 in one tile) */
static inline int lean_box_tile(uint32_t b) {
  const uint32_t u0 = b & 0xFF, v0 = b >> 8 & 0xFF, u1 = b >> 16 & 0xFF, v1 = b >> 24;
  return !(u0 & 31) && u1 == u0 + 31 && !(v0 & 31) && v1 == v0 + 31;
}

/* D105: the two rules above on whole words, for the walk. On the Vita
 * ARMv6's SIMD instructions; elsewhere the same lane operations in C, so
 * the host's checks run this form against the rules above (native_draw.c's
 * --nd-direct-check, tests/lean). */
#if defined(__ARM_FEATURE_SIMD32) && !defined(LEAN_SIMD_C)
static inline uint32_t lean_qsub16(uint32_t a, uint32_t b) {
  uint32_t r;
  __asm__("qsub16 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
  return r;
}
static inline uint32_t lean_uadd16(uint32_t a, uint32_t b) {
  uint32_t r;
  __asm__("uadd16 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b) : "cc");
  return r;
}
static inline uint32_t lean_uqsub16(uint32_t a, uint32_t b) {
  uint32_t r;
  __asm__("uqsub16 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
  return r;
}
/* each byte the lower of a's and b's: usub8 sets GE where a >= b */
static inline uint32_t lean_min8(uint32_t a, uint32_t b) {
  uint32_t r;
  __asm__("usub8 %0, %1, %2\n\tsel %0, %2, %1" : "=&r"(r) : "r"(a), "r"(b) : "cc");
  return r;
}
#else
static inline uint32_t lean_qsub16(uint32_t a, uint32_t b) {
  uint32_t r = 0;
  int k;
  for (k = 0; k < 32; k += 16) {
    int d = (int16_t)(a >> k) - (int16_t)(b >> k);
#ifndef LEAN_MUTANT /* tests/lean's mutant: no saturation */
    d = d > 32767 ? 32767 : d < -32768 ? -32768 : d;
#endif
    r |= ((uint32_t)d & 0xFFFF) << k;
  }
  return r;
}
static inline uint32_t lean_uadd16(uint32_t a, uint32_t b) {
  return (((a & 0xFFFF) + (b & 0xFFFF)) & 0xFFFF) | ((a >> 16) + (b >> 16)) << 16;
}
static inline uint32_t lean_uqsub16(uint32_t a, uint32_t b) {
  uint32_t r = 0;
  int k;
  for (k = 0; k < 32; k += 16) {
    const uint32_t x = a >> k & 0xFFFF, y = b >> k & 0xFFFF;
    r |= (x > y ? x - y : 0) << k;
  }
  return r;
}
static inline uint32_t lean_min8(uint32_t a, uint32_t b) {
  uint32_t r = 0;
  int k;
  for (k = 0; k < 32; k += 8) {
    const uint32_t x = a >> k & 0xFF, y = b >> k & 0xFF;
    r |= (x < y ? x : y) << k;
  }
  return r;
}
#endif

/* Nonzero if the edge between the x/y words a and b spans >= 1024 x or
 * >= 512 y (lean_skipped's rule). qsub16 saturates, so a difference past
 * 16 bits stays past the limit; +1023 and +511 put the allowed differences
 * at 0..2046 and 0..1022, and uqsub16 leaves only what is above them. */
static inline uint32_t lean_edge_far(uint32_t a, uint32_t b) {
  return lean_uqsub16(lean_uadd16(lean_qsub16(a, b), 0x01FF03FFu), 0x03FE07FEu);
}

/* A vertex's u/v word (word 2) as the box's lanes: u, v, 255 - u, 255 - v.
 * The lowest of three such words, byte by byte, is the box with its high
 * half inverted (lean_box_of). */
static inline uint32_t lean_uvw(uint32_t w) { return (w & 0xFFFF) | ~w << 16; }

/* The box of those lowest bytes m, snapped as lean_box snaps it. The spans
 * u1 - u0 and v1 - v0 have no borrow between them (u1 >= u0). */
static inline uint32_t lean_box_of(uint32_t m) {
  uint32_t b = (m & 0xFFFF) | (~m & 0xFFFF0000u);
  const uint32_t d = (b >> 16) - (b & 0xFFFF);
  if (!((b ^ b >> 16) & 0xE0E0) && (d & 0xFF) >= 15 && d >> 8 >= 15)
    b = (b & 0xE0E0u) * 0x10001u + 0x1F1F0000u;
  return b;
}

/* lean_box(t, 3) in the lane form (the walk's em_direct shares a quad's
 * two vertices between its triangles; nd_direct=2's check runs this) */
static inline uint32_t lean_box_lanes(const uint32_t *t) {
  return lean_box_of(lean_min8(lean_min8(lean_uvw(t[2]), lean_uvw(t[LEAN_VW + 2])),
                               lean_uvw(t[2 * LEAN_VW + 2])));
}

/* The walk's side: a triangle that isn't skipped, its box into each
 * vertex's spare bytes (the rule; the walk itself runs D105's form) */
static inline void lean_direct_tri(uint32_t *t) {
  const uint32_t b = lean_box(t, 3);
  t[4] = t[LEAN_VW + 4] = t[2 * LEAN_VW + 4] = b;
}

/* The renderer's side, for a triangle the walk didn't make direct
 * (PsyCross's own primitives, a skipped one, a list without nd_direct): the
 * first n (1..3) vertices at t in place as gpu_fused's pass makes its lean
 * vertex. A skipped triangle becomes three copies of its first vertex, its
 * box that vertex's texel. Returns the reference the pass reads from the
 * spare bytes (0 for a skipped one), before the box goes there. */
static inline uint32_t lean_convert(uint32_t *t, int n) {
  uint32_t ref, b;
  int k;
  if (n == 3 && lean_skipped(t)) {
    for (k = 0; k < 4; k++)
      t[LEAN_VW + k] = t[2 * LEAN_VW + k] = t[k];
    b = lean_box(t, 1);
    t[4] = t[LEAN_VW + 4] = t[2 * LEAN_VW + 4] = b;
    return 0;
  }
  ref = t[4];
  b = lean_box(t, n);
  for (k = 0; k < n; k++)
    t[k * LEAN_VW + 4] = b;
  return ref;
}

#endif

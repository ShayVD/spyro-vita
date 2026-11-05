/* The walk's lane form of the lean vertex's rules (src/platform/common/
 * psx_lean.h, D105) against the rules themselves: lean_edge_far against
 * lean_skipped, lean_box_of/lean_min8/lean_uvw (and lean_box_lanes, the
 * Vita's nd_direct=2 check) against lean_box, for a triangle and for a
 * quad's second triangle (which shares two vertices' lowest bytes with the
 * first, as native_draw.c's em_direct does).
 *
 * Random triangles (x/y over the whole 16 bits, near the 1024/512 limits,
 * small; u/v anywhere, inside one tile, on the tile's spans 14-16 and 31,
 * with random brightness and dither bits above them), then edge cases:
 * degenerate triangles, edges of exactly 1023/1024 and 511/512 each way,
 * differences that wrap 16 bits (32767 against -32768), u/v 0 and 255.
 *
 * Built three ways: on the host with the lane operations in C (ctest's
 * lean), the mutant (LEAN_MUTANT: qsub16 without its saturation, which the
 * wrapping cases must catch), and with the Vita's compiler for ARM, run
 * under qemu-arm (tests/lean/arm_check.sh: the real usub8/sel/qsub16/
 * uqsub16/uadd16). LEAN_FREESTANDING: no C library (the ARM build). */
#include "psx_lean.h"

#ifdef LEAN_FREESTANDING
static void out(const char *s) {
  register long r0 __asm__("r0") = 1;
  register const char *r1 __asm__("r1") = s;
  register long r2 __asm__("r2") = 0;
  register long r7 __asm__("r7") = 4; /* write */
  while (s[r2])
    r2++;
  __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
}
#else
#include <stdio.h>
#include <stdlib.h>
static void out(const char *s) { fputs(s, stdout); }
#endif

static void out_u(unsigned long long v) {
  char b[24];
  int i = 23;
  b[i] = 0;
  do
    b[--i] = (char)('0' + v % 10);
  while (v /= 10);
  out(b + i);
}

static void out_x(uint32_t v) {
  char b[9];
  int i;
  for (i = 7; i >= 0; i--, v >>= 4)
    b[i] = "0123456789abcdef"[v & 15];
  b[8] = 0;
  out(b);
}

static uint32_t s_rng = 0x2545F491u;
static uint32_t rnd(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

static unsigned long long s_tris, s_bad;

/* t: four vertices (a quad's prim order 0 1 2 3); the first triangle is
 * 0 1 2, the second 1 2 3 */
static void check(const uint32_t *t) {
  uint32_t tri[2][3 * LEAN_VW];
  const uint32_t w0 = lean_uvw(t[2]), w1 = lean_uvw(t[LEAN_VW + 2]);
  const uint32_t w2 = lean_uvw(t[2 * LEAN_VW + 2]), w3 = lean_uvw(t[3 * LEAN_VW + 2]);
  const uint32_t m12 = lean_min8(w1, w2);
  const uint32_t e12 = lean_edge_far(t[LEAN_VW], t[2 * LEAN_VW]);
  uint32_t far[2], box[2];
  int q, k, j;
  far[0] = e12 | lean_edge_far(t[0], t[LEAN_VW]) | lean_edge_far(t[2 * LEAN_VW], t[0]);
  far[1] = e12 | lean_edge_far(t[2 * LEAN_VW], t[3 * LEAN_VW]) |
           lean_edge_far(t[3 * LEAN_VW], t[LEAN_VW]);
  box[0] = lean_box_of(lean_min8(m12, w0));
  box[1] = lean_box_of(lean_min8(m12, w3));
  for (q = 0; q < 2; q++) {
    for (k = 0; k < 3; k++)
      for (j = 0; j < (int)LEAN_VW; j++)
        tri[q][k * LEAN_VW + j] = t[(q + k) * LEAN_VW + j];
    s_tris++;
    if (!far[q] != !lean_skipped(tri[q]) || box[q] != lean_box(tri[q], 3) ||
        lean_box_lanes(tri[q]) != box[q]) {
      if (s_bad++ < 10) {
        out("lean: differs, triangle");
        for (k = 0; k < 3; k++) {
          out(" ");
          out_x(tri[q][k * LEAN_VW]);
          out("/");
          out_x(tri[q][k * LEAN_VW + 2]);
        }
        out(": far ");
        out_x(far[q]);
        out(" box ");
        out_x(box[q]);
        out(", the rule: skipped ");
        out_u((unsigned long long)lean_skipped(tri[q]));
        out(" box ");
        out_x(lean_box(tri[q], 3));
        out("\n");
      }
    }
  }
}

/* x/y of a kind: 0 the whole 16 bits, 1 near the limits, 2 small */
static uint32_t rnd_xy(int kind, uint32_t base) {
  uint32_t r = rnd();
  int x, y;
  if (kind == 0)
    return r;
  if (kind == 1) {
    static const int dx[] = {1022, 1023, 1024, 1025, -1022, -1023, -1024, -1025, 0};
    static const int dy[] = {510, 511, 512, 513, -510, -511, -512, -513, 0};
    x = (int16_t)base + dx[r % 9] + (int)(r >> 8 & 1);
    y = (int16_t)(base >> 16) + dy[(r >> 4) % 9] - (int)(r >> 9 & 1);
    return ((uint32_t)x & 0xFFFF) | (uint32_t)y << 16;
  }
  x = (int)(r & 0x7FF) - 1024;
  y = (int)(r >> 11 & 0x3FF) - 512;
  return ((uint32_t)x & 0xFFFF) | (uint32_t)y << 16;
}

/* u/v of a kind: 0 anywhere, 1 inside one 32x32 tile, 2 a tile's corner
 * plus spans 13..17 or 30..31, 3 0 and 255 only */
static uint32_t rnd_uv(int kind, uint32_t tile) {
  const uint32_t r = rnd(), hi = r & 0xFFFF0000u;
  uint32_t u, v;
  switch (kind) {
  case 0:
    return r;
  case 1:
    u = (tile & 0xE0) | (r & 31);
    v = (tile >> 8 & 0xE0) | (r >> 5 & 31);
    break;
  case 2: {
    static const uint32_t s[] = {0, 13, 14, 15, 16, 17, 30, 31, 32};
    u = (tile & 0xE0) + s[r % 9];
    v = (tile >> 8 & 0xE0) + s[(r >> 4) % 9];
    break;
  }
  default:
    u = r & 1 ? 255 : 0;
    v = r & 2 ? 255 : 0;
    break;
  }
  return hi | (u & 0xFF) | (v & 0xFF) << 8;
}

static void random_quads(unsigned long long n) {
  uint32_t t[4 * LEAN_VW];
  unsigned long long i;
  int k, j;
  for (i = 0; i < n; i++) {
    const int xk = (int)(i % 3), uk = (int)(i / 3 % 4);
    const uint32_t base = rnd(), tile = rnd();
    for (k = 0; k < 4; k++) {
      for (j = 0; j < (int)LEAN_VW; j++)
        t[k * LEAN_VW + j] = rnd();
      t[k * LEAN_VW] = rnd_xy(xk, base);
      t[k * LEAN_VW + 2] = rnd_uv(uk, tile);
    }
    if (xk == 1)
      t[0] = base; /* the others near this one's limits */
    if (i % 7 == 0) /* degenerate: two vertices the same */
      for (j = 0; j < (int)LEAN_VW; j++)
        t[(int)(i % 3) * LEAN_VW + j] = t[3 * LEAN_VW + j];
    check(t);
  }
}

static uint32_t xy(int x, int y) { return ((uint32_t)x & 0xFFFF) | (uint32_t)y << 16; }

static void edge_cases(void) {
  static const int xs[] = {0, 1, -1, 511, 512, 1023, 1024, -1023, -1024, 32767, -32768,
                           32766, -32767, 2047, -2048};
  static const uint32_t uvs[] = {0x0000, 0x00FF, 0xFF00, 0xFFFF, 0x1F1F, 0x2020, 0x0F0F,
                                 0x1010, 0xE0E0, 0xFFE0, 0x0E0E, 0x3F20};
  const int nx = (int)(sizeof xs / sizeof *xs), nu = (int)(sizeof uvs / sizeof *uvs);
  uint32_t t[4 * LEAN_VW];
  int a, b, c, d, k;
  for (k = 0; k < 4 * (int)LEAN_VW; k++)
    t[k] = 0;
  /* every pair of coordinates on one edge, each axis, the others at 0 */
  for (a = 0; a < nx; a++)
    for (b = 0; b < nx; b++)
      for (c = 0; c < 4; c++) {
        t[0] = c & 1 ? xy(xs[a], 0) : xy(0, xs[a]);
        t[LEAN_VW] = c & 1 ? xy(xs[b], 0) : xy(0, xs[b]);
        t[2 * LEAN_VW] = c & 2 ? t[0] : t[LEAN_VW]; /* degenerate */
        t[3 * LEAN_VW] = c & 2 ? xy(xs[b], xs[a]) : xy(xs[a], xs[b]);
        check(t);
      }
  /* every three of the u/v words, the fourth each of them in turn */
  for (k = 0; k < 4 * (int)LEAN_VW; k++)
    t[k] = 0;
  for (a = 0; a < nu; a++)
    for (b = 0; b < nu; b++)
      for (c = 0; c < nu; c++)
        for (d = 0; d < nu; d++) {
          t[2] = uvs[a] | 0x01020000u;
          t[LEAN_VW + 2] = uvs[b] | 0xFF000000u;
          t[2 * LEAN_VW + 2] = uvs[c];
          t[3 * LEAN_VW + 2] = uvs[d] | 0x00FF0000u;
          check(t);
        }
  /* all four vertices the same: one point, its box that texel */
  for (a = 0; a < 256; a++) {
    for (k = 0; k < 4; k++) {
      t[k * LEAN_VW] = xy(a * 128 - 16384, a * 64 - 8192);
      t[k * LEAN_VW + 2] = (uint32_t)a | (uint32_t)(255 - a) << 8 | 0x20000u;
    }
    check(t);
  }
}

int lean_test(unsigned long long n) {
  edge_cases();
  random_quads(n);
  out("lean: ");
  out_u(s_tris);
  out(" triangles, ");
  out_u(s_bad);
  out(" differ (D105)\n");
  return s_bad != 0;
}

#ifndef LEAN_FREESTANDING
int main(int argc, char **argv) {
  return lean_test(argc > 1 ? strtoull(argv[1], NULL, 0) : 1u << 22);
}
#endif

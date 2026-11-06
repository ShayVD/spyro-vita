/* psx_fill.h's psx_fill_rect (D111) against ClearImage's plain loop
 * (gpu_soft.c, gpu_gl.c before D111): random rects at every alignment, the
 * ones that wrap right and down, wider than 1024 and higher than 512, empty
 * and negative ones, each with its own colour; after each, every row it
 * reached compared whole. Built for the host (ctest fill; the C path), the
 * mutant (PSX_FILL_MUTANT: the wrapped piece one pixel short, which must
 * fail), and with the Vita's compiler for ARM, run under qemu-arm
 * (tests/fill/arm_check.sh: the NEON path). FILL_FREESTANDING: no C library. */
#include <stdint.h>

#ifdef PSX_FILL_MUTANT
#include "psx_fill.h"
static inline void fill_rect(uint16_t *v, int x, int y, int w, int h, uint16_t c) {
  psx_fill_rect(v, x, y, w > 1 && (x & 1023) + w > 1024 && w < 1024 ? w - 1 : w, h, c);
}
#else
#include "psx_fill.h"
#define fill_rect psx_fill_rect
#endif

#ifdef FILL_FREESTANDING
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

#define VW 1024
#define VH 512
static uint16_t s_ref[VW * VH], s_got[VW * VH];
static uint32_t s_rng = 0x2545F491u;

static uint32_t rnd(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

static void plain(uint16_t *v, int x, int y, int w, int h, uint16_t c) {
  int i, j;
  for (i = 0; i < h; i++) {
    uint16_t *row = v + ((y + i) & (VH - 1)) * VW;
    for (j = 0; j < w; j++)
      row[(x + j) & (VW - 1)] = c;
  }
}

static unsigned long long s_rects, s_px, s_bad;

static void one(int x, int y, int w, int h) {
  const uint16_t c = (uint16_t)rnd();
  int i, j, rows = h < VH ? h : VH;
  plain(s_ref, x, y, w, h, c);
  fill_rect(s_got, x, y, w, h, c);
  s_rects++;
  if (w > 0 && h > 0)
    s_px += (unsigned long long)w * h;
  for (i = 0; i < (rows > 0 ? rows : 0); i++) {
    const int r = ((y + i) & (VH - 1)) * VW;
    for (j = 0; j < VW; j++)
      if (s_ref[r + j] != s_got[r + j]) {
        if (!s_bad++)
          out("first difference\n");
        s_got[r + j] = s_ref[r + j];
      }
  }
}

int fill_test(unsigned long long n) {
  unsigned long long k;
  int i;
  for (i = 0; i < VW * VH; i++)
    s_ref[i] = s_got[i] = (uint16_t)rnd();
  /* the edges: Spyro's clears (logs/clr-count), whole rows and VRAM, the
   * wraps by one, empty and negative */
  one(0, 8, 512, 224), one(0, 248, 512, 224), one(0, 0, 512, 480), one(0, 0, 512, 512);
  one(0, 0, 1024, 512), one(0, 0, 0, 0), one(1023, 511, 2, 2), one(1, 0, 1024, 1);
  one(5, 3, 1030, 1), one(-3, -2, 7, 5), one(100, 100, -5, 3), one(100, 100, 3, -5);
  one(0, 0, 3000, 1100), one(1020, 0, 24, 1), one(1000, 0, 23, 1), one(1000, 0, 24, 1);
  for (i = 0; i < 64; i++)
    one(i, i, 1, 1), one(i, i, 15 + i, 1), one(1024 - i, i, 2 * i + 1, 1);
  for (k = 0; k < n; k++) {
    const uint32_t a = rnd(), b = rnd();
    int x = (int)(a & 4095) - 2048, y = (int)(a >> 12 & 2047) - 1024, w, h;
    switch (b & 7) {
    case 0: w = (int)(b >> 3 & 2047); break;        /* up to twice VRAM's width */
    case 1: w = 1024 - (int)(b >> 3 & 31); break;   /* about a row */
    case 2: w = (int)(b >> 3 & 7) - 2; break;       /* tiny, empty, negative */
    default: w = (int)(b >> 3 & 127); break;        /* the usual */
    }
    h = b >> 20 & 15 ? (int)(b >> 24 & 15) : (int)(b >> 24 & 255) * 3 - 8;
    one(x, y, w, h);
  }
  out_u(s_rects);
  out(" rects, ");
  out_u(s_px);
  out(" pixels, ");
  out_u(s_bad);
  out(" differ (D111)\n");
  return s_bad != 0;
}

#ifndef FILL_FREESTANDING
int main(int argc, char **argv) {
  return fill_test(argc > 1 ? strtoull(argv[1], NULL, 0) : 1u << 18);
}
#endif

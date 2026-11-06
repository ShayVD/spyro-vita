/* psx_fill.h: ClearImage's fill of the PS1's 1024x512 16-bit VRAM, row by
 * row (D111). The plain loop it replaces sets one pixel at a time through
 * the wrap (row[(x + j) & 1023] = c), which the compiler can't widen. Here a
 * row's run is split where it wraps and each piece filled in one go: 16
 * pixels a step with NEON on the Vita, two a step elsewhere. The pixels
 * written are the plain loop's, in VRAM and everywhere else.
 * Shared by the Vita's GL side (gpu_gl.c's gpu_clear=1) and the host's
 * software rasterizer (gpu_soft.c, --gpu-clear). tests/fill compares it
 * with the plain loop, on the host and built for the Vita under qemu-arm. */
#ifndef PSX_FILL_H
#define PSX_FILL_H

#include <stdint.h>

#if defined(__ARM_NEON) && !defined(PSX_FILL_C)
#include <arm_neon.h>
#endif

#define PSX_FILL_VW 1024
#define PSX_FILL_VH 512

/* n pixels from p set to c */
static inline void psx_fill16(uint16_t *p, uint16_t c, int n) {
#if defined(__ARM_NEON) && !defined(PSX_FILL_C)
  if (n >= 24) {
    const uint16x8_t v = vdupq_n_u16(c);
    for (; (uintptr_t)p & 15; n--) /* p is 2-aligned: at most 7 */
      *p++ = c;
    for (; n >= 16; n -= 16, p += 16) {
      vst1q_u16(p, v);
      vst1q_u16(p + 8, v);
    }
  }
#else
  if (n >= 4) {
    const uint32_t w = (uint32_t)c | (uint32_t)c << 16;
    uint32_t *q;
    if ((uintptr_t)p & 2) {
      *p++ = c;
      n--;
    }
    for (q = (uint32_t *)p; n >= 2; n -= 2)
      *q++ = w;
    p = (uint16_t *)q;
  }
#endif
  while (n-- > 0)
    *p++ = c;
}

/* ClearImage's rect of vram (1024x512) set to c, wrapping at the edges as
 * the PS1's coordinates do: the same pixels as
 *   for (i = 0; i < h; i++) for (j = 0; j < w; j++)
 *     vram[((y + i) & 511) * 1024 + ((x + j) & 1023)] = c;
 * A rect 1024 or more wide fills whole rows; one 512 or more high, every row. */
static inline void psx_fill_rect(uint16_t *vram, int x, int y, int w, int h, uint16_t c) {
  const int px = x & (PSX_FILL_VW - 1);
  int w0, w1, i;
  if (w <= 0 || h <= 0)
    return;
  if (w >= PSX_FILL_VW)
    w0 = PSX_FILL_VW - px, w1 = px;
  else
    w0 = px + w > PSX_FILL_VW ? PSX_FILL_VW - px : w, w1 = w - w0;
  if (h > PSX_FILL_VH)
    h = PSX_FILL_VH; /* the rows past 512 are those again, the same colour */
  for (i = 0; i < h; i++) {
    uint16_t *row = vram + ((y + i) & (PSX_FILL_VH - 1)) * PSX_FILL_VW;
    psx_fill16(row + px, c, w0);
    if (w1)
      psx_fill16(row, c, w1);
  }
}

#endif

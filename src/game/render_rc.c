/* fast_render=3's pieces outside the renderer files (stage 7d2, decisions
 * D124; render_hand.h NGTE_RC).
 *
 *  - g_render_work: the readable renderers' working memory, in place of the
 *    PS1 scratchpad. A call may use any of it and nothing reads it after
 *    the call (below), so it is a buffer, not state. The presenter build
 *    has its own (render_ctx_names.h: g_pres_scratch).
 *  - --render-poison (host): the audit that shows it. At fast_render 1 and
 *    2, after each call of a renderer (or of the B2 functions beside them,
 *    render_hand.h HAND_UNIT_FN_RC) returns to code outside the renderers,
 *    everything the register-file code leaves behind and fast_render=3
 *    doesn't write is overwritten with a pattern: the scratchpad, the GTE's
 *    32 data registers and FLAG, the spill area D_80077DD8 and the hand
 *    stack below g_mips_sp. Anything that read one of them later, a
 *    renderer's next call included, would see the pattern, and the run's
 *    traces, shots, audio or cards would change. The pattern changes every
 *    call (a counter mixed in), so a reader can't happen to see the same
 *    value twice. */
#include "render_hand.h"

#include "psx_arena.h"

#include <string.h>

unsigned char g_render_work[1024] __attribute__((aligned(1024)));

int g_render_poison;
static int s_depth;
static u32 s_calls;
unsigned long g_render_poison_calls; /* for the log at exit */

extern char D_80077DD8[];
enum {
  SPILL_BYTES = 0xC8, /* D_80077DD8 .. D_80077EA0: hand_spill_regs, the sky's clipping, the particles */
#if SPYRO_NATIVE_MEM
  STACK_BYTES = 0x100, /* port.c's s_hand_stack */
#else
  STACK_BYTES = 0x1000,
#endif
};

static u32 mix(u32 x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

static void fill(void *p, u32 bytes, u32 seed) {
  u32 *w = (u32 *)p, i;
  for (i = 0; i < bytes / 4; i++)
    w[i] = mix(seed + i);
}

static void poison(void) {
  const u32 seed = mix(++s_calls) ;
#if SPYRO_NATIVE_MEM
  fill(g_psx_scratch, sizeof g_psx_scratch, seed);
#else
  fill((void *)g_psx_arena_base, 1024, seed);
#endif
  fill(gteRegs.CP2D.r, sizeof gteRegs.CP2D.r, seed ^ 0x1111u);
  gteRegs.CP2C.r[31] = mix(seed ^ 0x2222u);
  fill(D_80077DD8, SPILL_BYTES, seed ^ 0x3333u);
  if (g_mips_sp)
    fill((void *)(uintptr_t)(g_mips_sp - STACK_BYTES), STACK_BYTES, seed ^ 0x4444u);
  if (g_render_poison == 2) /* --render-poison-bite: OFX too, which the renderers do read */
    gteRegs.CP2C.r[24] = mix(seed ^ 0x5555u) & 0x00FF0000u;
  g_render_poison_calls++;
}

u32 render_poison(const char *name, HandFn fn, u32 a0, u32 a1, u32 a2, u32 a3) {
  u32 r;
  (void)name;
  s_depth++;
  r = fn(a0, a1, a2, a3);
  if (--s_depth == 0)
    poison();
  return r;
}

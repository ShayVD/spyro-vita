/* The glow and sparkle slots (asm/renderers/r_particles.s: func_8005882C,
 * func_80058AE8, func_80058B60, func_80058B68, hand-written asm in the
 * original), written by hand from the asm so it reads as C
 * (docs/native-plan.md stage 3, decisions D96). The asm uses neither the
 * scratchpad nor the GTE, and leaves nothing in RAM but the slots.
 *
 * The rest of r_particles.s is hand C already (particles_hand.c, D95), so
 * the unit stays linked for fast_render=0 (render_hand.h HAND_UNIT_FN):
 * fast_render=3 runs this, --fast-render-check compares each call with the
 * unit (docs/translation-log.md). */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

/* the decomp's prototypes, out of the way of the entries' own */
#define func_8005882C func_8005882C_decl
#define func_80058AE8 func_80058AE8_decl
#define func_80058B60 func_80058B60_decl
#define func_80058B68 func_80058B68_decl
#include "graphics.h"
#include "renderers.h"
#undef func_8005882C
#undef func_80058AE8
#undef func_80058B60
#undef func_80058B68

#include "render_hand.h"

#include <stddef.h>

static_assert(sizeof(Sparkle) == 0x18 && offsetof(Sparkle, m_Life) == 0xC, "Sparkle");
static_assert(sizeof(Glow) == 0x24 && offsetof(Glow, m_Vertices) == 4 && offsetof(Glow, m_OtOffset) == 0x20,
              "Glow");

#define SPARKLE_SLOTS 8
#define GLOW_SLOTS 16
/* a new glow's outline: g_DefaultGlowVerts' 17 points */
#define GLOW_DEFAULT_VERTS 17

/* A sparkle slot is free when its life is 0, a glow's when it has no
 * vertices. */

/* The first free sparkle slot's index, or -1. */
static u32 FindSparkle(u32 a0, u32 a1, u32 a2, u32 a3) {
  int i;
  (void)a0, (void)a1, (void)a2, (void)a3;
  for (i = 0; i < SPARKLE_SLOTS; i++)
    if (g_Sparkles[i].m_Life == 0)
      return (u32)i;
  return (u32)-1;
}

/* The first free glow, given the default outline; NULL when all are used. */
static u32 AllocGlow(u32 a0, u32 a1, u32 a2, u32 a3) {
  int i;
  (void)a0, (void)a1, (void)a2, (void)a3;
  for (i = 0; i < GLOW_SLOTS; i++) {
    Glow *glow = &g_Glows[i];
    if (glow->m_VertexCount == 0) {
      glow->m_VertexCount = GLOW_DEFAULT_VERTS;
      glow->m_Vertices = g_DefaultGlowVerts;
      glow->m_OtOffset = 0;
      return PSX_ADDR(glow);
    }
  }
  return 0;
}

/* Frees a glow (the asm stores through the pointer as the PS1 maps it). */
static u32 FreeGlow(u32 a0, u32 a1, u32 a2, u32 a3) {
  (void)a1, (void)a2, (void)a3;
  HPTR(Glow, a0)->m_VertexCount = 0;
  return 0;
}

/* Frees every glow and sparkle. */
static u32 FreeAll(u32 a0, u32 a1, u32 a2, u32 a3) {
  int i;
  (void)a0, (void)a1, (void)a2, (void)a3;
  for (i = 0; i < GLOW_SLOTS; i++)
    g_Glows[i].m_VertexCount = 0;
  for (i = 0; i < SPARKLE_SLOTS; i++)
    g_Sparkles[i].m_Life = 0;
  return 0;
}

/* v1, which the asm leaves as its last load or address, is dead at every
 * caller (all C; tools/oracle/ignore_ranges.txt). */
HAND_UNIT_FN_V0(func_8005882C, FindSparkle)
HAND_UNIT_FN_V0(func_80058AE8, AllocGlow)
HAND_UNIT_FN(func_80058B60, FreeGlow)
HAND_UNIT_FN(func_80058B68, FreeAll)

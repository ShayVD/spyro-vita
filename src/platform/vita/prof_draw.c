/* Where the game's own CPU time goes on the Vita ("draw" lines in spyro.log,
 * every 300 vblanks, next to prof.c's "time" lines): the update, and each of
 * the renderers GamestateDraw calls (environment, mobys, Spyro, shadows and
 * effects, particles, sky). They are wrapped at link time
 * (-Wl,--wrap=NAME, src/platform/vita/CMakeLists.txt); only calls from the
 * decomp's C are seen, which is every top-level call. Presentation passes
 * (src/game/present.c) run the same renderers, so an in-between frame shows
 * up here too. prof.c's "ot" and "raster" stay where they were. */
#include "vita.h"

#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { D_UPDATE, D_ENV, D_MOBYS, D_PETE, D_FX, D_PARTICLES, D_SKY, D_COUNT };
static const char *const k_names[D_COUNT] = {"update", "env", "mobys",    "spyro",
                                             "fx",     "particles", "sky"};
static uint64_t s_acc[D_COUNT];
static unsigned s_calls[D_COUNT];

static uint64_t now_us(void) { return sceKernelGetProcessTimeWide(); }

#define WRAP_M2C(part, name)                                                   \
  uint32_t __real_##name(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,      \
                         uint32_t, uint32_t, uint32_t);                        \
  uint32_t __wrap_##name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,    \
                         uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { \
    uint64_t t = now_us();                                                     \
    uint32_t r = __real_##name(a0, a1, a2, a3, a4, a5, a6, a7);                \
    s_acc[part] += now_us() - t;                                               \
    s_calls[part]++;                                                           \
    return r;                                                                  \
  }

WRAP_M2C(D_ENV, func_800258F0)    /* r_environment: the level */
WRAP_M2C(D_MOBYS, func_8001F158)  /* r_moby: cull */
WRAP_M2C(D_MOBYS, func_8001F798)  /* r_moby: regular mobys */
WRAP_M2C(D_MOBYS, func_800208FC)  /* r_moby: specular cull */
WRAP_M2C(D_MOBYS, func_80020F34)  /* r_moby: specular mobys */
WRAP_M2C(D_MOBYS, func_80022A2C)  /* r_moby: shaded mobys, the HUD */
WRAP_M2C(D_PETE, func_80023AC4)   /* r_pete: Spyro */
WRAP_M2C(D_FX, func_80059F8C)     /* r_shadows: moby shadows */
WRAP_M2C(D_FX, func_80059A48)     /* r_shadows: Spyro's shadow */
WRAP_M2C(D_FX, func_80058D64)     /* r_flame */
WRAP_M2C(D_FX, func_80058BA8)     /* r_particles: glows and sparkles */
WRAP_M2C(D_PARTICLES, func_800573C8) /* r_particles */
WRAP_M2C(D_SKY, func_80050BD0)    /* cyclorama set-up and r_cyclorama */
WRAP_M2C(D_SKY, func_8004EBA8)    /* r_cyclorama: the sky, from draw.c (func_80050BD0
                                     calls its register entry, not this) */

void __real_GamestateUpdate(void);
void __wrap_GamestateUpdate(void) {
  uint64_t t = now_us();
  __real_GamestateUpdate();
  s_acc[D_UPDATE] += now_us() - t;
  s_calls[D_UPDATE]++;
}

/* vblank.c, with the other reports. */
void prof_draw_report(int vblank, int vblanks) {
  char buf[256];
  int i, n;
  if (vblanks <= 0)
    return;
  n = snprintf(buf, sizeof buf, "draw %d: per vblank ms", vblank);
  for (i = 0; i < D_COUNT; i++)
    n += snprintf(buf + n, sizeof buf - n, " %s %u.%02u", k_names[i],
                  (unsigned)(s_acc[i] / vblanks / 1000),
                  (unsigned)(s_acc[i] / vblanks % 1000 / 10));
  snprintf(buf + n, sizeof buf - n, " (env calls %u)", s_calls[D_ENV]);
  vita_log("%s", buf);
  memset(s_acc, 0, sizeof s_acc);
  memset(s_calls, 0, sizeof s_calls);
}

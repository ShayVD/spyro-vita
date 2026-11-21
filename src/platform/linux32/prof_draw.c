/* --prof-draw: the CPU time of each renderer GamestateDraw calls, as the
 * Vita's "draw" lines (src/platform/vita/prof_draw.c) but in total over the
 * run, logged at exit. Under qemu-i386 these are only relative numbers: for
 * comparing two builds or fast_render=0 against 1 on the same script
 * (decisions D72). The functions are wrapped at link time (CMakeLists.txt).
 * Each part counts its time once (the sky: D181). */
#include "host.h"

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

int g_host_prof_draw;

enum { D_UPDATE, D_ENV, D_MOBYS, D_PETE, D_FX, D_PARTICLES, D_SKY, D_COUNT };
static const char *const k_names[D_COUNT] = {"update", "env", "mobys", "spyro",
                                             "fx", "particles", "sky"};
static uint64_t s_acc[D_COUNT];
static unsigned s_calls[D_COUNT];

static uint64_t now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

#define WRAP_M2C(part, name)                                                   \
  uint32_t __real_##name(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,      \
                         uint32_t, uint32_t, uint32_t);                        \
  uint32_t __wrap_##name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,    \
                         uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { \
    uint64_t t;                                                                \
    uint32_t r;                                                                \
    if (!g_host_prof_draw)                                                     \
      return __real_##name(a0, a1, a2, a3, a4, a5, a6, a7);                    \
    t = now_ns();                                                              \
    r = __real_##name(a0, a1, a2, a3, a4, a5, a6, a7);                         \
    s_acc[part] += now_ns() - t;                                               \
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
/* The sky (D181, #200; as src/platform/vita/prof_draw.c). func_80050BD0
 * (cyclorama_hand.c: the portals, then the level's sky) calls func_8004EBA8
 * (sky_hand.c), and the wrap catches that call too, so charging both counted
 * the main draw twice. `sky` is the whole sky once: func_80050BD0 with what
 * it calls, plus func_8004EBA8 when draw.c, the title screen or the credits
 * call it on their own. The main draw's own time, from either caller, is
 * "sky main", on its own line, outside the parts and the renderers' sum. */
static int s_in_cyclorama;
static uint64_t s_skymain;
static unsigned s_skymain_calls;

uint32_t __real_func_80050BD0(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t);
uint32_t __wrap_func_80050BD0(uint32_t a0, uint32_t a1, uint32_t a2,
                              uint32_t a3, uint32_t a4, uint32_t a5,
                              uint32_t a6, uint32_t a7) {
  uint64_t t;
  uint32_t r;
  if (!g_host_prof_draw)
    return __real_func_80050BD0(a0, a1, a2, a3, a4, a5, a6, a7);
  t = now_ns();
  s_in_cyclorama++;
  r = __real_func_80050BD0(a0, a1, a2, a3, a4, a5, a6, a7);
  s_in_cyclorama--;
  s_acc[D_SKY] += now_ns() - t;
  s_calls[D_SKY]++;
  return r;
}

uint32_t __real_func_8004EBA8(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t);
uint32_t __wrap_func_8004EBA8(uint32_t a0, uint32_t a1, uint32_t a2,
                              uint32_t a3, uint32_t a4, uint32_t a5,
                              uint32_t a6, uint32_t a7) {
  uint64_t t, ns;
  uint32_t r;
  if (!g_host_prof_draw)
    return __real_func_8004EBA8(a0, a1, a2, a3, a4, a5, a6, a7);
  t = now_ns();
  r = __real_func_8004EBA8(a0, a1, a2, a3, a4, a5, a6, a7);
  ns = now_ns() - t;
  s_skymain += ns;
  s_skymain_calls++;
  if (!s_in_cyclorama) {
    s_acc[D_SKY] += ns;
    s_calls[D_SKY]++;
  }
  return r;
}

void __real_GamestateUpdate(void);
void __wrap_GamestateUpdate(void) {
  uint64_t t;
  if (!g_host_prof_draw) {
    __real_GamestateUpdate();
    return;
  }
  t = now_ns();
  __real_GamestateUpdate();
  s_acc[D_UPDATE] += now_ns() - t;
  s_calls[D_UPDATE]++;
}

/* main.c at exit */
void host_prof_draw_report(void) {
  uint64_t all = 0;
  int i;
  if (!g_host_prof_draw)
    return;
  for (i = D_ENV; i < D_COUNT; i++)
    all += s_acc[i];
  for (i = 0; i < D_COUNT; i++)
    host_log("prof draw: %-9s %9.1f ms in %6u calls", k_names[i], s_acc[i] / 1e6, s_calls[i]);
  host_log("prof draw: renderers %9.1f ms", all / 1e6);
  host_log("prof draw: (sky main %9.1f ms in %6u calls, part of sky)", s_skymain / 1e6,
           s_skymain_calls);
}

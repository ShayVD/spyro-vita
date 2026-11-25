/* Where the game's own CPU time goes on the Vita ("draw" lines in spyro.log,
 * every 300 vblanks, next to prof.c's "time" lines): the update, and each of
 * the renderers GamestateDraw calls (environment, mobys, Spyro, shadows and
 * effects, particles, sky; each counted once). They are wrapped at link time
 * (-Wl,--wrap=NAME, src/platform/vita/CMakeLists.txt); only calls from the
 * decomp's C are seen, which is every top-level call. Presentation passes
 * (src/game/present.c) run the same renderers, so an in-between frame shows
 * up here too. prof.c's "ot" and "raster" stay where they were. Since #207
 * each part also counts the bytes of primitives it wrote (the primitive
 * pointer's move across each wrapped call, as the host's --prof-draw does
 * since #200), logged as a "draw kb N:" line after the "draw N:" line, so a
 * hardware log gives each part's ms per KB. With 60 Hz logic the update is
 * GamestateUpdate_60, wrapped too since #223 (below). */
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
/* #207: each part's primitives, the bytes the primitive pointer moved on */
static uint64_t s_bytes[D_COUNT];

extern P32F(void *) D_800757B0; /* the primitive buffer's next free byte */

typedef struct {
  uint64_t t;
  uint32_t prim;
} Mark;
/* nd_ab=1 (D91, D92): the same, by the way (native_draw 0, 1 or 2) the list being
 * built is counted under */
enum { AB_N = 3 }; /* ND_AB_VARIANTS */
int nd_ab_slot(void);
void nd_ab_take(unsigned lists[AB_N]);
static uint64_t s_ab_acc[AB_N][D_COUNT];

static uint64_t now_us(void) { return sceKernelGetProcessTimeWide(); }

static uint32_t prim_at(void) { return (uint32_t)(uintptr_t)D_800757B0; }

static Mark mark(void) {
  Mark m;
  m.prim = prim_at();
  m.t = now_us();
  return m;
}

static void charge(int part, const Mark *m) {
  const uint64_t us = now_us() - m->t;
  const uint32_t prim = prim_at() - m->prim;
  const int v = nd_ab_slot();
  s_acc[part] += us;
  s_calls[part]++;
  if (prim < 0x100000) /* the HUD's mobys are drawn from the buffer's top */
    s_bytes[part] += prim;
  if ((unsigned)v < AB_N)
    s_ab_acc[v][part] += us;
}

#define WRAP_M2C(part, name)                                                   \
  uint32_t __real_##name(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,      \
                         uint32_t, uint32_t, uint32_t);                        \
  uint32_t __wrap_##name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,    \
                         uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { \
    Mark m = mark();                                                           \
    uint32_t r = __real_##name(a0, a1, a2, a3, a4, a5, a6, a7);                \
    charge(part, &m);                                                          \
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
/* The sky (#190). func_80050BD0 (cyclorama_hand.c: the portals, then the
 * level's sky) calls func_8004EBA8 (sky_hand.c), and the wrap catches that
 * call too, so charging both counted the main draw twice (D177). `sky` is the
 * whole sky once: func_80050BD0 with what it calls, plus func_8004EBA8 when
 * draw.c, the title screen or the credits call it on their own. The main
 * draw's own time, from either caller, is "sky main" in the line's
 * parenthetical, outside the parts (the scripts in tools/vita cut the line at
 * "("). */
static int s_in_cyclorama;
static uint64_t s_skymain;

uint32_t __real_func_80050BD0(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t);
uint32_t __wrap_func_80050BD0(uint32_t a0, uint32_t a1, uint32_t a2,
                              uint32_t a3, uint32_t a4, uint32_t a5,
                              uint32_t a6, uint32_t a7) {
  Mark m = mark();
  uint32_t r;
  s_in_cyclorama++;
  r = __real_func_80050BD0(a0, a1, a2, a3, a4, a5, a6, a7);
  s_in_cyclorama--;
  charge(D_SKY, &m);
  return r;
}

uint32_t __real_func_8004EBA8(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t);
uint32_t __wrap_func_8004EBA8(uint32_t a0, uint32_t a1, uint32_t a2,
                              uint32_t a3, uint32_t a4, uint32_t a5,
                              uint32_t a6, uint32_t a7) {
  Mark m = mark();
  uint32_t r = __real_func_8004EBA8(a0, a1, a2, a3, a4, a5, a6, a7);
  s_skymain += now_us() - m.t;
  if (!s_in_cyclorama)
    charge(D_SKY, &m);
  return r;
}

void __real_GamestateUpdate(void);
void __wrap_GamestateUpdate(void) {
  Mark m = mark();
  __real_GamestateUpdate();
  charge(D_UPDATE, &m);
}

#ifdef SPYRO_PROF_TICK60
/* With 60 Hz logic in play the update is GamestateUpdate_60 (tick60.c's
 * PortGamestateUpdate calls it instead of GamestateUpdate; the update path
 * built again, cmake/tick60.cmake), so until #223 the "update" column read
 * 0.00 in 60 Hz windows. The same column: each PortGamestateUpdate runs one
 * or the other, and tick60_update.o's calls inside itself go to its own
 * copies, so the GamestateUpdate wrap never sees one of them. */
void __real_GamestateUpdate_60(void);
void __wrap_GamestateUpdate_60(void) {
  Mark m = mark();
  __real_GamestateUpdate_60();
  charge(D_UPDATE, &m);
}
#endif

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
  snprintf(buf + n, sizeof buf - n, " (env calls %u; sky main %u.%02u)",
           s_calls[D_ENV], (unsigned)(s_skymain / vblanks / 1000),
           (unsigned)(s_skymain / vblanks % 1000 / 10));
  vita_log("%s", buf);
  /* "draw kb": the same parts' KB of primitives per vblank, so a part's ms
   * per KB is its "draw" figure over this one. Its own line: the "draw N:"
   * line stays as the scripts in tools/vita read it, and they match no
   * "draw kb" (#207). */
  n = snprintf(buf, sizeof buf, "draw kb %d: per vblank KB of primitives", vblank);
  for (i = 0; i < D_COUNT; i++) {
    const uint64_t tenths = s_bytes[i] * 10 / 1024 / (unsigned)vblanks;
    n += snprintf(buf + n, sizeof buf - n, " %s %u.%u", k_names[i],
                  (unsigned)(tenths / 10), (unsigned)(tenths % 10));
  }
  vita_log("%s", buf);
  memset(s_acc, 0, sizeof s_acc);
  s_skymain = 0;
  memset(s_calls, 0, sizeof s_calls);
  memset(s_bytes, 0, sizeof s_bytes);
  {
    /* "draw ab": per variant, the lists counted and each part's whole us
     * (sums, so windows add up; tools/vita/nd_ab_summary.py) */
    char ab[512];
    unsigned lists[AB_N];
    int v;
    nd_ab_take(lists);
    for (v = 0; v < AB_N; v++)
      if (lists[v])
        break;
    if (v == AB_N)
      return;
    n = snprintf(ab, sizeof ab, "draw ab %d:", vblank);
    for (v = 0; v < AB_N; v++) {
      n += snprintf(ab + n, sizeof ab - n, " v%d %u", v, lists[v]);
      for (i = D_ENV; i < D_COUNT; i++)
        n += snprintf(ab + n, sizeof ab - n, "/%u", (unsigned)s_ab_acc[v][i]);
      n += snprintf(ab + n, sizeof ab - n, ";");
    }
    vita_log("%s", ab);
    memset(s_ab_acc, 0, sizeof s_ab_acc);
  }
}

/* --prof-draw: the CPU time of each renderer GamestateDraw calls, as the
 * Vita's "draw" lines (src/platform/vita/prof_draw.c) but in total over the
 * run, logged at exit. Under qemu-i386 these are only relative numbers: for
 * comparing two builds or fast_render=0 against 1 on the same script
 * (decisions D72). The functions are wrapped at link time (CMakeLists.txt).
 * Each part counts its time once (the sky: D181), and since #200 the bytes
 * of primitives it wrote, so its cost per KB can be read off. Since #259
 * it also counts the vertex cache's records the game thread made in each
 * part (as the Vita's "draw vc" line), and PROF_DRAW_FROM=N in the
 * environment counts only from vblank N on (a scene of a route: Artisans
 * on the owner60 pad from 6,600). */
#include "host.h"
#include "psx_vcache.h"

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

int g_host_prof_draw;

extern char D_800757B0[]; /* the primitive buffer's next free byte (a word) */

enum { D_UPDATE, D_ENV, D_MOBYS, D_PETE, D_FX, D_PARTICLES, D_SKY, D_COUNT };
static const char *const k_names[D_COUNT] = {"update", "env", "mobys", "spyro",
                                             "fx", "particles", "sky"};
static uint64_t s_acc[D_COUNT];
static unsigned s_calls[D_COUNT];
/* #200: each part's primitives, the bytes the primitive pointer moved on */
static uint64_t s_bytes[D_COUNT];

/* #259: each part's vertex-cache records: recorded, worked out at once,
 * kept for the walk, repeats skipped */
enum { R_REC, R_ONCE, R_KEPT, R_REP, R_N };
static uint64_t s_recs[D_COUNT][R_N];
static int s_from = -1; /* PROF_DRAW_FROM (-1: not read yet) */

typedef struct {
  uint64_t t;
  uint32_t prim;
  unsigned rec[R_N];
} Mark;

/* counting yet (PROF_DRAW_FROM) */
static int counting(void) {
  if (s_from < 0) {
    const char *const v = getenv("PROF_DRAW_FROM");
    s_from = v ? atoi(v) : 0;
  }
  return host_vblank_count() >= s_from;
}

static uint64_t now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static Mark mark(void) {
  Mark m;
  m.prim = *(uint32_t *)D_800757B0;
  psx_vcache_game_recs(m.rec);
  m.t = now_ns();
  return m;
}

static void charge(int part, const Mark *m) {
  const uint64_t t = now_ns();
  const uint32_t prim = *(uint32_t *)D_800757B0 - m->prim;
  unsigned r[R_N], k;
  if (!counting())
    return;
  psx_vcache_game_recs(r);
  for (k = 0; k < R_N; k++) /* (a frame's end inside the call starts the counts again) */
    s_recs[part][k] += r[k] >= m->rec[k] ? r[k] - m->rec[k] : r[k];
  s_acc[part] += t - m->t;
  s_calls[part]++;
  if (prim < 0x100000) /* the HUD's mobys are drawn from the buffer's top */
    s_bytes[part] += prim;
}

#define WRAP_M2C(part, name)                                                   \
  uint32_t __real_##name(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,      \
                         uint32_t, uint32_t, uint32_t);                        \
  uint32_t __wrap_##name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,    \
                         uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { \
    Mark m;                                                                    \
    uint32_t r;                                                                \
    if (!g_host_prof_draw)                                                     \
      return __real_##name(a0, a1, a2, a3, a4, a5, a6, a7);                    \
    m = mark();                                                                \
    r = __real_##name(a0, a1, a2, a3, a4, a5, a6, a7);                         \
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
  Mark m;
  uint32_t r;
  if (!g_host_prof_draw)
    return __real_func_80050BD0(a0, a1, a2, a3, a4, a5, a6, a7);
  m = mark();
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
  Mark m;
  uint32_t r;
  uint64_t t;
  if (!g_host_prof_draw)
    return __real_func_8004EBA8(a0, a1, a2, a3, a4, a5, a6, a7);
  m = mark();
  r = __real_func_8004EBA8(a0, a1, a2, a3, a4, a5, a6, a7);
  t = now_ns();
  if (counting()) {
    s_skymain += t - m.t;
    s_skymain_calls++;
  }
  if (!s_in_cyclorama)
    charge(D_SKY, &m);
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
  if (!counting())
    return;
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
    host_log("prof draw: %-9s %9.1f ms in %6u calls; %8llu KB of primitives", k_names[i],
             s_acc[i] / 1e6, s_calls[i], (unsigned long long)(s_bytes[i] / 1024));
  host_log("prof draw: renderers %9.1f ms", all / 1e6);
  for (i = 0; i < D_COUNT; i++) /* #259 */
    host_log("prof draw vc: %-9s %9llu records, %9llu worked out at once (%llu repeats skipped), "
             "%9llu kept for the walk",
             k_names[i], (unsigned long long)s_recs[i][R_REC], (unsigned long long)s_recs[i][R_ONCE],
             (unsigned long long)s_recs[i][R_REP], (unsigned long long)s_recs[i][R_KEPT]);
  if (s_from > 0)
    host_log("prof draw: counted from vblank %d (PROF_DRAW_FROM)", s_from);
  host_log("prof draw: (sky main %9.1f ms in %6u calls, part of sky)", s_skymain / 1e6,
           s_skymain_calls);
}

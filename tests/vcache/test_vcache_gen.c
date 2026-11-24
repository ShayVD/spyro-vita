/* The vertex cache's generations' check (#218, vcache.c gc_check): the record
 * compares a native projection's registers by their generation (gte_native.h
 * NgteProj gen, #205), so a renderer that changes a field after a projection
 * without ngte_proj_regen would get another projection's answers. The check
 * (host builds, VC_CHECK_GEN=1 or psx_vcache_set_check_gen) must count each
 * such projection (gen_stale) under the renderer context that returns next,
 * and must count nothing for a new generation, for a projection with none,
 * or while it is off (the default). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "gte_native.h"
#include "psx_gpu.h"
#include "psx_vcache.h"

/* the polygon lookups' walk (gpu_walk.c), which no lookup here reaches:
 * the cache's object alone links, without the walk and the GPU */
PsxGpuWalk *psx_gpu_game_walk(void) { return NULL; }

/* the platform's log (log.c in the game's build) */
void PsyX_Log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("log: ", stdout);
  vprintf(fmt, ap);
  fputc('\n', stdout);
  va_end(ap);
}

static int s_fail;

static void expect_n(unsigned got, unsigned want, const char *what) {
  if (got != want) {
    printf("FAIL %s: gen_stale %u, want %u\n", what, got, want);
    s_fail++;
  } else
    printf("ok   %s: gen_stale %u\n", what, got);
}

/* a camera-like projection, a new generation */
static NgteProj proj(void) {
  NgteProj p;
  p.r11 = 4096, p.r12 = 0, p.r13 = 0;
  p.r21 = 0, p.r22 = 4096, p.r23 = 0;
  p.r31 = 0, p.r32 = 0, p.r33 = 4096;
  p.trx = 10, p.try_ = -20, p.trz = 2000;
  p.ofx = 256 << 16, p.ofy = 120 << 16;
  p.h = 320;
  p.dqa = -100, p.dqb = 0x1400000;
  ngte_proj_regen(&p);
  return p;
}

/* RTPS on a vertex, recorded (gte_native.h ngte_rtps_w -> ngte_vcache) */
static void rtps(const NgteProj *p, int x, int y, int z) {
  (void)ngte_rtps_w(p, (unsigned)(x & 0xFFFF) | (unsigned)y << 16, (unsigned)(z & 0xFFFF));
}

/* the presenter's record of the same (its build's ngte_vcache) */
static void rtps_pres(const NgteProj *p, int x, int y, int z) {
  const unsigned vxy = (unsigned)(x & 0xFFFF) | (unsigned)y << 16, vz = (unsigned)(z & 0xFFFF);
  const NgteRtp o = ngte_rtps(p, ngte_vx(vxy), ngte_vy(vxy), ngte_vz(vz));
  pres_vcache_ngte(p, vxy, vz, &o);
}

int main(void) {
  NgteProj p, q;
  unsigned base;

  unsetenv("VC_CHECK_GEN"); /* the default: off */
  psx_vcache_enable(2);     /* the cache records: projections take generations */

  /* off: a field changed without a new generation is not counted */
  p = proj();
  if (!p.gen) {
    printf("FAIL no generation while the cache records\n");
    return 1;
  }
  rtps(&p, 100, 50, 0);
  p.trx += 7;
  rtps(&p, 100, 50, 0);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, -1), 0, "off by default");

  psx_vcache_set_check_gen(1);

  /* the same registers projected again under one generation: nothing */
  p = proj();
  rtps(&p, 100, 50, 0);
  rtps(&p, -30, 80, 40);
  q = p; /* a copy keeps the generation and the registers */
  rtps(&q, 5, 5, 5);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, -1), 0, "same registers, same generation");

  /* a field changed after a projection, no ngte_proj_regen: counted, and
   * again for each projection of it */
  p.trx += 7;
  rtps(&p, 100, 50, 0);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, -1), 1, "translation changed without regen");
  expect_n(psx_vcache_gen_stale(VC_LOGIC, VCX_N), 1, "... not named before a renderer returns");
  rtps(&p, -30, 80, 40);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, -1), 2, "... its next projection too");

  /* named by the renderer that returns next */
  psx_vc_ctx("MobyDraw", 0, 0, 0, 0, 0);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, VCX_MOBY), 2, "named MobyDraw as it returns");
  expect_n(psx_vcache_gen_stale(VC_LOGIC, VCX_N), 0, "... none left unnamed");

  /* a copy with one field changed (the rotation, H, the depth cue) */
  q = proj();
  rtps(&q, 1, 2, 3);
  p = q;
  p.r12 = 17;
  rtps(&p, 1, 2, 3);
  p = q;
  p.h = 200;
  rtps(&p, 1, 2, 3);
  p = q;
  p.dqb += 1;
  rtps(&p, 1, 2, 3);
  psx_vc_ctx("SpyroDraw", 0, 0, 0, 0, 0);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, VCX_SPYRO), 3, "rotation, H and depth cue changed");
  expect_n(psx_vcache_gen_stale(VC_LOGIC, -1), 5, "... in all");

  /* the rule kept: a field changed and a new generation taken */
  base = psx_vcache_gen_stale(VC_LOGIC, -1);
  p = proj();
  rtps(&p, 100, 50, 0);
  p.trz += 100;
  ngte_proj_regen(&p);
  rtps(&p, 100, 50, 0);
  ngte_proj_rot(&p, 0x1000, 0, 0x1000, 0, 0x1000); /* (each takes a new one) */
  rtps(&p, 100, 50, 0);
  ngte_proj_tr(&p, 1, 2, 3000);
  rtps(&p, 100, 50, 0);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, -1) - base, 0, "fields changed with a new generation");

  /* no generation (gen 0): the record compares the words, nothing to check */
  p.gen = 0;
  rtps(&p, 100, 50, 0);
  p.trx += 1;
  rtps(&p, 100, 50, 0);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, -1) - base, 0, "no generation");

  /* the presenter's record: its own count */
  if (psx_vcache_pres_begin(1)) {
    q = proj();
    rtps_pres(&q, 9, 9, 9);
    q.ofy += 1 << 16;
    rtps_pres(&q, 9, 9, 9);
    pres_vc_ctx("SkyDraw", 0, 0, 0, 0, 0);
    expect_n(psx_vcache_gen_stale(VC_PRES, VCX_SKY), 1, "the presenter's, named SkyDraw");
    expect_n(psx_vcache_gen_stale(VC_LOGIC, -1) - base, 0, "... not the logic draw's");
  } else {
    printf("FAIL the presenter's table\n");
    s_fail++;
  }

  /* off again: nothing more */
  psx_vcache_set_check_gen(0);
  base = psx_vcache_gen_stale(VC_LOGIC, -1);
  p = proj();
  rtps(&p, 100, 50, 0);
  p.trx += 7;
  rtps(&p, 100, 50, 0);
  expect_n(psx_vcache_gen_stale(VC_LOGIC, -1) - base, 0, "turned off");

  printf("test_vcache_gen: %s\n", s_fail ? "FAILED" : "passed");
  return s_fail != 0;
}

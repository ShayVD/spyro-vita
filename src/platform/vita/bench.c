/* bench=1 (decisions D41): one hardware run that measures what each GPU
 * picture option costs. With no input the game plays its title intro, the
 * title and both attract demos; from vblank BENCH_START the option sets
 * below take turns, BENCH_SLOT vblanks each, for BENCH_CYCLES rounds, so
 * every set sees the same mix of scenes. Each set's pictures per second and
 * GPU-timed frames (one picture in two, timed with glFinish: see gpu_gl.c's
 * "timed frame") are summed, and "bench" lines in the log give the averages
 * every few rounds and at the end. The first vblanks after a switch don't
 * count: a resolution or widescreen change redraws every target. bench=1
 * runs at 30 fps (interp=0); bench=2 keeps interp=. The sets choose
 * widescreen themselves. The game itself runs as always: only the picture
 * changes. */
#include "vita.h"

#include <stdio.h>

#define BENCH_START 600
#define BENCH_SLOT 60
#define BENCH_SETTLE 12
#define BENCH_CYCLES 12

typedef struct {
  const char *name;
  int ws, res, filter, persp, subpixel, cache;
} BenchSet;

/* grouped by widescreen and resolution, so those (which redraw every
 * target) change four times a round */
static const BenchSet k_sets[] = {
    {"4:3 native, all off, tex_cache=0", 0, 0, 0, 0, 0, 0},
    {"4:3 native, all off (tex_cache=1)", 0, 0, 0, 0, 0, 1},
    {"4:3 native, tex_filter=1", 0, 0, 1, 0, 0, 1},
    {"4:3 native, tex_filter=2", 0, 0, 2, 0, 0, 1},
    {"4:3 native, persp=1", 0, 0, 0, 1, 0, 1},
    {"4:3 native, subpixel=1", 0, 0, 0, 0, 1, 1},
    {"4:3 native, tex_filter=1 persp subpixel", 0, 0, 1, 1, 1, 1},
    {"4:3 native, tex_filter=2 persp subpixel", 0, 0, 2, 1, 1, 1},
    {"4:3 ps1, all off, tex_cache=0", 0, 1, 0, 0, 0, 0},
    {"4:3 ps1, all off (tex_cache=1)", 0, 1, 0, 0, 0, 1},
    {"4:3 ps1, tex_filter=1 persp subpixel", 0, 1, 1, 1, 1, 1},
    {"16:9 native, all off, tex_cache=0", 1, 0, 0, 0, 0, 0},
    {"16:9 native, all off (tex_cache=1)", 1, 0, 0, 0, 0, 1},
    {"16:9 native, tex_filter=1 persp subpixel", 1, 0, 1, 1, 1, 1},
    {"16:9 ps1, all off, tex_cache=0", 1, 1, 0, 0, 0, 0},
    {"16:9 ps1, all off (tex_cache=1)", 1, 1, 0, 0, 0, 1},
};
#define NSETS ((int)(sizeof k_sets / sizeof k_sets[0]))
#define BENCH_END (BENCH_START + NSETS * BENCH_SLOT * BENCH_CYCLES)

static struct {
  unsigned vblanks, pictures, samples;
  uint64_t frame_us, tail_us;
  unsigned tail_max_us;
} s_acc[NSETS];
static int s_cur = -1, s_counting, s_odd;
static BenchSet s_orig;

static void report(const char *when) {
  int i;
  for (i = 0; i < NSETS; i++) {
    unsigned v = s_acc[i].vblanks ? s_acc[i].vblanks : 1, n = s_acc[i].samples ? s_acc[i].samples : 1;
    unsigned fps10 = s_acc[i].pictures * 600u / v;
    vita_log("bench %s: %-42s %2u.%u pictures/s; timed frame %3u.%u ms, GPU tail %3u.%u ms "
             "(worst %u ms), %u frames timed",
             when, k_sets[i].name, fps10 / 10, fps10 % 10,
             (unsigned)(s_acc[i].frame_us / n / 1000), (unsigned)(s_acc[i].frame_us / n % 1000 / 100),
             (unsigned)(s_acc[i].tail_us / n / 1000), (unsigned)(s_acc[i].tail_us / n % 1000 / 100),
             s_acc[i].tail_max_us / 1000, s_acc[i].samples);
  }
}

static void apply(const BenchSet *b) {
  if (b->ws != g_vita_opts.widescreen) {
    g_vita_opts.widescreen = b->ws;
    vita_present_apply();
    vita_gpu_rescale(); /* targets at the new width */
  }
  vita_gpu_set_options(b->res, b->filter, b->persp, b->subpixel, b->cache);
}

void vita_bench_vblank(int v) {
  int slot, set;
  if (!g_vita_opts.bench || !g_vita_gl || v < BENCH_START)
    return;
  if (v >= BENCH_END) {
    if (s_cur >= 0) {
      report("final");
      apply(&s_orig);
      vita_log("bench: done; the configured options are back");
      s_cur = -1;
      g_vita_opts.bench = 0;
    }
    return;
  }
  slot = (v - BENCH_START) / BENCH_SLOT;
  set = slot % NSETS;
  if (set != s_cur) {
    if (s_cur < 0) {
      s_orig.ws = g_vita_opts.widescreen;
      s_orig.res = g_vita_opts.gpu_res;
      s_orig.filter = g_vita_opts.tex_filter;
      s_orig.persp = g_vita_opts.persp;
      s_orig.subpixel = g_vita_opts.subpixel;
      s_orig.cache = g_vita_opts.tex_cache;
      vita_log("bench: %d option sets, %d vblanks each, %d rounds, vblanks %d-%d", NSETS,
               BENCH_SLOT, BENCH_CYCLES, BENCH_START, BENCH_END);
    } else if (set == 0 && slot % (NSETS * 4) == 0) {
      char when[24];
      snprintf(when, sizeof when, "round %d", slot / NSETS);
      report(when);
    }
    s_cur = set;
    apply(&k_sets[set]);
  }
  s_counting = (v - BENCH_START) % BENCH_SLOT >= BENCH_SETTLE;
  if (s_counting)
    s_acc[s_cur].vblanks++;
}

/* after each present; shown = a new picture */
void vita_bench_present(int shown) {
  unsigned f, t;
  if (s_cur < 0)
    return;
  if (vita_gpu_take_sample(&f, &t) && s_counting) {
    s_acc[s_cur].samples++;
    s_acc[s_cur].frame_us += f;
    s_acc[s_cur].tail_us += t;
    if (t > s_acc[s_cur].tail_max_us)
      s_acc[s_cur].tail_max_us = t;
  }
  if (shown && s_counting)
    s_acc[s_cur].pictures++;
  if (shown && (s_odd ^= 1))
    vita_gpu_arm_sample();
}

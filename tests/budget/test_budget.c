/* interp_budget_us's hold (src/game/budget.c, decisions D68, D74), driven
 * with costs from the hardware logs instead of a game: the game thread's
 * busy time and logic pass per tick, the render thread's time per picture
 * as the Vita measures it (an average over ~8 pictures, gpu_thread.c), and
 * the GPU, which shows only at 60 fps (late pictures: the game waited for
 * the previous one). 12% noise per tick, and optionally one spike every 5 s
 * (a picture or a game-thread tick far over the rest).
 *
 * Each case says what must happen: 60 fps where it fits, coming back from a
 * hold within a few seconds; a steady 30 where it doesn't. Prints a table
 * either way. */
#include "../../src/game/budget.h"

#include <stdio.h>
#include <string.h>

#define RT_TICK 33367 /* us: a tick, two NTSC vblanks */
#define MINUTE 1800   /* ticks */

static unsigned s_rng;

static double uniform(void) {
  s_rng = s_rng * 1664525u + 1013904223u;
  return (s_rng >> 8) / 16777216.0;
}

/* about N(1, sd): a sum of four uniforms */
static double noise(double sd) {
  return 1.0 + sd * (uniform() + uniform() + uniform() + uniform() - 2.0) * 1.7320508;
}

typedef struct {
  int ticks;
  long long b;     /* budget, us */
  double busy;     /* game thread per tick without the in-between frame, us */
  double logic;    /* its logic pass, us (the in-between pass costs 0.95 of it) */
  double pic;      /* render thread per full picture, us (an in-between one: 0.9) */
  double gpu;      /* GPU per picture, us: at 60, two over a tick make a late picture */
  int spike_every; /* ticks; 0: none */
  double spike_pic, spike_cpu; /* us added to one picture, one tick */
} Phase;

typedef struct {
  int ticks, interp, falls, first_60; /* first_60: ticks into the phase, -1 never */
} Result;

typedef struct {
  Budget budget;
  BudgetStats st;
  double ema; /* the render thread's time per picture, averaged as gpu_thread.c does */
  unsigned late;
  int prev_interp;
  long long interp_cost;
} Sim;

static void picture(Sim *s, double d) { s->ema = (s->ema * 7 + d) / 8; }

/* Runs one phase. The tick's costs are drawn first; the decision sees the
 * last tick's, as present.c's does. */
static Result run(Sim *s, const Phase *p) {
  Result r = {0, 0, 0, -1};
  long long busy = (long long)p->busy, logic = (long long)p->logic;
  int i, holds0 = (int)s->st.holds;
  for (i = 0; i < p->ticks; i++) {
    BudgetTick in;
    int spike = p->spike_every && i % p->spike_every == p->spike_every / 2;
    int interp;
    in.b = p->b;
    in.cpu = busy + (s->prev_interp ? s->interp_cost : logic);
    in.gl = (long long)(2 * s->ema);
    in.has_late = 1;
    in.late = s->late;
    in.prev_interp = s->prev_interp;
    in.busy = busy;
    in.logic = logic;
    interp = budget_tick(&s->budget, &in, &s->st);
    /* this tick */
    busy = (long long)(p->busy * noise(0.12) + (spike ? p->spike_cpu : 0));
    logic = (long long)(p->logic * noise(0.12));
    if (interp) {
      s->interp_cost = (long long)(logic * 0.95);
      picture(s, p->pic * 0.9 * noise(0.12));
      if (2 * p->gpu * noise(0.05) > RT_TICK || busy + s->interp_cost > RT_TICK)
        s->late++;
      r.interp++;
      if (r.first_60 < 0)
        r.first_60 = i;
    }
    picture(s, p->pic * noise(0.12) + (spike ? p->spike_pic : 0));
    s->prev_interp = interp;
    r.ticks++;
  }
  r.falls = (int)s->st.holds - holds0;
  return r;
}

static void sim_init(Sim *s, unsigned seed) {
  memset(s, 0, sizeof *s);
  budget_init(&s->budget, 0);
  s->ema = 10000;
  s_rng = seed;
}

/* Heavy stretches 2 s apart, as around the hardware log's first falls:
 * each fall within 10 s of the last doubles the hold, so it ends at 240
 * ticks (8 s), the hold the log never came back from. */
static void lead_in(Sim *s, long long b) {
  Phase heavy = {60, b, 14000, 5000, 18000, 20000, 0, 0, 0};
  Phase light = {60, b, 11800, 4000, 11000, 9000, 0, 0, 0};
  int i;
  for (i = 0; i < 4; i++) {
    run(s, &heavy);
    run(s, &light);
  }
  run(s, &heavy);
  printf("  (lead-in: %lld falls, hold %d ticks)\n", s->st.holds, s->st.hold_len);
}

static int s_fail;

static void check(const char *name, int ok, const char *what) {
  printf("  %-44s %s: %s\n", name, ok ? "ok  " : "FAIL", what);
  s_fail += !ok;
}

static void show(const char *name, const Result *r) {
  printf("%-46s %5d ticks, %5.1f%% at 60, %3d falls, first 60 after %d ticks\n", name, r->ticks,
         100.0 * r->interp / r->ticks, r->falls, r->first_60);
}

int main(void) {
  /* The hardware log (main 2a0691c, Toasty): at 30 fps, held, the game
   * thread busy ~11.8 ms a tick with its joins, the logic pass ~4 ms, the
   * render thread ~11 ms a picture; the same place at budget 0 ran a clean
   * 60 at 8.2 ms a picture. */
  static const Phase toasty = {MINUTE, 26000, 11800, 4000, 11000, 9000, 0, 0, 0};
  static const Phase toasty_pic = {MINUTE, 26000, 11800, 4000, 11000, 9000, 150, 60000, 0};
  static const Phase toasty_cpu = {MINUTE, 26000, 11800, 4000, 11000, 9000, 150, 0, 60000};
  static const Phase toasty_30 = {MINUTE, 30000, 11800, 4000, 11000, 9000, 150, 60000, 60000};
  /* where 60 doesn't fit: the render thread (two pictures over 31.3 ms),
   * the GPU (only seen at 60), the game thread */
  static const Phase no_render = {MINUTE, 26000, 11800, 4000, 16500, 12000, 150, 60000, 0};
  static const Phase no_gpu = {MINUTE, 26000, 11800, 4000, 11000, 20000, 150, 60000, 0};
  static const Phase no_cpu = {MINUTE, 26000, 20000, 8000, 9000, 9000, 150, 0, 60000};
  /* Artisans at 60 (C1): light */
  static const Phase artisans = {MINUTE, 26000, 7000, 3000, 9000, 7000, 150, 60000, 0};
  /* marginal: the game thread's cost with the in-between frame ~22 ms of 26,
   * over it on a tick now and then */
  static const Phase marginal = {MINUTE, 26000, 15500, 7000, 9000, 9000, 0, 0, 0};
  Sim s;
  Result r;

  printf("budget: one minute per case after the lead-in; spikes every 5 s\n");

  sim_init(&s, 1);
  r = run(&s, &artisans);
  show("artisans", &r);
  check("artisans", r.interp > r.ticks * 95 / 100 && r.falls <= 1, "60 throughout");

  sim_init(&s, 2);
  lead_in(&s, 26000);
  r = run(&s, &toasty);
  show("held 8 s, then toasty (no spikes)", &r);
  check("toasty", r.first_60 >= 0 && r.first_60 <= 300 && r.interp > r.ticks * 80 / 100,
        "back to 60 within 10 s");

  sim_init(&s, 3);
  lead_in(&s, 26000);
  r = run(&s, &toasty_pic);
  show("held 8 s, then toasty, a slow picture every 5 s", &r);
  check("toasty_pic", r.first_60 >= 0 && r.first_60 <= 300 && r.interp > r.ticks * 80 / 100,
        "back to 60 within 10 s");

  sim_init(&s, 4);
  lead_in(&s, 26000);
  r = run(&s, &toasty_cpu);
  show("held 8 s, then toasty, a slow tick every 5 s", &r);
  check("toasty_cpu", r.first_60 >= 0 && r.first_60 <= 300 && r.interp > r.ticks * 80 / 100,
        "back to 60 within 10 s");

  sim_init(&s, 5);
  lead_in(&s, 26000);
  r = run(&s, &toasty_30);
  show("held 8 s, then budget 30 (the menu), both spikes", &r);
  check("toasty_30", r.first_60 >= 0 && r.first_60 <= 60 && r.interp > r.ticks * 80 / 100,
        "a new budget starts again: 60 at once");

  sim_init(&s, 6);
  r = run(&s, &no_render);
  show("render thread over", &r);
  check("no_render", r.interp < r.ticks * 3 / 100 && r.falls <= 2, "a steady 30");

  sim_init(&s, 7);
  run(&s, &no_gpu);
  r = run(&s, &no_gpu);
  show("GPU over (seen only at 60), the second minute", &r);
  check("no_gpu", r.interp < r.ticks * 3 / 100 && r.falls <= 8,
        "30, a look at 60 at most every 8 s");

  sim_init(&s, 8);
  r = run(&s, &no_cpu);
  show("game thread over", &r);
  check("no_cpu", r.interp < r.ticks * 3 / 100 && r.falls <= 2, "a steady 30");

  sim_init(&s, 9);
  r = run(&s, &marginal);
  show("marginal game thread", &r);
  check("marginal", r.falls <= 10, "a few changes a minute, not every tick");

  printf("budget: %s\n", s_fail ? "FAILED" : "all cases as expected");
  return s_fail != 0;
}

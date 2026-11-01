/* interp_budget_us with hysteresis (D68, D74). Deciding each tick alone
 * flipped between 60 and 30 fps where the in-between frame only sometimes
 * fitted, which looks worse than a steady 30 (D44). A miss holds 30 fps for
 * at least a whole hold (1 s), until the costs over the last hold's worth of
 * ticks have fitted with an eighth to spare; a miss within 10 s of coming
 * back doubles the hold, up to 8 s.
 * A miss is any of:
 * - the game thread: its CPU time per tick plus the in-between frame's,
 *   averaged over a few ticks, over the budget; or over it on two ticks in
 *   a row (a jump the average hasn't caught up with);
 * - the render thread (the platform's, on its own core): two pictures over
 *   15/16 of the tick, on 3 ticks in a row. It has the whole tick, not the
 *   game thread's budget;
 * - 3 late pictures within about half a second while the in-between frame
 *   ran (the GPU behind).
 * One slow tick or picture on its own is noise and changes nothing (D74): a
 * tick counts at most 11/8 of the line in the averages, so it can't take an
 * average with an eighth to spare over the line, and the render thread's
 * time (an average over ~8 pictures already) must stay over for 3 ticks.
 * On hardware such a tick came every few seconds; each one made a fall at
 * 60 and, held, started the whole hold again, so at 8 s the hold never let
 * go (main 2a0691c in Toasty).
 * While held, the in-between frame's cost is taken as this tick's logic pass
 * (present.c: it draws the same scene, 0.85-1.0 of it on hardware), not the
 * last one measured, which may be from a heavier spot and would never let
 * go; and the render thread's two pictures as twice the full one, about
 * what both cost at 60 (C1 against D68's log). The hold judges the average
 * over its ticks, so a slow tick in it weighs as one tick of hold_len; a
 * tick over the miss line only keeps it from ending on that tick.
 * A new budget (the menu) starts again: at 60, the shortest hold. */
#include "budget.h"

#define HOLD_MIN 30  /* ticks: 1 s at 30 fps */
#define HOLD_MAX BUDGET_HOLD_MAX
#define STEADY 300   /* ticks at 60 fps after which a hold is HOLD_MIN again */
#define LATE_WEIGHT 30 /* a late picture's score; a tick takes 1 off */
#define LATE_MISS 60   /* score above which late pictures count as a miss */
#define RT_TICK 33367  /* us: a tick, two NTSC vblanks */
#define RT_TICKS 3     /* ticks in a row the render thread must be over */

void budget_init(Budget *s, unsigned late) {
  s->b = 0;
  s->held = 0;
  s->n = 0;
  s->hold_len = HOLD_MIN;
  s->since_back = STEADY;
  s->late_score = 0;
  s->cpu_over = 0;
  s->gl_over = 0;
  s->cpu_avg = 0;
  s->sum_cpu = s->sum_gl = 0;
  s->late_seen = late;
}

static long long at_most(long long v, long long max) { return v < max ? v : max; }

int budget_tick(Budget *s, const BudgetTick *in, BudgetStats *st) {
  long long b = in->b, cpu = in->cpu, gl = in->gl;
  long long cpu_c, gl_c; /* this tick's share of the averages */
  int over_avg, over_twice, over_gl, over, late = 0, i;
  if (b != s->b) {
    budget_init(s, in->late);
    s->b = b;
  }
  cpu_c = at_most(cpu, b * 11 / 8);
  gl_c = at_most(gl, RT_TICK * 11 / 8);
  s->cpu_avg = s->cpu_avg ? (s->cpu_avg * 3 + cpu_c) / 4 : cpu_c;
  if (in->has_late) {
    if (in->prev_interp)
      s->late_score += (int)(in->late - s->late_seen) * LATE_WEIGHT;
    s->late_seen = in->late;
  }
  if (s->late_score > LATE_MISS) {
    late = 1;
    s->late_score = 0;
  } else if (s->late_score > 0) {
    s->late_score--;
  }
  over_avg = s->cpu_avg > b;
  over_twice = cpu > b && s->cpu_over;
  s->cpu_over = cpu > b;
  s->gl_over = gl * 16 > RT_TICK * 15 ? s->gl_over + 1 : 0;
  over_gl = s->gl_over >= RT_TICKS;
  over = over_avg || over_twice || over_gl;
  if (!s->held) {
    if (!over && !late) {
      if (s->since_back < STEADY && ++s->since_back == STEADY)
        s->hold_len = HOLD_MIN;
      st->hold = 0, st->hold_len = s->hold_len;
      return 1;
    }
    st->why[BUDGET_FALL_CPU_AVG] += over_avg;
    st->why[BUDGET_FALL_CPU_TWICE] += over_twice;
    st->why[BUDGET_FALL_RENDER] += over_gl;
    st->why[BUDGET_FALL_LATE] += late;
    if (s->since_back < STEADY)
      s->hold_len = s->hold_len * 2 > HOLD_MAX ? HOLD_MAX : s->hold_len * 2;
    s->held = 1;
    s->n = 0;
    s->sum_cpu = s->sum_gl = 0;
    s->since_back = 0;
    s->late_score = 0;
    st->holds++;
    st->hold = s->hold_len, st->hold_len = s->hold_len;
    return 0;
  }
  /* the last hold_len held ticks */
  i = s->n % s->hold_len;
  if (s->n >= s->hold_len) {
    s->sum_cpu -= s->win_cpu[i];
    s->sum_gl -= s->win_gl[i];
  }
  s->win_cpu[i] = (int)cpu_c;
  s->win_gl[i] = (int)gl_c;
  s->sum_cpu += cpu_c;
  s->sum_gl += gl_c;
  s->n++;
  st->held++;
  st->cpu += cpu;
  st->cpu_avg += s->cpu_avg;
  st->gl += gl;
  st->busy += in->busy;
  st->logic += in->logic;
  if (cpu > st->cpu_max)
    st->cpu_max = cpu;
  if (gl > st->gl_max)
    st->gl_max = gl;
  over_gl = gl * 16 > RT_TICK * 15; /* held: this tick alone */
  if (over_avg || over_twice || over_gl) {
    st->why[BUDGET_AGAIN_CPU_AVG] += over_avg;
    st->why[BUDGET_AGAIN_CPU_TWICE] += over_twice;
    st->why[BUDGET_AGAIN_RENDER] += over_gl;
  } else if (s->cpu_avg * 8 > b * 7 || gl * 8 > RT_TICK * 7) {
    st->why[BUDGET_PAUSE_CPU] += s->cpu_avg * 8 > b * 7;
    st->why[BUDGET_PAUSE_RENDER] += gl * 8 > RT_TICK * 7;
  } else {
    st->why[BUDGET_COUNTED]++;
  }
  st->hold = s->n < s->hold_len ? s->hold_len - s->n : 0, st->hold_len = s->hold_len;
  /* a whole hold, with an eighth to spare on average, and not over now */
  if (s->n >= s->hold_len && !over_avg && !over_gl && s->sum_cpu * 8 <= b * 7 * s->hold_len &&
      s->sum_gl * 8 <= (long long)RT_TICK * 7 * s->hold_len) {
    s->held = 0;
    st->hold = 0;
    return 1;
  }
  return 0;
}

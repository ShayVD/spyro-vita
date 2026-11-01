/* interp_budget_us's decision (decisions D68, D74): whether a tick may have
 * its in-between frame, with a hold at 30 fps after a miss. present.c feeds
 * it each tick's costs; no globals, so tests/budget can drive it with costs
 * from hardware logs. */
#ifndef SPYRO_BUDGET_H
#define SPYRO_BUDGET_H

#ifdef __cplusplus
extern "C" {
#endif

/* BudgetStats.why: a fall to the held 30 (the tests that were over), then
 * on a held tick: over the miss line (render: on that tick), fitting without
 * an eighth to spare, or fitting with it */
enum {
  BUDGET_FALL_CPU_AVG, BUDGET_FALL_CPU_TWICE, BUDGET_FALL_RENDER, BUDGET_FALL_LATE,
  BUDGET_AGAIN_CPU_AVG, BUDGET_AGAIN_CPU_TWICE, BUDGET_AGAIN_RENDER,
  BUDGET_PAUSE_CPU, BUDGET_PAUSE_RENDER, BUDGET_COUNTED,
  BUDGET_WHYS
};

/* Counted up, never reset: the platform's report takes differences, and
 * zeroes the two maxima after reading them. */
typedef struct {
  long long holds; /* falls to a held 30 */
  long long why[BUDGET_WHYS];
  long long held; /* ticks with the hold on */
  long long cpu, cpu_avg, gl, busy, logic; /* the held ticks' costs, summed (us) */
  long long cpu_max, gl_max;
  int hold, hold_len; /* the hold's ticks left (at least) and length after the last tick */
} BudgetStats;

/* One tick's inputs (us). */
typedef struct {
  long long b;    /* interp_budget_us, > 0 */
  long long cpu;  /* the game thread's CPU time for a tick with the in-between frame */
  long long gl;   /* the render thread's two pictures; 0 without one */
  int has_late;   /* late is valid */
  unsigned late;  /* the platform's late pictures so far */
  int prev_interp; /* the last tick had its in-between frame */
  long long busy, logic; /* cpu's parts, for the stats only */
} BudgetTick;

#define BUDGET_HOLD_MAX 240 /* ticks: 8 s */

typedef struct {
  long long b; /* the budget this is for: another starts again */
  int held;    /* holding 30 fps */
  int n;       /* ticks into the hold */
  int hold_len, since_back, late_score;
  int cpu_over; /* the last tick's CPU time was over the budget */
  int gl_over;  /* ticks in a row the render thread was over its line */
  long long cpu_avg; /* CPU time per tick with the in-between frame, over ~4 ticks */
  unsigned late_seen;
  long long sum_cpu, sum_gl; /* over the hold's last hold_len ticks: */
  int win_cpu[BUDGET_HOLD_MAX], win_gl[BUDGET_HOLD_MAX];
} Budget;

/* Start again: at 60 fps, the shortest hold. late: the platform's count now.
 * budget_tick does it too when the budget changes. */
void budget_init(Budget *s, unsigned late);
/* 1 if this tick may have its in-between frame. */
int budget_tick(Budget *s, const BudgetTick *in, BudgetStats *st);

#ifdef __cplusplus
}
#endif

#endif

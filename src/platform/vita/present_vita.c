/* Widescreen and 60 fps on the Vita (docs/presentation.md, decisions D30):
 * config.txt keys into src/game/present.c's switches, the platform clock for
 * interp_budget, and a log line with every stats report. The pictures are
 * shown by video.c (gpu=0) and gpu_gl.c (gpu=1, which also captures them on
 * the GPU). */
#include "vita.h"
#include "../../game/present.h"
#include "host.h" /* linux32's, for png.c's shots */
#include "menu_opts.h"

#include <psp2/kernel/processmgr.h>

static long long vita_clock_us(void) { return (long long)sceKernelGetProcessTimeWide(); }

/* Also from the menu, between ticks or inside the game's VSync wait:
 * present.c takes new switches at its next tick. */
void vita_present_apply(void) {
  g_present.widescreen = g_vita_opts.widescreen != 0;
  g_present.ws_exact = g_vita_opts.widescreen_exact != 0;
  g_present.interp = g_vita_opts.interp != 0;
  g_present.interp_mobys = g_vita_opts.interp == 1 || g_vita_opts.interp == 2;
  g_present.interp_anim = g_vita_opts.interp == 1;
  g_present.interp_budget_us = g_vita_opts.interp_budget * 1000;
}

/* Widescreen from the menu: the GPU's targets again at the new width. */
static void widescreen_changed(void) {
  vita_present_apply();
  if (g_vita_gpu)
    vita_gpu_rescale();
}

/* The menu's rows (menu.c, D45). The 60 fps values are short enough to fit
 * beside the label when selected ("< NO ANIM >"); 60 fps is off by default
 * (D44). */
static const char *const k_interp_names[] = {"OFF", "ON", "NO ANIM", "CAMERA"};
static const int k_budgets[] = {0, 20, 23, 26, 30}; /* ms; 0: never skip */
static const char *const k_budget_names[] = {"NEVER SKIP", "20 MS", "23 MS", "26 MS", "30 MS"};
static const MenuOption k_rows[] = {
    {.section = MENU_PICTURE, .label = "WIDESCREEN", .key = "widescreen",
     .value = &g_vita_opts.widescreen, .names = g_menu_off_on, .count = 2,
     .on_change = widescreen_changed},
    {.section = MENU_PICTURE, .label = "60 FPS (EXPERIMENTAL)", .key = "interp",
     .value = &g_vita_opts.interp, .names = k_interp_names, .count = 4,
     .on_change = vita_present_apply},
    {.section = MENU_PICTURE, .label = "60 FPS BUDGET", .key = "interp_budget",
     .value = &g_vita_opts.interp_budget, .names = k_budget_names, .values = k_budgets,
     .count = 5, .fmt = "%d MS", .on_change = vita_present_apply},
};
MENU_OPTION_LIST(g_menu_present, k_rows);

void vita_present_init(void) {
  vita_present_apply();
  g_present.stats = g_vita_opts.stats ? 1 : 0;
  g_present_clock_us = vita_clock_us;
  if (g_vita_gpu) {
    g_present_capture = vita_gpu_present_capture();
    g_present_render_us = vita_gpu_render_cost(); /* the render thread's share (D63) */
    g_present_late = vita_gpu_late_pictures();     /* and its late pictures (D68) */
  }
  else
    g_host_opts.out960 = 2; /* shots: the presentation frame on screen, 512x240 */
  vita_log("present: widescreen %d, interp %d (budget %d ms), %s capture; "
           "hold SELECT and press START for the options menu",
           g_vita_opts.widescreen, g_vita_opts.interp, g_vita_opts.interp_budget,
           g_present_capture ? "GPU" : "CPU");
}

/* One line per stats report, totals since the last one. */
void vita_present_report(int vblank) {
  static PresentStats last;
  const PresentStats *s = &g_present_stats;
  long long ticks = s->ticks - last.ticks;
  if (!port_present_active() || ticks <= 0) {
    last = *s; /* windows with both off aren't reported, nor carried over */
    return;
  }
  vita_log("present %d: %lld ticks; per tick us: logic draw %lld, passes %lld, snapshot %lld, "
           "blend %lld; %lld in-between frames, %lld skipped for the budget (%lld falls to a held "
           "30 fps), %lld otherwise; "
           "%lld pictures from the 4:3 display",
           vblank, ticks, (s->us_logic - last.us_logic) / ticks,
           (s->us_pass - last.us_pass) / ticks, (s->us_snapshot - last.us_snapshot) / ticks,
           (s->us_blend - last.us_blend) / ticks, s->interp_frames - last.interp_frames,
           s->budget_skips - last.budget_skips, s->budget.holds - last.budget.holds,
           s->skipped_interp - last.skipped_interp,
           s->display_fallbacks - last.display_fallbacks);
  if (g_present.interp_budget_us || s->budget.held != last.budget.held ||
      s->budget.holds != last.budget.holds) {
    const BudgetStats *u = &s->budget, *l = &last.budget;
    long long held = u->held - l->held, n = held > 0 ? held : 1;
    vita_log("budget %d: %d ms; %lld ticks held, hold %d of %d at the last; held again by cpu avg "
             "%lld, cpu twice %lld, render %lld; paused by cpu %lld, render %lld; counted down "
             "%lld; falls by cpu avg %lld, cpu twice %lld, render %lld, late %lld; held ticks' us: "
             "cpu %lld (most %lld), cpu avg %lld, busy %lld, logic %lld, render x2 %lld (most %lld)",
             vblank, g_present.interp_budget_us / 1000, held, u->hold, u->hold_len,
             u->why[BUDGET_AGAIN_CPU_AVG] - l->why[BUDGET_AGAIN_CPU_AVG],
             u->why[BUDGET_AGAIN_CPU_TWICE] - l->why[BUDGET_AGAIN_CPU_TWICE],
             u->why[BUDGET_AGAIN_RENDER] - l->why[BUDGET_AGAIN_RENDER],
             u->why[BUDGET_PAUSE_CPU] - l->why[BUDGET_PAUSE_CPU],
             u->why[BUDGET_PAUSE_RENDER] - l->why[BUDGET_PAUSE_RENDER],
             u->why[BUDGET_COUNTED] - l->why[BUDGET_COUNTED],
             u->why[BUDGET_FALL_CPU_AVG] - l->why[BUDGET_FALL_CPU_AVG],
             u->why[BUDGET_FALL_CPU_TWICE] - l->why[BUDGET_FALL_CPU_TWICE],
             u->why[BUDGET_FALL_RENDER] - l->why[BUDGET_FALL_RENDER],
             u->why[BUDGET_FALL_LATE] - l->why[BUDGET_FALL_LATE], (u->cpu - l->cpu) / n,
             u->cpu_max, (u->cpu_avg - l->cpu_avg) / n, (u->busy - l->busy) / n,
             (u->logic - l->logic) / n, (u->gl - l->gl) / n, u->gl_max);
    g_present_stats.budget.cpu_max = g_present_stats.budget.gl_max = 0;
  }
  last = *s;
}

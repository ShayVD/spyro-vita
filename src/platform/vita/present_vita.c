/* Widescreen and 60 fps on the Vita (docs/presentation.md, decisions D30):
 * config.txt keys into src/game/present.c's switches, the platform clock for
 * the game thread's safety net (interp_budget), and a log line with every
 * stats report. The pictures are
 * shown by video.c (gpu=0) and gpu_gl.c (gpu=1, which also captures them on
 * the GPU). */
#include "vita.h"
#include "../../game/present.h"
#include "../../game/tick60.h"
#include "host.h" /* linux32's, for png.c's shots */
#include "menu_opts.h"

#include <psp2/kernel/processmgr.h>
#include <stdio.h>

static long long vita_clock_us(void) { return (long long)sceKernelGetProcessTimeWide(); }

/* interp_budget (D75): the safety net that skips the in-between frame only
 * if the game thread itself would run late: 15/16 of a tick (two NTSC
 * vblanks, 33.37 ms), whatever the value, or 0 to turn it off. The GPU never
 * makes 60 fps fall to 30: dynres lowers the resolution instead. */
#define NET_US 31281

/* Also from the menu, between ticks or inside the game's VSync wait:
 * present.c takes new switches at its next tick. */
void vita_present_apply(void) {
  g_present.widescreen = g_vita_opts.widescreen != 0;
  g_present.ws_exact = g_vita_opts.widescreen_exact != 0;
  /* 4, 60 Hz logic (D130): in play every frame is a real one; the other
   * gamestates and the attract demos run at 30 Hz with the in-between frame */
  g_present.interp = g_vita_opts.interp != 0;
  g_present.interp_mobys = g_vita_opts.interp == 1 || g_vita_opts.interp == 2 || g_vita_opts.interp == 4;
  g_present.interp_anim = g_vita_opts.interp == 1 || g_vita_opts.interp == 4;
  g_present.interp_budget_us = g_vita_opts.interp_budget ? NET_US : 0;
}

/* Widescreen from the menu: the GPU's targets again at the new width. */
static void widescreen_changed(void) {
  vita_present_apply();
  if (g_vita_gpu)
    vita_gpu_rescale();
}

/* 60 Hz logic is read once, at start (D130): a change of the 60 FPS row
 * into or out of 60 HZ asks whether to restart now (RESTART NOW), or puts
 * the row back (UNDO); O leaves it for the next start, and the 60 HZ LOGIC
 * row says so while it waits (D136). */
static int s_tick60_now;  /* this run's logic: 60 Hz (1) or 30 Hz (0) */
static int s_interp_last; /* the 60 FPS row's value before the last change */
static int s_interp_undo; /* UNDO's value: the row's before the restart was pending */
static const char *const k_interp_names[] = {"OFF", "ON", "NO ANIM", "CAMERA", "60 HZ"};
static char s_undo_hint[24];
static const char *s_pending_text[1];

static int logic60(int interp) { return interp == 4 && tick60_built(); }
static int restart_pending(void) { return logic60(g_vita_opts.interp) != s_tick60_now; }

static void restart_answer(int answer);
static const VitaMenuAsk k_ask_on = {
    .title = "60 HZ LOGIC",
    .text = {"60 HZ LOGIC BEGINS WHEN THE GAME", "STARTS AGAIN. A RESTART LOSES", "PROGRESS SINCE THE LAST SAVE."},
    .choice = {"RESTART NOW", "UNDO"}, .hint = {"60 HZ", s_undo_hint}, .nchoice = 2,
    .footer = "UP/DOWN: CHOOSE  X: OK  O: AT NEXT START", .done = restart_answer};
static const VitaMenuAsk k_ask_off = {
    .title = "60 HZ LOGIC",
    .text = {"60 HZ LOGIC STOPS WHEN THE GAME", "STARTS AGAIN. A RESTART LOSES", "PROGRESS SINCE THE LAST SAVE."},
    .choice = {"RESTART NOW", "UNDO"}, .hint = {"30 HZ", s_undo_hint}, .nchoice = 2,
    .footer = "UP/DOWN: CHOOSE  X: OK  O: AT NEXT START", .done = restart_answer};

static void restart_ask(void) {
  snprintf(s_undo_hint, sizeof s_undo_hint, "BACK TO %s", k_interp_names[s_interp_undo]);
  vita_menu_ask(s_tick60_now ? &k_ask_off : &k_ask_on);
}

static void restart_answer(int answer) {
  if (answer == 0) {
    vita_log("menu: interp=%d: restarting for %s Hz logic", g_vita_opts.interp,
             logic60(g_vita_opts.interp) ? "60" : "30");
    vita_menu_restart(); /* doesn't return */
  } else if (answer == 1) {
    g_vita_opts.interp = s_interp_last = s_interp_undo;
    vita_present_apply();
    vita_log("menu: 60 fps back to %s (interp=%d)", k_interp_names[s_interp_undo], s_interp_undo);
  } else
    vita_log("menu: interp=%d: %s Hz logic at the next start", g_vita_opts.interp,
             logic60(g_vita_opts.interp) ? "60" : "30");
}

/* The 60 FPS row changed: applied (what changes without a restart), then
 * the question if this change made a restart pending. */
static void interp_changed(void) {
  const int last = s_interp_last;
  s_interp_last = g_vita_opts.interp;
  vita_present_apply();
  if (restart_pending() && logic60(last) == s_tick60_now) {
    s_interp_undo = last;
    restart_ask();
  }
}

/* The 60 HZ LOGIC row: only while a restart is pending; X asks again. */
static int pending_visible(void) {
  s_pending_text[0] = logic60(g_vita_opts.interp) ? "ON AT RESTART" : "OFF AT RESTART";
  return restart_pending();
}
static int pending_action(void) {
  restart_ask();
  return 0;
}

/* The menu's rows (menu.c, D45). The 60 fps values are short enough to fit
 * beside the label when selected ("< NO ANIM >"); 60 fps is off by default
 * (D44). The 60 FPS BUDGET row went with D75: with 60 fps on, the
 * in-between frame always runs (interp_budget is config.txt's only). */
static const MenuOption k_rows[] = {
    {.section = MENU_PICTURE, .label = "WIDESCREEN", .key = "widescreen",
     .value = &g_vita_opts.widescreen, .names = g_menu_off_on, .count = 2,
     .on_change = widescreen_changed},
    {.section = MENU_PICTURE, .label = "60 FPS (EXPERIMENTAL)", .key = "interp",
     .value = &g_vita_opts.interp, .names = k_interp_names, .count = 5,
     .on_change = interp_changed},
    {.section = MENU_PICTURE, .label = "60 HZ LOGIC", .names = s_pending_text,
     .action = pending_action, .visible = pending_visible},
};
MENU_OPTION_LIST(g_menu_present, k_rows);

int pres_draw_available(void); /* src/game/pres/pres_draw.c */
const char *pres_snap_misplaced(void); /* src/game/pres/pres_snap.c (D128) */

void vita_present_init(void) {
  /* 60 Hz logic (D130) is read once, at start: its value in the menu takes
   * effect at the next start */
  g_tick60_mode = s_tick60_now = logic60(g_vita_opts.interp);
  s_interp_last = s_interp_undo = g_vita_opts.interp;
  if (g_vita_opts.interp == 4)
    vita_log("present: 60 Hz logic in play (interp=4, D130)%s",
             tick60_built() ? "; the other gamestates at 30 Hz with the in-between frame"
                            : ": not in this build, 30 Hz");
  vita_present_apply();
  g_present.stats = g_vita_opts.stats ? 1 : 0;
  g_present_clock_us = vita_clock_us;
  if (g_vita_gpu) {
    g_present_capture = vita_gpu_present_capture();
  }
  else
    g_host_opts.out960 = 2; /* shots: the presentation frame on screen, 512x240 */
  vita_log("present: widescreen %d, interp %d (the game thread's safety net %s), %s capture; "
           "hold SELECT and press START for the options menu",
           g_vita_opts.widescreen, g_vita_opts.interp,
           g_vita_opts.interp_budget ? "at 31.3 ms a tick" : "off",
           g_present_capture ? "GPU" : "CPU");
  if (pres_draw_available()) /* stages 7e-7g (D125, D126, D128, D129) */
    vita_log("present: the presenter draws the in-between frame and the attract demos' 16:9 "
             "frame");
  else
    vita_log("present: no presenter (not built, no area, fast_render not 3, or a label outside "
             "the data region: %s): no in-between frame, and the attract demos 4:3",
             pres_snap_misplaced() ? pres_snap_misplaced() : "none");
}

/* One line per stats report, totals since the last one. */
void vita_present_report(int vblank) {
  static PresentStats last;
  const PresentStats *s = &g_present_stats;
  long long ticks = s->ticks - last.ticks;
  char due[320];
  int has_due = vita_gpu_due_report(due, sizeof due); /* 7c (D112); its window too */
  if (!port_present_active() || ticks <= 0) {
    last = *s; /* windows with both off aren't reported, nor carried over */
    return;
  }
  vita_log("present %d: %lld ticks; per tick us: logic draw %lld, the presenter on this thread "
           "%lld, blend %lld; %lld in-between frames, %lld skipped for the budget (%lld falls to a held "
           "30 fps), %lld otherwise; "
           "%lld pictures from the 4:3 display%s%s",
           vblank, ticks, (s->us_logic - last.us_logic) / ticks,
           (s->us_pass - last.us_pass) / ticks,
           (s->us_blend - last.us_blend) / ticks, s->interp_frames - last.interp_frames,
           s->budget_skips - last.budget_skips, s->budget_holds - last.budget_holds,
           s->skipped_interp - last.skipped_interp,
           s->display_fallbacks - last.display_fallbacks, has_due ? "; " : "", has_due ? due : "");
  if (has_due && (s->due_late != last.due_late || s->due_superseded != last.due_superseded ||
                  s->queue_drops != last.queue_drops))
    vita_log("present %d: the game thread's frames by due vblank: %lld taken late, %lld never "
             "taken, %lld dropped from a full queue", vblank, s->due_late - last.due_late,
             s->due_superseded - last.due_superseded, s->queue_drops - last.queue_drops);
  last = *s;
}

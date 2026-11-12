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
#include "psx_region.h"
#include "psx_clock.h"

#include <psp2/kernel/processmgr.h>
#include <stdio.h>

static long long vita_clock_us(void) { return (long long)sceKernelGetProcessTimeWide(); }

/* interp_budget (D75): the safety net that skips the in-between frame only
 * if the game thread itself would run late: 15/16 of a tick (two NTSC
 * vblanks, 33.37 ms), whatever the value, or 0 to turn it off. The GPU never
 * makes 60 fps fall to 30: dynres lowers the resolution instead. */
#define NET_US 31281

/* Also from the menu, between ticks or inside the game's VSync wait:
 * present.c takes new switches at its next tick. The 60 FPS row's values
 * (D148): 0 OFF, 1 INTERPOLATED (the in-between frame: camera, Spyro, the
 * mobys, their animation, the particles), 4 60 HZ (60 Hz logic in play,
 * D130; the other gamestates and the attract demos run at 30 Hz with the
 * in-between frame). D44's diagnostic values 2 (no animation blending) and
 * 3 (camera only) went with D148: main.c reads them as 1, and the host keeps
 * them as --interp-no-anim and --interp-camera. */
void vita_present_apply(void) {
  g_present.widescreen = g_vita_opts.widescreen != 0;
  g_present.ws_exact = g_vita_opts.widescreen_exact != 0;
  g_present.interp = g_vita_opts.interp != 0;
  g_present.interp_mobys = g_present.interp_anim = g_present.interp;
  g_present.interp_budget_us = g_vita_opts.interp_budget ? NET_US : 0;
}

/* Widescreen from the menu: the GPU's targets again at the new width. */
static void widescreen_changed(void) {
  vita_present_apply();
  if (g_vita_gpu)
    vita_gpu_rescale();
}

/* Two settings are read once, at start, so a change in the menu asks
 * whether to restart now (RESTART NOW) or puts the row back (UNDO); O
 * leaves it for the next start, and a row says so while it waits (D136):
 *  - 60 Hz logic (D130), the 60 FPS row's 60 HZ: the US disc only (D148);
 *  - PAL 60 (D138), the PAL disc's PAL 60 row (D148): PAL's code with US
 *    timing, read before the disc opens. */
enum { ASK_T60, ASK_PAL60 };
static int s_tick60_now;  /* this run's logic: 60 Hz (1) or 30 Hz (0) */
static int s_pal60_now;   /* this run's PAL 60 */
static int s_interp_last; /* the 60 FPS row's value before the last change */
static int s_pal60_last;  /* the PAL 60 row's */
static int s_undo;        /* UNDO's value: the row's before the restart was pending */
static int s_asking;      /* ASK_T60 or ASK_PAL60 */
static const char *const k_interp_names[] = {"OFF", "INTERPOLATED", "60 HZ"};
static const int k_interp_values[] = {0, 1, 4};
static char s_undo_hint[24];
static const char *s_pending_text[1], *s_pal60_pending_text[1];

static const char *interp_name(int v) {
  int i;
  for (i = 0; i < 3; i++)
    if (k_interp_values[i] == v)
      return k_interp_names[i];
  return "INTERPOLATED"; /* (2 and 3 are read as 1) */
}

/* the US disc only (D138, D148): PAL's and JP's added code isn't judged
 * for 60 Hz (regions plan §6.3) */
static int region_us(void) { return g_Region == PSX_REGION_US; }
static int region_pal(void) { return g_Region == PSX_REGION_PAL; }
static int region_other(void) { return !region_us(); }
static int logic60(int interp) { return interp == 4 && tick60_built() && region_us(); }
static int t60_pending(void) { return logic60(g_vita_opts.interp) != s_tick60_now; }
static int pal60_pending(void) { return region_pal() && (g_RegionPal60 != 0) != s_pal60_now; }

static void restart_answer(int answer);
#define ASK_LINES "STARTS AGAIN. A RESTART LOSES", "PROGRESS SINCE THE LAST SAVE."
#define ASK_FOOTER "UP/DOWN: CHOOSE  X: OK  O: AT NEXT START"
static const VitaMenuAsk k_ask_on = {
    .title = "60 HZ LOGIC", .text = {"60 HZ LOGIC BEGINS WHEN THE GAME", ASK_LINES},
    .choice = {"RESTART NOW", "UNDO"}, .hint = {"60 HZ", s_undo_hint}, .nchoice = 2,
    .footer = ASK_FOOTER, .done = restart_answer};
static const VitaMenuAsk k_ask_off = {
    .title = "60 HZ LOGIC", .text = {"60 HZ LOGIC STOPS WHEN THE GAME", ASK_LINES},
    .choice = {"RESTART NOW", "UNDO"}, .hint = {"30 HZ", s_undo_hint}, .nchoice = 2,
    .footer = ASK_FOOTER, .done = restart_answer};
static const VitaMenuAsk k_ask_pal60_on = {
    .title = "PAL 60", .text = {"PAL 60 BEGINS WHEN THE GAME", ASK_LINES},
    .choice = {"RESTART NOW", "UNDO"}, .hint = {"60 HZ", s_undo_hint}, .nchoice = 2,
    .footer = ASK_FOOTER, .done = restart_answer};
static const VitaMenuAsk k_ask_pal60_off = {
    .title = "PAL 60", .text = {"PAL 60 STOPS WHEN THE GAME", ASK_LINES},
    .choice = {"RESTART NOW", "UNDO"}, .hint = {"50 HZ", s_undo_hint}, .nchoice = 2,
    .footer = ASK_FOOTER, .done = restart_answer};

static void restart_ask(int which) {
  s_asking = which;
  if (which == ASK_PAL60) {
    snprintf(s_undo_hint, sizeof s_undo_hint, "BACK TO %s", s_undo ? "ON" : "OFF");
    vita_menu_ask(s_pal60_now ? &k_ask_pal60_off : &k_ask_pal60_on);
  } else {
    snprintf(s_undo_hint, sizeof s_undo_hint, "BACK TO %s", interp_name(s_undo));
    vita_menu_ask(s_tick60_now ? &k_ask_off : &k_ask_on);
  }
}

static void restart_answer(int answer) {
  const int pal = s_asking == ASK_PAL60;
  if (answer == 0) {
    if (pal)
      vita_log("menu: pal60=%d: restarting for PAL %s", g_RegionPal60, g_RegionPal60 ? "60" : "50");
    else
      vita_log("menu: interp=%d: restarting for %s Hz logic", g_vita_opts.interp,
               logic60(g_vita_opts.interp) ? "60" : "30");
    vita_menu_restart(); /* doesn't return */
  } else if (answer == 1) {
    if (pal) {
      g_RegionPal60 = s_pal60_last = s_undo;
      vita_log("menu: PAL 60 back to %s (pal60=%d)", s_undo ? "ON" : "OFF", s_undo);
    } else {
      g_vita_opts.interp = s_interp_last = s_undo;
      vita_present_apply();
      vita_log("menu: 60 fps back to %s (interp=%d)", interp_name(s_undo), s_undo);
    }
  } else if (pal)
    vita_log("menu: pal60=%d: PAL %s at the next start", g_RegionPal60, g_RegionPal60 ? "60" : "50");
  else
    vita_log("menu: interp=%d: %s Hz logic at the next start", g_vita_opts.interp,
             logic60(g_vita_opts.interp) ? "60" : "30");
}

/* The 60 FPS row changed: applied (what changes without a restart), then
 * the question if this change made a restart pending. */
static void interp_changed(void) {
  const int last = s_interp_last;
  s_interp_last = g_vita_opts.interp;
  vita_present_apply();
  if (t60_pending() && logic60(last) == s_tick60_now) {
    s_undo = last;
    restart_ask(ASK_T60);
  }
}

/* The 60 FPS row on another disc (D148): OFF and INTERPOLATED only, with
 * a row under it saying 60 HZ is the US disc's. Its own copy of the value,
 * so an interp=4 kept for the US disc stays in config.txt (the US row,
 * hidden here, saves it) until the player changes this row. */
static int s_interp_row;
static const int k_interp_row_values[] = {0, 1};
static void interp_row_changed(void) {
  g_vita_opts.interp = s_interp_row;
  s_interp_last = s_interp_row;
  vita_present_apply();
}
static const char *const k_us_only[] = {"US DISC ONLY"};
static int us_only_action(void) { return 0; }

/* The PAL 60 row changed: the question if a restart is pending now. */
static void pal60_changed(void) {
  const int last = s_pal60_last;
  s_pal60_last = g_RegionPal60 != 0;
  if (pal60_pending() && last == s_pal60_now) {
    s_undo = last;
    restart_ask(ASK_PAL60);
  }
}

/* The rows saying a restart is pending: only while one is; X asks again. */
static int pending_visible(void) {
  s_pending_text[0] = logic60(g_vita_opts.interp) ? "ON AT RESTART" : "OFF AT RESTART";
  return t60_pending();
}
static int pending_action(void) {
  restart_ask(ASK_T60);
  return 0;
}
static int pal60_pending_visible(void) {
  s_pal60_pending_text[0] = g_RegionPal60 ? "ON AT RESTART" : "OFF AT RESTART";
  return pal60_pending();
}
static int pal60_pending_action(void) {
  restart_ask(ASK_PAL60);
  return 0;
}

/* The menu's rows (menu.c, D45). The selected value is shown between
 * arrows beside the label ("< INTERPOLATED >"), so the label is short: the
 * 640-pixel panel holds 32 characters a row (D148; "60 FPS (EXPERIMENTAL)"
 * until then, when the longest value was "NO ANIM"). 60 fps is off by
 * default (D44). The 60 FPS BUDGET row went with D75: with 60 fps on, the
 * in-between frame always runs (interp_budget is config.txt's only). */
static const MenuOption k_rows[] = {
    {.section = MENU_PICTURE, .label = "WIDESCREEN", .key = "widescreen",
     .value = &g_vita_opts.widescreen, .names = g_menu_off_on, .count = 2,
     .on_change = widescreen_changed},
    {.section = MENU_PICTURE, .label = "60 FPS", .key = "interp", .value = &g_vita_opts.interp,
     .names = k_interp_names, .values = k_interp_values, .count = 3,
     .on_change = interp_changed, .visible = region_us},
    {.section = MENU_PICTURE, .label = "60 FPS", .value = &s_interp_row,
     .names = k_interp_names, .values = k_interp_row_values, .count = 2,
     .on_change = interp_row_changed, .visible = region_other},
    {.section = MENU_PICTURE, .label = "60 HZ LOGIC", .names = k_us_only,
     .action = us_only_action, .visible = region_other},
    {.section = MENU_PICTURE, .label = "60 HZ LOGIC", .names = s_pending_text,
     .action = pending_action, .visible = pending_visible},
    {.section = MENU_PICTURE, .label = "PAL 60", .key = "pal60", .value = &g_RegionPal60,
     .names = g_menu_off_on, .count = 2, .on_change = pal60_changed, .visible = region_pal},
    {.section = MENU_PICTURE, .label = "PAL 60 TIMING", .names = s_pal60_pending_text,
     .action = pal60_pending_action, .visible = pal60_pending_visible},
};
MENU_OPTION_LIST(g_menu_present, k_rows);

int pres_draw_available(void); /* src/game/pres/pres_draw.c */
const char *pres_snap_misplaced(void); /* src/game/pres/pres_snap.c (D128) */

void vita_present_init(void) {
  /* 60 Hz logic (D130) and PAL 60 (D138) are read once, at start: their
   * values in the menu take effect at the next start */
  g_tick60_mode = s_tick60_now = logic60(g_vita_opts.interp);
  s_interp_last = g_vita_opts.interp;
  s_interp_row = g_vita_opts.interp != 0;
  s_pal60_now = s_pal60_last = region_pal() && !g_RegionTiming;
  if (g_vita_opts.interp == 4) {
    if (!region_us()) /* D148: the log said "60 Hz logic in play" here before */
      vita_log("present: 60 Hz logic is the US disc's only (interp=4, D138, D148): this disc "
               "runs %d Hz logic with the in-between frame (INTERPOLATED); interp=4 stays in "
               "config.txt for the US disc until the 60 FPS row is changed",
               psx_region_vblank_hz() / 2);
    else if (!tick60_built())
      vita_log("present: 60 Hz logic (interp=4, D130) isn't in this build: 30 Hz logic with "
               "the in-between frame");
    else
      vita_log("present: 60 Hz logic in play (interp=4, D130); the other gamestates at 30 Hz "
               "with the in-between frame");
  }
  /* D148: with the render thread showing each frame at its due vblank
   * (7c), a 50 Hz game (PAL at its own timing) files its frames due at the
   * display's vblanks, and 60 fps is a frame for each, blended by time */
  psx_clock_set_due_display(vita_gpu_due_mode());
  if (psx_clock_due_display())
    vita_log("present: %d Hz vblanks, frames due at the display's 60 Hz ones: 60 fps blended "
             "by time (D148)", psx_clock_hz());
  else if (psx_clock_hz() != 60)
    vita_log("present: %d Hz vblanks without the render thread's due frames (present_due=1, "
             "render_thread=1, realtime=1): 60 fps is one in-between frame a tick, %d fps",
             psx_clock_hz(), psx_clock_hz());
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
  if (s->time_ticks != last.time_ticks || s->time_fallbacks != last.time_fallbacks)
    vita_log("present %d: by time (D148): %lld ticks, %lld frames, %lld logic draws hidden, "
             "%lld ticks shown by their logic draw; the game thread's view: %lld display "
             "vblanks, %lld with no new frame",
             vblank, s->time_ticks - last.time_ticks, s->time_frames - last.time_frames,
             s->time_hidden - last.time_hidden, s->time_fallbacks - last.time_fallbacks,
             s->due_units - last.due_units, s->due_held - last.due_held);
  if (has_due && (s->due_late != last.due_late || s->due_superseded != last.due_superseded ||
                  s->queue_drops != last.queue_drops))
    vita_log("present %d: the game thread's frames by due vblank: %lld taken late, %lld never "
             "taken, %lld dropped from a full queue", vblank, s->due_late - last.due_late,
             s->due_superseded - last.due_superseded, s->queue_drops - last.queue_drops);
  last = *s;
}

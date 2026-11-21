/* Widescreen and 60 fps on the PS4 (docs/presentation.md, decisions D30,
 * D194), the Vita's present_vita.c: config.txt keys into
 * src/game/present.c's switches, the platform clock for the game thread's
 * safety net (interp_budget), the menu's PICTURE rows and a log line with
 * every stats report. The pictures are shown by video.c (the software
 * path: the presenter's CPU slots, D30) and gpu_gles.c (the GPU path).
 *
 * The presenter's in-between frame runs on the game thread, inline (the
 * host's way, stage 7f, D126): the PS4's cores are fast enough that the
 * Vita's worker thread isn't needed. */
#include "ps4.h"
#include "../../game/present.h"
#include "../../game/tick60.h"
#include "host.h" /* linux32's, for png.c's shots */
#include "menu_opts.h"
#include "psx_region.h"
#include "psx_clock.h"

#include <stdio.h>

static long long ps4_clock_us(void) { return (long long)ps4_now_us(); }

/* interp_budget (D75): the safety net that skips the in-between frame only
 * if the game thread itself would run late: 15/16 of a tick (two NTSC
 * vblanks, 33.37 ms), or 0 to turn it off. */
#define NET_US 31281

/* The 60 FPS row's values (D148, D150): 0 OFF, 1 INTERPOLATED (the
 * in-between frame: camera, Spyro, the mobys, their animation, the
 * particles), 4 60 HZ (60 Hz logic in play, D130; the other gamestates and
 * the attract demos at 30 Hz with the in-between frame). The Vita's
 * default, 60 HZ, since D150. */
void ps4_present_apply(void) {
  g_present.widescreen = g_ps4_opts.widescreen != 0;
  g_present.interp = g_ps4_opts.interp != 0;
  g_present.interp_mobys = g_present.interp_anim = g_present.interp;
  g_present.interp_budget_us = g_ps4_opts.interp_budget ? NET_US : 0;
}

/* 60 Hz logic (D130) is read once, at start, so a change into or out of it
 * asks whether to close the game now (the player starts it again; QUIT
 * NOW) or puts the row back (UNDO); O leaves it for the next start, and a
 * row says so while it waits (the Vita's D136, which restarts the app in
 * place: a PS4 fpkg closes instead). */
static int s_tick60_now;  /* this run's logic: 60 Hz (1) or 30 Hz (0) */
static int s_interp_last; /* the 60 FPS row's value before the last change */
static int s_undo;        /* UNDO's value */
static const char *const k_interp_names[] = {"OFF", "INTERPOLATED", "60 HZ"};
static const int k_interp_values[] = {0, 1, 4};
static char s_undo_hint[24];
static const char *s_pending_text[1];

static const char *interp_name(int v) {
  int i;
  for (i = 0; i < 3; i++)
    if (k_interp_values[i] == v)
      return k_interp_names[i];
  return "INTERPOLATED";
}

/* Where 60 Hz logic runs: the US disc, and the PAL disc at US timing
 * (D150); elsewhere INTERPOLATED (present_vita.c has the reasons). */
static int logic60_disc(void) {
  return g_Region == PSX_REGION_US || (g_Region == PSX_REGION_PAL && !g_RegionTiming);
}
static int logic60(int interp) { return interp == 4 && tick60_built() && logic60_disc(); }
static int t60_pending(void) { return logic60(g_ps4_opts.interp) != s_tick60_now; }

static void quit_answer(int answer);
#define ASK_LINES "CLOSES. START IT AGAIN. CLOSING", "LOSES PROGRESS SINCE THE SAVE."
#define ASK_FOOTER "UP/DOWN: CHOOSE  X: OK  O: AT NEXT START"
static const Ps4MenuAsk k_ask_on = {
    .title = "60 HZ LOGIC", .text = {"60 HZ LOGIC BEGINS WHEN THE GAME", ASK_LINES},
    .choice = {"QUIT NOW", "UNDO"}, .hint = {"60 HZ", s_undo_hint}, .nchoice = 2,
    .footer = ASK_FOOTER, .done = quit_answer};
static const Ps4MenuAsk k_ask_off = {
    .title = "60 HZ LOGIC", .text = {"60 HZ LOGIC STOPS WHEN THE GAME", ASK_LINES},
    .choice = {"QUIT NOW", "UNDO"}, .hint = {"30 HZ", s_undo_hint}, .nchoice = 2,
    .footer = ASK_FOOTER, .done = quit_answer};

static void quit_ask(void) {
  snprintf(s_undo_hint, sizeof s_undo_hint, "BACK TO %s", interp_name(s_undo));
  ps4_menu_ask(s_tick60_now ? &k_ask_off : &k_ask_on);
}

static void quit_answer(int answer) {
  if (answer == 0) {
    host_log("menu: interp=%d: closing for %s Hz logic at the next start", g_ps4_opts.interp,
             logic60(g_ps4_opts.interp) ? "60" : "30");
    ps4_menu_quit(); /* doesn't return */
  } else if (answer == 1) {
    g_ps4_opts.interp = s_interp_last = s_undo;
    ps4_present_apply();
    host_log("menu: 60 fps back to %s (interp=%d)", interp_name(s_undo), s_undo);
  } else
    host_log("menu: interp=%d: %s Hz logic at the next start", g_ps4_opts.interp,
             logic60(g_ps4_opts.interp) ? "60" : "30");
}

static void interp_changed(void) {
  const int last = s_interp_last;
  s_interp_last = g_ps4_opts.interp;
  ps4_present_apply();
  if (t60_pending() && logic60(last) == s_tick60_now) {
    s_undo = last;
    quit_ask();
  }
}

static int pending_visible(void) {
  s_pending_text[0] = logic60(g_ps4_opts.interp) ? "ON AT NEXT START" : "OFF AT NEXT START";
  return t60_pending();
}
static int pending_action(void) {
  quit_ask();
  return 0;
}

static const MenuOption k_rows[] = {
    {.section = MENU_PICTURE, .label = "WIDESCREEN", .key = "widescreen",
     .value = &g_ps4_opts.widescreen, .names = g_menu_off_on, .count = 2,
     .on_change = ps4_present_apply},
    {.section = MENU_PICTURE, .label = "60 FPS", .key = "interp", .value = &g_ps4_opts.interp,
     .names = k_interp_names, .values = k_interp_values, .count = 3,
     .on_change = interp_changed},
    {.section = MENU_PICTURE, .label = "60 HZ LOGIC", .names = s_pending_text,
     .action = pending_action, .visible = pending_visible},
};
MENU_OPTION_LIST(g_menu_present, k_rows);

int pres_draw_available(void); /* src/game/pres/pres_draw.c */
const char *pres_snap_misplaced(void); /* src/game/pres/pres_snap.c (D128) */

void ps4_present_init(void) {
  if (g_ps4_opts.interp == 2 || g_ps4_opts.interp == 3) /* D44's gone values (D148) */
    g_ps4_opts.interp = 1;
  g_tick60_mode = s_tick60_now = logic60(g_ps4_opts.interp);
  s_interp_last = g_ps4_opts.interp;
  if (g_ps4_opts.interp == 4) {
    if (!logic60_disc())
      host_log("present: 60 Hz logic doesn't run on %s (interp=4): the in-between frame",
               psx_region()->serial);
    else if (!tick60_built())
      host_log("present: 60 Hz logic (interp=4) isn't in this build: the in-between frame");
    else
      host_log("present: 60 Hz logic in play (interp=4, D130); the other gamestates at 30 Hz "
               "with the in-between frame");
  }
  if (psx_clock_hz() != 60)
    host_log("present: %d Hz vblanks: 60 fps is one in-between frame a tick, %d fps",
             psx_clock_hz(), psx_clock_hz());
  ps4_present_apply();
  g_present.stats = g_ps4_opts.stats ? 1 : 0;
  g_present_clock_us = ps4_clock_us;
  if (g_ps4_gpu) /* the presenter's frames as textures (gpu_gles.c) */
    g_present_capture = ps4_gpu_capture();
  else
    g_host_opts.out960 = 2; /* shots: the presentation frame on screen, 512x240 */
  host_log("present: widescreen %d, interp %d (the safety net %s), %s capture; hold the touch pad's click "
           "and press OPTIONS for the options menu",
           g_ps4_opts.widescreen, g_ps4_opts.interp,
           g_ps4_opts.interp_budget ? "at 31.3 ms a tick" : "off",
           g_present_capture ? "GPU" : "CPU");
  if (pres_draw_available())
    host_log("present: the presenter draws the in-between frame and the attract demos' 16:9 "
             "frame (on the game thread)");
  else
    host_log("present: no presenter (%s): no in-between frame, and the attract demos 4:3",
             pres_snap_misplaced() ? pres_snap_misplaced() : "not built, or no area");
}

/* One line per stats report, totals since the last one. */
void ps4_present_report(int vblank) {
  static PresentStats last;
  const PresentStats *s = &g_present_stats;
  long long ticks = s->ticks - last.ticks;
  if (!port_present_active() || ticks <= 0) {
    last = *s;
    return;
  }
  host_log("present %d: %lld ticks; per tick us: logic draw %lld, the presenter %lld, blend %lld, "
           "update %lld; %lld in-between frames, %lld skipped for the budget (%lld falls to a "
           "held 30 fps), %lld otherwise; %lld pictures from the 4:3 display",
           vblank, ticks, (s->us_logic - last.us_logic) / ticks,
           (s->us_pass - last.us_pass) / ticks, (s->us_blend - last.us_blend) / ticks,
           (s->us_update - last.us_update) / ticks, s->interp_frames - last.interp_frames,
           s->budget_skips - last.budget_skips, s->budget_holds - last.budget_holds,
           s->skipped_interp - last.skipped_interp,
           s->display_fallbacks - last.display_fallbacks);
  last = *s;
}

/* The options menu's PICTURE and CONTROLS rows on PC (decisions D275; the
 * PS4's present_ps4.c, picture_ps4.c and pad.c rows, the Vita's D30, D39,
 * D45, D47, D130, D162): config.txt's keys, which main.c's set_opt reads as
 * before, as menu rows.
 *
 * At once: FULLSCREEN and VSYNC (video.c, on the main thread), WIDESCREEN
 * and 60 FPS's in-between frame (src/game/present.c), FILTER (video.c
 * reads it each picture), PERSPECTIVE, SUB-PIXEL and RESOLUTION (gpu_gl.c),
 * DRAW DISTANCE (src/game/lod.c) and the CONTROLS rows (pad.c). At the next
 * start: RENDERER (the GPU renderer's context is made at start), 60 FPS's 60
 * HZ (60 Hz logic, D130, is read once, at start) and PERSPECTIVE or
 * SUB-PIXEL where the GPU renderer started with both off; a NEXT START row
 * says what waits. */
#include "pc.h"
#include "host.h"
#include "menu_opts.h"
#include "../../game/lod.h"
#include "../../game/present.h"
#include "../../game/tick60.h"
#include "psx_region.h"

/* ---- 60 FPS and 60 Hz logic (the PS4's present_ps4.c) ---- */

/* Where 60 Hz logic runs: the US disc, and the PAL disc at US timing
 * (D150); elsewhere the in-between frame (present_vita.c has the reasons). */
static int logic60_disc(void) {
  return g_Region == PSX_REGION_US || (g_Region == PSX_REGION_PAL && !g_RegionTiming);
}
static int logic60(int interp) { return interp == 4 && tick60_built() && logic60_disc(); }

static int s_tick60_now; /* this run's logic: 60 Hz (1) or 30 Hz (0) */
static int s_gpu_start;  /* gpu= at start */
static int s_vc_start;   /* persp or subpixel at start (gpu_gl.c's vcache) */

static void present_apply(void) {
  g_present.widescreen = g_pc_opts.widescreen != 0;
  g_present.interp = g_present.interp_mobys = g_present.interp_anim = g_pc_opts.interp != 0;
}

static void interp_changed(void) {
  present_apply();
  if (logic60(g_pc_opts.interp) != s_tick60_now)
    host_log("menu: interp=%d: %s Hz logic at the next start", g_pc_opts.interp,
             logic60(g_pc_opts.interp) ? "60" : "30");
}

/* ---- the window (video.c, on the main thread) ---- */

static void window_changed(void) { pc_video_want(g_pc_opts.fullscreen, g_pc_opts.vsync); }

/* ---- the GPU renderer's options (gpu_gl.c) ---- */

static int gpu_shown(void) { return g_pc_gpu; }

static void gpu_options_changed(void) {
  if (g_pc_gpu && !pc_gpu_set_options(g_pc_opts.persp, g_pc_opts.subpixel))
    host_log("menu: persp=%d subpixel=%d at the next start", g_pc_opts.persp, g_pc_opts.subpixel);
}

static void res_changed(void) {
  if (g_pc_gpu)
    pc_gpu_set_res(g_pc_opts.res);
}

static void gpu_changed(void) {
  host_log("menu: gpu=%d (the %s renderer) at the next start", g_pc_opts.gpu,
           g_pc_opts.gpu ? "GPU" : "software");
}

/* ---- the draw distance (the PS4's picture_ps4.c, #260, #261) ---- */

#define DD_MAX_PCT 400

/* draw_distance= is a percentage, 100 to DD_MAX_PCT: the menu's NORMAL (100,
 * the original) and MAX (ADAPTIVE) (DD_MAX_PCT); 150 and other values stay
 * config.txt's (shown as "150%", left or right goes to the nearest). */
static void dd_apply(void) {
  int pct = g_pc_opts.draw_distance;
  pct = pct < 100 ? 100 : pct > DD_MAX_PCT ? DD_MAX_PCT : pct;
  g_pc_opts.draw_distance = pct;
  g_lod.scale = pct * LOD_ONE / 100;
}

static void dd_changed(void) {
  dd_apply();
  host_log("picture: draw distance %d%%", g_pc_opts.draw_distance);
}

/* ---- what waits for the next start ---- */

static const char *s_pending_text[1];

static int pending_visible(void) {
  int n = 0;
  const char *t = "";
  if (g_pc_opts.gpu != s_gpu_start)
    n++, t = g_pc_opts.gpu ? "GPU RENDERER" : "SOFTWARE";
  if (logic60(g_pc_opts.interp) != s_tick60_now)
    n++, t = s_tick60_now ? "60 HZ LOGIC OFF" : "60 HZ LOGIC";
  if (g_pc_gpu && !s_vc_start && (g_pc_opts.persp || g_pc_opts.subpixel))
    n++, t = g_pc_opts.persp ? "PERSPECTIVE" : "SUB-PIXEL";
  s_pending_text[0] = n > 1 ? "SEVERAL CHANGES" : t;
  return n > 0;
}

static int pending_action(void) { return 0; }

/* on open: the window as it is (F11 may have changed it) */
static void picture_open(void) { g_pc_opts.fullscreen = pc_video_fullscreen(); }

static const char *const k_interp_names[] = {"OFF", "INTERPOLATED", "60 HZ"};
static const int k_interp_values[] = {0, 1, 4};
static const char *const k_filter_names[] = {"NEAREST", "LINEAR"};
static const char *const k_gpu_names[] = {"SOFTWARE", "GPU"};
static const char *const k_res_names[] = {"WINDOW", "1X", "2X", "3X", "4X",
                                          "5X", "6X", "7X", "8X"};
/* "DRAW DISTANCE" and "< MAX (ADAPTIVE) >" are 31 letters, the most the
 * 640-pixel panel takes (the Vita's picture.c) */
static const char *const k_dd_names[] = {"NORMAL", "MAX (ADAPTIVE)"};
static const int k_dd_values[] = {100, DD_MAX_PCT};

static const MenuOption k_picture[] = {
    {.section = MENU_PICTURE, .label = "FULLSCREEN", .key = "fullscreen",
     .value = &g_pc_opts.fullscreen, .names = g_menu_off_on, .count = 2,
     .on_change = window_changed},
    {.section = MENU_PICTURE, .label = "WIDESCREEN", .key = "widescreen",
     .value = &g_pc_opts.widescreen, .names = g_menu_off_on, .count = 2,
     .on_change = present_apply},
    {.section = MENU_PICTURE, .label = "60 FPS", .key = "interp", .value = &g_pc_opts.interp,
     .names = k_interp_names, .values = k_interp_values, .count = 3,
     .on_change = interp_changed},
    {.section = MENU_PICTURE, .label = "VSYNC", .key = "vsync", .value = &g_pc_opts.vsync,
     .names = g_menu_off_on, .count = 2, .on_change = window_changed},
    {.section = MENU_PICTURE, .label = "FILTER", .key = "filter", .value = &g_pc_opts.filter,
     .names = k_filter_names, .count = 2},
    {.section = MENU_PICTURE, .label = "RENDERER", .key = "gpu", .value = &g_pc_opts.gpu,
     .names = k_gpu_names, .count = 2, .on_change = gpu_changed},
    {.section = MENU_PICTURE, .label = "PERSPECTIVE", .key = "persp", .value = &g_pc_opts.persp,
     .names = g_menu_off_on, .count = 2, .on_change = gpu_options_changed,
     .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "SUB-PIXEL", .key = "subpixel",
     .value = &g_pc_opts.subpixel, .names = g_menu_off_on, .count = 2,
     .on_change = gpu_options_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "RESOLUTION", .key = "res", .value = &g_pc_opts.res,
     .names = k_res_names, .count = 9, .on_change = res_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "DRAW DISTANCE", .key = "draw_distance",
     .value = &g_pc_opts.draw_distance, .names = k_dd_names, .values = k_dd_values,
     .count = 2, .fmt = "%d%%", .on_change = dd_changed},
    {.section = MENU_PICTURE, .label = "NEXT START", .names = s_pending_text,
     .action = pending_action, .visible = pending_visible},
};
const MenuOptionList g_menu_pc_picture = {k_picture, (int)(sizeof k_picture / sizeof k_picture[0]),
                                          picture_open};

/* ---- CONTROLS (the PS4's pad.c rows; pad.c applies them) ---- */

static const MenuOption k_controls[] = {
    {.section = MENU_CONTROLS, .label = "ANALOG STICK", .key = "analog",
     .value = &g_pc_opts.analog, .names = g_menu_off_on, .count = 2,
     .on_change = pc_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "FREE CAMERA", .key = "free_camera",
     .value = &g_pc_opts.free_camera, .names = g_menu_off_on, .count = 2,
     .on_change = pc_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "INVERT CAMERA X", .key = "cam_invert_x",
     .value = &g_pc_opts.cam_invert_x, .names = g_menu_off_on, .count = 2,
     .on_change = pc_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "INVERT CAMERA Y", .key = "cam_invert_y",
     .value = &g_pc_opts.cam_invert_y, .names = g_menu_off_on, .count = 2,
     .on_change = pc_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "CAMERA SPEED", .key = "cam_speed",
     .value = &g_pc_opts.cam_speed, .min = 50, .max = 200, .step = 25, .fmt = "%d%%",
     .on_change = pc_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "CAMERA RECENTRE", .key = "cam_recentre",
     .value = &g_pc_opts.cam_recentre, .names = g_menu_off_on, .count = 2,
     .on_change = pc_pad_controls_apply},
};
MENU_OPTION_LIST(g_menu_pc_controls, k_controls);

/* main.c, on the game thread before the game: the rows' settings that the
 * game reads (60 Hz logic once, here), after the disc's region is known. */
void pc_menu_init(void) {
  if (g_pc_opts.interp == 2 || g_pc_opts.interp == 3) /* D44's gone values (D148) */
    g_pc_opts.interp = 1;
  g_tick60_mode = s_tick60_now = logic60(g_pc_opts.interp);
  s_gpu_start = g_pc_opts.gpu;
  s_vc_start = g_pc_opts.persp || g_pc_opts.subpixel;
  if (g_pc_opts.interp == 4) {
    if (!logic60_disc())
      host_log("present: 60 Hz logic doesn't run on %s (interp=4): the in-between frame",
               psx_region()->serial);
    else if (!tick60_built())
      host_log("present: 60 Hz logic (interp=4) isn't in this build: the in-between frame");
    else
      host_log("present: 60 Hz logic in play (interp=4, D130); the other gamestates at 30 Hz "
               "with the in-between frame");
  }
  present_apply();
  dd_apply();
  if (g_lod.scale > LOD_ONE)
    host_log("picture: draw distance %d%%", g_pc_opts.draw_distance);
  host_log("menu: SELECT+START (Back+Start; Backspace or Tab, then Enter), Esc or F1 opens "
           "the options menu");
}

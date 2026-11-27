/* The options menu's other PICTURE rows on the PS4 (D245; the Vita's
 * picture.c, decisions D39, D48, D49, D194): the GLES2 renderer's PSX
 * WOBBLE (ON: the PS1's look; OFF, the default as on the Vita: perspective-
 * correct textures and sub-pixel vertices together) and resolution, and the longer
 * draw distance (src/game/lod.c), which both renderers draw. The GPU rows
 * are hidden with the software renderer, where they do nothing; their keys
 * are still read and saved. */
#include "ps4.h"
#include "host.h"
#include "../../game/lod.h"
#include "menu_opts.h"

static int gpu_shown(void) { return g_ps4_gpu; }

static void gpu_changed(void) {
  if (g_ps4_gpu)
    ps4_gpu_set_options(!g_ps4_opts.wobble, !g_ps4_opts.wobble);
}

static void res_changed(void) {
  if (g_ps4_gpu)
    ps4_gpu_set_res(g_ps4_opts.gpu_res);
}

/* The menu's MAX (ADAPTIVE), percent, and the most draw_distance= gives
 * (#260): one place. 400 since #261: past 4x no level draws more (the
 * whole-game tour, D256), and the scale passes 2x only where a 60 Hz budget
 * measures the draws (lod.c scale_top); 200 until then. */
#define DD_MAX_PCT 400

/* draw_distance= is a percentage, 100 to DD_MAX_PCT: the menu's NORMAL (100,
 * the original) and MAX (ADAPTIVE) (DD_MAX_PCT, #260); 150 and other values
 * stay config.txt's. */
static void picture_apply(void) {
  int pct = g_ps4_opts.draw_distance;
  pct = pct < 100 ? 100 : pct > DD_MAX_PCT ? DD_MAX_PCT : pct;
  g_ps4_opts.draw_distance = pct;
  g_lod.scale = pct * LOD_ONE / 100;
  g_lod.stats = g_ps4_opts.stats != 0 && pct > 100;
}

static void draw_distance_changed(void) {
  picture_apply();
  host_log("picture: draw distance %d%%", g_ps4_opts.draw_distance);
}

void ps4_picture_init(void) {
  if (g_ps4_opts.gpu_res < 0 || g_ps4_opts.gpu_res > 1)
    g_ps4_opts.gpu_res = 0;
  picture_apply();
  if (g_lod.scale > LOD_ONE)
    host_log("picture: draw distance %d%%", g_ps4_opts.draw_distance);
  if (g_ps4_gpu) {
    gpu_changed();
    ps4_gpu_set_res(g_ps4_opts.gpu_res);
  }
}

/* One "lod" line per stats report while the draw distance is longer. */
void ps4_picture_report(int vblank) {
  char line[400];
  if (!g_lod.stats)
    return;
  port_lod_report(line, sizeof line);
  if (line[0])
    host_log("lod %d: %s", vblank, line);
}

static const char *const k_res_names[] = {"NATIVE", "PS1"};
/* DRAW DISTANCE (#260, the owner's decision, as the Vita's picture.c; "DRAW
 * DISTANCE (EXP.)" with OFF, 1.5X and 2X until then). MAX (ADAPTIVE) is
 * DD_MAX_PCT where the frame leaves room: lod.c's limiter backs off before the draw's
 * span fills and, with 60 Hz logic in play, past its fixed 48 KB work
 * budget (LOD_BUDGET_60, #180). The PS4 has no budget sized from measured
 * time (the Vita's lod_time.c, D212). A config.txt value not listed here
 * (150) shows as "150%"; left or right goes to the nearest, NORMAL on a
 * tie. "DRAW DISTANCE" and "< MAX (ADAPTIVE) >" are 31 letters, the most
 * the 640-pixel panel takes (the Vita's picture.c). */
static const char *const k_dd_names[] = {"NORMAL", "MAX (ADAPTIVE)"};
static const int k_dd_values[] = {100, DD_MAX_PCT};
/* WOBBLE FIX (D282, as the Vita's picture.c; PSX WOBBLE until then): ON is
 * the fix (wobble=0), OFF the PS1's wobble (wobble=1); the key and its values
 * are unchanged. */
static const char *const k_fix_names[] = {"OFF", "ON"};
static const int k_fix_values[] = {1, 0};
static const MenuOption k_rows[] = {
    {.section = MENU_PICTURE, .label = "WOBBLE FIX", .key = "wobble",
     .value = &g_ps4_opts.wobble, .names = k_fix_names, .values = k_fix_values, .count = 2,
     .on_change = gpu_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "RESOLUTION", .key = "res", .value = &g_ps4_opts.gpu_res,
     .names = k_res_names, .count = 2, .on_change = res_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "DRAW DISTANCE", .key = "draw_distance",
     .value = &g_ps4_opts.draw_distance, .names = k_dd_names, .values = k_dd_values,
     .count = 2, .fmt = "%d%%", .on_change = draw_distance_changed},
};
MENU_OPTION_LIST(g_menu_picture, k_rows);

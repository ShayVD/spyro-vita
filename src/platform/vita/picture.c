/* The options menu's picture rows (decisions D48, D49): the GPU renderer's
 * texture filter, perspective, sub-pixel vertices and resolution (D39-D43,
 * until now config.txt only), and the longer draw distance
 * (src/game/lod.c). All change at once. The GPU rows are hidden with the
 * software renderer (gpu=0, or the GPU's shaders failed), where they do
 * nothing; their keys are still read and saved. */
#include "vita.h"
#include "../../game/lod.h"
#include "menu_opts.h"

static int gpu_shown(void) { return g_vita_gpu; }

/* The options the GPU draws with now: a set whose shaders don't compile
 * (vita_gpu_set_options) puts the menu's value back. */
static int s_filter, s_persp, s_subpixel;

static void gpu_changed(void) {
  if (!g_vita_gpu)
    return;
  if (vita_gpu_set_options(g_vita_opts.gpu_res, g_vita_opts.tex_filter, g_vita_opts.persp,
                           g_vita_opts.subpixel, g_vita_opts.tex_cache) != 0) {
    g_vita_opts.tex_filter = s_filter;
    g_vita_opts.persp = s_persp;
    g_vita_opts.subpixel = s_subpixel;
    return;
  }
  s_filter = g_vita_opts.tex_filter;
  s_persp = g_vita_opts.persp;
  s_subpixel = g_vita_opts.subpixel;
}

/* The menu already wrote gpu_res, so set_options sees no change: every
 * target again at the new size here. */
static void res_changed(void) {
  if (g_vita_gpu)
    vita_gpu_rescale();
}

/* draw_distance= is a percentage: 100 (off), 150, 200 */
void vita_picture_apply(void) {
  int pct = g_vita_opts.draw_distance;
  pct = pct < 100 ? 100 : pct > 200 ? 200 : pct;
  g_lod.scale = pct * LOD_ONE / 100;
  g_lod.stats = g_vita_opts.stats != 0 && pct > 100;
}

static void draw_distance_changed(void) {
  vita_picture_apply();
  vita_log("picture: draw distance %d%%", g_vita_opts.draw_distance);
}

void vita_picture_init(void) {
  s_filter = g_vita_opts.tex_filter;
  s_persp = g_vita_opts.persp;
  s_subpixel = g_vita_opts.subpixel;
  vita_picture_apply();
  if (g_lod.scale > LOD_ONE)
    vita_log("picture: draw distance %d%% (experimental, D49)", g_vita_opts.draw_distance);
}

/* One "lod" line per stats report while the draw distance is longer. */
void vita_picture_report(int vblank) {
  char line[400];
  if (!g_lod.stats)
    return;
  port_lod_report(line, sizeof line);
  if (line[0])
    vita_log("lod %d: %s", vblank, line);
}

/* 3-point filtering stays, labelled: on hardware it costs about a third of
 * the frame rate at native resolution (D43, bench on 26f3138). */
static const char *const k_filter_names[] = {"OFF", "BILINEAR", "3-POINT (SLOW)"};
static const char *const k_res_names[] = {"NATIVE", "PS1", "2X", "3X", "4X"};
static const char *const k_dd_names[] = {"OFF", "1.5X", "2X"};
static const int k_dd_values[] = {100, 150, 200};
/* Labels and values fit the 640-pixel panel with "< >" round the value
 * (18 pixels a letter: at most 31 letters together). */
static const MenuOption k_rows[] = {
    {.section = MENU_PICTURE, .label = "TEX FILTER", .key = "tex_filter",
     .value = &g_vita_opts.tex_filter, .names = k_filter_names, .count = 3,
     .on_change = gpu_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "PERSPECTIVE TEXTURES", .key = "persp",
     .value = &g_vita_opts.persp, .names = g_menu_off_on, .count = 2,
     .on_change = gpu_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "SUB-PIXEL VERTICES", .key = "subpixel",
     .value = &g_vita_opts.subpixel, .names = g_menu_off_on, .count = 2,
     .on_change = gpu_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "RESOLUTION", .key = "res", .value = &g_vita_opts.gpu_res,
     .names = k_res_names, .count = 5, .on_change = res_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "DRAW DISTANCE (EXP.)", .key = "draw_distance",
     .value = &g_vita_opts.draw_distance, .names = k_dd_names, .values = k_dd_values,
     .count = 3, .fmt = "%d%%", .on_change = draw_distance_changed},
};
MENU_OPTION_LIST(g_menu_picture, k_rows);

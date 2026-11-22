/* The options menu's other PICTURE rows on the PS4 (D194; the Vita's
 * picture.c, decisions D39, D48, D49): the GLES2 renderer's PS1 WOBBLE
 * (off: the Vita's perspective-correct textures and sub-pixel vertices
 * together, one row as the owner asked) and resolution, and the longer
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

/* draw_distance= is a percentage: 100 (off), 150, 200 */
static void picture_apply(void) {
  int pct = g_ps4_opts.draw_distance;
  pct = pct < 100 ? 100 : pct > 200 ? 200 : pct;
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
    host_log("picture: draw distance %d%% (experimental, D49)", g_ps4_opts.draw_distance);
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
static const char *const k_dd_names[] = {"OFF", "1.5X", "2X"};
static const int k_dd_values[] = {100, 150, 200};
static const MenuOption k_rows[] = {
    {.section = MENU_PICTURE, .label = "PS1 WOBBLE", .key = "wobble",
     .value = &g_ps4_opts.wobble, .names = g_menu_off_on, .count = 2,
     .on_change = gpu_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "RESOLUTION", .key = "res", .value = &g_ps4_opts.gpu_res,
     .names = k_res_names, .count = 2, .on_change = res_changed, .visible = gpu_shown},
    {.section = MENU_PICTURE, .label = "DRAW DISTANCE (EXP.)", .key = "draw_distance",
     .value = &g_ps4_opts.draw_distance, .names = k_dd_names, .values = k_dd_values,
     .count = 3, .fmt = "%d%%", .on_change = draw_distance_changed},
};
MENU_OPTION_LIST(g_menu_picture, k_rows);

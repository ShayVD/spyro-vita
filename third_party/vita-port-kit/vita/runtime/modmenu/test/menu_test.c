/* The options menu's host-side parts: the panel (vpk_menu_panel.c) with
 * sample rows, section headers and scrolling, checked at a few pixels and
 * written as PPMs for a look; the option table (vpk_menu.c): layout,
 * stepping, config.txt load and save (other lines kept); and the config
 * rewrite in place (kh1-decomp's save). From spyro-vita
 * tests/menu_panel/test_menu_panel.c @ 987363e.
 *   menu_test [directory for the PPMs and a scratch config.txt] */
#include "vpk_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) \
  do {                \
    if (!(c))         \
      printf("FAIL: " __VA_ARGS__), printf("\n"), fails++; \
  } while (0)

static int count(const uint8_t *rgb, int n, int r, int g, int b) {
  int i, k = 0;
  for (i = 0; i < n; i++)
    k += rgb[i * 3] == r && rgb[i * 3 + 1] == g && rgb[i * 3 + 2] == b;
  return k;
}

static void write_ppm(const char *dir, const char *name, const uint8_t *rgb, int h) {
  char path[512];
  FILE *f;
  if (!dir)
    return;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  if ((f = fopen(path, "wb"))) {
    fprintf(f, "P6\n%d %d\n255\n", VPK_MENU_PANEL_W, h);
    fwrite(rgb, 3, (size_t)VPK_MENU_PANEL_W * h, f);
    fclose(f);
  }
}

/* Sample sections and rows, as two feature files would list them. */
enum { MENU_PICTURE, MENU_CONTROLS, MENU_GAME, MENU_SAVE, MENU_NSECTIONS };
static const char *const g_menu_section_names[MENU_NSECTIONS] = {"PICTURE", "CONTROLS", "GAME", "SAVE"};
static int s_ws = 1, s_interp, s_budget = 26, s_speed = 100, s_changes, s_actions, s_res,
           s_gpu = 1;
static void changed(void) { s_changes++; }
static int gpu_on(void) { return s_gpu; }
static const char *const k_res[] = {"NATIVE", "PS1", "2X"};
static int act(void) { return ++s_actions, 1; }
static const char *const k_interp[] = {"OFF", "ON", "NO ANIM", "CAMERA"};
static const int k_budgets[] = {0, 20, 23, 26, 30};
static const char *const k_budget_names[] = {"NEVER SKIP", "20 MS", "23 MS", "26 MS", "30 MS"};
static const char *const k_save_names[] = {"PRESS X"};
static const VpkMenuOption k_a[] = {
    {.section = MENU_PICTURE, .label = "WIDESCREEN", .key = "widescreen", .value = &s_ws,
     .names = vpk_menu_off_on, .count = 2, .on_change = changed},
    {.section = MENU_PICTURE, .label = "60 FPS (EXPERIMENTAL)", .key = "interp",
     .value = &s_interp, .names = k_interp, .count = 4},
    {.section = MENU_PICTURE, .label = "60 FPS BUDGET", .key = "interp_budget",
     .value = &s_budget, .names = k_budget_names, .values = k_budgets, .count = 5,
     .fmt = "%d MS"},
};
static const VpkMenuOption k_b[] = {
    {.section = MENU_SAVE, .label = "SAVE STATE", .names = k_save_names, .action = act},
    {.section = MENU_CONTROLS, .label = "CAMERA SPEED", .key = "cam_speed", .value = &s_speed,
     .min = 50, .max = 200, .step = 25, .fmt = "%d%%"},
    {.section = MENU_PICTURE, .label = "RESOLUTION", .key = "res", .value = &s_res,
     .names = k_res, .count = 3, .visible = gpu_on},
};
static VPK_MENU_LIST(s_list_a, k_a);
static VPK_MENU_LIST(s_list_b, k_b);

/* kh1-decomp's save: in place, a key's later lines dropped, an alias, the missing keys at the end, a last line
 * without a newline; and the append form's drop list and head */
static void test_rewrite(void) {
  static const char *const keys[] = {"wide", "rcam", "volume"}, *const vals[] = {"1", "2", "80"};
  static const char *const alias[] = {"widescreen", "wide", NULL};
  static const char *const drop[] = {"config_version", NULL};
  const char *old = "iso=ux0:data/kh1/kh1.iso\n# rcam=9 a comment\nwidescreen=0\nrcam=1\nframes=100\nrcam=0\nx";
  char out[512];
  size_t n = vpk_config_rewrite(old, strlen(old), keys, vals, 3, NULL, NULL, 1, alias, out, sizeof out);
  CHECK(n == strlen(out) && !strcmp(out, "iso=ux0:data/kh1/kh1.iso\n# rcam=9 a comment\nwide=1\nrcam=2\nframes=100\nx\n"
                                         "volume=80\n"), "in place: %s", out);
  n = vpk_config_rewrite(old, strlen(old), keys, vals, 3, NULL, NULL, 1, alias, out, 20);
  CHECK(n == 19 && strlen(out) == 19, "in place: cut short at the end of out (%d)", (int)n);
  old = "a=1\nconfig_version=1\nvolume=10\n";
  vpk_config_rewrite(old, strlen(old), keys, vals, 3, drop, "config_version=3\n", 0, NULL, out, sizeof out);
  CHECK(!strcmp(out, "a=1\nconfig_version=3\nwide=1\nrcam=2\nvolume=80\n"), "append: %s", out);
  vpk_config_rewrite(NULL, 0, keys, vals, 1, NULL, NULL, 1, NULL, out, sizeof out);
  CHECK(!strcmp(out, "wide=1\n"), "no file: %s", out);
}

static void test_table(const char *dir) {
  const VpkMenuList *lists[] = {&s_list_a, NULL, &s_list_b};
  VpkMenuRow rows[16];
  const VpkMenuOption *opts[16];
  char buf[32], path[512], line[128], text[1024] = "";
  int n = vpk_menu_layout(lists, 3, g_menu_section_names, MENU_NSECTIONS, rows, opts, 16);
  FILE *f;
  /* PICTURE header, 4 rows, CONTROLS header, speed, SAVE header, action */
  CHECK(n == 9, "layout gave %d lines", n);
  CHECK(!opts[0] && !strcmp(rows[0].label, "PICTURE") && !rows[0].value, "PICTURE header");
  CHECK(opts[1] == &k_a[0] && opts[3] == &k_a[2] && opts[4] == &k_b[2],
        "PICTURE rows in list order");
  CHECK(!opts[5] && !strcmp(rows[5].label, "CONTROLS"), "CONTROLS header after PICTURE");
  CHECK(opts[6] == &k_b[1] && !opts[7] && opts[8] == &k_b[0], "CONTROLS then SAVE");
  s_gpu = 0;
  n = vpk_menu_layout(lists, 3, g_menu_section_names, MENU_NSECTIONS, rows, opts, 16);
  CHECK(n == 8 && opts[4] == NULL, "a hidden row isn't laid out (%d lines)", n);
  s_gpu = 1;

  CHECK(!strcmp(vpk_menu_opt_text(&k_a[0], buf, sizeof buf), "ON"), "widescreen ON");
  CHECK(vpk_menu_opt_change(&k_a[0], 1) == 1 && s_ws == 0 && s_changes == 1, "widescreen toggled");
  CHECK(vpk_menu_opt_change(&k_a[0], -1) == 1 && s_ws == 1, "widescreen back");
  CHECK(vpk_menu_opt_change(&k_a[1], -1) == 1 && s_interp == 3, "interp wraps left");
  CHECK(!strcmp(vpk_menu_opt_text(&k_a[2], buf, sizeof buf), "26 MS"), "budget 26 MS");
  vpk_menu_opt_change(&k_a[2], 1), vpk_menu_opt_change(&k_a[2], 1);
  CHECK(s_budget == 0 && !strcmp(vpk_menu_opt_text(&k_a[2], buf, sizeof buf), "NEVER SKIP"),
        "budget wraps to never skip (%d)", s_budget);
  s_budget = 25; /* written by hand: shown as it is, steps to the nearest */
  CHECK(!strcmp(vpk_menu_opt_text(&k_a[2], buf, sizeof buf), "25 MS"), "budget 25 shown");
  vpk_menu_opt_change(&k_a[2], 1);
  CHECK(s_budget == 26, "budget 25 -> nearest 26 (%d)", s_budget);
  CHECK(vpk_menu_opt_change(&k_b[1], 1) == 1 && s_speed == 125, "speed +25");
  s_speed = 200;
  CHECK(vpk_menu_opt_change(&k_b[1], 1) == 0 && s_speed == 200, "speed clamps at max");
  CHECK(!strcmp(vpk_menu_opt_text(&k_b[1], buf, sizeof buf), "200%"), "speed shown with %%");
  CHECK(vpk_menu_opt_change(&k_b[0], -1) == 0 && s_actions == 0, "action not on LEFT");
  CHECK(vpk_menu_opt_change(&k_b[0], 1) == 2 && s_actions == 1, "action closes");
  CHECK(!strcmp(vpk_menu_opt_text(&k_b[0], buf, sizeof buf), "PRESS X"), "action text");

  /* config.txt: other lines kept in order, the rows' keys and the drop
   * keys replaced, the head and every key written */
  snprintf(path, sizeof path, "%s/menu_config.txt", dir ? dir : ".");
  f = fopen(path, "w");
  fputs("disc=ux0:data/x\nwidescreen=0\n# a comment\nconfig_version=1\ninterp=1\nstats=2", f);
  fclose(f);
  CHECK(vpk_menu_load(lists, 3, "interp", "2") == 1 && s_interp == 2, "load interp");
  CHECK(vpk_menu_load(lists, 3, "stats", "2") == 0, "stats isn't a row");
  CHECK(vpk_menu_load(lists, 3, "cam_speed", "75") == 1 && s_speed == 75, "load speed");
  CHECK(vpk_menu_load(lists, 3, "res", "ps1") == 1 && s_res == 1, "load res by name");
  CHECK(vpk_menu_load(lists, 3, "interp_budget", "never skip") == 1 && s_budget == 0,
        "load a name with values");
  CHECK(vpk_menu_load(lists, 3, "interp_budget", "26") == 1 && s_budget == 26, "load a number");
  {
    static const char *const drop[] = {"config_version", NULL};
    CHECK(vpk_menu_save(lists, 3, path, drop, "config_version=2\n") == 0, "save");
  }
  f = fopen(path, "r");
  while (f && fgets(line, sizeof line, f))
    strncat(text, line, sizeof text - strlen(text) - 1);
  if (f)
    fclose(f);
  CHECK(!strcmp(text, "disc=ux0:data/x\n# a comment\nstats=2\nconfig_version=2\nwidescreen=1\n"
                      "interp=2\ninterp_budget=26\ncam_speed=75\nres=1\n"),
        "saved config.txt:\n%s", text);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : NULL;
  int h = vpk_menu_panel_height(VPK_MENU_PANEL_LINES), i, lines;
  uint8_t *rgb = calloc((size_t)VPK_MENU_PANEL_W * h, 3);
  VpkMenuRow many[24];
  static char labels[24][16];
  static const VpkMenuRow rows[] = {{"PICTURE", NULL},
                                 {"WIDESCREEN", "ON"},
                                 {"60 FPS (EXPERIMENTAL)", "NO ANIM"},
                                 {"60 FPS BUDGET", "26 MS"}};

  CHECK(h <= 520, "a full panel is %d pixels high (screen 544)", h);
  lines = 4;
  vpk_menu_panel_draw(rgb, "A PORT'S OPTIONS", rows, 4, 2, 0, lines,
                  "UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE  O: CLOSE");
  CHECK(count(rgb, VPK_MENU_PANEL_W * vpk_menu_panel_height(lines), 245, 205, 70) >= 200,
        "title colour missing");
  CHECK(count(rgb, VPK_MENU_PANEL_W * vpk_menu_panel_height(lines), 58, 66, 120) >= 5000,
        "selected row bar missing");
  CHECK(rgb[0] == 200 && rgb[1] == 160 && rgb[2] == 40, "border");
  write_ppm(dir, "menu_panel.ppm", rgb, vpk_menu_panel_height(lines));

  /* a disc screen's message (D60): wrapped at spaces, '\n' kept, 32 columns */
  h = vpk_menu_panel_message(rgb, "AN ISO HAS NO MUSIC",
                         "A Game (USA).iso is a 2048-byte .iso, which has lost the "
                         "game's music.\n\nDump the disc again.",
                         "PRESS X TO EXIT");
  /* wrapped at the panel's 32 columns, then "" and "DUMP THE DISC AGAIN." */
  CHECK(h == vpk_menu_panel_height(5), "message: %d pixels high, want 5 lines", h);
  write_ppm(dir, "vpk_menu_panel_message.ppm", rgb, h);
  h = vpk_menu_panel_message(rgb, "T", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", "");
  CHECK(h == vpk_menu_panel_height(2), "message: a long word is cut, %d", h);
  h = vpk_menu_panel_height(VPK_MENU_PANEL_LINES);

  /* 24 lines (4 sections of 5 rows), 13 shown: scrolled to row 20 */
  for (i = 0; i < 24; i++) {
    if (i % 6 == 0) {
      many[i].label = g_menu_section_names[i / 6];
      many[i].value = NULL;
    } else {
      snprintf(labels[i], sizeof labels[i], "ROW %d", i);
      many[i].label = labels[i];
      many[i].value = "OFF";
    }
  }
  CHECK(vpk_menu_panel_top(24, 1, 0, 13) == 0, "top stays at 0");
  CHECK(vpk_menu_panel_top(24, 13, 0, 13) == 1, "scrolls one line down");
  CHECK(vpk_menu_panel_top(24, 23, 0, 13) == 11, "scrolls to the end");
  CHECK(vpk_menu_panel_top(24, 7, 11, 13) == 6, "scrolls up with the header above");
  CHECK(vpk_menu_panel_top(4, 3, 2, 13) == 0, "no scroll when it fits");
  memset(rgb, 0, (size_t)VPK_MENU_PANEL_W * h * 3);
  vpk_menu_panel_draw(rgb, "A PORT'S OPTIONS", many, 24, 20, vpk_menu_panel_top(24, 20, 0, 13), 13,
                  "UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE  O: CLOSE");
  write_ppm(dir, "vpk_menu_panel_scroll.ppm", rgb, h);

  test_table(dir);
  test_rewrite();
  printf("menu_test: panel %dx%d %s\n", VPK_MENU_PANEL_W, h, fails ? "FAIL" : "ok");
  free(rgb);
  return fails != 0;
}

/* The Vita options menu's host-side parts: the panel (menu_panel.c) with
 * sample rows, section headers and scrolling, checked at a few pixels and
 * written as PPMs for a look; and the option table (menu_opts.c): layout,
 * stepping, config.txt load and save (other lines kept). */
#include "menu_opts.h"

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
    fprintf(f, "P6\n%d %d\n255\n", MENU_PANEL_W, h);
    fwrite(rgb, 3, (size_t)MENU_PANEL_W * h, f);
    fclose(f);
  }
}

/* Sample rows, as present_vita.c's and a second file's. */
static int s_ws = 1, s_interp, s_budget = 26, s_speed = 100, s_changes, s_actions;
static void changed(void) { s_changes++; }
static int act(void) { return ++s_actions, 1; }
static const char *const k_interp[] = {"OFF", "ON", "NO ANIM", "CAMERA"};
static const int k_budgets[] = {0, 20, 23, 26, 30};
static const char *const k_budget_names[] = {"NEVER SKIP", "20 MS", "23 MS", "26 MS", "30 MS"};
static const char *const k_save_names[] = {"PRESS X"};
static const MenuOption k_a[] = {
    {.section = MENU_PICTURE, .label = "WIDESCREEN", .key = "widescreen", .value = &s_ws,
     .names = g_menu_off_on, .count = 2, .on_change = changed},
    {.section = MENU_PICTURE, .label = "60 FPS (EXPERIMENTAL)", .key = "interp",
     .value = &s_interp, .names = k_interp, .count = 4},
    {.section = MENU_PICTURE, .label = "60 FPS BUDGET", .key = "interp_budget",
     .value = &s_budget, .names = k_budget_names, .values = k_budgets, .count = 5,
     .fmt = "%d MS"},
};
static const MenuOption k_b[] = {
    {.section = MENU_SAVE, .label = "SAVE STATE", .names = k_save_names, .action = act},
    {.section = MENU_CONTROLS, .label = "CAMERA SPEED", .key = "cam_speed", .value = &s_speed,
     .min = 50, .max = 200, .step = 25, .fmt = "%d%%"},
};
static MENU_OPTION_LIST(s_list_a, k_a);
static MENU_OPTION_LIST(s_list_b, k_b);

static void test_table(const char *dir) {
  const MenuOptionList *lists[] = {&s_list_a, NULL, &s_list_b};
  MenuRow rows[16];
  const MenuOption *opts[16];
  char buf[32], path[512], line[128], text[1024] = "";
  int n = menu_opts_layout(lists, 3, rows, opts, 16);
  FILE *f;
  /* PICTURE header, 3 rows, CONTROLS header, speed, SAVE header, action */
  CHECK(n == 8, "layout gave %d lines", n);
  CHECK(!opts[0] && !strcmp(rows[0].label, "PICTURE") && !rows[0].value, "PICTURE header");
  CHECK(opts[1] == &k_a[0] && opts[3] == &k_a[2], "PICTURE rows in list order");
  CHECK(!opts[4] && !strcmp(rows[4].label, "CONTROLS"), "CONTROLS header after PICTURE");
  CHECK(opts[5] == &k_b[1] && !opts[6] && opts[7] == &k_b[0], "CONTROLS then SAVE");

  CHECK(!strcmp(menu_opt_text(&k_a[0], buf, sizeof buf), "ON"), "widescreen ON");
  CHECK(menu_opt_change(&k_a[0], 1) == 1 && s_ws == 0 && s_changes == 1, "widescreen toggled");
  CHECK(menu_opt_change(&k_a[0], -1) == 1 && s_ws == 1, "widescreen back");
  CHECK(menu_opt_change(&k_a[1], -1) == 1 && s_interp == 3, "interp wraps left");
  CHECK(!strcmp(menu_opt_text(&k_a[2], buf, sizeof buf), "26 MS"), "budget 26 MS");
  menu_opt_change(&k_a[2], 1), menu_opt_change(&k_a[2], 1);
  CHECK(s_budget == 0 && !strcmp(menu_opt_text(&k_a[2], buf, sizeof buf), "NEVER SKIP"),
        "budget wraps to never skip (%d)", s_budget);
  s_budget = 25; /* written by hand: shown as it is, steps to the nearest */
  CHECK(!strcmp(menu_opt_text(&k_a[2], buf, sizeof buf), "25 MS"), "budget 25 shown");
  menu_opt_change(&k_a[2], 1);
  CHECK(s_budget == 26, "budget 25 -> nearest 26 (%d)", s_budget);
  CHECK(menu_opt_change(&k_b[1], 1) == 1 && s_speed == 125, "speed +25");
  s_speed = 200;
  CHECK(menu_opt_change(&k_b[1], 1) == 0 && s_speed == 200, "speed clamps at max");
  CHECK(!strcmp(menu_opt_text(&k_b[1], buf, sizeof buf), "200%"), "speed shown with %%");
  CHECK(menu_opt_change(&k_b[0], -1) == 0 && s_actions == 0, "action not on LEFT");
  CHECK(menu_opt_change(&k_b[0], 1) == 2 && s_actions == 1, "action closes");
  CHECK(!strcmp(menu_opt_text(&k_b[0], buf, sizeof buf), "PRESS X"), "action text");

  /* config.txt: other lines kept in order, the rows' keys and the drop
   * keys replaced, the head and every key written */
  snprintf(path, sizeof path, "%s/menu_config.txt", dir ? dir : ".");
  f = fopen(path, "w");
  fputs("disc=ux0:data/x\nwidescreen=0\n# a comment\nconfig_version=1\ninterp=1\nstats=2", f);
  fclose(f);
  CHECK(menu_opts_load(lists, 3, "interp", "2") == 1 && s_interp == 2, "load interp");
  CHECK(menu_opts_load(lists, 3, "stats", "2") == 0, "stats isn't a row");
  CHECK(menu_opts_load(lists, 3, "cam_speed", "75") == 1 && s_speed == 75, "load speed");
  {
    static const char *const drop[] = {"config_version", NULL};
    CHECK(menu_opts_save(lists, 3, path, drop, "config_version=2\n") == 0, "save");
  }
  f = fopen(path, "r");
  while (f && fgets(line, sizeof line, f))
    strncat(text, line, sizeof text - strlen(text) - 1);
  if (f)
    fclose(f);
  CHECK(!strcmp(text, "disc=ux0:data/x\n# a comment\nstats=2\nconfig_version=2\nwidescreen=1\n"
                      "interp=2\ninterp_budget=26\ncam_speed=75\n"),
        "saved config.txt:\n%s", text);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : NULL;
  int h = menu_panel_height(MENU_PANEL_LINES), i, lines;
  uint8_t *rgb = calloc((size_t)MENU_PANEL_W * h, 3);
  MenuRow many[24];
  static char labels[24][16];
  static const MenuRow rows[] = {{"PICTURE", NULL},
                                 {"WIDESCREEN", "ON"},
                                 {"60 FPS (EXPERIMENTAL)", "NO ANIM"},
                                 {"60 FPS BUDGET", "26 MS"}};

  CHECK(h <= 520, "a full panel is %d pixels high (screen 544)", h);
  lines = 4;
  menu_panel_draw(rgb, "SPYRO VITA OPTIONS", rows, 4, 2, 0, lines,
                  "UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE  O: CLOSE");
  CHECK(count(rgb, MENU_PANEL_W * menu_panel_height(lines), 245, 205, 70) >= 200,
        "title colour missing");
  CHECK(count(rgb, MENU_PANEL_W * menu_panel_height(lines), 58, 66, 120) >= 5000,
        "selected row bar missing");
  CHECK(rgb[0] == 200 && rgb[1] == 160 && rgb[2] == 40, "border");
  write_ppm(dir, "menu_panel.ppm", rgb, menu_panel_height(lines));

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
  CHECK(menu_panel_top(24, 1, 0, 13) == 0, "top stays at 0");
  CHECK(menu_panel_top(24, 13, 0, 13) == 1, "scrolls one line down");
  CHECK(menu_panel_top(24, 23, 0, 13) == 11, "scrolls to the end");
  CHECK(menu_panel_top(24, 7, 11, 13) == 6, "scrolls up with the header above");
  CHECK(menu_panel_top(4, 3, 2, 13) == 0, "no scroll when it fits");
  memset(rgb, 0, (size_t)MENU_PANEL_W * h * 3);
  menu_panel_draw(rgb, "SPYRO VITA OPTIONS", many, 24, 20, menu_panel_top(24, 20, 0, 13), 13,
                  "UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE  O: CLOSE");
  write_ppm(dir, "menu_panel_scroll.ppm", rgb, h);

  test_table(dir);
  printf("menu panel %dx%d: %s\n", MENU_PANEL_W, h, fails ? "FAIL" : "ok");
  free(rgb);
  return fails != 0;
}

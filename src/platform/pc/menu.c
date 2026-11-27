/* The port's options menu on PC (decisions D275; the PS4's menu.c, itself
 * the Vita's, D30, D45, D245): hold SELECT and press START on a gamepad
 * (Back + Start) or on the keyboard (Backspace or Tab, then Enter), or press
 * Esc or F1, or a gamepad's guide button. The game stops where it is (the
 * menu runs inside its VSync wait, vblank.c) and the panel (vita-port-kit's
 * vpk_menu_panel, through src/platform/common/menu_panel.h) is shown over
 * the last picture (video.c). The rows are the common table (menu_opts.h):
 * menu_rows.c's PICTURE and CONTROLS, and the PS4's menu_game.c's GAME
 * (skipping the logos, the cheats). Changes apply at once (or, for the
 * renderer and 60 Hz logic, at the next start, which a row says) and
 * closing saves every row's key to config.txt in the user folder, keeping
 * the file's other lines. The game's code never sees the menu's buttons. */
#include "pc.h"
#include "host.h"
#include "menu_opts.h"

#include "encore/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { /* PS1 pad bits (pc.h), and the menu key (Esc, F1, guide) above them */
  B_SELECT = PSX_SELECT, B_START = PSX_START, B_UP = PSX_UP, B_RIGHT = PSX_RIGHT,
  B_DOWN = PSX_DOWN, B_LEFT = PSX_LEFT, B_TRIANGLE = PSX_TRIANGLE, B_CIRCLE = PSX_CIRCLE,
  B_CROSS = PSX_CROSS, B_MENU = 0x10000,
};

static int s_requested;
static uint16_t s_swallow; /* buttons hidden from the game until released */
static uint16_t s_prev;
static int s_prev_key;

/* pad.c, every vblank: the controls -> what the game sees. */
uint16_t pc_menu_filter(uint16_t m, int menu_key) {
  uint16_t pressed = m & ~s_prev;
  int key_pressed = menu_key && !s_prev_key;
  s_prev = m;
  s_prev_key = menu_key;
  if ((m & B_SELECT) && (pressed & B_START)) {
    s_requested = 1;
    s_swallow |= B_SELECT | B_START;
  }
  if (key_pressed)
    s_requested = 1;
  s_swallow &= m;
  return m & ~s_swallow;
}

/* The rows (menu_opts.h, D45), by section in this order within one. */
extern const MenuOptionList g_menu_pc_picture;  /* menu_rows.c */
extern const MenuOptionList g_menu_pc_controls; /* menu_rows.c */
extern const MenuOptionList g_menu_game;        /* src/platform/ps4/menu_game.c */
static const MenuOptionList *const k_lists[] = {
    &g_menu_pc_picture, &g_menu_pc_controls, &g_menu_game,
};
#define NLISTS (int)(sizeof k_lists / sizeof k_lists[0])
#define MAX_LINES 64

int pc_menu_load(const char *key, const char *v) {
  return menu_opts_load(k_lists, NLISTS, key, v);
}

static void save(void) {
  static const char *const drop[] = {NULL};
  char path[1100], line[600] = "";
  size_t n = 0;
  int l, i;
  snprintf(path, sizeof path, "%sconfig.txt", g_pc_user_dir);
  if (menu_opts_save(k_lists, NLISTS, path, drop, NULL) != 0) {
    host_log("menu: cannot write %s", path);
    return;
  }
  for (l = 0; l < NLISTS; l++)
    for (i = 0; i < k_lists[l]->n && n < sizeof line - 40; i++) {
      const MenuOption *o = &k_lists[l]->opts[i];
      if (o->key && o->value)
        n += (size_t)snprintf(line + n, sizeof line - n, " %s=%d", o->key, *o->value);
    }
  host_log("menu: saved%s", line);
  host_log_flush();
}

static int step_row(const MenuOption *const *opts, int n, int sel, int dir) {
  int i;
  for (i = 0; i < n; i++) {
    sel = (sel + n + dir) % n;
    if (opts[sel])
      break;
  }
  return sel;
}

static int relayout(MenuRow *rows, const MenuOption **opts, int *sel) {
  const MenuOption *was = opts[*sel];
  int i, m = menu_opts_layout(k_lists, NLISTS, rows, opts, MAX_LINES);
  for (i = 0; i < m && opts[i] != was; i++)
    ;
  if (i < m)
    *sel = i;
  else if (*sel < m)
    *sel = step_row(opts, m, *sel - 1, 1);
  else
    *sel = step_row(opts, m, m, -1);
  return m;
}

/* menu_test=V: what a player would press, one button per 4 passes (pressed
 * for 2, released for 2), a shot of the window after each change:
 * WIDESCREEN off, then up past the first row to the last (the list
 * scrolls), then close. menu_keys=K1,K2,... (up, down, left, right, cross,
 * circle, triangle, start, select, menu, none) presses those instead. */
#define MAX_TEST 64
static uint32_t k_test[MAX_TEST] = {0, B_DOWN, B_RIGHT, B_UP, B_UP, B_CIRCLE};
static int NTEST = 6;

static void test_keys(void) {
  static const struct {
    const char *name;
    uint32_t bit;
  } k_names[] = {{"up", B_UP},         {"down", B_DOWN},         {"left", B_LEFT},
                 {"right", B_RIGHT},   {"cross", B_CROSS},       {"circle", B_CIRCLE},
                 {"triangle", B_TRIANGLE}, {"start", B_START},   {"select", B_SELECT},
                 {"menu", B_MENU},     {"none", 0}};
  char buf[sizeof g_pc_opts.menu_keys], *tok, *next;
  size_t i;
  if (!g_pc_opts.menu_keys[0])
    return;
  snprintf(buf, sizeof buf, "%s", g_pc_opts.menu_keys);
  NTEST = 0;
  k_test[NTEST++] = 0; /* what opened it is released first */
  for (tok = buf; tok && *tok && NTEST < MAX_TEST; tok = next) {
    if ((next = strchr(tok, ',')) != NULL)
      *next++ = 0;
    for (i = 0; i < sizeof k_names / sizeof k_names[0] && strcmp(tok, k_names[i].name); i++)
      ;
    if (i == sizeof k_names / sizeof k_names[0])
      host_log("menu: menu_keys: no key %s", tok);
    else
      k_test[NTEST++] = k_names[i].bit;
  }
  g_pc_opts.menu_keys[0] = 0;
}

#define PASS_US 16667 /* the menu's pace: about 60 passes a second */

/* menu_test: the window with the panel as it is now, as a PNG */
static void test_shot(int v, int n) {
  char path[1200];
  int i;
  snprintf(path, sizeof path, "%sshots/menu-%d-%d.png", g_pc_user_dir, v, n);
  pc_video_shot(path, 0);
  for (i = 0; i < 200 && pc_video_shot_pending() && !g_pc_quit; i++)
    enc_sleep_us(10000);
}

/* vblank.c, at each VSync: runs the menu if it was asked for. Returns 1 if
 * it ran (the game was paused). */
int pc_menu_service(void) {
  static uint8_t *panel;
  static MenuRow rows[MAX_LINES];
  static const MenuOption *opts[MAX_LINES];
  static char texts[MAX_LINES][32];
  int n, lines, h, sel, top = 0, dirty = 1, open = 1;
  uint32_t prev = 0xFFFFFFFF; /* what opened the menu is already held */
  int test = 0, test_v = 0, nshot = 0, step = 0, i;
  unsigned long long t0;
  for (i = 0; i < g_pc_opts.nmenu_test; i++)
    if (g_pc_opts.menu_test[i] > 0 && host_vblank_count() >= g_pc_opts.menu_test[i]) {
      test_v = g_pc_opts.menu_test[i];
      g_pc_opts.menu_test[i] = 0;
      s_requested = test = 1;
      test_keys();
      break;
    }
  if (!s_requested)
    return 0;
  s_requested = 0;
  for (i = 0; i < NLISTS; i++)
    if (k_lists[i]->on_open)
      k_lists[i]->on_open();
  n = menu_opts_layout(k_lists, NLISTS, rows, opts, MAX_LINES);
  if (n == 0)
    return 0;
  lines = n < MENU_PANEL_LINES ? n : MENU_PANEL_LINES;
  h = menu_panel_height(lines);
  sel = step_row(opts, n, n - 1, 1); /* the first row */
  if (!panel)
    panel = malloc((size_t)MENU_PANEL_W * menu_panel_height(MENU_PANEL_LINES) * 3);
  if (!panel)
    return 0;
  t0 = pc_now_us();
  host_log("menu: open%s", test ? " (menu_test)" : "");
  host_log_flush();
  while (open && !g_pc_quit) {
    uint32_t m, pressed;
    uint16_t b;
    unsigned char sticks[4];
    int changed = 0;
    pc_input_read(&b, sticks);
    m = b | (pc_input_menu_key() ? B_MENU : 0);
    if (test) {
      m = step / 4 < NTEST && step % 4 < 2 ? k_test[step / 4] : 0;
      if (step / 4 >= NTEST)
        m = B_CIRCLE; /* the sequence's end: close */
      step++;
    }
    pressed = m & ~prev;
    prev = m;
    if (pressed & B_UP)
      sel = step_row(opts, n, sel, -1), dirty = 1;
    if (pressed & B_DOWN)
      sel = step_row(opts, n, sel, 1), dirty = 1;
    if (opts[sel] && (pressed & (B_RIGHT | B_CROSS)))
      open = menu_opt_change(opts[sel], 1) != 2, dirty = changed = 1;
    if (opts[sel] && (pressed & B_LEFT))
      menu_opt_change(opts[sel], -1), dirty = changed = 1;
    if (changed) { /* rows may have come or gone */
      n = relayout(rows, opts, &sel);
      lines = n < MENU_PANEL_LINES ? n : MENU_PANEL_LINES;
      h = menu_panel_height(lines);
    }
    if (pressed & (B_CIRCLE | B_START | B_TRIANGLE | B_MENU))
      open = 0;
    if (dirty && open) {
      for (i = 0; i < n; i++)
        if (opts[i])
          rows[i].value = menu_opt_text(opts[i], texts[i], sizeof texts[i]);
      top = menu_panel_top(n, sel, top, lines);
      menu_panel_draw(panel, "SPYRO PC OPTIONS", rows, n, sel, top, lines,
                      "UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE  ESC: CLOSE");
      pc_video_overlay(panel, MENU_PANEL_W, h);
      dirty = 0;
      if (test)
        test_shot(test_v, nshot++);
    }
    if (open)
      enc_sleep_us(PASS_US); /* the main thread keeps showing the last picture under it */
  }
  pc_video_overlay(NULL, 0, 0);
  /* what closed it stays away from the game until released */
  s_swallow = (uint16_t)prev;
  s_prev = (uint16_t)prev;
  s_prev_key = (prev & B_MENU) != 0;
  save();
  host_log("menu: closed after %u ms", (unsigned)((pc_now_us() - t0) / 1000));
  return 1;
}

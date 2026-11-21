/* The port's options menu on the PS4 (decisions D30, D45, D194), the
 * Vita's menu.c with the DualShock 4: hold the touch pad's click (the PS1's
 * SELECT, pad.c) and press OPTIONS (START), or, with touch_menu=1, tap the
 * touch pad. The game stops where it is (the menu runs inside its VSync
 * wait, vblank.c) and the panel (vita-port-kit's vpk_menu_panel, through
 * src/platform/common/menu_panel.h) is shown over the last picture. The
 * rows are the common table (menu_opts.h): present_ps4.c's PICTURE rows,
 * pad.c's CONTROLS, menu_game.c's GAME, savestate_ps4.c's SAVE. Changes
 * apply at once and closing saves every row's key to config.txt, keeping
 * the file's other lines. The game's code never sees the menu's buttons. */
#include "ps4.h"
#include "host.h"
#include "menu_opts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { /* PS1 pad bits (pad.c) */
  B_SELECT = 0x0001, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040,
  B_LEFT = 0x0080, B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000, B_CROSS = 0x4000,
};

#define CONFIG PS4_DATA_DIR "/config.txt"

static int s_requested;
static uint16_t s_swallow; /* buttons hidden from the game until released */
static uint16_t s_prev;

static const Ps4MenuAsk *s_ask;
static int s_ask_sel;

void ps4_menu_ask(const Ps4MenuAsk *ask) {
  s_ask = ask;
  s_ask_sel = 0;
}

void ps4_menu_request(void) { s_requested = 1; }

/* pad.c, every vblank: the controller's buttons -> what the game sees. */
uint16_t ps4_menu_filter(uint16_t m) {
  uint16_t pressed = m & ~s_prev;
  s_prev = m;
  if ((m & B_SELECT) && (pressed & B_START)) {
    s_requested = 1;
    s_swallow |= B_SELECT | B_START;
  }
  s_swallow &= m;
  return m & ~s_swallow;
}

/* The rows (menu_opts.h, D45), by section in this order within one. Strong
 * references, not the Vita's weak ones: an undefined weak symbol becomes an
 * import create-fself refuses (D180). */
extern const MenuOptionList g_menu_present;  /* present_ps4.c */
extern const MenuOptionList g_menu_controls; /* pad.c */
extern const MenuOptionList g_menu_game;     /* menu_game.c */
extern const MenuOptionList g_menu_save;     /* savestate_ps4.c */
static const MenuOptionList *const k_lists[] = {
    &g_menu_present, &g_menu_controls, &g_menu_game, &g_menu_save,
};
#define NLISTS (int)(sizeof k_lists / sizeof k_lists[0])
#define MAX_LINES 64

int ps4_menu_load(const char *key, const char *v) {
  return menu_opts_load(k_lists, NLISTS, key, v);
}

static void save(void) {
  const char *drop[1] = {"config_version"};
  char line[512] = "";
  size_t n = 0;
  int l, i;
  if (menu_opts_save(k_lists, NLISTS, CONFIG, drop, "config_version=5\n") != 0) {
    host_log("menu: cannot write %s", CONFIG);
    return;
  }
  for (l = 0; l < NLISTS; l++)
    for (i = 0; k_lists[l] && i < k_lists[l]->n && n < sizeof line - 40; i++) {
      const MenuOption *o = &k_lists[l]->opts[i];
      if (o->key && o->value)
        n += (size_t)snprintf(line + n, sizeof line - n, " %s=%d", o->key, *o->value);
    }
  host_log("menu: saved%s", line);
  host_log_flush();
}

void ps4_menu_quit(void) {
  save();
  host_log("menu: closing the game (start it again from the home screen)");
  ps4_exit(0);
}

static int ask_draw(uint8_t *panel) {
  MenuRow rows[8];
  int n = 0, i, first;
  for (i = 0; i < 3 && s_ask->text[i]; i++)
    rows[n].label = s_ask->text[i], rows[n++].value = "";
  rows[n].label = "", rows[n++].value = "";
  first = n;
  for (i = 0; i < s_ask->nchoice; i++)
    rows[n].label = s_ask->choice[i], rows[n++].value = s_ask->hint[i] ? s_ask->hint[i] : "";
  menu_panel_draw(panel, s_ask->title, rows, n, first + s_ask_sel, 0, n, s_ask->footer);
  return menu_panel_height(n);
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

/* menu_test=V: what a player would press, one button per 4 pictures
 * (pressed for 2, released for 2): 60 fps one step right, widescreen
 * toggled twice, close. */
static const uint16_t k_test[] = {0, B_DOWN, B_RIGHT, B_UP, B_RIGHT, B_RIGHT, B_CIRCLE};
#define NTEST (int)(sizeof k_test / sizeof k_test[0])

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

/* vblank.c, at each VSync: runs the menu if it was asked for. Returns 1 if
 * it ran (the game was paused). */
int ps4_menu_service(void) {
  static uint8_t *panel;
  static MenuRow rows[MAX_LINES];
  static const MenuOption *opts[MAX_LINES];
  static char texts[MAX_LINES][32];
  int n, lines, h, sel, top = 0, dirty = 1, open = 1;
  uint16_t prev = 0xFFFF; /* what opened the menu is already held */
  int test = 0, step = 0, i;
  unsigned long long t0;
  for (i = 0; i < g_ps4_opts.nmenu_test; i++)
    if (g_ps4_opts.menu_test[i] > 0 && ps4_vblank_count() >= g_ps4_opts.menu_test[i]) {
      g_ps4_opts.menu_test[i] = 0;
      s_requested = test = 1;
      break;
    }
  if (!s_requested)
    return 0;
  s_requested = 0;
  for (i = 0; i < NLISTS; i++)
    if (k_lists[i] && k_lists[i]->on_open)
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
  t0 = ps4_now_us();
  ps4_pad_take_tap(); /* the tap that opened it, if one did */
  host_log("menu: open%s", test ? " (menu_test)" : "");
  while (open) {
    uint16_t m, pressed;
    /* a tap closes it, but not in its first 300 ms (GPU run 1: the menu a
     * tap opened closed 47 ms later) */
    int tap = ps4_pad_take_tap() && ps4_now_us() - t0 > 300000;
    m = ps4_pad_read();
    if (test) {
      m = step / 4 < NTEST && step % 4 < 2 ? k_test[step / 4] : 0;
      if (step / 4 >= NTEST)
        m = B_CIRCLE; /* the sequence's end: close */
      step++;
    }
    pressed = m & ~prev;
    prev = m;
    if (s_ask) { /* a question (D136): its choices, or O for none */
      int answer = -2;
      if (pressed & (B_UP | B_DOWN))
        s_ask_sel = (s_ask_sel + (pressed & B_UP ? s_ask->nchoice - 1 : 1)) % s_ask->nchoice,
        dirty = 1;
      if (pressed & B_CROSS)
        answer = s_ask_sel;
      if ((pressed & (B_CIRCLE | B_START | B_TRIANGLE)) || tap)
        answer = -1;
      if (answer != -2) {
        const Ps4MenuAsk *a = s_ask;
        s_ask = NULL;
        host_log("menu: \"%s\": %s", a->title, answer < 0 ? "none (O)" : a->choice[answer]);
        a->done(answer); /* may not return: QUIT NOW */
        n = relayout(rows, opts, &sel);
        lines = n < MENU_PANEL_LINES ? n : MENU_PANEL_LINES;
        h = menu_panel_height(lines);
        dirty = 1;
      }
      pressed = 0;
    }
    if (pressed & B_UP)
      sel = step_row(opts, n, sel, -1), dirty = 1;
    if (pressed & B_DOWN)
      sel = step_row(opts, n, sel, 1), dirty = 1;
    if (opts[sel] && (pressed & (B_RIGHT | B_CROSS)))
      open = menu_opt_change(opts[sel], 1) != 2, dirty = 1;
    if (opts[sel] && (pressed & B_LEFT))
      menu_opt_change(opts[sel], -1), dirty = 1;
    if (pressed & (B_RIGHT | B_CROSS | B_LEFT)) { /* rows may have come or gone */
      n = relayout(rows, opts, &sel);
      lines = n < MENU_PANEL_LINES ? n : MENU_PANEL_LINES;
      h = menu_panel_height(lines);
    }
    if (!s_ask && ((pressed & (B_CIRCLE | B_START | B_TRIANGLE)) || tap))
      open = 0;
    if (dirty && open && s_ask) {
      ps4_video_overlay(panel, MENU_PANEL_W, ask_draw(panel));
      dirty = 0;
    }
    if (dirty && open) {
      for (i = 0; i < n; i++)
        if (opts[i])
          rows[i].value = menu_opt_text(opts[i], texts[i], sizeof texts[i]);
      top = menu_panel_top(n, sel, top, lines);
      menu_panel_draw(panel, "SPYRO PS4 OPTIONS", rows, n, sel, top, lines,
                      "UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE  O: CLOSE");
      ps4_video_overlay(panel, MENU_PANEL_W, h);
      dirty = 0;
    }
    if (open) {
      ps4_video_present(); /* the software path waits for the last flip: the display's pace */
      if (g_ps4_gpu)
        ps4_video_wait_vblank();
    }
  }
  ps4_video_overlay(NULL, 0, 0);
  ps4_video_present();
  s_swallow = prev; /* what closed it stays away from the game until released */
  s_prev = prev;
  save();
  host_log("menu: closed after %u ms", (unsigned)((ps4_now_us() - t0) / 1000));
  return 1;
}

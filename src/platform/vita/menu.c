/* The port's options menu on the Vita (docs/presentation.md, decisions D30,
 * D45): hold SELECT and press START. The game stops where it is (the menu
 * runs inside its VSync wait, vblank.c) and the panel (src/platform/common/
 * menu_panel.c) is shown over the last picture. The rows are a table
 * (src/platform/common/menu_opts.h) that feature files add to; changes apply
 * at once and closing saves every row's key to config.txt. The game's code
 * is untouched: it never sees the menu's buttons. */
#include "vita.h"
#include "menu_opts.h"

#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { /* PS1 pad bits (pad.c) */
  B_SELECT = 0x0001, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040,
  B_LEFT = 0x0080, B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000, B_CROSS = 0x4000,
};

#define CONFIG VITA_DATA_DIR "/config.txt"

static int s_requested;
static uint16_t s_swallow; /* buttons hidden from the game until released */
static uint16_t s_prev;

int vita_pad_take_tap(void); /* pad.c: a tap on the front touch screen */

/* pad.c: a tap in the middle of the front touch screen (D46). */
void vita_menu_request(void) { s_requested = 1; }

/* pad.c, every vblank: the controller's buttons -> what the game sees. */
uint16_t vita_menu_filter(uint16_t m) {
  uint16_t pressed = m & ~s_prev;
  s_prev = m;
  if ((m & B_SELECT) && (pressed & B_START)) {
    s_requested = 1;
    s_swallow |= B_SELECT | B_START;
  }
  s_swallow &= m;
  return m & ~s_swallow;
}

/* The rows (menu_opts.h, D45): each feature file defines its own list, and
 * the menu shows them by section (PICTURE, CONTROLS, GAME, SAVE), in this
 * order within a section. A list that isn't linked in is a null weak
 * reference and is skipped, so a feature file adds its rows by defining one
 * of these names without editing this file. g_menu_present comes first:
 * menu_test's sequence starts on its rows. */
extern const MenuOptionList g_menu_present;                         /* present_vita.c */
extern const MenuOptionList g_menu_picture __attribute__((weak));   /* picture, draw distance */
extern const MenuOptionList g_menu_controls __attribute__((weak));  /* pad.c */
extern const MenuOptionList g_menu_game __attribute__((weak));      /* cheats, logos */
extern const MenuOptionList g_menu_save __attribute__((weak));      /* quick save */
static const MenuOptionList *const k_lists[] = {
    &g_menu_present, &g_menu_picture, &g_menu_controls, &g_menu_game, &g_menu_save,
};
#define NLISTS (int)(sizeof k_lists / sizeof k_lists[0])
#define MAX_LINES 64

/* main.c's read_config: a menu row's key=value. Returns 1 if one took it. */
int vita_menu_load(const char *key, const char *v) {
  return menu_opts_load(k_lists, NLISTS, key, v);
}

/* config.txt with the rows' keys replaced (other lines kept). */
static void save(void) {
  static const char *const drop[] = {"config_version", NULL};
  char line[512] = "";
  size_t n = 0;
  int l, i;
  /* config_version=2: this interp= is the player's (main.c, read_config; D44) */
  if (menu_opts_save(k_lists, NLISTS, CONFIG, drop, "config_version=2\n") != 0) {
    vita_log("menu: cannot write %s", CONFIG);
    return;
  }
  for (l = 0; l < NLISTS; l++)
    for (i = 0; k_lists[l] && i < k_lists[l]->n && n < sizeof line - 40; i++) {
      const MenuOption *o = &k_lists[l]->opts[i];
      if (o->key && o->value)
        n += (size_t)snprintf(line + n, sizeof line - n, " %s=%d", o->key, *o->value);
    }
  vita_log("menu: saved%s", line);
}

/* The next row from line `sel` in direction dir, skipping headers. */
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
 * (pressed for 2, released for 2), after letting go of the combo: 60 fps
 * one step right (off -> on from the default), widescreen toggled, close
 * (the first two rows: g_menu_present's). */
static const uint16_t k_test[] = {0, B_DOWN, B_RIGHT, B_UP, B_RIGHT, B_CIRCLE};
#define NTEST (int)(sizeof k_test / sizeof k_test[0])

/* vblank.c, at each VSync: runs the menu if it was asked for. Returns 1 if
 * it ran (the game was paused). */
int vita_menu_service(void) {
  static uint8_t *panel;
  static MenuRow rows[MAX_LINES];
  static const MenuOption *opts[MAX_LINES];
  static char texts[MAX_LINES][32];
  int n, lines, h, sel, top = 0, dirty = 1, open = 1;
  uint16_t prev = 0xFFFF; /* what opened the menu is already held */
  int test = 0, step = 0, i;
  for (i = 0; i < g_vita_opts.nmenu_test; i++)
    if (g_vita_opts.menu_test[i] > 0 && vita_vblank_count() >= g_vita_opts.menu_test[i]) {
      g_vita_opts.menu_test[i] = 0;
      s_requested = test = 1;
      break;
    }
  if (!s_requested)
    return 0;
  s_requested = 0;
  n = menu_opts_layout(k_lists, NLISTS, rows, opts, MAX_LINES);
  if (n == 0)
    return 0;
  lines = n < MENU_PANEL_LINES ? n : MENU_PANEL_LINES;
  h = menu_panel_height(lines);
  sel = step_row(opts, n, n - 1, 1); /* the first row */
  if (!panel)
    panel = malloc((size_t)MENU_PANEL_W * menu_panel_height(MENU_PANEL_LINES) * 3);
  SceUInt64 t0 = sceKernelGetProcessTimeWide();
  vita_pad_take_tap(); /* the tap that opened it, if one did */
  vita_log("menu: open%s", test ? " (menu_test)" : "");
  while (open) {
    uint16_t m, pressed;
    vita_pad_poll();
    m = vita_pad_read();
    if (test) {
      m = step / 4 < NTEST && step % 4 < 2 ? k_test[step / 4] : 0;
      step++;
    }
    pressed = m & ~prev;
    prev = m;
    if (pressed & B_UP)
      sel = step_row(opts, n, sel, -1), dirty = 1;
    if (pressed & B_DOWN)
      sel = step_row(opts, n, sel, 1), dirty = 1;
    if (opts[sel] && (pressed & (B_RIGHT | B_CROSS)))
      open = menu_opt_change(opts[sel], 1) != 2, dirty = 1;
    if (opts[sel] && (pressed & B_LEFT))
      menu_opt_change(opts[sel], -1), dirty = 1;
    if ((pressed & (B_CIRCLE | B_START | B_TRIANGLE)) || vita_pad_take_tap())
      open = 0; /* or a tap again */
    if (dirty && open) {
      for (i = 0; i < n; i++)
        if (opts[i])
          rows[i].value = menu_opt_text(opts[i], texts[i], sizeof texts[i]);
      top = menu_panel_top(n, sel, top, lines);
      menu_panel_draw(panel, "SPYRO VITA OPTIONS", rows, n, sel, top, lines,
                      "UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE  O: CLOSE");
      vita_video_overlay(panel, MENU_PANEL_W, h);
      dirty = 0;
    }
    if (open) {
      vita_video_force_redraw();
      vita_video_present(); /* waits for the display's vblank */
    }
  }
  vita_video_overlay(NULL, 0, 0);
  vita_video_force_redraw();
  vita_video_present();
  s_swallow = prev; /* what closed it stays away from the game until released */
  s_prev = prev;
  save();
  /* the game was paused this long: the next stats line's vblank rate and
   * presents include it */
  vita_log("menu: closed after %u ms", (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000));
  return 1;
}

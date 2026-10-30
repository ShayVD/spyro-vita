/* The port's options menu on the Vita (docs/presentation.md, decisions D30):
 * hold SELECT and press START. The game stops where it is (the menu runs
 * inside its VSync wait, vblank.c) and the panel (src/platform/common/
 * menu_panel.c) is shown over the last picture. Widescreen, 60 fps and the
 * 60 fps budget change at once; closing saves them to config.txt. The game's
 * code is untouched: it never sees the menu's buttons. */
#include "vita.h"
#include "../../game/present.h"
#include "menu_panel.h"

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

static const int k_budgets[] = {0, 20, 23, 26, 30}; /* ms; 0: never skip */
#define NBUDGETS (int)(sizeof k_budgets / sizeof k_budgets[0])
#define NROWS 3

static void rows_for(MenuRow *rows, char *budget, size_t budget_len) {
  /* short enough to fit beside the label when selected ("< NO ANIM >") */
  static const char *const k_interp[] = {"OFF", "ON", "NO ANIM", "CAMERA"};
  int i = g_vita_opts.interp;
  rows[0].label = "WIDESCREEN";
  rows[0].value = g_vita_opts.widescreen ? "ON" : "OFF";
  rows[1].label = "60 FPS (EXPERIMENTAL)"; /* off by default (D44) */
  rows[1].value = k_interp[i >= 0 && i <= 3 ? i : 0];
  rows[2].label = "60 FPS BUDGET";
  if (g_vita_opts.interp_budget > 0)
    snprintf(budget, budget_len, "%d MS", g_vita_opts.interp_budget);
  else
    snprintf(budget, budget_len, "NEVER SKIP");
  rows[2].value = budget;
}

static void change(int row, int dir) {
  int i;
  if (row == 0) {
    g_vita_opts.widescreen = !g_vita_opts.widescreen;
  } else if (row == 1) {
    g_vita_opts.interp = (g_vita_opts.interp + 4 + dir) % 4;
  } else {
    for (i = 0; i < NBUDGETS && k_budgets[i] != g_vita_opts.interp_budget; i++)
      ;
    i = i == NBUDGETS ? 3 : (i + NBUDGETS + dir) % NBUDGETS;
    g_vita_opts.interp_budget = k_budgets[i];
  }
  vita_present_apply();
  if (row == 0 && g_vita_gpu)
    vita_gpu_rescale(); /* targets at the new width */
}

/* config.txt with this menu's keys replaced (other lines kept). */
static void save(void) {
  char *keep = NULL, line[512];
  size_t n = 0, cap = 0, len;
  FILE *f = fopen(CONFIG, "r");
  if (f) {
    while (fgets(line, sizeof line, f)) {
      if (!strncmp(line, "widescreen=", 11) || !strncmp(line, "interp=", 7) ||
          !strncmp(line, "interp_budget=", 14) || !strncmp(line, "config_version=", 15))
        continue;
      len = strlen(line);
      if (n + len + 1 > cap) {
        cap = (n + len + 1) * 2;
        keep = realloc(keep, cap);
      }
      memcpy(keep + n, line, len);
      n += len;
      if (len && line[len - 1] != '\n')
        keep[n++] = '\n';
    }
    fclose(f);
  }
  f = fopen(CONFIG, "w");
  if (!f) {
    vita_log("menu: cannot write %s", CONFIG);
    free(keep);
    return;
  }
  if (n)
    fwrite(keep, 1, n, f);
  /* config_version=2: this interp= is the player's (main.c, read_config) */
  fprintf(f, "config_version=2\nwidescreen=%d\ninterp=%d\ninterp_budget=%d\n",
          g_vita_opts.widescreen, g_vita_opts.interp, g_vita_opts.interp_budget);
  fclose(f);
  free(keep);
  vita_log("menu: saved widescreen=%d interp=%d interp_budget=%d", g_vita_opts.widescreen,
           g_vita_opts.interp, g_vita_opts.interp_budget);
}

/* menu_test=V: what a player would press, one button per 4 pictures
 * (pressed for 2, released for 2), after letting go of the combo: 60 fps
 * one step right (off -> on from the default), widescreen toggled, close. */
static const uint16_t k_test[] = {0, B_DOWN, B_RIGHT, B_UP, B_RIGHT, B_CIRCLE};
#define NTEST (int)(sizeof k_test / sizeof k_test[0])

/* vblank.c, at each VSync: runs the menu if it was asked for. Returns 1 if
 * it ran (the game was paused). */
int vita_menu_service(void) {
  static uint8_t *panel;
  int h = menu_panel_height(NROWS), sel = 0, dirty = 1, open = 1;
  uint16_t prev = 0xFFFF; /* what opened the menu is already held */
  MenuRow rows[NROWS];
  char budget[32];
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
  if (!panel)
    panel = malloc((size_t)MENU_PANEL_W * h * 3);
  SceUInt64 t0 = sceKernelGetProcessTimeWide();
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
      sel = (sel + NROWS - 1) % NROWS, dirty = 1;
    if (pressed & B_DOWN)
      sel = (sel + 1) % NROWS, dirty = 1;
    if (pressed & (B_RIGHT | B_CROSS))
      change(sel, 1), dirty = 1;
    if (pressed & B_LEFT)
      change(sel, -1), dirty = 1;
    if (pressed & (B_CIRCLE | B_START | B_TRIANGLE))
      open = 0;
    if (dirty && open) {
      rows_for(rows, budget, sizeof budget);
      menu_panel_draw(panel, "SPYRO VITA OPTIONS", rows, NROWS, sel,
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

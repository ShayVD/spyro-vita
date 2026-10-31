/* The options menu's GAME rows (decisions D45, D50): skipping the intro
 * logos, and the game's own cheats (src/game/cheats_port.c). Cheats are
 * actions, never saved: ✕ once asks, ✕ again applies and closes the menu,
 * and the game then takes the cheat in its inventory, as if its code had
 * been typed there. They are offered only in a level (playing, paused or in
 * the inventory), never in an attract demo or at the title. */
#include "vita.h"
#include "menu_opts.h"
#include "../../game/cheats_port.h"

#include <stdio.h>

enum { ROW_LIVES, ROW_LEVELS, ROW_CREDITS, ROW_WARP, NCHEATS };

static const int k_cheat[NCHEATS] = {PORT_CHEAT_LIVES, PORT_CHEAT_LEVELS, PORT_CHEAT_CREDITS,
                                     PORT_CHEAT_WARP};
/* the value text each cheat row shows (names[0]), set on open and on ✕ */
static const char *s_text[NCHEATS][1];
static int s_armed = -1; /* the row whose ✕ asked; ✕ again applies */
static int s_warp;       /* index into port_warp_level() */
static char s_warp_names[40][24];
static const char *s_warp_name_ptrs[40];

static const char *idle_text(int row) {
  (void)row;
  return port_cheat_available() ? "X: APPLY" : "IN A LEVEL";
}

static void on_open(void) {
  int i, n = port_warp_count();
  s_armed = -1;
  for (i = 0; i < NCHEATS; i++)
    s_text[i][0] = idle_text(i);
  for (i = 0; i < n && i < 40; i++)
    s_warp_name_ptrs[i] =
        port_level_name(port_warp_level(i), s_warp_names[i], sizeof s_warp_names[i]);
}

static int cheat_action(int row) {
  int i;
  if (!port_cheat_available()) {
    s_text[row][0] = "IN A LEVEL";
    vita_log("menu: cheats only in a level (playing, paused or in the inventory)");
    return 0;
  }
  if (s_armed != row) { /* first ✕: ask */
    for (i = 0; i < NCHEATS; i++)
      s_text[i][0] = idle_text(i);
    s_armed = row;
    /* LEVELS OPEN is written to the memory card with the next save */
    s_text[row][0] = row == ROW_LEVELS ? "PERMANENT? X" : "X AGAIN: YES";
    return 0;
  }
  s_armed = -1;
  s_text[row][0] = idle_text(row);
  return port_cheat_request(k_cheat[row], port_warp_level(s_warp)); /* 1: close */
}

static int lives(void) { return cheat_action(ROW_LIVES); }
static int levels(void) { return cheat_action(ROW_LEVELS); }
static int credits(void) { return cheat_action(ROW_CREDITS); }
static int warp(void) { return cheat_action(ROW_WARP); }

/* The warp's target changed: a ✕ that asked about the old one is undone. */
static void warp_target_changed(void) {
  if (s_armed == ROW_WARP) {
    s_armed = -1;
    s_text[ROW_WARP][0] = idle_text(ROW_WARP);
  }
}

static const MenuOption k_rows[] = {
    /* takes effect at the next start: Initialize() has run by now */
    {.section = MENU_GAME, .label = "SKIP INTRO LOGOS", .key = "skip_logos",
     .value = &g_port_skip_logos, .names = g_menu_off_on, .count = 2},
    {.section = MENU_GAME, .label = "99 LIVES", .names = s_text[ROW_LIVES], .action = lives},
    {.section = MENU_GAME, .label = "LEVELS OPEN", .names = s_text[ROW_LEVELS], .action = levels},
    {.section = MENU_GAME, .label = "CREDITS", .names = s_text[ROW_CREDITS], .action = credits},
    {.section = MENU_GAME, .label = "WARP TO", .value = &s_warp, .names = s_warp_name_ptrs,
     .count = 35, .on_change = warp_target_changed},
    {.section = MENU_GAME, .label = "LEVEL WARP", .names = s_text[ROW_WARP], .action = warp},
};
const MenuOptionList g_menu_game = {k_rows, (int)(sizeof k_rows / sizeof k_rows[0]), on_open};

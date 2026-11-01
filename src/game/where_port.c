/* Where the player is (decisions D73): the level and gamestate the Vita's
 * log names at every change and in a hang line, read from the game's own
 * globals (common.h, loaders.h, gamepad.h). */
#include "where_port.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"
#include "gamepad.h"
#include "loaders.h"

void port_where(PortWhere *w) {
  w->level = g_LevelId;
  w->gamestate = g_Gamestate;
  w->load_stage = g_LoadStage;
  w->demo = g_DemoMode;
}

/* GS_* in common.h's order */
static const char *const k_states[] = {
    "playing",   "level transition", "pause menu",     "inventory", "respawn",
    "game over", "old dragon",       "flight results", "dragon",    "entrance animation",
    "exit level", "fairy",           "balloonist",     "title screen", "cutscene",
    "credits"};

const char *port_gamestate_name(int gamestate) {
  if (gamestate < 0 || gamestate >= (int)(sizeof k_states / sizeof k_states[0]))
    return "unknown";
  return k_states[gamestate];
}

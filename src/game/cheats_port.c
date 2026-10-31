/* The game's own cheats from the port's options menu (decisions D50).
 * Spyro reads cheat codes only in the inventory (func_8002EB2C calls
 * CheatsProcess, cheats.c). A queued cheat is applied there, through the
 * game's code, as if its code had just been typed:
 *   99 LIVES, LEVELS OPEN, CREDITS: CheatApplyEffect(4/3/2) and
 *     CheatResetBuffer(), what CheatsProcess does on a match;
 *   LEVEL WARP: g_LevelCheatActive and the world digit set, then
 *     CheatProcessLevelWarp() with the level digit as the pad's press, what
 *     the inventory runs while g_LevelCheatActive (no code sets it in the
 *     retail game; the host's --tour uses it, D31).
 * From play or the pause menu the game opens the inventory first, by the
 * call its SELECT button makes (func_8002C714). The hooks are in
 * patches/spyro-1-port/0008-cheats-from-options-menu.patch. A cheat left
 * pending is dropped when the game leaves play, pause and inventory (death,
 * a level load, a demo) or after 10 seconds. */
#include "cheats_port.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdio.h>

#include "cheats.h"
#include "common.h"
#include "gamepad.h"
#include "loaders.h"
#include "strings.h"

extern int cheat_HomeworldSelected; /* cheats.c */
void CheatApplyEffect(int pCheatId);  /* cheats.c (not in cheats.h) */

int g_port_skip_logos;

#define PENDING_TICKS 300 /* game ticks (30 per second) */

static int s_pending, s_level, s_age;
static void (*s_log)(const char *fmt, ...);

void port_cheats_set_log(void (*log)(const char *fmt, ...)) { s_log = log; }

static const char *const k_names[] = {"", "99 LIVES", "LEVELS OPEN", "CREDITS", "LEVEL WARP"};

/* CheatProcessLevelWarp's digits: CIRCLE 0, CROSS 1, ... LEFT 6 */
static const int k_digit[] = {PAD_CIRCLE, PAD_CROSS, PAD_SQUARE, PAD_TRIANGLE,
                              PAD_RIGHT,  PAD_DOWN,  PAD_LEFT};

static int playing_state(void) {
  return g_Gamestate == GS_Playing || g_Gamestate == GS_PauseMenu ||
         g_Gamestate == GS_InventoryMenu;
}

int port_cheat_available(void) {
  return playing_state() && !g_DemoMode && g_LoadStage < 0 && !g_LevelCheatActive &&
         g_LevelId >= 10 && g_LevelId < 65;
}

int port_warp_count(void) { return 5 * 6 + 5; }

int port_warp_level(int index) {
  if (index < 0 || index >= port_warp_count())
    return 10;
  return (index / 6 + 1) * 10 + index % 6;
}

const char *port_level_name(int level, char *buf, size_t len) {
  int world = level / 10, n = level % 10;
  if (world < 1 || world > 6 || n > 5 || level >= 65)
    snprintf(buf, len, "LEVEL %d", level);
  else if (n == 0 && world < 6)
    snprintf(buf, len, "%s HOME", g_HomeworldNames[world - 1]);
  else
    snprintf(buf, len, "%s", g_LevelNames[(world - 1) * 6 + n]);
  return buf;
}

int port_cheat_request(int cheat, int level) {
  char name[48];
  if (cheat <= PORT_CHEAT_NONE || cheat > PORT_CHEAT_WARP || !port_cheat_available())
    return 0;
  if (cheat == PORT_CHEAT_WARP && (level < 10 || level >= 65 || level % 10 > 5))
    return 0;
  s_pending = cheat;
  s_level = level;
  s_age = 0;
  if (s_log)
    s_log("cheats: %s%s%s queued (gamestate %d, level %d)", k_names[cheat],
          cheat == PORT_CHEAT_WARP ? " to " : "",
          cheat == PORT_CHEAT_WARP ? port_level_name(level, name, sizeof name) : "", g_Gamestate,
          g_LevelId);
  return 1;
}

int port_cheat_pending(void) { return s_pending; }

/* TitlescreenUpdate (0009-skip-intro-logos.patch): with the logos skipped,
 * the first title after boot also skips its flythrough, once. */
int PortSkipTitleIntro(void) {
  static int s_done; /* the first title only, whatever the menu says later */
  int skip = g_port_skip_logos && !s_done;
  s_done = 1;
  return skip;
}

/* --- hooks in the game's code (0008-cheats-from-options-menu.patch) --- */

/* GamestateUpdate, first: drop a cheat that can no longer be applied. */
void PortCheatsTick(void) {
  if (!s_pending)
    return;
  if (!playing_state() || g_DemoMode || ++s_age > PENDING_TICKS) {
    if (s_log)
      s_log("cheats: %s dropped (gamestate %d, demo %d, %d ticks)", k_names[s_pending],
            g_Gamestate, g_DemoMode, s_age);
    s_pending = 0;
  }
}

/* The pause menu and play: open the inventory, as SELECT does. */
int PortCheatPending(void) { return s_pending != 0 && !g_DemoMode; }

/* func_8002EB2C (the inventory), where it starts reading the pad. Returns 1
 * when the inventory's update must end there: the warp (the game's own warp
 * branch returns too) and the credits. */
int PortCheatsInventory(void) {
  int cheat = s_pending, down;
  /* CheatsProcess's own condition, and the warp code takes priority */
  if (!cheat || g_DemoMode || g_LoadStage >= 0 || g_LevelCheatActive)
    return 0;
  s_pending = 0;
  if (s_log)
    s_log("cheats: %s applied in the inventory (level %d)", k_names[cheat], g_LevelId);
  if (cheat == PORT_CHEAT_WARP) {
    g_LevelCheatActive = 1;
    cheat_HomeworldSelected = s_level / 10;
    down = g_Pad.m_Down;
    g_Pad.m_Down = k_digit[s_level % 10];
    CheatProcessLevelWarp();
    g_Pad.m_Down = down;
    return 1;
  }
  CheatApplyEffect(cheat == PORT_CHEAT_LIVES ? 4 : cheat == PORT_CHEAT_LEVELS ? 3 : 2);
  CheatResetBuffer();
  /* the credits took over (gamestate 15): nothing more of the inventory,
   * whose START/TRIANGLE check would pause over them */
  return cheat == PORT_CHEAT_CREDITS;
}

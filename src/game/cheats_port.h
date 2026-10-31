/* The game's own cheats from the port's options menu, and skipping the boot
 * logos (src/game/cheats_port.c, decisions D50). Platform-facing: no decomp
 * headers. Everything here runs on the game thread (the Vita's menu runs
 * inside the game's VSync wait). */
#ifndef SPYRO_CHEATS_PORT_H
#define SPYRO_CHEATS_PORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  PORT_CHEAT_NONE,
  PORT_CHEAT_LIVES,   /* 99 LIVES: CheatApplyEffect(4) */
  PORT_CHEAT_LEVELS,  /* LEVELS OPEN: CheatApplyEffect(3), changes the save */
  PORT_CHEAT_CREDITS, /* CREDITS: CheatApplyEffect(2) */
  PORT_CHEAT_WARP,    /* the unused level warp: CheatProcessLevelWarp() */
};

/* Set by the platform before the game starts: Initialize() leaves out the
 * Sony and Universal logos (their fades and 3.5 s holds; every load stays),
 * and the first title skips its flythrough as START would. */
extern int g_port_skip_logos;

/* Optional log for the cheat lines (printf-style). */
void port_cheats_set_log(void (*log)(const char *fmt, ...));

/* 1 when the menu may offer cheats: in a level, playing, paused or in the
 * inventory, not in an attract demo, not loading. */
int port_cheat_available(void);
/* Queue a cheat (level: 10..64, for PORT_CHEAT_WARP). It is applied in the
 * inventory, where the game reads cheat codes: from play or the pause menu
 * the game first opens the inventory, as SELECT does. Returns 1 if queued. */
int port_cheat_request(int cheat, int level);
int port_cheat_pending(void);

/* Level warp targets, in order: 10..15, 20..25, ..., 50..55, 60..64. */
int port_warp_count(void);
int port_warp_level(int index);
/* "ARTISANS: STONE HILL", from the game's own name tables (strings.c). */
const char *port_level_name(int level, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif

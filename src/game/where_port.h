/* Where the player is, from the game's own globals, for the platform's log
 * (src/game/where_port.c, decisions D71). Platform-facing: no decomp
 * headers. */
#ifndef SPYRO_WHERE_PORT_H
#define SPYRO_WHERE_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int level;      /* g_LevelId: world * 10 + level, 10..64 */
  int gamestate;  /* g_Gamestate (GS_*, common.h) */
  int load_stage; /* g_LoadStage: < 0 when not loading */
  int demo;       /* g_DemoMode: 1 an attract demo plays */
} PortWhere;

void port_where(PortWhere *w);
const char *port_gamestate_name(int gamestate); /* "playing", "level transition", ... */

#ifdef __cplusplus
}
#endif

#endif

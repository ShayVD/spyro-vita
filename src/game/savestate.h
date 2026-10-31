/* Save states (src/game/savestate.c, decisions D51): the whole game saved to
 * <save dir>/state<N>.bin and loaded back, at the top of the game's main
 * loop. Separate from the game's own memory card. */
#ifndef SPYRO_SAVESTATE_H
#define SPYRO_SAVESTATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAVESTATE_SLOTS 3 /* slots 1..3 */

/* Ask for a save or load of `slot`; it happens at the next main loop top
 * (savestate_point). A newer request replaces one not yet done. */
void savestate_request(int load, int slot);
int savestate_pending(void); /* a request is waiting */

/* PortPresentBeforeUpdate (present.c): the main loop top. */
void savestate_point(void);

/* The last request's outcome, for the platform to show ("STATE 1 SAVED",
 * "STATE IS FROM ANOTHER BUILD"); serial goes up by one per outcome. */
const char *savestate_message(int *serial);

/* What a slot holds, from its header (the menu). 0 = a usable state; -1 none;
 * -2 from another build or layout (msg says which). */
typedef struct {
  int vblank, gamestate, level;
} SaveStateInfo;
int savestate_info(int slot, SaveStateInfo *info, const char **msg);

/* Where a state is, for the menu: "STONE HILL", "ARTISANS", "TITLE". */
const char *savestate_label(const SaveStateInfo *info);

/* The save directory (default: the memory cards' root, libmcrd.c). */
void savestate_set_dir(const char *dir);

/* Called at the end of every savestate_point (tests on the linux32 host). */
extern void (*g_savestate_tick_hook)(void);

/* Tests (linux32 --state-hash): the state's chunks hashed now, without
 * writing a file: tag and 32-bit hash per chunk, then the whole. */
int savestate_hash(char tags[][5], uint32_t *hashes, int max);

#ifdef __cplusplus
}
#endif

#endif

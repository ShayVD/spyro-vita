/* 60 Hz logic, opt-in (decisions D130; docs/logic-60hz-audit.md §7,
 * docs/tick60.md).
 *
 * The 60 fps option has three values: off (30 Hz logic, 30 fps),
 * interpolated (30 Hz logic and the presenter's in-between frame) and 60 Hz
 * logic, where a tick in play is one vblank. The first two run the game's
 * update as it always has; the third runs a second compile of the update
 * path (cmake/tick60.cmake: the files with SPYRO_TICK60 hunks and their
 * callers, built again with -DSPYRO_TICK60=1, every function local but
 * GamestateUpdate_60). The 30 Hz objects are today's code: only main.c's
 * dispatch (0012-tick60-dispatch.patch) changes on their path.
 *
 * Only play ticks at 60 Hz: g_Gamestate GS_Playing outside the attract
 * demos. Every other gamestate (the menus, the cutscenes, the transitions,
 * the title) runs the original's update at 30 Hz, with the in-between frame
 * if interpolation is on. The 60 Hz rules keep every piece of game state in
 * the original's units (a counter still counts 30 Hz ticks, a speed is still
 * per 30 Hz tick; each vblank applies its share), so the two rule sets can
 * take turns at any gamestate change. */
#ifndef SPYRO_TICK60_H
#define SPYRO_TICK60_H

#ifdef __cplusplus
extern "C" {
#endif

struct PsxStateIO;

/* The setting, chosen at boot (host --tick60; the Vita's config.txt
 * interp=4 / the menu's "60 HZ", from the next start). A save state made
 * in the other mode switches it until the next start (§7.2). */
extern int g_tick60_mode;

typedef struct {
  int active;     /* the update running now is a 60 Hz one */
  int acc;        /* vblanks into the current 30 Hz tick: 0, or 1 between its halves */
  int tick;       /* this update starts a 30 Hz tick: its once-a-tick steps and the 30 Hz parts run */
  int tick_delta; /* the vblanks since the last tick started (2..4): the 30 Hz parts' g_DeltaTime */
  int since;      /* vblanks since the last tick started */
  int upper;      /* this vblank is a tick's second half (t60_share's larger part) */
  int next;       /* PortTickDelta's: the next update is a 60 Hz one */
  int spyro_carry[3]; /* Spyro's move: the low 6 bits of a tick's first half (t60_spyro_shift) */
} Tick60State;
extern Tick60State g_t60;

typedef struct {
  long long ticks60, ticks30, half_ticks, late; /* late: 60 Hz updates of 2+ vblanks */
  long long us60, us30; /* the update's time (--tick60-stats) */
  long long ticks30_play, us30_play; /* of those at 30 Hz, the ones in play */
} Tick60Stats;
extern Tick60Stats g_t60_stats;
extern int g_t60_stats_on;

/* main.c (0012-tick60-dispatch.patch): */
void PortGamestateUpdate(void); /* GamestateUpdate, or the 60 Hz rules' */
int PortTickDelta(int pUnprocessed); /* g_DeltaTime for the next update: 2..4, or 1..4 */

int tick60_built(void); /* this build has the 60 Hz rules (cmake/tick60.cmake) */
void tick60_state(struct PsxStateIO *io); /* save states' "T60 " chunk */
void tick60_report(void (*log)(const char *fmt, ...)); /* the stats, at exit */

#ifdef __cplusplus
}
#endif
#endif

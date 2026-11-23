/* Longer draw distance (src/game/lod.c, decisions D49): optional, off by
 * default. The game's render-side distances are scaled as the renderers read
 * them (tools/mips2c/hints.yaml `before`): the world's high-detail distance,
 * the mobys' detail distance, and the mobys' draw distance, the last through
 * an extra pass that never touches what the game logic reads. */
#ifndef SPYRO_LOD_H
#define SPYRO_LOD_H

#ifdef __cplusplus
extern "C" {
#endif

#define LOD_ONE 256 /* scales are x256 */

typedef struct {
  int scale; /* LOD_ONE: off (the original distances); 384: 1.5x, 512: 2x */
  int stats; /* collect the numbers port_lod_report() logs */
  int guards_only; /* tests: no limiter and no reset at a new level, only the
                      hard bounds keep the buffer (host --lod-guards-only) */
  int budget; /* with 60 Hz logic: the bytes of primitives a draw may reach
                 before the longer distance backs off (0: no budget, only
                 the buffer's limits; the Vita's dd_budget=0, the host's
                 --lod-budget 0, #185). With the Vita's dd_budget=auto
                 (#212) lod_set_budget sets it every logic frame */
} LodConfig;

#define LOD_BUDGET_60 0xC000 /* 48 KB: the default budget (lod.c, #180), and the
                                least the Vita's dd_budget=auto sets (#212) */

extern LodConfig g_lod;

/* dd_budget=auto (the Vita's default, src/platform/vita/lod_time.c, #212):
 * the platform sizes the 60 Hz budget from the game thread's measured time
 * per logic frame; lod.c still does all the limiting. The game's copy only
 * (not the presenter's, lod_pres.c).
 *  - lod_last_draw: 1 if the last shown draw kept to the 60 Hz budget (60 Hz
 *    logic in play, the option on), with the bytes of primitives it wrote
 *    (what the budget is compared with) and the budget it had;
 *  - lod_set_budget: the budget for the next draws, clamped here between
 *    LOD_BUDGET_60 and the span the draw has (draw_room), with the frame's
 *    time it was sized from (used_us of target_us) for the lod line (its
 *    average, its longest and the budget's range per window, #224). Returns
 *    the budget as clamped. The host never calls it: its budget stays the
 *    fixed one (--lod-budget). */
int lod_last_draw(int *bytes, int *budget);
int lod_set_budget(int bytes, int used_us, int target_us);
/* The platform's call at the start of every shown draw with the option on,
 * on the game thread, before the limiter's scale is taken: where it sizes the
 * budget from the frame just done. Once a logic frame at 60 Hz too, where
 * the update is GamestateUpdate_60 (tick60.c). None by default (the host). */
void lod_set_budget_hook(void (*fn)(void));

/* present.c, around every GamestateDraw: shown = its picture is shown (not
 * the hidden 4:3 logic draw of attract demos). pres_draw.c's lod_begin calls
 * port_lod_draw_begin(1) for each presenter frame and puts the state back.
 * In an attract demo every draw is at 1x until the game's last draw is one
 * of the demo's own shown draws, so the demos' 16:9 presenter frames, whose
 * logic draws are hidden, are at 1x (the demo rule, D224, #236). */
void port_lod_draw_begin(int shown);
void port_lod_draw_end(int shown);

/* A save state was loaded (src/game/savestate.c, D51): the next draw is at
 * the chosen scale at once (the limiter backs off after it if that spot is
 * heavy; the hard bounds hold the buffer either way), with no moby kept from
 * before. g_lod, the player's setting, stays. */
void port_lod_reset(void);

/* One line of statistics since the last call (empty when nothing was drawn
 * with the option on); resets them. */
void port_lod_report(char *out, unsigned len);
/* All of lod.c's state, for the presenter's copy (lod_pres.c, D123) */
unsigned port_lod_state_size(void);
void port_lod_state_get(unsigned char *p);
void port_lod_state_set(const unsigned char *p);

#ifdef __cplusplus
}
#endif

#endif

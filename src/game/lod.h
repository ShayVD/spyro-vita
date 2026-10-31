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
} LodConfig;

extern LodConfig g_lod;

/* present.c, around every GamestateDraw: shown = its picture is shown (not
 * the hidden 4:3 logic draw of attract demos). */
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

#ifdef __cplusplus
}
#endif

#endif

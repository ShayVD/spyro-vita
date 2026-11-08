/* The presenter's draw (stage 7e, decisions D124; docs/native-plan.md
 * stage 7, "The plan refreshed (D119)").
 *
 * interp_mode=2 (host --interp-mode 2): in play, the in-between frame is
 * drawn by the game's own draw code compiled a second time
 * (GamestateDraw_pres, cmake/presenter.cmake) from a snapshot
 * (pres_snap.c), not by D30's pass over the game's own memory. It runs on
 * the game thread at D30's pass's point; D30's pass stays the reference
 * (--pres-check) and the fallback for the other gamestates. */
#ifndef PRES_DRAW_H
#define PRES_DRAW_H

#include <stdint.h>

/* whether this build has the presenter (not with the moved arena) */
extern const int g_pres_built;

/* Before the arena is set up: the presenter needs its area after the
 * game's memory (render_ctx_want_area). */
void pres_draw_want(void);
/* 1 if the presenter can draw now: built, its area there, fast_render=2 */
int pres_draw_available(void);

/* The in-between frame from the game's state now, blended (blend 1) or as
 * it is (0). wide: 16:9. flags/caches: the sector flags and the sectors'
 * four animation bytes the draw starts from (present.c's 16:9 copies in
 * an attract demo), NULL for the game's. Its DrawOTag goes through
 * present.c's hooks as any pass's. Returns 0 if there was no frame (the
 * camera jumped). */
int pres_draw_frame(int blend, int wide, const uint8_t *flags, const uint32_t *caches, int ncaches);
/* After pres_draw_frame: the presenter's sector flags and caches, for
 * present.c's 16:9 copies. */
void pres_draw_visibility(uint8_t *flags, uint32_t *caches, int ncaches);

/* present.c's blend_state on the snapshot's copies (present.c) */
int port_present_blend_to(void *camera, void *spyro, void *mobys, void *particles);

/* --pres-check (present.c): the presenter's frame against D30's pass's,
 * record for record. Each DrawOTag's list is read into a frame record
 * (pres_check.c): the draw and display environments put, then each packet
 * in the list's order, a marker's records expanded (native_draw.h). */
extern int g_pres_check;
void pres_check_frame(int which, const void *ot_head); /* 0 D30's pass, 1 the presenter */
void pres_check_compare(void);                          /* after both, this tick */
void pres_check_skip(int why);                          /* a tick not compared */
int pres_check_report(void (*log)(const char *fmt, ...)); /* 0 when nothing differed */
void pres_check_set_log(void (*log)(const char *fmt, ...));

#endif

/* The presenter's draw (stage 7e, decisions D125; docs/native-plan.md
 * stage 7, "The plan refreshed (D119)").
 *
 * The in-between frame, and the attract demos' 16:9 real frame, are drawn
 * by the game's own draw code compiled a second time (GamestateDraw_pres,
 * cmake/presenter.cmake) from a snapshot (pres_snap.c), in every gamestate
 * (stage 7g, D128). It starts after the update, before the logic draw;
 * with a platform worker (stage 7f, D126; present.c) it draws on its own
 * core beside the logic draw, else inline on the game thread. D30's pass
 * over the game's own memory drew them until D129. */
#ifndef PRES_DRAW_H
#define PRES_DRAW_H

#include <stdint.h>

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include "psx/types.h"
#include "psx/libgte.h"
#include "psx/libgpu.h"

/* whether this build has the presenter (not with the moved arena) */
extern const int g_pres_built;

/* Before the arena is set up: the presenter needs its area after the
 * game's memory (render_ctx_want_area). */
void pres_draw_want(void);
/* 1 if the presenter can draw now: built, its area there, fast_render=2 */
int pres_draw_available(void);

/* The in-between frame from the game's state now, in two (stage 7f, D126):
 *  - pres_draw_prepare, on the game thread after the update: the snapshot,
 *    blended (blend 1) or as it is (0), and the renderers' context. wide:
 *    16:9. flags/caches: the sector flags and the sectors' four animation
 *    bytes the draw starts from (present.c's 16:9 copies in an attract
 *    demo), NULL for the game's. Returns 0 if there is no frame (the camera
 *    jumped);
 *  - GamestateDraw_pres, the draw, on the worker or inline: its output is
 *    present.c's presenter versions of DrawOTag and the rest
 *    (pres_names.h). */
int pres_draw_prepare(int blend, int wide, const uint8_t *flags, const uint32_t *caches,
                      int ncaches);
void GamestateDraw_pres(void);
/* between the two, for an inline draw that is shown: the vertex cache
 * records its projections too, if it is on (D152) */
void pres_draw_vcache(void);
/* the presenter's records' pool (its own; NULL with native_draw off) */
void pres_draw_pool(const uint32_t **w, uint32_t *cap);
/* the records' mode of its list (native_draw.c's g_psx_walk_nd, its own) */
extern int g_pres_walk_nd;
/* After the draw: the presenter's sector flags and caches, for
 * present.c's 16:9 copies. */
void pres_draw_visibility(uint8_t *flags, uint32_t *caches, int ncaches);

/* Stage 7g (D128): the overlay functions the other gamestates' draws call
 * through a pointer the level set (overlay_pointers.c): the flight
 * results' draw and its time (D_8007567C, D_800758C4; Flight5_LEVEL_*,
 * Flight4_LEVEL_*) and the balloonist's talk (D_800758D8,
 * Balloonist4_LEVEL_*). Which level's each pointer is, as an index into
 * the levels below, is set on the game thread from the game's pointers
 * (pres_draw_prepare), -1 for none of them; pres_ovl.c's pres_flight5,
 * pres_flight4 and pres_balloon4 are the presenter's calls' targets.
 * D145: PAL's flight results also count a line's letters through a pointer
 * the level set (g_PalLetterCount, func_pal_80081758_LEVEL_*): pres_letters. */
#define PRES_FLIGHT_LEVELS(X) X(15) X(25) X(35) X(45) X(55)
#define PRES_BALLOON_LEVELS(X) X(10) X(11) X(13) X(20) X(21) X(22) X(30) X(40) X(50) X(60)
#define PRES_FLIGHTS 5
#define PRES_BALLOONS 10
enum { PRES_OVL_FLIGHT5, PRES_OVL_FLIGHT4, PRES_OVL_BALLOON4, PRES_OVL_LETTERS, PRES_OVLS };
extern int g_pres_ovl[PRES_OVLS];
extern long long g_pres_ovl_unknown; /* a pointer set to none of them, drawn as nothing */
void (**pres_flight5(void))(void);
int (**pres_flight4(void))(int, void *, int);
void (**pres_balloon4(void))(void);
int (**pres_letters(void))(char *);

/* present.c's blend on the snapshot's copies (present.c) */
int port_present_blend_to(void *camera, void *spyro, void *mobys, void *particles);

/* --pres-check (present.c): the presenter's frame against another drawn
 * from the same state, record for record: since D129 its frame from the
 * snapshot unblended against the logic draw's own (D125-D128: its
 * in-between frame against D30's pass's). Each DrawOTag's list is read into a frame record
 * (pres_check.c): the draw and display environments put, then each packet
 * in the list's order, a marker's records expanded (native_draw.h). */
extern int g_pres_check;
/* Stage 7f (D126), the host's --pres-check-thread: the presenter's list is
 * recorded on a thread of its own (g_present_pres's post and wait) beside
 * the logic draw, and compared at the join (--pres-check-late, D128: at
 * the join, after it); the logic
 * draw's face replacements go into a copy meanwhile, as on the Vita, unless
 * g_faces_race (--faces-race, diagnostic: in place, as before 7f). */
extern int g_pres_check_thread, g_faces_race;
/* which: 0 the logic draw (D30's pass until D129), 1 the presenter; de, dd: the draw and display
 * environments its list is walked with; pool: the presenter's records'
 * pool (pool_cap words), NULL for g_nd_pool's */
void pres_check_frame(int which, const void *ot_head, const DRAWENV *de, const DISPENV *dd,
                      const uint32_t *pool, uint32_t pool_cap);
void pres_check_compare(void);                          /* after both, this tick */
void pres_check_skip(int why);                          /* a tick not compared */
int pres_check_report(void (*log)(const char *fmt, ...)); /* 0 when nothing differed */
void pres_check_set_log(void (*log)(const char *fmt, ...));

#endif

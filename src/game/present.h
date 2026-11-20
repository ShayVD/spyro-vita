/* Optional presentation features (src/game/present.c, docs/presentation.md):
 * 16:9 widescreen and 60 fps interpolated frames over the original 30 Hz
 * logic. The platform sets g_present before the game starts and shows
 * port_present_frame() at each vblank. */
#ifndef SPYRO_PRESENT_H
#define SPYRO_PRESENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRESENT_W 512 /* captured frame: the 512x240 display area, PS1 15-bit */
#define PRESENT_H 240
/* PAL's display is 512x256, all of it drawn (D134): its frames are 256 lines
 * (D148; until then 240 with the US's 8-line margin, so the bottom 32 of
 * PAL's lines, the title's language row among them, were never shown).
 * port_present_slot_h(): a slot's lines. */
#define PRESENT_H_MAX 256
#define PRESENT_SLOTS 6   /* captured frames: queued and on screen */

typedef struct {
  int widescreen; /* narrow the projection to 17/21 and fill the screen */
  int ws_logic;   /* one 16:9 draw always, attract demos too (diagnostic:
                     the demos desync) */
  int ws_exact;   /* the original 4:3 logic always: a hidden 4:3 draw plus the
                     presenter's 16:9 frame (the default does this only in
                     attract demos, and draws once in 16:9 otherwise) */
  int interp;     /* an extra frame between ticks, blended state */
  int interp_mobys; /* blend mobys and Spyro too, not only the camera */
  int interp_anim;  /* with interp_mobys: animation frames and particles too */
  int interp_budget_us; /* >0: skip the in-between frame while the game
                           thread's CPU time per tick with it would exceed
                           this (the safety net against running late, D75) */
  int stats;        /* time update and draw (also with both features off) */
} PresentConfig;

typedef struct {
  long long ticks, passes, logic_passes, interp_frames, skipped_interp; /* passes: the
                                                                   presenter's frames */
  long long us_pass, us_logic, us_update; /* summed microseconds (us_pass: the
                                             presenter's frames on the game thread);
                                             us_logic leaves out VSync waits */
  long long us_vsync; /* inside VSync(0) during the logic pass (host vblank work) */
  long long us_blend; /* saving the previous tick and blending (interp) */
  long long squeezed_2d; /* 2D primitives narrowed for widescreen */
  long long anim_blended, anim_kept; /* animation tracks in in-between frames */
  long long cut_placed; /* in-between frames with PAL 60's cutscene mobys at their own time (D156) */
  long long budget_skips; /* in-between frames skipped for interp_budget_us */
  long long budget_holds; /* times 60 fps fell back to a held 30 (D68) */
  /* stage 7c (D112): frames carry their due vblank. On the host and with
   * virtual vblanks the frame due at each vblank is shown at it, so these
   * stay 0 there; with the Vita's real time, the game thread's own view. */
  long long due_late;       /* frames taken after their due vblank */
  long long due_superseded; /* frames never taken: a newer one was due too */
  long long queue_drops;    /* frames dropped from a full queue (QUEUE waiting) */
  long long display_fallbacks; /* pictures that showed the PS1 display instead */
  /* D148, 60 fps by time (the game's clock at 50 Hz, frames due at the
   * display's 60 Hz vblanks): */
  long long time_ticks;  /* ticks whose frames were blended by time */
  long long time_frames; /* the presenter's frames for them, one a display vblank */
  long long time_hidden; /* logic draws not drawn (the presenter's frames shown) */
  long long time_fallbacks; /* ticks shown by their logic draw (no blend: a jump,
                               a new gamestate, the presenter unable) */
  long long due_units, due_held; /* port_present_frame_due's vblanks, and those
                                    with no new frame while one was shown */
  long long particles_blended;
} PresentStats;

extern PresentConfig g_present;
extern PresentStats g_present_stats;

/* A frame filed into a slot (stage 7c, D112): the vblank it's due at, the
 * first vblank it is shown at (shown at the first vblank >= due, until a
 * newer frame is due), whether it's 16:9, and whether it's an in-between
 * frame (D30) rather than a real one. dropped: the slot of the oldest
 * waiting frame the full queue dropped to make room for this one (never
 * shown; the frames still waiting are each due a vblank sooner), or -1. */
typedef struct PresentFrame {
  int slot, due, wide, interp, dropped;
  int fixed; /* by time (D148): the frames still waiting keep their dues
                when one is dropped (each is drawn for its own display vblank) */
} PresentFrame;

/* A platform that keeps captured pictures itself (the Vita's GPU renderer):
 * capture() is called after a captured draw's DrawOTag with the VRAM rect
 * just drawn, commit() then files that picture as slot 0..PRESENT_SLOTS-1.
 * begin() (may be NULL) comes before that, at the first DrawOTag of a draw
 * that captures: the frame's draws start, with the due vblank it will get
 * as far as known then (7c's render thread skips a late frame's draws).
 * NULL (default): StoreImage into our own pixel slots. */
typedef struct PresentCapture {
  void (*capture)(int x, int y, int w, int h);
  void (*commit)(const PresentFrame *f);
  void (*begin)(int due, int interp);
} PresentCapture;
extern const PresentCapture *g_present_capture;

/* Stage 7f (D126): a platform that runs the presenter's in-between frame
 * on a core of its own (the Vita). The presenter records the frame into a
 * buffer of its own, which the render thread runs whole where the game
 * thread placed it in the command ring: before the logic draw's commands.
 * Without it (the host) the presenter runs inline, on the game thread, at
 * the same point, and draws through the GR_* calls.
 *  - usable: the worker can draw the frame now (its thread is up; no
 *    vertex cache, whose tables are the game thread's);
 *  - submit (game thread): the buffer's place in the ring; f: the frame it
 *    files (its slot reserved), NULL: none;
 *  - post (game thread): fn on the worker; wait: until it has returned;
 *  - on the worker, between open and close: gpu (the walk's draws), env
 *    (the display state its draws see), begin, capture and commit (as
 *    PresentCapture's, into the buffer). close: the buffer is whole; the
 *    render thread may run it. */
struct PsxGpuOut;
typedef struct PresentPresOut {
  int (*usable)(void);
  void (*submit)(const PresentFrame *f);
  void (*post)(void (*fn)(void));
  void (*wait)(void);
  const struct PsxGpuOut *gpu;
  void (*open)(void);
  void (*env)(const void *dispenv, int disabled, int wide);
  void (*begin)(int due, int interp);
  void (*capture)(int x, int y, int w, int h);
  void (*commit)(const PresentFrame *f);
  void (*close)(void);
} PresentPresOut;
extern const PresentPresOut *g_present_pres;
/* The host's --pres-write-check (stage 7g, D128): called before (0) and
 * after (1) each presenter frame drawn inline; NULL elsewhere. */
extern void (*g_present_pres_watch)(int after);

/* Stage 7f's numbers since the last call (the platform's present line):
 * frames the presenter drew on the worker and inline, the worker's time,
 * the game thread's waits for it at the join (count, time), and the
 * display environments the game thread put for it that weren't the
 * presenter's (0 expected). */
typedef struct {
  long long worker_frames, inline_frames, worker_us, joins_waited, join_wait_us, disp_misses;
  long long uncaptured; /* frames it drew without a capture (none expected in play) */
  /* stage 7g (D128): the gamestates (bit k: gamestate k) of the frames
   * counted in disp_misses and uncaptured */
  unsigned disp_miss_gs, uncaptured_gs;
} PresentPresStats;
void port_present_pres_stats(PresentPresStats *out);
/* The game thread, before anything that changes what the presenter reads
 * (the update, a save state, the exit): the presenter's frame, if one is
 * under way on the worker, has finished. */
void port_present_pres_join(void);

/* Microseconds for interp_budget_us and the stats; default clock() (CPU
 * time). The Vita sets sceKernelGetProcessTimeWide. */
extern long long (*g_present_clock_us)(void);

int port_present_active(void); /* any feature on */
void port_present_reset(void);  /* a save state was loaded: nothing queued */
/* Call once per vblank. Returns the frame to show now (PS1 15-bit,
 * PRESENT_W x PRESENT_H), or NULL to show the PS1 display as usual (no
 * presentation frame was made, e.g. boot logos or loading). */
const uint16_t *port_present_frame(void);
int port_present_frame_is_wide(void); /* the frame returned was widescreen */
/* The same, as a slot number (-1: show the PS1 display); with
 * g_present_capture set, port_present_frame() is NULL and this is the one
 * to use. Call one of the two, once per picture shown. */
int port_present_frame_slot(void);
/* Stage 7c (D112): the same, by due vblank: the newest frame due at vblank
 * v (taken once per vblank, at the vblank itself: the host, realtime=0, and
 * the Vita's game thread with present_due=1). Frames are due one per vblank
 * after the vblank they were filed at, in order, so on the host this is the
 * same frame port_present_frame_slot() takes. */
int port_present_frame_due(int v);
int port_present_slot_due(int slot); /* the due vblank filed with a slot */
int port_present_slot_h(int slot);   /* its lines: PRESENT_H, or 256 for PAL's display (D148) */
const uint16_t *port_present_slot_pixels(int slot); /* NULL with a platform capture */
int port_present_frame_new(void); /* that call took a frame not shown before */
/* The pixels on screen now, without taking a new frame (screenshots); NULL
 * when the PS1 display is shown or the platform keeps the pictures. */
const uint16_t *port_present_shown_pixels(void);
int port_present_shown_h(void); /* their lines (port_present_slot_h) */

/* order_table.c: a C-built 2D primitive went into the HUD OT. */
void port_present_note_2d(void *prim);

/* Where a draw into db_buf (a DB's m_PolyBuf) starts writing primitives: the
 * lower buffer while any option is on (D54), PortPolySpace() in
 * GamestateDraw (patches/spyro-1-port/0010). */
void *port_poly_start(void *db_buf);
void PortPolySpace(void);

/* The PS1 RAM a DrawOTag list can reach, for the OT walk from a copy
 * (src/platform/common/walk.c, D71): a psx_walk.h PsxWalkRangesFn. */
struct PsxWalkRange;
int port_present_prim_ranges(struct PsxWalkRange *r, int max);

#ifdef __cplusplus
}
#endif

#endif

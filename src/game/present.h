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
#define PRESENT_PAGES 512 /* 4 KB pages of the 2 MB arena */
#define PRESENT_SLOTS 6   /* captured frames: queued and on screen */

typedef struct {
  int widescreen; /* narrow the projection to 17/21 and fill the screen */
  int ws_logic;   /* one 16:9 draw always, attract demos too (diagnostic:
                     the demos desync) */
  int ws_exact;   /* the original 4:3 logic always: a hidden 4:3 draw plus a
                     16:9 presentation pass (the default does this only in
                     attract demos, and draws once in 16:9 otherwise) */
  int interp;     /* an extra frame between ticks, blended state */
  int interp_mobys; /* blend mobys and Spyro too, not only the camera */
  int interp_anim;  /* with interp_mobys: animation frames and particles too */
  int interp_budget_us; /* >0: skip the in-between frame while the game
                           thread's CPU time per tick with it would exceed
                           this (the safety net against running late, D75) */
  int stats;        /* time update and draw (also with both features off);
                     2 also maps the arena pages each pass dirties */
} PresentConfig;

typedef struct {
  long long ticks, passes, logic_passes, interp_frames, skipped_interp;
  long long us_pass, us_snapshot, us_logic, us_update; /* summed microseconds;
                                                   us_logic leaves out VSync waits */
  long long us_vsync; /* inside VSync(0) during the logic pass (host vblank work) */
  long long us_blend; /* saving the previous tick and blending (interp) */
  long long spu_dropped; /* SPU writes swallowed inside presentation passes */
  long long squeezed_2d; /* 2D primitives narrowed for widescreen */
  long long anim_blended, anim_kept; /* animation tracks in in-between frames */
  long long budget_skips; /* in-between frames skipped for interp_budget_us */
  long long budget_holds; /* times 60 fps fell back to a held 30 (D68) */
  long long display_fallbacks; /* pictures that showed the PS1 display instead */
  long long particles_blended;
  long long snapshot_bytes; /* copied in and out by the region snapshot */
  long long dirty_pages, dirty_max; /* stats 2: 4 KB pages a pass wrote */
  uint32_t dirty_union[PRESENT_PAGES / 32]; /* every page any pass wrote */
  long long missed_pages; /* stats 2: dirty pages outside snapshot and scratch */
  uint32_t missed_union[PRESENT_PAGES / 32];
} PresentStats;

extern PresentConfig g_present;
extern PresentStats g_present_stats;

/* A platform that keeps captured pictures itself (the Vita's GPU renderer):
 * capture() is called after a presentation pass's DrawOTag with the VRAM rect
 * just drawn, commit() then files that picture as slot 0..PRESENT_SLOTS-1.
 * NULL (default): StoreImage into our own pixel slots. */
typedef struct PresentCapture {
  void (*capture)(int x, int y, int w, int h);
  void (*commit)(int slot);
} PresentCapture;
extern const PresentCapture *g_present_capture;

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
const uint16_t *port_present_slot_pixels(int slot); /* NULL with a platform capture */
int port_present_frame_new(void); /* that call took a frame not shown before */
/* The pixels on screen now, without taking a new frame (screenshots); NULL
 * when the PS1 display is shown or the platform keeps the pictures. */
const uint16_t *port_present_shown_pixels(void);

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

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
  int interp_mode;  /* stage 7e (D124): 2 = in play the in-between frame is the
                       presenter's (the draw code built again over a snapshot,
                       src/game/pres/), not D30's pass; 0 or 1 = D30's pass */
  int audit;        /* stage 7a (D104), debug only: the in-between frames per
                       gamestate and their cost, and the HUD check */
} PresentConfig;

#define PRESENT_GAMESTATES 16 /* GS_Playing .. GS_Credits (common.h) */
#define PRESENT_HUD_DIFFS 8   /* the first ticks whose two HUD lists differ */
#define PRESENT_HUD_WORDS 13  /* a packet's tag and up to 12 words, kept for those */

/* audit: per gamestate (g_Gamestate at the draw) */
typedef struct {
  long long ticks, interp, skipped; /* skipped: a cut, a gamestate or level
                                       change, a camera jump, the budget */
  long long us_pass, packets; /* the in-between pass: time, OT packets drawn */
  long long hud_same, hud_differ; /* its HUD list against the real frame's */
} PresentGsStats;

typedef struct {
  int vblank, gamestate, level;
  int packets[2]; /* the in-between pass's HUD list, the real frame's */
  int first;      /* the first packet that differs */
  uint32_t words[2][PRESENT_HUD_WORDS]; /* its tag and words in each list */
} PresentHudDiff;

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
  /* stage 7c (D112): frames carry their due vblank. On the host and with
   * virtual vblanks the frame due at each vblank is shown at it, so these
   * stay 0 there; with the Vita's real time, the game thread's own view. */
  long long due_late;       /* frames taken after their due vblank */
  long long due_superseded; /* frames never taken: a newer one was due too */
  long long queue_drops;    /* frames dropped from a full queue (QUEUE waiting) */
  long long display_fallbacks; /* pictures that showed the PS1 display instead */
  long long particles_blended;
  long long snapshot_bytes; /* copied in and out by the region snapshot */
  long long dirty_pages, dirty_max; /* stats 2: 4 KB pages a pass wrote */
  uint32_t dirty_union[PRESENT_PAGES / 32]; /* every page any pass wrote */
  long long missed_pages; /* stats 2: dirty pages outside snapshot and scratch */
  uint32_t missed_union[PRESENT_PAGES / 32];
  PresentGsStats gs[PRESENT_GAMESTATES]; /* audit */
  PresentHudDiff hud_diff[PRESENT_HUD_DIFFS];
  int nhud_diff;
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
} PresentFrame;

/* A platform that keeps captured pictures itself (the Vita's GPU renderer):
 * capture() is called after a presentation pass's DrawOTag with the VRAM rect
 * just drawn, commit() then files that picture as slot 0..PRESENT_SLOTS-1.
 * begin() (may be NULL) comes before that, at the first DrawOTag of a pass
 * that captures: the frame's draws start, with the due vblank it will get
 * as far as known then (7c's render thread skips a late frame's draws).
 * NULL (default): StoreImage into our own pixel slots. */
typedef struct PresentCapture {
  void (*capture)(int x, int y, int w, int h);
  void (*commit)(const PresentFrame *f);
  void (*begin)(int due, int interp);
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
/* Stage 7c (D112): the same, by due vblank: the newest frame due at vblank
 * v (taken once per vblank, at the vblank itself: the host, realtime=0, and
 * the Vita's game thread with present_due=1). Frames are due one per vblank
 * after the vblank they were filed at, in order, so on the host this is the
 * same frame port_present_frame_slot() takes. */
int port_present_frame_due(int v);
int port_present_slot_due(int slot); /* the due vblank filed with a slot */
const uint16_t *port_present_slot_pixels(int slot); /* NULL with a platform capture */
int port_present_frame_new(void); /* that call took a frame not shown before */
/* The pixels on screen now, without taking a new frame (screenshots); NULL
 * when the PS1 display is shown or the platform keeps the pictures. */
const uint16_t *port_present_shown_pixels(void);

/* order_table.c: a C-built 2D primitive went into the HUD OT. */
void port_present_note_2d(void *prim);
/* order_table.c func_80016784, before it links the HUD OT (head..tail) into
 * a list: the audit's HUD check (D104). */
void port_present_hud_list(const void *head, const void *tail);

/* Stage 7a's read audit (D104, the linux32 host's --present-reads): called
 * at each draw's start (after the update) and around the in-between pass's
 * GamestateDraw. NULL: off. */
typedef struct PresentReadAudit {
  void (*tick)(void);
  void (*pass)(int begin);
} PresentReadAudit;
extern const PresentReadAudit *g_present_reads;
/* The region of the PS1 address a (for the read audit's lines): scratchpad,
 * data, stack, mobys, particles, prims (the buffers and OTs the next draw
 * rewrites), level (everything else). */
const char *port_present_region(uint32_t a);

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

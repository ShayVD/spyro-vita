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

typedef struct {
  int widescreen; /* narrow the projection to 3/4 and show at 16:9 */
  int ws_logic;   /* widescreen in the logic pass too: one pass, but the
                     wider frustum reaches m_WasDrawn (gameplay differs) */
  int interp;     /* an extra frame between ticks, blended state */
  int interp_mobys; /* blend mobys and Spyro too, not only the camera */
  int stats;        /* time update and draw (also with both features off);
                     2 also maps the arena pages each pass dirties */
} PresentConfig;

typedef struct {
  long long ticks, passes, logic_passes, interp_frames, skipped_interp;
  long long us_pass, us_snapshot, us_logic, us_update; /* summed microseconds */
  long long us_vsync; /* inside VSync(0) during the logic pass (host vblank work) */
  long long spu_dropped; /* SPU writes swallowed inside presentation passes */
  long long squeezed_2d; /* 2D primitives narrowed for widescreen */
  long long snapshot_bytes; /* copied in and out by the region snapshot */
  long long dirty_pages, dirty_max; /* stats 2: 4 KB pages a pass wrote */
  uint32_t dirty_union[PRESENT_PAGES / 32]; /* every page any pass wrote */
  long long missed_pages; /* stats 2: dirty pages outside snapshot and scratch */
  uint32_t missed_union[PRESENT_PAGES / 32];
} PresentStats;

extern PresentConfig g_present;
extern PresentStats g_present_stats;

int port_present_active(void); /* any feature on */
/* Call once per vblank. Returns the frame to show now (PS1 15-bit,
 * PRESENT_W x PRESENT_H), or NULL to show the PS1 display as usual (no
 * presentation frame was made, e.g. boot logos or loading). */
const uint16_t *port_present_frame(void);
int port_present_frame_is_wide(void); /* the frame returned was widescreen */

/* order_table.c: a C-built 2D primitive went into the HUD OT. */
void port_present_note_2d(void *prim);

#ifdef __cplusplus
}
#endif

#endif

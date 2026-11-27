/* The two touch panels: fingers in raw and 0..1 coordinates, the rear panel's quarters (for buttons a PS1 / PS2
 * pad has and the Vita lacks: L2 / R2 / L3 / R3), and a tap detector (a tap opens a port's options menu).
 *
 * vpk_touch_init starts sampling on both panels and reads each one's active area (with the front panel's range,
 * 0..1919 x 0..1087, when the call fails).  vpk_touch_read gives a panel's fingers now.  vpk_touch_quarters says
 * which quarters the fingers are in, split at a raw point the caller picks.  VpkTap judges one finger poll by
 * poll: down and up within max_us, moving less than max_move (in whatever units the caller passes x and y in),
 * optionally no second finger; the caller decides where a tap counts.
 *
 * From kh1-decomp host/vita/pad.c (the rear quarters) and host/vita/menu.c (the tap) @ 85befa4a, and spyro-vita
 * src/platform/vita/pad.c (panel info, normalised fingers, the tap with a second-finger cancel; decisions D46)
 * @ 987363e. */
#ifndef VPK_TOUCH_H
#define VPK_TOUCH_H

#define VPK_TOUCH_FRONT 0
#define VPK_TOUCH_BACK 1
#define VPK_TOUCH_MAX 8

typedef struct {
  int x, y;   /* the panel's raw position */
  float u, v; /* 0..1 across its active area */
  int id;
} VpkTouchFinger;

typedef struct {
  int min_x, min_y, max_x, max_y; /* the active area, raw */
} VpkTouchPanel;

void vpk_touch_init(void);
const VpkTouchPanel *vpk_touch_panel(int port);
/* Up to max fingers on the panel now: how many, or -1 when the panel could not be read. */
int vpk_touch_read(int port, VpkTouchFinger *f, int max);

/* The quarters the fingers are in, split at (split_x, split_y) raw: bit 0 upper left, 1 upper right, 2 lower left,
 * 3 lower right (left: x < split_x; upper: y < split_y). */
#define VPK_TOUCH_UL 1u
#define VPK_TOUCH_UR 2u
#define VPK_TOUCH_LL 4u
#define VPK_TOUCH_LR 8u
unsigned vpk_touch_quarters(const VpkTouchFinger *f, int n, int split_x, int split_y);
/* the active area's middle, raw (vpk_touch_quarters' split for halves at 0.5) */
void vpk_touch_middle(int port, float *x, float *y);

typedef struct {
  unsigned long long max_us; /* down to up at most this long */
  float max_move;            /* a move this far (on either axis) or further is no tap */
  int cancel_multi;          /* a second finger (or a new one) is no tap */
  /* the detector's */
  int down, multi, id;
  unsigned long long t0;
  float x0, y0, x, y, moved;
} VpkTap;

/* One poll: n fingers now (n 0: none), the first at (x, y) with its id, at time now (us).  Returns 1 when that
 * finger has just been let go as a tap: its start and end positions are then (x0, y0) and (x, y). */
int vpk_tap_poll(VpkTap *t, int n, float x, float y, int id, unsigned long long now);

#endif /* VPK_TOUCH_H */

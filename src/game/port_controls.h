/* The port's control options that reach the game's camera (decisions D46,
 * D47): set by the platform (the Vita's menu, src/platform/vita/pad.c),
 * all off on the host so its replays stay the original's. */
#ifndef PORT_CONTROLS_H
#define PORT_CONTROLS_H

typedef struct {
  int free_camera; /* the DualShock's right stick turns and tilts the follow camera */
  int invert_x;    /* the right stick's turn the other way */
  int invert_y;    /* the right stick's tilt, and Triangle look mode's up/down */
  int speed;       /* turn speed in percent (100: L2/R2's) */
} PortControls;

extern PortControls g_port_controls;

#endif

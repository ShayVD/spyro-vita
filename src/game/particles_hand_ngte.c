/* The native-GTE build of particles_hand.c (fast_render=2 with ngte_fx=1,
 * render_hand.h HAND_RENDERER_UNIT): the same source with NGTE 1, which adds
 * native_draw's records (decisions D95) and the native GTE maths of its
 * `#if NGTE_MATH` blocks (D98). */
#define NGTE 1
#include "particles_hand.c"

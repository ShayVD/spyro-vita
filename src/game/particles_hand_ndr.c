/* The records-only build of particles_hand.c (fast_render=2 with ngte_fx=0, the
 * default until the hardware runs of D98): NGTE 1 for native_draw's records
 * (decisions D95), NGTE_MATH 0 for the GTE register file, as D95 made it. */
#define NGTE 1
#define NGTE_MATH 0
#include "particles_hand.c"

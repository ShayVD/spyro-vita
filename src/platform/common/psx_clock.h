/* The game's vblank clock (stage 7b, D104), one for every platform. libetc.c
 * is PsyQ's VSync over it, in place of PsyCross's LIBETC.C:
 *  - VSync(-1) (and VSync(n > 0), as PsyCross's) is psx_clock_count();
 *  - VSync(0) is the platform's psx_platform_vsync(): one virtual vblank on
 *    the host and with realtime=0 (D17); on the Vita by default, up to the
 *    display's vcount with psx_clock_due(), catching up after a late tick
 *    (D28, D90).
 * Each vblank the platform runs starts with psx_clock_tick(), and runs the
 * game's callback (psx_clock_callback) among its own work, in its own order.
 * Nothing here runs on a timer. */
#ifndef PSX_CLOCK_H
#define PSX_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_CLOCK_MAX_CATCH_UP 8 /* vblanks run at once after a slow tick; more are dropped */

int psx_clock_count(void); /* the vblanks run so far: VSync(-1) */
int psx_clock_tick(void);  /* one vblank starts: the count, plus 1 */
/* A save state was loaded (D51): its count from now on, and the display
 * counted afresh. */
void psx_clock_set(int vblank);
/* The game was paused (the Vita's menu): count the display afresh, with no
 * catch-up for the pause. */
void psx_clock_resync(void);

/* Real time: how many vblanks to run now, for VSync(0) asking for `count`.
 * vcount: the display's vblanks (any start, widened); wait: until its next
 * vblank. Waits while ours would be ahead of the display's; if ours are
 * behind, every one missed, at most PSX_CLOCK_MAX_CATCH_UP (the rest are
 * dropped: a load or a stall). *late gets the vblanks over `count`. The count
 * itself moves as the caller runs them (psx_clock_tick). */
int psx_clock_due(int count, int (*vcount)(void), void (*wait)(void), int *late);

/* The game's VSyncCallback, if it set one (libetc.c). */
void psx_clock_callback(void);

/* The platform's VSync(0): run the vblanks due and present
 * (src/platform/<host>/vblank.c). */
void psx_platform_vsync(void);

#ifdef __cplusplus
}
#endif

#endif

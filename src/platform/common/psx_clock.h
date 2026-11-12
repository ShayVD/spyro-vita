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

/* The display's vcount at our vblank 0, as psx_clock_due() counts it, so
 * another thread can tell which of our vblanks the display is at (stage 7c,
 * D112: vcount - base; at 50 Hz through the two calls below). Returns 0
 * while it isn't counted (before the first wait, after psx_clock_set or
 * psx_clock_resync). Any thread. */
int psx_clock_display_base(int *base);

/* The game's vblank rate (D138): 60, or 50 for the PAL disc at its own
 * timing (psx_region_vblank_hz), set by the platform once the disc is open.
 * psx_clock_due then runs 5 of our vblanks for every 6 of the display's. */
void psx_clock_set_hz(int hz);
int psx_clock_hz(void);
/* At 60 Hz: base + due, and n. At 50: the display vblank our vblank `due`
 * starts at, and display vblanks as ours (any thread). */
int psx_clock_display_due(int base, int due);
int psx_clock_display_to_ours(int n);

/* Frames due at the display's vblanks (D148): a platform that shows each
 * presentation frame at its due display vblank (the Vita's 7c, D112; the
 * host's --display60) sets this once, at start. Then, at 50 Hz, the dues
 * present.c files and the two calls above are the display's 60 Hz
 * vblanks (from the same base), so the presenter can blend a frame for
 * every display vblank by its time between two ticks. At 60 Hz nothing
 * changes. psx_clock_due_at: our vblank as a due, the first display vblank
 * at or after it (our vblank itself at 60 Hz or without this). */
void psx_clock_set_due_display(int on);
int psx_clock_due_display(void); /* on, and the clock at 50 Hz */
int psx_clock_due_at(int vblank);

/* The game's VSyncCallback, if it set one (libetc.c). */
void psx_clock_callback(void);

/* The platform's VSync(0): run the vblanks due and present
 * (src/platform/<host>/vblank.c). */
void psx_platform_vsync(void);

#ifdef __cplusplus
}
#endif

#endif

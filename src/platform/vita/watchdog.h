/* The hang watchdog (watchdog.c, decisions D71): breadcrumbs the threads
 * leave as they go (plain stores, read by the watchdog only when the game
 * thread has stopped), and the calls that feed it. */
#ifndef SPYRO_VITA_WATCHDOG_H
#define SPYRO_VITA_WATCHDOG_H

#include <psp2/types.h>

enum { WD_CTX_BOOT, WD_CTX_PLAY, WD_CTX_MENU, WD_CTX_STATE }; /* the game thread's context */
enum { WD_T_GAME, WD_T_RENDER, WD_T_MIXER, WD_T_XA, WD_T_COUNT };

typedef struct {
  volatile int game_part;      /* prof.c's PROF_* the game thread is in */
  volatile int game_ctx;       /* WD_CTX_* */
  volatile unsigned menu_frames; /* the options menu's frames: it pauses the vblanks */
  volatile int rt;             /* render thread: 0 asleep (nothing posted), else 1 + the command it runs */
  volatile unsigned rt_runs;   /* commands it has started */
  volatile int rt_wait;        /* game thread waits for the render thread: 1 + gpu_thread.c's W_* */
  volatile int xa;             /* XA read-ahead: 1 reading, 0 waiting for work */
} WdCrumbs;

extern WdCrumbs g_wd;

void vita_watchdog_start(void);           /* main.c, on the game thread */
void vita_watchdog_thread(int which, SceUID th); /* a thread's id, for its state in the hang line */
void vita_watchdog_vblank(int vblank);    /* vblank.c: the "where" lines, hang_test= */
int vita_watchdog_config(const char *key, const char *v); /* main.c: 1 if hang_dump= or hang_test= */

/* mixer.c: its phase (0 idle, 1 XA, 2 voices, 3 mix, 4 out), packets posted and done */
int vita_mixer_where(unsigned *posted, unsigned *done);

#endif

/* The host's presenter thread, for --pres-check-thread only (stage 7f,
 * decisions D126). The host draws the presenter's frame inline (its GR_*
 * calls go to the software rasterizer, which is the game thread's), so
 * this thread never draws: with --pres-check the presenter's list is only
 * recorded, and here it is recorded on a thread of its own beside D30's
 * pass and the logic draw, as the Vita's presenter runs beside the logic
 * draw. The compare then shows what the two draws running at once would
 * change (present.c joins the thread before it compares). */
#include "present.h"

#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>

static pthread_t s_th;
static sem_t s_go, s_done;
static void (*volatile s_fn)(void);
static int s_up;

static void *pres_main(void *arg) {
  (void)arg;
  for (;;) {
    while (sem_wait(&s_go) != 0)
      ;
    s_fn();
    sem_post(&s_done);
  }
  return NULL;
}

static int usable(void) { return 0; } /* the frame itself stays inline */

static void post(void (*fn)(void)) {
  s_fn = fn;
  sem_post(&s_go);
}

static void wait_done(void) {
  while (sem_wait(&s_done) != 0)
    ;
}

static const PresentPresOut s_out = {usable, NULL, post, wait_done, NULL, NULL,
                                     NULL,   NULL, NULL, NULL,      NULL};

/* 0 when the thread is up and g_present_pres set */
int host_pres_thread_init(void) {
  if (s_up)
    return 0;
  if (sem_init(&s_go, 0, 0) || sem_init(&s_done, 0, 0) ||
      pthread_create(&s_th, NULL, pres_main, NULL))
    return -1;
  s_up = 1;
  g_present_pres = &s_out;
  return 0;
}

/* --pres-check-late (stage 7g, D128; pres_watch.c): the presenter's list
 * for the compare recorded at the join, after the logic draw, on the game
 * thread */
static void (*s_late)(void);
static void late_post(void (*fn)(void)) { s_late = fn; }
static void late_wait(void) {
  if (s_late)
    s_late();
  s_late = NULL;
}
static const PresentPresOut s_late_out = {usable, NULL, late_post, late_wait, NULL, NULL,
                                          NULL,   NULL, NULL,      NULL,      NULL};

void host_pres_late_init(void) { g_present_pres = &s_late_out; }

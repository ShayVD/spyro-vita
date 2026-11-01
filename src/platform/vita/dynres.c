/* Dynamic resolution (config.txt dynres=, the menu's DYNAMIC RES row;
 * decisions D75): the game's targets drawn into a smaller part of
 * themselves when the GPU can't keep up, and scaled up when shown.
 *
 * gpu_gl.c does the drawing: a target takes the scale at its whole clear
 * (the start of the game's next frame there), draws into the dpw x dph
 * texels at its texture's origin with the same NDC mapping, and the present
 * samples that part. Here is only the choice of scale, on the thread that
 * calls vitaGL, once per picture:
 *   - dynres=0 (off): always 1, every draw exactly as before;
 *   - dynres=88, 75 or 67: that scale, always;
 *   - dynres=1 (auto): the GPU's busy time per picture (gpu_meter.c),
 *     averaged over WINDOW pictures, against what a picture may take: a
 *     vblank with 60 fps on, two without. Over HI of it, down to the
 *     largest step predicted to fit in FIT; up one step once the next is
 *     predicted to fit in UP for a whole hold (2 s, doubled to 16 s by a
 *     fall soon after a rise, as the 60 fps budget's hold, D68).
 * A picture's GPU time is the scenes gpu_meter.c measures (the targets,
 * atlas decodes) plus the present's full-screen quad, which it can't
 * (PRESENT_US, an estimate; the same at every scale).
 * The prediction: the measured part is a fixed part plus a part that goes
 * with the pixels (the scale squared). Going down, the fixed part is taken
 * as FIXED_US (the target's clear and store), which predicts less saving
 * than all-pixels would; going up, as 0, which predicts more cost. So both
 * err towards the lower scale, and the gap between FIT and UP is the
 * hysteresis. */
#include "vita.h"
#include "gpu_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The steps, per mille of the screen's pixels in each direction: about a
 * quarter fewer pixels each (960x544, 840x476, 720x408, 641x363). */
static const int k_steps[] = {1000, 875, 750, 667};
#define NSTEPS (int)(sizeof k_steps / sizeof k_steps[0])
#define WINDOW 12         /* pictures per measure: 0.2 s at 60 fps */
#define SETTLE 3          /* pictures after a change still in flight at the old scale */
#define VBLANK_US 16683   /* NTSC */
#define HI 94             /* % of a picture's time: over it, down */
#define FIT 85            /* % a step down is chosen to fit in */
#define UP 80             /* % the next step up must be predicted to fit in */
#define FIXED_US 1000
#define PRESENT_US 1500   /* one 960x544 pass: 1.3-1.8 ms on hardware (D69's estimate) */
#define HOLD_MIN 10       /* windows (2 s) the next step must fit before going up */
#define HOLD_MAX 80       /* 16 s */
#define STEADY 150        /* windows (30 s) after a rise with no fall: the hold is HOLD_MIN */

static int s_step;            /* the scale in use (index into k_steps) */
static volatile int s_pm = 1000; /* k_steps[s_step], for gpu_gl.c and the game thread */
static int s_mode = -1;       /* the dynres= it was set up for */
static int s_settle, s_n, s_fit_windows, s_hold = HOLD_MIN, s_since_up = STEADY;
static uint32_t s_busy0;
static int s_have0;

/* per report: pictures and GPU time at each step, and the changes */
static struct {
  unsigned pics[NSTEPS];
  uint64_t busy[NSTEPS];
  unsigned downs, ups;
  unsigned last_cost; /* us per picture, the last window */
} s_st;

int vita_gpu_meter(uint32_t *busy_us, uint32_t *scenes); /* gpu_meter.c */
void vita_gpu_meter_start(void);
unsigned vita_gpu_meter_dropped(void);

/* dynres=N (percent): the nearest step (88 -> 875, 67 -> 667) */
static int fixed_step(int mode) {
  int i, best = 0;
  for (i = 1; i < NSTEPS; i++)
    if (abs(mode * 10 - k_steps[i]) < abs(mode * 10 - k_steps[best]))
      best = i;
  return best;
}

static void set_step(int i) {
  s_step = i;
  __atomic_store_n(&s_pm, k_steps[i], __ATOMIC_RELEASE);
  s_settle = SETTLE;
  s_n = 0;
  s_have0 = 0;
}

/* The scale a target takes at its next whole clear, per mille: 1000 when
 * off (gpu_gl.c then draws exactly as without dynres). */
int dynres_scale(void) { return __atomic_load_n(&s_pm, __ATOMIC_ACQUIRE); }

/* 1 while dynres=auto could still go down (gpu_thread.c: late pictures are
 * then dynres's to fix before the 60 fps budget's, D75). Any thread. */
int dynres_room(void) {
  uint32_t busy, scenes;
  return g_vita_opts.dynres == 1 && g_vita_opts.gpu_res == 0 &&
         __atomic_load_n(&s_pm, __ATOMIC_ACQUIRE) > k_steps[NSTEPS - 1] &&
         vita_gpu_meter(&busy, &scenes);
}

static int predict(unsigned cost, int from, int to, int fixed) {
  long long f = fixed < (int)cost ? fixed : (int)cost;
  long long r = (long long)k_steps[to] * k_steps[to], q = (long long)k_steps[from] * k_steps[from];
  return (int)(f + ((long long)cost - f) * r / q);
}

/* cost: the measured scenes' GPU time per picture, us; with the present's
 * quad, against a picture's time */
static void window(unsigned cost) {
  int frame = VBLANK_US * (g_vita_opts.interp ? 1 : 2), i;
  int total = (int)cost + PRESENT_US;
  s_st.last_cost = cost;
  if (s_since_up < STEADY && ++s_since_up == STEADY)
    s_hold = HOLD_MIN;
  if (total * 100 > frame * HI && s_step < NSTEPS - 1) {
    for (i = s_step + 1; i < NSTEPS - 1; i++)
      if ((predict(cost, s_step, i, FIXED_US) + PRESENT_US) * 100 <= frame * FIT)
        break;
    vita_log("dynres: GPU %u.%02u ms a picture at %d%% (with the present's %d.%d ms), over %d%% "
             "of %u.%02u ms: %d%%", total / 1000, total % 1000 / 10, k_steps[s_step] / 10,
             PRESENT_US / 1000, PRESENT_US % 1000 / 100, HI, frame / 1000, frame % 1000 / 10,
             k_steps[i] / 10);
    if (s_since_up < STEADY) /* fell soon after a rise: wait longer next time */
      s_hold = s_hold * 2 > HOLD_MAX ? HOLD_MAX : s_hold * 2;
    s_fit_windows = 0;
    s_st.downs++;
    set_step(i);
    return;
  }
  if (s_step > 0 && (predict(cost, s_step, s_step - 1, 0) + PRESENT_US) * 100 <= frame * UP) {
    if (++s_fit_windows >= s_hold) {
      vita_log("dynres: GPU %u.%02u ms a picture at %d%% for %d windows, %d%% predicted to fit: "
               "%d%%", total / 1000, total % 1000 / 10, k_steps[s_step] / 10, s_hold, UP,
               k_steps[s_step - 1] / 10);
      s_fit_windows = 0;
      s_since_up = 0;
      s_st.ups++;
      set_step(s_step - 1);
    }
  } else {
    s_fit_windows = 0;
  }
}

/* Every picture, from gl_present (the vitaGL thread). With the options
 * panel up the game is paused and the same picture is shown again: not
 * measured (it would let the scale rise during every menu visit). */
void dynres_picture(int menu) {
  int mode = g_vita_opts.gpu_res == 0 ? g_vita_opts.dynres : 0;
  uint32_t busy, scenes;
  if (mode != s_mode) {
    if (mode == 1 || g_vita_opts.gpu_meter)
      vita_gpu_meter_start(); /* once; it stays (it only reads) */
    s_mode = mode;
    s_hold = HOLD_MIN;
    s_since_up = STEADY;
    s_fit_windows = 0;
    set_step(mode == 1 ? 0 : mode ? fixed_step(mode) : 0);
    vita_log("dynres: %s, scale %d%% (D75)", mode == 1 ? "auto" : mode ? "fixed" : "off",
             k_steps[s_step] / 10);
  }
  if (!vita_gpu_meter(&busy, &scenes))
    return;
  if (menu) {
    s_have0 = 0; /* the window starts again after the menu */
    return;
  }
  if (s_settle) { /* the pictures in flight were drawn at the old scale */
    s_settle--;
    return;
  }
  if (!s_have0) {
    s_busy0 = busy;
    s_have0 = 1;
    s_n = 0;
    return;
  }
  if (++s_n < WINDOW)
    return;
  {
    unsigned cost = (busy - s_busy0) / (unsigned)s_n;
    s_st.pics[s_step] += s_n;
    s_st.busy[s_step] += busy - s_busy0;
    s_busy0 = busy;
    s_n = 0;
    if (mode == 1)
      window(cost);
  }
}

/* One "dynres" line per report (gl_report). */
void dynres_report(int vblank) {
  char line[256];
  int i, len = 0, any = 0;
  for (i = 0; i < NSTEPS; i++) {
    unsigned avg = s_st.pics[i] ? (unsigned)(s_st.busy[i] / s_st.pics[i]) : 0;
    any |= s_st.pics[i] != 0;
    if (s_st.pics[i])
      len += snprintf(line + len, sizeof line - len, "%s%d%%: %u pictures, %u.%02u ms", len ? "; " : "",
                      k_steps[i] / 10, s_st.pics[i], avg / 1000, avg % 1000 / 10);
  }
  if (any || s_st.downs || s_st.ups)
    vita_log("dynres %d: GPU time per picture (the measured scenes, without the present's quad) "
             "%s; %u down, %u up, now %d%% (hold %d windows; %u scenes not measured)",
             vblank, any ? line : "(none measured)", s_st.downs, s_st.ups, k_steps[s_step] / 10,
             s_hold, vita_gpu_meter_dropped());
  memset(&s_st, 0, sizeof s_st);
}

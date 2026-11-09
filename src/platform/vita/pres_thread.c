/* The presenter's thread (stage 7f, decisions D126): with interp_mode=2, the
 * in-between frame in play is drawn here, on a core of its own, beside the
 * logic draw on the game thread (src/game/present.c's presenter, recording
 * into gpu_thread.c's buffers). Core 1 (the mixer's, the XA reader's and
 * the parse thread's, behind them), or core 3 with pres_core=3 when the
 * start-up check (parse.c, D71) found a thread of ours may run there.
 *
 * The game thread hands it one job a tick (vita_pres_post) and joins it
 * before the next update (vita_pres_wait, from port_present_pres_join). */
#include "vita.h"
#include "../../game/present.h"

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#define PRIORITY 0x9A /* behind the parse thread (0x98): the logic's list first */
#define STACK (256 << 10)

static SceUID s_th = -1, s_go, s_done;
static int s_core;
static void (*volatile s_fn)(void);
static volatile uint32_t s_busy_us, s_jobs; /* counted up; the report takes the differences */
extern unsigned g_faces_in_place; /* src/game/render_ctx.c (moby_hand.c) */

static unsigned long long now_us(void) { return sceKernelGetProcessTimeWide(); }

static int pres_main(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  for (;;) {
    unsigned long long t0;
    sceKernelWaitSema(s_go, 1, NULL);
    t0 = now_us();
    s_fn();
    s_busy_us += (uint32_t)(now_us() - t0);
    s_jobs++;
    sceKernelSignalSema(s_done, 1);
  }
  return 0;
}

int vita_pres_thread_up(void) { return s_th >= 0; }

void vita_pres_post(void (*fn)(void)) {
  s_fn = fn;
  sceKernelSignalSema(s_go, 1);
}

void vita_pres_wait(void) {
  unsigned long long t0 = now_us();
  prof_enter(PROF_WAIT);
  for (;;) {
    SceUInt tmo = 5000000;
    if (sceKernelWaitSema(s_done, 1, &tmo) >= 0)
      break;
    vita_log("pres: still waiting for the presenter's frame after %u s",
             (unsigned)((now_us() - t0) / 1000000));
  }
  prof_leave();
}

void vita_pres_init(void) {
  unsigned mask = SCE_KERNEL_CPU_MASK_USER_1;
  SceUID th;
  if (g_vita_opts.interp_mode != 2)
    return;
  if (!g_vita_opts.pres_thread) {
    vita_log("pres: pres_thread=0: the presenter draws on the game thread (as 7e, D125)");
    return;
  }
  g_present_pres = vita_gpu_pres_out();
  if (!g_present_pres) {
    vita_log("pres: the presenter's thread needs gpu=1, render_thread=1 and its buffers: the "
             "presenter draws on the game thread (D126)");
    return;
  }
  s_core = 1;
  if (g_vita_opts.pres_core == 3) {
    if (vita_core3_ok()) {
      mask = SCE_KERNEL_CPU_MASK_SYSTEM;
      s_core = 3;
    } else {
      vita_log("pres: pres_core=3, but core 3 isn't usable (the cores line): core 1");
    }
  }
  s_go = sceKernelCreateSema("spyro_pres_go", 0, 0, 1, NULL);
  s_done = sceKernelCreateSema("spyro_pres_done", 0, 0, 1, NULL);
  th = sceKernelCreateThread("spyro_pres", pres_main, PRIORITY, STACK, 0, (int)mask, NULL);
  if (s_go < 0 || s_done < 0 || th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("pres: no thread (%08x): the presenter draws on the game thread", (unsigned)th);
    g_present_pres = NULL;
    return;
  }
  s_th = th;
  vita_log("pres: in play the presenter draws the in-between frame on its own thread, core %d, "
           "priority 0x%x, beside the logic draw (stage 7f, D126)",
           s_core, PRIORITY);
}

/* "pres" line: the presenter's frames, its thread's time, the game
 * thread's waits for it, and the render thread's side */
void vita_pres_report(int vblank) {
  static unsigned long long t0;
  static int v0;
  static uint32_t busy0, jobs0;
  static unsigned faces0;
  unsigned long long t = now_us();
  PresentPresStats s;
  uint32_t rt[6] = {0, 0, 0, 0, 0, 0}, busy = s_busy_us, jobs = s_jobs;
  if (g_vita_opts.interp_mode != 2)
    return;
  port_present_pres_stats(&s);
  if (g_present_pres)
    vita_gpu_pres_report(rt);
  if (t0 && vblank > v0 && (s.worker_frames || s.inline_frames)) {
    unsigned n = (unsigned)(vblank - v0), wall = (unsigned)(t - t0), b = busy - busy0;
    vita_log("pres %d: %lld frames on core %d, %lld inline; its thread busy %u.%02u ms per vblank "
             "(%u%%, %u jobs); the game thread waited for it %lld.%02lld ms per vblank in %lld of "
             "the joins; the render thread ran %u of its buffers, passed %u by (not ready by their "
             "vblank) and waited %u.%02u ms per vblank for them; %u waits for a free buffer, %u "
             "too big; %lld display environments not the presenter's, %lld frames uncaptured; "
             "%u face lists too long for the logic draw's copy (replaced in place); %u presents of "
             "a slot never filed (the screen kept)",
             vblank, s.worker_frames, s_core, s.inline_frames, b / n / 1000, b / n % 1000 / 10,
             wall ? (unsigned)(b * 100ull / wall) : 0, jobs - jobs0, s.join_wait_us / n / 1000,
             s.join_wait_us / n % 1000 / 10, s.joins_waited, rt[0], rt[1], rt[2] / n / 1000,
             rt[2] / n % 1000 / 10, rt[3], rt[4], s.disp_misses, s.uncaptured,
             g_faces_in_place - faces0, rt[5]);
  }
  faces0 = g_faces_in_place;
  t0 = t;
  v0 = vblank;
  busy0 = busy;
  jobs0 = jobs;
}

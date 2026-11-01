/* The parse thread (config.txt parse_thread=1, decisions D69): PsyCross's
 * OT walk (ParsePrimitivesLinkedList and DrawAllSplits, which record the
 * render thread's commands) on a thread of its own, walking a copy of the
 * frame's primitives (src/platform/common/walk.c) while the game thread goes
 * on with the next frame. The game thread joins at its next PsyCross call or
 * VSync: GamestateDraw's DrawSync(0), after the next tick's render path.
 * Needs the render thread (D63): the walk's GR_* calls are commands there,
 * so no GL call runs here.
 *
 * Also the start-up check of core 3, the core the Vita keeps for the system
 * (SCE_KERNEL_CPU_MASK_SYSTEM): whether a thread of ours is let run there,
 * and how much of it it gets. parse_core=3 uses it only if so. */
#include "vita.h"
#include "psx_walk.h"
#include "../../game/present.h"

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>

extern int g_rt_active; /* gpu_thread.c */

#define PRIORITY 0x98 /* behind the mixer (0x70) and the XA reader (0x90) on core 1 */
#define STACK (256 << 10)
#define PROBE_US 50000

static unsigned long long now_us(void) { return sceKernelGetProcessTimeWide(); }

/* ---- core 3 ---- */

static int s_core3_ok;
static volatile int s_probe_cpu0 = -1, s_probe_cpu1 = -1;
static volatile unsigned s_probe_ran_us, s_probe_wall_us;

/* Spins for PROBE_US: the time it ran is the sum of the gaps between clock
 * reads under 200 us; longer gaps are time another thread had the core. */
static int probe_main(SceSize args, void *argp) {
  unsigned long long t0 = now_us(), last = t0, t, ran = 0;
  (void)args;
  (void)argp;
  s_probe_cpu0 = sceKernelGetCpuId();
  while ((t = now_us()) - t0 < PROBE_US) {
    if (t - last < 200)
      ran += t - last;
    last = t;
  }
  s_probe_ran_us = (unsigned)ran;
  s_probe_wall_us = (unsigned)(t - t0);
  s_probe_cpu1 = sceKernelGetCpuId();
  return 0;
}

void vita_cores_probe(void) {
  SceUInt tmo = 1000000;
  int status = 0, mask, r;
  SceUID th = sceKernelCreateThread("spyro_core3_probe", probe_main, PRIORITY, 0x4000, 0,
                                    SCE_KERNEL_CPU_MASK_SYSTEM, NULL);
  if (th < 0) {
    vita_log("cores: no thread of ours on core 3 (SCE_KERNEL_CPU_MASK_SYSTEM): "
             "sceKernelCreateThread %08x (D69)", (unsigned)th);
    return;
  }
  mask = sceKernelGetThreadCpuAffinityMask(th);
  r = sceKernelStartThread(th, 0, NULL);
  if (r < 0) {
    vita_log("cores: a thread with core 3's mask was made (affinity %08x) but not started: %08x "
             "(D69)", (unsigned)mask, (unsigned)r);
    sceKernelDeleteThread(th);
    return;
  }
  r = sceKernelWaitThreadEnd(th, &status, &tmo);
  if (r < 0) {
    vita_log("cores: a thread with core 3's mask (affinity %08x) didn't finish a %u ms spin in "
             "1 s: %08x (D69)", (unsigned)mask, PROBE_US / 1000, (unsigned)r);
    sceKernelDeleteThread(th); /* fails if it is still running; it ends on its own */
    return;
  }
  sceKernelDeleteThread(th);
  {
    unsigned share = s_probe_wall_us ? s_probe_ran_us * 100u / s_probe_wall_us : 0;
    s_core3_ok = s_probe_cpu0 == 3 && s_probe_cpu1 == 3 && share >= 50;
    vita_log("cores: a thread with core 3's mask (SCE_KERNEL_CPU_MASK_SYSTEM, affinity %08x) ran "
             "on CPU %d to %d and had %u%% of a %u ms spin: core 3 %s (D69)",
             (unsigned)mask, s_probe_cpu0, s_probe_cpu1, share, s_probe_wall_us / 1000,
             s_core3_ok ? "usable (parse_core=3 may use it)" : "not usable");
  }
}

/* ---- the parse thread ---- */

static SceUID s_th = -1, s_go, s_done;
static int s_core;
static uintptr_t s_stack_lo, s_stack_hi; /* the parse thread's stack */

static int parse_main(SceSize args, void *argp) {
  uintptr_t top = (uintptr_t)&args;
  (void)argp;
  /* its stack: from here (near the top) down by the stack's size; then
   * tell vita_parse_init it's ready */
  s_stack_hi = top + 256;
  s_stack_lo = top - STACK + 4096;
  sceKernelSignalSema(s_done, 1);
  for (;;) {
    sceKernelWaitSema(s_go, 1, NULL);
    psx_walk_run();
    sceKernelSignalSema(s_done, 1);
  }
  return 0;
}

static void post(void) { sceKernelSignalSema(s_go, 1); }

/* the game thread: until the walk handed over has run (charged to "ot",
 * where the walk itself was before) */
static void wait_done(void) {
  unsigned long long t0 = now_us();
  prof_enter(PROF_OT);
  for (;;) {
    SceUInt tmo = 5000000;
    if (sceKernelWaitSema(s_done, 1, &tmo) >= 0)
      break;
    vita_log("parse: still waiting for the OT walk after %u s",
             (unsigned)((now_us() - t0) / 1000000));
  }
  prof_leave();
}

/* Called for every command and every profiled GR_* call: a stack address
 * tells the threads apart without a kernel call. */
int vita_parse_on_walker(void) {
  uintptr_t sp = (uintptr_t)&sp;
  return sp - s_stack_lo < s_stack_hi - s_stack_lo;
}

static const PsxWalkThread s_thr = {post, wait_done, vita_parse_on_walker, vita_gpu_walk_submitted};

void vita_parse_init(void) {
  unsigned mask = SCE_KERNEL_CPU_MASK_USER_1;
  SceUID th;
  if (!g_vita_opts.parse_thread) {
    vita_log("parse: OT walk on the game thread (parse_thread=0)");
    return;
  }
  if (!g_vita_gpu || !g_rt_active) {
    vita_log("parse: parse_thread=1 needs gpu=1 and render_thread=1: OT walk on the game thread");
    return;
  }
  s_core = 1;
  if (g_vita_opts.parse_core == 3) {
    if (s_core3_ok) {
      mask = SCE_KERNEL_CPU_MASK_SYSTEM;
      s_core = 3;
    } else {
      vita_log("parse: parse_core=3, but core 3 isn't usable (the cores line): core 1");
    }
  }
  s_go = sceKernelCreateSema("spyro_parse_go", 0, 0, 1, NULL);
  s_done = sceKernelCreateSema("spyro_parse_done", 0, 0, 1, NULL);
  th = sceKernelCreateThread("spyro_parse", parse_main, PRIORITY, STACK, 0, (int)mask, NULL);
  if (s_go < 0 || s_done < 0 || th < 0) {
    vita_log("parse: no thread (%08x): OT walk on the game thread", (unsigned)th);
    return;
  }
  if (sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("parse: thread didn't start: OT walk on the game thread");
    sceKernelDeleteThread(th);
    return;
  }
  sceKernelWaitSema(s_done, 1, NULL); /* it has its stack's range */
  s_th = th; /* it waits for its first list */
  psx_walk_set_clock(now_us);
  if (psx_walk_init(PSX_WALK_THREAD, port_present_prim_ranges, &s_thr) != 0) {
    vita_log("parse: no memory for the primitives' copy or the vertex cache's second table: "
             "OT walk on the game thread");
    s_th = -1;
    s_stack_lo = s_stack_hi = 0;
    return;
  }
  vita_log("parse: OT walk on its own thread, core %d, priority 0x%x, from a copy of the "
           "primitives (parse_thread=1, D69)",
           s_core, PRIORITY);
}

/* "parse" line: the walks, the copies, and how long the game thread waited */
void vita_parse_report(int vblank) {
  static unsigned long long t0;
  static int v0;
  unsigned long long t = now_us();
  PsxWalkStats w;
  if (s_th < 0)
    return;
  psx_walk_stats_take(&w);
  if (t0 && vblank > v0) {
    unsigned n = (unsigned)(vblank - v0), wall = (unsigned)(t - t0);
    vita_log("parse %d: %u lists walked on core %d (%u in place), busy %u.%02u ms per vblank "
             "(%u%%); copied %llu KB in %u.%02u ms per vblank on the game thread; it waited for "
             "the walk %u.%02u ms per vblank in %u joins; %u packets outside the copy (last at "
             "%08x); %u calls deferred",
             vblank, w.walks, s_core, w.in_place, (unsigned)(w.walk_us / n / 1000),
             (unsigned)(w.walk_us / n % 1000 / 10),
             wall ? (unsigned)(w.walk_us * 100 / wall) : 0, w.copied / 1024,
             (unsigned)(w.copy_us / n / 1000), (unsigned)(w.copy_us / n % 1000 / 10),
             (unsigned)(w.wait_us / n / 1000), (unsigned)(w.wait_us / n % 1000 / 10), w.joins,
             w.foreign, (unsigned)w.foreign_at, w.deferred);
  }
  t0 = t;
  v0 = vblank;
}

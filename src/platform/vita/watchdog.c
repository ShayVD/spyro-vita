/* The hang watchdog (decisions D73). A freeze used to leave nothing in
 * spyro.log: the lines just stopped. Now:
 *
 *   - Breadcrumbs (g_wd, plain stores): the game thread's part of the frame
 *     (prof.c's parts: game logic, OT, raster, SPU, present, wait) and its
 *     context (play, the options menu, a save state); the render thread's
 *     command and whether the game thread waits for it (gpu_thread.c); the
 *     mixer's phase (mixer.c) and the XA read-ahead's (xa_ahead.c).
 *   - A "where" line at every change of level or gamestate, from the game's
 *     own globals (src/game/where_port.c), so a log that stops says where.
 *   - A thread that looks once a second. If the game thread's vblank count
 *     (and the options menu's frame count, which runs while the vblanks are
 *     paused) hasn't moved for LIMIT_S seconds, LIMIT_MENU_S in the menu or
 *     with bench= (a shader compile there takes 4-10 s on hardware), it logs
 *     one "hang" line: every thread's breadcrumb and kernel state (running,
 *     or waiting and on what), the CPU time each used in the last second,
 *     the vblank, the level and gamestate, the disc reads. If the game moves
 *     again, a "hang: over" line.
 *   - hang_dump=1 (the default): DUMP_AFTER_S seconds after the hang line,
 *     still stuck, it stops the app with an undefined instruction, so the
 *     Vita writes a crash dump (a .psp2dmp in ux0:data) with every thread's
 *     registers and stack; docs/vita-testing.md says how to read it.
 *
 * It arms only once the vblank count reaches ARM_VBLANK (start-up's shader
 * compile and the first frames stall for seconds), never with shot_hold=
 * (the game holds for a window grab), and a second whose sleep took over
 * GAP_S seconds (the app suspended: PS button, standby) starts the count
 * again. hang_test=V (spin) or hang_test=V,wait (block on a semaphore
 * nobody signals) stops the game thread at vblank V, to see all of this. */
#include "vita.h"
#include "watchdog.h"
#include "psx_cdimage.h"
#include "../../game/cheats_port.h"
#include "../../game/where_port.h"

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRIORITY 0xB0 /* behind the game thread (0xA0); a second's work is a few syscalls */
#define ARM_VBLANK 600
#define LIMIT_S 10
#define LIMIT_MENU_S 60
#define DUMP_AFTER_S 20 /* the start-up line says 20 */
#define GAP_S 3

WdCrumbs g_wd;

static SceUID s_tid[WD_T_COUNT];
static int s_dump = 1;      /* hang_dump= */
static int s_test, s_test_wait; /* hang_test=V[,wait] */
static PortWhere s_where;   /* the game thread's, at the last vblank */
static int s_where_valid;
static volatile unsigned s_spin;

/* prof.c's, gpu_thread.c's C_* and W_*, mixer.c's phases: the same order */
static const char *const k_parts[] = {"game logic", "OT walk", "raster", "SPU/CD", "present", "wait"};
static const char *const k_ctx[] = {"start-up", "play", "options menu", "save state"};
static const char *const k_cmds[] = {"wrap", "env", "clip", "texture", "blend", "vertices", "draw",
                                     "clear", "copy", "call", "present", "capture", "commit"};
static const char *const k_rt_waits[] = {"ring full", "previous frame not shown yet", "sync call",
                                         "vertex slabs", "GPU start-up"};
static const char *const k_mixer[] = {"idle", "XA decode", "voices", "mix", "audio out"};

#define NAME(table, i) \
  ((i) >= 0 && (i) < (int)(sizeof table / sizeof table[0]) ? table[i] : "?")

int vita_watchdog_config(const char *key, const char *v) {
  if (!strcmp(key, "hang_dump")) {
    s_dump = atoi(v);
    return 1;
  }
  if (!strcmp(key, "hang_test")) {
    s_test = atoi(v);
    s_test_wait = strstr(v, "wait") != NULL;
    return 1;
  }
  return 0;
}

void vita_watchdog_thread(int which, SceUID th) {
  if (which >= 0 && which < WD_T_COUNT)
    s_tid[which] = th;
}

/* "ARTISANS: STONE HILL (level 11)"; "level 0" outside the levels (the title) */
static const char *level_text(int level, char *buf, int n) {
  char name[48];
  if (level >= 10 && level < 65)
    snprintf(buf, n, "%s (level %d)", port_level_name(level, name, sizeof name), level);
  else
    snprintf(buf, n, "level %d", level);
  return buf;
}

/* Game thread, every vblank. */
void vita_watchdog_vblank(int vblank) {
  PortWhere w;
  port_where(&w);
  if (!s_where_valid || w.level != s_where.level || w.gamestate != s_where.gamestate) {
    char name[64];
    vita_log("where: %s, gamestate %d %s%s", level_text(w.level, name, sizeof name),
             w.gamestate, port_gamestate_name(w.gamestate),
             w.demo == 1 ? ", attract demo" : "");
  }
  s_where = w;
  s_where_valid = 1;
  g_wd.game_ctx = WD_CTX_PLAY;
  if (s_test > 0 && vblank >= s_test) {
    s_test = 0;
    vita_log("hang_test: the game thread %s from vblank %d",
             s_test_wait ? "waits on a semaphore nobody signals" : "spins", vblank);
    if (s_test_wait)
      sceKernelWaitSema(sceKernelCreateSema("spyro_hang_test", 0, 0, 1, NULL), 1, NULL);
    for (;;)
      s_spin++;
  }
}

/* ---- the watchdog thread ---- */

static uint64_t s_run[WD_T_COUNT]; /* each thread's CPU time at the last look */
static uint64_t s_run_dt[WD_T_COUNT]; /* and in the second before it */

static void sample(void) {
  int i;
  for (i = 0; i < WD_T_COUNT; i++) {
    SceKernelThreadInfo info;
    if (!s_tid[i])
      continue;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    if (sceKernelGetThreadInfo(s_tid[i], &info) < 0)
      continue;
    s_run_dt[i] = s_run[i] ? info.runClocks - s_run[i] : 0;
    s_run[i] = info.runClocks;
  }
}

/* "running, 998 ms CPU in the last second", "waiting on spyro_render_game
 * (sema 0x..., wait type 0x...), 0 ms CPU ..." */
static int thread_text(char *buf, int n, int which) {
  SceKernelThreadInfo info;
  int len;
  if (!s_tid[which])
    return snprintf(buf, n, "no thread");
  memset(&info, 0, sizeof info);
  info.size = sizeof info;
  if (sceKernelGetThreadInfo(s_tid[which], &info) < 0)
    return snprintf(buf, n, "no thread info");
  if (info.status & SCE_THREAD_RUNNING)
    len = snprintf(buf, n, "running on core %d", (int)info.currentCpuId);
  else if (info.status & SCE_THREAD_READY)
    len = snprintf(buf, n, "ready (no core free)");
  else if (info.status & SCE_THREAD_WAITING) {
    SceKernelSemaInfo sema;
    memset(&sema, 0, sizeof sema);
    sema.size = sizeof sema;
    if (info.waitId > 0 && sceKernelGetSemaInfo(info.waitId, &sema) >= 0)
      len = snprintf(buf, n, "waiting on semaphore %s (count %d, wait type 0x%x)", sema.name,
                     sema.currentCount, (unsigned)info.waitType);
    else
      len = snprintf(buf, n, "waiting (wait type 0x%x, on 0x%x)", (unsigned)info.waitType,
                     (unsigned)info.waitId);
  } else {
    len = snprintf(buf, n, "status 0x%x", (unsigned)info.status);
  }
  if (len < n)
    len += snprintf(buf + len, n - len, ", %u ms CPU in the last second",
                    (unsigned)(s_run_dt[which] / 1000));
  return len;
}

static void report(int vblank, int stuck, int limit) {
  char line[1024], t[WD_T_COUNT][160];
  char name[64];
  unsigned posted = 0, done = 0;
  int phase, i, rt = g_wd.rt, rt_wait = g_wd.rt_wait;
  PsxCdIo io;
  for (i = 0; i < WD_T_COUNT; i++)
    thread_text(t[i], sizeof t[i], i);
  phase = vita_mixer_where(&posted, &done);
  psx_cd_io(&io);
  snprintf(line, sizeof line,
           "hang: no vblank for %d s (limit %d s) at vblank %d; in %s, gamestate %d %s, "
           "load stage %d%s. game thread: %s, %s; %s. render thread: %s (%u started); %s; game "
           "thread %s%s. mixer: %s, %u packets posted, %u done; %s. xa read-ahead: %s; %s. cd: %u "
           "reads",
           stuck, limit, vblank, level_text(s_where.level, name, sizeof name), s_where.gamestate, port_gamestate_name(s_where.gamestate), s_where.load_stage,
           s_where.demo == 1 ? ", attract demo" : "", NAME(k_ctx, g_wd.game_ctx),
           NAME(k_parts, g_wd.game_part), t[WD_T_GAME],
           rt ? NAME(k_cmds, rt - 1) : "asleep, nothing posted", g_wd.rt_runs, t[WD_T_RENDER],
           rt_wait ? "waits for it: " : "doesn't wait for it",
           rt_wait ? NAME(k_rt_waits, rt_wait - 1) : "", NAME(k_mixer, phase), posted, done,
           t[WD_T_MIXER], g_wd.xa ? "reading" : "waiting for work", t[WD_T_XA], io.reads);
  vita_log("%s", line);
}

static int wd_main(SceSize args, void *argp) {
  int armed = 0, stuck = 0, logged = 0, last_v = -1;
  unsigned last_menu = 0;
  int64_t t_last = sceKernelGetSystemTimeWide();
  (void)args;
  (void)argp;
  for (;;) {
    int v, limit;
    unsigned menu;
    int64_t t;
    sceKernelDelayThread(1000000);
    t = sceKernelGetSystemTimeWide();
    v = vita_vblank_count();
    menu = g_wd.menu_frames;
    sample();
    if (!armed) {
      if (v < ARM_VBLANK || g_vita_opts.shot_hold > 0)
        continue;
      armed = 1;
      vita_log("watchdog: armed at vblank %d", v);
    }
    if (t - t_last > GAP_S * 1000000LL || v != last_v || menu != last_menu) {
      if (logged)
        vita_log("hang: over, the game thread moved again after %d s (vblank %d)", stuck, v);
      stuck = logged = 0;
      last_v = v;
      last_menu = menu;
      t_last = t;
      continue;
    }
    t_last = t;
    stuck++;
    limit = g_wd.game_ctx == WD_CTX_MENU || g_vita_opts.bench ? LIMIT_MENU_S : LIMIT_S;
    if (!logged && stuck >= limit) {
      report(v, stuck, limit);
      logged = 1;
      if (s_dump)
        vita_log("hang: a crash dump in %d s if it stays stuck (hang_dump=1)", DUMP_AFTER_S);
    }
    if (logged && s_dump && stuck >= limit + DUMP_AFTER_S) {
      vita_log("hang: stopping the app for a crash dump (a .psp2dmp in ux0:data; hang_dump=0 "
               "turns this off)");
      sceKernelDelayThread(200000);
      __builtin_trap();
    }
  }
  return 0;
}

void vita_watchdog_start(void) {
  SceUID th;
  s_tid[WD_T_GAME] = sceKernelGetThreadId();
  g_wd.game_ctx = WD_CTX_BOOT;
  /* cores 1 and 2: never the game thread's core 0, and one busy thread
   * can't keep it off both (D73) */
  th = sceKernelCreateThread("spyro_watchdog", wd_main, PRIORITY, 0x4000, 0,
                             SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL);
  if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("watchdog: no thread (%08x)", (unsigned)th);
    return;
  }
  vita_log("watchdog: on cores 1-2, priority 0x%x; from vblank %d, a hang line after %d s "
           "without a vblank (%d s in the options menu); hang_dump=%d: %s",
           PRIORITY, ARM_VBLANK, LIMIT_S, LIMIT_MENU_S, s_dump,
           s_dump ? "a crash dump if still stuck 20 s later" : "no crash dump");
}

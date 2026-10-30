/* linux32 headless host entry point (docs/porting-plan.md §5).
 *   spyro --disc DIR [--frames N] [--shot V1,V2,...] [--shot-every N]
 *         [--shot-dir DIR] [--vram-shot] [--dither] [--tri-dump V] [--pad FILE]
 *         [--log FILE] [--cd-latency TICKS] [--save DIR] [--wav FILE]
 *         [--serve PORT] [--trace FILE]
 *         [--step DIR]
 *         [--poke V:ADDR=VALUE]...
 *         [--widescreen | --widescreen-logic]
 *         [--interp | --interp-no-anim | --interp-camera]
 *         [--out960] [--present-stats | --present-dirty]
 *   spyro --replay DIR [--only NAME] [--ignore FILE] [--fnmap FILE]
 * --replay: replay the oracle's function samples in DIR through the
 *   translated C and compare (src/game/oracle_replay.c); no disc needed.
 * --poke: at vblank V, write a 32-bit word to PS1 RAM (hex ADDR and VALUE),
 *   for test set-ups such as "no lives left".
 * --trace: one TSV row of game state per vblank (src/game/trace.c).
 * --moby-dump: the level's mobys (class, position) as mobys-V.tsv next to
 *   every shot, and DIR/now.tsv at every --step stop.
 * --step: stop and wait for commands in DIR (src/platform/linux32/step.c).
 * --serve: real-time vblanks and a browser player on PORT (decisions D22).
 * --widescreen: 16:9 picture from a presentation pass; the logic still sees
 *   the 4:3 frustum. --widescreen-logic: one pass, wider frustum in the logic
 *   too (gameplay can differ). --interp: 60 fps, an extra frame between ticks
 *   with camera, Spyro, mobys, their animation frames and the particles
 *   blended; --interp-no-anim: transforms only; --interp-camera: camera only.
 *   Either turns on --out960: pictures at 960x544 (docs/presentation.md).
 * --present-stats: time update, draw and passes; logged at exit.
 *   --present-dirty also lists the arena pages the passes wrote.
 * Time is virtual: N counts vblanks (60 per second, the game ticks every 2). */
#include "host.h"
#include "present.h"
#include "psx_arena.h"
#include "psx_hooks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void spyro_main(void);

static void usage(void) {
  fprintf(stderr,
          "usage: spyro --disc DIR [--frames N] [--shot V1,V2,...] "
          "[--shot-every N]\n"
          "             [--shot-dir DIR] [--vram-shot] [--dither] "
          "[--pad FILE] [--log FILE]\n"
          "             [--cd-latency TICKS] [--save DIR] [--wav FILE] "
          "[--serve PORT] [--trace FILE]\n"
          "             [--step DIR] [--poke V:ADDR=VALUE]... [--moby-dump]\n"
          "             [--widescreen | --widescreen-logic] "
          "[--interp | --interp-no-anim | --interp-camera] [--out960] [--present-stats]\n"
          "       spyro --replay DIR [--only NAME] [--ignore FILE] [--fnmap FILE]\n");
  exit(2);
}

static void log_pages(const char *what, const uint32_t *bits) {
  int i, start = -1;
  for (i = 0; i <= PRESENT_PAGES; i++) {
    int d = i < PRESENT_PAGES && (bits[i / 32] >> (i % 32) & 1);
    if (d && start < 0)
      start = i;
    if (!d && start >= 0) {
      host_log("present: %s %08x-%08x", what, 0x80000000u + start * 4096,
               0x80000000u + i * 4096 - 1);
      start = -1;
    }
  }
}

static void present_report(void) {
  const PresentStats *s = &g_present_stats;
  long long t = s->ticks ? s->ticks : 1;
  if (!g_present.stats)
    return;
  host_log("present: %lld ticks; per tick us: update %lld, logic draw %lld "
           "(+%lld in VSync waits), passes %lld (%lld passes), snapshot %lld",
           s->ticks, s->us_update / t, (s->us_logic - s->us_vsync) / t,
           s->us_vsync / t, s->us_pass / t, s->passes, s->us_snapshot / t);
  host_log("present: interp frames %lld, skipped %lld, 2D narrowed %lld, "
           "SPU writes dropped %lld",
           s->interp_frames, s->skipped_interp, s->squeezed_2d, s->spu_dropped);
  host_log("present: snapshot %lld bytes copied per tick (in and out)",
           s->snapshot_bytes / t);
  host_log("present: animation tracks blended %lld, kept %lld; particles "
           "blended %lld; blending %lld us per tick", s->anim_blended,
           s->anim_kept, s->particles_blended, s->us_blend / t);
  if (g_present.stats > 1 && s->passes) {
    host_log("present: pages still changed after the region restore, per pass: avg %lld, max %lld (4 KB); "
             "outside the snapshot and scratch: %lld",
             s->dirty_pages / s->passes, s->dirty_max, s->missed_pages);
    log_pages("dirty", s->dirty_union);
    log_pages("MISSED", s->missed_union);
  }
  host_log_flush();
}

static int to_int(const char *s) {
  char *end;
  long v = strtol(s, &end, 10);
  if (*s == 0 || *end != 0 || v < 0)
    usage();
  return (int)v;
}

int main(int argc, char **argv) {
  const char *pad = NULL, *replay = NULL, *only = NULL, *ignore = NULL, *fnmap = NULL;
  int i, cd_latency = 0, serve_port = 0;
  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    const char *val = i + 1 < argc ? argv[i + 1] : NULL;
#define TAKE()                                                                 \
  do {                                                                         \
    if (!val)                                                                  \
      usage();                                                                 \
    i++;                                                                       \
  } while (0)
    if (strcmp(a, "--disc") == 0) {
      TAKE();
      g_host_opts.disc_dir = val;
    } else if (strcmp(a, "--replay") == 0) {
      TAKE();
      replay = val;
    } else if (strcmp(a, "--only") == 0) {
      TAKE();
      only = val;
    } else if (strcmp(a, "--ignore") == 0) {
      TAKE();
      ignore = val;
    } else if (strcmp(a, "--moby-dump") == 0) {
      g_host_opts.moby_dump = 1;
    } else if (strcmp(a, "--fnmap") == 0) {
      TAKE();
      fnmap = val;
    } else if (strcmp(a, "--frames") == 0) {
      TAKE();
      g_host_opts.frames = to_int(val);
    } else if (strcmp(a, "--shot") == 0) {
      char buf[4096], *tok;
      TAKE();
      snprintf(buf, sizeof buf, "%s", val);
      for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        if (g_host_opts.nshots == HOST_MAX_SHOTS)
          usage();
        g_host_opts.shots[g_host_opts.nshots++] = to_int(tok);
      }
    } else if (strcmp(a, "--shot-every") == 0) {
      TAKE();
      g_host_opts.shot_every = to_int(val);
    } else if (strcmp(a, "--shot-dir") == 0) {
      TAKE();
      g_host_opts.shot_dir = val;
    } else if (strcmp(a, "--vram-shot") == 0) {
      g_host_opts.vram_shot = 1;
    } else if (strcmp(a, "--serve") == 0) {
      TAKE();
      serve_port = to_int(val);
    } else if (strcmp(a, "--step") == 0) {
      TAKE();
      host_step_set_dir(val);
    } else if (strcmp(a, "--trace") == 0) {
      TAKE();
      if (spyro_trace_open(val) != 0)
        return 1;
    } else if (strcmp(a, "--wav") == 0) {
      TAKE();
      if (host_audio_wav_open(val) != 0)
        return 1;
    } else if (strcmp(a, "--poke") == 0) {
      unsigned v, addr, value;
      TAKE();
      if (g_host_opts.npokes == 16 ||
          sscanf(val, "%u:%x=%x", &v, &addr, &value) != 3 ||
          addr < 0x80000000u || addr > 0x801FFFFCu)
        usage();
      g_host_opts.pokes[g_host_opts.npokes].vblank = (int)v;
      g_host_opts.pokes[g_host_opts.npokes].addr = addr;
      g_host_opts.pokes[g_host_opts.npokes++].value = value;
    } else if (strcmp(a, "--tri-dump") == 0) {
      TAKE();
      g_host_opts.tri_dump = to_int(val);
    } else if (strcmp(a, "--widescreen") == 0) {
      g_present.widescreen = 1;
    } else if (strcmp(a, "--widescreen-logic") == 0) {
      g_present.widescreen = g_present.ws_logic = 1;
    } else if (strcmp(a, "--interp") == 0) {
      g_present.interp = g_present.interp_mobys = g_present.interp_anim = 1;
    } else if (strcmp(a, "--interp-no-anim") == 0) {
      g_present.interp = g_present.interp_mobys = 1;
      g_present.interp_anim = 0;
    } else if (strcmp(a, "--interp-camera") == 0) {
      g_present.interp = 1;
      g_present.interp_mobys = g_present.interp_anim = 0;
    } else if (strcmp(a, "--out960") == 0) {
      g_host_opts.out960 = 1;
    } else if (strcmp(a, "--present-stats") == 0) {
      g_present.stats = 1;
    } else if (strcmp(a, "--present-dirty") == 0) {
      g_present.stats = 2;
    } else if (strcmp(a, "--dither") == 0) {
      g_host_opts.dither = 1;
    } else if (strcmp(a, "--pad") == 0) {
      TAKE();
      pad = val;
    } else if (strcmp(a, "--save") == 0) {
      TAKE();
      psx_mcrd_set_root(val); /* memory cards: DIR/card0, DIR/card1 */
    } else if (strcmp(a, "--cd-latency") == 0) {
      TAKE();
      cd_latency = to_int(val);
    } else if (strcmp(a, "--log") == 0) {
      TAKE();
      host_log_open(val);
    } else {
      usage();
    }
#undef TAKE
  }
  if (replay) {
    psx_arena_init();
    return spyro_replay(replay, only, ignore, fnmap) == 0 ? 0 : 1;
  }
  if (!g_host_opts.disc_dir)
    usage();
  if (port_present_active())
    g_host_opts.out960 = 1;
  atexit(present_report);
  if (pad && host_pad_load(pad) != 0)
    return 1;

  if (serve_port && host_serve_start(serve_port) != 0)
    return 1;
  psx_arena_init();
  if (psx_cd_open(g_host_opts.disc_dir) != 0) {
    host_log("psx_cd_open(%s) failed", g_host_opts.disc_dir);
    host_log_flush();
    return 1;
  }
  /* CDLoadSync (cd.c) busy-waits on m_IsReading without VSync: on the PS1
   * the CD interrupt ends it. With no interrupts here, a read completes
   * inside CdRead unless a latency is asked for (decisions D17). */
  psx_cd_set_read_latency(cd_latency);
  host_log("disc %s, frames %d, cd latency %d", g_host_opts.disc_dir,
           g_host_opts.frames, cd_latency);
  spyro_main(); /* never returns */
  return 0;
}

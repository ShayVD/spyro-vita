/* linux32 headless host entry point (docs/porting-plan.md §5).
 *   spyro --disc DIR [--frames N] [--shot V1,V2,...] [--shot-every N]
 *         [--shot-dir DIR] [--vram-shot] [--dither] [--tri-dump V] [--pad FILE]
 *         [--log FILE] [--cd-latency TICKS] [--save DIR] [--wav FILE]
 *         [--serve PORT] [--trace FILE]
 *         [--step DIR]
 *         [--poke V:ADDR=VALUE]... [--tour LIST] [--tour-stay N]
 *         [--cheat V:NAME]... [--skip-logos]
 *         [--pad-after FILE] [--peek ADDR:N]...
 *         [--widescreen | --widescreen-exact | --widescreen-logic]
 *         [--interp | --interp-no-anim | --interp-camera]
 *         [--interp-budget US] [--out960 | --present-native]
 *         [--present-stats | --present-dirty] [--vcache]
 *         [--analog] [--free-camera] [--cam-invert-x] [--cam-invert-y] [--cam-speed PERCENT]
 *         [--state V:save|load:N]... [--state-hash FILE]
 *         [--draw-distance X] [--lod-stats N] [--disc-marker FILE]
 *   spyro --replay DIR [--only NAME] [--ignore FILE] [--fnmap FILE] [--disc DIR]
 * --disc: the extracted files (with manifest.json), or a folder holding a
 *   disc image (.cue/.bin/.img; decisions D60). --disc-marker: also check
 *   WAD.WAD the first time and remember it in FILE, as the Vita does.
 * --replay: replay the oracle's function samples in DIR through the
 *   translated C and compare (src/game/oracle_replay.c); --disc only for
 *   samples that read the CD.
 * --cheat: at vblank V, what the Vita's options menu does for a cheat (NAME
 *   lives, levels, credits or warp:LEVEL; src/game/cheats_port.c, D50).
 * --skip-logos: no Sony and Universal logos at boot (D50).
 * --poke: at vblank V, write a 32-bit word to PS1 RAM (hex ADDR and VALUE),
 *   for test set-ups such as "no lives left".
 * --peek: log N words of PS1 RAM from hex ADDR with every shot.
 * --tour: after the pad script, visit levels with the level-warp cheat, run
 *   the credits or an ending (src/platform/linux32/tour.c).
 * --trace: one TSV row of game state per vblank (src/game/trace.c).
 * --moby-dump: the level's mobys (class, position) as mobys-V.tsv next to
 *   every shot, and DIR/now.tsv at every --step stop.
 * --step: stop and wait for commands in DIR (src/platform/linux32/step.c).
 * --serve: real-time vblanks and a browser player on PORT (decisions D22).
 * --widescreen: 16:9, drawn once (the wider view reaches the logic: mobys
 *   visible at the edges count as on screen), except in attract demos, which
 *   keep the original 4:3 logic (a hidden 4:3 draw plus a 16:9 pass) so they
 *   replay. --widescreen-exact: that two-draw form always (logic identical
 *   to 4:3); --widescreen-logic: one draw always, demos too (they desync). --interp: 60 fps, an extra frame between ticks
 *   with camera, Spyro, mobys, their animation frames and the particles
 *   blended; --interp-no-anim: transforms only; --interp-camera: camera only.
 *   --interp-budget US: skip the in-between frame while the last tick's game
 *   CPU time plus one would pass US (on the host, qemu CPU time).
 *   Either turns on --out960: pictures at 960x544 (docs/presentation.md).
 *   --present-native: the presentation frames as drawn, 512x240 (to compare
 *   with the Vita's shots).
 * --present-stats: time update, draw and passes; logged at exit.
 *   --present-dirty also lists the arena pages the passes wrote.
 * --analog: the pad is a DualShock in analog mode; pad script lines may set
 *   the sticks (lx=N ly=N rx=N ry=N, src/platform/linux32/pad.c; D46).
 *   --free-camera: the right stick turns and tilts the camera
 *   (--cam-invert-x, --cam-invert-y, --cam-speed PERCENT; D47).
 * --vcache: record the GTE's projections in the vertex cache the Vita's
 *   persp=/subpixel= use (decisions D39) and log its counters at exit. The
 *   pictures don't use it here; it is for checking that the game runs the
 *   same with it on, and for its hit rates.
 * --walk-lazy: hand each DrawOTag's list over to the OT walk from a copy
 *   (src/platform/common/walk.c, decisions D71), walked at the game
 *   thread's next gate instead of at once: the latest the Vita's parse
 *   thread could walk it. Logs the walks and any packet outside the copy at
 *   exit. The run must be identical to one without it.
 * --state: at vblank V, ask for a save state of slot N (or a load), done at
 *   the next main loop top, in the --save DIR (src/game/savestate.c, D51);
 *   several are taken in the order given.
 *   --state-hash: per main loop top, a hash of each save-state chunk.
 * --draw-distance X: longer draw distance, X times the original (1.5, 2;
 *   src/game/lod.c, decisions D49). --lod-stats N: a "lod" line every N
 *   vblanks (primitive buffer use, scale, extra mobys), also with it off.
 *   --lod-guards-only (tests): no limiter, only the hard buffer bounds.
 * Time is virtual: N counts vblanks (60 per second, the game ticks every 2). */
#include "cheats_port.h"
#include "host.h"
#include "port_controls.h"
#include "lod.h"
#include "present.h"
#include "menu_panel.h"
#include "psx_arena.h"
#include "psx_cdimage.h"
#include "psx_hooks.h"
#include "psx_vcache.h"
#include "psx_walk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void spyro_main(void);
/* libcd (PsyQ signatures) and the game's read callback (cd.c) */
typedef void (*CdlCB)(unsigned char, unsigned char *);
CdlCB CdReadCallback(CdlCB func);
void CDReadDone(unsigned char intr, unsigned char *result);

static void usage(void) {
  fprintf(stderr,
          "usage: spyro --disc DIR [--frames N] [--shot V1,V2,...] "
          "[--shot-every N]\n"
          "             [--shot-dir DIR] [--vram-shot] [--dither] "
          "[--pad FILE] [--log FILE]\n"
          "             [--cd-latency TICKS] [--save DIR] [--wav FILE] "
          "[--serve PORT] [--trace FILE]\n"
          "             [--step DIR] [--poke V:ADDR=VALUE]... [--moby-dump]\n"
          "             [--tour LIST] [--tour-stay N] [--pad-after FILE]\n"
          "             [--peek ADDR:N]... [--cheat V:NAME]... [--skip-logos]\n"
          "             [--widescreen | --widescreen-exact | --widescreen-logic] "
          "[--interp | --interp-no-anim | --interp-camera] [--interp-budget US]\n"
          "             [--out960] [--present-stats] [--vcache] [--walk-lazy]\n"
          "             [--analog] [--free-camera] [--cam-invert-x] [--cam-invert-y] "
          "[--cam-speed PERCENT]\n"
          "             [--state V:save|load:N]... [--state-hash FILE]\n"
          "             [--draw-distance X] [--lod-stats N] [--disc-marker FILE]\n"
          "       spyro --replay DIR [--only NAME] [--ignore FILE] [--fnmap FILE] [--disc DIR]\n");
  exit(2);
}

/* The Vita's disc screen (D60) as text, and with --shot-dir as the panel the
 * Vita shows (disc-error.png). */
static void disc_refused(void) {
  const PsxCdError *e = psx_cd_error();
  uint8_t *rgb;
  char path[512];
  int h;
  if (!e->code)
    return;
  fprintf(stderr, "\n%s\n%s\n\n", e->title, e->text);
  if (!g_host_opts.shot_dir ||
      !(rgb = malloc((size_t)MENU_PANEL_W * menu_panel_height(MENU_PANEL_LINES) * 3)))
    return;
  h = menu_panel_message(rgb, e->title, e->text, "PRESS X TO EXIT");
  snprintf(path, sizeof path, "%s/disc-error.png", g_host_opts.shot_dir);
  host_png_write(path, rgb, MENU_PANEL_W, h);
  free(rgb);
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
           s->ticks, s->us_update / t, s->us_logic / t,
           s->us_vsync / t, s->us_pass / t, s->passes, s->us_snapshot / t);
  host_log("present: interp frames %lld, skipped %lld (%lld for the budget), "
           "2D narrowed %lld, SPU writes dropped %lld", s->interp_frames,
           s->skipped_interp, s->budget_skips, s->squeezed_2d, s->spu_dropped);
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

static int s_vcache;

static void vcache_report(void) {
  const VCacheStats *s = &g_vcache_stats;
  unsigned f = s->frames ? s->frames : 1;
  if (!s_vcache)
    return;
  host_log("vcache: %u frames; per frame: %u vertices projected (peak %u entries), "
           "%u invalid, %u ambiguous, %u not recorded; polygons %u, all vertices found %u (%.1f%%); "
           "vertices found %.1f%% (not found: no entry %u, invalid %u, ambiguous %u per frame)",
           s->frames, s->records / f, s->peak, s->invalid / f, s->ambiguous / f, s->full / f,
           s->polys / f, s->persp / f, s->polys ? 100.0 * s->persp / s->polys : 0.0,
           s->verts ? 100.0 * s->hits / s->verts : 0.0, s->miss_none / f, s->miss_invalid / f,
           s->miss_ambiguous / f);
  host_log_flush();
}

static int s_walk;

static void walk_report(void) {
  PsxWalkStats w;
  if (!s_walk)
    return;
  psx_walk_stats_take(&w);
  host_log("walk: %u lists walked from the copy, %u in place; %llu KB copied; "
           "%u packets outside the copy (last at %08x); %u calls deferred",
           w.walks, w.in_place, w.copied / 1024, w.foreign, (unsigned)w.foreign_at, w.deferred);
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
  const char *pad = NULL, *pad_after = NULL, *replay = NULL, *only = NULL, *ignore = NULL, *fnmap = NULL;
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
    } else if (strcmp(a, "--disc-marker") == 0) {
      TAKE();
      psx_cd_set_verify(val, NULL);
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
    } else if (strcmp(a, "--cheat") == 0) {
      static const char *const names[] = {"", "lives", "levels", "credits", "warp"};
      char name[16];
      int v, c, level = 0;
      TAKE();
      if (g_host_opts.ncheats == 8 || sscanf(val, "%d:%15[a-z]:%d", &v, name, &level) < 2)
        usage();
      for (c = 1; c <= PORT_CHEAT_WARP && strcmp(name, names[c]) != 0; c++)
        ;
      if (c > PORT_CHEAT_WARP)
        usage();
      g_host_opts.cheats[g_host_opts.ncheats].vblank = v;
      g_host_opts.cheats[g_host_opts.ncheats].cheat = c;
      g_host_opts.cheats[g_host_opts.ncheats++].level = level;
    } else if (strcmp(a, "--skip-logos") == 0) {
      g_port_skip_logos = 1;
    } else if (strcmp(a, "--peek") == 0) {
      unsigned addr;
      int n;
      TAKE();
      if (g_host_opts.npeeks == 8 || sscanf(val, "%x:%d", &addr, &n) != 2 || n < 1 ||
          n > 64 || addr < 0x80000000u || addr + 4u * n > 0x80200000u)
        usage();
      g_host_opts.peeks[g_host_opts.npeeks].addr = addr;
      g_host_opts.peeks[g_host_opts.npeeks++].n = n;
    } else if (strcmp(a, "--tour") == 0) {
      TAKE();
      if (host_tour_set(val) != 0)
        usage();
    } else if (strcmp(a, "--tour-stay") == 0) {
      TAKE();
      host_tour_set_stay(to_int(val));
    } else if (strcmp(a, "--tri-dump") == 0) {
      TAKE();
      g_host_opts.tri_dump = to_int(val);
    } else if (strcmp(a, "--widescreen") == 0) {
      g_present.widescreen = 1;
    } else if (strcmp(a, "--widescreen-logic") == 0) {
      g_present.widescreen = g_present.ws_logic = 1;
    } else if (strcmp(a, "--widescreen-exact") == 0) {
      g_present.widescreen = g_present.ws_exact = 1;
    } else if (strcmp(a, "--interp") == 0) {
      g_present.interp = g_present.interp_mobys = g_present.interp_anim = 1;
    } else if (strcmp(a, "--interp-no-anim") == 0) {
      g_present.interp = g_present.interp_mobys = 1;
      g_present.interp_anim = 0;
    } else if (strcmp(a, "--interp-camera") == 0) {
      g_present.interp = 1;
      g_present.interp_mobys = g_present.interp_anim = 0;
    } else if (strcmp(a, "--interp-budget") == 0) {
      TAKE();
      g_present.interp_budget_us = to_int(val);
    } else if (strcmp(a, "--out960") == 0) {
      g_host_opts.out960 = 1;
    } else if (strcmp(a, "--present-native") == 0) {
      g_host_opts.out960 = 2;
    } else if (strcmp(a, "--present-stats") == 0) {
      g_present.stats = 1;
    } else if (strcmp(a, "--present-dirty") == 0) {
      g_present.stats = 2;
    } else if (strcmp(a, "--draw-distance") == 0) {
      TAKE();
      g_lod.scale = (int)(atof(val) * LOD_ONE + 0.5);
    } else if (strcmp(a, "--lod-guards-only") == 0) {
      g_lod.guards_only = 1; /* tests: only the hard bounds (lod.c) */
    } else if (strcmp(a, "--lod-stats") == 0) {
      TAKE();
      g_lod.stats = 1;
      g_host_opts.lod_stats = to_int(val);
    } else if (strcmp(a, "--vcache") == 0) {
      psx_vcache_enable(2);
      s_vcache = 1;
    } else if (strcmp(a, "--walk-lazy") == 0) {
      s_walk = 1;
    } else if (strcmp(a, "--dither") == 0) {
      g_host_opts.dither = 1;
    } else if (strcmp(a, "--analog") == 0) {
      g_host_pad_analog = 1;
    } else if (strcmp(a, "--free-camera") == 0) {
      g_port_controls.free_camera = 1;
    } else if (strcmp(a, "--cam-invert-x") == 0) {
      g_port_controls.invert_x = 1;
    } else if (strcmp(a, "--cam-invert-y") == 0) {
      g_port_controls.invert_y = 1;
    } else if (strcmp(a, "--cam-speed") == 0) {
      TAKE();
      g_port_controls.speed = atoi(val);
    } else if (strcmp(a, "--pad") == 0) {
      TAKE();
      pad = val;
    } else if (strcmp(a, "--pad-after") == 0) {
      TAKE();
      pad_after = val;
    } else if (strcmp(a, "--save") == 0) {
      TAKE();
      psx_mcrd_set_root(val); /* memory cards: DIR/card0, DIR/card1 */
    } else if (strcmp(a, "--cd-latency") == 0) {
      TAKE();
      cd_latency = to_int(val);
    } else if (strcmp(a, "--state") == 0) {
      TAKE();
      if (host_state_option(val) != 0)
        usage();
    } else if (strcmp(a, "--state-hash") == 0) {
      TAKE();
      if (host_state_hash_open(val) != 0)
        return 1;
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
    /* With --disc, CD reads made inside a sampled call (the credits overlay's
     * CDLoadSync) read the disc; without, they never complete. The game's
     * read callback (cd.c, set at start-up) is libcd state on the host, not
     * RAM, so the replay sets it as the game does. */
    if (g_host_opts.disc_dir) {
      if (psx_cd_open(g_host_opts.disc_dir) != 0) {
        disc_refused();
        return 1;
      }
      psx_cd_set_read_latency(0); /* no vblanks here: reads end inside CdRead */
      CdReadCallback(CDReadDone);
    }
    return spyro_replay(replay, only, ignore, fnmap) == 0 ? 0 : 1;
  }
  if (!g_host_opts.disc_dir)
    usage();
  if (port_present_active() && !g_host_opts.out960)
    g_host_opts.out960 = 1;
  atexit(present_report);
  atexit(vcache_report);
  atexit(walk_report);
  if (pad && host_pad_load(pad) != 0)
    return 1;
  if (pad_after && host_pad_load_after(pad_after) != 0)
    return 1;

  if (serve_port && host_serve_start(serve_port) != 0)
    return 1;
  psx_arena_init();
  if (s_walk && psx_walk_init(PSX_WALK_LAZY, port_present_prim_ranges, NULL) != 0) {
    host_log("walk: no 16 MB-aligned memory for the copy");
    return 1;
  }
  if (psx_cd_open(g_host_opts.disc_dir) != 0) {
    host_log("psx_cd_open(%s) failed", g_host_opts.disc_dir);
    host_log_flush();
    disc_refused();
    return 1;
  }
  /* CDLoadSync (cd.c) busy-waits on m_IsReading without VSync: on the PS1
   * the CD interrupt ends it. With no interrupts here, a read completes
   * inside CdRead unless a latency is asked for (decisions D17). */
  psx_cd_set_read_latency(cd_latency);
  host_log("disc %s, frames %d, cd latency %d", g_host_opts.disc_dir,
           g_host_opts.frames, cd_latency);
  port_cheats_set_log(host_log);
  spyro_main(); /* never returns */
  return 0;
}

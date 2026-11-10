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
 *         [--present-stats] [--vcache] [--tick60] [--tick60-stats]
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
 * --tick60: 60 Hz logic in play, the 60 fps option's third value (D130,
 *   docs/tick60.md): an update in play is one vblank, run by the update
 *   path's second compile (cmake/tick60.cmake); every other gamestate and
 *   the attract demos stay at 30 Hz (add --interp for their in-between
 *   frame). --tick60-stats: the updates' CPU time by rate, logged at exit.
 * --present-stats: time update, draw and the presenter's frames; logged at
 *   exit. (--present-dirty, --present-audit and --present-reads, D30's
 *   pass's and stage 7a's audits of it, went with the pass: D129.)
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
 * --fast-render 3 (the default since D127): the hand-written renderers as
 *   readable C (stage 7d2, D124: no GTE data registers, scratchpad, spill
 *   area or hand stack); 0 (the arena build): the renderers' mips2c units
 *   and PsyCross's GTE_operator instead. 1 and 2, the register-file builds
 *   (D72, D80), went in D127. --fast-render-check (the arena build): each
 *   call of a hand-written renderer or unit function also runs its mips2c
 *   unit from the same state first, and the two must leave the same RAM
 *   but the scratchpad, spill area and stack, and the same control
 *   registers but FLAG (src/game/hand_check.c; "hand check" lines).
 *   --particle-sweep (with the check, D95): before each particle draw every
 *   live particle's type is set to 4..7 in turn, which no run reaches, both
 *   ways are compared, and the types are put back.
 *   --native-draw: the converted renderers' primitives go to the OT walk
 *   as native draw lists (src/game/native_draw.h, decisions D82);
 *   --native-draw-check runs each of their calls both ways and compares the
 *   triangles PsyCross's walk makes of them ("native draw check" lines).
 *   --native-draw-parse: native_draw=2 (D92): the renderers' PS1
 *   primitives, parsed by the walk with the records' converter; with
 *   --native-draw-parse-check (also with --native-draw, or with the
 *   check) every packet that converter takes is parsed by PsyCross too
 *   and compared ("native draw parse check" lines). --nd-fast 0: with 1
 *   and 2, only the records go through the converter.
 *   --nd-ab: native_draw 0, 1 and 2 in turn by blocks of lists (nd_ab, the
 *   Vita's timing run, D91, D92).
 *   These checks need the reference build (the arena build, linux32's
 *   default: SPYRO_PSYCROSS_REF, D117), whose PsyCross walk they compare with.
 *   The presenter (stage 7e-7g, D125, D126, D128, D129; src/game/pres/):
 *   the game's draw code built again over a snapshot draws every
 *   in-between frame (--interp) and the attract demos' 16:9 frame
 *   (--widescreen); D30's pass over the game's own memory did until D129
 *   (--interp-mode 2 chose the presenter before; it is gone). The host runs
 *   it inline (stage 7f, D126: on the Vita it has a thread of its own).
 *   --pres-check (D129): each tick the presenter also draws from the
 *   snapshot unblended, and its list is compared with the logic draw's
 *   record for record ("pres check" lines); with or without --interp and
 *   --widescreen, every file is the same as without it. --pres-audit:
 *   which words of the snapshot point into the moby array or the particle
 *   space ("pres audit"). --faces-copy: the logic draw's moby face
 *   replacements in a copy for the whole run, as while the Vita's
 *   presenter draws beside it (moby_hand.c); every file must be the same.
 *   --pres-check-thread: --pres-check with the presenter's list recorded
 *   on a thread of its own beside the logic draw (the Vita's 7f), compared
 *   at the join; --faces-race (diagnostic) leaves the logic draw's face
 *   replacements in place meanwhile, as before 7f, to show the race the
 *   copy removes. --pres-check-late (D128): --pres-check with the
 *   presenter's list recorded after the logic draw (at the join), so a
 *   frame that depends on what the logic draw writes differs.
 *   --pres-write-check (D128): each presenter frame must leave the game's
 *   memory as it found it ("pres write check" lines; pres_watch.c).
 *   --render-ctx-check (stage 7d, D123): each renderer call also runs its
 *   presenter build (NAME_hand_pres.c) from the same inputs in its own
 *   context, with the game's memory read-only, and compares the two
 *   ("render ctx:" lines; src/game/render_ctx_check.c).
 *   --walk-check: every packet our OT walk (gpu_walk.c) parses goes through
 *   PsyCross's walk too from the same state; the vertices, splits and draw
 *   environment must be the same ("walk check" lines; D117). WALK_MUTATE=1
 *   or 2 in the environment: a mutant the check must catch.
 *   --walk-check-synth N: N random packets of every GP0 kind (those no run
 *   reaches too) through both walks, then exit; nothing else runs.
 *   --nd-direct (with --native-draw or --native-draw-parse): the walk
 *   leaves its triangles in the GPU renderer's lean form (nd_direct=1,
 *   D99); the software rasterizer doesn't read what changes, so every
 *   file is the same. --nd-direct-check: every packet the walk parses
 *   itself is parsed both ways and compared ("native draw direct check"
 *   lines; what stays is the direct way's with --nd-direct). Not with
 *   --vcache or the other two checks, whose walks compare GrVertex.
 *   --gpu-clear: ClearImage's rect filled row by row (psx_fill.h, the
 *   Vita's gpu_clear=1, D111): the same pixels, so every file is the same.
 *   --prof-draw: log
 *   each renderer's CPU time over the run at exit (prof_draw.c; relative
 *   numbers only, under qemu).
 *   --mixer 3: the native SPU mix (spu_native.c, D84; default 1: libspu.c's
 *   psx_spu_mix, the same samples), its PCM cache counts logged at exit.
 * Time is virtual: N counts vblanks (60 per second, the game ticks every 2). */
#include "cheats_port.h"
#include "host.h"
#include "port_controls.h"
#include "lod.h"
#include "tick60.h"
#include "present.h"
#include "menu_panel.h"
#include "psx_arena.h"
#include "psx_cdimage.h"
#include "psx_cdnative.h"
#include "psx_hooks.h"
#include "psx_vcache.h"
#include "psx_walk.h"
#include "psx_spu.h"

extern int g_fast_render; /* src/game/mips2c_rt.h, D72 */
extern int g_host_prof_draw; /* prof_draw.c */
void host_prof_draw_report(void);
extern int g_hand_check; /* src/game/hand_check.c, D80, D127 */
void hand_check_set_log(void (*log)(const char *fmt, ...));
void hand_check_report(void);
extern int g_native_draw, g_nd_check; /* src/game/native_draw.c, D82 */
extern int g_nd_ab;                    /* D91 */
extern int g_nd_fast, g_nd_pcheck;     /* D92 */
extern int g_nd_direct, g_nd_direct_ok, g_nd_dcheck; /* D99 */
extern int g_gpu_clear; /* gpu_soft.c, D111 */
extern const int g_walk_ref_built;  /* src/game/walk_check.c, D117 */
int walk_check_init(void (*log)(const char *fmt, ...));
void walk_check_report(void);
unsigned long long walk_check_synth(unsigned n, unsigned seed);
static int s_walk_check, s_walk_synth;
void nd_dcheck_report(void);
void nd_init(void (*log)(const char *fmt, ...));
void nd_report(int vblank);
void nd_check_report(void);
void nd_pcheck_report(void);
/* src/game/pres/ (stage 7e, D125) */
extern int g_pres_check, g_pres_audit;
extern long long g_pres_ovl_unknown; /* pres_draw.c (stage 7g, D128) */
extern int g_faces_copy; /* src/game/render_ctx.c (stage 7f, D126) */
extern int g_pres_check_thread, g_faces_race; /* src/game/pres/pres_check.c (D126) */
int host_pres_thread_init(void);              /* pres_thread.c (D126) */
void host_pres_late_init(void);               /* pres_thread.c (D128) */
void host_pres_watch_init(void);              /* pres_watch.c (D128) */
int host_pres_watch_report(void);
static int s_pres_watch;
extern unsigned g_faces_swaps, g_pres_face_overlaps; /* render_ctx.c (D126) */
void pres_draw_want(void);
void pres_check_set_log(void (*log)(const char *fmt, ...));
int pres_check_report(void (*log)(const char *fmt, ...));
void pres_snap_audit_report(void (*log)(const char *fmt, ...));
const char *pres_snap_misplaced(void); /* pres_snap.c (D128) */
static void pres_atexit(void) {
  if (pres_snap_misplaced())
    host_log("pres: %s isn't in the data region the snapshot mirrors: no presenter",
             pres_snap_misplaced());
  if (port_present_active()) { /* stage 7f (D126): the host runs it inline */
    PresentPresStats st;
    port_present_pres_stats(&st);
    host_log("pres: %lld frames inline; %lld display environments not the presenter's "
             "(gamestates 0x%X), %lld frames uncaptured (gamestates 0x%X); %lld overlay pointers "
             "it couldn't follow", st.inline_frames, st.disp_misses, st.disp_miss_gs,
             st.uncaptured, st.uncaptured_gs, g_pres_ovl_unknown);
  }
  if (g_pres_check_thread)
    host_log("pres check thread: %u records with face replacements in the game's draws (the "
             "faces %s), %u reads by the presenter of a model whose faces the game's draw had "
             "replaced in place then", g_faces_swaps, g_faces_race ? "in place" : "in a copy",
             g_pres_face_overlaps);
  if (g_pres_check)
    pres_check_report(host_log);
  if (s_pres_watch)
    host_pres_watch_report();
  if (g_pres_audit)
    pres_snap_audit_report(host_log);
}

/* src/game/render_ctx_check.c (stage 7d, D123) */
extern int g_render_ctx_check;
void render_ctx_want_area(void);
void render_ctx_check_set_log(void (*log)(const char *fmt, ...));
int render_ctx_check_report(void);
static void render_ctx_atexit(void) {
  if (g_render_ctx_check)
    render_ctx_check_report();
}
static void nd_atexit(void) {
  nd_report((int)host_vblank_count());
  nd_check_report();
  nd_pcheck_report();
  nd_dcheck_report();
  walk_check_report();
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern void spyro_main(void);
void port_arena_symbols(void (*log)(const char *fmt, ...)); /* src/game/port.c */
/* libcd (PsyQ signatures) and the game's read callback (cd.c) */
typedef void (*CdlCB)(unsigned char, unsigned char *);
CdlCB CdReadCallback(CdlCB func);
void CDReadDone(unsigned char intr, unsigned char *result);

static void mixer_report(void) {
  const PsxSpuNativeStats *st = &g_psx_spu_native_stats;
  if (g_psx_spu_native)
    host_log("mixer: native (D84): PCM blocks %llu from the cache, %llu from its second way, "
             "%llu decoded in the mix; %llu forgotten at SpuWrite (D88)",
             st->hits[0], st->hits[1], st->misses, st->written);
}

static int s_cd_native; /* --cd-native (D93) */

static unsigned long long now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000u + (unsigned long long)ts.tv_nsec / 1000u;
}

/* --cd-stats: the Vita's "cd:" line (vblank.c) once, at exit; the times are
 * the container's, so they stay out of the deterministic log otherwise. */
static void cd_stats_report(void) {
  PsxCdIo io;
  psx_cd_io(&io);
  host_log("cd: %u reads, %u KB, %u ms in the file system; %u CdReads, game thread waited %u ms",
           io.reads, io.kbytes, (unsigned)(io.us / 1000), io.loads, (unsigned)(io.wait_us / 1000));
  if (g_psx_cd_native) {
    PsxCdnStats st;
    psx_cdn_stats(&st);
    host_log("cd: stream %u chunks (%u reads, %u KB, %u ms); %u sectors answered from them, %u "
             "read on the game thread; %u asks found the slot busy, %u pinned",
             st.chunks, st.chunk_reads, st.chunk_kb, (unsigned)(st.chunk_us / 1000), st.hits,
             st.misses, st.busy, st.pinned);
    host_log("cd: the worker %u ms on the CdReads; %u of them after a stream chunk",
             (unsigned)(st.job_us / 1000), st.behind_chunk);
  }
  host_log_flush();
}

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
          "             [--out960] [--present-stats] [--vcache] [--walk-lazy] [--tick60]\n"
          "             [--analog] [--free-camera] [--cam-invert-x] [--cam-invert-y] "
          "[--cam-speed PERCENT]\n"
          "             [--state V:save|load:N]... [--state-hash FILE]\n"
          "             [--draw-distance X] [--lod-stats N] [--disc-marker FILE] [--fast-render 0|3] [--fast-render-check [--particle-sweep]] [--native-draw[-check]] [--native-draw-parse[-check]] [--nd-fast 0|1] [--nd-ab] [--nd-direct[-check]] [--walk-check] [--walk-check-synth N] [--render-ctx-check] [--pres-check] [--pres-check-thread [--faces-race]] [--pres-check-late] [--pres-write-check] [--faces-copy] [--pres-audit] [--gpu-clear] [--prof-draw]\n"
          "             [--mixer 1|3] [--cd-native] [--cd-stats]\n"
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

static void present_report(void) {
  const PresentStats *s = &g_present_stats;
  long long t = s->ticks ? s->ticks : 1;
  if (!g_present.stats)
    return;
  host_log("present: %lld ticks; per tick us: update %lld, logic draw %lld "
           "(+%lld in VSync waits), the presenter %lld (%lld frames)",
           s->ticks, s->us_update / t, s->us_logic / t,
           s->us_vsync / t, s->us_pass / t, s->passes);
  host_log("present: interp frames %lld, skipped %lld (%lld for the budget), "
           "2D narrowed %lld", s->interp_frames,
           s->skipped_interp, s->budget_skips, s->squeezed_2d);
  host_log("present: frames by due vblank (7c, D112): %lld shown late, %lld never shown, "
           "%lld dropped from a full queue", s->due_late, s->due_superseded, s->queue_drops);
  host_log("present: animation tracks blended %lld, kept %lld; particles "
           "blended %lld; blending %lld us per tick", s->anim_blended,
           s->anim_kept, s->particles_blended, s->us_blend / t);
  host_log_flush();
}

static void tick60_atexit(void) {
  tick60_report(host_log);
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
    } else if (strcmp(a, "--moby-trace") == 0) {
      TAKE();
      if (spyro_moby_trace_open(val) != 0) /* 60 Hz logic's statistics (D130) */
        return 1;
    } else if (strcmp(a, "--wav") == 0) {
      TAKE();
      if (host_audio_wav_open(val) != 0)
        return 1;
    } else if (strcmp(a, "--poke") == 0) {
      unsigned v, addr, value;
      TAKE();
      if (g_host_opts.npokes == 16 ||
          sscanf(val, "%u:%x=%x", &v, &addr, &value) != 3 || !PSX_MEM_OK(addr, 4u))
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
          n > 64 || !PSX_MEM_OK(addr, 4u * (unsigned)n))
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
    } else if (strcmp(a, "--faces-copy") == 0) {
      g_faces_copy = 1; /* D126: the logic draw's faces in a copy all run (as beside the worker) */
    } else if (strcmp(a, "--pres-check") == 0 || strcmp(a, "--pres-check-thread") == 0) {
      g_pres_check = 1;
      pres_draw_want();
      pres_check_set_log(host_log);
      if (strcmp(a, "--pres-check-thread") == 0) { /* D126: the presenter beside the draws */
        if (host_pres_thread_init() != 0) {
          fprintf(stderr, "--pres-check-thread: no thread\n");
          return 2;
        }
        g_pres_check_thread = 1;
      }
    } else if (strcmp(a, "--pres-check-late") == 0) {
      /* stage 7g (D128): --pres-check with the presenter's list recorded
       * after the logic draw (pres_thread.c) */
      g_pres_check = 1;
      pres_draw_want();
      pres_check_set_log(host_log);
      host_pres_late_init();
      g_pres_check_thread = 1;
    } else if (strcmp(a, "--pres-write-check") == 0) {
      host_pres_watch_init(); /* stage 7g (D128): pres_watch.c */
      s_pres_watch = 1;
    } else if (strcmp(a, "--faces-race") == 0) {
      g_faces_race = 1; /* D126, diagnostic: the faces in place beside the presenter's thread */
    } else if (strcmp(a, "--pres-audit") == 0) {
      g_pres_audit = 1;
      pres_draw_want();
    } else if (strcmp(a, "--interp-budget") == 0) {
      TAKE();
      g_present.interp_budget_us = to_int(val);
    } else if (strcmp(a, "--out960") == 0) {
      g_host_opts.out960 = 1;
    } else if (strcmp(a, "--present-native") == 0) {
      g_host_opts.out960 = 2;
    } else if (strcmp(a, "--present-stats") == 0) {
      g_present.stats = 1;
    } else if (strcmp(a, "--tick60") == 0) {
      g_tick60_mode = 1; /* 60 Hz logic in play (D130, src/game/tick60.c) */
    } else if (strcmp(a, "--tick60-stats") == 0) {
      g_t60_stats_on = 1;
    } else if (strcmp(a, "--draw-distance") == 0) {
      TAKE();
      g_lod.scale = (int)(atof(val) * LOD_ONE + 0.5);
    } else if (strcmp(a, "--lod-guards-only") == 0) {
      g_lod.guards_only = 1; /* tests: only the hard bounds (lod.c) */
    } else if (strcmp(a, "--lod-stats") == 0) {
      TAKE();
      g_lod.stats = 1;
      g_host_opts.lod_stats = to_int(val);
    } else if (strcmp(a, "--prof-draw") == 0) {
      g_host_prof_draw = 1;
    } else if (strcmp(a, "--fast-render") == 0) {
      TAKE();
      g_fast_render = to_int(val);
      if (g_fast_render != 0 && g_fast_render != 3) {
        fprintf(stderr, "spyro: --fast-render %d: 3 (the readable renderers) or 0 (the mips2c units); "
                        "1 and 2 went in D127\n", g_fast_render);
        return 1;
      }
#if SPYRO_NATIVE_MEM
      if (g_fast_render == 0) {
        fprintf(stderr, "spyro: --fast-render 0 runs the mips2c units, which a native build doesn't link (D100)\n");
        return 1;
      }
#endif
    } else if (strcmp(a, "--particle-sweep") == 0) {
      extern int g_particle_sweep; /* hand_check.c, D95 */
      g_particle_sweep = 1;
      g_hand_check = 1;
      hand_check_set_log(host_log);
    } else if (strcmp(a, "--fast-render-check") == 0) {
      g_hand_check = 1;
      hand_check_set_log(host_log);
    } else if (strcmp(a, "--render-ctx-check") == 0) {
      g_render_ctx_check = 1;
      render_ctx_want_area();
      render_ctx_check_set_log(host_log);
    } else if (strcmp(a, "--native-draw") == 0) {
      g_native_draw = 1;
    } else if (strcmp(a, "--native-draw-check") == 0) {
      g_native_draw = 1;
      g_nd_check = 1;
    } else if (strcmp(a, "--native-draw-parse") == 0) {
      g_native_draw = 2;
    } else if (strcmp(a, "--native-draw-parse-check") == 0) {
      g_nd_pcheck = 1;
      if (!g_native_draw)
        g_native_draw = 2;
    } else if (strcmp(a, "--nd-fast") == 0) {
      TAKE();
      g_nd_fast = to_int(val);
    } else if (strcmp(a, "--nd-ab") == 0) {
      g_nd_ab = 1;
    } else if (strcmp(a, "--nd-direct") == 0) {
      g_nd_direct = 1;
    } else if (strcmp(a, "--nd-direct-check") == 0) {
      g_nd_dcheck = 1;
    } else if (strcmp(a, "--walk-check") == 0) {
      s_walk_check = 1;
    } else if (strcmp(a, "--walk-check-synth") == 0) {
      TAKE();
      s_walk_synth = to_int(val);
      s_walk_check = 1;
    } else if (strcmp(a, "--gpu-clear") == 0) {
      g_gpu_clear = 1;
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
    } else if (strcmp(a, "--cd-native") == 0) {
      s_cd_native = 1;
    } else if (strcmp(a, "--cd-stats") == 0) {
      psx_cd_set_clock(now_us);
      atexit(cd_stats_report);
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
    } else if (strcmp(a, "--mixer") == 0) {
      TAKE();
      if (to_int(val) != 1 && to_int(val) != 3)
        usage();
      g_psx_spu_native = to_int(val) == 3;
    } else {
      usage();
    }
#undef TAKE
  }
  if (replay) {
#if SPYRO_ARENA_MOVED || SPYRO_NATIVE_MEM
    fprintf(stderr, "spyro: --replay needs the arena build (the arena at 0x80000000, no "
                    "SPYRO_NATIVE_MEM; D97, D100)\n");
    return 1;
#else
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
      if (s_cd_native)
        psx_cdn_start();
      CdReadCallback(CDReadDone);
    }
    return spyro_replay(replay, only, ignore, fnmap) == 0 ? 0 : 1;
#endif
  }
  if (!g_host_opts.disc_dir)
    usage();
  if (port_present_active() && !g_host_opts.out960)
    g_host_opts.out960 = 1;
  atexit(present_report);
  atexit(tick60_atexit);
  atexit(vcache_report);
  atexit(host_prof_draw_report);
  atexit(hand_check_report);
  atexit(walk_report);
  atexit(mixer_report);
  atexit(nd_atexit);
  atexit(render_ctx_atexit);
  atexit(pres_atexit);
  if (pad && host_pad_load(pad) != 0)
    return 1;
  if (pad_after && host_pad_load_after(pad_after) != 0)
    return 1;

  if (serve_port && host_serve_start(serve_port) != 0)
    return 1;
  /* the presenter's area after the game's memory, whatever the switches
   * (stage 7g, D129: it draws every frame the logic draw doesn't) */
  pres_draw_want();
  psx_arena_init();
#if SPYRO_ARENA_MOVED
  port_arena_symbols(host_log); /* the Vita's data labels (D25, D97) */
#endif
  /* the software rasterizer takes direct vertices (D99), but the vertex
   * cache's references and the other checks' GrVertex comparisons don't */
  g_nd_direct_ok = !s_vcache && !g_nd_check && !g_nd_pcheck;
  if (g_nd_dcheck && !g_nd_direct_ok) {
    host_log("--nd-direct-check: not with --vcache, --native-draw-check or "
             "--native-draw-parse-check");
    return 1;
  }
  if ((g_nd_check || g_nd_pcheck) && !g_walk_ref_built) {
    host_log("--native-draw-check and --native-draw-parse-check compare with PsyCross's walk: "
             "the reference build only (SPYRO_PSYCROSS_REF)");
    return 1;
  }
  if (s_walk_check && g_nd_pcheck) {
    host_log("--walk-check: not with --native-draw-parse-check (both drive PsyCross's walk)");
    return 1;
  }
  if (s_walk_check && walk_check_init(host_log) != 0)
    return 1;
  if (s_walk_synth) /* random packets through both walks, then exit (the report at exit) */
    return walk_check_synth((unsigned)s_walk_synth, 1) ? 1 : 0;
  nd_init(host_log);
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
  if (s_cd_native && psx_cdn_start() == 0)
    host_log("cd: native file reads on a worker thread (--cd-native, D93)");
  port_cheats_set_log(host_log);
  spyro_main(); /* never returns */
  return 0;
}

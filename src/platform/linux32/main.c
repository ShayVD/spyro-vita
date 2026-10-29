/* linux32 headless host entry point (docs/porting-plan.md §5).
 *   spyro --disc DIR [--frames N] [--shot V1,V2,...] [--shot-every N]
 *         [--shot-dir DIR] [--vram-shot] [--dither] [--tri-dump V] [--pad FILE]
 *         [--log FILE] [--cd-latency TICKS] [--save DIR] [--wav FILE]
 *         [--serve PORT] [--trace FILE]
 *         [--step DIR]
 * --trace: one TSV row of game state per vblank (src/game/trace.c).
 * --step: stop and wait for commands in DIR (src/platform/linux32/step.c).
 * --serve: real-time vblanks and a browser player on PORT (decisions D22).
 * Time is virtual: N counts vblanks (60 per second, the game ticks every 2). */
#include "host.h"
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
          "             [--step DIR]\n");
  exit(2);
}

static int to_int(const char *s) {
  char *end;
  long v = strtol(s, &end, 10);
  if (*s == 0 || *end != 0 || v < 0)
    usage();
  return (int)v;
}

int main(int argc, char **argv) {
  const char *pad = NULL;
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
    } else if (strcmp(a, "--tri-dump") == 0) {
      TAKE();
      g_host_opts.tri_dump = to_int(val);
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
  if (!g_host_opts.disc_dir)
    usage();
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

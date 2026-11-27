/* PC entry point (docs/pc-build.md, decisions D240).
 *
 * The low-image model (D173, D174), as linux64 and the PS4: everything the
 * game can point at lies below 2 GB. The executable is non-PIE at 0x400000
 * (Linux) or has a fixed base of 0x400000 (Windows); the arena is a .bss
 * array (arena.c) and the game runs on a thread whose stack is a .bss array,
 * since the hand C takes the addresses of locals. main() checks all three
 * and refuses to run otherwise. On Linux nothing of the host's lies below
 * 4 GB (Encore's lp64_low_image_dyn.c reserves it before any library runs; low.c), so a host pointer
 * cut to 32 bits crashes instead of aliasing the game's memory; on Windows
 * the whole process is below 2 GB (not large-address-aware; low.c).
 *
 * The main thread owns the platform (Encore's platform interface over its
 * SDL3 PC backend, D247: window, GL, events) and shows the frames; the game
 * thread runs the game (pc.h). */
#include "pc.h"
#include "host.h"
#include "../../game/present.h"
#include "cheats_port.h"
#include "psx_arena.h"
#include "psx_cdimage.h"
#include "psx_clock.h"
#include "psx_hooks.h"
#include "psx_region.h"
#include "psx_spu.h"
#include "region_data.h"

#include "gaps.h"

#include "encore/platform.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define pc_mkdir(p) _mkdir(p)
#else
#include <pthread.h>
#define pc_mkdir(p) mkdir(p, 0777)
#endif

#define LOW_LIMIT 0x80000000ull /* the game's addresses: below 2 GB */
#define GAME_STACK (8u << 20)   /* the main thread's usual 8 MB (linux64's) */

extern const void *psx_low_arena_end(void); /* arena.c */
extern void spyro_main(void);
void pres_draw_want(void);                       /* src/game/pres/pres_draw.c */
void nd_init(void (*log)(const char *fmt, ...)); /* src/game/native_draw.c */
extern int g_nd_direct_ok;
int pc_low_reserved_pages(void);    /* low.c */
const char *pc_low_report(void);    /* the same: what lies where */

#if defined(__APPLE__)
#define STACK_ALIGN 16384 /* pthread_attr_setstack wants macOS's 16 KB pages */
#elif defined(__aarch64__)
#define STACK_ALIGN 65536 /* Linux on ARM: a kernel's page may be 4, 16 or 64 KB (D250 O5) */
#else
#define STACK_ALIGN 4096
#endif
static unsigned char s_game_stack[GAME_STACK] __attribute__((aligned(STACK_ALIGN)));

PcOpts g_pc_opts = {.stats = 1, .analog = 1, .vsync = 1, .filter = 1, .widescreen = 1,
                    .interp = 1, .win_w = 1280, .win_h = 720,
                    .gpu = 1, .persp = 1, .subpixel = 1}; /* D243 */
char g_pc_user_dir[1024];
static char s_disc_dir[1024];
volatile int g_pc_quit;

unsigned long long pc_now_us(void) { return enc_time_us(); }

void pc_message(const char *title, const char *text) {
  host_log("%s: %s", title, text);
  host_log_flush();
  if (!g_pc_opts.frames) /* frames= is an unattended run (tests, CI): no dialog to wait on */
    pc_gap_message_box(title, text);
}

void pc_exit(int code) {
  host_log_flush();
  fflush(stdout);
  pc_audio_close();
  exit(code);
}

#if SPYRO_P32_OFFSET
/* macOS and Linux on ARM (D250 O4, O5): the offset model, within 4 GB above the base */
#include "p32_image.h"
static int low(const void *p, size_t len) { return p32_within(p, len); }
#else
static int low(const void *p, size_t len) { return (uintptr_t)p + len <= LOW_LIMIT; }
#endif

/* config.txt in the user folder, then the command line (which wins):
 * key=value lines, '#' comments; --key value or --key on the command line */
static int set_opt(const char *key, const char *v) {
  int n = atoi(v);
  if (!strcmp(key, "frames"))
    g_pc_opts.frames = n;
  else if (!strcmp(key, "stats"))
    g_pc_opts.stats = n;
  else if (!strcmp(key, "analog"))
    g_pc_opts.analog = n;
  else if (!strcmp(key, "fullscreen"))
    g_pc_opts.fullscreen = n;
  else if (!strcmp(key, "vsync"))
    g_pc_opts.vsync = n;
  else if (!strcmp(key, "filter"))
    g_pc_opts.filter = n;
  else if (!strcmp(key, "widescreen"))
    g_pc_opts.widescreen = n;
  else if (!strcmp(key, "interp"))
    g_pc_opts.interp = n;
  else if (!strcmp(key, "virtual"))
    g_pc_opts.virt = n;
  else if (!strcmp(key, "gpu"))
    g_pc_opts.gpu = n;
  else if (!strcmp(key, "persp"))
    g_pc_opts.persp = n;
  else if (!strcmp(key, "subpixel"))
    g_pc_opts.subpixel = n;
  else if (!strcmp(key, "res")) {
    if (n < 0 || n > 8)
      return -1;
    g_pc_opts.res = n;
  }
  else if (!strcmp(key, "window")) {
    int w, h;
    if (sscanf(v, "%dx%d", &w, &h) != 2 || w < 320 || h < 240)
      return -1;
    g_pc_opts.win_w = w;
    g_pc_opts.win_h = h;
  } else if (!strcmp(key, "shot")) {
    const char *p = v;
    while (*p && g_pc_opts.nshots < PC_MAX_SHOTS) {
      g_pc_opts.shots[g_pc_opts.nshots++] = atoi(p);
      p = strchr(p, ',');
      if (!p)
        break;
      p++;
    }
  } else if (!strcmp(key, "pad"))
    snprintf(g_pc_opts.pad, sizeof g_pc_opts.pad, "%s", v);
  else if (!strcmp(key, "disc"))
    snprintf(g_pc_opts.disc, sizeof g_pc_opts.disc, "%s", v);
  else
    return -1;
  return 0;
}

static void read_config(void) {
  char path[1100], line[1200];
  FILE *f;
  snprintf(path, sizeof path, "%sconfig.txt", g_pc_user_dir);
  if (!(f = fopen(path, "r")))
    return;
  while (fgets(line, sizeof line, f)) {
    char key[64], val[1100];
    if (line[0] == '#' || sscanf(line, " %63[a-z_] = %1099[^\r\n]", key, val) != 2)
      continue;
    if (set_opt(key, val) == 0)
      host_log("config: %s=%s", key, val);
    else
      host_log("config: unknown or bad line %s=%s", key, val);
  }
  fclose(f);
}

static void usage(void) {
  fprintf(stderr,
          "usage: spyro [--disc DIR] [--fullscreen] [--window WxH] [--widescreen 0|1]\n"
          "             [--interp 0|1] [--vsync 0|1] [--filter 0|1] [--analog 0|1]\n"
          "             [--frames N] [--shot V1,V2,...] [--stats 0|1|2] [--pad FILE]\n"
          "             [--gpu 0|1] [--persp 0|1] [--subpixel 0|1] [--res 0..8] [--virtual 0|1]\n"
          "DIR holds the disc image (.bin + .cue), the extracted files or EBOOT.PBP.\n");
}

static int parse_args(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (strncmp(a, "--", 2) != 0) {
      usage();
      return -1;
    }
    a += 2;
    if (!strcmp(a, "fullscreen")) {
      g_pc_opts.fullscreen = 1;
      continue;
    }
    if (!strcmp(a, "help")) {
      usage();
      exit(0);
    }
    if (i + 1 >= argc || set_opt(a, argv[i + 1]) != 0) {
      usage();
      return -1;
    }
    i++;
  }
  return 0;
}

static int dir_exists(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

/* where the disc is: --disc, a "disc" folder beside the executable, or the
 * user folder */
static void find_disc_dir(void) {
  const char *base = pc_gap_base_dir();
  char beside[1100];
  if (g_pc_opts.disc[0]) {
    snprintf(s_disc_dir, sizeof s_disc_dir, "%s", g_pc_opts.disc);
    return;
  }
  snprintf(beside, sizeof beside, "%sdisc", base);
  if (base[0] && dir_exists(beside)) {
    snprintf(s_disc_dir, sizeof s_disc_dir, "%s", beside);
    return;
  }
  snprintf(s_disc_dir, sizeof s_disc_dir, "%s", g_pc_user_dir);
  {
    size_t n = strlen(s_disc_dir);
    if (n > 1 && (s_disc_dir[n - 1] == '/' || s_disc_dir[n - 1] == '\\'))
      s_disc_dir[n - 1] = 0;
  }
}

static void no_disc(void) {
  const PsxCdError *e = psx_cd_error();
  char text[2600];
  snprintf(text, sizeof text,
           "No Spyro the Dragon disc data was found in\n%s\n\n%s%s"
           "Put your own disc image (.bin + .cue), the files tools/extract_disc.py writes, "
           "or the PlayStation Store copy's EBOOT.PBP there, or start with --disc DIR.",
           s_disc_dir, e->code ? e->text : "", e->code ? "\n\n" : "");
  pc_message(e->code ? e->title : "Spyro: no disc", text);
  pc_exit(1);
}

static int game_thread(void *arg) {
  int here;
  char dir[1100];
  (void)arg;
  pc_crash_init(); /* the game thread's own alternate signal stack */
#ifdef _WIN32
  /* the process isn't large-address-aware: every stack is below 2 GB */
  if (!low(&here, 0)) {
#else
  if (!((unsigned char *)&here >= s_game_stack && (unsigned char *)&here < s_game_stack + GAME_STACK)) {
#endif
    host_log("the game thread isn't on a low stack (%p)", (void *)&here);
    pc_exit(1);
  }
  /* the GPU renderer (D243) on this thread's own GL context, before the
   * game's first draw; if it can't start, the software rasterizer draws */
  if (g_pc_opts.gpu) {
    if (pc_video_game_context() == 0 && pc_gpu_init() == 0)
      g_pc_gpu = 1;
    else
      host_log("gpu: the GPU renderer can't start: the software rasterizer draws (as gpu=0)");
  }
  /* the arena first, the presenter's area after it (as the Vita, D51, D129) */
  pres_draw_want();
  psx_arena_init();
  g_nd_direct_ok = !g_pc_gpu; /* the software rasterizer takes direct vertices (D99) */

  /* the save folder: the user folder's save/ (gaps.c G1 makes it the backend's root) */
  if (enc_path(ENC_DIR_SAVE, "disc-verified.txt", dir, sizeof dir) != ENC_OK) {
    host_log("no save folder in %s", g_pc_user_dir);
    pc_exit(1);
  }
  psx_cd_set_verify(dir, NULL);
  psx_cd_set_clock(pc_now_us);
  if (psx_cd_open(s_disc_dir) != 0)
    no_disc();
  host_log("disc: %s", psx_cd_source());
  psx_clock_set_hz(psx_region_vblank_hz());
  g_psx_spu_vblank_samples = PSX_SPU_RATE / psx_region_vblank_hz();
  if (g_Region != PSX_REGION_US)
    host_log("region: %s, %d Hz vblanks", psx_region()->serial, psx_region_vblank_hz());
  nd_init(host_log);
  pc_pad_init();
  if (pc_audio_init() != 0)
    host_log("audio: off");
  {
    char saves[1100];
    enc_path(ENC_DIR_SAVE, "", dir, sizeof dir);
    psx_mcrd_set_root(psx_region_path(saves, sizeof saves, dir, NULL, 1));
    host_log("disc: Spyro the Dragon (%s), %s; saves in %s", psx_region()->area,
             psx_region()->serial, saves);
  }
  psx_cd_set_read_latency(0); /* reads complete inside CdRead (D17) */
  port_cheats_set_log(host_log);

  /* widescreen and 60 fps (docs/presentation.md), the Vita's defaults */
  g_present.widescreen = g_pc_opts.widescreen != 0;
  g_present.interp = g_present.interp_mobys = g_present.interp_anim = g_pc_opts.interp != 0;
  g_present.stats = g_pc_opts.stats > 1;
  psx_clock_set_due_display(1); /* frames shown at their due display vblank (D148) */
  pc_frames_install_capture();
  host_log_flush();
  spyro_main(); /* never returns */
  return 0;
}

#ifndef _WIN32
static void *game_thread_posix(void *arg) {
  game_thread(arg);
  return NULL;
}
#else
static void game_thread_enc(void *arg) { game_thread(arg); }
#endif

static int start_game_thread(void) {
#ifdef _WIN32
  /* low.c: the process is not large-address-aware, so every thread's
   * stack is below 2 GB (game_thread checks); s_game_stack goes unused.
   * Never joined: the process ends with it (pc_exit, or main's return). */
  const EncThreadConfig cfg = {"spyro game", GAME_STACK, ENC_PRIO_NORMAL, -1};
  EncThread *th;
  int rc;
  (void)s_game_stack;
  if ((rc = enc_thread_start(&th, &cfg, game_thread_enc, NULL)) != ENC_OK) {
    host_log("the game thread: %s", enc_error_name(rc));
    return -1;
  }
  return 0;
#else
  pthread_attr_t attr;
  pthread_t th;
  int e;
  if ((e = pthread_attr_init(&attr)) || (e = pthread_attr_setstack(&attr, s_game_stack, GAME_STACK)) ||
      (e = pthread_create(&th, &attr, game_thread_posix, NULL))) {
    host_log("the game thread: error %d", e);
    return -1;
  }
  pthread_detach(th);
  return 0;
#endif
}

int main(int argc, char **argv) {
  const void *arena_end = psx_low_arena_end();
  EncPlatformConfig pcfg = {"spyro", "Spyro the Dragon", argc, argv};
  int rc;
#if SPYRO_P32_OFFSET
  {
    char why[160];
    if (p32_image_start(why, sizeof why) < 0) {
      fprintf(stderr, "spyro: %s (the offset model, D250)\n", why);
      return 1;
    }
  }
  if (!low(arena_end, 0) || !low(s_game_stack, GAME_STACK) || !low((void *)&pc_exit, 0)) {
    fprintf(stderr, "spyro: the game's memory must lie within 4 GB of the image (the offset model, D250): %s\n",
            pc_low_report());
    return 1;
  }
#else
  if (!low(arena_end, 0) || !low(s_game_stack, GAME_STACK) || !low((void *)&pc_exit, 0)) {
    fprintf(stderr, "spyro: the game's memory must lie below 2 GB (the low image, D174): %s\n",
            pc_low_report());
    return 1;
  }
#endif
  if (parse_args(argc, argv) != 0)
    return 2;
  pc_crash_init();
  pc_gap_user_dir(g_pc_user_dir, sizeof g_pc_user_dir); /* and the backend's root (G1) */
  {
    char log[1100];
    snprintf(log, sizeof log, "%sspyro.log", g_pc_user_dir);
    host_log_open(log);
  }
  host_log("user folder %s", g_pc_user_dir);
  read_config();
  if (parse_args(argc, argv) != 0) /* again: the command line wins */
    return 2;
  pc_gap_window_size(g_pc_opts.win_w, g_pc_opts.win_h); /* G3 */
  if ((rc = enc_platform_init(&pcfg)) != ENC_OK) {
    fprintf(stderr, "spyro: enc_platform_init: %s\n", enc_error_name(rc));
    host_log("enc_platform_init: %s", enc_error_name(rc));
    host_log_flush();
    return 1;
  }
  {
    const EncPlatformInfo *info = enc_platform_info();
    host_log("Spyro PC (" __DATE__ " " __TIME__ "), Encore platform API %d (%s, %d cores); %s, %d pages "
             "below 4 GB reserved",
             ENC_PLATFORM_API, info->target, info->cpu_cores, pc_low_report(),
             pc_low_reserved_pages());
  }
  find_disc_dir();
  host_log("disc folder %s", s_disc_dir);
  if (g_pc_opts.nshots) {
    char d[1100];
    snprintf(d, sizeof d, "%sshots", g_pc_user_dir);
    pc_mkdir(d);
  }
  pc_frames_init();
  pc_input_init();
  if (pc_video_init() != 0) {
    pc_message("Spyro: no display", "The window or its OpenGL 3.3 context could not be made. "
                                    "See spyro.log in the user folder.");
    return 1;
  }
  host_log_flush();
  if (start_game_thread() != 0)
    return 1;
  while (!g_pc_quit) {
    EncEvent ev;
    enc_input_poll(); /* the backend's events: F11, resizes, pads, the close button */
    while (enc_event_poll(&ev)) /* focus and resizes need nothing here */
      if (ev.type == ENC_EVENT_QUIT)
        g_pc_quit = 1;
    if (g_pc_quit || enc_platform_should_quit())
      break;
    pc_input_update();
    pc_video_frame();
  }
  g_pc_quit = 1;
  /* the game thread exits at its next vblank (vblank.c); if it is stuck
   * (a load), go anyway */
  host_log("window closed");
  enc_sleep_us(500000);
  host_log_flush();
  pc_audio_close();
  pc_video_close();
  enc_platform_shutdown();
  return 0;
}

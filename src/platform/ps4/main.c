/* PS4 entry point (docs/ps4-plan.md P3, decisions D193).
 *
 * The low-image model (D173, D174; on a PS4, D180): everything the game can
 * point at lies below 2 GB. The eboot is a PIE the PS4 loads at 0x400000; the
 * arena is a .bss array (arena.c) and the game runs on a thread whose stack is
 * a .bss array (scePthreadAttrSetstack), since the hand C takes the addresses
 * of locals. main() checks all three and refuses to run otherwise. Host
 * memory (malloc, other threads' stacks) is above 8 GB on a PS4 (D180): a
 * host pointer that reaches a P32 field or a u32 by mistake points at nothing.
 *
 * Before anything reads the game's data image, the 32-bit words a PIE can't
 * relocate get their addresses (s1_pie_words, tools/data2s.py --pie).
 *
 * The system libraries the eboot imports beyond libkernel aren't loaded by
 * the PS4 for a fake PKG (hardware run 1: a call into libSceUserService
 * found it missing, PRX_NOT_RESOLVED_FUNCTION): main() loads each through
 * sceSysmoduleLoadModuleInternal before anything calls it.
 *
 * Options (/data/spyro/config.txt, key=value lines): frames=N, stats=0|1|2,
 * realtime=0|1, shot=V, shot_every=N, interp_budget=0|1, gpu=0|1,
 * menu_test=V,..., and the options menu's rows, which it writes back when
 * it closes (widescreen, interp, analog, free_camera, cam_*, touch_menu,
 * skip_logos, state_slot; ps4.h). */
#include "ps4.h"
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

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>

#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#define LOW_LIMIT 0x80000000ull /* the game's addresses: below 2 GB */
#define GAME_STACK (8u << 20)   /* the main thread's usual 8 MB (linux64's) */

extern char __text_start[], _end[]; /* link.x; --defsym (CMakeLists.txt) */
extern const void *psx_low_arena_end(void); /* arena.c */
extern const uint64_t s1_pie_words[];       /* tools/data2s.py --pie */
extern void spyro_main(void);
void pres_draw_want(void);                   /* src/game/pres/pres_draw.c */
void nd_init(void (*log)(const char *fmt, ...)); /* src/game/native_draw.c */
extern int g_nd_direct_ok;

/* Small alignment on purpose: create-fself maps .bss as if it began right
 * after the data, so a large alignment can leave the end of .bss unmapped
 * (D180, tools/ps4/oelf_check.py runs after every link). */
static unsigned char s_game_stack[GAME_STACK] __attribute__((aligned(64)));

static const char k_build[] = __DATE__ " " __TIME__; /* stamps the gpu_novsync marker */
const char *volatile g_ps4_where = "starting";
volatile unsigned g_ps4_beat;

/* The watchdog: every 500 ms, whether the game thread's VSync count moved.
 * After 3 s without, one log line naming where it is, flushed (GPU run 3
 * hung with nothing in the log after start-up). */
static void *watchdog(void *arg) {
  unsigned last = g_ps4_beat, still = 0, told = 0;
  (void)arg;
  for (;;) {
    sceKernelUsleep(500000);
    if (g_ps4_beat != last) {
      last = g_ps4_beat;
      if (told)
        host_log("watchdog: the game thread moves again (after %u.%u s)", still / 2, still % 2 * 5);
      still = told = 0;
      continue;
    }
    if (++still == 6 || (told && still % 20 == 0)) {
      const char *where = g_ps4_where;
      told = 1;
      host_log("watchdog: the game thread has been at \"%s\" for %u s (VSync %u)", where,
               still / 2, last);
      if (!strncmp(where, "eglSwapBuffers", 14) && g_ps4_opts.gpu_vsync) {
        FILE *f = fopen(PS4_NOVSYNC, "w");
        if (f) { /* the build's stamp first: another build tries VSync again */
          fprintf(f, "%s\neglSwapBuffers blocked with swap interval 1; delete this file to "
                     "try again\n", k_build);
          fclose(f);
        }
        host_log("watchdog: wrote " PS4_NOVSYNC ": the next start swaps with interval 0");
        ps4_notify("Spyro: the GPU's swap is stuck. Close the game and start it again.");
      }
      host_log_flush();
    }
  }
  return NULL;
}

/* the Vita's defaults (vita/main.c): 16:9, 60 Hz logic in play (D150), the
 * free camera (D53); the GLES2 renderer when the player's Piglet loads */
Ps4Opts g_ps4_opts = {.stats = 1, .realtime = 1, .analog = 1, .widescreen = 1, .interp = 4,
                      .interp_budget = 1, .free_camera = 1, .cam_invert_x = 1,
                      .cam_invert_y = 1, .cam_speed = 100, .cam_recentre = 1, .gpu = 1,
                      /* swap interval 0, as OpenOrbis's Piglet sample: with 1
                       * the first eglSwapBuffers never returned (GPU runs 3-5) */
                      .gpu_vsync = 0, .shader_dump = 1,
                      .draw_distance = 100,
                      .touch_menu = 1 /* the owner's choice, 2026-11-21 (D194) */};
int g_ps4_user = -1;

unsigned long long ps4_now_us(void) { return sceKernelGetProcessTime(); }

void ps4_notify(const char *fmt, ...) {
  OrbisNotificationRequest req;
  va_list ap;
  memset(&req, 0, sizeof req);
  req.type = NotificationRequest;
  req.targetId = -1;
  req.useIconImageUri = 1;
  strcpy(req.iconUri, "cxml://psnotification/tex_icon_system");
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, ap);
  va_end(ap);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
  host_log("notify: %s", req.message);
}

void ps4_exit(int code) {
  host_log_flush();
  fflush(stdout);
  exit(code);
}

/* the kernel's debug output: GoldHEN's klog (port 3232) has it even when the
 * log file can't be written */
static void klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void klog(const char *fmt, ...) {
  char line[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  sceKernelDebugOutText(0, "%s", line);
}

/* the system libraries we call, loaded by hand (see the top) */
static const struct {
  OrbisSysModuleInternal id;
  const char *name;
} k_modules[] = {
    {ORBIS_SYSMODULE_INTERNAL_USER_SERVICE, "libSceUserService"},
    {ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT, "libSceVideoOut"},
    {ORBIS_SYSMODULE_INTERNAL_PAD, "libScePad"},
    {ORBIS_SYSMODULE_INTERNAL_AUDIOOUT, "libSceAudioOut"},
    {ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE, "libSceSystemService"}, /* Piglet's samples link it */
};
#define N_MODULES (int)(sizeof k_modules / sizeof k_modules[0])
static uint32_t s_module_rc[N_MODULES];

static int low(const void *p, size_t len) { return (uintptr_t)p + len <= LOW_LIMIT; }

/* s1_pie_words: {slot, target} pairs, {0, 0} at the end; each slot a 32-bit
 * word of the data image that holds target's address on the PS1 */
static int pie_fixups(void) {
  const uint64_t *w;
  int n = 0;
  for (w = s1_pie_words; w[0]; w += 2, n++) {
    if (w[1] >= LOW_LIMIT || w[0] >= LOW_LIMIT)
      return -1;
    *(uint32_t *)(uintptr_t)w[0] = (uint32_t)w[1];
  }
  return n;
}

static void parse_list(const char *v, int *out, int *n, int max) {
  char buf[256], *tok, *save = NULL;
  snprintf(buf, sizeof buf, "%s", v);
  for (tok = strtok_r(buf, ",", &save); tok && *n < max; tok = strtok_r(NULL, ",", &save))
    out[(*n)++] = atoi(tok);
}

/* config.txt: key=value lines. The menu's rows (present_ps4.c, pad.c,
 * menu_game.c, savestate_ps4.c) take their own keys; the rest are here. */
static void read_config(void) {
  char line[256];
  FILE *f = fopen(PS4_DATA_DIR "/config.txt", "r");
  if (!f)
    return;
  while (fgets(line, sizeof line, f)) {
    char key[64], v[160];
    int n;
    if (line[0] == '#' || sscanf(line, " %63[a-z_0-9] = %159[^\r\n]", key, v) != 2)
      continue;
    n = atoi(v);
    if (!strcmp(key, "frames"))
      g_ps4_opts.frames = n;
    else if (!strcmp(key, "stats"))
      g_ps4_opts.stats = n;
    else if (!strcmp(key, "realtime"))
      g_ps4_opts.realtime = n;
    else if (!strcmp(key, "shot"))
      g_ps4_opts.shot_at = n;
    else if (!strcmp(key, "shot_every"))
      g_ps4_opts.shot_every = n;
    else if (!strcmp(key, "interp_budget"))
      g_ps4_opts.interp_budget = n;
    else if (!strcmp(key, "gpu"))
      g_ps4_opts.gpu = n;
    else if (!strcmp(key, "shader_dump"))
      g_ps4_opts.shader_dump = n;
    else if (!strcmp(key, "gpu_vsync"))
      g_ps4_opts.gpu_vsync = n;
    else if (!strcmp(key, "menu_test"))
      parse_list(v, g_ps4_opts.menu_test, &g_ps4_opts.nmenu_test, PS4_MAX_MENU_TEST);
    else if (!strcmp(key, "config_version"))
      continue;
    else if (!ps4_menu_load(key, v)) {
      host_log("config: %s: not a key of ours", key);
      continue;
    }
    host_log("config: %s=%s", key, v);
  }
  fclose(f);
}

static void no_disc(void) {
  const PsxCdError *e = psx_cd_error();
  host_log("disc: %s: %s", e->code ? e->title : "no disc", e->code ? e->text : "");
  ps4_notify("Spyro: no disc in " PS4_DATA_DIR "/ (a .bin/.cue image, the extracted files or "
             "the Store copy's EBOOT.PBP). See spyro.log.");
  ps4_exit(1);
}

static void *game_thread(void *arg) {
  int here, fixed;
  (void)arg;
  if (!((unsigned char *)&here >= s_game_stack && (unsigned char *)&here < s_game_stack + GAME_STACK)) {
    klog("spyro: the game thread isn't on its low stack (%p)\n", (void *)&here);
    exit(1);
  }
  mkdir(PS4_DATA_DIR, 0777);
  mkdir(PS4_DATA_DIR "/save", 0777);
  host_log_open(PS4_DATA_DIR "/spyro.log");
  host_log("Spyro PS4 (" __DATE__ " " __TIME__ "): image %p-%p, arena end %p, game stack %p-%p",
           (void *)__text_start, (void *)_end, psx_low_arena_end(), (void *)s_game_stack,
           (void *)(s_game_stack + GAME_STACK));
  for (int i = 0; i < N_MODULES; i++)
    host_log("module %s: sceSysmoduleLoadModuleInternal 0x%x", k_modules[i].name,
             (unsigned)s_module_rc[i]);
  host_log_flush();
  fixed = pie_fixups();
  if (fixed < 0) {
    host_log("pie: a data word's slot or target is above 2 GB");
    ps4_exit(1);
  }
  host_log("pie: %d data words given their addresses", fixed);
  host_log_flush(); /* a crash loses what isn't written (hardware run 1) */
  read_config();
  {
    FILE *f = fopen(PS4_NOVSYNC, "r");
    if (f) { /* the watchdog saw eglSwapBuffers stuck with interval 1 */
      char line[64] = "";
      if (!fgets(line, sizeof line, f))
        line[0] = 0;
      fclose(f);
      line[strcspn(line, "\n")] = 0;
      if (!strcmp(line, k_build)) {
        g_ps4_opts.gpu_vsync = 0;
        host_log("gpu: " PS4_NOVSYNC " is there: swap interval 0");
      } else { /* written by another build (GPU run 4's second display handle) */
        remove(PS4_NOVSYNC);
        host_log("gpu: " PS4_NOVSYNC " from another build (%s) removed: swap interval 1", line);
      }
    }
  }
  {
    pthread_t th;
    if (pthread_create(&th, NULL, watchdog, NULL) != 0)
      host_log("watchdog: didn't start");
  }
  if (g_ps4_opts.shot_at)
    mkdir(PS4_DATA_DIR "/shots", 0777);

  {
    OrbisUserServiceInitializeParams param;
    memset(&param, 0, sizeof param);
    param.priority = 700; /* ORBIS_KERNEL_PRIO_FIFO_LOWEST */
    sceUserServiceInitialize(&param);
    if (sceUserServiceGetInitialUser(&g_ps4_user) < 0)
      host_log("user: no initial user");
  }

  /* the arena first, the presenter's area after it (as the Vita, D51, D129) */
  pres_draw_want();
  psx_arena_init();
  g_nd_direct_ok = 1; /* the software rasterizer takes direct vertices (D99) */

  host_log("user 0x%x", (unsigned)g_ps4_user);
  host_log_flush();
  /* the GLES2 renderer needs the player's own Piglet and shader compiler
   * (D193, D194); without them, the software renderer, and a notification
   * saying why (as the Vita's D163 screen) */
  if (g_ps4_opts.gpu) {
    char why[256] = "";
    if (ps4_gpu_load(why, sizeof why) == 0 && ps4_gpu_init() == 0)
      g_ps4_gpu = 1;
    else {
      if (!why[0])
        snprintf(why, sizeof why, "the GLES2 renderer couldn't start (see spyro.log)");
      host_log("renderer: software: %s", why);
      ps4_notify("Spyro: the software renderer: %s", why);
    }
  } else
    host_log("renderer: software (gpu=0)");
  if (g_ps4_gpu)
    host_log("renderer: GLES2 on Piglet (gpu=1)");
  host_log_flush();
  if (ps4_video_init() != 0) {
    ps4_notify("Spyro: no display (sceVideoOut). See spyro.log.");
    ps4_exit(1);
  }
  psx_cd_set_verify(PS4_DATA_DIR "/save/disc-verified.txt", NULL);
  psx_cd_set_clock(ps4_now_us);
  if (psx_cd_open(PS4_DATA_DIR) != 0)
    no_disc();
  host_log("disc: %s", psx_cd_source());
  host_log_flush(); /* a crash loses what isn't written (hardware run 1) */
  psx_clock_set_hz(psx_region_vblank_hz());
  g_psx_spu_vblank_samples = PSX_SPU_RATE / psx_region_vblank_hz();
  if (g_Region != PSX_REGION_US)
    host_log("region: %s, %d Hz vblanks", psx_region()->serial, psx_region_vblank_hz());
  ps4_present_init(); /* after the region's clock (60 Hz logic, PAL's rates) */
  ps4_raster_init();  /* the software rasterizer's threads (raster_mt.c) */
  ps4_picture_init(); /* the PICTURE rows: the GPU's options, the draw distance */
  nd_init(host_log);
  if (ps4_pad_init() != 0)
    ps4_notify("Spyro: no controller (scePad). See spyro.log.");
  if (ps4_audio_init() != 0)
    host_log("audio: off");
  host_log_flush(); /* a crash loses what isn't written (hardware run 1) */
  {
    char dir[256];
    psx_mcrd_set_root(psx_region_path(dir, sizeof dir, PS4_DATA_DIR "/save", NULL, 1));
    host_log("disc: Spyro the Dragon (%s), %s; saves in %s", psx_region()->area,
             psx_region()->serial, dir);
  }
  psx_cd_set_read_latency(0); /* reads complete inside CdRead (D17) */
  port_cheats_set_log(host_log); /* menu_game.c's cheats (D50) */
  ps4_savestate_init();
  host_log_flush();
  ps4_notify("Spyro PS4: starting (%s)", psx_region()->serial);
  g_ps4_where = "the game";
  spyro_main(); /* never returns */
  return NULL;
}

int main(void) {
  const void *arena_end = psx_low_arena_end();
  OrbisPthreadAttr attr;
  OrbisPthread th;
  int e;
  klog("spyro: main: image %p-%p, arena end %p, game stack %p\n", (void *)__text_start,
       (void *)_end, arena_end, (void *)s_game_stack);
  if (!low(_end, 0) || !low(arena_end, 0) || !low(s_game_stack, GAME_STACK)) {
    klog("spyro: the game's memory must lie below 2 GB (the low image, D174)\n");
    ps4_notify("Spyro: the PS4 loaded the game above 2 GB (image %p-%p, arena end %p, stack %p); "
               "it can't run.", (void *)__text_start, (void *)_end, arena_end,
               (void *)s_game_stack);
    return 1;
  }
  for (int i = 0; i < N_MODULES; i++) {
    s_module_rc[i] = sceSysmoduleLoadModuleInternal(k_modules[i].id);
    klog("spyro: %s: 0x%x\n", k_modules[i].name, (unsigned)s_module_rc[i]);
  }
  if ((e = scePthreadAttrInit(&attr)) != 0 ||
      (e = scePthreadAttrSetstack(&attr, s_game_stack, GAME_STACK)) != 0 ||
      (e = scePthreadCreate(&th, &attr, game_thread, NULL, "spyro_game")) != 0) {
    klog("spyro: the game thread: 0x%x\n", (unsigned)e);
    return 1;
  }
  scePthreadJoin(th, NULL);
  return 0;
}

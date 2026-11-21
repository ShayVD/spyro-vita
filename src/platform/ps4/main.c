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
 * Options (/data/spyro/config.txt, key=value lines): frames=N, stats=0|1|2,
 * realtime=0|1, analog=0|1, shot=V. Everything else is the game's defaults. */
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
#include <sys/stat.h>

#include <orbis/libkernel.h>
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

Ps4Opts g_ps4_opts = {.stats = 1, .realtime = 1, .analog = 1};
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

static void read_config(void) {
  char line[256];
  FILE *f = fopen(PS4_DATA_DIR "/config.txt", "r");
  if (!f)
    return;
  while (fgets(line, sizeof line, f)) {
    char key[64];
    int v;
    if (line[0] == '#' || sscanf(line, " %63[a-z_] = %d", key, &v) != 2)
      continue;
    if (!strcmp(key, "frames"))
      g_ps4_opts.frames = v;
    else if (!strcmp(key, "stats"))
      g_ps4_opts.stats = v;
    else if (!strcmp(key, "realtime"))
      g_ps4_opts.realtime = v;
    else if (!strcmp(key, "analog"))
      g_ps4_opts.analog = v;
    else if (!strcmp(key, "shot"))
      g_ps4_opts.shot_at = v;
    else
      continue;
    host_log("config: %s=%d", key, v);
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
  fixed = pie_fixups();
  if (fixed < 0) {
    host_log("pie: a data word's slot or target is above 2 GB");
    ps4_exit(1);
  }
  host_log("pie: %d data words given their addresses", fixed);
  read_config();
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

  if (ps4_video_init() != 0) {
    ps4_notify("Spyro: no display (sceVideoOut). See spyro.log.");
    ps4_exit(1);
  }
  psx_cd_set_verify(PS4_DATA_DIR "/save/disc-verified.txt", NULL);
  psx_cd_set_clock(ps4_now_us);
  if (psx_cd_open(PS4_DATA_DIR) != 0)
    no_disc();
  host_log("disc: %s", psx_cd_source());
  psx_clock_set_hz(psx_region_vblank_hz());
  g_psx_spu_vblank_samples = PSX_SPU_RATE / psx_region_vblank_hz();
  if (g_Region != PSX_REGION_US)
    host_log("region: %s, %d Hz vblanks", psx_region()->serial, psx_region_vblank_hz());
  nd_init(host_log);
  if (ps4_pad_init() != 0)
    ps4_notify("Spyro: no controller (scePad). See spyro.log.");
  if (ps4_audio_init() != 0)
    host_log("audio: off");
  {
    char dir[256];
    psx_mcrd_set_root(psx_region_path(dir, sizeof dir, PS4_DATA_DIR "/save", NULL, 1));
    host_log("disc: Spyro the Dragon (%s), %s; saves in %s", psx_region()->area,
             psx_region()->serial, dir);
  }
  psx_cd_set_read_latency(0); /* reads complete inside CdRead (D17) */
  port_cheats_set_log(host_log);
  host_log_flush();
  ps4_notify("Spyro PS4: starting (%s)", psx_region()->serial);
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
  if ((e = scePthreadAttrInit(&attr)) != 0 ||
      (e = scePthreadAttrSetstack(&attr, s_game_stack, GAME_STACK)) != 0 ||
      (e = scePthreadCreate(&th, &attr, game_thread, NULL, "spyro_game")) != 0) {
    klog("spyro: the game thread: 0x%x\n", (unsigned)e);
    return 1;
  }
  scePthreadJoin(th, NULL);
  return 0;
}

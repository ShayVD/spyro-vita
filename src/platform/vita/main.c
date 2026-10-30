/* PS Vita entry point (docs/vita-testing.md, decisions D25-D26).
 *
 *   ux0:data/spyro/            the files tools/extract_disc.py writes from the
 *                              user's own disc (manifest.json, WAD.WAD,
 *                              PETEXA*.STR, ...); never in the VPK
 *   ux0:data/spyro/save/       memory cards: card0 (port A), card1 (port B)
 *   ux0:data/spyro/spyro.log   the log (send it back after a test)
 *   ux0:data/spyro/config.txt  optional key=value lines:
 *       disc=PATH       the extracted files somewhere else
 *       frames=N        exit after N vblanks (default 0: never)
 *       shot=V1,V2,...  PNGs of the picture at these vblanks, in shots/
 *       shot_every=N    and every N vblanks
 *       stats=N         0: no timing line; 1 (default): one every 300 vblanks;
 *                       2: also a line per pad change
 *       filter=N        0: nearest-neighbour scaling; 1 (default): linear
 *       pad=PATH        a pad script in the linux32 host's --pad format
 *                       ("<vblank> <button>[+<button>...] | none"), played on
 *                       top of the controller (tests, docs/vita-testing.md)
 *       wav=1           also write the audio to ux0:data/spyro/audio.wav
 *       reartouch=1     the rear touch pad's left/right half is L2/R2
 *                       (the right stick always is, decisions D27)
 *       realtime=0      virtual vblanks: VSync(0) is exactly one vblank, as
 *                       on the linux32 host, however long a frame takes (for
 *                       scripted runs compared with the host; D28)
 *       gpu=0           the software rasterizer instead of the GPU renderer
 *                       (gpu=1, the default, needs ur0:data/libshacccg.suprx
 *                       and falls back to gpu=0 without it; D35)
 *       gpu_res=N       GPU targets at N times the PS1's pixels (default 0:
 *                       the screen's own resolution)
 *       shot_hd=1       with gpu=1, each shot also as a 960x544 PNG
 *       gpu_readback=0  with gpu=1, read drawn pixels back from the GPU
 *                       (glReadPixels) instead of replaying the draws in the
 *                       software rasterizer (default 1)
 *       mixer=N         where the SPU mix and XA decode run (D37): 1 (default)
 *                       their own thread; 2 that thread, but each vblank waits
 *                       for its mix; 0 the game thread, as before
 */
#define SDL_MAIN_HANDLED
#include "vita.h"
#include "psx_arena.h"
#include "psx_hooks.h"

#include <SDL2/SDL.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* newlib's heap (the arena's 16 MB-aligned 2 MB comes out of it, D25) and a
 * small main stack: the game's MIPS stack lives in the arena, and kh1's
 * 52 MiB stack failed on hardware while Vita3K took it (D7). */
int _newlib_heap_size_user = 128 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

VitaOpts g_vita_opts = {.disc_dir = VITA_DATA_DIR, .stats = 1, .filter = 1, .realtime = 1,
                        .gpu = 1, .gpu_readback = 1, .mixer = 1};

extern void spyro_main(void);

/* tools/vita/arena_syms.py (D25): the data labels' pointers, and the data
 * words that hold a label's PS1 address. */
typedef struct {
  void **ptr;
  uint32_t psx_addr;
} VitaSym;
extern const VitaSym s1_vita_syms[];
extern uint32_t *const s1_vita_fixups[];

static void arena_symbols(void) {
  const VitaSym *s;
  uint32_t *const *f;
  uint32_t delta = (uint32_t)g_psx_arena_base - PSX_RAM_BASE;
  int n = 0, k = 0;
  for (s = s1_vita_syms; s->ptr; s++, n++)
    *s->ptr = PSX_ARENA(s->psx_addr);
  /* before port.c's psx_layout copies the images into the arena */
  for (f = s1_vita_fixups; *f; f++, k++)
    **f += delta;
  vita_log("arena at 0x%08x: %d data labels, %d address words moved",
           (unsigned)g_psx_arena_base, n, k);
}

static int file_exists(const char *path) {
  SceIoStat st;
  return sceIoGetstat(path, &st) >= 0;
}

static void parse_list(const char *v, int *out, int *n, int max) {
  char buf[512], *tok;
  snprintf(buf, sizeof buf, "%s", v);
  for (tok = strtok(buf, ","); tok && *n < max; tok = strtok(NULL, ","))
    out[(*n)++] = atoi(tok);
}

static void read_config(void) {
  char line[512];
  FILE *f = fopen(VITA_DATA_DIR "/config.txt", "r");
  if (!f)
    return;
  while (fgets(line, sizeof line, f)) {
    char *eq = strchr(line, '='), *v, *e;
    if (line[0] == '#' || !eq)
      continue;
    *eq = 0;
    v = eq + 1;
    for (e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); )
      *--e = 0;
    if (!strcmp(line, "disc"))
      snprintf(g_vita_opts.disc_dir, sizeof g_vita_opts.disc_dir, "%s", v);
    else if (!strcmp(line, "frames"))
      g_vita_opts.frames = atoi(v);
    else if (!strcmp(line, "shot"))
      parse_list(v, g_vita_opts.shots, &g_vita_opts.nshots, VITA_MAX_SHOTS);
    else if (!strcmp(line, "shot_every"))
      g_vita_opts.shot_every = atoi(v);
    else if (!strcmp(line, "stats"))
      g_vita_opts.stats = atoi(v);
    else if (!strcmp(line, "filter"))
      g_vita_opts.filter = atoi(v);
    else if (!strcmp(line, "pad"))
      snprintf(g_vita_opts.pad, sizeof g_vita_opts.pad, "%s", v);
    else if (!strcmp(line, "wav"))
      g_vita_opts.wav = atoi(v);
    else if (!strcmp(line, "reartouch"))
      g_vita_opts.reartouch = atoi(v);
    else if (!strcmp(line, "realtime"))
      g_vita_opts.realtime = atoi(v);
    else if (!strcmp(line, "gpu"))
      g_vita_opts.gpu = atoi(v);
    else if (!strcmp(line, "gpu_res"))
      g_vita_opts.gpu_res = atoi(v);
    else if (!strcmp(line, "gpu_dump"))
      g_vita_opts.gpu_dump = atoi(v);
    else if (!strcmp(line, "gpu_readback"))
      g_vita_opts.gpu_readback = atoi(v);
    else if (!strcmp(line, "mixer"))
      g_vita_opts.mixer = atoi(v);
    else if (!strcmp(line, "shot_hd"))
      g_vita_opts.shot_hd = atoi(v);
    else
      vita_log("config: unknown key %s", line);
  }
  fclose(f);
}

/* Without the disc files there is nothing to run: say so on screen (a red
 * picture) and in the log, and wait for START to quit. */
static void no_disc(void) {
  vita_log("no %s/manifest.json: copy the files from your own disc there "
           "(docs/vita-testing.md)", g_vita_opts.disc_dir);
  for (;;) {
    SDL_GameController *c = SDL_NumJoysticks() > 0 ? SDL_GameControllerOpen(0) : NULL;
    SDL_PumpEvents();
    vita_video_fill_red();
    if (c && SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START))
      sceKernelExitProcess(0);
  }
}

int main(int argc, char **argv) {
  char path[300];
  (void)argc;
  (void)argv;
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  vita_log_open();
  vita_log("Spyro Vita (" __DATE__ " " __TIME__ ")");
  read_config();
  sceIoMkdir(VITA_DATA_DIR "/save", 0777);
  if (g_vita_opts.nshots || g_vita_opts.shot_every)
    sceIoMkdir(VITA_DATA_DIR "/shots", 0777);

  /* gpu=1 needs vitaGL's runtime shader compiler; without it, gpu=0 */
  if (g_vita_opts.gpu && !file_exists("ur0:data/libshacccg.suprx") &&
      !file_exists("ur0:data/external/libshacccg.suprx")) {
    vita_log("gpu: no ur0:data/libshacccg.suprx, so no shader compiler: "
             "using the software renderer (gpu=0)");
    g_vita_opts.gpu = 0;
  }
  g_vita_gpu = g_vita_opts.gpu != 0;
  vita_log("renderer: %s", g_vita_gpu ? "GPU (vitaGL, gpu=1)" : "software (gpu=0)");

  SDL_SetMainReady();
  /* with gpu=1 vitaGL owns the GXM display; SDL is only the pad and audio */
  if (SDL_Init((g_vita_gpu ? 0 : SDL_INIT_VIDEO) | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    vita_log("SDL_Init failed: %s", SDL_GetError());
    sceKernelExitProcess(1);
  }
  if (vita_video_init() != 0) {
    /* once vitaGL has the display, SDL's renderer can't take it */
    vita_log("video: renderer set-up failed%s", g_vita_gpu ? " (try gpu=0 in config.txt)" : "");
    sceKernelExitProcess(1);
  }
  vita_pad_init();
  if (g_vita_opts.pad[0] && vita_pad_load(g_vita_opts.pad) != 0)
    sceKernelExitProcess(1);
  vita_audio_init();

  psx_arena_init();
  arena_symbols();
  psx_mcrd_set_root(VITA_DATA_DIR "/save");
  snprintf(path, sizeof path, "%s/manifest.json", g_vita_opts.disc_dir);
  {
    FILE *f = fopen(path, "rb");
    if (!f)
      no_disc();
    fclose(f);
  }
  if (psx_cd_open(g_vita_opts.disc_dir) != 0) {
    vita_log("psx_cd_open(%s) failed", g_vita_opts.disc_dir);
    no_disc();
  }
  /* reads complete inside CdRead, as on the linux32 host (D17) */
  psx_cd_set_read_latency(0);
  vita_log("disc %s, frames %d", g_vita_opts.disc_dir, g_vita_opts.frames);
  spyro_main(); /* never returns */
  return 0;
}

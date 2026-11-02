/* PS Vita entry point (docs/vita-testing.md, decisions D25-D26).
 *
 *   ux0:data/spyro/            the user's own disc: its image (.bin + .cue,
 *                              or .img; read directly, D60), or the files
 *                              tools/extract_disc.py writes from it
 *                              (manifest.json, WAD.WAD, PETEXA*.STR, ...);
 *                              never in the VPK
 *   ux0:data/spyro/save/disc-verified.txt  the image whose WAD.WAD was
 *                              checked (D60)
 *   ux0:data/spyro/save/       memory cards: card0 (port A), card1 (port B)
 *   ux0:data/spyro/spyro.log   the log (send it back after a test)
 *   ux0:data/spyro/config.txt  optional key=value lines:
 *       disc=PATH       the disc image or extracted files somewhere else
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
 *                       (the right stick's X is too, unless free_camera=1;
 *                       decisions D27)
 *       analog=0        a digital pad: the left stick is the d-pad, 8
 *                       directions (default 1: a DualShock in analog mode,
 *                       the game reads the sticks; D46)
 *       free_camera=0   the right stick is L2/R2 again (default 1: it turns
 *                       the camera in proportion and tilts it up and down;
 *                       let go, the game's own camera takes over again;
 *                       needs analog=1; D47, D53)
 *       cam_invert_x=0  the free camera turns the other way (default 1)
 *       cam_invert_y=0  up on the free camera and in Triangle look mode
 *                       looks down, as in the original (default 1: up
 *                       looks up)
 *       cam_speed=N     the free camera's turn speed, 50..200 percent
 *                       (default 100, L2/R2's)
 *                       (the controls are also in the menu, CONTROLS)
 *       realtime=0      virtual vblanks: VSync(0) is exactly one vblank, as
 *                       on the linux32 host, however long a frame takes (for
 *                       scripted runs compared with the host; D28)
 *       gpu=0           the software rasterizer instead of the GPU renderer
 *                       (gpu=1, the default, needs ur0:data/libshacccg.suprx
 *                       and falls back to gpu=0 without it; D35)
 *       res=native      gpu=1 draws at the screen's resolution (the default:
 *                       the 4:3 picture is 778x544 screen pixels, 16:9 is
 *                       960x544, from the 512x224 the game draws; D67);
 *                       res=ps1 (or 1): at the PS1's own 512x240,
 *                       scaled up when shown (filter=); res=N, 2..4: N times
 *                       the PS1's, scaled down when shown (D40). gpu_res=
 *                       is the old name.
 *       gpu_time=1      a GPU-timed frame in the "gpu" log lines every 300
 *                       vblanks (two glFinish: a hitch at 60 fps, so the
 *                       default is 0; D68); gpu_time=2 also times that
 *                       frame's scene on its own
 *                       and counts GL draw calls and vertex uploads
 *                       (measurement only, docs/fps-plan.md)
 *       shot_hd=1       with gpu=1, each shot also as a 960x544 PNG
 *       gpu_readback=0  with gpu=1, read drawn pixels back from the GPU
 *                       (glReadPixels) instead of replaying the draws in the
 *                       software rasterizer (default 1)
 *       widescreen=0    4:3 instead of the default 16:9 (a wider view; the
 *                       game logic is unchanged; docs/presentation.md, D30)
 *       interp=1        60 fps, experimental (default 0: 30 fps; D44):
 *                       an in-between frame per tick (camera, Spyro, mobys,
 *                       animation, particles); 2: no animation blending;
 *                       3: camera only. interp=1 needs config_version=2 (or 3) in
 *                       the file too: without it the line is taken as the
 *                       old default the menu used to write, and read as 0
 *                       (widescreen, interp, interp_budget and the other
 *                       menu rows can also be set in the game: hold SELECT
 *                       and press START; the menu writes them back here,
 *                       with config_version=2; D45)
 *       interp_budget=0 no safety net: the in-between frame always runs.
 *                       Any other value (default 1): skip it only while the
 *                       game thread's own CPU time per tick would pass 15/16
 *                       of the tick (D75; the GPU never makes it skip)
 *       mixer=N         where the SPU mix and XA decode run (D37): 1 (default)
 *                       their own thread; 2 that thread, but each vblank waits
 *                       for its mix; 0 the game thread, as before
 *       render_thread=0 GL on the game thread, as before D63 (default 1:
 *                       with gpu=1, every GL call on a render thread of its
 *                       own on core 2, the game thread on core 0; D68)
 *       xa_ahead=0      the XA music's sectors read on the game thread
 *                       (default 1: read ahead of the CD head on core 1;
 *                       D64, D68)
 *       gpu_opaque=0    every atlas triangle drawn with the discard shader
 *                       (default 1: with gpu=1 and tex_cache=1, triangles
 *                       whose texture blocks have no transparent or
 *                       semi-transparent texel drawn by a shader without
 *                       discard, the same pixels; D65, D68). A 0 for any of
 *                       these three needs config_version=3 in the file too:
 *                       without it, it is taken as written before they were
 *                       on by default, and ignored
 *       fast_render=0   the renderers as translated from the asm and
 *                       PsyCross's GTE_operator (default 1: the hand-written
 *                       renderers and the inline GTE commands, the same
 *                       results; D72)
 *       gpu_direct=0    with gpu=1, each widescreen or 60 fps picture copied
 *                       into its slot and the screen cleared before it, as
 *                       before D69 (default 1: a picture that is one target
 *                       whole is shown from it, or takes its texture when
 *                       the game clears it; the same pixels)
 *       gpu_lean=1      with gpu=1 and neither persp nor subpixel, a 28-byte
 *                       vertex instead of 40 (the same pixels; D70)
 *       parse_thread=1  with gpu=1 and render_thread=1, PsyCross's OT walk on
 *                       a thread of its own (core 1), walking a copy of the
 *                       frame's primitives while the game goes on (default
 *                       0, experimental: D71)
 *       parse_core=3    that thread on core 3, the system's, if the start-up
 *                       check ("cores" line) found a thread of ours may run
 *                       there; else core 1 (default 1; D71)
 *       tex_filter=N    with gpu=1, filter textures after the palette lookup:
 *                       1 bilinear (the GPU's own, cheap), 2 3-point (N64
 *                       style, by hand, slower) (default 0: off; D43;
 *                       decisions D39)
 *       persp=1         with gpu=1, perspective-correct textures on polygons
 *                       the GTE projected (D39)
 *       subpixel=1      with gpu=1, those polygons' vertices at their exact
 *                       sub-pixel positions (D39)
 *                       (tex_filter, persp, subpixel, res, draw_distance are
 *                       also options menu rows: picture.c, D48)
 *       draw_distance=N longer draw distance, percent: 150 or 200 (default
 *                       100: the original; experimental, D49): the world's
 *                       detail and the mobys, picture only (src/game/lod.c)
 *       tex_cache=0     with gpu=1, sample textures from VRAM with the
 *                       palette lookup per pixel instead of the decoded
 *                       atlas (default 1; the same pixels; D41)
 *       bench=1         with gpu=1, leave the game alone (no input): from
 *                       vblank 600 it cycles res, tex_filter, persp and
 *                       subpixel through the intro, title and attract demos
 *                       and logs each set's speed ("bench" lines; D41), at
 *                       30 fps, 4:3 and 16:9; bench=2 keeps interp=
 *       shot_hold=MS    after each shot, hold the game (its picture on
 *                       screen) until hold.ack appears, at most MS ms
 *                       (tools/vita/vita3k_run.sh CAPTURE=1)
 *       poke=V:ADDR=VALUE  at vblank V, write a 32-bit word to PS1 RAM (hex
 *                       ADDR and VALUE; tests, as the linux32 host's --poke)
 *       hang_dump=0     no crash dump when the watchdog finds the game stuck
 *                       (default 1: 20 s after its "hang:" line; watchdog.c, D73)
 *       hang_test=V     tests: the game thread spins from vblank V
 *                       (hang_test=V,wait: blocks on a semaphore)
 */
#define SDL_MAIN_HANDLED
#include "vita.h"
#include "watchdog.h"
#include "psx_arena.h"
#include "psx_cdimage.h"
#include "psx_hooks.h"
#include "../../game/cheats_port.h"

#include <SDL2/SDL.h>
#include <psp2/display.h>
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
                        .gpu = 1, .gpu_readback = 1, .gpu_time = 0, .widescreen = 1, .interp = 0,
                        .interp_budget = 1, .mixer = 1, .tex_cache = 1, .analog = 1,
                        .render_thread = 1, .xa_ahead = 1, .gpu_opaque = 1, /* D68 */
                        .gpu_direct = 1, /* D69 */
                        .dynres = 1, /* D75 */
                        .parse_core = 1,
                        .free_camera = 1, .cam_invert_x = 1, .cam_invert_y = 1, /* D53 */
                        .cam_speed = 100, .draw_distance = 100};

extern void spyro_main(void);
extern int g_fast_render; /* src/game/mips2c_rt.h, D72 */

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

/* res=native | ps1 | N */
static int parse_res(const char *v) {
  if (!strcmp(v, "native"))
    return 0;
  if (!strcmp(v, "ps1"))
    return 1;
  return atoi(v);
}

static void parse_poke(const char *v) {
  unsigned vb, addr, value;
  if (g_vita_opts.npokes < VITA_MAX_POKES && sscanf(v, "%u:%x=%x", &vb, &addr, &value) == 3) {
    g_vita_opts.pokes[g_vita_opts.npokes].vblank = (int)vb;
    g_vita_opts.pokes[g_vita_opts.npokes].addr = addr;
    g_vita_opts.pokes[g_vita_opts.npokes++].value = value;
  } else {
    vita_log("config: bad poke %s", v);
  }
}

/* render_thread=0, xa_ahead=0 and gpu_opaque=0 lines in a file older than
 * config_version=3, not taken (D68): the menu's save drops them (menu.c) */
const char *g_vita_config_stale[4];

static void read_config(void) {
  char line[512];
  int version = 1, interp_line = 0, i;
  /* D68: the three switches' lines; a 0 before config_version=3 was the
   * old default or a test file's (tools/vita/fps-runs) */
  static const char *const k_switch_keys[3] = {"render_thread", "xa_ahead", "gpu_opaque"};
  int *const switches[3] = {&g_vita_opts.render_thread, &g_vita_opts.xa_ahead,
                            &g_vita_opts.gpu_opaque};
  int switch_line[3] = {0, 0, 0};
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
    if (!strcmp(line, "interp"))
      interp_line = 1; /* D44, below */
    if (vita_menu_load(line, v))
      ; /* a menu row's key: widescreen, interp, interp_budget, ... (menu.c, D45) */
    else if (!strcmp(line, "disc"))
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
    else if (!strcmp(line, "realtime"))
      g_vita_opts.realtime = atoi(v);
    else if (!strcmp(line, "gpu"))
      g_vita_opts.gpu = atoi(v);
    else if (!strcmp(line, "gpu_res")) /* res='s old name; res= is a menu row */
      g_vita_opts.gpu_res = parse_res(v);
    else if (!strcmp(line, "gpu_time"))
      g_vita_opts.gpu_time = atoi(v);
    else if (!strcmp(line, "shot_hold"))
      g_vita_opts.shot_hold = atoi(v);
    else if (!strcmp(line, "poke"))
      parse_poke(v);
    else if (!strcmp(line, "gpu_dump"))
      g_vita_opts.gpu_dump = atoi(v);
    else if (!strcmp(line, "gpu_readback"))
      g_vita_opts.gpu_readback = atoi(v);
    else if (!strcmp(line, "mixer"))
      g_vita_opts.mixer = atoi(v);
    else if (!strcmp(line, "render_thread"))
      g_vita_opts.render_thread = atoi(v), switch_line[0] = 1;
    else if (!strcmp(line, "xa_ahead"))
      g_vita_opts.xa_ahead = atoi(v), switch_line[1] = 1;
    else if (!strcmp(line, "gpu_opaque"))
      g_vita_opts.gpu_opaque = atoi(v), switch_line[2] = 1;
    else if (!strcmp(line, "fast_render"))
      g_fast_render = atoi(v);
    else if (!strcmp(line, "gpu_direct"))
      g_vita_opts.gpu_direct = atoi(v);
    else if (!strcmp(line, "gpu_lean"))
      g_vita_opts.gpu_lean = atoi(v);
    else if (!strcmp(line, "parse_thread"))
      g_vita_opts.parse_thread = atoi(v);
    else if (!strcmp(line, "parse_core"))
      g_vita_opts.parse_core = atoi(v);
    else if (!strcmp(line, "interp_budget")) /* a menu row until D75 */
      g_vita_opts.interp_budget = atoi(v);
    else if (!strcmp(line, "gpu_meter"))
      g_vita_opts.gpu_meter = atoi(v);
    else if (!strcmp(line, "shot_hd"))
      g_vita_opts.shot_hd = atoi(v);
    else if (!strcmp(line, "widescreen_exact"))
      g_vita_opts.widescreen_exact = atoi(v);
    else if (!strcmp(line, "menu_test"))
      parse_list(v, g_vita_opts.menu_test, &g_vita_opts.nmenu_test, 4);
    else if (!strcmp(line, "menu_keys")) {
      if (vita_menu_test_keys(v) != 0)
        vita_log("config: menu_keys: unknown button in %s", v);
    }
    else if (!strcmp(line, "bench"))
      g_vita_opts.bench = atoi(v);
    else if (!strcmp(line, "tex_cache"))
      g_vita_opts.tex_cache = atoi(v);
    else if (!strcmp(line, "state_test"))
      vita_savestate_config(v);
    else if (vita_watchdog_config(line, v))
      ; /* hang_dump=, hang_test= (watchdog.c, D73) */
    else if (!strcmp(line, "config_version"))
      version = atoi(v);
    else
      vita_log("config: unknown key %s", line);
  }
  fclose(f);
  /* Before 60 fps went experimental (D44) it was the default, and the menu
   * wrote interp=1 on every save whether or not the player chose it. */
  if (interp_line && g_vita_opts.interp == 1 && version < 2) {
    g_vita_opts.interp = 0;
    vita_log("config: interp=1 without config_version=2 is the old default: 60 fps stays off "
             "(turn it on in the menu, SELECT+START)");
  }
  /* The render thread, the XA read-ahead and the opaque path were off by
   * default until their hardware run (D63-D66); a 0 written before that
   * isn't the player's choice (D68). */
  for (i = 0; i < 3; i++)
    if (switch_line[i] && *switches[i] == 0 && version < 3) {
      *switches[i] = 1;
      g_vita_config_stale[i] = k_switch_keys[i]; /* NULL-ended: [3] stays NULL */
      vita_log("config: %s=0 without config_version=3 is from before the switch was on by "
               "default: on (D68)", k_switch_keys[i]);
    }
}

/* Without a disc there is nothing to run: say why on screen (bootscreen.c)
 * and in the log (psx_cd_error: no image, an .iso, a wrong disc, ...) and
 * wait for a button to quit. frames= quits after that many vblanks (Vita3K
 * tests). */
static void no_disc(void) {
  const PsxCdError *e = psx_cd_error();
  int buttons[] = {SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_START};
  int held = 1, frame = 0, i;
  SDL_GameController *c = NULL;
  vita_log("disc: %s: %s", e->title, e->text);
  vita_boot_panel(e->title, e->text, "PRESS X TO EXIT");
  for (;; frame++) {
    int down = 0;
    SDL_PumpEvents();
    if (!c && SDL_NumJoysticks() > 0)
      c = SDL_GameControllerOpen(0);
    for (i = 0; c && i < (int)(sizeof buttons / sizeof *buttons); i++)
      down |= SDL_GameControllerGetButton(c, buttons[i]);
    if (down && !held) /* a press, not a button held since the bubble */
      sceKernelExitProcess(0);
    held = down;
    sceDisplayWaitVblankStart(); /* the buttons once a vblank, not a spinning core */
    if (g_vita_opts.frames > 0 && frame >= g_vita_opts.frames) {
      vita_log("frames=%d reached, exiting", g_vita_opts.frames);
      sceKernelExitProcess(0);
    }
  }
}

/* The first start with a disc image hashes WAD.WAD (D60): a few seconds. */
static void disc_progress(int percent) {
  char text[200];
  if (percent % 4)
    return;
  snprintf(text, sizeof text,
           "First start: checking that your disc image is a good dump of SCUS-94228. This is "
           "done once.\n\n%d%%",
           percent);
  vita_boot_panel("CHECKING THE DISC IMAGE", text, "");
}

static unsigned long long now_us(void) { return sceKernelGetProcessTimeWide(); }

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  /* the arena first, before anything else uses the heap: save states hold
   * pointers into it, so it should land at the same address every launch,
   * whatever config.txt turns on (D51) */
  psx_arena_init();
  vita_log_open();
  vita_log("Spyro Vita (" __DATE__ " " __TIME__ ")");
  /* what the clocks above came to (docs/fps-plan.md) */
  vita_log("clocks: ARM %d MHz, bus %d, GPU %d, GPU crossbar %d", scePowerGetArmClockFrequency(),
           scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(),
           scePowerGetGpuXbarClockFrequency());
  read_config();
  vita_log("fast_render=%d: %s", g_fast_render,
           g_fast_render ? "hand-written renderers, inline GTE commands (D72)"
                         : "translated renderers, PsyCross's GTE_operator");
  vita_cores_probe(); /* may a thread of ours run on core 3? (D71) */
  if (g_vita_opts.bench == 1) /* the GPU options measured at 30 fps */
    g_vita_opts.interp = 0;
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
  /* the disc before the renderer: its screens (a first-start check, or why
   * the game can't start) go straight to the display (bootscreen.c, D60) */
  psx_cd_set_verify(VITA_DATA_DIR "/save/disc-verified.txt", disc_progress);
  psx_cd_set_clock(now_us);
  {
    unsigned long long t0 = now_us();
    PsxCdIo io;
    if (psx_cd_open(g_vita_opts.disc_dir) != 0) {
      vita_log("psx_cd_open(%s) failed", g_vita_opts.disc_dir);
      no_disc();
    }
    psx_cd_io(&io); /* the check's reads, which the "cd:" lines include */
    vita_log("disc: %s, opened in %u ms (%u reads, %u KB)", psx_cd_source(),
             (unsigned)((now_us() - t0) / 1000), io.reads, io.kbytes);
  }
  if (vita_video_init() != 0) {
    /* once vitaGL has the display, SDL's renderer can't take it */
    vita_log("video: renderer set-up failed%s", g_vita_gpu ? " (try gpu=0 in config.txt)" : "");
    sceKernelExitProcess(1);
  }
  vita_parse_init(); /* after the render thread (D71) */
  vita_present_init();
  vita_picture_init();
  vita_pad_init();
  if (g_vita_opts.pad[0] && vita_pad_load(g_vita_opts.pad) != 0)
    sceKernelExitProcess(1);
  vita_audio_init();
  vita_xa_ahead_init();

  arena_symbols();
  psx_mcrd_set_root(VITA_DATA_DIR "/save");
  /* reads complete inside CdRead, as on the linux32 host (D17) */
  psx_cd_set_read_latency(0);
  vita_log("disc %s, frames %d", g_vita_opts.disc_dir, g_vita_opts.frames);
  port_cheats_set_log(vita_log); /* menu_game.c's cheats (D50) */
  vita_savestate_init();
  vita_watchdog_start(); /* D73 */
  spyro_main(); /* never returns */
  return 0;
}

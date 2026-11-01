/* PS Vita platform layer (docs/vita-testing.md, decisions D25-D26): the calls
 * between its parts. The game data comes from ux0:data/spyro/ (the user's
 * disc image, or the files tools/extract_disc.py writes, with manifest.json;
 * D60), saves go to ux0:data/spyro/save/. */
#ifndef SPYRO_VITA_H
#define SPYRO_VITA_H

#include <stdint.h>

#define VITA_DATA_DIR "ux0:data/spyro"
#define VITA_MAX_SHOTS 32
#define VITA_MAX_POKES 16

typedef struct {
  char disc_dir[256]; /* disc=  (default ux0:data/spyro) */
  int frames;         /* frames= exit after this many vblanks; 0 = never */
  int shots[VITA_MAX_SHOTS]; /* shot=V1,V2,... PNGs of the picture in shots/ */
  int nshots;
  int shot_every;     /* shot_every=N */
  int stats;          /* stats=0 drops the line every 5 s of vblanks */
  int filter;         /* filter=0 nearest-neighbour scaling, 1 (default) linear */
  char pad[256];      /* pad= a pad script (linux32 --pad format), ORed with the controller */
  int wav;            /* wav=1 also writes the audio to audio.wav */
  int reartouch;      /* reartouch=1: rear touch left/right half = L2/R2 */
  int analog;         /* analog=1 (default): a DualShock with real sticks; 0: digital (D46) */
  int free_camera;    /* free_camera=1 (default): the right stick turns and tilts the camera (D47, D53) */
  int cam_invert_x;   /* cam_invert_x=1, cam_invert_y=1 (both default): the free camera's axes */
  int cam_invert_y;   /*   flipped; Y also flips Triangle look mode's up/down (D47, D53) */
  int cam_speed;      /* cam_speed=50..200: the free camera's turn speed in % (default 100) */
  int realtime;       /* realtime=0: virtual vblanks (D17) for exact test replays */
  int gpu;            /* gpu=1 (default): GPU renderer; 0: software rasterizer (D35) */
  int gpu_res;        /* res= (gpu_res=): GPU targets at N x the PS1's pixels; 0 (default,
                         res=native): the screen's; 1: res=ps1 (D40) */
  int gpu_time;       /* gpu_time=0: no GPU-timed frame per report (default 1) */
  int shot_hold;      /* shot_hold=MS: hold each shot's picture for a window capture */
  struct { int vblank; uint32_t addr, value; } pokes[VITA_MAX_POKES]; /* poke=V:ADDR=VALUE */
  int npokes;
  int shot_hd;        /* shot_hd=1: with gpu=1, also a 960x544 PNG of the screen per shot */
  int gpu_dump;       /* gpu_dump=V: log every split drawn at vblanks V, V+1 (gpu=1) */
  int gpu_readback;   /* gpu_readback=0: read the GPU back with glReadPixels, not by replay */
  int widescreen;     /* widescreen=1 (default): 16:9 (docs/presentation.md) */
  int interp;         /* interp=1 (default): 60 fps; 2: no animation blending; 3: camera only; 0: off */
  int widescreen_exact; /* widescreen_exact=1: keep the original 4:3 logic in play too (a hidden
                           4:3 draw: costs CPU); attract demos always do */
  int interp_budget;  /* interp_budget=MS: skip the in-between frame over this (default 26; 0 never) */
  int menu_test[4];   /* menu_test=V1,V2,...: open the options menu at these vblanks and
                         play a fixed sequence (tests without a controller: menu.c) */
  int nmenu_test;
  int mixer;          /* mixer=1 (default): SPU mix on its own thread; 2: lockstep; 0: game thread (D37) */
  int tex_filter;     /* tex_filter=1: bilinear, 2: 3-point texture filtering (gpu=1, D39, D43) */
  int persp;          /* persp=1: perspective-correct textures (gpu=1, D39) */
  int subpixel;       /* subpixel=1: sub-pixel vertex positions (gpu=1, D39) */
  int bench;          /* bench=1: cycle the GPU picture options and time each (D41) */
  int tex_cache;      /* tex_cache=1: textures sampled from the decoded atlas (gpu=1, D41) */
  int draw_distance;  /* draw_distance=150|200: longer draw distance, percent (D49); 100 off */
  int render_thread;  /* render_thread=1: the GPU renderer's GL calls on their own core (D63) */
  int xa_ahead;       /* xa_ahead=1: the XA music's sectors read ahead on core 1 (D64) */
  int gpu_opaque;     /* gpu_opaque=1: textures with no transparent texel drawn without discard (D65) */
  int parse_thread;   /* parse_thread=1: PsyCross's OT walk on its own thread, from a copy (D71) */
  int parse_core;     /* parse_core=3: that thread on core 3, if a user thread may run there (D71) */
} VitaOpts;

extern VitaOpts g_vita_opts;

/* log.c: ux0:data/spyro/spyro.log through sceIoWrite (D7) */
void vita_log_open(void);
void vita_log(const char *fmt, ...);  /* one line, no trailing \n needed */

/* vblank.c */
int vita_vblank_count(void);
void vita_set_vblank(int vblank); /* a save state's count (D51) */

/* video.c: the PS1 display rect, scaled 4:3 into 960x544 (either renderer) */
int vita_video_init(void); /* 0 = ok */
int vita_video_present(void); /* 1 = a new picture */

/* bootscreen.c: the disc screens before any renderer (D60) */
int vita_boot_panel(const char *title, const char *text, const char *footer); /* 0 = shown */
void vita_boot_close(void); /* frees it; at the renderer's first picture */

/* gpu.c: which renderer the GR_* calls reach, and who presents (D35) */
extern int g_vita_gpu;
extern int g_vita_gl;

/* gpu_gl.c: the GPU renderer on vitaGL */
int vita_gpu_init(void); /* 0 = ok, 1 = only presenting works (use gpu=0), <0 = failed */
int vita_gpu_present(void);
void vita_gpu_sync_display(void); /* VRAM gets what the GPU drew under the display */
void vita_gpu_shot_hd(int vblank); /* a 960x544 PNG at the next present */
void vita_gpu_report(int vblank); /* "gpu" line in the log */
int vita_gpu_set_options(int res, int filter, int persp, int subpixel, int cache); /* bench (D41), menu (D48) */
void vita_gpu_arm_sample(void);   /* GPU-time the next picture too */
int vita_gpu_take_sample(unsigned *frame_us, unsigned *tail_us); /* 1 = a new one */

/* bench.c: bench=1 (D41) */
void vita_bench_vblank(int v);
void vita_bench_present(int shown);

/* present_vita.c: widescreen and 60 fps (D30) */
void vita_present_init(void); /* after vita_video_init */
void vita_present_apply(void); /* g_vita_opts' switches -> the game's presenter */
void vita_present_report(int vblank);

/* picture.c: the menu's picture rows and the draw distance (D48, D49) */
void vita_picture_init(void); /* after vita_video_init */
void vita_picture_apply(void);
void vita_picture_report(int vblank);

/* menu.c: the port's options menu (hold SELECT, press START) */
uint16_t vita_menu_filter(uint16_t buttons); /* pad.c: hides the menu's buttons */
int vita_menu_service(void); /* vblank.c: 1 if the menu ran (the game was paused) */
int vita_menu_load(const char *key, const char *v); /* main.c: 1 if a menu row's key */
extern const char *g_vita_config_stale[4]; /* main.c: stale switch lines (D68), NULL-ended */
int vita_menu_test_keys(const char *list); /* main.c: menu_keys= for menu_test; 0 = ok */

/* video.c: an RGB panel over the picture (NULL: none), drawn 1:1 in the
 * middle; and a redraw even when the picture didn't change (the menu) */
void vita_video_overlay(const uint8_t *rgb, int w, int h);
void vita_video_force_redraw(void);
void vita_gpu_overlay(const uint8_t *rgb, int w, int h); /* gpu_gl.c */
void vita_gpu_force_redraw(void);
void vita_gpu_repeat(void); /* the picture on screen again (shot_hold) */
void vita_gpu_rescale(void); /* targets again at the current widescreen width */
uint16_t *vita_gpu_vram_take(void); /* save states: targets dropped, s_vram complete (D51) */
void vita_gpu_vram_loaded(void);    /* s_vram was overwritten */
/* gpu_gl.c: presentation frames captured and kept on the GPU */
struct PresentCapture;
const struct PresentCapture *vita_gpu_present_capture(void);
long long (*vita_gpu_render_cost(void))(void); /* present.c's g_present_render_us, or NULL */
unsigned (*vita_gpu_late_pictures(void))(void); /* present.c's g_present_late, or NULL */

/* pad.c: SDL2 game controller -> PS1 digital pad */
void vita_pad_init(void);
void vita_pad_vblank(void);
void vita_pad_poll(void); /* SDL events (controller hot-plug) */

/* pad.c: pad= script */
int vita_pad_load(const char *path); /* 0 = ok */
uint16_t vita_pad_read(void); /* the controller's buttons now (PS1 bits) */
void vita_pad_rewind(int vblank); /* a save state loaded: the script from vblank (D51) */

/* audio.c: the SPU mix (psx_audio_out) -> SDL2 audio, and wav=1 */
void vita_audio_init(void);
void vita_audio_close(void); /* finish audio.wav */
void vita_audio_clear(void); /* drop what is queued (a save state loaded) */
extern unsigned g_vita_audio_dry, g_vita_audio_cleared; /* SDL queue empty / cut, since the last report */

/* mixer.c: the SPU mix and XA decode on their own thread (D37) */
void vita_mixer_init(void);
void vita_mixer_vblank(void); /* once per vblank, in place of psx_spu_vblank */
void vita_mixer_drain(void);  /* wait until everything posted is mixed */
void vita_mixer_report(int vblank); /* "mixer" line in the log */
void vita_mixer_state(int load); /* save states: decoder state in or out of g_psx_spu (D51) */

/* xa_ahead.c: the XA music read ahead of the CD head (D64) */
void vita_xa_ahead_init(void);
int vita_xa_accept(int lba, unsigned char *raw); /* psx_xa_accept, from the read-ahead */
void vita_xa_ahead_report(int vblank);

/* parse.c: the OT walk on its own thread (D71), and the start-up check of
 * core 3 */
void vita_cores_probe(void); /* logs whether a user thread runs on core 3 */
void vita_parse_init(void);  /* after vita_video_init (the render thread) */
int vita_parse_on_walker(void); /* 1 on the parse thread */
void vita_parse_report(int vblank);
void vita_gpu_walk_submitted(void); /* gpu_thread.c: the display state for that walk */

/* savestate_vita.c: save states (D51) */
void vita_savestate_init(void);
void vita_savestate_config(const char *v); /* config.txt state_test= */

/* prof.c: per-vblank CPU time by part ("time" lines in the log) */
enum { PROF_GAME, PROF_OT, PROF_RASTER, PROF_SPU, PROF_PRESENT, PROF_WAIT, PROF_COUNT };
void prof_enter(int part);
void prof_leave(void);
void prof_vblank(void);
void prof_report(int vblank);
void prof_draw_report(int vblank, int vblanks); /* prof_draw.c: "draw" lines */
unsigned prof_vram_gen(void); /* changes whenever the rasterizer writes VRAM */
void prof_vram_touch(void);   /* VRAM was written some other way (a save state loaded) */

#endif

/* PS4 platform layer (docs/ps4-plan.md P3-P4, decisions D193, D194): the
 * calls between its parts. The game data comes from /data/spyro/ (the
 * player's disc image, the files tools/extract_disc.py writes, or the
 * PlayStation Store copy's EBOOT.PBP), as on the Vita from ux0:data/spyro/;
 * saves go to /data/spyro/save/. The low-image model (D173, D174): the
 * image, the arena and the game thread's stack lie below 2 GB (main.c
 * checks).
 *
 * Two renderers (D193, D194): the GLES2 renderer on Piglet (gpu_gles.c),
 * with the player's own Piglet and shader compiler modules, and linux32's
 * software rasterizer (gpu_soft.c) when they're missing, its picture
 * scaled into a sceVideoOut framebuffer (video.c). Both show the
 * presenter's frames (widescreen, 60 fps; present_ps4.c) at 1080p, and the
 * port's options menu over them (menu.c). */
#ifndef SPYRO_PS4_H
#define SPYRO_PS4_H

#include <stdint.h>

#define PS4_DATA_DIR "/data/spyro"
#define PS4_MAX_MENU_TEST 4

typedef struct {
  int frames;   /* frames= exit after this many vblanks; 0 = never */
  int stats;    /* stats=0 drops the line every 5 s of vblanks (default 1) */
  int realtime; /* realtime=0: virtual vblanks (D17), one per VSync(0) */
  int analog;   /* analog=1 (default): a DualShock with real sticks; 0: digital */
  int shot_at;  /* shot=V: a PNG of the picture in shots/ at vblank V (0: none) */
  /* the menu's rows (D194; the Vita's keys and defaults) */
  int widescreen;   /* widescreen=1 (default): 16:9 */
  int interp;       /* interp=4 (default, 60 Hz logic in play), 1 (interpolated), 0 (off) */
  int interp_budget; /* interp_budget=0: no safety net (default 1, D75) */
  int free_camera;  /* free_camera=1 (default): the right stick turns the camera */
  int cam_invert_x; /* cam_invert_x=1, cam_invert_y=1 (both default) */
  int cam_invert_y;
  int cam_speed;    /* cam_speed=50..200 (default 100) */
  int cam_recentre; /* cam_recentre=0: no swing back behind Spyro (D162) */
  int touch_menu;   /* touch_menu=1 (default): a tap on the touch pad opens the menu too (D194) */
  int persp;        /* persp=1: perspective-correct textures (the GPU's, the Vita's D39) */
  int subpixel;     /* subpixel=1: polygons' vertices at their exact positions (the GPU's) */
  int gpu_res;      /* res=native (0, default): the GPU draws at 1080p; res=ps1 (1): 512x240 */
  int draw_distance; /* draw_distance=100 (default, off), 150, 200 (D49, experimental) */
  int gpu;          /* gpu=1 (default): the GLES2 renderer if Piglet's modules load; 0: software */
  int shader_dump;  /* shader_dump=1 (default): with the player's Piglet pair, each compiled
                       shader saved once to shaders/ (D194: the binaries the firmware's
                       Piglet loads); 0: none */
  int gpu_vsync;    /* gpu_vsync=1: eglSwapInterval 1 (default 0: with 1 Piglet's first
                       swap never returned, GPU runs 3-5) */
  int menu_test[PS4_MAX_MENU_TEST]; /* menu_test=V1,...: the menu opened at these vblanks,
                                       a fixed sequence played (tests without a controller) */
  int nmenu_test;
  int shot_every;   /* shot_every=N: a PNG every N vblanks */
} Ps4Opts;

extern Ps4Opts g_ps4_opts;

/* main.c */
void ps4_notify(const char *fmt, ...) __attribute__((format(printf, 1, 2))); /* a system notification */
void ps4_exit(int code) __attribute__((noreturn));
unsigned long long ps4_now_us(void);

/* video.c: sceVideoOut, 1920x1080. The software path's pictures (the PS1
 * display or a presenter frame) are scaled on the CPU; the GPU path
 * (gpu_gles.c) draws through Piglet instead and video.c only keeps the
 * vblank count. */
int ps4_video_init(void); /* 0 = ok */
int ps4_video_handle(void);
int ps4_video_present(void); /* 1 = a new picture flipped */
uint64_t ps4_video_vcount(void); /* the display's vblanks since it opened */
void ps4_video_wait_vblank(void);
void ps4_video_report(int vblank);
/* an RGB panel over the picture (NULL: none), drawn at twice its size in
 * the middle (the menu, the save states' notices) */
void ps4_video_overlay(const uint8_t *rgb, int w, int h);

/* gpu_gles.c: the GLES2 renderer on Piglet (D194) */
extern int g_ps4_gpu; /* 1: the GR_* calls draw on the GPU, which presents */
int ps4_gpu_load(char *why, int whylen); /* the player's modules; 0 = loaded */
int ps4_gpu_init(void);                  /* after ps4_gpu_load; 0 = ok */
int ps4_gpu_present(void);
void ps4_gpu_overlay(const uint8_t *rgb, int w, int h);
void ps4_gpu_report(int vblank);
struct PresentCapture;
const struct PresentCapture *ps4_gpu_capture(void); /* the presenter's frames kept on the GPU */
void ps4_gpu_vram_loaded(void); /* a save state replaced VRAM */
void ps4_gpu_set_options(int persp, int subpixel); /* the PICTURE rows (picture_ps4.c) */
void ps4_gpu_set_res(int res);                     /* 0: 1080p targets, 1: the PS1's pixels */
/* picture_ps4.c: the PICTURE rows the Vita's picture.c has */
void ps4_picture_init(void); /* after the renderer is chosen */
void ps4_picture_report(int vblank);

/* pad.c: scePad -> the PS1 pad */
int ps4_pad_init(void); /* after sceUserServiceInitialize; 0 = ok */
void ps4_pad_vblank(void);
uint16_t ps4_pad_read(void); /* the controller's buttons now (PS1 bits), for the menu */
int ps4_pad_take_tap(void);  /* a touch pad tap since the last call */
void ps4_pad_controls_apply(void);

/* audio.c: the SPU's 44.1 kHz -> sceAudioOut at 48 kHz on its own thread */
int ps4_audio_init(void); /* 0 = ok */
void ps4_audio_report(int vblank);

/* present_ps4.c: widescreen and 60 fps (the Vita's D30, D148, D150) */
void ps4_present_init(void);
void ps4_present_apply(void);
void ps4_present_report(int vblank);

/* menu.c: the port's options menu (hold the touch pad's click, SELECT, and
 * press OPTIONS, START) */
uint16_t ps4_menu_filter(uint16_t buttons); /* pad.c: hides the menu's buttons */
int ps4_menu_service(void); /* vblank.c: 1 if the menu ran (the game was paused) */
int ps4_menu_load(const char *key, const char *v); /* main.c: 1 if a menu row's key */
void ps4_menu_request(void); /* pad.c: a tap (touch_menu=1) */
/* A question over the menu (the Vita's VitaMenuAsk, D136): UP/DOWN and X
 * answer 0..nchoice-1; O, OPTIONS or TRIANGLE -1. */
typedef struct {
  const char *title;
  const char *text[3];
  const char *choice[2];
  const char *hint[2];
  int nchoice;
  const char *footer;
  void (*done)(int answer);
} Ps4MenuAsk;
void ps4_menu_ask(const Ps4MenuAsk *ask);
void ps4_menu_quit(void) __attribute__((noreturn)); /* the options saved, then the app closes */

/* savestate_ps4.c: the menu's SAVE rows */
void ps4_savestate_init(void);

/* raster_mt.c: the software rasterizer's draws on several threads */
int ps4_raster_init(void); /* 0 = the workers are up */
void ps4_raster_report(int vblank);

/* vblank.c */
int ps4_vblank_count(void);

/* main.c: the watchdog (D194): the game thread says where it is and counts
 * its VSyncs; a stall of 3 s is logged with the place (and the log flushed).
 * A stall in eglSwapBuffers writes gpu_novsync: the next start swaps with
 * interval 0. */
extern const char *volatile g_ps4_where;
extern volatile unsigned g_ps4_beat;
#define PS4_NOVSYNC PS4_DATA_DIR "/gpu_novsync"

/* the user who started the game (main.c) */
extern int g_ps4_user;

#endif

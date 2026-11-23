/* PC platform layer (docs/pc-build.md): Linux and Windows desktop builds on
 * SDL3 (window, input, audio) and OpenGL 3.3 (the picture), in the low-image
 * model of linux64 and the PS4 (D173, D174): the image, the arena and the
 * game thread's stack lie below 2 GB.
 *
 * Two threads:
 *  - the main thread owns SDL: the window, its GL context, events, the
 *    keyboard and gamepads. It shows a picture at every display refresh:
 *    the newest frame whose due vblank the game's clock has reached
 *    (frames.c), as the Vita's render thread does with present_due=1 (D112).
 *  - the game thread (its stack a .bss array, main.c) runs the game, the
 *    software rasterizer (linux32's gpu_soft.c) and the SPU, paced by a 60 Hz
 *    clock (vblank.c). It files every frame it makes with its due vblank and
 *    never waits for the display.
 *
 * The game data: the player's disc image (.bin + .cue), the files
 * tools/extract_disc.py writes, or the PlayStation Store copy's EBOOT.PBP,
 * in the data folder (pc_data_dir: --disc, else a "disc" folder beside the
 * executable, else the user folder). Saves, the log, config.txt and
 * screenshots go in the user folder (SDL_GetPrefPath). */
#ifndef SPYRO_PC_H
#define SPYRO_PC_H

#include <stdint.h>

#define PC_MAX_SHOTS 16

typedef struct {
  int frames;      /* frames=N, --frames N: exit after N vblanks; 0 = never */
  int stats;       /* stats=0|1|2: the line every 5 s of vblanks (default 1) */
  int analog;      /* analog=1 (default): a DualShock's sticks; 0: digital */
  int fullscreen;  /* fullscreen=1, --fullscreen: start in (borderless) fullscreen */
  int vsync;       /* vsync=1 (default): the picture at the display's refresh */
  int filter;      /* filter=1 (default): linear scaling; 0: nearest */
  int widescreen;  /* widescreen=1 (default): 16:9 (docs/presentation.md) */
  int interp;      /* interp=1 (default): 60 fps in-between frames; 0: off */
  int win_w, win_h; /* window=WxH: the window's first size (default 1280x720) */
  int shots[PC_MAX_SHOTS]; /* shot=V1,V2,...: a PNG of the window at these vblanks */
  int nshots;
  char disc[1024]; /* --disc PATH: the game data's folder */
  char pad[1024];  /* pad=FILE: a pad script, ORed with the controls (pad.c) */
} PcOpts;

extern PcOpts g_pc_opts;
extern char g_pc_user_dir[1024]; /* saves, log, config.txt, shots (ends in '/') */

/* main.c */
void pc_exit(int code) __attribute__((noreturn)); /* from the game thread */
unsigned long long pc_now_us(void);
void pc_message(const char *title, const char *text); /* a message box, and the log */

/* crash.c: a fault's address and backtrace in the log (each thread calls it) */
void pc_crash_init(void);

/* frames.c: the game thread files pictures, the main thread shows them */
void pc_frames_init(void);
/* game thread: the PS1 display as it is now, due at our vblank `due` */
void pc_frames_post_display(int due);
/* game thread: present.c's capture (frames.c installs g_present_capture) */
void pc_frames_install_capture(void);
/* main thread: the newest frame due by `now` (our vblanks, see vblank.c);
 * 1 and *out filled if it is newer than the one last taken */
typedef struct {
  const uint32_t *rgba; /* w x h, 0xAABBGGRR as bytes R, G, B, A */
  int w, h;
  int wide;             /* 16:9 (the presenter's widescreen frame) */
  int due;
  unsigned seq;
} PcFrame;
int pc_frames_take(int now, PcFrame *out);
void pc_frames_release(void); /* main thread: done with the frame taken */
void pc_frames_report(int vblank);

/* vblank.c: the game's clock, at 60 Hz of real time */
int pc_vblank_now(void); /* any thread: our vblank due now by the clock */
extern volatile int g_pc_quit;

/* video.c (main thread) */
int pc_video_init(void); /* 0 = ok */
void pc_video_frame(void); /* show the newest due frame, swap */
void pc_video_event(const void *sdl_event);
void pc_video_shot(const char *path); /* the window's next picture as a PNG */
void pc_video_report(int vblank);
void pc_video_close(void);

/* input.c: SDL events (main thread) -> the PS1 pad (game thread) */
void pc_input_init(void);
void pc_input_event(const void *sdl_event);
void pc_input_read(uint16_t *buttons, unsigned char sticks[4]); /* any thread */

/* pad.c (game thread) */
void pc_pad_init(void);
void pc_pad_vblank(void);
int pc_pad_load(const char *path); /* 0 = ok */

/* audio.c */
int pc_audio_init(void); /* 0 = ok */
void pc_audio_report(int vblank);
void pc_audio_close(void);

#endif

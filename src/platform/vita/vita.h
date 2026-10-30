/* PS Vita platform layer (docs/vita-testing.md, decisions D25-D26): the calls
 * between its parts. The game data comes from ux0:data/spyro/ (the files
 * tools/extract_disc.py writes, with manifest.json), saves go to
 * ux0:data/spyro/save/. */
#ifndef SPYRO_VITA_H
#define SPYRO_VITA_H

#include <stdint.h>

#define VITA_DATA_DIR "ux0:data/spyro"
#define VITA_MAX_SHOTS 32

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
  int realtime;       /* realtime=0: virtual vblanks (D17) for exact test replays */
  int gpu;            /* gpu=1 (default): GPU renderer; 0: software rasterizer (D35) */
  int gpu_res;        /* gpu_res=N: GPU targets at N x the PS1's pixels; 0 (default): the screen's */
  int shot_hd;        /* shot_hd=1: with gpu=1, also a 960x544 PNG of the screen per shot */
  int gpu_dump;       /* gpu_dump=V: log every split drawn at vblanks V, V+1 (gpu=1) */
  int gpu_readback;   /* gpu_readback=0: read the GPU back with glReadPixels, not by replay */
  int widescreen;     /* widescreen=1: 16:9 (docs/presentation.md) */
  int interp;         /* interp=1: 60 fps; 2: no animation blending; 3: camera only */
  int interp_budget;  /* interp_budget=MS: skip the in-between frame over this (default 26; 0 never) */
} VitaOpts;

extern VitaOpts g_vita_opts;

/* log.c: ux0:data/spyro/spyro.log through sceIoWrite (D7) */
void vita_log_open(void);
void vita_log(const char *fmt, ...);  /* one line, no trailing \n needed */

/* vblank.c */
int vita_vblank_count(void);

/* video.c: the PS1 display rect, scaled 4:3 into 960x544 (either renderer) */
int vita_video_init(void); /* 0 = ok */
int vita_video_present(void); /* 1 = a new picture */
void vita_video_fill_red(void); /* the no-disc screen */

/* gpu.c: which renderer the GR_* calls reach, and who presents (D35) */
extern int g_vita_gpu;
extern int g_vita_gl;

/* gpu_gl.c: the GPU renderer on vitaGL */
int vita_gpu_init(void); /* 0 = ok, 1 = only presenting works (use gpu=0), <0 = failed */
int vita_gpu_present(void);
void vita_gpu_sync_display(void); /* VRAM gets what the GPU drew under the display */
void vita_gpu_shot_hd(int vblank); /* a 960x544 PNG at the next present */
void vita_gpu_fill(float r, float g, float b);
void vita_gpu_report(int vblank); /* "gpu" line in the log */

/* present_vita.c: widescreen and 60 fps (D30) */
void vita_present_init(void); /* after vita_video_init */
void vita_present_report(int vblank);
/* gpu_gl.c: presentation frames captured and kept on the GPU */
struct PresentCapture;
const struct PresentCapture *vita_gpu_present_capture(void);

/* pad.c: SDL2 game controller -> PS1 digital pad */
void vita_pad_init(void);
void vita_pad_vblank(void);
void vita_pad_poll(void); /* SDL events (controller hot-plug) */

/* pad.c: pad= script */
int vita_pad_load(const char *path); /* 0 = ok */

/* audio.c: the SPU mix (psx_audio_out) -> SDL2 audio, and wav=1 */
void vita_audio_init(void);
void vita_audio_close(void); /* finish audio.wav */

/* prof.c: per-vblank CPU time by part ("time" lines in the log) */
enum { PROF_GAME, PROF_OT, PROF_RASTER, PROF_SPU, PROF_PRESENT, PROF_WAIT, PROF_COUNT };
void prof_enter(int part);
void prof_leave(void);
void prof_vblank(void);
void prof_report(int vblank);
unsigned prof_vram_gen(void); /* changes whenever the rasterizer writes VRAM */

#endif

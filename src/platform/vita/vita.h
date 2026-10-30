/* PS Vita platform layer (docs/vita-testing.md, decisions D24-D25): the calls
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
} VitaOpts;

extern VitaOpts g_vita_opts;

/* log.c: ux0:data/spyro/spyro.log through sceIoWrite (D7) */
void vita_log_open(void);
void vita_log(const char *fmt, ...);  /* one line, no trailing \n needed */

/* vblank.c */
int vita_vblank_count(void);

/* video.c: the PS1 display rect, scaled 4:3 into 960x544 */
int vita_video_init(void); /* 0 = ok */
int vita_video_present(void); /* 1 = a new picture */

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

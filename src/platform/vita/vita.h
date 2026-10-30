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
} VitaOpts;

extern VitaOpts g_vita_opts;

/* log.c: ux0:data/spyro/spyro.log through sceIoWrite (D7) */
void vita_log_open(void);
void vita_log(const char *fmt, ...);  /* one line, no trailing \n needed */

/* vblank.c */
int vita_vblank_count(void);

/* video.c: the PS1 display rect, scaled 4:3 into 960x544 */
int vita_video_init(void); /* 0 = ok */
void vita_video_present(void);

/* pad.c: SDL2 game controller -> PS1 digital pad */
void vita_pad_init(void);
void vita_pad_vblank(void);
void vita_pad_poll(void); /* SDL events (controller hot-plug) */

/* audio.c: the SPU mix (psx_audio_out) -> SDL2 audio */
void vita_audio_init(void);

#endif

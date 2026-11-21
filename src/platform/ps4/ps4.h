/* PS4 platform layer (docs/ps4-plan.md P3, decisions D193): the calls between
 * its parts. The game data comes from /data/spyro/ (the player's disc image,
 * the files tools/extract_disc.py writes, or the PlayStation Store copy's
 * EBOOT.PBP), as on the Vita from ux0:data/spyro/; saves go to
 * /data/spyro/save/. The low-image model (D173, D174): the image, the arena
 * and the game thread's stack lie below 2 GB (main.c checks).
 *
 * The first renderer is linux32's software rasterizer (gpu_soft.c), its
 * picture scaled into a sceVideoOut framebuffer (video.c): it needs no Sony
 * module. The GLES2 renderer on Piglet comes with the player's own Piglet
 * and shader compiler modules (D193, the owner's choice). */
#ifndef SPYRO_PS4_H
#define SPYRO_PS4_H

#include <stdint.h>

#define PS4_DATA_DIR "/data/spyro"

typedef struct {
  int frames;   /* frames= exit after this many vblanks; 0 = never */
  int stats;    /* stats=0 drops the line every 5 s of vblanks (default 1) */
  int realtime; /* realtime=0: virtual vblanks (D17), one per VSync(0) */
  int analog;   /* analog=1 (default): a DualShock with real sticks; 0: digital */
  int shot_at;  /* shot=V: a PNG of the picture in shots/ at vblank V (0: none) */
} Ps4Opts;

extern Ps4Opts g_ps4_opts;

/* main.c */
void ps4_notify(const char *fmt, ...) __attribute__((format(printf, 1, 2))); /* a system notification */
void ps4_exit(int code) __attribute__((noreturn));
unsigned long long ps4_now_us(void);

/* video.c: sceVideoOut, the PS1 display scaled 4:3 into 1920x1080 */
int ps4_video_init(void); /* 0 = ok */
int ps4_video_handle(void);
int ps4_video_present(void); /* 1 = a new picture flipped */
uint64_t ps4_video_vcount(void); /* the display's vblanks since it opened */
void ps4_video_wait_vblank(void);
void ps4_video_report(int vblank);

/* pad.c: scePad -> the PS1 pad */
int ps4_pad_init(void); /* after sceUserServiceInitialize; 0 = ok */
void ps4_pad_vblank(void);

/* audio.c: the SPU's 44.1 kHz -> sceAudioOut at 48 kHz on its own thread */
int ps4_audio_init(void); /* 0 = ok */
void ps4_audio_report(int vblank);

/* the user who started the game (main.c) */
extern int g_ps4_user;

#endif

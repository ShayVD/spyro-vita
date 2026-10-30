/* linux32 headless host: options and the calls between its parts
 * (docs/porting-plan.md §5; virtual vblanks, pad script, soft GPU, PNG). */
#ifndef LINUX32_HOST_H
#define LINUX32_HOST_H

#include <stddef.h>
#include <stdint.h>

#define HOST_MAX_SHOTS 256

typedef struct {
  const char *disc_dir;
  int frames;          /* exit(0) once this many vblanks have passed; 0 = never */
  int shots[HOST_MAX_SHOTS];
  int nshots;
  int shot_every;      /* 0 = off */
  const char *shot_dir;
  int vram_shot;       /* also dump the whole VRAM at each shot */
  int dither;          /* PS1 4x4 dither on shaded/modulated prims */
  int moby_dump;       /* --moby-dump: mobys-V.tsv with every shot and step */
  int tri_dump;        /* log every triangle of the frame shown at this vblank */
  int out960;          /* pictures at 960x544 (presentation frames, present.c) */
  int npokes;          /* --poke V:ADDR=VALUE: test set-ups (a word in PS1 RAM) */
  struct { int vblank; uint32_t addr, value; } pokes[16];
  int npeeks;          /* --peek ADDR:N: log N words of PS1 RAM with every shot */
  struct { uint32_t addr; int n; } peeks[8];
} HostOpts;

extern HostOpts g_host_opts;

/* log.c */
void host_log_open(const char *path); /* also log to this file */
void host_log(const char *fmt, ...);  /* one line, no trailing \n needed */
void host_log_flush(void);

/* vblank.c */
int host_vblank_count(void);
void host_vblank(void); /* advance exactly one virtual vblank */

/* pad.c */
int host_pad_load(const char *path); /* 0 = ok */
int host_pad_load_after(const char *path); /* --pad-after: once the tour is done */
void host_pad_vblank(int vblank);

/* pad.c: buttons from the browser (--serve), ORed with the script's */
void host_pad_set_live(uint16_t mask);
int host_pad_parse(const char *text, uint16_t *mask); /* "up+cross", "none" */

/* tour.c: --tour LIST, --tour-stay N (level warps, credits, endings) */
int host_tour_set(const char *list); /* 0 = ok */
void host_tour_set_stay(int vblanks);
int host_tour_vblank(int vblank, int pad_done); /* buttons, or -1 */
int host_tour_done(void); /* every entry taken and its sequence over */

/* step.c: --step DIR, closed-loop stepping (tools/step.sh) */
void host_step_set_dir(const char *dir);
void host_step_vblank(int vblank);

/* serve.c: --serve PORT (decisions D22) */
int host_serve_start(int port); /* 0 = ok; vblanks then run at real time */
void host_serve_vblank(int vblank);

/* audio.c: --wav FILE, and PCM for --serve */
int host_audio_wav_open(const char *path); /* 0 = ok */
void host_audio_close(void);
long long host_audio_take(long long after, int16_t **pcm, size_t *frames);

/* gpu_soft.c: 1024x512 PS1 VRAM, 1-5-5-5 (bit 15 = mask/STP, R in low bits). */
uint16_t *host_vram(void);

/* png.c */
int host_png_write(const char *path, const uint8_t *rgb, int w, int h); /* 0 = ok */
int host_shot_display(const char *path); /* the picture the TV would show */
int host_shot_vram(const char *path);    /* whole VRAM as 15-bit (+ raw .bin) */
uint8_t *host_display_rgb(int *w, int *h); /* malloc'd RGB of the display */
void host_present_vblank(void); /* take this vblank's presentation frame */

#endif

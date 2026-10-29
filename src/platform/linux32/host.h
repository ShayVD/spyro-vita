/* linux32 headless host: options and the calls between its parts
 * (docs/porting-plan.md §5; virtual vblanks, pad script, soft GPU, PNG). */
#ifndef LINUX32_HOST_H
#define LINUX32_HOST_H

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
void host_pad_vblank(int vblank);

/* gpu_soft.c: 1024x512 PS1 VRAM, 1-5-5-5 (bit 15 = mask/STP, R in low bits). */
uint16_t *host_vram(void);

/* png.c */
int host_png_write(const char *path, const uint8_t *rgb, int w, int h); /* 0 = ok */
int host_shot_display(const char *path); /* the picture the TV would show */
int host_shot_vram(const char *path);    /* whole VRAM as 15-bit */

#endif

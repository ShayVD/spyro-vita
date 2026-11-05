/* The ring between a sound mixer (the producer) and the sound device's output thread (the consumer): s16 stereo
 * frames, one producer and one consumer thread, no locks.  Portable C, so hosts test it.
 *
 * Two ways to play it (VpkAudioRingConfig.rate_control):
 *   0  plain: the consumer takes what is there, up to what it asked for, and pads the rest with silence.  For a
 *      mixer paced by the output (it mixes until the ring holds enough: vpk_audio_ring_level), whose samples are
 *      made at the device's own rate.
 *   1  held level: for a producer paced by another clock (a game that mixes a vblank's worth each vblank: on the
 *      Vita the display's 59.94 Hz against the port's 44,100 or 48,000 frames a second).  A ring that only passes
 *      frames on runs dry by ~1 ms a second, whatever its size.  So the consumer plays the ring at a rate a slow
 *      controller sets, within 0.5% of 1, linearly interpolated: it holds the ring's low point (its level just
 *      before the producer's next push; after a dip it rises again only slowly, `rise` frames a pull) at `low`
 *      frames.  When it runs out (a load stops the game) it plays silence until the ring holds `prime` again, and
 *      drops what is above that; above `max` it drops back to `prime`.  The samples the producer made do not
 *      change; only what the device plays is resampled.
 *
 * From spyro-vita src/platform/common/psx_audio_ring.h / audio_ring.c @ 987363e (decisions D88: the held level; its
 * constants became VpkAudioRingConfig) and kh1-decomp host/vita/audio.c @ 85befa4a (the plain ring). */
#ifndef VPK_AUDIO_RING_H
#define VPK_AUDIO_RING_H

#define VPK_AUDIO_RING_MAX_FRAMES 16384 /* the most a ring holds (a power of two) */
#define VPK_AUDIO_RING_NO_CUT 0xFFFFFFFFu

typedef struct {
  unsigned frames;  /* the ring's size, a power of two up to VPK_AUDIO_RING_MAX_FRAMES */
  int rate_control; /* 0 plain, 1 held level (above) */
  /* held level only (spyro-vita's: low 2304, prime 3840, max 6615, window 16384, rise 2.0f at 44.1 kHz) */
  unsigned low;    /* frames: the low point held */
  unsigned prime;  /* frames: where play starts again after running out */
  unsigned max;    /* frames: above it, back to prime */
  unsigned window; /* frames played between the controller's steps */
  float rise;      /* frames a pull the low point rises by */
} VpkAudioRingConfig;

typedef struct {
  short buf[VPK_AUDIO_RING_MAX_FRAMES][2];
  VpkAudioRingConfig cfg;
  unsigned wr;  /* frames pushed (producer) */
  unsigned rd;  /* frames played (consumer) */
  unsigned cut; /* producer -> consumer: drop what is before this */
  /* the consumer's */
  unsigned frac;   /* between rd and rd + 1, of 65536 */
  unsigned step;   /* frames read per frame played, 16.16 */
  int priming;     /* filling to prime: play silence */
  float low;       /* the low point: the lowest level, rising cfg.rise a pull */
  unsigned played; /* frames played in this window */
  float rate;      /* the step - 1 */
  float drift;     /* the controller's integral: the clocks' ratio - 1 */
  unsigned last_level; /* frames in the ring at the last pull, before it took any */
  /* counts (the consumer's; the caller clears or diffs them) */
  unsigned underruns; /* times the ring ran out (plain: pulls that came up short) */
  unsigned silent;    /* pulls with any silence in them */
  unsigned trimmed;   /* frames dropped to come back to prime */
  unsigned trims;
  unsigned grains;    /* pulls */
  unsigned long long level_sum;
  unsigned level_min;
} VpkAudioRing;

void vpk_audio_ring_init(VpkAudioRing *r, const VpkAudioRingConfig *c);
/* The producer: frames of s16 stereo in; all or nothing (0 when they do not fit: the consumer stopped). */
int vpk_audio_ring_push(VpkAudioRing *r, const short *pcm, int frames);
/* The producer: frames in the ring now (what the consumer has not played yet). */
unsigned vpk_audio_ring_level(const VpkAudioRing *r);
/* The producer: drop everything pushed so far (a save state was loaded). */
void vpk_audio_ring_clear(VpkAudioRing *r);
/* The consumer: the next frames to play into out (silence where there are none).  Returns the frames taken from
 * the ring (plain), or the frames played that were not silence (held level). */
int vpk_audio_ring_pull(VpkAudioRing *r, short (*out)[2], int frames);
/* held level: frames read per frame played, in parts per million from 1 */
int vpk_audio_ring_ppm(const VpkAudioRing *r);

#endif /* VPK_AUDIO_RING_H */

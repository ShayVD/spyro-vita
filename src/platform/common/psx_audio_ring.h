/* The output ring between the mixer and the sound device (decisions D88).
 * The mixer pushes 735 frames a vblank, and the game's vblanks follow the
 * display's clock (59.94 Hz on the Vita, vblank.c), so it makes ~44,056
 * frames a second; the device plays 44,100 by its own clock. A ring that
 * only passes frames on runs dry by ~1 ms a second, whatever its size.
 *
 * So the consumer plays the ring at a rate a slow controller sets, within
 * 0.5% of 1, linearly interpolated: it holds the ring's low point (its
 * level just before the game's next post; after a dip it rises again only
 * slowly) at PSX_AUDIO_RING_LOW frames, where the clocks differ. When it runs out (a
 * load stops the game), it plays silence until the ring holds
 * PSX_AUDIO_RING_PRIME again, and drops what is above that: the audio
 * catches up with the game, as the picture does. The samples the mixer
 * made (audio.wav) don't change; only what the device plays is resampled.
 *
 * One producer and one consumer thread, no locks. Portable C, so the host
 * tests it (tests/audio). */
#ifndef PSX_AUDIO_RING_H
#define PSX_AUDIO_RING_H

#define PSX_AUDIO_RING_FRAMES 16384 /* a power of two */
#define PSX_AUDIO_RING_LOW 2304     /* frames: the low point held (a 512-frame grain, two late vblanks and some) */
#define PSX_AUDIO_RING_PRIME 3840   /* frames: ~87 ms, where play starts again after running out */
#define PSX_AUDIO_RING_MAX 6615     /* frames: ~150 ms; above it, back to PRIME */
#define PSX_AUDIO_RING_WINDOW 16384 /* frames played between the controller's steps: ~0.37 s */
#define PSX_AUDIO_RING_RISE 2.0f    /* frames a grain: a dip of a vblank is forgotten in ~4 s */
#define PSX_AUDIO_RING_NO_CUT 0xFFFFFFFFu

typedef struct {
  short buf[PSX_AUDIO_RING_FRAMES][2];
  unsigned wr;  /* frames pushed (producer) */
  unsigned rd;  /* frames played (consumer) */
  unsigned cut; /* producer -> consumer: drop what is before this */
  /* the consumer's */
  unsigned frac; /* between rd and rd + 1, of 65536 */
  unsigned step; /* frames read per frame played, 16.16 */
  int priming;   /* filling to PRIME: play silence */
  float low;     /* the low point: the lowest level, rising PSX_AUDIO_RING_RISE a grain */
  unsigned played; /* frames played in this window */
  float rate;    /* the step - 1 */
  float drift;   /* the controller's integral: the clocks' ratio - 1 */
  /* counts since the caller last cleared them (the consumer's) */
  unsigned underruns; /* times the ring ran out */
  unsigned silent;    /* grains with any silence in them */
  unsigned trimmed;   /* frames dropped to come back to PRIME */
  unsigned trims;
  unsigned grains;
  unsigned long long level_sum;
  unsigned level_min;
} PsxAudioRing;

void psx_audio_ring_init(PsxAudioRing *r);
/* The producer: frames of s16 stereo in. */
void psx_audio_ring_push(PsxAudioRing *r, const short *pcm, int frames);
/* The producer: drop everything pushed so far (a save state was loaded). */
void psx_audio_ring_clear(PsxAudioRing *r);
/* The consumer: the next frames to play. */
void psx_audio_ring_pull(PsxAudioRing *r, short (*out)[2], int frames);
/* frames read per frame played, in parts per million from 1 */
int psx_audio_ring_ppm(const PsxAudioRing *r);

#endif

/* Audio out for the headless linux32 host: the SPU mixer pushes 44.1 kHz
 * s16 stereo once per vblank (psx_spu_vblank). It goes to a WAV file
 * (--wav FILE) and to a ring that --serve hands to the browser. */
#include "host.h"
#include "psx_spu.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING (44100 * 2) /* frames: two seconds */

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static int16_t s_ring[RING * 2];
static long long s_written; /* frames pushed since start */
static FILE *s_wav;
static unsigned s_wav_frames;

static void put_le(uint8_t *p, uint32_t v, int n) {
  while (n--)
    *p++ = (uint8_t)v, v >>= 8;
}

static void wav_header(FILE *f, unsigned frames) {
  uint8_t h[44];
  memcpy(h, "RIFF", 4);
  put_le(h + 4, 36 + frames * 4, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  put_le(h + 16, 16, 4);
  put_le(h + 20, 1, 2);         /* PCM */
  put_le(h + 22, 2, 2);         /* stereo */
  put_le(h + 24, 44100, 4);
  put_le(h + 28, 44100 * 4, 4); /* bytes per second */
  put_le(h + 32, 4, 2);
  put_le(h + 34, 16, 2);
  memcpy(h + 36, "data", 4);
  put_le(h + 40, frames * 4, 4);
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, 44, f);
  fseek(f, 0, SEEK_END);
}

int host_audio_wav_open(const char *path) {
  s_wav = fopen(path, "wb");
  if (!s_wav) {
    host_log("audio: cannot write %s", path);
    return -1;
  }
  wav_header(s_wav, 0);
  return 0;
}

void host_audio_close(void) {
  if (s_wav) {
    wav_header(s_wav, s_wav_frames);
    fclose(s_wav);
    s_wav = NULL;
  }
}

void psx_audio_out(const short *pcm, int frames) {
  int i;
  if (s_wav) {
    fwrite(pcm, 4, (size_t)frames, s_wav);
    s_wav_frames += (unsigned)frames;
  }
  pthread_mutex_lock(&s_lock);
  for (i = 0; i < frames; i++) {
    int k = (int)((s_written + i) % RING);
    s_ring[k * 2] = pcm[i * 2];
    s_ring[k * 2 + 1] = pcm[i * 2 + 1];
  }
  s_written += frames;
  pthread_mutex_unlock(&s_lock);
}

/* Frames after position `after` (-1: start ~100 ms behind the newest).
 * Returns the new position, or -1 if nothing is new. */
long long host_audio_take(long long after, int16_t **pcm, size_t *frames) {
  long long from, n, i;
  pthread_mutex_lock(&s_lock);
  from = after < 0 ? s_written - 4410 : after;
  if (from < s_written - RING + 4410)
    from = s_written - RING + 4410; /* the reader fell behind */
  if (from < 0)
    from = 0;
  n = s_written - from;
  if (n <= 0) {
    pthread_mutex_unlock(&s_lock);
    return -1;
  }
  *pcm = malloc((size_t)n * 4);
  for (i = 0; i < n; i++) {
    int k = (int)((from + i) % RING);
    (*pcm)[i * 2] = s_ring[k * 2];
    (*pcm)[i * 2 + 1] = s_ring[k * 2 + 1];
  }
  *frames = (size_t)n;
  pthread_mutex_unlock(&s_lock);
  return from + n;
}

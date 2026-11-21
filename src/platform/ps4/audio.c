/* Audio out on the PS4 (docs/ps4-plan.md P3, D193): the SPU mix
 * (psx_spu_vblank on the game thread) pushes 44.1 kHz s16 stereo through
 * psx_audio_out into a ring; a thread of ours takes it at 48 kHz, the only
 * rate sceAudioOut's main port plays, by linear interpolation, and hands
 * sceAudioOut 256-frame grains (it blocks until the previous one is played,
 * as OpenOrbis's audio-wav sample uses it). When the ring runs dry the grain
 * is padded with silence; when it holds more than LAG_MAX the oldest audio
 * goes, so the sound never trails the picture by much. */
#include "ps4.h"
#include "host.h"
#include "psx_spu.h"

#include <pthread.h>
#include <string.h>

#include <orbis/AudioOut.h>

#define IN_RATE 44100
#define OUT_RATE 48000
#define GRAIN 256
#define RING 16384           /* frames, a power of two: 370 ms at 44.1 kHz */
#define LAG_MAX (IN_RATE / 8) /* 125 ms queued at most */

static int16_t s_ring[RING * 2];
static volatile unsigned s_head; /* frames written (the game thread) */
static volatile unsigned s_tail; /* frames read (the audio thread) */
static int s_port = -1;
static unsigned s_dry, s_dropped, s_grains;

void psx_audio_out(const short *pcm, int frames) {
  unsigned h = s_head;
  int i;
  if (s_port < 0)
    return;
  for (i = 0; i < frames; i++, h++) {
    s_ring[(h & (RING - 1)) * 2] = pcm[i * 2];
    s_ring[(h & (RING - 1)) * 2 + 1] = pcm[i * 2 + 1];
  }
  __atomic_store_n(&s_head, h, __ATOMIC_RELEASE);
}

static void *audio_thread(void *arg) {
  static int16_t out[GRAIN * 2];
  unsigned pos_frac = 0; /* 16.16 position between s_tail and s_tail + 1 */
  const unsigned step = (unsigned)(((unsigned long long)IN_RATE << 16) / OUT_RATE);
  (void)arg;
  for (;;) {
    unsigned head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE), tail = s_tail;
    int i;
    if (head - tail > LAG_MAX) { /* too far behind: drop the oldest */
      s_dropped += head - tail - LAG_MAX / 2;
      tail = head - LAG_MAX / 2;
    }
    for (i = 0; i < GRAIN; i++) {
      if (head - tail < 2) { /* dry: silence for the rest of the grain */
        memset(out + i * 2, 0, (size_t)(GRAIN - i) * 4);
        s_dry++;
        break;
      }
      {
        const int16_t *a = s_ring + (tail & (RING - 1)) * 2;
        const int16_t *b = s_ring + ((tail + 1) & (RING - 1)) * 2;
        int f = (int)(pos_frac >> 1); /* 15 bits */
        out[i * 2] = (int16_t)(a[0] + (((b[0] - a[0]) * f) >> 15));
        out[i * 2 + 1] = (int16_t)(a[1] + (((b[1] - a[1]) * f) >> 15));
      }
      pos_frac += step;
      tail += pos_frac >> 16;
      pos_frac &= 0xFFFF;
    }
    __atomic_store_n(&s_tail, tail, __ATOMIC_RELEASE);
    sceAudioOutOutput(s_port, out); /* blocks until the last grain is played */
    s_grains++;
  }
  return NULL;
}

int ps4_audio_init(void) {
  pthread_t th;
  int rc = sceAudioOutInit();
  if (rc < 0 && rc != (int)0x8026000Fu) { /* ...0F: already initialised */
    host_log("audio: sceAudioOutInit failed: 0x%x", (unsigned)rc);
    return -1;
  }
  s_port = sceAudioOutOpen(g_ps4_user, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, GRAIN, OUT_RATE,
                           ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
  if (s_port < 0) {
    host_log("audio: sceAudioOutOpen failed: 0x%x", (unsigned)s_port);
    return -1;
  }
  if ((rc = pthread_create(&th, NULL, audio_thread, NULL)) != 0) {
    host_log("audio: no thread (%d)", rc);
    s_port = -1;
    return -1;
  }
  host_log("audio: sceAudioOut port %d, %d Hz s16 stereo from the SPU's %d Hz, %d-frame grains",
           s_port, OUT_RATE, IN_RATE, GRAIN);
  return 0;
}

void ps4_audio_report(int vblank) {
  host_log("audio %d: %u grains, %u dry, %u frames dropped, %u queued", vblank, s_grains, s_dry,
           s_dropped, s_head - s_tail);
}

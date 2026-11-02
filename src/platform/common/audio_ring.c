/* The output ring's level control (psx_audio_ring.h, decisions D88). */
#include "psx_audio_ring.h"

#include <string.h>

#define MASK (PSX_AUDIO_RING_FRAMES - 1)
#define ONE 65536u
/* The controller, once a window: e is the low point's distance from
 * PSX_AUDIO_RING_LOW in LOWs, the rate 1 + KP * e + drift, and drift
 * += KI * e for each frame of the window. The low point then answers a
 * step in the clocks' ratio critically damped, in ~10 s, and the integral
 * holds it at LOW, so drift ends as the clocks' ratio - 1 (~-1000 ppm on
 * the Vita: 59.94 Hz x 735 frames against 44,100). */
#define KP 0.005f
#define KI 6e-9f
#define MAX_RATE 0.005f  /* 8.6 cents: a pitch nobody hears move */
#define MAX_DRIFT 0.003f

void psx_audio_ring_init(PsxAudioRing *r) {
  memset(r, 0, sizeof *r);
  r->cut = PSX_AUDIO_RING_NO_CUT;
  r->step = ONE;
  r->priming = 1;
  r->low = PSX_AUDIO_RING_PRIME;
  r->level_min = ~0u;
}

void psx_audio_ring_push(PsxAudioRing *r, const short *pcm, int frames) {
  unsigned wr = r->wr, rd = __atomic_load_n(&r->rd, __ATOMIC_ACQUIRE), i;
  if (wr - rd + (unsigned)frames > PSX_AUDIO_RING_FRAMES)
    return; /* the consumer stopped: nothing it would play in time */
  for (i = 0; i < (unsigned)frames; i++, wr++) {
    r->buf[wr & MASK][0] = pcm[i * 2];
    r->buf[wr & MASK][1] = pcm[i * 2 + 1];
  }
  __atomic_store_n(&r->wr, wr, __ATOMIC_RELEASE);
}

void psx_audio_ring_clear(PsxAudioRing *r) {
  __atomic_store_n(&r->cut, __atomic_load_n(&r->wr, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
}

int psx_audio_ring_ppm(const PsxAudioRing *r) {
  return (int)(r->drift * 1e6f + (r->drift < 0 ? -0.5f : 0.5f));
}

/* Back to PRIME frames: drop the oldest, start a new window. */
static void trim(PsxAudioRing *r, unsigned *rd, unsigned wr) {
  if (wr - *rd > PSX_AUDIO_RING_PRIME) {
    r->trimmed += wr - *rd - PSX_AUDIO_RING_PRIME;
    r->trims++;
    *rd = wr - PSX_AUDIO_RING_PRIME;
  }
  r->frac = 0;
  r->low = (float)(wr - *rd);
  r->played = 0;
}

static void control(PsxAudioRing *r) {
  float e = (r->low - PSX_AUDIO_RING_LOW) / PSX_AUDIO_RING_LOW, rate;
  if (e > 1)
    e = 1; /* well above LOW: no hurry */
  r->drift += KI * e * (float)r->played;
  r->drift = r->drift > MAX_DRIFT ? MAX_DRIFT : r->drift < -MAX_DRIFT ? -MAX_DRIFT : r->drift;
  rate = KP * e + r->drift;
  r->rate = rate > MAX_RATE ? MAX_RATE : rate < -MAX_RATE ? -MAX_RATE : rate;
  r->step = (unsigned)((float)ONE * (1.0f + r->rate) + 0.5f);
  r->played = 0;
}

void psx_audio_ring_pull(PsxAudioRing *r, short (*out)[2], int frames) {
  unsigned cut = __atomic_exchange_n(&r->cut, PSX_AUDIO_RING_NO_CUT, __ATOMIC_ACQ_REL);
  unsigned wr = __atomic_load_n(&r->wr, __ATOMIC_ACQUIRE), rd = r->rd, level, frac, step;
  int i = 0;
  if (cut != PSX_AUDIO_RING_NO_CUT && cut - rd <= wr - rd) {
    rd = cut;
    r->frac = 0;
    r->priming = 1;
  }
  level = wr - rd;
  r->grains++;
  r->level_sum += level;
  if (level < r->level_min)
    r->level_min = level;
  if (r->priming) {
    if (level < PSX_AUDIO_RING_PRIME) {
      memset(out, 0, (size_t)frames * sizeof out[0]);
      r->silent++;
      __atomic_store_n(&r->rd, rd, __ATOMIC_RELEASE);
      return;
    }
    r->priming = 0;
    trim(r, &rd, wr);
  } else if (level > PSX_AUDIO_RING_MAX) {
    trim(r, &rd, wr);
  }
  level = wr - rd;
  r->low += PSX_AUDIO_RING_RISE;
  if ((float)level < r->low)
    r->low = (float)level;
  /* linear between rd and rd + 1; at a step of exactly 1 from frac 0, the
   * frames as they were pushed */
  frac = r->frac;
  step = r->step;
  for (; i < frames; i++) {
    const short *a, *b;
    if (wr - rd < 2) {
      r->underruns++;
      r->silent++;
      r->priming = 1;
      memset(out + i, 0, (size_t)(frames - i) * sizeof out[0]);
      break;
    }
    a = r->buf[rd & MASK];
    b = r->buf[(rd + 1) & MASK];
    out[i][0] = (short)(a[0] + (int)(((long long)(b[0] - a[0]) * frac) >> 16));
    out[i][1] = (short)(a[1] + (int)(((long long)(b[1] - a[1]) * frac) >> 16));
    frac += step;
    rd += frac >> 16;
    frac &= ONE - 1;
  }
  r->frac = frac;
  __atomic_store_n(&r->rd, rd, __ATOMIC_RELEASE);
  r->played += (unsigned)frames;
  if (r->played >= PSX_AUDIO_RING_WINDOW && !r->priming)
    control(r);
}

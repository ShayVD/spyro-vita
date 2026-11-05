/* The output ring (vpk_audio_ring.h).
 * From spyro-vita src/platform/common/audio_ring.c @ 987363e (the held level, decisions D88) and kh1-decomp
 * host/vita/audio.c @ 85befa4a (the plain ring). */
#include "vpk_audio_ring.h"

#include <string.h>

#define ONE 65536u
/* The held level's controller, once a window: e is the low point's distance from cfg.low in lows, the rate
 * 1 + KP * e + drift, and drift += KI * e for each frame of the window.  The low point then answers a step in the
 * clocks' ratio critically damped, in ~10 s, and the integral holds it at low, so drift ends as the clocks' ratio
 * - 1 (~-1000 ppm on the Vita: 59.94 Hz x 735 frames against 44,100). */
#define KP 0.005f
#define KI 6e-9f
#define MAX_RATE 0.005f /* 8.6 cents: a pitch nobody hears move */
#define MAX_DRIFT 0.003f

void vpk_audio_ring_init(VpkAudioRing *r, const VpkAudioRingConfig *c) {
  memset(r, 0, sizeof *r);
  r->cfg = *c;
  if (!r->cfg.frames || r->cfg.frames > VPK_AUDIO_RING_MAX_FRAMES || (r->cfg.frames & (r->cfg.frames - 1)))
    r->cfg.frames = VPK_AUDIO_RING_MAX_FRAMES;
  r->cut = VPK_AUDIO_RING_NO_CUT;
  r->step = ONE;
  r->priming = r->cfg.rate_control != 0;
  r->low = (float)r->cfg.prime;
  r->level_min = ~0u;
}

int vpk_audio_ring_push(VpkAudioRing *r, const short *pcm, int frames) {
  unsigned wr = r->wr, rd = __atomic_load_n(&r->rd, __ATOMIC_ACQUIRE), mask = r->cfg.frames - 1, i;
  if (wr - rd + (unsigned)frames > r->cfg.frames)
    return 0; /* the consumer stopped: nothing it would play in time */
  for (i = 0; i < (unsigned)frames; i++, wr++) {
    r->buf[wr & mask][0] = pcm[i * 2];
    r->buf[wr & mask][1] = pcm[i * 2 + 1];
  }
  __atomic_store_n(&r->wr, wr, __ATOMIC_RELEASE);
  return frames;
}

unsigned vpk_audio_ring_level(const VpkAudioRing *r) {
  return r->wr - __atomic_load_n(&r->rd, __ATOMIC_ACQUIRE);
}

void vpk_audio_ring_clear(VpkAudioRing *r) {
  __atomic_store_n(&r->cut, __atomic_load_n(&r->wr, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
}

int vpk_audio_ring_ppm(const VpkAudioRing *r) {
  return (int)(r->drift * 1e6f + (r->drift < 0 ? -0.5f : 0.5f));
}

/* Back to prime frames: drop the oldest, start a new window. */
static void trim(VpkAudioRing *r, unsigned *rd, unsigned wr) {
  if (wr - *rd > r->cfg.prime) {
    r->trimmed += wr - *rd - r->cfg.prime;
    r->trims++;
    *rd = wr - r->cfg.prime;
  }
  r->frac = 0;
  r->low = (float)(wr - *rd);
  r->played = 0;
}

static void control(VpkAudioRing *r) {
  float target = (float)r->cfg.low, e = (r->low - target) / target, rate;
  if (e > 1)
    e = 1; /* well above low: no hurry */
  r->drift += KI * e * (float)r->played;
  r->drift = r->drift > MAX_DRIFT ? MAX_DRIFT : r->drift < -MAX_DRIFT ? -MAX_DRIFT : r->drift;
  rate = KP * e + r->drift;
  r->rate = rate > MAX_RATE ? MAX_RATE : rate < -MAX_RATE ? -MAX_RATE : rate;
  r->step = (unsigned)((float)ONE * (1.0f + r->rate) + 0.5f);
  r->played = 0;
}

/* plain: what is there, up to frames, then silence */
static int pull_plain(VpkAudioRing *r, short (*out)[2], int frames, unsigned rd, unsigned wr) {
  unsigned level = wr - rd, n = level < (unsigned)frames ? level : (unsigned)frames, mask = r->cfg.frames - 1, i;
  for (i = 0; i < n; i++) {
    out[i][0] = r->buf[(rd + i) & mask][0];
    out[i][1] = r->buf[(rd + i) & mask][1];
  }
  if (n < (unsigned)frames) {
    memset(out + n, 0, (size_t)(frames - (int)n) * sizeof out[0]);
    r->underruns++;
    r->silent++;
  }
  __atomic_store_n(&r->rd, rd + n, __ATOMIC_RELEASE);
  return (int)n;
}

int vpk_audio_ring_pull(VpkAudioRing *r, short (*out)[2], int frames) {
  unsigned cut = __atomic_exchange_n(&r->cut, VPK_AUDIO_RING_NO_CUT, __ATOMIC_ACQ_REL);
  unsigned wr = __atomic_load_n(&r->wr, __ATOMIC_ACQUIRE), rd = r->rd, level, frac, step, mask = r->cfg.frames - 1;
  int i = 0;
  if (cut != VPK_AUDIO_RING_NO_CUT && cut - rd <= wr - rd) {
    rd = cut;
    r->frac = 0;
    r->priming = r->cfg.rate_control != 0;
  }
  level = wr - rd;
  r->last_level = level;
  r->grains++;
  r->level_sum += level;
  if (level < r->level_min)
    r->level_min = level;
  if (!r->cfg.rate_control)
    return pull_plain(r, out, frames, rd, wr);
  if (r->priming) {
    if (level < r->cfg.prime) {
      memset(out, 0, (size_t)frames * sizeof out[0]);
      r->silent++;
      __atomic_store_n(&r->rd, rd, __ATOMIC_RELEASE);
      return 0;
    }
    r->priming = 0;
    trim(r, &rd, wr);
  } else if (level > r->cfg.max) {
    trim(r, &rd, wr);
  }
  level = wr - rd;
  r->low += r->cfg.rise;
  if ((float)level < r->low)
    r->low = (float)level;
  /* linear between rd and rd + 1; at a step of exactly 1 from frac 0, the frames as they were pushed */
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
    a = r->buf[rd & mask];
    b = r->buf[(rd + 1) & mask];
    out[i][0] = (short)(a[0] + (int)(((long long)(b[0] - a[0]) * frac) >> 16));
    out[i][1] = (short)(a[1] + (int)(((long long)(b[1] - a[1]) * frac) >> 16));
    frac += step;
    rd += frac >> 16;
    frac &= ONE - 1;
  }
  r->frac = frac;
  __atomic_store_n(&r->rd, rd, __ATOMIC_RELEASE);
  r->played += (unsigned)frames;
  if (r->played >= r->cfg.window && !r->priming)
    control(r);
  return i;
}

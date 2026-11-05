/* The output ring (psx_audio_ring.h: vita-port-kit's vpk_audio_ring, D88, D107) against a model of
 * the Vita's clocks: the game posts two vblanks of 735 frames every two
 * display vblanks (30 Hz logic, vblank.c), the display runs at 59.94 Hz,
 * the port takes 512-frame grains at 44,100 Hz. A load stops the game; it
 * then runs up to 8 missed vblanks at once and drops the rest (vblank.c's
 * MAX_CATCH_UP). The same model with D84's ring (pass frames on, pad a short
 * grain) shows why it ran dry; the D88 ring must not, outside the loads.
 *
 * test_audio_ring [minutes] */
#include "psx_audio_ring.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VBLANK_FRAMES 735
#define GRAIN 512
#define RATE 44100.0
#define CATCH_UP 8

static int s_fail;
#define CHECK(c, ...)                                                                               \
  do {                                                                                              \
    if (!(c)) {                                                                                     \
      printf("FAIL: " __VA_ARGS__);                                                                 \
      printf("\n");                                                                                 \
      s_fail = 1;                                                                                   \
    }                                                                                               \
  } while (0)

/* D84's ring (audio.c before D88): the consumer takes what there is, up to
 * a grain, and pads the rest with silence. */
typedef struct {
  unsigned wr, rd, short_grains, dry;
} OldRing;

typedef struct {
  double hz;          /* the display's vblank rate */
  double minutes;
  int stalls;         /* loads: one each minute from the second, 0.7 s */
  unsigned seed;
  /* results */
  unsigned underruns, trims, silent, settled_underruns, settled_trims, old_short, old_dry;
  unsigned level_lo, level_hi, level_mean; /* after settling, outside the loads */
  unsigned long long level_sum, level_n;
  int ppm;
  int ramp_bad;
} Run;

static unsigned rnd(unsigned *s) {
  *s = *s * 1103515245u + 12345u;
  return *s >> 16;
}

static void run(Run *t) {
  static PsxAudioRing r;
  static short pcm[CATCH_UP * VBLANK_FRAMES * 2];
  static short out[GRAIN][2];
  OldRing old = {0, 0, 0, 0};
  const double vblank_s = 1.0 / t->hz, grain_s = GRAIN / RATE, end = t->minutes * 60.0;
  double next_vblank = 0, next_grain = grain_s * 0.37;
  unsigned produced = 0; /* frames pushed: the ramp's value */
  unsigned pending = 0, late = 0, seed = t->seed;
  int last_out = -1, last_r = 0, i;
  psx_audio_ring_init(&r);
  t->level_lo = ~0u;
  t->level_hi = 0;
  t->level_sum = t->level_n = 0;
  t->settled_underruns = t->settled_trims = 0;
  t->ramp_bad = 0;
  while (next_vblank < end || next_grain < end) {
    if (next_vblank <= next_grain) {
      double now = next_vblank;
      int in_load = t->stalls && now > 60.0 && fmod(now, 60.0) < 0.7;
      next_vblank += vblank_s;
      pending++;
      if (in_load)
        continue;
      /* the game posts at the end of each logic frame (every 2 vblanks),
       * or catches up after a load; one logic frame in 8 is a vblank late,
       * one in 64 two (Vita3K showed those, D88) */
      if (pending == 2) {
        unsigned x = rnd(&seed) % 64;
        late = x == 0 ? 2 : x < 8 ? 1 : 0;
      }
      if (pending >= 2 + late) {
        unsigned n = pending > CATCH_UP ? CATCH_UP : pending, f;
        pending = late = 0;
        for (f = 0; f < n * VBLANK_FRAMES; f++, produced++) {
          pcm[f * 2] = (short)(1 + produced % 30000);
          pcm[f * 2 + 1] = (short)(1 + (produced + 15000) % 30000);
        }
        psx_audio_ring_push(&r, pcm, (int)(n * VBLANK_FRAMES));
        if (old.wr == old.rd)
          old.dry++;
        old.wr += n * VBLANK_FRAMES;
      }
    } else {
      double now = next_grain;
      int in_load = t->stalls && now > 60.0 && fmod(now, 60.0) < 1.0;
      unsigned before = r.underruns, trims = r.trims, lvl = r.wr - r.rd;
      next_grain += grain_s;
      if (now > 30.0 && !in_load) {
        if (lvl < t->level_lo)
          t->level_lo = lvl;
        if (lvl > t->level_hi)
          t->level_hi = lvl;
        t->level_sum += lvl;
        t->level_n++;
      }
      psx_audio_ring_pull(&r, out, GRAIN);
      if (now > 30.0 && !in_load && r.underruns != before)
        t->settled_underruns++;
      if (now > 30.0 && !in_load && r.trims != trims)
        t->settled_trims++;
      if (r.trims != trims)
        last_out = -1; /* dropped back to the target: a jump ahead */
      /* what was played moves along the two ramps (half a turn apart):
       * each frame 0 to 2 ahead of the last (a step of at most 1.005) on
       * both, or on one while the other wraps; or silence */
      for (i = 0; i < GRAIN; i++) {
        int v = out[i][0], w = out[i][1];
        if (v == 0 && w == 0) {
          last_out = -1;
          continue;
        }
        if (last_out >= 0) {
          int dv = v - last_out, dw = w - last_r;
          int ok_v = dv >= 0 && dv <= 2, ok_w = dw >= 0 && dw <= 2;
          if (!(ok_v && ok_w) && !(ok_v && dw < 0) && !(ok_w && dv < 0))
            t->ramp_bad++;
        }
        last_out = v;
        last_r = w;
      }
      /* D84's ring */
      {
        unsigned have = old.wr - old.rd;
        if (have < GRAIN) {
          old.short_grains++;
          old.rd = old.wr;
        } else {
          old.rd += GRAIN;
        }
      }
    }
  }
  t->underruns = r.underruns;
  t->trims = r.trims;
  t->silent = r.silent;
  t->ppm = psx_audio_ring_ppm(&r);
  t->level_mean = t->level_n ? (unsigned)(t->level_sum / t->level_n) : 0;
  t->old_short = old.short_grains;
  t->old_dry = old.dry;
}

static void report(const char *name, const Run *t) {
  printf("%-28s %4.0f min: D84 ring %5u short grains (%u dry) | D88 ring %u underruns (%u after "
         "30 s outside loads), %u trims (%u), %u silent grains, level %u..%u (mean %u), clocks %+d ppm, ramp "
         "errors %d\n",
         name, t->minutes, t->old_short, t->old_dry, t->underruns, t->settled_underruns, t->trims, t->settled_trims,
         t->silent, t->level_lo, t->level_hi, t->level_mean, t->ppm, t->ramp_bad);
}

int main(int argc, char **argv) {
  double minutes = argc > 1 ? atof(argv[1]) : 10;
  Run vita = {.hz = 60000.0 / 1001, .minutes = minutes, .seed = 1};
  Run same = {.hz = RATE / VBLANK_FRAMES, .minutes = minutes, .seed = 2};
  Run fast = {.hz = 60.1, .minutes = minutes, .seed = 3};
  Run loads = {.hz = 60000.0 / 1001, .minutes = minutes, .stalls = 1, .seed = 4};
  const int expect_ppm = (int)((60000.0 / 1001 * VBLANK_FRAMES / RATE - 1) * 1e6);

  run(&vita);
  report("59.94 Hz (the Vita)", &vita);
  run(&same);
  report("60.00 Hz (clocks agree)", &same);
  run(&fast);
  report("60.10 Hz (game ahead)", &fast);
  run(&loads);
  report("59.94 Hz, a load a minute", &loads);

  /* the cause: D84's ring runs short again and again at the Vita's clocks */
  CHECK(vita.old_short > 100, "the model doesn't show D84's ring running dry (%u)", vita.old_short);
  /* the fix */
  CHECK(vita.underruns == 0, "59.94 Hz: %u underruns", vita.underruns);
  CHECK(same.underruns == 0, "60 Hz: %u underruns", same.underruns);
  CHECK(fast.underruns == 0, "60.1 Hz: %u underruns", fast.underruns);
  CHECK(!vita.settled_trims && !same.settled_trims && !fast.settled_trims && !loads.settled_trims,
        "trims outside the start and the loads");
  CHECK(abs(vita.ppm - expect_ppm) < 300, "59.94 Hz: clocks %+d ppm, expected %+d", vita.ppm,
        expect_ppm);
  CHECK(abs(same.ppm) < 300, "60 Hz: clocks %+d ppm, expected 0", same.ppm);
  CHECK(vita.level_lo > GRAIN && vita.level_hi < PSX_AUDIO_RING_MAX, "59.94 Hz: level %u..%u",
        vita.level_lo, vita.level_hi);
  /* a load runs dry once (the game stopped), then the ring is back */
  CHECK(loads.settled_underruns == 0, "loads: %u underruns outside the loads",
        loads.settled_underruns);
  CHECK(loads.underruns <= (unsigned)minutes, "loads: %u underruns for %d loads", loads.underruns,
        (int)minutes - 1);
  CHECK(!vita.ramp_bad && !same.ramp_bad && !fast.ramp_bad && !loads.ramp_bad,
        "frames played out of order or skipped");
  printf("%s\n", s_fail ? "FAILED" : "ok");
  return s_fail;
}

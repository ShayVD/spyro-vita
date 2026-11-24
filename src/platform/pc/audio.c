/* Audio out on PC (docs/pc-build.md): the SPU mix (psx_spu_vblank on the
 * game thread) pushes 44.1 kHz s16 stereo through psx_audio_out into
 * Encore's audio (enc_audio_push, D247; an SDL3 audio stream underneath,
 * which converts it to the device's format and rate and plays it on SDL's
 * own thread). When more than LAG_MAX is queued (the game caught up after a
 * stall: up to 8 vblanks' audio at once) new audio is dropped until the
 * queue is down to LAG_KEEP, so the sound never trails the picture by much;
 * when it runs dry the device plays silence. Before D247 the oldest was
 * dropped at once; the interface has no call that drops queued audio, so
 * the newest goes instead and the lag comes down over LAG_MAX - LAG_KEEP of
 * playing (gap G9). */
#include "pc.h"
#include "host.h"
#include "psx_spu.h"

#include "encore/platform.h"

#define IN_RATE 44100
#define FRAMES_MS (IN_RATE / 1000.0)
#define LAG_MAX (int)(250 * FRAMES_MS)  /* frames queued at most: 250 ms */
#define LAG_KEEP (int)(80 * FRAMES_MS)  /* what a trim leaves */
#define QUEUE_MS 250                    /* the backend's queue: twice this */

static int s_open, s_trimming;
static unsigned long long s_frames_in, s_trims, s_trimmed;

void psx_audio_out(const short *pcm, int frames) {
  int q;
  if (!s_open)
    return;
  q = enc_audio_queued();
  if (!s_trimming && q > LAG_MAX) {
    s_trimming = 1;
    s_trims++;
  }
  if (s_trimming) {
    if (q > LAG_KEEP) {
      s_trimmed += (unsigned)frames;
      return;
    }
    s_trimming = 0;
  }
  enc_audio_push(pcm, frames);
  s_frames_in += (unsigned)frames;
}

int pc_audio_init(void) {
  const EncAudioConfig cfg = {IN_RATE, 2, QUEUE_MS};
  int rc = enc_audio_open(&cfg);
  if (rc != ENC_OK) {
    host_log("audio: enc_audio_open: %s", enc_error_name(rc));
    return -1;
  }
  s_open = 1;
  host_log("audio: %d Hz s16 stereo in (Encore's PC backend converts to the device's)", IN_RATE);
  return 0;
}

void pc_audio_close(void) {
  s_open = 0;
  enc_audio_close();
}

void pc_audio_report(int vblank) {
  if (!s_open)
    return;
  host_log("audio %d: %llu frames in, %d bytes queued, %llu trims (%llu frames dropped)", vblank,
           s_frames_in, enc_audio_queued() * 4, s_trims, s_trimmed);
}

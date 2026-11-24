/* Audio out on PC (docs/pc-build.md): the SPU mix (psx_spu_vblank on the
 * game thread) pushes 44.1 kHz s16 stereo through psx_audio_out into an
 * SDL3 audio stream, which converts it to the device's format and rate and
 * plays it on SDL's own thread. When more than LAG_MAX is queued (the game
 * caught up after a stall: up to 8 vblanks' audio at once) the oldest is
 * dropped down to LAG_KEEP, so the sound never trails the picture by much;
 * when it runs dry SDL plays silence. */
#include "pc.h"
#include "host.h"
#include "psx_spu.h"

#include <SDL3/SDL.h>

#define IN_RATE 44100
#define BYTES_MS (IN_RATE * 4 / 1000)
#define LAG_MAX (250 * BYTES_MS)  /* bytes queued at most: 250 ms */
#define LAG_KEEP (80 * BYTES_MS)  /* what a trim leaves */

static SDL_AudioStream *s_stream;
static unsigned long long s_frames_in, s_trims, s_trimmed;
static char s_drop[LAG_MAX];

void psx_audio_out(const short *pcm, int frames) {
  if (!s_stream)
    return;
  {
    int q = SDL_GetAudioStreamQueued(s_stream);
    if (q > LAG_MAX) { /* the oldest goes: whole frames */
      int n = (q - LAG_KEEP) & ~3;
      if (n > (int)sizeof s_drop)
        n = sizeof s_drop;
      n = SDL_GetAudioStreamData(s_stream, s_drop, n);
      s_trims++;
      s_trimmed += n > 0 ? (unsigned)n / 4 : 0;
    }
  }
  SDL_PutAudioStreamData(s_stream, pcm, frames * 4);
  s_frames_in += (unsigned)frames;
}

int pc_audio_init(void) {
  const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, IN_RATE};
  s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
  if (!s_stream) {
    host_log("audio: SDL_OpenAudioDeviceStream: %s", SDL_GetError());
    return -1;
  }
  SDL_ResumeAudioStreamDevice(s_stream);
  host_log("audio: %s, %d Hz s16 stereo in (SDL converts to the device's)",
           SDL_GetCurrentAudioDriver(), IN_RATE);
  return 0;
}

void pc_audio_close(void) {
  if (s_stream)
    SDL_DestroyAudioStream(s_stream);
  s_stream = NULL;
}

void pc_audio_report(int vblank) {
  if (!s_stream)
    return;
  host_log("audio %d: %llu frames in, %d bytes queued, %llu trims (%llu frames dropped)", vblank,
           s_frames_in, SDL_GetAudioStreamQueued(s_stream), s_trims, s_trimmed);
}

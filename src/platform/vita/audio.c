/* Vita audio: the software SPU mixer (src/platform/common/libspu.c) pushes
 * 44.1 kHz s16 stereo once per vblank (psx_spu_vblank -> psx_audio_out); it
 * is queued to an SDL2 audio device. If the game runs slower than real time
 * the queue runs dry (gaps); if it gets ahead, the oldest audio is dropped
 * past ~150 ms so latency stays bounded. */
#include "vita.h"
#include "psx_spu.h"

#include <SDL2/SDL.h>

#define RATE 44100
#define MAX_QUEUED (RATE * 4 * 15 / 100) /* bytes: ~150 ms */

static SDL_AudioDeviceID s_dev;

void vita_audio_init(void) {
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = RATE;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!s_dev) {
    vita_log("audio: SDL_OpenAudioDevice failed: %s", SDL_GetError());
    return;
  }
  vita_log("audio: %d Hz, %d channels, %d samples", have.freq, have.channels, have.samples);
  SDL_PauseAudioDevice(s_dev, 0);
}

void psx_audio_out(const short *pcm, int frames) {
  if (!s_dev)
    return;
  if (SDL_GetQueuedAudioSize(s_dev) > MAX_QUEUED)
    SDL_ClearQueuedAudio(s_dev);
  SDL_QueueAudio(s_dev, pcm, (Uint32)frames * 4);
}

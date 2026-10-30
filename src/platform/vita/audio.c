/* Vita audio: the software SPU mixer (src/platform/common/libspu.c, run on
 * the mixer thread by mixer.c) pushes 44.1 kHz s16 stereo once per vblank
 * (psx_spu_mix -> psx_audio_out, on that thread); it is queued to an SDL2 audio device. If the game runs slower than real time
 * the queue runs dry (gaps); if it gets ahead, the oldest audio is dropped
 * past ~150 ms so latency stays bounded. With wav=1 the same samples also go
 * to ux0:data/spyro/audio.wav, in the linux32 host's --wav format, so the two
 * can be compared byte for byte. */
#include "vita.h"
#include "psx_spu.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define RATE 44100
#define MAX_QUEUED (RATE * 4 * 15 / 100) /* bytes: ~150 ms */

static SDL_AudioDeviceID s_dev;
static FILE *s_wav;
static unsigned s_wav_frames;
unsigned g_vita_audio_dry, g_vita_audio_cleared; /* queue ran dry / was cut (mixer line) */

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
  put_le(h + 20, 1, 2); /* PCM */
  put_le(h + 22, 2, 2); /* stereo */
  put_le(h + 24, RATE, 4);
  put_le(h + 28, RATE * 4, 4);
  put_le(h + 32, 4, 2);
  put_le(h + 34, 16, 2);
  memcpy(h + 36, "data", 4);
  put_le(h + 40, frames * 4, 4);
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, 44, f);
  fseek(f, 0, SEEK_END);
}

void vita_audio_close(void) {
  vita_mixer_drain();
  if (s_wav) {
    wav_header(s_wav, s_wav_frames);
    fclose(s_wav);
    s_wav = NULL;
    vita_log("audio: %u frames in audio.wav", s_wav_frames);
  }
}

void vita_audio_init(void) {
  SDL_AudioSpec want, have;
  vita_mixer_init();
  if (g_vita_opts.wav) {
    s_wav = fopen(VITA_DATA_DIR "/audio.wav", "wb");
    if (s_wav)
      wav_header(s_wav, 0);
    else
      vita_log("audio: cannot write audio.wav");
  }
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
  if (s_wav) {
    fwrite(pcm, 4, (size_t)frames, s_wav);
    s_wav_frames += (unsigned)frames;
  }
  if (!s_dev)
    return;
  {
    Uint32 queued = SDL_GetQueuedAudioSize(s_dev);
    if (queued > MAX_QUEUED) {
      SDL_ClearQueuedAudio(s_dev);
      g_vita_audio_cleared++;
    } else if (queued == 0) {
      g_vita_audio_dry++;
    }
  }
  SDL_QueueAudio(s_dev, pcm, (Uint32)frames * 4);
}

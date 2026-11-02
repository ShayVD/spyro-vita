/* Vita audio: the software SPU mixer (src/platform/common/libspu.c, run on
 * the mixer thread by mixer.c) pushes 44.1 kHz s16 stereo once per vblank
 * (psx_spu_mix -> psx_audio_out, on that thread); it is queued to an SDL2 audio device. If the game runs slower than real time
 * the queue runs dry (gaps); if it gets ahead, the oldest audio is dropped
 * past ~150 ms so latency stays bounded. With wav=1 the same samples also go
 * to ux0:data/spyro/audio.wav, in the linux32 host's --wav format, so the two
 * can be compared byte for byte.
 *
 * With the native mix (mixer=3 or 4, D84) the queue is our own ring instead,
 * and a thread on core 1 feeds sceAudioOut's BGM port from it, GRAIN frames
 * at a time (sceAudioOutOutput returns when the port wants the next ones).
 * The same rules: dry when the mixer finds it empty, cut back to nothing
 * when it holds more than ~150 ms. */
#include "vita.h"
#include "psx_spu.h"

#include <SDL2/SDL.h>
#include <psp2/audioout.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <string.h>

#define RATE 44100
#define MAX_QUEUED (RATE * 4 * 15 / 100) /* bytes: ~150 ms */

/* mixer=3: the ring (frames, free-running counts) and sceAudioOut */
#define GRAIN 512                 /* frames per sceAudioOutOutput: 11.6 ms */
#define RING_FRAMES 16384         /* a power of two, > MAX_QUEUED / 4 + a vblank */
#define OUT_PRIORITY 0x68         /* ahead of the mixer (0x70): it only copies and waits */
#define NO_CUT 0xFFFFFFFFu
static short s_ring[RING_FRAMES][2];
static volatile unsigned s_wr, s_rd; /* the mixer thread's, the output thread's */
static volatile unsigned s_cut = NO_CUT; /* the mixer's: drop what's before this */
static int s_port = -1;
static SceUID s_out_thread;
unsigned g_vita_audio_short; /* mixer=3: grains the output thread padded with silence */

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

static int out_main(SceSize args, void *argp) {
  static short buf[GRAIN][2];
  (void)args;
  (void)argp;
  for (;;) {
    unsigned cut = __atomic_exchange_n(&s_cut, NO_CUT, __ATOMIC_ACQ_REL);
    unsigned rd = s_rd, wr = __atomic_load_n(&s_wr, __ATOMIC_ACQUIRE), n, i;
    if (cut != NO_CUT && wr - cut <= wr - rd)
      rd = cut;
    n = wr - rd < GRAIN ? wr - rd : GRAIN;
    for (i = 0; i < n; i++, rd++) {
      buf[i][0] = s_ring[rd % RING_FRAMES][0];
      buf[i][1] = s_ring[rd % RING_FRAMES][1];
    }
    if (n < GRAIN) {
      memset(buf[n], 0, (GRAIN - n) * sizeof buf[0]);
      g_vita_audio_short++;
    }
    __atomic_store_n(&s_rd, rd, __ATOMIC_RELEASE);
    sceAudioOutOutput(s_port, buf);
  }
  return 0;
}

static void out_init(void) {
  int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
  SceUID th;
  s_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, RATE, SCE_AUDIO_OUT_MODE_STEREO);
  if (s_port < 0) {
    vita_log("audio: sceAudioOutOpenPort failed: %08x", (unsigned)s_port);
    return;
  }
  sceAudioOutSetVolume(s_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
  th = sceKernelCreateThread("spyro_audio_out", out_main, OUT_PRIORITY, 0x4000, 0,
                             SCE_KERNEL_CPU_MASK_USER_1, NULL);
  if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("audio: no output thread (%08x)", (unsigned)th);
    sceAudioOutReleasePort(s_port);
    s_port = -1;
    return;
  }
  s_out_thread = th;
  vita_log("audio: sceAudioOut BGM port, %d Hz, 2 channels, %d frames a grain; output thread on "
           "core 1, priority 0x%x (mixer=%d, D84)",
           RATE, GRAIN, OUT_PRIORITY, g_vita_opts.mixer);
}

SceUID vita_audio_out_thread(void) { return s_port >= 0 ? s_out_thread : 0; }

static int native_out(void) { return g_vita_opts.mixer >= 3; }

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
  if (native_out()) {
    out_init();
    return;
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

/* A save state was loaded (D51): what is queued is from before it. */
void vita_audio_clear(void) {
  if (s_port >= 0)
    __atomic_store_n(&s_cut, __atomic_load_n(&s_wr, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
  if (s_dev)
    SDL_ClearQueuedAudio(s_dev);
}

/* mixer=3: one vblank into the ring (the mixer thread). */
static void ring_out(const short *pcm, int frames) {
  unsigned wr = s_wr, queued = wr - __atomic_load_n(&s_rd, __ATOMIC_ACQUIRE), i;
  if (queued * 4 > MAX_QUEUED) {
    __atomic_store_n(&s_cut, wr, __ATOMIC_RELEASE); /* SDL_ClearQueuedAudio */
    g_vita_audio_cleared++;
  } else if (queued == 0) {
    g_vita_audio_dry++;
  }
  for (i = 0; i < (unsigned)frames; i++, wr++) {
    s_ring[wr % RING_FRAMES][0] = pcm[i * 2];
    s_ring[wr % RING_FRAMES][1] = pcm[i * 2 + 1];
  }
  __atomic_store_n(&s_wr, wr, __ATOMIC_RELEASE);
}

void psx_audio_out(const short *pcm, int frames) {
  if (s_wav) {
    fwrite(pcm, 4, (size_t)frames, s_wav);
    s_wav_frames += (unsigned)frames;
  }
  if (s_port >= 0) {
    ring_out(pcm, frames);
    return;
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

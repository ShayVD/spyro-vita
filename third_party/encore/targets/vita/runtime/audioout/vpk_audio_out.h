/* The sound device's output thread: an sceAudioOut port fed from a VpkAudioRing (vpk_audio_ring.h), a grain at a
 * time.  sceAudioOutOutput blocks until the port wants the next grain, so the thread is paced by the audio hardware
 * and does almost no work: give it a priority above the mixer's.  Two buffers: the port plays one while the thread
 * fills the other.
 *
 * From kh1-decomp host/vita/audio.c @ 85befa4a (out_thread, the volume) and spyro-vita src/platform/vita/audio.c @
 * 987363e (out_main, out_init). */
#ifndef VPK_AUDIO_OUT_H
#define VPK_AUDIO_OUT_H

#include "vpk_audio_ring.h"

#define VPK_AUDIO_OUT_MAX_GRAIN 2048 /* frames */

typedef struct {
  int port_type;    /* SCE_AUDIO_OUT_PORT_TYPE_MAIN or _BGM */
  int grain;        /* frames a sceAudioOutOutput, up to VPK_AUDIO_OUT_MAX_GRAIN */
  int rate;         /* 48000, 44100 ... */
  int volume;       /* percent of 0 dB at the open, 0-100 */
  const char *thread_name;
  int priority;     /* the output thread's */
  int cpu_mask;     /* SCE_KERNEL_CPU_MASK_USER_* */
  int stack;        /* bytes (0x4000 is plenty) */
  /* after each pull, on the output thread, before the grain goes to the port: got = vpk_audio_ring_pull's
   * return (may be 0) */
  void (*after_pull)(void *user, int got, const VpkAudioRing *r);
  void *user;
} VpkAudioOutConfig;

typedef struct {
  short buf[2][VPK_AUDIO_OUT_MAX_GRAIN][2] __attribute__((aligned(64)));
  VpkAudioRing *ring;
  VpkAudioOutConfig cfg;
  int port;   /* -1 until open */
  int thread; /* the thread's id once started, else 0 */
  int k;      /* the buffer being filled */
} VpkAudioOut;

/* Opens the port (with cfg's volume) for ring: the port number, or the SDK's negative error (o->port stays -1). */
int vpk_audio_out_open(VpkAudioOut *o, VpkAudioRing *ring, const VpkAudioOutConfig *cfg);
/* Starts the thread: its id, or the SDK's negative error.  Needs an open port. */
int vpk_audio_out_start(VpkAudioOut *o);
/* The port's volume, percent of 0 dB (clamped to 0-100); kept for an open to come when the port is not open. */
void vpk_audio_out_volume(VpkAudioOut *o, int percent);
/* Releases the port (a thread must not be running). */
void vpk_audio_out_close(VpkAudioOut *o);
/* One grain: pull, after_pull, output.  The thread runs it forever; tests run it once. */
void vpk_audio_out_step(VpkAudioOut *o);

#endif /* VPK_AUDIO_OUT_H */

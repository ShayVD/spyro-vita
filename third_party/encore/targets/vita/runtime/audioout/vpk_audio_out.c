/* The output thread (vpk_audio_out.h).
 * From kh1-decomp host/vita/audio.c @ 85befa4a and spyro-vita src/platform/vita/audio.c @ 987363e. */
#include "vpk_audio_out.h"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

static void set_volume(int port, int percent) {
  int vol[2];
  vol[0] = vol[1] = SCE_AUDIO_VOLUME_0DB * percent / 100;
  sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
}

int vpk_audio_out_open(VpkAudioOut *o, VpkAudioRing *ring, const VpkAudioOutConfig *cfg) {
  int port;
  o->ring = ring;
  o->cfg = *cfg;
  if (o->cfg.grain > VPK_AUDIO_OUT_MAX_GRAIN)
    o->cfg.grain = VPK_AUDIO_OUT_MAX_GRAIN;
  o->cfg.volume = o->cfg.volume < 0 ? 0 : o->cfg.volume > 100 ? 100 : o->cfg.volume;
  o->port = -1;
  o->thread = 0;
  o->k = 0;
  port = sceAudioOutOpenPort((SceAudioOutPortType)o->cfg.port_type, o->cfg.grain, o->cfg.rate,
                             SCE_AUDIO_OUT_MODE_STEREO);
  if (port < 0)
    return port;
  o->port = port;
  set_volume(port, o->cfg.volume);
  return port;
}

void vpk_audio_out_volume(VpkAudioOut *o, int percent) {
  o->cfg.volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  if (o->ring && o->port >= 0) /* a zeroed VpkAudioOut was never opened */
    set_volume(o->port, o->cfg.volume);
}

void vpk_audio_out_step(VpkAudioOut *o) {
  int got = vpk_audio_ring_pull(o->ring, o->buf[o->k], o->cfg.grain);
  if (o->cfg.after_pull)
    o->cfg.after_pull(o->cfg.user, got, o->ring);
  sceAudioOutOutput(o->port, o->buf[o->k]);
  o->k ^= 1;
}

static int out_main(SceSize args, void *argp) {
  VpkAudioOut *o = *(VpkAudioOut **)argp;
  (void)args;
  for (;;)
    vpk_audio_out_step(o);
  return 0;
}

int vpk_audio_out_start(VpkAudioOut *o) {
  SceUID t;
  int r;
  if (o->port < 0)
    return o->port;
  t = sceKernelCreateThread(o->cfg.thread_name, out_main, o->cfg.priority, (SceSize)o->cfg.stack, 0,
                            o->cfg.cpu_mask, 0);
  if (t < 0)
    return t;
  if ((r = sceKernelStartThread(t, sizeof o, &o)) < 0)
    return r;
  o->thread = t;
  return t;
}

void vpk_audio_out_close(VpkAudioOut *o) {
  if (o->port >= 0)
    sceAudioOutReleasePort(o->port);
  o->port = -1;
}

/* vpk_audio_out against fakes of sceAudioOut and the thread calls: the port opened as asked, the volume, one step
 * pulls a grain from the ring, calls after_pull and hands the grain to the port, alternating the two buffers. */
#include <stdio.h>
#include <string.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include "../vpk_audio_out.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("audio_out_test: FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int open_args[4], vol_set[2], released, outputs, created_prio, created_mask;
static const void *last_buf;
static short first_left;
int sceAudioOutOpenPort(SceAudioOutPortType type, int len, int freq, SceAudioOutMode mode) {
  open_args[0] = type, open_args[1] = len, open_args[2] = freq, open_args[3] = mode;
  return 3;
}
int sceAudioOutReleasePort(int port) { (void)port; released++; return 0; }
int sceAudioOutOutput(int port, const void *buf) {
  (void)port;
  outputs++;
  last_buf = buf;
  first_left = ((const short *)buf)[0];
  return 0;
}
int sceAudioOutSetVolume(int port, SceAudioOutChannelFlag ch, int *vol) {
  (void)port; (void)ch;
  vol_set[0] = vol[0], vol_set[1] = vol[1];
  return 0;
}
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int prio, SceSize stack, unsigned int attr,
                             int cpu_mask, const void *option) {
  (void)name; (void)entry; (void)stack; (void)attr; (void)option;
  created_prio = prio, created_mask = cpu_mask;
  return 77;
}
int sceKernelStartThread(SceUID id, SceSize args, void *argp) { (void)id; (void)args; (void)argp; return 0; }

static int pulls, last_got;
static void after(void *user, int got, const VpkAudioRing *r) {
  (void)r;
  pulls += *(int *)user;
  last_got = got;
}

int main(void) {
  static VpkAudioRing ring;
  static VpkAudioOut o;
  static const VpkAudioRingConfig rc = {8192, 0, 0, 0, 0, 0, 0};
  static short pcm[300 * 2];
  int one = 1, i;
  VpkAudioOutConfig c = {SCE_AUDIO_OUT_PORT_TYPE_MAIN, 256, 48000, 50, "t_out", 0x40, 0x20000, 0x4000, after, &one};
  for (i = 0; i < 300; i++) pcm[i * 2] = (short)(i + 1);
  vpk_audio_out_volume(&o, 30); /* before the open: kept, no port touched */
  CHECK(vol_set[0] == 0 && o.cfg.volume == 30);
  vpk_audio_ring_init(&ring, &rc);
  CHECK(vpk_audio_out_open(&o, &ring, &c) == 3 && o.port == 3);
  CHECK(open_args[0] == 0 && open_args[1] == 256 && open_args[2] == 48000 && open_args[3] == SCE_AUDIO_OUT_MODE_STEREO);
  CHECK(vol_set[0] == 16384 && vol_set[1] == 16384);
  vpk_audio_out_volume(&o, 150);
  CHECK(vol_set[0] == 32768 && o.cfg.volume == 100);
  CHECK(vpk_audio_out_start(&o) == 77 && o.thread == 77 && created_prio == 0x40 && created_mask == 0x20000);
  vpk_audio_ring_push(&ring, pcm, 300);
  vpk_audio_out_step(&o);
  CHECK(pulls == 1 && last_got == 256 && outputs == 1 && last_buf == o.buf[0] && first_left == 1);
  vpk_audio_out_step(&o);
  CHECK(pulls == 2 && last_got == 44 && outputs == 2 && last_buf == o.buf[1] && first_left == 257 && ring.underruns == 1);
  vpk_audio_out_close(&o);
  CHECK(released == 1 && o.port == -1);
  printf("audio_out_test: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}

/* The host backend: the portable conformance run (conformance.c), then what only the host can show, through
 * enc_host.h: a presented picture lands in its aspect box, a scripted pad and touch come back from the reads, the
 * fake speaker plays what was pushed, the quit flag, and a synthetic ISO (no game data) read through enc_disc.
 *   core/test/run_tests.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conformance.h"
#include "enc_host.h"

static int fails;
#define CHECK(name, cond)                                                                                             \
  do {                                                                                                                \
    if (cond)                                                                                                         \
      printf("  ok   %s\n", name);                                                                                    \
    else {                                                                                                            \
      printf("  FAIL %s (line %d)\n", name, __LINE__);                                                                \
      fails++;                                                                                                        \
    }                                                                                                                 \
  } while (0)

static unsigned char img[24][2048];
static void le32(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16),
  p[3] = (unsigned char)(v >> 24);
}
static int rec(unsigned char *p, const char *name, int nl, unsigned lba, unsigned size, int dir) {
  int len = 33 + nl + (1 - nl % 2);
  p[0] = (unsigned char)len;
  le32(p + 2, lba);
  le32(p + 10, size);
  p[25] = dir ? 2 : 0;
  p[32] = (unsigned char)nl;
  memcpy(p + 33, name, (size_t)nl);
  return len;
}

static void disc_checks(void) {
  char path[512];
  unsigned char got[2048];
  uint32_t lba = 0, size = 0;
  EncDisc *d = NULL;
  FILE *f;
  int at;
  img[16][0] = 1;
  memcpy(img[16] + 1, "CD001", 5);
  rec(img[16] + 156, "\0", 1, 18, 2048, 1);
  at = rec(img[18], "\0", 1, 18, 2048, 1);
  at += rec(img[18] + at, "\1", 1, 18, 2048, 1);
  rec(img[18] + at, "HELLO.TXT;1", 11, 20, 5, 0);
  memcpy(img[20], "hello", 5);
  if (enc_path(ENC_DIR_DATA, "disc.iso", path, sizeof path) != ENC_OK || !(f = fopen(path, "wb"))) {
    CHECK("write a synthetic iso", 0);
    return;
  }
  fwrite(img, 1, sizeof img, f);
  fclose(f);
  CHECK("disc open (a synthetic iso)", enc_disc_open(&d, path) == ENC_OK);
  if (!d) return;
  CHECK("disc find", enc_disc_find(d, "/HELLO.TXT", &lba, &size) == ENC_OK && lba == 20 && size == 5);
  CHECK("disc read", enc_disc_read(d, 20, 1, got) == ENC_OK && !memcmp(got, "hello", 5));
  CHECK("disc read past the end", enc_disc_read(d, 1000, 1, got) == ENC_ERR_IO);
  CHECK("disc find a missing file", enc_disc_find(d, "NOPE.BIN", &lba, &size) == ENC_ERR_IO);
  enc_disc_close(d);
  CHECK("disc open of a missing file", enc_disc_open(&d, "/nonexistent/x.iso") == ENC_ERR_IO && d == NULL);
}

static int seen[8], nseen;
static void on_event(const EncEvent *ev, void *user) {
  (void)user;
  if (nseen < 8) seen[nseen++] = ev->type;
}

/* API 2: suspend and resume (the clock, the vblank and the audio across them), focus, quit, and the optional
 * devices driven from the script side. */
static void lifecycle_checks(void) {
  static int16_t pcm[2 * 4410];
  EncAudioConfig ac = {44100, 2, 200};
  EncEvent ev;
  EncMotion m = {{0.0f, 0.0f, 1.0f}, {0.5f, 0.0f, -0.25f}, 0}, mg;
  EncTextConfig tc = {"Name", "Spyro", 0, ENC_TEXT_PLAIN};
  char buf[8];
  uint64_t t0, v0, dt;
  int low = -1, high = -1, ms = -1;

  while (enc_event_poll(&ev) == 1) {
  }
  CHECK("suspend: audio open", enc_audio_open(&ac) == ENC_OK && enc_audio_push(pcm, 4410) == 4410);
  t0 = enc_time_us(), v0 = enc_vblank_count();
  enc_host_suspend();
  enc_sleep_us(150000);
  dt = enc_time_us() - t0;
  CHECK("suspend: the clock stands still", dt < 1000);
  CHECK("suspend: the vblank count stands still", enc_vblank_count() - v0 <= 1);
  CHECK("suspend: the speaker stops (the queue keeps its audio)", enc_audio_queued() >= 4410 - 441 - 64);
  enc_host_resume();
  CHECK("resume: the queued audio is dropped", enc_audio_queued() == 0);
  enc_sleep_us(20000);
  dt = enc_time_us() - t0;
  CHECK("resume: the clock goes on from where it stopped", dt >= 19000 && dt < 120000);
  CHECK("resume: so does the vblank count", enc_vblank_count() - v0 < 8);
  enc_input_poll();
  CHECK("events: SUSPEND, then RESUME with its length",
        enc_event_poll(&ev) == 1 && ev.type == ENC_EVENT_SUSPEND && enc_event_poll(&ev) == 1 &&
            ev.type == ENC_EVENT_RESUME && ev.suspended_us >= 140000 && ev.suspended_us < 1000000);
  CHECK("events: nothing more", enc_event_poll(&ev) == 0);
  CHECK("audio plays on after a resume", enc_audio_push(pcm, 441) == 441);
  enc_sleep_us(40000);
  CHECK("and drains", enc_audio_queued() == 0);
  enc_audio_close();

  nseen = 0;
  enc_event_set_handler(on_event, NULL);
  enc_host_set_focus(0);
  enc_host_set_focus(1);
  CHECK("handler: nothing before a poll", nseen == 0);
  enc_input_poll();
  CHECK("handler: FOCUS_LOST then FOCUS_GAINED inside the poll",
        nseen == 2 && seen[0] == ENC_EVENT_FOCUS_LOST && seen[1] == ENC_EVENT_FOCUS_GAINED);
  enc_event_set_handler(NULL, NULL);
  CHECK("handler: nothing queued behind it", enc_event_poll(&ev) == 0);

  CHECK("rumble: recorded", enc_pad_rumble(1, 1000, 65535, 250) == ENC_OK);
  enc_host_rumble(1, &low, &high, &ms);
  CHECK("rumble: as set", low == 1000 && high == 65535 && ms == 250);

  enc_host_set_motion(0, &m);
  CHECK("motion: nothing until a poll", enc_motion_read(0, &mg) == ENC_OK && mg.accel[2] == 0.0f);
  enc_input_poll();
  CHECK("motion: the scripted sample, stamped",
        enc_motion_read(0, &mg) == ENC_OK && mg.accel[2] == 1.0f && mg.gyro[2] == -0.25f && mg.time_us > 0);

  CHECK("text: begin", enc_text_input_begin(&tc) == ENC_OK);
  CHECK("text: pending, with the initial text cut at a character", enc_text_input_poll(buf, 4) == ENC_TEXT_PENDING &&
                                                                         !strcmp(buf, "Spy"));
  enc_host_set_text("Gn\xc3\xa4sty", ENC_TEXT_DONE); /* "Gnästy": a cut must not split the a-umlaut */
  CHECK("text: done, cut to fit at a character", enc_text_input_poll(buf, 4) == ENC_TEXT_DONE && !strcmp(buf, "Gn"));
  CHECK("text: closed after done", enc_text_input_poll(buf, sizeof buf) == ENC_ERR_STATE);
  CHECK("text: begin again", enc_text_input_begin(&tc) == ENC_OK);
  enc_host_set_text(NULL, ENC_TEXT_CANCELLED);
  CHECK("text: cancelled by the player", enc_text_input_poll(buf, sizeof buf) == ENC_TEXT_CANCELLED);

  CHECK("gpu mode: the host has no graphics API", enc_platform_info()->gfx_apis == 0);
}

static void host_checks(void) {
  static uint32_t px[240][320];
  static int16_t pcm[2 * 4410];
  EncVideoConfig vc = {320, 240, 60, ENC_ASPECT_4_3, 0, 0, ENC_GFX_NONE, 0, 0};
  EncFrame fr = {px, 320, 240, 320 * 4, ENC_PIX_XRGB8888};
  EncAudioConfig ac = {44100, 2, 100};
  EncPad set = {ENC_BTN_SOUTH | ENC_BTN_START, 1000, -2000, 0, 0, 0, 255, 1}, got;
  EncTouch t = {7, 0.25f, 0.75f}, tg[2];
  const uint32_t *disp;
  int w, h, i;
  uint64_t p0 = enc_host_presents();

  enc_video_close(); /* conformance left it open */
  CHECK("video reopens", enc_video_open(&vc) == ENC_OK);
  for (i = 0; i < 320 * 240; i++) px[i / 320][i % 320] = 0x00ff0000;
  CHECK("present", enc_video_present(&fr) == ENC_OK && enc_host_presents() == p0 + 1);
  disp = enc_host_display(&w, &h);
  CHECK("display is 1280x720", disp && w == 1280 && h == 720);
  if (disp) {
    CHECK("the 4:3 box is red in the middle", disp[360 * 1280 + 640] == 0x00ff0000);
    CHECK("and black in the bars", disp[360 * 1280 + 10] == 0 && disp[360 * 1280 + 1270] == 0);
    CHECK("the box starts at x=160", disp[360 * 1280 + 159] == 0 && disp[360 * 1280 + 160] == 0x00ff0000);
  }
  for (i = 0; i < 320 * 240; i++) ((uint16_t *)px)[i] = 0x001f; /* XBGR1555: pure red */
  fr.format = ENC_PIX_XBGR1555, fr.stride = 320 * 2;
  CHECK("present XBGR1555 converts", enc_video_present(&fr) == ENC_OK && disp[360 * 1280 + 640] == 0x00ff0000);

  enc_host_set_pad(0, &set);
  CHECK("pad: nothing until a poll", enc_pad_read(0, &got) == ENC_OK && got.buttons == 0);
  enc_input_poll();
  CHECK("pad: the scripted state after a poll",
        enc_pad_read(0, &got) == ENC_OK && got.buttons == set.buttons && got.lx == 1000 && got.ly == -2000 &&
            got.r2 == 255 && got.connected);
  enc_host_set_touch(ENC_TOUCH_REAR, &t, 1);
  enc_input_poll();
  CHECK("touch: the scripted point", enc_touch_read(ENC_TOUCH_REAR, tg, 2) == 1 && tg[0].id == 7 && tg[0].x == 0.25f);
  CHECK("touch: none on the front", enc_touch_read(ENC_TOUCH_FRONT, tg, 2) == 0);

  CHECK("audio open", enc_audio_open(&ac) == ENC_OK);
  CHECK("audio takes 100 ms", enc_audio_push(pcm, 4410) == 4410);
  enc_sleep_us(250000);
  CHECK("the speaker played it all", enc_audio_queued() == 0 && enc_host_audio_played() == 4410);
  enc_audio_close();

  disc_checks();
  lifecycle_checks();

  CHECK("quit flag", enc_platform_should_quit() == 0);
  enc_host_request_quit();
  CHECK("quit after a request", enc_platform_should_quit() == 1);
  {
    EncEvent ev;
    enc_input_poll();
    CHECK("QUIT event", enc_event_poll(&ev) == 1 && ev.type == ENC_EVENT_QUIT);
  }
}

int main(int argc, char **argv) {
  EncPlatformConfig pc = {"encore-host-test", NULL, argc, argv};
  int f;
  if (enc_platform_init(&pc) != ENC_OK) {
    printf("host_test: init failed\n");
    return 2;
  }
  CHECK("a second init is refused", enc_platform_init(&pc) == ENC_ERR_STATE);
  f = enc_conformance_run();
  host_checks();
  enc_platform_shutdown();
  printf("host_test: conformance %d failed, host %d failed\n", f, fails);
  return f || fails ? 1 : 0;
}

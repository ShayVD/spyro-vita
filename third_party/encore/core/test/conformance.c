/* The platform interface's conformance checks (docs/platform-interface.md): what every backend must do, written
 * against encore/platform.h alone so the same file runs on any target. A target runs it in a test app (on the host
 * in CI; on a console only with the owner's yes). Returns the number of failures; logs one line per check.
 * With -DENC_CONFORMANCE_MAIN it is a program of its own; core/test/host_test.c adds the host-only checks. */
#include "conformance.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(name, cond)                                                                                             \
  do {                                                                                                                \
    if (cond)                                                                                                         \
      enc_log("  ok   %s", name);                                                                                     \
    else {                                                                                                            \
      enc_log("  FAIL %s (%s:%d)", name, __FILE__, __LINE__);                                                         \
      fails++;                                                                                                        \
    }                                                                                                                 \
  } while (0)

static void check_aspect(void) {
  EncRect r = enc_aspect_box(320, 240, 960, 544, ENC_ASPECT_4_3);
  CHECK("aspect 4:3 on the Vita's 960x544 is 725x544 centred", r.w == 725 && r.h == 544 && r.x == 117 && r.y == 0);
  r = enc_aspect_box(640, 448, 1920, 1080, ENC_ASPECT_16_9);
  CHECK("aspect 16:9 on 1080p is the whole screen", r.w == 1920 && r.h == 1080 && r.x == 0 && r.y == 0);
  r = enc_aspect_box(320, 240, 1280, 1024, ENC_ASPECT_4_3);
  CHECK("aspect 4:3 on 5:4 has bars above and below", r.w == 1280 && r.h == 960 && r.y == 32);
  r = enc_aspect_box(320, 240, 1280, 720, ENC_ASPECT_INTEGER);
  CHECK("aspect integer: 3x 320x240 in 720p", r.w == 960 && r.h == 720 && r.x == 160);
  r = enc_aspect_box(320, 240, 960, 544, ENC_ASPECT_STRETCH);
  CHECK("aspect stretch fills", r.w == 960 && r.h == 544);
  r = enc_aspect_box(0, 240, 960, 544, ENC_ASPECT_4_3);
  CHECK("aspect: a zero size gives an empty box", r.w == 0 && r.h == 0);
  CHECK("error names", !strcmp(enc_error_name(ENC_ERR_UNSUPPORTED), "unsupported"));
}

static void check_info(void) {
  const EncPlatformInfo *i = enc_platform_info();
  CHECK("info after init", i != NULL);
  if (!i) return;
  CHECK("info names the target", i->target && i->target[0]);
  CHECK("info has a display and a rate", i->display_width > 0 && i->display_height > 0 && i->refresh_hz > 0);
  CHECK("info has a core", i->cpu_cores >= 1);
  CHECK("not quitting at start", enc_platform_should_quit() == 0);
}

static void check_video(void) {
  static uint32_t px[240][320];
  EncVideoConfig vc = {320, 240, 60, ENC_ASPECT_4_3, 0, 0, ENC_GFX_NONE, 0, 0};
  EncFrame f = {px, 320, 240, 320 * 4, ENC_PIX_XRGB8888};
  int i;
  CHECK("present before open is refused", enc_video_present(&f) == ENC_ERR_STATE);
  CHECK("video open", enc_video_open(&vc) == ENC_OK);
  CHECK("a second open is refused", enc_video_open(&vc) == ENC_ERR_STATE);
  for (i = 0; i < 320 * 240; i++) px[i / 320][i % 320] = 0x00ff8040;
  CHECK("present XRGB8888", enc_video_present(&f) == ENC_OK);
  f.format = ENC_PIX_XBGR1555, f.stride = 320 * 2;
  CHECK("present XBGR1555", enc_video_present(&f) == ENC_OK);
  f.stride = 10;
  CHECK("a stride shorter than a row is refused", enc_video_present(&f) == ENC_ERR_ARG);
  CHECK("aspect change", enc_video_set_aspect(ENC_ASPECT_16_9) == ENC_OK);
  CHECK("NULL frame refused", enc_video_present(NULL) == ENC_ERR_ARG);
}

static const char *const gfx_name[] = {"none", "gl", "gles", "vulkan", "gxm", "deko3d", "gnm"};
#define GFX_LAST ENC_GFX_GNM

static void check_text(int must) {
  EncTextConfig tc = {"Encore conformance", "abc", 16, ENC_TEXT_PLAIN};
  char buf[64];
  int r;
  if (!(enc_platform_info()->caps & ENC_CAP_TEXT_INPUT)) {
    CHECK("text input: unsupported without the cap", enc_text_input_begin(&tc) == ENC_ERR_UNSUPPORTED);
    return;
  }
  CHECK("text input: NULL config refused", enc_text_input_begin(NULL) == ENC_ERR_ARG);
  r = enc_text_input_begin(&tc);
  if (!must && r == ENC_ERR_UNSUPPORTED) { /* the Vita draws its keyboard with the GPU, the game's GL when it brings one */
    enc_log("  skip text input (unsupported here without a GPU core drives)");
    return;
  }
  CHECK("text input: begin", r == ENC_OK);
  if (r != ENC_OK) return;
  CHECK("text input: one at a time", enc_text_input_begin(&tc) == ENC_ERR_STATE);
  r = enc_text_input_poll(buf, sizeof buf);
  CHECK("text input: pending with nobody typing", r == ENC_TEXT_PENDING);
  CHECK("text input: poll needs a buffer", enc_text_input_poll(NULL, 4) == ENC_ERR_ARG);
  enc_text_input_cancel();
  CHECK("text input: closed after a cancel", enc_text_input_poll(buf, sizeof buf) == ENC_ERR_STATE);
}

static void check_gpu(void) {
  static uint32_t px[16 * 16];
  const EncPlatformInfo *info = enc_platform_info();
  EncVideoConfig vc = {320, 240, 60, ENC_ASPECT_4_3, 0, 0, ENC_GFX_NONE, 0, 0};
  EncFrame f = {px, 16, 16, 16 * 4, ENC_PIX_XRGB8888};
  EncGpu g;
  EncGpuFrame fr;
  int api, missing = -1, rc, i;
  /* check_video left the video open with CPU frames */
  CHECK("gpu: no handles with CPU frames", enc_video_gpu(&g) == ENC_ERR_STATE);
  CHECK("gpu: no begin with CPU frames", enc_video_gpu_begin(&fr) == ENC_ERR_STATE);
  CHECK("gpu: no present with CPU frames", enc_video_gpu_present() == ENC_ERR_STATE);
  check_text(0);
  enc_video_close();
  for (api = ENC_GFX_GL; api <= GFX_LAST; api++)
    if (!(info->gfx_apis & (1u << api))) missing = api;
  CHECK("gpu: gfx_apis names only known APIs and never NONE", (info->gfx_apis & ~(((2u << GFX_LAST) - 1) & ~1u)) == 0);
  if (missing > 0) {
    vc.gfx = missing;
    CHECK("gpu: an API the target lacks is unsupported", enc_video_open(&vc) == ENC_ERR_UNSUPPORTED);
  }
  for (api = ENC_GFX_GL; api <= GFX_LAST; api++) {
    char name[96];
    uint64_t t0;
    if (!(info->gfx_apis & (1u << api))) continue;
    enc_log("  gpu mode: %s", gfx_name[api]);
    vc.gfx = api, vc.vsync = 0, vc.refresh_hz = 60, vc.aspect = ENC_ASPECT_4_3;
    rc = enc_video_open(&vc);
    snprintf(name, sizeof name, "gpu %s: open (%s)", gfx_name[api], enc_error_name(rc));
    CHECK(name, rc == ENC_OK);
    if (rc != ENC_OK) continue;
    memset(&g, 0, sizeof g);
    CHECK("gpu: handles", enc_video_gpu(&g) == ENC_OK && g.api == api && (g.core_swaps == 0 || g.core_swaps == 1));
    if (api == ENC_GFX_GL) CHECK("gpu gl: a context and a loader", g.context && g.get_proc && g.core_swaps == 1);
    if (api == ENC_GFX_VULKAN)
      CHECK("gpu vulkan: a surface maker and a loader", g.vk_create_surface && g.get_proc && g.core_swaps == 0);
    if (api == ENC_GFX_GXM) CHECK("gpu gxm: core owns the buffers and swaps", g.buffers >= 2 && g.core_swaps == 1);
    CHECK("gpu: no CPU frames in GPU mode", enc_video_present(&f) == ENC_ERR_STATE);
    CHECK("gpu: a present needs a begin", enc_video_gpu_present() == ENC_ERR_STATE);
    memset(&fr, 0, sizeof fr);
    rc = enc_video_gpu_begin(&fr);
    CHECK("gpu: begin", rc == ENC_OK && fr.width > 0 && fr.height > 0);
    if (rc == ENC_OK) {
      EncRect b = enc_aspect_box(320, 240, fr.width, fr.height, ENC_ASPECT_4_3);
      CHECK("gpu: the box is the 4:3 aspect box of the drawable",
            fr.box.x == b.x && fr.box.y == b.y && fr.box.w == b.w && fr.box.h == b.h);
      if (api == ENC_GFX_GXM)
        CHECK("gpu gxm: a surface, a sync object and memory", fr.surface && fr.sync && fr.pixels && fr.stride_px >= fr.width);
      CHECK("gpu: one begin at a time", enc_video_gpu_begin(&fr) == ENC_ERR_STATE);
      CHECK("gpu: present", enc_video_gpu_present() == ENC_OK);
    }
    CHECK("gpu: aspect change", enc_video_set_aspect(ENC_ASPECT_16_9) == ENC_OK);
    rc = enc_video_gpu_begin(&fr);
    if (rc == ENC_OK) {
      EncRect b = enc_aspect_box(320, 240, fr.width, fr.height, ENC_ASPECT_16_9);
      CHECK("gpu: the box follows the aspect", fr.box.w == b.w && fr.box.h == b.h && fr.box.x == b.x);
      enc_video_gpu_present();
    } else
      CHECK("gpu: begin after an aspect change", 0);
    /* text input in GPU mode, unless the game brings the GL library and so the GPU (the Vita's vitaGL): then
     * the keyboard needs the game's GL up, which this run doesn't have */
    check_text(!(api == ENC_GFX_GLES && !g.get_proc));
    enc_video_close();
    /* pacing: a 30 Hz source with vsync on a faster display returns at its own vblanks */
    vc.vsync = 1, vc.refresh_hz = 30;
    if (enc_video_open(&vc) == ENC_OK) {
      t0 = enc_time_us();
      for (i = 0, rc = ENC_OK; i < 6 && rc == ENC_OK; i++)
        if ((rc = enc_video_gpu_begin(&fr)) == ENC_OK) rc = enc_video_gpu_present();
      t0 = enc_time_us() - t0;
      CHECK("gpu: six presents of a 30 Hz source with vsync take at least five of its vblanks", rc == ENC_OK && t0 >= 160000);
      enc_video_close();
    } else
      CHECK("gpu: reopens", 0);
  }
}

static int handled;
static void on_event(const EncEvent *ev, void *user) {
  (void)ev;
  (*(int *)user)++;
}

static void check_events(void) {
  EncEvent ev;
  int i, ok = 1, r = 0;
  CHECK("events: poll needs a struct", enc_event_poll(NULL) == ENC_ERR_ARG);
  enc_input_poll();
  for (i = 0; i < 64 && (r = enc_event_poll(&ev)) == 1; i++)
    if (ev.type <= ENC_EVENT_NONE || ev.type > ENC_EVENT_DISPLAY) ok = 0;
  CHECK("events: the queue empties, every event a known type", r == 0 && ok);
  handled = 0;
  enc_event_set_handler(on_event, &handled);
  CHECK("events: poll with a handler set", enc_input_poll() == ENC_OK && handled >= 0);
  enc_event_set_handler(NULL, NULL);
  CHECK("events: none queued while the handler had them", enc_event_poll(&ev) == 0);
}

static void check_optional_input(void) {
  uint32_t caps = enc_platform_info()->caps;
  EncMotion m;
  int r;
  if (caps & ENC_CAP_RUMBLE) {
    r = enc_pad_rumble(0, 20000, 40000, 50);
    CHECK("rumble: on, or unsupported on this pad", r == ENC_OK || r == ENC_ERR_UNSUPPORTED);
    r = enc_pad_rumble(0, 0, 0, 0);
    CHECK("rumble: off", r == ENC_OK || r == ENC_ERR_UNSUPPORTED);
    CHECK("rumble: port -1 refused", enc_pad_rumble(-1, 0, 0, 0) == ENC_ERR_ARG);
    CHECK("rumble: a strength past 65535 refused", enc_pad_rumble(0, 70000, 0, 0) == ENC_ERR_ARG);
  } else
    CHECK("rumble: unsupported without the cap", enc_pad_rumble(0, 1000, 1000, 10) == ENC_ERR_UNSUPPORTED);
  if (caps & ENC_CAP_MOTION) {
    enc_input_poll();
    r = enc_motion_read(0, &m);
    CHECK("motion: reads, or unsupported on this pad", r == ENC_OK || r == ENC_ERR_UNSUPPORTED);
    if (r == ENC_OK)
      CHECK("motion: finite and under 16 g", m.accel[0] == m.accel[0] && m.accel[0] < 16 && m.accel[0] > -16 &&
                                                 m.accel[1] < 16 && m.accel[1] > -16 && m.accel[2] < 16 &&
                                                 m.accel[2] > -16 && m.gyro[0] == m.gyro[0]);
    CHECK("motion: port -1 refused", enc_motion_read(-1, &m) == ENC_ERR_ARG);
    CHECK("motion: NULL refused", enc_motion_read(0, NULL) == ENC_ERR_ARG);
  } else
    CHECK("motion: unsupported without the cap", enc_motion_read(0, &m) == ENC_ERR_UNSUPPORTED);
}

static void check_input(void) {
  EncPad p;
  EncTouch t[4];
  int n;
  memset(&p, 0xaa, sizeof p);
  CHECK("input poll", enc_input_poll() == ENC_OK);
  CHECK("pad 0 reads", enc_pad_read(0, &p) == ENC_OK);
  CHECK("pad buttons are known bits", (p.buttons & ~0x1ffffu) == 0);
  CHECK("pad -1 refused", enc_pad_read(-1, &p) == ENC_ERR_ARG);
  CHECK("pad 1000 refused", enc_pad_read(1000, &p) == ENC_ERR_ARG);
  n = enc_touch_read(ENC_TOUCH_FRONT, t, 4);
  CHECK("front touch: points or unsupported", (n >= 0 && n <= 4) || n == ENC_ERR_UNSUPPORTED);
  CHECK("touch panel 7 refused", enc_touch_read(7, t, 4) == ENC_ERR_ARG);
}

static void check_audio(void) {
  static int16_t pcm[2 * 1024];
  EncAudioConfig ac = {44100, 2, 0};
  int got, q;
  CHECK("push before open is refused", enc_audio_push(pcm, 1) == ENC_ERR_STATE);
  CHECK("audio open", enc_audio_open(&ac) == ENC_OK);
  got = enc_audio_push(pcm, 512);
  CHECK("push takes frames", got > 0 && got <= 512);
  q = enc_audio_queued();
  CHECK("queued is sane", q >= 0 && q <= got);
  CHECK("push of 0 frames", enc_audio_push(pcm, 0) == 0);
  CHECK("volume", enc_audio_set_volume(50) == ENC_OK);
  enc_sleep_us(60000);
  CHECK("the queue drains", enc_audio_queued() < got || got == 0);
  enc_audio_close();
  CHECK("closed again", enc_audio_push(pcm, 1) == ENC_ERR_STATE);
}

static void check_files(void) {
  char path[512], back[16] = {0};
  FILE *f;
  CHECK("save path", enc_path(ENC_DIR_SAVE, "conformance.txt", path, sizeof path) == ENC_OK);
  f = fopen(path, "w");
  CHECK("save dir is writable", f != NULL);
  if (f) {
    fputs("encore", f);
    fclose(f);
    f = fopen(path, "r");
    CHECK("and readable", f && fgets(back, sizeof back, f) && !strcmp(back, "encore"));
    if (f) fclose(f);
    remove(path);
  }
  CHECK("config dir", enc_path(ENC_DIR_CONFIG, "", path, sizeof path) == ENC_OK && path[0]);
  CHECK("a path that does not fit", enc_path(ENC_DIR_SAVE, "x", path, 2) == ENC_ERR_ARG);
  CHECK("'..' refused", enc_path(ENC_DIR_SAVE, "../escape", path, sizeof path) == ENC_ERR_ARG);
}

static void check_timing(void) {
  uint64_t t0 = enc_time_us(), v0, dt;
  enc_sleep_us(20000);
  dt = enc_time_us() - t0;
  CHECK("sleep 20 ms takes 20-200 ms", dt >= 19000 && dt < 200000);
  v0 = enc_vblank_count();
  CHECK("vblank wait", enc_vblank_wait() == ENC_OK && enc_vblank_count() > v0);
}

static int counter;
static void bump(void *arg) {
  EncMutex *m = (EncMutex *)arg;
  int i;
  for (i = 0; i < 10000; i++) {
    enc_mutex_lock(m);
    counter++;
    enc_mutex_unlock(m);
  }
}
static void poster(void *arg) {
  enc_sleep_us(10000);
  enc_sem_post((EncSem *)arg);
}

static void check_threads(void) {
  EncThread *a = NULL, *b = NULL;
  EncMutex *m = NULL;
  EncSem *s = NULL;
  EncThreadConfig tc = {"conformance", 0, ENC_PRIO_NORMAL, -1};
  CHECK("mutex", enc_mutex_create(&m) == ENC_OK);
  counter = 0;
  CHECK("two threads", enc_thread_start(&a, &tc, bump, m) == ENC_OK && enc_thread_start(&b, &tc, bump, m) == ENC_OK);
  CHECK("join", enc_thread_join(a) == ENC_OK && enc_thread_join(b) == ENC_OK);
  CHECK("the mutex kept 20000 increments", counter == 20000);
  enc_mutex_destroy(m);
  CHECK("semaphore", enc_sem_create(&s, 0) == ENC_OK);
  CHECK("a wait times out", enc_sem_wait(s, 5000) == ENC_ERR_STATE);
  CHECK("a post from a thread wakes a wait", enc_thread_start(&a, &tc, poster, s) == ENC_OK &&
                                                 enc_sem_wait(s, 2000000) == ENC_OK && enc_thread_join(a) == ENC_OK);
  enc_sem_destroy(s);
}

static int trace[8], ntrace;
static void co_body(void *arg) {
  trace[ntrace++] = *(int *)arg;
  enc_co_yield();
  trace[ntrace++] = *(int *)arg + 1;
  enc_co_yield();
  trace[ntrace++] = enc_co_self() != NULL ? 100 : -1;
}

static void check_coroutines(void) {
  EncCo *a = NULL, *b = NULL;
  int ia = 10, ib = 20;
  if (!(enc_platform_info()->caps & ENC_CAP_COROUTINES)) {
    enc_log("  skip coroutines (not in caps)");
    return;
  }
  ntrace = 0;
  CHECK("coroutine create", enc_co_create(&a, co_body, &ia, 0) == ENC_OK && enc_co_create(&b, co_body, &ib, 0) == ENC_OK);
  CHECK("not inside a coroutine", enc_co_self() == NULL);
  CHECK("interleaved resumes", enc_co_resume(a) == 1 && enc_co_resume(b) == 1 && enc_co_resume(a) == 1 &&
                                   enc_co_resume(b) == 1 && enc_co_resume(a) == 0 && enc_co_resume(b) == 0);
  CHECK("in order", ntrace == 6 && trace[0] == 10 && trace[1] == 20 && trace[2] == 11 && trace[3] == 21 &&
                        trace[4] == 100 && trace[5] == 100);
  CHECK("a finished coroutine stays finished", enc_co_resume(a) == 0);
  enc_co_destroy(a);
  enc_co_destroy(b);
}

int enc_conformance_run(void) {
  fails = 0;
  enc_log("encore conformance (API %d) on %s", ENC_PLATFORM_API,
          enc_platform_info() ? enc_platform_info()->target : "?");
  check_aspect();
  check_info();
  check_video();
  check_gpu();
  check_events();
  check_input();
  check_optional_input();
  check_audio();
  check_files();
  check_timing();
  check_threads();
  check_coroutines();
  enc_log("encore conformance: %d failed", fails);
  enc_log_flush();
  return fails;
}

#ifdef ENC_CONFORMANCE_MAIN
int main(int argc, char **argv) {
  EncPlatformConfig pc = {"encore-conformance", "Encore conformance", argc, argv};
  int f;
  if (enc_platform_init(&pc) != ENC_OK) {
    fprintf(stderr, "platform init failed\n");
    return 2;
  }
  f = enc_conformance_run();
  enc_platform_shutdown();
  return f ? 1 : 0;
}
#endif

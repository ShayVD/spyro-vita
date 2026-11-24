/* Encore's headless host backend (POSIX: Linux, macOS; 32- and 64-bit): the platform interface with no window, no
 * pad and no speaker, for tests, CI and a debugging build of a port. A desktop build with a real window is
 * targets/pc's job; this one is the reference every target's conformance run is compared with.
 *
 *   video   frames are scaled into an in-memory display (ENC_HOST_DISPLAY=WxH, default 1280x720); with
 *           ENC_HOST_FRAMES=DIR each present also writes DIR/frame-NNNNNN.ppm
 *           GPU mode: no graphics API (gfx_apis 0), so a GPU-mode open is ENC_ERR_UNSUPPORTED
 *   input   whatever enc_host_set_pad / _touch / _motion / _text last set (enc_host.h); rumble is recorded
 *           (enc_host_rumble)
 *   events  scripted: enc_host_suspend / _resume / _set_focus / _request_quit, handed over at the next poll
 *   audio   a queue a fake speaker drains in real time at the game's rate; ENC_HOST_AUDIO=FILE appends what it
 *           plays as raw s16 PCM
 *   files   ENC_HOST_ROOT (default ./encore-data/<game>) with data/, save/, config/, log/, cache/
 *   timing  CLOCK_MONOTONIC less the time spent suspended; the vblank is timed at the source rate
 *   threads pthreads (priority and core hints ignored); coroutines: core/src/enc_co.c
 * Build: core/src/enc_common.c enc_blit.c enc_disc.c enc_discimage.c enc_co.c enc_event.c and this file, with
 * -Icore/include -Icore/host, -lpthread. */
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "enc_host.h"

#include "encore/backend.h"

#if ENC_PLATFORM_API != 2
#error "this backend implements Encore platform API 2"
#endif

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- state ---------------------------------------------------------------------------------------------------- */

static struct {
  int up;
  char game[64];
  char root[512];
  EncPlatformInfo info;
  volatile int quit;
  FILE *log;
  pthread_mutex_t log_lock;
  uint64_t t0;
  int vblank_hz;
  /* video */
  int video_open;
  EncVideoConfig vcfg;
  uint32_t *display;
  uint64_t presents;
  const char *frames_dir;
  /* input */
  EncPad pads[ENC_HOST_PADS], set_pads[ENC_HOST_PADS];
  EncTouch touch[2][10], set_touch[2][10];
  int ntouch[2], set_ntouch[2];
  EncMotion motion[ENC_HOST_PADS], set_motion[ENC_HOST_PADS];
  int rumble[ENC_HOST_PADS][3];
  pthread_mutex_t input_lock;
  /* events scripted from any thread, handed over at the next poll */
  struct { int type; uint64_t suspended_us; } pend[16];
  int npend;
  /* the clock: CLOCK_MONOTONIC less the time spent suspended, standing still while suspended */
  pthread_mutex_t clock_lock;
  uint64_t clock_off, frozen;
  int suspended;
  /* text input */
  int text_open, text_result;
  char text[256];
} H = {0};

static uint64_t now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uint64_t clock_us(void) {
  uint64_t t;
  pthread_mutex_lock(&H.clock_lock);
  t = H.suspended ? H.frozen : now_us() - H.clock_off;
  pthread_mutex_unlock(&H.clock_lock);
  return t;
}

static int mkdirs(const char *path) {
  char tmp[512];
  size_t i, n = strlen(path);
  if (n >= sizeof tmp) return -1;
  memcpy(tmp, path, n + 1);
  for (i = 1; i <= n; i++) {
    if (tmp[i] == '/' || tmp[i] == 0) {
      char c = tmp[i];
      tmp[i] = 0;
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
      tmp[i] = c;
    }
  }
  return 0;
}

/* ---- lifecycle ------------------------------------------------------------------------------------------------ */

int enc_platform_init(const EncPlatformConfig *cfg) {
  const char *root, *disp;
  char path[600];
  long cores;
  int w = 1280, h = 720;
  if (!cfg || !cfg->game || !cfg->game[0] || strlen(cfg->game) >= sizeof H.game) return ENC_ERR_ARG;
  if (H.up) return ENC_ERR_STATE;
  memset(&H, 0, sizeof H);
  strcpy(H.game, cfg->game);
  root = getenv("ENC_HOST_ROOT");
  if (root && root[0])
    snprintf(H.root, sizeof H.root, "%s", root);
  else
    snprintf(H.root, sizeof H.root, "encore-data/%s", H.game);
  disp = getenv("ENC_HOST_DISPLAY");
  if (disp && sscanf(disp, "%dx%d", &w, &h) != 2) w = 1280, h = 720;
  if (w < 16 || h < 16 || w > 8192 || h > 8192) w = 1280, h = 720;
  pthread_mutex_init(&H.log_lock, NULL);
  pthread_mutex_init(&H.input_lock, NULL);
  pthread_mutex_init(&H.clock_lock, NULL);
  enc_event_reset();
  if (mkdirs(H.root) != 0) return ENC_ERR_IO;
  if (enc_path(ENC_DIR_LOG, "", path, sizeof path) == ENC_OK) {
    size_t n = strlen(path);
    snprintf(path + n, sizeof path - n, "/%s.log", H.game);
    H.log = fopen(path, "w");
  }
  cores = sysconf(_SC_NPROCESSORS_ONLN);
  H.info.target = "host";
  H.info.caps = ENC_CAP_HEADLESS | ENC_CAP_ANALOG | ENC_CAP_TRIGGERS_ANALOG | ENC_CAP_TOUCH_FRONT |
                ENC_CAP_TOUCH_REAR | ENC_CAP_COROUTINES | ENC_CAP_RUMBLE | ENC_CAP_MOTION | ENC_CAP_TEXT_INPUT;
  H.info.gfx_apis = 0;
  H.info.cpu_cores = cores > 0 ? (int)cores : 1;
  H.info.display_width = w, H.info.display_height = h;
  H.info.refresh_hz = 60;
  H.vblank_hz = 60;
  H.frames_dir = getenv("ENC_HOST_FRAMES");
  H.t0 = clock_us();
  H.up = 1;
  enc_log("encore-host: %s up (%s, %dx%d)", H.game, H.root, w, h);
  return ENC_OK;
}

const EncPlatformInfo *enc_platform_info(void) { return H.up ? &H.info : NULL; }

int enc_platform_should_quit(void) { return H.quit; }

static void pend(int type, uint64_t suspended_us) { /* under input_lock */
  if (H.npend == (int)(sizeof H.pend / sizeof H.pend[0])) return;
  H.pend[H.npend].type = type, H.pend[H.npend].suspended_us = suspended_us;
  H.npend++;
}

void enc_host_request_quit(void) {
  pthread_mutex_lock(&H.input_lock);
  if (!H.quit) pend(ENC_EVENT_QUIT, 0);
  H.quit = 1;
  pthread_mutex_unlock(&H.input_lock);
}

void enc_host_suspend(void) {
  int was;
  pthread_mutex_lock(&H.clock_lock);
  was = H.suspended;
  if (!was) H.frozen = now_us() - H.clock_off, H.suspended = 1;
  pthread_mutex_unlock(&H.clock_lock);
  if (was) return;
  pthread_mutex_lock(&H.input_lock);
  pend(ENC_EVENT_SUSPEND, 0);
  pthread_mutex_unlock(&H.input_lock);
}

static void audio_drop(void);

void enc_host_resume(void) {
  uint64_t gap = 0;
  int was;
  pthread_mutex_lock(&H.clock_lock);
  was = H.suspended;
  if (was) {
    gap = now_us() - H.clock_off - H.frozen;
    H.clock_off += gap;
    H.suspended = 0;
  }
  pthread_mutex_unlock(&H.clock_lock);
  if (!was) return;
  audio_drop();
  pthread_mutex_lock(&H.input_lock);
  pend(ENC_EVENT_RESUME, gap);
  pthread_mutex_unlock(&H.input_lock);
}

void enc_host_set_focus(int focused) {
  pthread_mutex_lock(&H.input_lock);
  pend(focused ? ENC_EVENT_FOCUS_GAINED : ENC_EVENT_FOCUS_LOST, 0);
  pthread_mutex_unlock(&H.input_lock);
}

void enc_platform_shutdown(void) {
  if (!H.up) return;
  enc_text_input_cancel();
  enc_audio_close();
  enc_video_close();
  enc_log("encore-host: shutdown");
  if (H.log) fclose(H.log);
  H.log = NULL;
  H.up = 0;
}

void enc_log(const char *fmt, ...) {
  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  pthread_mutex_lock(&H.log_lock);
  fprintf(stderr, "%s\n", line);
  if (H.log) fprintf(H.log, "%s\n", line);
  pthread_mutex_unlock(&H.log_lock);
}

void enc_log_flush(void) {
  pthread_mutex_lock(&H.log_lock);
  fflush(stderr);
  if (H.log) fflush(H.log);
  pthread_mutex_unlock(&H.log_lock);
}

/* ---- video ---------------------------------------------------------------------------------------------------- */

int enc_video_open(const EncVideoConfig *cfg) {
  size_t n;
  if (!H.up) return ENC_ERR_STATE;
  if (!cfg || cfg->source_width <= 0 || cfg->source_height <= 0 || cfg->gfx < 0 || cfg->gfx > 31) return ENC_ERR_ARG;
  if (cfg->gfx != ENC_GFX_NONE && !(H.info.gfx_apis & (1u << cfg->gfx))) return ENC_ERR_UNSUPPORTED;
  if (H.video_open) return ENC_ERR_STATE;
  n = (size_t)H.info.display_width * (size_t)H.info.display_height;
  H.display = (uint32_t *)calloc(n, sizeof *H.display);
  if (!H.display) return ENC_ERR_NOMEM;
  H.vcfg = *cfg;
  if (cfg->refresh_hz > 0) H.vblank_hz = cfg->refresh_hz;
  H.video_open = 1;
  return ENC_OK;
}

int enc_video_set_aspect(int aspect) {
  if (!H.video_open) return ENC_ERR_STATE;
  if ((unsigned)aspect > ENC_ASPECT_INTEGER) return ENC_ERR_ARG;
  H.vcfg.aspect = aspect;
  return ENC_OK;
}

static void write_ppm(void) {
  char path[600];
  FILE *f;
  int i, n = H.info.display_width * H.info.display_height;
  snprintf(path, sizeof path, "%s/frame-%06llu.ppm", H.frames_dir, (unsigned long long)H.presents);
  f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", H.info.display_width, H.info.display_height);
  for (i = 0; i < n; i++) {
    unsigned char rgb[3] = {(unsigned char)(H.display[i] >> 16), (unsigned char)(H.display[i] >> 8),
                            (unsigned char)H.display[i]};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int enc_video_present(const EncFrame *f) {
  int dw = H.info.display_width, dh = H.info.display_height, rc;
  if (!H.video_open) return ENC_ERR_STATE;
  if (!f) return ENC_ERR_ARG;
  rc = enc_blit_scaled(f, H.display, dw, dh, dw, enc_aspect_box(f->width, f->height, dw, dh, H.vcfg.aspect), 0);
  if (rc != ENC_OK) return rc;
  H.presents++;
  if (H.frames_dir && H.frames_dir[0]) write_ppm();
  if (H.vcfg.vsync) enc_vblank_wait();
  return ENC_OK;
}

void enc_video_close(void) {
  free(H.display);
  H.display = NULL;
  H.video_open = 0;
}

const uint32_t *enc_host_display(int *w, int *h) {
  if (w) *w = H.info.display_width;
  if (h) *h = H.info.display_height;
  return H.display;
}

uint64_t enc_host_presents(void) { return H.presents; }

/* No GPU mode on the host: a GPU-mode open never succeeds, so these are always in the wrong state. */
int enc_video_gpu(EncGpu *gpu) { return gpu ? ENC_ERR_STATE : ENC_ERR_ARG; }
int enc_video_gpu_begin(EncGpuFrame *frame) { return frame ? ENC_ERR_STATE : ENC_ERR_ARG; }
int enc_video_gpu_present(void) { return ENC_ERR_STATE; }

/* ---- input ---------------------------------------------------------------------------------------------------- */

void enc_host_set_pad(int port, const EncPad *pad) {
  if (port < 0 || port >= ENC_HOST_PADS || !pad) return;
  pthread_mutex_lock(&H.input_lock);
  H.set_pads[port] = *pad;
  pthread_mutex_unlock(&H.input_lock);
}

void enc_host_set_touch(int panel, const EncTouch *pts, int n) {
  if (panel < 0 || panel > 1 || n < 0 || n > 10 || (n && !pts)) return;
  pthread_mutex_lock(&H.input_lock);
  memcpy(H.set_touch[panel], pts, (size_t)n * sizeof *pts);
  H.set_ntouch[panel] = n;
  pthread_mutex_unlock(&H.input_lock);
}

void enc_host_set_motion(int port, const EncMotion *m) {
  if (port < 0 || port >= ENC_HOST_PADS || !m) return;
  pthread_mutex_lock(&H.input_lock);
  H.set_motion[port] = *m;
  pthread_mutex_unlock(&H.input_lock);
}

int enc_input_poll(void) {
  struct { int type; uint64_t suspended_us; } ev[16];
  int i, n;
  uint64_t t;
  if (!H.up) return ENC_ERR_STATE;
  t = clock_us();
  pthread_mutex_lock(&H.input_lock);
  memcpy(H.pads, H.set_pads, sizeof H.pads);
  memcpy(H.touch, H.set_touch, sizeof H.touch);
  memcpy(H.ntouch, H.set_ntouch, sizeof H.ntouch);
  memcpy(H.motion, H.set_motion, sizeof H.motion);
  for (i = 0; i < ENC_HOST_PADS; i++) H.motion[i].time_us = t;
  n = H.npend;
  memcpy(ev, H.pend, (size_t)n * sizeof ev[0]);
  H.npend = 0;
  pthread_mutex_unlock(&H.input_lock);
  for (i = 0; i < n; i++) enc_event_emit(ev[i].type, 0, 0, ev[i].suspended_us); /* outside the lock: a handler may
                                                                                  call back in */
  return ENC_OK;
}

int enc_pad_read(int port, EncPad *pad) {
  if (!pad || port < 0 || port >= ENC_HOST_PADS) return ENC_ERR_ARG;
  *pad = H.pads[port];
  return ENC_OK;
}

int enc_touch_read(int panel, EncTouch *pts, int max) {
  int n;
  if (panel != ENC_TOUCH_FRONT && panel != ENC_TOUCH_REAR) return ENC_ERR_ARG;
  if (max < 0 || (max && !pts)) return ENC_ERR_ARG;
  n = H.ntouch[panel] < max ? H.ntouch[panel] : max;
  memcpy(pts, H.touch[panel], (size_t)n * sizeof *pts);
  return n;
}

int enc_pad_rumble(int port, int low, int high, int duration_ms) {
  if (port < 0 || port >= ENC_HOST_PADS) return ENC_ERR_ARG;
  if (low < 0 || low > 65535 || high < 0 || high > 65535 || duration_ms < 0) return ENC_ERR_ARG;
  pthread_mutex_lock(&H.input_lock);
  H.rumble[port][0] = low, H.rumble[port][1] = high, H.rumble[port][2] = duration_ms;
  pthread_mutex_unlock(&H.input_lock);
  return ENC_OK;
}

void enc_host_rumble(int port, int *low, int *high, int *duration_ms) {
  if (port < 0 || port >= ENC_HOST_PADS) return;
  pthread_mutex_lock(&H.input_lock);
  if (low) *low = H.rumble[port][0];
  if (high) *high = H.rumble[port][1];
  if (duration_ms) *duration_ms = H.rumble[port][2];
  pthread_mutex_unlock(&H.input_lock);
}

int enc_motion_read(int port, EncMotion *m) {
  if (!m || port < 0 || port >= ENC_HOST_PADS) return ENC_ERR_ARG;
  *m = H.motion[port];
  return ENC_OK;
}

/* ---- text input: a scripted keyboard -------------------------------------------------------------------------- */

static void copy_utf8(char *out, size_t cap, const char *in) { /* cut at a character boundary */
  size_t n = strlen(in);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)in[n] & 0xc0) == 0x80) n--;
  }
  memcpy(out, in, n);
  out[n] = 0;
}

int enc_text_input_begin(const EncTextConfig *cfg) {
  if (!H.up) return ENC_ERR_STATE;
  if (!cfg || cfg->max_chars < 0 || cfg->kind < ENC_TEXT_PLAIN || cfg->kind > ENC_TEXT_MULTILINE) return ENC_ERR_ARG;
  pthread_mutex_lock(&H.input_lock);
  if (H.text_open) {
    pthread_mutex_unlock(&H.input_lock);
    return ENC_ERR_STATE;
  }
  copy_utf8(H.text, sizeof H.text, cfg->initial ? cfg->initial : "");
  H.text_open = 1, H.text_result = ENC_TEXT_PENDING;
  pthread_mutex_unlock(&H.input_lock);
  enc_log("encore-host: text input open (%s)", cfg->title ? cfg->title : "no title");
  return ENC_OK;
}

void enc_host_set_text(const char *text, int result) {
  pthread_mutex_lock(&H.input_lock);
  if (H.text_open) {
    if (text) copy_utf8(H.text, sizeof H.text, text);
    H.text_result = result;
  }
  pthread_mutex_unlock(&H.input_lock);
}

int enc_text_input_poll(char *out, size_t cap) {
  int r;
  if (!out || !cap) return ENC_ERR_ARG;
  pthread_mutex_lock(&H.input_lock);
  if (!H.text_open) {
    pthread_mutex_unlock(&H.input_lock);
    return ENC_ERR_STATE;
  }
  copy_utf8(out, cap, H.text);
  r = H.text_result;
  if (r != ENC_TEXT_PENDING) H.text_open = 0;
  pthread_mutex_unlock(&H.input_lock);
  return r;
}

void enc_text_input_cancel(void) {
  pthread_mutex_lock(&H.input_lock);
  H.text_open = 0;
  pthread_mutex_unlock(&H.input_lock);
}

/* ---- audio: a queue the fake speaker drains at the game's rate ------------------------------------------------- */

static struct {
  int open, channels, rate, volume;
  int16_t *buf;
  int cap, head, count; /* frames */
  uint64_t played;
  volatile int stop;
  pthread_t thread;
  pthread_mutex_t lock;
  FILE *raw;
} A = {0};

static void *speaker(void *arg) { /* on the suspend-aware clock: it stands still, and so does the speaker */
  uint64_t start = clock_us(), consumed = 0;
  (void)arg;
  while (!A.stop) {
    uint64_t due = (clock_us() - start) * (uint64_t)A.rate / 1000000u;
    pthread_mutex_lock(&A.lock);
    while (consumed < due) {
      if (A.count) {
        if (A.raw) fwrite(A.buf + (size_t)A.head * A.channels, sizeof(int16_t), (size_t)A.channels, A.raw);
        A.head = (A.head + 1) % A.cap, A.count--, A.played++;
      }
      consumed++; /* an empty queue is an underrun: the speaker plays silence and time moves on */
    }
    pthread_mutex_unlock(&A.lock);
    enc_sleep_us(2000);
  }
  return NULL;
}

int enc_audio_open(const EncAudioConfig *cfg) {
  const char *raw;
  int ms;
  if (!H.up) return ENC_ERR_STATE;
  if (!cfg || cfg->rate < 4000 || cfg->rate > 192000 || cfg->channels < 1 || cfg->channels > 2) return ENC_ERR_ARG;
  if (A.open) return ENC_ERR_STATE;
  ms = cfg->latency_ms > 0 ? cfg->latency_ms : 64;
  memset(&A, 0, sizeof A);
  A.rate = cfg->rate, A.channels = cfg->channels, A.volume = 100;
  A.cap = (int)((long)cfg->rate * ms * 2 / 1000) + 256; /* twice the latency, so a push of one frame's worth fits */
  A.buf = (int16_t *)malloc((size_t)A.cap * (size_t)A.channels * sizeof *A.buf);
  if (!A.buf) return ENC_ERR_NOMEM;
  pthread_mutex_init(&A.lock, NULL);
  raw = getenv("ENC_HOST_AUDIO");
  if (raw && raw[0]) A.raw = fopen(raw, "ab");
  if (pthread_create(&A.thread, NULL, speaker, NULL) != 0) {
    free(A.buf);
    return ENC_ERR_SYSTEM;
  }
  A.open = 1;
  return ENC_OK;
}

int enc_audio_push(const int16_t *pcm, int frames) {
  int i, n, c;
  if (!A.open) return ENC_ERR_STATE;
  if (frames < 0 || (frames && !pcm)) return ENC_ERR_ARG;
  pthread_mutex_lock(&A.lock);
  n = A.cap - A.count < frames ? A.cap - A.count : frames;
  for (i = 0; i < n; i++) {
    int at = (A.head + A.count + i) % A.cap;
    for (c = 0; c < A.channels; c++)
      A.buf[(size_t)at * A.channels + c] = (int16_t)((int)pcm[(size_t)i * A.channels + c] * A.volume / 100);
  }
  A.count += n;
  pthread_mutex_unlock(&A.lock);
  return n;
}

int enc_audio_queued(void) {
  int n;
  if (!A.open) return ENC_ERR_STATE;
  pthread_mutex_lock(&A.lock);
  n = A.count;
  pthread_mutex_unlock(&A.lock);
  return n;
}

int enc_audio_set_volume(int percent) {
  if (!A.open) return ENC_ERR_STATE;
  A.volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  return ENC_OK;
}

void enc_audio_close(void) {
  if (!A.open) return;
  A.stop = 1;
  pthread_join(A.thread, NULL);
  if (A.raw) fclose(A.raw);
  free(A.buf);
  pthread_mutex_destroy(&A.lock);
  A.open = 0;
}

uint64_t enc_host_audio_played(void) { return A.played; }

static void audio_drop(void) {
  if (!A.open) return;
  pthread_mutex_lock(&A.lock);
  A.count = 0;
  pthread_mutex_unlock(&A.lock);
}

/* ---- files ---------------------------------------------------------------------------------------------------- */

int enc_path(int dir, const char *name, char *out, size_t cap) {
  static const char *const sub[] = {"data", "save", "config", "log", "cache"};
  char d[600];
  int n;
  if (!H.up) return ENC_ERR_STATE;
  if ((unsigned)dir > ENC_DIR_CACHE || !name || !out || !cap) return ENC_ERR_ARG;
  if (strstr(name, "..")) return ENC_ERR_ARG;
  snprintf(d, sizeof d, "%s/%s", H.root, sub[dir]);
  if (mkdirs(d) != 0) return ENC_ERR_IO; /* DATA too on the host: the player's folder must exist to be filled */
  n = name[0] ? snprintf(out, cap, "%s/%s", d, name) : snprintf(out, cap, "%s", d);
  return n < 0 || (size_t)n >= cap ? ENC_ERR_ARG : ENC_OK;
}

/* ---- timing --------------------------------------------------------------------------------------------------- */

uint64_t enc_time_us(void) { return clock_us(); }

void enc_sleep_us(uint64_t us) {
  struct timespec ts;
  ts.tv_sec = (time_t)(us / 1000000u);
  ts.tv_nsec = (long)(us % 1000000u) * 1000;
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

uint64_t enc_vblank_count(void) {
  if (!H.up) return 0;
  return (clock_us() - H.t0) * (uint64_t)H.vblank_hz / 1000000u;
}

int enc_vblank_wait(void) {
  uint64_t next, at;
  if (!H.up) return ENC_ERR_STATE;
  next = enc_vblank_count() + 1;
  at = H.t0 + (next * 1000000u + (uint64_t)H.vblank_hz - 1) / (uint64_t)H.vblank_hz;
  for (;;) { /* on the suspend-aware clock */
    uint64_t now = clock_us();
    if (now >= at) break;
    enc_sleep_us(at - now);
  }
  return ENC_OK;
}

/* ---- threads -------------------------------------------------------------------------------------------------- */

struct EncThread {
  pthread_t t;
  void (*fn)(void *);
  void *arg;
};

static void *thread_main(void *p) {
  EncThread *t = (EncThread *)p;
  t->fn(t->arg);
  return NULL;
}

int enc_thread_start(EncThread **out, const EncThreadConfig *cfg, void (*fn)(void *arg), void *arg) {
  EncThread *t;
  pthread_attr_t at;
  size_t stack = cfg && cfg->stack ? cfg->stack : (256u << 10);
  int rc;
  if (!out || !fn) return ENC_ERR_ARG;
  *out = NULL;
  t = (EncThread *)calloc(1, sizeof *t);
  if (!t) return ENC_ERR_NOMEM;
  t->fn = fn, t->arg = arg;
  pthread_attr_init(&at);
  if (stack < (64u << 10)) stack = 64u << 10;
  pthread_attr_setstacksize(&at, stack);
  rc = pthread_create(&t->t, &at, thread_main, t);
  pthread_attr_destroy(&at);
  if (rc != 0) {
    free(t);
    return ENC_ERR_SYSTEM;
  }
  *out = t;
  return ENC_OK;
}

int enc_thread_join(EncThread *t) {
  if (!t) return ENC_ERR_ARG;
  if (pthread_join(t->t, NULL) != 0) return ENC_ERR_SYSTEM;
  free(t);
  return ENC_OK;
}

struct EncMutex {
  pthread_mutex_t m;
};

int enc_mutex_create(EncMutex **m) {
  if (!m) return ENC_ERR_ARG;
  *m = (EncMutex *)malloc(sizeof **m);
  if (!*m) return ENC_ERR_NOMEM;
  pthread_mutex_init(&(*m)->m, NULL);
  return ENC_OK;
}
void enc_mutex_lock(EncMutex *m) { pthread_mutex_lock(&m->m); }
void enc_mutex_unlock(EncMutex *m) { pthread_mutex_unlock(&m->m); }
void enc_mutex_destroy(EncMutex *m) {
  if (!m) return;
  pthread_mutex_destroy(&m->m);
  free(m);
}

struct EncSem { /* a counter under a mutex and a condition: macOS has no unnamed POSIX semaphores */
  pthread_mutex_t m;
  pthread_cond_t c;
  int n;
};

int enc_sem_create(EncSem **s, int initial) {
  if (!s || initial < 0) return ENC_ERR_ARG;
  *s = (EncSem *)malloc(sizeof **s);
  if (!*s) return ENC_ERR_NOMEM;
  pthread_mutex_init(&(*s)->m, NULL);
  pthread_cond_init(&(*s)->c, NULL);
  (*s)->n = initial;
  return ENC_OK;
}

void enc_sem_post(EncSem *s) {
  pthread_mutex_lock(&s->m);
  s->n++;
  pthread_cond_signal(&s->c);
  pthread_mutex_unlock(&s->m);
}

int enc_sem_wait(EncSem *s, int64_t timeout_us) {
  int rc = ENC_OK;
  struct timespec ts;
  if (!s) return ENC_ERR_ARG;
  if (timeout_us >= 0) {
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(timeout_us / 1000000);
    ts.tv_nsec += (long)(timeout_us % 1000000) * 1000;
    if (ts.tv_nsec >= 1000000000L) ts.tv_sec++, ts.tv_nsec -= 1000000000L;
  }
  pthread_mutex_lock(&s->m);
  while (s->n == 0) {
    if (timeout_us < 0)
      pthread_cond_wait(&s->c, &s->m);
    else if (pthread_cond_timedwait(&s->c, &s->m, &ts) == ETIMEDOUT && s->n == 0) {
      rc = ENC_ERR_STATE;
      break;
    }
  }
  if (rc == ENC_OK) s->n--;
  pthread_mutex_unlock(&s->m);
  return rc;
}

void enc_sem_destroy(EncSem *s) {
  if (!s) return;
  pthread_cond_destroy(&s->c);
  pthread_mutex_destroy(&s->m);
  free(s);
}

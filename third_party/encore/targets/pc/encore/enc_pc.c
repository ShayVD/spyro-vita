/* Encore's platform interface on a PC (core/include/encore/platform.h) over SDL3: Linux, macOS and Windows
 * (mingw-w64), x86-64 and arm64. One thread owns SDL: the game's, which must be the main thread (macOS makes
 * windows and reads events only there), so init, video and input all come from main.
 *
 *   video   a resizable window. CPU frames: converted to 32-bit (enc_blit_scaled at 1:1) into a streaming texture
 *           and drawn by an SDL_Renderer into the aspect box (bilinear with cfg.smooth), black bars around.
 *           GPU mode: GL (a core-profile context, 3.3 unless asked otherwise) or Vulkan (the window, its instance
 *           extensions and its surface), each offered only when a probe at init makes one.
 *           F11 or Alt+Enter toggles borderless fullscreen.
 *   input   up to 4 SDL gamepads, by position (SDL3's buttons are SOUTH, EAST, WEST, NORTH on every pad),
 *           with rumble and their motion sensors where they have them; the keyboard on port 0 (arrows, Z X A S,
 *           Q W, 1 3, Enter, Right Shift, Esc as the menu); text input from the keyboard, drawn by the game
 *   events  FOCUS_LOST / _GAINED (focus, minimise), QUIT (the window closed), DISPLAY (a resize); a desktop app is
 *           never suspended (the system's sleep is invisible to it), so no SUSPEND / RESUME
 *   audio   an SDL audio stream at the game's rate and channels on the default device (SDL converts to the
 *           device's), the queue capped at twice the latency
 *   files   ENC_PC_ROOT when set (tests) with data/, save/, config/, log/, cache/; else DATA is data/ beside the
 *           executable and the rest are in SDL's user folder (~/.local/share/encore/<game>/,
 *           ~/Library/Application Support/encore/<game>/, %APPDATA%\encore\<game>\)
 *   timing  SDL's performance counter, SDL_DelayPrecise; the vblank is timed at the source rate
 *   threads SDL threads, mutexes (recursive) and semaphores; coroutines: core/src/enc_co.c on POSIX,
 *           targets/pc/encore/enc_co_win.c (fibers) on Windows
 * Environment: ENC_PC_ROOT, ENC_PC_WINDOW=WxH (default 1280x720), ENC_PC_NO_GPU=1 (skip the GL / Vulkan probes);
 * SDL's own SDL_VIDEODRIVER and SDL_AUDIODRIVER (dummy, offscreen) for headless runs.
 * Build: targets/pc/test/run_tests.sh. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#endif
#include "enc_pc.h"

#define SDL_MAIN_HANDLED /* the game has its own main */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include "encore/backend.h"

#if ENC_PLATFORM_API != 2
#error "this backend implements Encore platform API 2"
#endif

#if !SDL_VERSION_ATLEAST(3, 2, 0)
#error "targets/pc needs SDL 3.2 or later"
#endif

#define PADS ENC_PC_PADS
#define TEXT_CAP 1024

/* ---- state ---------------------------------------------------------------------------------------------------- */

static struct {
  int up;
  char game[64];
  char root[768], data[800];
  EncPlatformInfo info;
  volatile int quit;
  FILE *log;
  SDL_Mutex *log_lock;
  uint64_t t0, freq;
  int vblank_hz;
  int real_display; /* a driver that shows and vsyncs (not dummy / offscreen) */
  /* video */
  int video_open;
  EncVideoConfig vcfg;
  int win_w, win_h;
  SDL_Window *win;
  SDL_Renderer *ren;
  SDL_Texture *tex;
  int tex_w, tex_h;
  SDL_FRect last_box; /* where the last CPU frame went (enc_pc_read_pixels draws it again) */
  uint32_t *staging;
  size_t staging_px;
  uint64_t presents;
  int hw_vsync;
  SDL_GLContext gl;
  int in_frame;
  int fullscreen;
  const char *const *vk_ext; /* SDL's */
  Uint32 vk_next;
  /* input */
  SDL_Gamepad *pad[PADS];
  EncPad pads[PADS];
  EncMotion motion[PADS];
  int has_motion[PADS];
  uint8_t keys[SDL_SCANCODE_COUNT];
  int focused;
  /* text input */
  int text_open, text_result, text_max, text_multiline;
  char text[TEXT_CAP];
} P;

static uint64_t now_us(void) {
  uint64_t c = SDL_GetPerformanceCounter() - P.t0;
  return c / P.freq * 1000000u + c % P.freq * 1000000u / P.freq;
}

static int mkdirs(const char *path) {
  char tmp[768];
  size_t i, n = strlen(path);
  if (n >= sizeof tmp) return -1;
  memcpy(tmp, path, n + 1);
  for (i = 1; i <= n; i++) {
    if (tmp[i] == '/' || tmp[i] == '\\' || tmp[i] == 0) {
      char c = tmp[i];
      if (i == 2 && tmp[1] == ':') continue; /* C: */
      tmp[i] = 0;
#if defined(_WIN32)
      if (_mkdir(tmp) != 0 && errno != EEXIST) return -1;
#else
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
#endif
      tmp[i] = c;
    }
  }
  return 0;
}

static void strip_slash(char *p) {
  size_t n = strlen(p);
  while (n > 1 && (p[n - 1] == '/' || p[n - 1] == '\\')) p[--n] = 0;
}

/* ---- GPU probes: offer an API only when this machine makes one --------------------------------------------------- */

static void gl_attributes(int major, int minor) {
  SDL_GL_ResetAttributes();
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); /* macOS wants it for core */
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

static int probe_gl(void) {
  SDL_Window *w;
  SDL_GLContext c;
  gl_attributes(3, 3);
  w = SDL_CreateWindow("encore-probe", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!w) return 0;
  c = SDL_GL_CreateContext(w);
  if (c) SDL_GL_DestroyContext(c);
  SDL_DestroyWindow(w);
  return c != NULL;
}

static int probe_vulkan(void) {
  SDL_Window *w;
  Uint32 n = 0;
  const char *const *ext;
  int ok;
  w = SDL_CreateWindow("encore-probe", 16, 16, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
  if (!w) return 0; /* no loader, or no Vulkan in this video driver */
  ext = SDL_Vulkan_GetInstanceExtensions(&n);
  ok = ext && SDL_Vulkan_GetVkGetInstanceProcAddr() != NULL;
  P.vk_ext = ok ? ext : NULL, P.vk_next = ok ? n : 0;
  SDL_DestroyWindow(w);
  return ok;
}

/* ---- lifecycle ------------------------------------------------------------------------------------------------ */

int enc_platform_init(const EncPlatformConfig *cfg) {
  const char *root, *win, *drv;
  char path[900];
  const SDL_DisplayMode *mode;
  int w = 1280, h = 720, v;
  if (!cfg || !cfg->game || !cfg->game[0] || strlen(cfg->game) >= sizeof P.game) return ENC_ERR_ARG;
  if (P.up) return ENC_ERR_STATE;
  memset(&P, 0, sizeof P);
  strcpy(P.game, cfg->game);
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  SDL_SetMainReady(); /* the game has its own main: no SDL_main */
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_SENSOR | SDL_INIT_EVENTS)) {
    fprintf(stderr, "encore-pc: SDL_Init: %s\n", SDL_GetError());
    return ENC_ERR_SYSTEM;
  }
  P.freq = SDL_GetPerformanceFrequency();
  P.t0 = SDL_GetPerformanceCounter();
  P.log_lock = SDL_CreateMutex();
  root = getenv("ENC_PC_ROOT");
  if (root && root[0]) {
    snprintf(P.root, sizeof P.root, "%s", root);
    strip_slash(P.root);
    snprintf(P.data, sizeof P.data, "%s/data", P.root);
  } else {
    char *pref = SDL_GetPrefPath("encore", P.game);
    const char *base = SDL_GetBasePath(); /* SDL's: not freed */
    if (!pref) {
      SDL_Quit();
      return ENC_ERR_IO;
    }
    snprintf(P.root, sizeof P.root, "%s", pref);
    strip_slash(P.root);
    snprintf(P.data, sizeof P.data, "%sdata", base ? base : "./");
    SDL_free(pref);
  }
  if (mkdirs(P.root) != 0) {
    SDL_Quit();
    return ENC_ERR_IO;
  }
  P.up = 1; /* enc_path works from here */
  if (enc_path(ENC_DIR_LOG, "", path, sizeof path) == ENC_OK) {
    size_t n = strlen(path);
    snprintf(path + n, sizeof path - n, "/%s.log", P.game);
    P.log = fopen(path, "w");
  }
  win = getenv("ENC_PC_WINDOW");
  if (win && (sscanf(win, "%dx%d", &w, &h) != 2 || w < 16 || h < 16 || w > 16384 || h > 16384)) w = 1280, h = 720;
  P.win_w = w, P.win_h = h;
  drv = SDL_GetCurrentVideoDriver();
  P.real_display = drv && strcmp(drv, "dummy") != 0 && strcmp(drv, "offscreen") != 0;
  P.info.target = "pc";
  P.info.caps = ENC_CAP_WINDOW | ENC_CAP_ANALOG | ENC_CAP_TRIGGERS_ANALOG | ENC_CAP_RUMBLE | ENC_CAP_KEYBOARD |
                ENC_CAP_MOTION | ENC_CAP_TEXT_INPUT | ENC_CAP_COROUTINES;
  if (!P.real_display) P.info.caps |= ENC_CAP_HEADLESS;
  P.info.cpu_cores = SDL_GetNumLogicalCPUCores() > 0 ? SDL_GetNumLogicalCPUCores() : 1;
  P.info.display_width = w, P.info.display_height = h;
  mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  P.info.refresh_hz = mode && mode->refresh_rate > 0 ? (int)(mode->refresh_rate + 0.5f) : 60;
  if (!getenv("ENC_PC_NO_GPU") || !getenv("ENC_PC_NO_GPU")[0]) {
    if (probe_gl()) P.info.gfx_apis |= 1u << ENC_GFX_GL;
    if (probe_vulkan()) P.info.gfx_apis |= 1u << ENC_GFX_VULKAN;
  }
  P.vblank_hz = 60;
  P.focused = 1;
  enc_event_reset();
  v = SDL_GetVersion();
  enc_log("encore-pc: %s up (SDL %d.%d.%d, video %s, audio %s, gl %s, vulkan %s, %s)", P.game,
          SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v), drv ? drv : "none",
          SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none",
          P.info.gfx_apis & (1u << ENC_GFX_GL) ? "yes" : "no", P.info.gfx_apis & (1u << ENC_GFX_VULKAN) ? "yes" : "no",
          P.root);
  return ENC_OK;
}

const EncPlatformInfo *enc_platform_info(void) { return P.up ? &P.info : NULL; }

int enc_platform_should_quit(void) { return P.quit; }

void enc_platform_shutdown(void) {
  int i;
  if (!P.up) return;
  enc_text_input_cancel();
  enc_audio_close();
  enc_video_close();
  for (i = 0; i < PADS; i++)
    if (P.pad[i]) SDL_CloseGamepad(P.pad[i]);
  enc_log("encore-pc: shutdown");
  if (P.log) fclose(P.log);
  P.log = NULL;
  SDL_DestroyMutex(P.log_lock);
  P.log_lock = NULL;
  P.up = 0;
  SDL_Quit();
}

void enc_log(const char *fmt, ...) {
  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (P.log_lock) SDL_LockMutex(P.log_lock);
  fprintf(stderr, "%s\n", line);
  if (P.log) fprintf(P.log, "%s\n", line);
  if (P.log_lock) SDL_UnlockMutex(P.log_lock);
}

void enc_log_flush(void) {
  if (P.log_lock) SDL_LockMutex(P.log_lock);
  fflush(stderr);
  if (P.log) fflush(P.log);
  if (P.log_lock) SDL_UnlockMutex(P.log_lock);
}

/* ---- video ---------------------------------------------------------------------------------------------------- */

static void drawable(int *w, int *h) {
  *w = *h = 0;
  if (P.ren)
    SDL_GetCurrentRenderOutputSize(P.ren, w, h);
  else
    SDL_GetWindowSizeInPixels(P.win, w, h);
  if (*w <= 0 || *h <= 0) SDL_GetWindowSize(P.win, w, h);
}

static void video_free(void) {
  if (P.tex) SDL_DestroyTexture(P.tex);
  if (P.ren) SDL_DestroyRenderer(P.ren);
  if (P.gl) SDL_GL_DestroyContext(P.gl);
  if (P.win) SDL_DestroyWindow(P.win);
  free(P.staging);
  P.tex = NULL, P.ren = NULL, P.gl = NULL, P.win = NULL, P.staging = NULL;
  P.tex_w = P.tex_h = 0, P.staging_px = 0;
}

int enc_video_open(const EncVideoConfig *cfg) {
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  int w, h;
  if (!P.up) return ENC_ERR_STATE;
  if (!cfg || cfg->source_width <= 0 || cfg->source_height <= 0 || cfg->gfx < 0 || cfg->gfx > 31) return ENC_ERR_ARG;
  if (cfg->gfx != ENC_GFX_NONE && !(P.info.gfx_apis & (1u << cfg->gfx))) return ENC_ERR_UNSUPPORTED;
  if (P.video_open) return ENC_ERR_STATE;
  P.vcfg = *cfg;
  if (cfg->gfx == ENC_GFX_GL) {
    if (!P.vcfg.gfx_major) P.vcfg.gfx_major = 3, P.vcfg.gfx_minor = 3;
    gl_attributes(P.vcfg.gfx_major, P.vcfg.gfx_minor);
    flags |= SDL_WINDOW_OPENGL;
  } else if (cfg->gfx == ENC_GFX_VULKAN)
    flags |= SDL_WINDOW_VULKAN;
  P.win = SDL_CreateWindow(P.game, P.win_w, P.win_h, flags);
  if (!P.win) {
    enc_log("encore-pc: window: %s", SDL_GetError());
    return ENC_ERR_SYSTEM;
  }
  if (cfg->gfx == ENC_GFX_GL) {
    P.gl = SDL_GL_CreateContext(P.win);
    if (!P.gl) {
      enc_log("encore-pc: GL %d.%d context: %s", P.vcfg.gfx_major, P.vcfg.gfx_minor, SDL_GetError());
      video_free();
      return ENC_ERR_SYSTEM;
    }
    SDL_GL_MakeCurrent(P.win, P.gl);
    P.hw_vsync = cfg->vsync && SDL_GL_SetSwapInterval(1) && P.real_display;
    if (!cfg->vsync) SDL_GL_SetSwapInterval(0);
  } else if (cfg->gfx == ENC_GFX_NONE) {
    int vs = 0;
    P.ren = SDL_CreateRenderer(P.win, NULL);
    if (!P.ren) P.ren = SDL_CreateRenderer(P.win, SDL_SOFTWARE_RENDERER);
    if (!P.ren) {
      enc_log("encore-pc: renderer: %s", SDL_GetError());
      video_free();
      return ENC_ERR_SYSTEM;
    }
    if (cfg->vsync) SDL_SetRenderVSync(P.ren, 1);
    P.hw_vsync = cfg->vsync && SDL_GetRenderVSync(P.ren, &vs) && vs == 1 && P.real_display;
  }
  if (P.text_open) SDL_StartTextInput(P.win); /* a field opened before the window */
  drawable(&w, &h);
  P.info.display_width = w, P.info.display_height = h;
  P.vblank_hz = cfg->refresh_hz > 0 ? cfg->refresh_hz : 60;
  P.in_frame = 0;
  P.video_open = 1;
  return ENC_OK;
}

int enc_video_set_aspect(int aspect) {
  if (!P.video_open) return ENC_ERR_STATE;
  if ((unsigned)aspect > ENC_ASPECT_INTEGER) return ENC_ERR_ARG;
  P.vcfg.aspect = aspect;
  return ENC_OK;
}

/* after a swap: the display paces a source at its own rate when it really vsyncs; anything else is timed */
static void pace(void) {
  if (P.vcfg.vsync && (!P.hw_vsync || P.vblank_hz != P.info.refresh_hz)) enc_vblank_wait();
}

static void draw_last(void) {
  SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 255);
  SDL_RenderClear(P.ren);
  SDL_RenderTexture(P.ren, P.tex, NULL, &P.last_box);
}

int enc_video_present(const EncFrame *f) {
  int rc, w, h;
  EncRect full, box;
  if (!P.video_open || P.vcfg.gfx != ENC_GFX_NONE) return ENC_ERR_STATE;
  if (!f) return ENC_ERR_ARG;
  if (f->width <= 0 || f->height <= 0 || f->width > 8192 || f->height > 8192) return ENC_ERR_ARG;
  if (f->width != P.tex_w || f->height != P.tex_h) { /* a new source size: not per frame */
    size_t px = (size_t)f->width * (size_t)f->height;
    if (P.tex) SDL_DestroyTexture(P.tex);
    P.tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
    if (!P.tex) return ENC_ERR_SYSTEM;
    if (px > P.staging_px) {
      uint32_t *s = (uint32_t *)realloc(P.staging, px * sizeof *s);
      if (!s) return ENC_ERR_NOMEM;
      P.staging = s, P.staging_px = px;
    }
    P.tex_w = f->width, P.tex_h = f->height;
  }
  SDL_SetTextureScaleMode(P.tex, P.vcfg.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
  full.x = full.y = 0, full.w = f->width, full.h = f->height;
  rc = enc_blit_scaled(f, P.staging, f->width, f->height, f->width, full, 0); /* to 0x00RRGGBB, 1:1 */
  if (rc != ENC_OK) return rc;
  SDL_UpdateTexture(P.tex, NULL, P.staging, f->width * 4);
  drawable(&w, &h);
  box = enc_aspect_box(f->width, f->height, w, h, P.vcfg.aspect);
  P.last_box.x = (float)box.x, P.last_box.y = (float)box.y, P.last_box.w = (float)box.w, P.last_box.h = (float)box.h;
  draw_last();
  SDL_RenderPresent(P.ren);
  P.presents++;
  pace();
  return ENC_OK;
}

void enc_video_close(void) {
  if (!P.video_open) return;
  video_free();
  P.video_open = 0;
}

static int vk_create_surface(void *instance, void *surface) {
  if (!P.win || !instance || !surface) return ENC_ERR_ARG;
  return SDL_Vulkan_CreateSurface(P.win, (VkInstance)instance, NULL, (VkSurfaceKHR *)surface) ? ENC_OK : ENC_ERR_SYSTEM;
}

static void *gl_proc(const char *name) { return (void *)SDL_GL_GetProcAddress(name); }

int enc_video_gpu(EncGpu *g) {
  if (!g) return ENC_ERR_ARG;
  if (!P.video_open || P.vcfg.gfx == ENC_GFX_NONE) return ENC_ERR_STATE;
  memset(g, 0, sizeof *g);
  g->api = P.vcfg.gfx;
  g->major = P.vcfg.gfx_major, g->minor = P.vcfg.gfx_minor;
  g->window = P.win;
  if (P.vcfg.gfx == ENC_GFX_GL) {
    g->core_swaps = 1;
    g->context = P.gl;
    g->get_proc = gl_proc;
  } else { /* Vulkan */
    g->core_swaps = 0;
    g->get_proc = (void *(*)(const char *))SDL_Vulkan_GetVkGetInstanceProcAddr();
    g->vk_extensions = P.vk_ext;
    g->vk_extension_count = (int)P.vk_next;
    g->vk_create_surface = vk_create_surface;
  }
  return ENC_OK;
}

int enc_video_gpu_begin(EncGpuFrame *f) {
  if (!f) return ENC_ERR_ARG;
  if (!P.video_open || P.vcfg.gfx == ENC_GFX_NONE || P.in_frame) return ENC_ERR_STATE;
  memset(f, 0, sizeof *f);
  drawable(&f->width, &f->height);
  f->box = enc_aspect_box(P.vcfg.source_width, P.vcfg.source_height, f->width, f->height, P.vcfg.aspect);
  f->framebuffer = 0;
  if (P.vcfg.gfx == ENC_GFX_GL) SDL_GL_MakeCurrent(P.win, P.gl);
  P.in_frame = 1;
  return ENC_OK;
}

int enc_video_gpu_present(void) {
  if (!P.video_open || P.vcfg.gfx == ENC_GFX_NONE || !P.in_frame) return ENC_ERR_STATE;
  P.in_frame = 0;
  if (P.vcfg.gfx == ENC_GFX_GL) SDL_GL_SwapWindow(P.win);
  P.presents++;
  pace();
  return ENC_OK;
}

SDL_Window *enc_pc_window(void) { return P.win; }
uint64_t enc_pc_presents(void) { return P.presents; }

/* the last frame drawn again (the back buffer after a present is undefined) and read back before any present */
int enc_pc_read_pixels(uint32_t *out, int w, int h) {
  int rw, rh, y, ok;
  SDL_Surface *s, *c;
  if (!out) return ENC_ERR_ARG;
  if (!P.ren || !P.tex) return ENC_ERR_STATE;
  if (!SDL_GetCurrentRenderOutputSize(P.ren, &rw, &rh) || rw != w || rh != h) return ENC_ERR_ARG;
  draw_last();
  s = SDL_RenderReadPixels(P.ren, NULL);
  if (!s) return ENC_ERR_SYSTEM;
  c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
  SDL_DestroySurface(s);
  ok = c && c->w == w && c->h == h;
  if (ok)
    for (y = 0; y < h; y++) memcpy(out + (size_t)y * w, (const uint8_t *)c->pixels + (size_t)y * c->pitch, (size_t)w * 4);
  SDL_DestroySurface(c);
  return ok ? ENC_OK : ENC_ERR_SYSTEM;
}

static void toggle_fullscreen(void) {
  if (!P.win) return;
  P.fullscreen = !P.fullscreen;
  SDL_SetWindowFullscreen(P.win, P.fullscreen); /* no mode set: borderless, at the desktop's */
}

/* ---- input ---------------------------------------------------------------------------------------------------- */

static void pad_added(SDL_JoystickID id) {
  int i;
  SDL_Gamepad *gc;
  for (i = 0; i < PADS; i++)
    if (P.pad[i] && SDL_GetGamepadID(P.pad[i]) == id) return; /* already */
  for (i = 0; i < PADS && P.pad[i]; i++) {
  }
  if (i == PADS) return;
  gc = SDL_OpenGamepad(id);
  if (!gc) return;
  P.pad[i] = gc;
  P.has_motion[i] = SDL_GamepadHasSensor(gc, SDL_SENSOR_ACCEL) && SDL_GamepadHasSensor(gc, SDL_SENSOR_GYRO);
  if (P.has_motion[i]) {
    SDL_SetGamepadSensorEnabled(gc, SDL_SENSOR_ACCEL, true);
    SDL_SetGamepadSensorEnabled(gc, SDL_SENSOR_GYRO, true);
  }
  enc_log("encore-pc: pad %d: %s%s", i, SDL_GetGamepadName(gc) ? SDL_GetGamepadName(gc) : "?",
          P.has_motion[i] ? " (motion)" : "");
}

static void pad_removed(SDL_JoystickID id) {
  int i;
  for (i = 0; i < PADS; i++)
    if (P.pad[i] && SDL_GetGamepadID(P.pad[i]) == id) {
      SDL_CloseGamepad(P.pad[i]);
      P.pad[i] = NULL, P.has_motion[i] = 0;
      enc_log("encore-pc: pad %d gone", i);
    }
}

static void set_focus(int focused) {
  if (focused == P.focused) return;
  P.focused = focused;
  enc_event_emit(focused ? ENC_EVENT_FOCUS_GAINED : ENC_EVENT_FOCUS_LOST, 0, 0, 0);
}

static size_t utf8_chars(const char *s) {
  size_t n = 0;
  for (; *s; s++)
    if (((unsigned char)*s & 0xc0) != 0x80) n++;
  return n;
}

static void text_append(const char *s) {
  size_t have = strlen(P.text);
  for (; *s; ) { /* whole characters while they fit */
    size_t k = 1;
    while (s[k] && ((unsigned char)s[k] & 0xc0) == 0x80) k++;
    if (have + k >= sizeof P.text || (P.text_max && utf8_chars(P.text) >= (size_t)P.text_max)) return;
    memcpy(P.text + have, s, k);
    have += k;
    P.text[have] = 0;
    s += k;
  }
}

static void text_key(SDL_Keycode k, SDL_Keymod mod) {
  size_t n = strlen(P.text);
  if (k == SDLK_BACKSPACE) {
    while (n > 0 && ((unsigned char)P.text[n - 1] & 0xc0) == 0x80) n--;
    if (n > 0) n--;
    P.text[n] = 0;
  } else if (k == SDLK_ESCAPE)
    P.text_result = ENC_TEXT_CANCELLED;
  else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
    if (P.text_multiline && !(mod & SDL_KMOD_CTRL))
      text_append("\n");
    else
      P.text_result = ENC_TEXT_DONE;
  }
}

static void handle(const SDL_Event *e) {
  switch (e->type) {
  case SDL_EVENT_QUIT:
    if (!P.quit) enc_event_emit(ENC_EVENT_QUIT, 0, 0, 0);
    P.quit = 1;
    break;
  case SDL_EVENT_WINDOW_FOCUS_LOST:
  case SDL_EVENT_WINDOW_MINIMIZED: set_focus(0); break;
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
  case SDL_EVENT_WINDOW_RESTORED: set_focus(1); break;
  case SDL_EVENT_WINDOW_RESIZED:
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    if (P.video_open) {
      int w, h;
      drawable(&w, &h);
      if (w != P.info.display_width || h != P.info.display_height) {
        P.info.display_width = w, P.info.display_height = h;
        enc_event_emit(ENC_EVENT_DISPLAY, w, h, 0);
      }
    }
    break;
  case SDL_EVENT_GAMEPAD_ADDED: pad_added(e->gdevice.which); break;
  case SDL_EVENT_GAMEPAD_REMOVED: pad_removed(e->gdevice.which); break;
  case SDL_EVENT_TEXT_INPUT:
    if (P.text_open && P.text_result == ENC_TEXT_PENDING && e->text.text) text_append(e->text.text);
    break;
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    if (e->key.scancode < SDL_SCANCODE_COUNT) P.keys[e->key.scancode] = e->key.down;
    if (e->key.down && !e->key.repeat) {
      if (e->key.key == SDLK_F11 || (e->key.key == SDLK_RETURN && (e->key.mod & SDL_KMOD_ALT)))
        toggle_fullscreen();
      else if (P.text_open && P.text_result == ENC_TEXT_PENDING)
        text_key(e->key.key, e->key.mod);
    } else if (e->key.down && P.text_open && e->key.key == SDLK_BACKSPACE)
      text_key(SDLK_BACKSPACE, 0); /* held backspace repeats */
    break;
  default: break;
  }
}

static uint32_t keyboard_buttons(void) {
  static const struct { SDL_Scancode k; uint32_t b; } map[] = {
      {SDL_SCANCODE_UP, ENC_BTN_UP},       {SDL_SCANCODE_DOWN, ENC_BTN_DOWN}, {SDL_SCANCODE_LEFT, ENC_BTN_LEFT},
      {SDL_SCANCODE_RIGHT, ENC_BTN_RIGHT}, {SDL_SCANCODE_Z, ENC_BTN_SOUTH},   {SDL_SCANCODE_X, ENC_BTN_EAST},
      {SDL_SCANCODE_A, ENC_BTN_WEST},      {SDL_SCANCODE_S, ENC_BTN_NORTH},   {SDL_SCANCODE_Q, ENC_BTN_L1},
      {SDL_SCANCODE_W, ENC_BTN_R1},        {SDL_SCANCODE_1, ENC_BTN_L2},      {SDL_SCANCODE_3, ENC_BTN_R2},
      {SDL_SCANCODE_RETURN, ENC_BTN_START}, {SDL_SCANCODE_RSHIFT, ENC_BTN_SELECT},
      {SDL_SCANCODE_ESCAPE, ENC_BTN_MENU}};
  uint32_t b = 0;
  unsigned i;
  if (P.text_open) return 0; /* the keyboard is typing */
  for (i = 0; i < sizeof map / sizeof map[0]; i++)
    if (P.keys[map[i].k]) b |= map[i].b;
  return b;
}

int enc_input_poll(void) {
  static const struct { SDL_GamepadButton s; uint32_t b; } map[] = {
      {SDL_GAMEPAD_BUTTON_DPAD_UP, ENC_BTN_UP},        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, ENC_BTN_DOWN},
      {SDL_GAMEPAD_BUTTON_DPAD_LEFT, ENC_BTN_LEFT},    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, ENC_BTN_RIGHT},
      {SDL_GAMEPAD_BUTTON_SOUTH, ENC_BTN_SOUTH},       {SDL_GAMEPAD_BUTTON_EAST, ENC_BTN_EAST},
      {SDL_GAMEPAD_BUTTON_WEST, ENC_BTN_WEST},         {SDL_GAMEPAD_BUTTON_NORTH, ENC_BTN_NORTH},
      {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, ENC_BTN_L1},  {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, ENC_BTN_R1},
      {SDL_GAMEPAD_BUTTON_LEFT_STICK, ENC_BTN_L3},     {SDL_GAMEPAD_BUTTON_RIGHT_STICK, ENC_BTN_R3},
      {SDL_GAMEPAD_BUTTON_START, ENC_BTN_START},       {SDL_GAMEPAD_BUTTON_BACK, ENC_BTN_SELECT},
      {SDL_GAMEPAD_BUTTON_GUIDE, ENC_BTN_MENU}};
  SDL_Event e;
  uint64_t t;
  int i;
  unsigned j;
  if (!P.up) return ENC_ERR_STATE;
  while (SDL_PollEvent(&e)) handle(&e);
  t = enc_time_us();
  for (i = 0; i < PADS; i++) {
    SDL_Gamepad *gc = P.pad[i];
    EncPad *p = &P.pads[i];
    memset(p, 0, sizeof *p);
    if (gc) {
      int l2 = SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
      int r2 = SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
      for (j = 0; j < sizeof map / sizeof map[0]; j++)
        if (SDL_GetGamepadButton(gc, map[j].s)) p->buttons |= map[j].b;
      p->lx = SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_LEFTX);
      p->ly = SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_LEFTY);
      p->rx = SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_RIGHTX);
      p->ry = SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_RIGHTY);
      p->l2 = (uint8_t)(l2 > 0 ? l2 >> 7 : 0), p->r2 = (uint8_t)(r2 > 0 ? r2 >> 7 : 0);
      if (p->l2 > 30) p->buttons |= ENC_BTN_L2;
      if (p->r2 > 30) p->buttons |= ENC_BTN_R2;
      p->connected = 1;
      if (P.has_motion[i]) {
        float a[3], g[3];
        if (SDL_GetGamepadSensorData(gc, SDL_SENSOR_ACCEL, a, 3) && SDL_GetGamepadSensorData(gc, SDL_SENSOR_GYRO, g, 3)) {
          for (j = 0; j < 3; j++) P.motion[i].accel[j] = a[j] / (float)SDL_STANDARD_GRAVITY, P.motion[i].gyro[j] = g[j];
          P.motion[i].time_us = t;
        }
      }
    }
  }
  /* the keyboard is player 1 too */
  P.pads[0].buttons |= keyboard_buttons();
  if (P.pads[0].buttons & ENC_BTN_L2) P.pads[0].l2 = P.pads[0].l2 ? P.pads[0].l2 : 255;
  if (P.pads[0].buttons & ENC_BTN_R2) P.pads[0].r2 = P.pads[0].r2 ? P.pads[0].r2 : 255;
  P.pads[0].connected = 1;
  return ENC_OK;
}

int enc_pad_read(int port, EncPad *pad) {
  if (!pad || port < 0 || port >= PADS) return ENC_ERR_ARG;
  *pad = P.pads[port];
  return ENC_OK;
}

int enc_touch_read(int panel, EncTouch *pts, int max) {
  if (panel != ENC_TOUCH_FRONT && panel != ENC_TOUCH_REAR) return ENC_ERR_ARG;
  if (max < 0 || (max && !pts)) return ENC_ERR_ARG;
  return ENC_ERR_UNSUPPORTED;
}

int enc_pad_rumble(int port, int low, int high, int duration_ms) {
  if (port < 0 || port >= PADS) return ENC_ERR_ARG;
  if (low < 0 || low > 65535 || high < 0 || high > 65535 || duration_ms < 0) return ENC_ERR_ARG;
  if (!P.pad[port]) return ENC_ERR_UNSUPPORTED;
  /* SDL stops a rumble after its duration: "until the next call" is its longest */
  if (!SDL_RumbleGamepad(P.pad[port], (Uint16)low, (Uint16)high, duration_ms ? (Uint32)duration_ms : 0xffffffffu))
    return ENC_ERR_UNSUPPORTED;
  return ENC_OK;
}

int enc_motion_read(int port, EncMotion *m) {
  if (!m || port < 0 || port >= PADS) return ENC_ERR_ARG;
  if (!P.pad[port] || !P.has_motion[port]) return ENC_ERR_UNSUPPORTED;
  *m = P.motion[port];
  return ENC_OK;
}

/* ---- text input: the keyboard, drawn by the game ----------------------------------------------------------------- */

static void copy_utf8(char *out, size_t cap, const char *in) {
  size_t n = strlen(in);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)in[n] & 0xc0) == 0x80) n--;
  }
  memcpy(out, in, n);
  out[n] = 0;
}

int enc_text_input_begin(const EncTextConfig *cfg) {
  if (!P.up) return ENC_ERR_STATE;
  if (!cfg || cfg->max_chars < 0 || cfg->kind < ENC_TEXT_PLAIN || cfg->kind > ENC_TEXT_MULTILINE) return ENC_ERR_ARG;
  if (P.text_open) return ENC_ERR_STATE;
  P.text[0] = 0;
  P.text_max = cfg->max_chars;
  P.text_multiline = cfg->kind == ENC_TEXT_MULTILINE;
  if (cfg->initial) text_append(cfg->initial);
  P.text_open = 1, P.text_result = ENC_TEXT_PENDING;
  if (P.win) SDL_StartTextInput(P.win); /* else when the window opens */
  return ENC_OK;
}

int enc_text_input_poll(char *out, size_t cap) {
  int r;
  if (!out || !cap) return ENC_ERR_ARG;
  if (!P.text_open) return ENC_ERR_STATE;
  copy_utf8(out, cap, P.text);
  r = P.text_result;
  if (r != ENC_TEXT_PENDING) enc_text_input_cancel();
  return r;
}

void enc_text_input_cancel(void) {
  if (!P.text_open) return;
  P.text_open = 0;
  if (P.win) SDL_StopTextInput(P.win);
}

/* ---- audio ---------------------------------------------------------------------------------------------------- */

static struct {
  SDL_AudioStream *dev;
  int channels, volume, cap; /* cap: frames the queue may hold */
  int16_t scratch[4096];
} A;

int enc_audio_open(const EncAudioConfig *cfg) {
  SDL_AudioSpec want;
  int ms;
  if (!P.up) return ENC_ERR_STATE;
  if (!cfg || cfg->rate < 4000 || cfg->rate > 192000 || cfg->channels < 1 || cfg->channels > 2) return ENC_ERR_ARG;
  if (A.dev) return ENC_ERR_STATE;
  SDL_zero(want);
  want.freq = cfg->rate;
  want.format = SDL_AUDIO_S16;
  want.channels = cfg->channels;
  /* a stream in the game's rate and channels: SDL converts to the device's itself */
  A.dev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
  if (!A.dev) {
    enc_log("encore-pc: audio: %s", SDL_GetError());
    return ENC_ERR_SYSTEM;
  }
  ms = cfg->latency_ms > 0 ? cfg->latency_ms : 64;
  A.channels = cfg->channels, A.volume = 100;
  A.cap = (int)((long)cfg->rate * ms * 2 / 1000) + 256;
  SDL_ResumeAudioStreamDevice(A.dev); /* opened paused */
  return ENC_OK;
}

int enc_audio_queued(void) {
  int b;
  if (!A.dev) return ENC_ERR_STATE;
  b = SDL_GetAudioStreamQueued(A.dev);
  return b > 0 ? b / (2 * A.channels) : 0;
}

int enc_audio_push(const int16_t *pcm, int frames) {
  int room, n, done = 0;
  if (!A.dev) return ENC_ERR_STATE;
  if (frames < 0 || (frames && !pcm)) return ENC_ERR_ARG;
  room = A.cap - enc_audio_queued();
  n = room < frames ? (room > 0 ? room : 0) : frames;
  while (done < n) { /* scaled by the volume a chunk at a time: no allocation */
    int k = n - done, i, per = (int)(sizeof A.scratch / sizeof A.scratch[0]) / A.channels;
    if (k > per) k = per;
    for (i = 0; i < k * A.channels; i++)
      A.scratch[i] = (int16_t)((int)pcm[(size_t)done * A.channels + i] * A.volume / 100);
    if (!SDL_PutAudioStreamData(A.dev, A.scratch, k * A.channels * 2)) break;
    done += k;
  }
  return done;
}

int enc_audio_set_volume(int percent) {
  if (!A.dev) return ENC_ERR_STATE;
  A.volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  return ENC_OK;
}

void enc_audio_close(void) {
  if (!A.dev) return;
  SDL_DestroyAudioStream(A.dev); /* and the device it opened */
  A.dev = NULL;
}

/* ---- files ---------------------------------------------------------------------------------------------------- */

int enc_path(int dir, const char *name, char *out, size_t cap) {
  static const char *const sub[] = {"data", "save", "config", "log", "cache"};
  char d[900];
  int n;
  if (!P.up) return ENC_ERR_STATE;
  if ((unsigned)dir > ENC_DIR_CACHE || !name || !out || !cap) return ENC_ERR_ARG;
  if (strstr(name, "..")) return ENC_ERR_ARG;
  if (dir == ENC_DIR_DATA)
    snprintf(d, sizeof d, "%s", P.data); /* the player's folder: never created over an install */
  else {
    snprintf(d, sizeof d, "%s/%s", P.root, sub[dir]);
    if (mkdirs(d) != 0) return ENC_ERR_IO;
  }
  n = name[0] ? snprintf(out, cap, "%s/%s", d, name) : snprintf(out, cap, "%s", d);
  return n < 0 || (size_t)n >= cap ? ENC_ERR_ARG : ENC_OK;
}

/* ---- timing --------------------------------------------------------------------------------------------------- */

uint64_t enc_time_us(void) { return now_us(); }

void enc_sleep_us(uint64_t us) { SDL_DelayPrecise(us * 1000u); /* sleeps, then spins the last bit */ }

uint64_t enc_vblank_count(void) {
  if (!P.up) return 0;
  return now_us() * (uint64_t)P.vblank_hz / 1000000u;
}

int enc_vblank_wait(void) {
  uint64_t next, at, now;
  if (!P.up) return ENC_ERR_STATE;
  next = enc_vblank_count() + 1;
  at = (next * 1000000u + (uint64_t)P.vblank_hz - 1) / (uint64_t)P.vblank_hz;
  while ((now = now_us()) < at) enc_sleep_us(at - now);
  return ENC_OK;
}

/* ---- threads -------------------------------------------------------------------------------------------------- */

struct EncThread {
  SDL_Thread *t;
  void (*fn)(void *);
  void *arg;
  int priority;
};

static int thread_main(void *p) {
  static const SDL_ThreadPriority prio[] = {SDL_THREAD_PRIORITY_LOW, SDL_THREAD_PRIORITY_NORMAL,
                                            SDL_THREAD_PRIORITY_HIGH, SDL_THREAD_PRIORITY_TIME_CRITICAL};
  EncThread *t = (EncThread *)p;
  if (t->priority != ENC_PRIO_NORMAL) SDL_SetCurrentThreadPriority(prio[t->priority]); /* a hint: may be refused */
  t->fn(t->arg);
  return 0;
}

int enc_thread_start(EncThread **out, const EncThreadConfig *cfg, void (*fn)(void *arg), void *arg) {
  EncThread *t;
  SDL_PropertiesID props;
  size_t stack = cfg && cfg->stack ? cfg->stack : (256u << 10);
  if (!out || !fn) return ENC_ERR_ARG;
  *out = NULL;
  t = (EncThread *)calloc(1, sizeof *t);
  if (!t) return ENC_ERR_NOMEM;
  t->fn = fn, t->arg = arg;
  t->priority = cfg && cfg->priority >= ENC_PRIO_LOW && cfg->priority <= ENC_PRIO_AUDIO ? cfg->priority : ENC_PRIO_NORMAL;
  if (stack < (64u << 10)) stack = 64u << 10;
  props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)thread_main);
  SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, cfg && cfg->name ? cfg->name : "encore");
  SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, t);
  SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, (Sint64)stack);
  t->t = SDL_CreateThreadWithProperties(props);
  SDL_DestroyProperties(props);
  if (!t->t) {
    free(t);
    return ENC_ERR_SYSTEM;
  }
  *out = t;
  return ENC_OK;
}

int enc_thread_join(EncThread *t) {
  if (!t) return ENC_ERR_ARG;
  SDL_WaitThread(t->t, NULL);
  free(t);
  return ENC_OK;
}

struct EncMutex {
  SDL_Mutex *m;
};

int enc_mutex_create(EncMutex **m) {
  if (!m) return ENC_ERR_ARG;
  *m = (EncMutex *)malloc(sizeof **m);
  if (!*m) return ENC_ERR_NOMEM;
  if (!((*m)->m = SDL_CreateMutex())) {
    free(*m);
    *m = NULL;
    return ENC_ERR_SYSTEM;
  }
  return ENC_OK;
}
void enc_mutex_lock(EncMutex *m) { SDL_LockMutex(m->m); }
void enc_mutex_unlock(EncMutex *m) { SDL_UnlockMutex(m->m); }
void enc_mutex_destroy(EncMutex *m) {
  if (!m) return;
  SDL_DestroyMutex(m->m);
  free(m);
}

struct EncSem {
  SDL_Semaphore *s;
};

int enc_sem_create(EncSem **s, int initial) {
  if (!s || initial < 0) return ENC_ERR_ARG;
  *s = (EncSem *)malloc(sizeof **s);
  if (!*s) return ENC_ERR_NOMEM;
  if (!((*s)->s = SDL_CreateSemaphore((Uint32)initial))) {
    free(*s);
    *s = NULL;
    return ENC_ERR_SYSTEM;
  }
  return ENC_OK;
}
void enc_sem_post(EncSem *s) { SDL_SignalSemaphore(s->s); }
int enc_sem_wait(EncSem *s, int64_t timeout_us) {
  if (!s) return ENC_ERR_ARG;
  if (timeout_us < 0) {
    SDL_WaitSemaphore(s->s);
    return ENC_OK;
  }
  /* milliseconds, rounded up; false only on a timeout */
  return SDL_WaitSemaphoreTimeout(s->s, (Sint32)((timeout_us + 999) / 1000)) ? ENC_OK : ENC_ERR_STATE;
}
void enc_sem_destroy(EncSem *s) {
  if (!s) return;
  SDL_DestroySemaphore(s->s);
  free(s);
}

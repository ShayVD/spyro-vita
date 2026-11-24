/* Encore's platform interface: the one set of calls every target implements (docs/platform-interface.md).
 *
 * A port's platform layer (the code under the game that stands in for the source console: its pad, its sound
 * chip, its display, its disc) is written once against these calls; each target supplies a backend:
 *   core/host/enc_host.c          a headless POSIX host (tests, CI, a debugging build)
 *   targets/vita/encore/          the Vita, over targets/vita/runtime's vpk_ modules
 *   targets/pc/, targets/ps4/, targets/switch/: to come (targets/README.md)
 *
 * Rules every backend keeps:
 *   - Calls return ENC_OK (0) or a negative EncError; nothing else is an error. A call a target cannot do returns
 *     ENC_ERR_UNSUPPORTED and the game carries on (enc_platform_info()->caps says which up front).
 *   - Call enc_platform_init first and enc_platform_shutdown last, from the main thread. Video and input calls
 *     come from one thread (the game's); audio pushes from one thread; the rest are thread-safe.
 *   - No call allocates per frame. No call blocks unless its comment says so.
 *   - Struct fields and parameters that take an enum's values are int: the layout must not depend on
 *     -fshort-enums (kh1-vita builds the runtime with it, most builds do not).
 *   - Files are plain C stdio once enc_path has named them: every target's libc has fopen.
 *
 * The GPU stays the game's: a renderer that translates the source GPU (the PS2 GS to the Vita's GXM, PS1 primitives
 * to GL) talks to the target's graphics API directly. This interface owns the window or screen, the display
 * buffers where the API lets it, the swap, the pacing and the aspect box: the CPU-picture path every port starts
 * with (enc_video_present), and a GPU mode (EncVideoConfig.gfx, enc_video_gpu_*) where the game draws with the
 * target's own API and core does the rest. */
#ifndef ENCORE_PLATFORM_H
#define ENCORE_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENC_PLATFORM_API 2 /* bumped on any change that breaks a backend or a caller (docs/versioning.md) */

typedef enum {
  ENC_OK = 0,
  ENC_ERR_UNSUPPORTED = -1, /* this target has no such thing */
  ENC_ERR_ARG = -2,         /* a bad argument */
  ENC_ERR_IO = -3,          /* a file or device failed */
  ENC_ERR_NOMEM = -4,
  ENC_ERR_SYSTEM = -5,      /* the target's SDK refused; the log has its code */
  ENC_ERR_STATE = -6        /* called in the wrong order (not open, already open) */
} EncError;

const char *enc_error_name(int err);

/* ---- lifecycle ------------------------------------------------------------------------------------------------ */

enum { /* EncPlatformInfo.caps */
  ENC_CAP_WINDOW = 1u << 0,      /* a resizable desktop window (else a fixed screen) */
  ENC_CAP_TOUCH_FRONT = 1u << 1,
  ENC_CAP_TOUCH_REAR = 1u << 2,
  ENC_CAP_ANALOG = 1u << 3,
  ENC_CAP_TRIGGERS_ANALOG = 1u << 4,
  ENC_CAP_RUMBLE = 1u << 5,
  ENC_CAP_KEYBOARD = 1u << 6,
  ENC_CAP_HEADLESS = 1u << 7,    /* no display at all: frames go nowhere (or to files) */
  ENC_CAP_COROUTINES = 1u << 8,
  ENC_CAP_MOTION = 1u << 9,      /* an accelerometer and a gyroscope (enc_motion_read) */
  ENC_CAP_TEXT_INPUT = 1u << 10  /* text entry: a system keyboard, or a physical one the game draws for */
};

typedef struct {
  const char *game;    /* short name, [a-z0-9_-]: the data folder, the log's name, the window title's default */
  const char *title;   /* human name for a window or notifications; NULL = game */
  int argc;            /* the host's arguments, when it has them (else 0, NULL) */
  char **argv;
} EncPlatformConfig;

typedef struct {
  const char *target;  /* "host", "vita", "ps4", "pc", "switch" */
  uint32_t caps;       /* ENC_CAP_* */
  int cpu_cores;       /* cores the game may use */
  int display_width;   /* the screen or the window at open, pixels */
  int display_height;
  int refresh_hz;      /* the display's, rounded */
  uint32_t gfx_apis;   /* 1u << ENC_GFX_*: the graphics APIs a GPU-mode enc_video_open can take (0: CPU frames only) */
} EncPlatformInfo;

int enc_platform_init(const EncPlatformConfig *cfg);
const EncPlatformInfo *enc_platform_info(void);
int enc_platform_should_quit(void);  /* 1 once the user closed the window / asked the system to end the app */
void enc_platform_shutdown(void);

void enc_log(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void enc_log_flush(void); /* before anything that may crash: a crash loses what is queued */

/* System events. A backend gathers them in enc_input_poll and hands them over on the game's thread, never from a
 * system callback: to the handler if one is set (inside enc_input_poll), else into a queue of at least 16 that
 * enc_event_poll empties (the oldest are dropped when it is full, and the log says so).
 *
 *   SUSPEND / RESUME   the system stops the whole app and later continues it: the Vita's sleep and PS button, PS4 /
 *                      PS5 rest mode, the Switch's sleep. Where the system warns first (the PC, the Switch) SUSPEND
 *                      comes before; where it freezes the app mid-frame (the Vita), SUSPEND and RESUME both come at
 *                      the first poll after it wakes. Save on SUSPEND if the target warns; never rely on it.
 *   FOCUS_LOST / _GAINED  the app keeps running but the player is elsewhere: the Switch's HOME menu, the PS4 / PS5
 *                      overlay, a PC window in the background or minimised. A game usually pauses itself.
 *   QUIT               enc_platform_should_quit() turned 1.
 *   DISPLAY            the drawable changed size (a PC window, a docked Switch): width x height, the size the next
 *                      enc_video_gpu_begin reports (CPU frames are rescaled by the backend).
 *
 * Across a suspend (rules every backend keeps, checked by the host's conformance run):
 *   - enc_time_us and enc_vblank_count stand still from SUSPEND to RESUME: the time spent suspended is not counted,
 *     so a catch-up loop does not run the missed vblanks. RESUME's suspended_us says how long it was (0: unknown).
 *   - audio: the device is paused at SUSPEND; at RESUME whatever was queued is dropped (enc_audio_queued() reads 0)
 *     and the device plays on. Focus changes leave audio alone: the game mutes or pauses if it wants to. */
typedef enum {
  ENC_EVENT_NONE = 0,
  ENC_EVENT_SUSPEND,
  ENC_EVENT_RESUME,
  ENC_EVENT_FOCUS_LOST,
  ENC_EVENT_FOCUS_GAINED,
  ENC_EVENT_QUIT,
  ENC_EVENT_DISPLAY
} EncEventType;

typedef struct {
  int type;               /* an EncEventType */
  int width, height;      /* DISPLAY: the new drawable size, pixels */
  uint64_t time_us;       /* enc_time_us() when it was gathered */
  uint64_t suspended_us;  /* RESUME: how long the app was stopped, as far as the backend knows (0: unknown) */
} EncEvent;

int enc_event_poll(EncEvent *ev); /* 1 with the oldest queued event in ev, 0 when none */
/* Events go to fn (on the thread that calls enc_input_poll, inside it) instead of the queue; NULL goes back to the
 * queue. Events already queued stay queued. */
void enc_event_set_handler(void (*fn)(const EncEvent *ev, void *user), void *user);

/* ---- window and present --------------------------------------------------------------------------------------- */

typedef enum {
  ENC_PIX_XRGB8888 = 0, /* 32-bit, 0x00RRGGBB in a uint32_t */
  ENC_PIX_XBGR8888,     /* 32-bit, 0x00BBGGRR (PS1 / PS2 GPU order) */
  ENC_PIX_RGB565,       /* 16-bit */
  ENC_PIX_XBGR1555      /* 16-bit, PS1 VRAM order: R in bits 0-4 */
} EncPixelFormat;

typedef enum {
  ENC_ASPECT_4_3 = 0,   /* the source picture as a 4:3 box, centred, black bars */
  ENC_ASPECT_16_9,      /* a 16:9 box (a game rendering wide) */
  ENC_ASPECT_STRETCH,   /* the whole display */
  ENC_ASPECT_INTEGER    /* the largest whole multiple of the source size that fits (pixel art) */
} EncAspect;

typedef enum { /* EncVideoConfig.gfx, EncPlatformInfo.gfx_apis (as 1u << value) */
  ENC_GFX_NONE = 0, /* CPU frames: enc_video_present */
  ENC_GFX_GL,       /* desktop OpenGL, core profile gfx_major.gfx_minor (default 3.3): the PC, Mesa on the Switch */
  ENC_GFX_GLES,     /* OpenGL ES: vitaGL on the Vita (2.0) */
  ENC_GFX_VULKAN,   /* the PC (where a loader is installed), the Switch */
  ENC_GFX_GXM,      /* the Vita's sceGxm */
  ENC_GFX_DEKO3D,   /* the Switch's */
  ENC_GFX_GNM       /* the PS4's and the PS5's (through the PS4 package) */
} EncGfxApi;

typedef struct {
  int source_width, source_height; /* the picture the game draws, before scaling */
  int refresh_hz;                  /* the source's rate: 60 (NTSC) or 50 (PAL); the vblank paces to it */
  int aspect;                      /* an EncAspect */
  int vsync;                       /* 1 = present waits for the display */
  int smooth;                      /* 1 = bilinear scaling, 0 = nearest (CPU frames) */
  int gfx;                         /* an EncGfxApi: NONE for CPU frames, else the GPU mode with that API */
  int gfx_major, gfx_minor;        /* the API version wanted (GL 3.3, GLES 2.0 ...); 0, 0 = the backend's default */
} EncVideoConfig;

typedef struct {
  const void *pixels;
  int width, height;
  int stride;            /* bytes between rows */
  int format;            /* an EncPixelFormat */
} EncFrame;

/* ENC_ERR_UNSUPPORTED for a gfx the target lacks (not in enc_platform_info()->gfx_apis). */
int enc_video_open(const EncVideoConfig *cfg);
int enc_video_set_aspect(int aspect); /* an EncAspect */ /* at any time (a menu option) */
/* Scales frame into the aspect box and shows it on the next vblank (cfg.vsync) or at once. Blocks at most for a
 * free buffer. The game owns frame->pixels again when this returns. CPU frames only: ENC_ERR_STATE in GPU mode. */
int enc_video_present(const EncFrame *frame);
void enc_video_close(void);

/* The box a source picture of src_w x src_h takes on a dst_w x dst_h display: portable, the same on every target
 * (core/src/enc_common.c). */
typedef struct { int x, y, w, h; } EncRect;
EncRect enc_aspect_box(int src_w, int src_h, int dst_w, int dst_h, int aspect);

/* ---- GPU mode: the game draws with the target's API, core keeps the window, the buffers, the swap, the pacing and
 * the aspect box. Per frame:
 *     enc_video_gpu_begin(&f)       which buffer, how big, where the picture goes (f.box)
 *     ... the game draws into it with the API (GL: framebuffer f.framebuffer, viewport f.box; GXM: a scene on
 *         f.surface with f.sync) ...
 *     [the game queues the swap itself when EncGpu.core_swaps is 0: vkQueuePresentKHR, vglSwapBuffers,
 *      dkQueuePresentImage]
 *     enc_video_gpu_present()       swaps (core_swaps), then paces: with vsync it returns at the source's next
 *                                   vblank when the source and display rates differ (50 Hz on 60)
 * Who owns what, per API:
 *   GL (PC)       core: the window and the context, current on the video thread; the game: everything drawn.
 *                 get_proc is the GL loader (SDL_GL_GetProcAddress). core_swaps = 1.
 *   Vulkan (PC)   core: the window; vk_extensions and vk_create_surface give the game what its VkInstance and
 *                 VkSurfaceKHR need; get_proc is vkGetInstanceProcAddr. The game owns the instance, device and
 *                 swapchain, and presents. core_swaps = 0.
 *   GLES (Vita)   vitaGL owns the GPU and its display queue: the game calls vglInit before enc_video_open and
 *                 vglSwapBuffers before enc_video_gpu_present (GL_TRUE while enc_text_input_poll is pending, so
 *                 the keyboard draws). core_swaps = 0.
 *   GXM (Vita)    core: sceGxmInitialize with its display queue, the display buffers in CDRAM (A8B8G8R8, mapped
 *                 for the GPU), their colour surfaces and sync objects, and the swap (sceGxmDisplayQueueAddEntry).
 *                 The game: its context, render target, depth buffer, shaders, and a scene per frame on f.surface
 *                 with f.sync as the fragment sync object, ended before enc_video_gpu_present. core_swaps = 1.
 *   deko3d (Switch) core: the NWindow (window); the game: the device, swapchain and presents. core_swaps = 0.
 *   GNM (PS4)     core: the video-out buffers (surface, index); the game: the command buffers. core_swaps = 1.
 * The pointers are the API's own types behind void *: a game includes the API's header and casts. */
typedef struct {
  int api;              /* the EncGfxApi opened */
  int major, minor;     /* the version given (GL: the context's) */
  int core_swaps;       /* 1: enc_video_gpu_present swaps; 0: the game queues the swap with its API first */
  int buffers;          /* display buffers core owns (GXM, GNM: 2 or more), 0 when the API owns them */
  void *window;         /* SDL_Window * (PC), NWindow * (Switch), else NULL */
  void *context;        /* GL: the SDL_GLContext / EGLContext, else NULL */
  void *(*get_proc)(const char *name); /* GL: the function loader; Vulkan: vkGetInstanceProcAddr (cast); else NULL */
  const char *const *vk_extensions;    /* Vulkan: the instance extensions the window surface needs */
  int vk_extension_count;
  /* Vulkan: makes the window's surface: instance is a VkInstance, surface a VkSurfaceKHR * to fill. */
  int (*vk_create_surface)(void *instance, void *surface);
} EncGpu;

typedef struct {
  int width, height;    /* the drawable now, pixels: a PC window may change between frames (ENC_EVENT_DISPLAY) */
  EncRect box;          /* where the source picture goes on it: enc_aspect_box(source, drawable, aspect) */
  int index;            /* the display buffer (core-owned buffers), else 0 */
  uint32_t framebuffer; /* GL / GLES: the framebuffer object to draw into (0: the default one) */
  void *surface;        /* GXM: the buffer's SceGxmColorSurface *; GNM: its render target; else NULL */
  void *sync;           /* GXM: the buffer's SceGxmSyncObject * (sceGxmBeginScene's fragment sync); else NULL */
  void *pixels;         /* GXM: the buffer's memory, A8B8G8R8, stride_px pixels a row; else NULL */
  int stride_px;
} EncGpuFrame;

int enc_video_gpu(EncGpu *gpu);           /* after a GPU-mode open: the handles (ENC_ERR_STATE otherwise) */
/* Before the game draws a frame: blocks at most for a free display buffer. ENC_ERR_STATE when the last begin has
 * not been presented yet, or when the video is not open in GPU mode. */
int enc_video_gpu_begin(EncGpuFrame *frame);
int enc_video_gpu_present(void);          /* after the frame is drawn (see above); ENC_ERR_STATE without a begin */

/* ---- input ---------------------------------------------------------------------------------------------------- */

enum { /* EncPad.buttons: positions, not labels (SOUTH is Cross on PlayStation, A on Xbox, B on Nintendo) */
  ENC_BTN_UP = 1u << 0, ENC_BTN_DOWN = 1u << 1, ENC_BTN_LEFT = 1u << 2, ENC_BTN_RIGHT = 1u << 3,
  ENC_BTN_SOUTH = 1u << 4, ENC_BTN_EAST = 1u << 5, ENC_BTN_WEST = 1u << 6, ENC_BTN_NORTH = 1u << 7,
  ENC_BTN_L1 = 1u << 8, ENC_BTN_R1 = 1u << 9, ENC_BTN_L2 = 1u << 10, ENC_BTN_R2 = 1u << 11,
  ENC_BTN_L3 = 1u << 12, ENC_BTN_R3 = 1u << 13,
  ENC_BTN_START = 1u << 14, ENC_BTN_SELECT = 1u << 15,
  ENC_BTN_MENU = 1u << 16   /* the port's own menu (a hotkey, a rear-touch tap): never passed to the game */
};

typedef struct {
  uint32_t buttons;       /* ENC_BTN_*, active high */
  int16_t lx, ly, rx, ry; /* sticks, -32768..32767, +y down; 0 when the pad has none */
  uint8_t l2, r2;         /* analog triggers 0..255 (255 when only digital and pressed) */
  uint8_t connected;
} EncPad;

typedef struct {
  int id;       /* stable while the finger stays down */
  float x, y;   /* 0..1 of the panel, +y down */
} EncTouch;

enum { ENC_TOUCH_FRONT = 0, ENC_TOUCH_REAR = 1 };

int enc_input_poll(void);                        /* once a frame, before the reads: samples every device */
int enc_pad_read(int port, EncPad *pad);         /* port 0 = player 1; ENC_ERR_ARG past the last port */
int enc_touch_read(int panel, EncTouch *pts, int max); /* points down now (>= 0), or ENC_ERR_UNSUPPORTED */

/* Optional devices. Each returns ENC_ERR_UNSUPPORTED where the target has none (its ENC_CAP_* bit is clear) or where
 * this port's pad has none (a PC pad without motors), and ENC_ERR_ARG past the last port. */

/* Rumble: low and high are the strengths (0..65535) of the low- and high-frequency motors, held for duration_ms
 * (0 = until the next call); 0, 0 stops. Needs ENC_CAP_RUMBLE. Never blocks. */
int enc_pad_rumble(int port, int low, int high, int duration_ms);

/* Motion, as sampled by the last enc_input_poll. Axes as the player holds the device (port 0 on a handheld is the
 * console itself, else that player's pad): +x right, +y up, +z out of the screen / pad towards the player. accel in
 * g (1.0 = Earth's gravity; a device lying flat, screen up, reads about +1 on z), gyro in radians a second about
 * those axes, counter-clockwise positive (right-handed). Needs ENC_CAP_MOTION. */
typedef struct {
  float accel[3];
  float gyro[3];
  uint64_t time_us;  /* enc_time_us() of the sample */
} EncMotion;
int enc_motion_read(int port, EncMotion *motion);

/* Text input: the system's on-screen keyboard (the Vita's IME dialog, the Switch's swkbd, the PS4's IME), or on a PC
 * the physical keyboard, where the game draws the text so far itself. Needs ENC_CAP_TEXT_INPUT. One at a time:
 *     enc_text_input_begin(&cfg)    ENC_ERR_STATE while one is open
 *     each frame: r = enc_text_input_poll(buf, sizeof buf)  and keep presenting frames while r == ENC_TEXT_PENDING
 *                 (buf holds the text so far where the backend knows it)
 *     r == ENC_TEXT_DONE: buf is the text (UTF-8, NUL-terminated, cut to fit); ENC_TEXT_CANCELLED: the player
 *     backed out. Either closes it; a poll after that is ENC_ERR_STATE. enc_text_input_cancel closes it early. */
enum { ENC_TEXT_PENDING = 0, ENC_TEXT_DONE = 1, ENC_TEXT_CANCELLED = 2 };
enum { ENC_TEXT_PLAIN = 0, ENC_TEXT_NUMBER, ENC_TEXT_PASSWORD, ENC_TEXT_MULTILINE }; /* EncTextConfig.kind */
typedef struct {
  const char *title;    /* the prompt shown above the field (UTF-8), or NULL */
  const char *initial;  /* the text the field starts with (UTF-8), or NULL */
  int max_chars;        /* characters, not bytes; 0 = the backend's (at least 64) */
  int kind;             /* ENC_TEXT_PLAIN ... */
} EncTextConfig;
int enc_text_input_begin(const EncTextConfig *cfg);
int enc_text_input_poll(char *out, size_t cap);
void enc_text_input_cancel(void);

/* ---- audio ---------------------------------------------------------------------------------------------------- */

typedef struct {
  int rate;           /* the game's rate (44100, 48000, 22050 ...): the backend resamples to the device's */
  int channels;       /* 1 or 2 */
  int latency_ms;     /* what the backend should keep queued, about (0 = its default) */
} EncAudioConfig;

int enc_audio_open(const EncAudioConfig *cfg);
/* Queues interleaved signed 16-bit frames: the frames taken (>= 0; fewer than given when the queue is full, never
 * blocking). */
int enc_audio_push(const int16_t *pcm, int frames);
int enc_audio_queued(void);         /* frames queued and not yet played (the game's pacing reads it) */
int enc_audio_set_volume(int percent);
void enc_audio_close(void);

/* ---- files and disc ------------------------------------------------------------------------------------------- */

typedef enum {
  ENC_DIR_DATA = 0, /* where the player's own game data lives (a disc image, extracted files): read only */
  ENC_DIR_SAVE,     /* saves (a memory card image, save slots) */
  ENC_DIR_CONFIG,   /* config.txt and the like */
  ENC_DIR_LOG,
  ENC_DIR_CACHE     /* anything that may be rebuilt (shader caches, upscaled textures) */
} EncDir;

/* The full path of name inside dir (name may be "" for the directory itself, and may hold '/'), created as a
 * directory for every dir but DATA. ENC_ERR_ARG when it does not fit in cap. */
int enc_path(int dir, const char *name, char *out, size_t cap); /* dir: an EncDir */

typedef struct EncDisc EncDisc;
/* A CD / DVD image (an ISO 9660 volume) by path: .iso (2048-byte sectors) or a raw .bin (2352), sniffed. Portable
 * C on every target (core/src/enc_disc.c, over core/src/enc_discimage.c). */
int enc_disc_open(EncDisc **disc, const char *path);
int enc_disc_read(EncDisc *disc, int lba, int count, void *buf);   /* 2048-byte user data sectors */
int enc_disc_find(EncDisc *disc, const char *iso_path, uint32_t *lba, uint32_t *size); /* ISO 9660 lookup */
void enc_disc_close(EncDisc *disc);

/* ---- timing --------------------------------------------------------------------------------------------------- */

uint64_t enc_time_us(void);         /* monotonic, from an arbitrary start */
void enc_sleep_us(uint64_t us);
/* The source's vblank (EncVideoConfig.refresh_hz, 60 before a video open): the count since init, and a wait for the
 * next one. On a display with the same rate it is the display's; otherwise it is timed. */
uint64_t enc_vblank_count(void);
int enc_vblank_wait(void);

/* ---- threads and coroutines ----------------------------------------------------------------------------------- */

typedef enum { ENC_PRIO_LOW = 0, ENC_PRIO_NORMAL, ENC_PRIO_HIGH, ENC_PRIO_AUDIO } EncPriority;

typedef struct EncThread EncThread;
typedef struct {
  const char *name;
  size_t stack;       /* bytes; 0 = the backend's default (>= 64 KiB) */
  int priority;       /* an EncPriority */
  int core;           /* a core to prefer, or -1 */
} EncThreadConfig;

int enc_thread_start(EncThread **t, const EncThreadConfig *cfg, void (*fn)(void *arg), void *arg);
int enc_thread_join(EncThread *t); /* waits for fn to return, then frees t */

typedef struct EncMutex EncMutex;
int enc_mutex_create(EncMutex **m);
void enc_mutex_lock(EncMutex *m);
void enc_mutex_unlock(EncMutex *m);
void enc_mutex_destroy(EncMutex *m);

typedef struct EncSem EncSem;
int enc_sem_create(EncSem **s, int initial);
void enc_sem_post(EncSem *s);
int enc_sem_wait(EncSem *s, int64_t timeout_us);  /* ENC_OK, or ENC_ERR_STATE on a timeout; timeout < 0 = forever */
void enc_sem_destroy(EncSem *s);

/* Coroutines: cooperative threads on the caller's thread, for a source console's threads that the game switches
 * between itself (PS2 EE threads, a PS1 game's tasks). enc_co_resume runs co until it calls enc_co_yield or its fn
 * returns; it returns 1 if co can resume again, 0 once fn has returned. Needs ENC_CAP_COROUTINES. */
typedef struct EncCo EncCo;
int enc_co_create(EncCo **co, void (*fn)(void *arg), void *arg, size_t stack);
int enc_co_resume(EncCo *co);
void enc_co_yield(void);
EncCo *enc_co_self(void); /* the running coroutine, or NULL on a thread's own stack */
void enc_co_destroy(EncCo *co); /* a coroutine that has not finished is dropped where it stands */

#ifdef __cplusplus
}
#endif
#endif /* ENCORE_PLATFORM_H */

/* Encore's platform interface on the Vita (core/include/encore/platform.h), over targets/vita/runtime's modules:
 * log, audioout (with its ring), touch, discimage (through core/src/enc_disc.c) and ucontext (through
 * core/src/enc_co.c, built with -DENC_TLS= : coroutines are resumed from the game thread only).
 *
 *   video   CPU frames: a 960x544 A8B8G8R8 framebuffer pair in CDRAM, the frame scaled in by the CPU
 *           (enc_blit_scaled) and flipped on the next vblank.
 *           GPU mode, GXM: core initialises sceGxm with its display queue and owns the buffer pair (mapped for the
 *           GPU, with colour surfaces and sync objects); the game draws a scene on each. GLES: vitaGL owns
 *           everything (the game calls vglInit and vglSwapBuffers); core paces and keeps the aspect box.
 *   events  SUSPEND and RESUME from a power callback (a thread of its own sleeping in sceKernelDelayThreadCB),
 *           handed over at the next enc_input_poll: the app is frozen in between, so both come after the wake
 *   input   sceCtrl (analog wide), both touch panels through vpk_touch, sceMotion; no rumble (a PS TV's pads:
 *           later); text through the IME dialog, in GPU mode only (the system draws it with the GPU: core at its
 *           GXM present, vitaGL at vglSwapBuffers(GL_TRUE))
 *   audio   a BGM port at the game's rate (sceAudioOut takes 8-48 kHz there), fed from vpk_audio_ring
 *   files   ux0:data/<game>/ (DATA is the same folder: the player copies the disc image there); save/, config/,
 *           log/, cache/ inside it
 *   timing  sceKernelGetProcessTimeWide less the time spent suspended; the vblank is the display's (vpk_vcount
 *           widened) when the source runs at 60 Hz, timed otherwise
 *
 * Status: compiled in CI with vitasdk; not yet run on a Vita (every run on hardware needs the owner's yes first).
 * API 2's GPU mode, power callback, motion and IME dialog are written from the SDK headers and samples, unproven.
 * The games that ship on the Vita today (kh1-vita, spyro-vita, atv2-vita) call the vpk_ modules directly. */
#include <psp2/audioout.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/ime_dialog.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/motion.h>
#include <psp2/power.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encore/backend.h"
#include "encore/platform.h"
#include "vpk_audio_out.h"
#include "vpk_audio_ring.h"
#include "vpk_log.h"
#include "vpk_touch.h"
#include "vpk_vcount.h"

#if ENC_PLATFORM_API != 2
#error "this backend implements Encore platform API 2"
#endif

#define FB_W 960
#define FB_H 544
#define FB_STRIDE 960
#define FB_BYTES (((FB_STRIDE * FB_H * 4) + 0x3ffff) & ~0x3ffff) /* CDRAM blocks are 256 KiB multiples */

static struct {
  int up;
  char game[64];
  char root[128];
  EncPlatformInfo info;
  int source_hz;
  VpkVcount vc;
  uint64_t t0;
  /* video */
  int video_open;
  EncVideoConfig vcfg;
  SceUID fb_block[2];
  uint32_t *fb[2];
  int back;
  int gxm_up, front, in_frame; /* GPU mode */
  SceGxmColorSurface surf[2];
  SceGxmSyncObject *sync[2];
  /* input */
  SceCtrlData ctrl;
  VpkTouchFinger touch[2][VPK_TOUCH_MAX];
  int ntouch[2];
  int motion_up;
  EncMotion motion;
  /* suspend: the power thread counts, the game thread hands over at its poll */
  SceUID power_thread;
  volatile int power_stop, suspends, resumes;
  volatile uint64_t suspend_at, resume_at; /* raw process time */
  int seen_suspends, seen_resumes;
  uint64_t clock_off;       /* time spent suspended: enc_time_us is the process time less this (atomic) */
  uint64_t vbl_skip, last_vbl; /* the display's vblanks spent suspended, and the last count handed out */
} V;

/* ---- lifecycle ------------------------------------------------------------------------------------------------ */

static void mkdir_p(const char *path) {
  char tmp[192];
  size_t i, n = strlen(path);
  if (n >= sizeof tmp) return;
  memcpy(tmp, path, n + 1);
  for (i = 5; i <= n; i++) /* past "ux0:/" */
    if (tmp[i] == '/' || tmp[i] == 0) {
      char c = tmp[i];
      tmp[i] = 0;
      sceIoMkdir(tmp, 0777);
      tmp[i] = c;
    }
}

static int power_cb(int notify_id, int count, int arg, void *user) {
  (void)notify_id, (void)count, (void)user;
  if (arg & SCE_POWER_CB_SYSTEM_SUSPEND) {
    V.suspend_at = sceKernelGetProcessTimeWide();
    V.suspends++;
  }
  if (arg & (SCE_POWER_CB_SYSTEM_RESUME | SCE_POWER_CB_AFTER_SYSTEM_RESUME)) {
    if (V.resumes < V.suspends) { /* one RESUME per SUSPEND, whichever bits come */
      V.resume_at = sceKernelGetProcessTimeWide();
      V.resumes++;
    }
  }
  return 0;
}

static int power_main(SceSize args, void *argp) { /* callbacks run on the thread that made them, while it sleeps */
  SceUID cb;
  (void)args, (void)argp;
  cb = sceKernelCreateCallback("encore_power_cb", 0, power_cb, NULL);
  if (cb < 0 || scePowerRegisterCallback(cb) < 0) {
    enc_log("encore-vita: no power callback (0x%08x): no SUSPEND / RESUME events", (unsigned)cb);
    return 0;
  }
  while (!V.power_stop) sceKernelDelayThreadCB(100000);
  scePowerUnregisterCallback(cb);
  sceKernelDeleteCallback(cb);
  return 0;
}

static uint64_t clock_off(void) { return __atomic_load_n(&V.clock_off, __ATOMIC_ACQUIRE); }

int enc_platform_init(const EncPlatformConfig *cfg) {
  char path[192];
  if (!cfg || !cfg->game || !cfg->game[0] || strlen(cfg->game) >= sizeof V.game) return ENC_ERR_ARG;
  if (V.up) return ENC_ERR_STATE;
  memset(&V, 0, sizeof V);
  strcpy(V.game, cfg->game);
  snprintf(V.root, sizeof V.root, "ux0:data/%s", V.game);
  mkdir_p(V.root);
  snprintf(path, sizeof path, "%s/log", V.root);
  mkdir_p(path);
  snprintf(path, sizeof path, "%s/log/%s.log", V.root, V.game);
  vpk_log_open(path, 0);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  vpk_touch_init();
  enc_event_reset();
  V.motion_up = sceMotionStartSampling() >= 0; /* a PS TV has no sensors */
  V.power_thread = sceKernelCreateThread("encore_power", power_main, 0x10000100, 0x4000, 0, 0, NULL);
  if (V.power_thread >= 0 && sceKernelStartThread(V.power_thread, 0, NULL) < 0) {
    sceKernelDeleteThread(V.power_thread);
    V.power_thread = -1;
  }
  V.info.target = "vita";
  V.info.caps = ENC_CAP_TOUCH_FRONT | ENC_CAP_TOUCH_REAR | ENC_CAP_ANALOG | ENC_CAP_COROUTINES | ENC_CAP_TEXT_INPUT |
                (V.motion_up ? ENC_CAP_MOTION : 0);
  V.info.gfx_apis = (1u << ENC_GFX_GXM) | (1u << ENC_GFX_GLES);
  V.info.cpu_cores = 3; /* the fourth is the system's */
  V.info.display_width = FB_W, V.info.display_height = FB_H;
  V.info.refresh_hz = 60;
  V.source_hz = 60;
  V.t0 = sceKernelGetProcessTimeWide();
  V.up = 1;
  enc_log("encore-vita: %s up", V.game);
  return ENC_OK;
}

const EncPlatformInfo *enc_platform_info(void) { return V.up ? &V.info : NULL; }

int enc_platform_should_quit(void) { return 0; } /* the system ends a Vita app itself (the PS button, close) */

void enc_platform_shutdown(void) {
  if (!V.up) return;
  enc_text_input_cancel();
  enc_audio_close();
  enc_video_close();
  if (V.motion_up) sceMotionStopSampling();
  if (V.power_thread >= 0) {
    V.power_stop = 1;
    sceKernelWaitThreadEnd(V.power_thread, NULL, NULL);
    sceKernelDeleteThread(V.power_thread);
  }
  enc_log("encore-vita: shutdown");
  vpk_log_flush();
  V.up = 0;
}

void enc_log(const char *fmt, ...) {
  char line[1024];
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(line, sizeof line - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
  line[n++] = '\n';
  vpk_log_write(line, n);
}

void enc_log_flush(void) { vpk_log_flush(); }

/* ---- video ---------------------------------------------------------------------------------------------------- */

static void show(int i) {
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof fb);
  fb.size = sizeof fb;
  fb.base = V.fb[i];
  fb.pitch = FB_STRIDE;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = FB_W, fb.height = FB_H;
  sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

/* frees the first n framebuffers; with the display still scanning one of them out, it would read freed CDRAM
 * (Vita3K stops on it), so the display is blanked first and the change waited for */
static void fb_free(int n) {
  sceDisplaySetFrameBuf(NULL, SCE_DISPLAY_SETBUF_NEXTFRAME);
  sceDisplayWaitSetFrameBuf();
  while (n-- > 0) {
    if (V.gxm_up) sceGxmUnmapMemory(V.fb[n]);
    sceKernelFreeMemBlock(V.fb_block[n]);
  }
}

static int fb_alloc(void) {
  int i;
  for (i = 0; i < 2; i++) {
    void *base = NULL;
    V.fb_block[i] = sceKernelAllocMemBlock("encore_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, FB_BYTES, NULL);
    if (V.fb_block[i] < 0 || sceKernelGetMemBlockBase(V.fb_block[i], &base) < 0) {
      enc_log("encore-vita: framebuffer %d: 0x%08x", i, (unsigned)V.fb_block[i]);
      fb_free(i);
      return ENC_ERR_SYSTEM;
    }
    V.fb[i] = (uint32_t *)base;
    memset(V.fb[i], 0, FB_STRIDE * FB_H * 4);
    if (V.gxm_up && sceGxmMapMemory(base, FB_BYTES, SCE_GXM_MEMORY_ATTRIB_RW) < 0) {
      sceKernelFreeMemBlock(V.fb_block[i]);
      fb_free(i);
      return ENC_ERR_SYSTEM;
    }
  }
  return ENC_OK;
}

/* GXM's display queue runs this on its own thread once the GPU is done with the buffer */
typedef struct {
  void *base;
  int vsync;
} DisplayData;

static void display_cb(const void *data) {
  const DisplayData *d = (const DisplayData *)data;
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof fb);
  fb.size = sizeof fb;
  fb.base = d->base;
  fb.pitch = FB_STRIDE;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = FB_W, fb.height = FB_H;
  sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
  if (d->vsync) sceDisplayWaitVblankStart();
}

static int gxm_open(void) {
  SceGxmInitializeParams p;
  int i, rc;
  memset(&p, 0, sizeof p);
  p.displayQueueMaxPendingCount = 1; /* two buffers */
  p.displayQueueCallback = display_cb;
  p.displayQueueCallbackDataSize = sizeof(DisplayData);
  p.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
  if ((rc = sceGxmInitialize(&p)) < 0) {
    enc_log("encore-vita: sceGxmInitialize 0x%08x (vitaGL already up? use ENC_GFX_GLES)", (unsigned)rc);
    return ENC_ERR_SYSTEM;
  }
  V.gxm_up = 1;
  if ((rc = fb_alloc()) != ENC_OK) {
    sceGxmTerminate();
    V.gxm_up = 0;
    return rc;
  }
  for (i = 0; i < 2; i++) {
    if (sceGxmColorSurfaceInit(&V.surf[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR,
                               SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, FB_W, FB_H,
                               FB_STRIDE, V.fb[i]) < 0 ||
        sceGxmSyncObjectCreate(&V.sync[i]) < 0) {
      while (i-- > 0) sceGxmSyncObjectDestroy(V.sync[i]);
      fb_free(2);
      sceGxmTerminate();
      V.gxm_up = 0;
      return ENC_ERR_SYSTEM;
    }
  }
  V.front = 0, V.back = 1;
  show(0);
  return ENC_OK;
}

static void gxm_close(void) {
  int i;
  sceGxmDisplayQueueFinish();
  for (i = 0; i < 2; i++) sceGxmSyncObjectDestroy(V.sync[i]);
  fb_free(2);
  sceGxmTerminate();
  V.gxm_up = 0;
}

int enc_video_open(const EncVideoConfig *cfg) {
  int rc;
  if (!V.up) return ENC_ERR_STATE;
  if (!cfg || cfg->source_width <= 0 || cfg->source_height <= 0 || cfg->gfx < 0 || cfg->gfx > 31) return ENC_ERR_ARG;
  if (cfg->gfx != ENC_GFX_NONE && !(V.info.gfx_apis & (1u << cfg->gfx))) return ENC_ERR_UNSUPPORTED;
  if (V.video_open) return ENC_ERR_STATE;
  if (cfg->gfx == ENC_GFX_GXM)
    rc = gxm_open();
  else if (cfg->gfx == ENC_GFX_GLES)
    rc = ENC_OK; /* vitaGL has the display */
  else if ((rc = fb_alloc()) == ENC_OK) {
    V.back = 1;
    show(0);
  }
  if (rc != ENC_OK) return rc;
  V.vcfg = *cfg;
  if (V.vcfg.gfx == ENC_GFX_GLES && !V.vcfg.gfx_major) V.vcfg.gfx_major = 2;
  V.source_hz = cfg->refresh_hz > 0 ? cfg->refresh_hz : 60;
  V.in_frame = 0;
  V.video_open = 1;
  return ENC_OK;
}

int enc_video_set_aspect(int aspect) {
  if (!V.video_open) return ENC_ERR_STATE;
  if ((unsigned)aspect > ENC_ASPECT_INTEGER) return ENC_ERR_ARG;
  V.vcfg.aspect = aspect;
  return ENC_OK;
}

int enc_video_present(const EncFrame *f) {
  int rc;
  if (!V.video_open || V.vcfg.gfx != ENC_GFX_NONE) return ENC_ERR_STATE;
  if (!f) return ENC_ERR_ARG;
  sceDisplayWaitSetFrameBuf(); /* the back buffer is no longer on screen */
  rc = enc_blit_scaled(f, V.fb[V.back], FB_W, FB_H, FB_STRIDE,
                       enc_aspect_box(f->width, f->height, FB_W, FB_H, V.vcfg.aspect), 1);
  if (rc != ENC_OK) return rc;
  show(V.back);
  V.back ^= 1;
  if (V.vcfg.vsync) sceDisplayWaitVblankStart();
  return ENC_OK;
}

void enc_video_close(void) {
  if (!V.video_open) return;
  enc_text_input_cancel(); /* the dialog draws on our buffers */
  if (V.vcfg.gfx == ENC_GFX_GXM)
    gxm_close();
  else if (V.vcfg.gfx == ENC_GFX_NONE) {
    sceDisplayWaitSetFrameBuf();
    fb_free(2);
  }
  V.video_open = 0;
}

int enc_video_gpu(EncGpu *g) {
  if (!g) return ENC_ERR_ARG;
  if (!V.video_open || V.vcfg.gfx == ENC_GFX_NONE) return ENC_ERR_STATE;
  memset(g, 0, sizeof *g);
  g->api = V.vcfg.gfx;
  g->major = V.vcfg.gfx_major, g->minor = V.vcfg.gfx_minor;
  g->core_swaps = V.vcfg.gfx == ENC_GFX_GXM;
  g->buffers = V.vcfg.gfx == ENC_GFX_GXM ? 2 : 0;
  return ENC_OK; /* vitaGL: the game links it and calls vglGetProcAddress itself */
}

int enc_video_gpu_begin(EncGpuFrame *f) {
  if (!f) return ENC_ERR_ARG;
  if (!V.video_open || V.vcfg.gfx == ENC_GFX_NONE || V.in_frame) return ENC_ERR_STATE;
  memset(f, 0, sizeof *f);
  f->width = FB_W, f->height = FB_H;
  f->box = enc_aspect_box(V.vcfg.source_width, V.vcfg.source_height, FB_W, FB_H, V.vcfg.aspect);
  if (V.vcfg.gfx == ENC_GFX_GXM) {
    /* no CPU wait: the GPU waits on the sync object for the display to let go of this buffer */
    f->index = V.back;
    f->surface = &V.surf[V.back];
    f->sync = V.sync[V.back];
    f->pixels = V.fb[V.back];
    f->stride_px = FB_STRIDE;
  }
  V.in_frame = 1;
  return ENC_OK;
}

static void ime_draw(int buffer);

int enc_video_gpu_present(void) {
  if (!V.video_open || V.vcfg.gfx == ENC_GFX_NONE || !V.in_frame) return ENC_ERR_STATE;
  V.in_frame = 0;
  if (V.vcfg.gfx == ENC_GFX_GXM) {
    DisplayData d;
    ime_draw(V.back);
    d.base = V.fb[V.back];
    d.vsync = V.vcfg.vsync && V.source_hz == 60;
    if (sceGxmDisplayQueueAddEntry(V.sync[V.front], V.sync[V.back], &d) < 0) return ENC_ERR_SYSTEM;
    V.front = V.back;
    V.back ^= 1;
  }
  /* the display's own vblank paces a 60 Hz source; any other rate is timed */
  if (V.vcfg.vsync && V.source_hz != 60) enc_vblank_wait();
  return ENC_OK;
}

/* ---- input ---------------------------------------------------------------------------------------------------- */

static void audio_drop(void);

/* SUSPEND / RESUME as the power thread counted them: the clock and the vblank skip the gap, the audio queue goes */
static void gather_suspends(void) {
  while (V.seen_suspends < V.suspends || V.seen_resumes < V.resumes) {
    if (V.seen_suspends < V.suspends && V.seen_suspends == V.seen_resumes) {
      V.seen_suspends++;
      audio_drop();
      enc_event_emit(ENC_EVENT_SUSPEND, 0, 0, 0);
    } else if (V.seen_resumes < V.resumes) {
      uint64_t s0 = V.suspend_at, r0 = V.resume_at, gap = r0 > s0 ? r0 - s0 : 0;
      V.seen_resumes++;
      __atomic_store_n(&V.clock_off, clock_off() + gap, __ATOMIC_RELEASE);
      if (V.source_hz == 60) V.vbl_skip = vpk_vcount_widen(&V.vc, (unsigned)sceDisplayGetVcount()) - V.last_vbl;
      audio_drop();
      enc_event_emit(ENC_EVENT_RESUME, 0, 0, gap);
    } else
      break;
  }
}

int enc_input_poll(void) {
  int p;
  if (!V.up) return ENC_ERR_STATE;
  gather_suspends();
  if (V.motion_up) {
    SceMotionSensorState ms;
    if (sceMotionGetSensorState(&ms, 1) >= 0) {
      /* the Vita's axes are the interface's: x right, y up, z out of the screen; accel in g, gyro in rad/s */
      V.motion.accel[0] = ms.accelerometer.x, V.motion.accel[1] = ms.accelerometer.y;
      V.motion.accel[2] = ms.accelerometer.z;
      V.motion.gyro[0] = ms.gyro.x, V.motion.gyro[1] = ms.gyro.y, V.motion.gyro[2] = ms.gyro.z;
      V.motion.time_us = enc_time_us();
    }
  }
  if (sceCtrlPeekBufferPositive(0, &V.ctrl, 1) < 0) memset(&V.ctrl, 0, sizeof V.ctrl);
  for (p = 0; p < 2; p++) {
    int n = vpk_touch_read(p == ENC_TOUCH_FRONT ? VPK_TOUCH_FRONT : VPK_TOUCH_BACK, V.touch[p], VPK_TOUCH_MAX);
    V.ntouch[p] = n > 0 ? n : 0;
  }
  return ENC_OK;
}

static int16_t stick(unsigned char v) { return (int16_t)(((int)v - 128) * 256); }

int enc_pad_read(int port, EncPad *pad) {
  static const struct { unsigned sce, enc; } map[] = {
      {SCE_CTRL_UP, ENC_BTN_UP},          {SCE_CTRL_DOWN, ENC_BTN_DOWN},     {SCE_CTRL_LEFT, ENC_BTN_LEFT},
      {SCE_CTRL_RIGHT, ENC_BTN_RIGHT},    {SCE_CTRL_CROSS, ENC_BTN_SOUTH},   {SCE_CTRL_CIRCLE, ENC_BTN_EAST},
      {SCE_CTRL_SQUARE, ENC_BTN_WEST},    {SCE_CTRL_TRIANGLE, ENC_BTN_NORTH}, {SCE_CTRL_LTRIGGER, ENC_BTN_L1},
      {SCE_CTRL_RTRIGGER, ENC_BTN_R1},    {SCE_CTRL_START, ENC_BTN_START},   {SCE_CTRL_SELECT, ENC_BTN_SELECT}};
  unsigned i;
  if (!pad || port < 0 || port > 0) return ENC_ERR_ARG; /* one pad (a PS TV's extra pads: later) */
  memset(pad, 0, sizeof *pad);
  for (i = 0; i < sizeof map / sizeof map[0]; i++)
    if (V.ctrl.buttons & map[i].sce) pad->buttons |= map[i].enc;
  pad->lx = stick(V.ctrl.lx), pad->ly = stick(V.ctrl.ly);
  pad->rx = stick(V.ctrl.rx), pad->ry = stick(V.ctrl.ry);
  pad->connected = 1;
  /* L2 / R2 / L3 / R3 and the menu come from the rear panel, by the game's own split (vpk_touch_quarters) */
  return ENC_OK;
}

int enc_touch_read(int panel, EncTouch *pts, int max) {
  int i, n;
  if (panel != ENC_TOUCH_FRONT && panel != ENC_TOUCH_REAR) return ENC_ERR_ARG;
  if (max < 0 || (max && !pts)) return ENC_ERR_ARG;
  n = V.ntouch[panel] < max ? V.ntouch[panel] : max;
  for (i = 0; i < n; i++) pts[i].id = V.touch[panel][i].id, pts[i].x = V.touch[panel][i].u, pts[i].y = V.touch[panel][i].v;
  return n;
}

int enc_pad_rumble(int port, int low, int high, int duration_ms) {
  (void)port, (void)low, (void)high, (void)duration_ms;
  return ENC_ERR_UNSUPPORTED; /* the Vita has no motors; a PS TV's DualShock 3 / 4: later */
}

int enc_motion_read(int port, EncMotion *m) {
  if (!V.motion_up) return ENC_ERR_UNSUPPORTED;
  if (!m || port != 0) return ENC_ERR_ARG;
  *m = V.motion;
  return ENC_OK;
}

/* ---- text input: the IME dialog --------------------------------------------------------------------------------- */

#define TEXT_MAX 256

static struct {
  int open;
  SceWChar16 title[SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1], initial[TEXT_MAX + 1], text[TEXT_MAX + 1];
} T;

static void to_utf16(SceWChar16 *out, int cap, const char *in) { /* BMP only: the IME has no surrogates */
  const unsigned char *p = (const unsigned char *)(in ? in : "");
  int n = 0;
  while (*p && n < cap - 1) {
    unsigned c = *p++;
    if (c >= 0xf0) { /* outside the BMP: skip it */
      p += 3;
      continue;
    }
    if (c >= 0xe0) c = ((c & 0x0f) << 12) | ((p[0] & 0x3fu) << 6) | (p[1] & 0x3fu), p += 2;
    else if (c >= 0xc0) c = ((c & 0x1f) << 6) | (p[0] & 0x3fu), p += 1;
    out[n++] = (SceWChar16)c;
  }
  out[n] = 0;
}

static void to_utf8(char *out, size_t cap, const SceWChar16 *in) { /* whole characters only */
  size_t n = 0;
  for (; *in; in++) {
    unsigned c = *in;
    size_t k = c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
    if (n + k >= cap) break;
    if (k == 1) out[n++] = (char)c;
    else if (k == 2) out[n++] = (char)(0xc0 | c >> 6), out[n++] = (char)(0x80 | (c & 0x3f));
    else
      out[n++] = (char)(0xe0 | c >> 12), out[n++] = (char)(0x80 | ((c >> 6) & 0x3f)),
      out[n++] = (char)(0x80 | (c & 0x3f));
  }
  out[n] = 0;
}

int enc_text_input_begin(const EncTextConfig *cfg) {
  SceImeDialogParam p;
  int rc;
  if (!V.up) return ENC_ERR_STATE;
  if (!cfg || cfg->max_chars < 0 || cfg->kind < ENC_TEXT_PLAIN || cfg->kind > ENC_TEXT_MULTILINE) return ENC_ERR_ARG;
  if (!V.video_open || V.vcfg.gfx == ENC_GFX_NONE) return ENC_ERR_UNSUPPORTED; /* the system draws with the GPU */
  if (T.open) return ENC_ERR_STATE;
  to_utf16(T.title, SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1, cfg->title);
  to_utf16(T.initial, TEXT_MAX + 1, cfg->initial);
  memset(T.text, 0, sizeof T.text);
  sceImeDialogParamInit(&p);
  p.supportedLanguages = 0; /* the system's */
  p.languagesForced = SCE_FALSE;
  p.type = cfg->kind == ENC_TEXT_NUMBER ? SCE_IME_TYPE_NUMBER : SCE_IME_TYPE_DEFAULT;
  p.option = cfg->kind == ENC_TEXT_MULTILINE ? SCE_IME_OPTION_MULTILINE : 0;
  p.textBoxMode = cfg->kind == ENC_TEXT_PASSWORD ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
  p.title = T.title;
  p.maxTextLength = cfg->max_chars > 0 && cfg->max_chars < TEXT_MAX ? (SceUInt32)cfg->max_chars : TEXT_MAX;
  p.initialText = T.initial;
  p.inputTextBuffer = T.text;
  if ((rc = sceImeDialogInit(&p)) < 0) {
    enc_log("encore-vita: sceImeDialogInit 0x%08x", (unsigned)rc);
    /* GLES mode before the game's vitaGL is up: no GXM to draw the keyboard with (the hardware says so; Vita3K
     * opens the dialog anyway) */
    return (unsigned)rc == (unsigned)SCE_COMMON_DIALOG_ERROR_GXM_IS_UNINITIALIZED ? ENC_ERR_UNSUPPORTED : ENC_ERR_SYSTEM;
  }
  T.open = 1;
  return ENC_OK;
}

int enc_text_input_poll(char *out, size_t cap) {
  SceImeDialogResult r;
  int st;
  if (!out || !cap) return ENC_ERR_ARG;
  if (!T.open) return ENC_ERR_STATE;
  st = sceImeDialogGetStatus();
  if (st == SCE_COMMON_DIALOG_STATUS_RUNNING) {
    to_utf8(out, cap, T.text[0] ? T.text : T.initial);
    return ENC_TEXT_PENDING;
  }
  memset(&r, 0, sizeof r);
  sceImeDialogGetResult(&r);
  sceImeDialogTerm();
  T.open = 0;
  to_utf8(out, cap, T.text);
  return r.button == SCE_IME_DIALOG_BUTTON_ENTER ? ENC_TEXT_DONE : ENC_TEXT_CANCELLED;
}

void enc_text_input_cancel(void) {
  if (!T.open) return;
  /* an aborted dialog runs on until its status says FINISHED and only then can be ended; ended before that, it
   * stays up and draws into buffers the caller is about to free (Vita3K stopped on it) */
  if (sceImeDialogAbort() >= 0) {
    int i;
    for (i = 0; i < 200 && sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING; i++) sceKernelDelayThread(1000);
  }
  sceImeDialogTerm();
  T.open = 0;
}

/* At a GXM present while the dialog is up: the system draws it over the game's frame on that buffer. vitaGL does
 * the same in vglSwapBuffers(GL_TRUE). */
static void ime_draw(int buffer) {
  SceCommonDialogUpdateParam u;
  if (!T.open) return;
  memset(&u, 0, sizeof u);
  u.renderTarget.colorFormat = SCE_GXM_COLOR_FORMAT_A8B8G8R8;
  u.renderTarget.surfaceType = SCE_GXM_COLOR_SURFACE_LINEAR;
  u.renderTarget.width = FB_W, u.renderTarget.height = FB_H;
  u.renderTarget.strideInPixels = FB_STRIDE;
  u.renderTarget.colorSurfaceData = V.fb[buffer];
  u.renderTarget.depthSurfaceData = NULL; /* unproven: the samples pass the game's depth buffer */
  u.displaySyncObject = V.sync[buffer];
  sceCommonDialogUpdate(&u);
}

/* ---- audio ---------------------------------------------------------------------------------------------------- */

static VpkAudioRing ring;
static VpkAudioOut out;
static int audio_open, audio_channels, audio_port, audio_rate; /* audio_port: the port and its thread, kept after a close */

int enc_audio_open(const EncAudioConfig *cfg) {
  VpkAudioRingConfig rc;
  VpkAudioOutConfig oc;
  if (!V.up) return ENC_ERR_STATE;
  if (!cfg || cfg->rate < 8000 || cfg->rate > 48000 || cfg->channels < 1 || cfg->channels > 2) return ENC_ERR_ARG;
  if (audio_open) return ENC_ERR_STATE;
  if (audio_port) { /* reopened: the port from the first open, at its rate */
    if (cfg->rate != audio_rate) return ENC_ERR_UNSUPPORTED;
    vpk_audio_ring_clear(&ring);
    vpk_audio_out_volume(&out, 100);
    audio_channels = cfg->channels;
    audio_open = 1;
    return ENC_OK;
  }
  memset(&rc, 0, sizeof rc);
  rc.frames = 8192;
  vpk_audio_ring_init(&ring, &rc);
  memset(&oc, 0, sizeof oc);
  oc.port_type = SCE_AUDIO_OUT_PORT_TYPE_BGM; /* the BGM port takes the game's rate */
  oc.grain = 512;
  oc.rate = cfg->rate;
  oc.volume = 100;
  oc.thread_name = "encore_audio";
  oc.priority = 0x40;
  oc.cpu_mask = 0;
  oc.stack = 0x4000;
  if (vpk_audio_out_open(&out, &ring, &oc) < 0) return ENC_ERR_SYSTEM;
  if (vpk_audio_out_start(&out) < 0) {
    vpk_audio_out_close(&out);
    return ENC_ERR_SYSTEM;
  }
  audio_channels = cfg->channels;
  audio_port = 1, audio_rate = cfg->rate;
  audio_open = 1;
  return ENC_OK;
}

int enc_audio_push(const int16_t *pcm, int frames) {
  short st[256][2];
  int done = 0;
  if (!audio_open) return ENC_ERR_STATE;
  if (frames < 0 || (frames && !pcm)) return ENC_ERR_ARG;
  if (audio_channels == 2) return vpk_audio_ring_push(&ring, pcm, frames);
  while (done < frames) { /* mono: doubled into stereo a chunk at a time */
    int i, n = frames - done < 256 ? frames - done : 256, got;
    for (i = 0; i < n; i++) st[i][0] = st[i][1] = pcm[done + i];
    got = vpk_audio_ring_push(&ring, &st[0][0], n);
    done += got;
    if (got < n) break;
  }
  return done;
}

int enc_audio_queued(void) { return audio_open ? (int)vpk_audio_ring_level(&ring) : ENC_ERR_STATE; }

int enc_audio_set_volume(int percent) {
  if (!audio_open) return ENC_ERR_STATE;
  vpk_audio_out_volume(&out, percent);
  return ENC_OK;
}

static void audio_drop(void) {
  if (audio_open) vpk_audio_ring_clear(&ring);
}

void enc_audio_close(void) {
  /* vpk_audio_out has no stop: its thread runs for the life of the app, so close keeps the port, silences it and
   * a reopen at the same rate takes it back (another rate: ENC_ERR_UNSUPPORTED) */
  if (!audio_open) return;
  vpk_audio_ring_clear(&ring);
  audio_open = 0;
}

/* ---- files ---------------------------------------------------------------------------------------------------- */

int enc_path(int dir, const char *name, char *o, size_t cap) {
  static const char *const sub[] = {"", "/save", "/config", "/log", "/cache"};
  char d[192];
  int n;
  if (!V.up) return ENC_ERR_STATE;
  if ((unsigned)dir > ENC_DIR_CACHE || !name || !o || !cap) return ENC_ERR_ARG;
  if (strstr(name, "..")) return ENC_ERR_ARG;
  snprintf(d, sizeof d, "%s%s", V.root, sub[dir]);
  mkdir_p(d);
  n = name[0] ? snprintf(o, cap, "%s/%s", d, name) : snprintf(o, cap, "%s", d);
  return n < 0 || (size_t)n >= cap ? ENC_ERR_ARG : ENC_OK;
}

/* ---- timing --------------------------------------------------------------------------------------------------- */

uint64_t enc_time_us(void) { return sceKernelGetProcessTimeWide() - clock_off(); }

void enc_sleep_us(uint64_t us) {
  while (us > 0) {
    SceUInt step = us > 1000000000u ? 1000000000u : (SceUInt)us;
    sceKernelDelayThread(step);
    us -= step;
  }
}

uint64_t enc_vblank_count(void) {
  if (!V.up) return 0;
  if (V.source_hz == 60)
    V.last_vbl = vpk_vcount_widen(&V.vc, (unsigned)sceDisplayGetVcount()) - V.vbl_skip;
  else
    V.last_vbl = (enc_time_us() - V.t0) * (uint64_t)V.source_hz / 1000000u;
  return V.last_vbl;
}

int enc_vblank_wait(void) {
  uint64_t next, at, now;
  if (!V.up) return ENC_ERR_STATE;
  if (V.source_hz == 60) {
    sceDisplayWaitVblankStart();
    return ENC_OK;
  }
  next = enc_vblank_count() + 1;
  at = V.t0 + (next * 1000000u + (uint64_t)V.source_hz - 1) / (uint64_t)V.source_hz;
  while ((now = enc_time_us()) < at) enc_sleep_us(at - now);
  return ENC_OK;
}

/* ---- threads -------------------------------------------------------------------------------------------------- */

struct EncThread {
  SceUID id;
  void (*fn)(void *);
  void *arg;
};

static int thread_entry(SceSize args, void *argp) {
  EncThread *t = *(EncThread **)argp;
  (void)args;
  t->fn(t->arg);
  return 0;
}

int enc_thread_start(EncThread **o, const EncThreadConfig *cfg, void (*fn)(void *arg), void *arg) {
  static const int prio[] = {0x80, 0x60, 0x40, 0x20}; /* LOW .. AUDIO; lower is more urgent on the Vita */
  EncThread *t;
  int p = cfg ? cfg->priority : ENC_PRIO_NORMAL, mask = 0;
  SceSize stack = cfg && cfg->stack ? (SceSize)cfg->stack : 0x40000;
  if (!o || !fn) return ENC_ERR_ARG;
  *o = NULL;
  if (p < ENC_PRIO_LOW || p > ENC_PRIO_AUDIO) p = ENC_PRIO_NORMAL;
  if (cfg && cfg->core >= 0 && cfg->core < 3) mask = SCE_KERNEL_CPU_MASK_USER_0 << cfg->core;
  if (stack < 0x10000) stack = 0x10000;
  t = (EncThread *)calloc(1, sizeof *t);
  if (!t) return ENC_ERR_NOMEM;
  t->fn = fn, t->arg = arg;
  t->id = sceKernelCreateThread(cfg && cfg->name ? cfg->name : "encore", thread_entry, prio[p], stack, 0, mask, NULL);
  if (t->id < 0 || sceKernelStartThread(t->id, sizeof t, &t) < 0) {
    if (t->id >= 0) sceKernelDeleteThread(t->id);
    free(t);
    return ENC_ERR_SYSTEM;
  }
  *o = t;
  return ENC_OK;
}

int enc_thread_join(EncThread *t) {
  if (!t) return ENC_ERR_ARG;
  if (sceKernelWaitThreadEnd(t->id, NULL, NULL) < 0) return ENC_ERR_SYSTEM;
  sceKernelDeleteThread(t->id);
  free(t);
  return ENC_OK;
}

struct EncMutex {
  SceKernelLwMutexWork w;
};

int enc_mutex_create(EncMutex **m) {
  if (!m) return ENC_ERR_ARG;
  *m = (EncMutex *)malloc(sizeof **m); /* newlib: 8-byte aligned, as the work area needs */
  if (!*m) return ENC_ERR_NOMEM;
  if (sceKernelCreateLwMutex(&(*m)->w, "encore_mutex", 2 /* recursive */, 0, NULL) < 0) {
    free(*m);
    *m = NULL;
    return ENC_ERR_SYSTEM;
  }
  return ENC_OK;
}
void enc_mutex_lock(EncMutex *m) { sceKernelLockLwMutex(&m->w, 1, NULL); }
void enc_mutex_unlock(EncMutex *m) { sceKernelUnlockLwMutex(&m->w, 1); }
void enc_mutex_destroy(EncMutex *m) {
  if (!m) return;
  sceKernelDeleteLwMutex(&m->w);
  free(m);
}

struct EncSem {
  SceUID id;
};

int enc_sem_create(EncSem **s, int initial) {
  if (!s || initial < 0) return ENC_ERR_ARG;
  *s = (EncSem *)malloc(sizeof **s);
  if (!*s) return ENC_ERR_NOMEM;
  (*s)->id = sceKernelCreateSema("encore_sem", 0, initial, 0x7fffffff, NULL);
  if ((*s)->id < 0) {
    free(*s);
    *s = NULL;
    return ENC_ERR_SYSTEM;
  }
  return ENC_OK;
}
void enc_sem_post(EncSem *s) { sceKernelSignalSema(s->id, 1); }
int enc_sem_wait(EncSem *s, int64_t timeout_us) {
  SceUInt t;
  if (!s) return ENC_ERR_ARG;
  if (timeout_us < 0) return sceKernelWaitSema(s->id, 1, NULL) < 0 ? ENC_ERR_SYSTEM : ENC_OK;
  t = timeout_us > 0x7fffffff ? 0x7fffffffu : (SceUInt)timeout_us;
  return sceKernelWaitSema(s->id, 1, &t) < 0 ? ENC_ERR_STATE : ENC_OK;
}
void enc_sem_destroy(EncSem *s) {
  if (!s) return;
  sceKernelDeleteSema(s->id);
  free(s);
}

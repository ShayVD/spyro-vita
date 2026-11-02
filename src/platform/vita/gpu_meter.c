/* The GPU's own time, always on and without a glFinish (decisions D75):
 * what the dynamic resolution (dynres.c) is steered by.
 *
 * vitaGL ends every GXM scene with a fragment notification of its own (its
 * query fence: gxm.o's scene_reset and vglSwapBuffers call
 * sceGxmEndScene(ctx, NULL or ..., &query_fence) after counting the fence's
 * value up, buffers.o's reset_queries points it at the notification
 * region). The GPU writes that value once the scene's pixels are done. So:
 *   - sceGxmEndScene is wrapped (-Wl,--wrap): each scene's fence value and
 *     the time it was handed to the GPU go into a small ring;
 *   - a thread on core 1 watches the fence word and notes when each scene
 *     finished. It polls every POLL_US while scenes are outstanding and
 *     sleeps on a semaphore otherwise.
 * A scene's GPU time is its finish minus the later of its submit and the
 * previous scene's finish: the time the GPU was busy with it. Summed, that
 * is the GPU's busy time; per picture it's what has to fit in a vblank for
 * 60 fps. The poll makes each finish up to POLL_US late; half of it is
 * taken off each scene that didn't queue behind another.
 * Except the swap's scene (the present into the display buffer, marked by
 * gpu_gl.c's swap()): it can't start until the display has let go of that
 * buffer, a vblank's flip, so its time is mostly waiting whenever the
 * pictures keep up with the display (in Vita3K it held the sum at 16.7 ms a
 * picture at any load). It's left out; it still ends the GPU's wait for the
 * scenes behind it. Its own work, one full-screen quad, is the same at
 * every scale: dynres.c adds an estimate for it.
 * Each scene is also counted by what it draws into (J6: at 67% the GPU
 * still took 11-17 ms a picture in Toasty, so most of its time doesn't go
 * with the targets' pixels): sceGxmBeginScene is wrapped too, and its
 * colour surface's address is looked up among those gpu_gl.c registered
 * (the targets, the atlas); the swap's scene is the present.
 * Nothing here changes what vitaGL or the GPU does: the wrappers pass every
 * argument through, and the thread only reads. */
#include "vita.h"
#include "gpu_rt.h"

#include <psp2/gxm.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <string.h>

#define RINGN 64      /* scenes in flight: vitaGL has a few per target */
#define POLL_US 200   /* while the GPU has scenes */
#define DEAD_US 2000000 /* a scene not done after this: no notifications (D75) */
#define PRIORITY 0x78 /* behind the mixer (0x70), ahead of the XA reader and the parse thread */

static uint64_t now_us(void) { return sceKernelGetProcessTimeWide(); }

typedef struct {
  volatile unsigned int *addr;
  unsigned int value;
  uint32_t t;  /* submitted, us (low 32 bits) */
  int kind;    /* GPU_K_*; GPU_K_PRESENT (the swap's scene) isn't in s_busy_us */
} Scene;

static Scene s_ring[RINGN];
static volatile uint32_t s_w, s_r; /* scenes submitted, scenes seen done */
static SceUID s_sema = -1;
static volatile int s_sleeping;
static int s_on;
static volatile int s_dead;      /* 1: notifications never came (the meter is off) */
static volatile uint32_t s_busy_us; /* GPU busy time, counted up (wraps; differences) */
static volatile uint32_t s_dropped; /* scenes the ring had no room for */
static int s_display;               /* the vitaGL thread is in vglSwapBuffers */
static volatile uint32_t s_kbusy[GPU_K_COUNT], s_kscenes[GPU_K_COUNT]; /* per kind, counted up */

/* colour surfaces gpu_gl.c named (the vitaGL thread only) */
#define NSURF 16
static struct {
  const void *data;
  int kind;
} s_surf[NSURF];
static int s_cur_kind = GPU_K_OTHER; /* the scene begun last */

/* gpu_gl.c's swap(), around vglSwapBuffers (the vitaGL thread) */
void vita_gpu_meter_display(int on) { s_display = on; }

/* gpu_gl.c: scenes drawing into this texture data are of this kind (the
 * vitaGL thread). A new address replaces the oldest when full. */
void vita_gpu_meter_surface(const void *data, int kind) {
  static int next;
  int i;
  if (!data)
    return;
  for (i = 0; i < NSURF; i++)
    if (s_surf[i].data == data) {
      s_surf[i].kind = kind;
      return;
    }
  s_surf[next].data = data;
  s_surf[next].kind = kind;
  next = (next + 1) % NSURF;
}

int __real_sceGxmBeginScene(SceGxmContext *context, unsigned int flags,
                            const SceGxmRenderTarget *renderTarget,
                            const SceGxmValidRegion *validRegion,
                            SceGxmSyncObject *vertexSyncObject,
                            SceGxmSyncObject *fragmentSyncObject,
                            const SceGxmColorSurface *colorSurface,
                            const SceGxmDepthStencilSurface *depthStencil);

int __wrap_sceGxmBeginScene(SceGxmContext *context, unsigned int flags,
                            const SceGxmRenderTarget *renderTarget,
                            const SceGxmValidRegion *validRegion,
                            SceGxmSyncObject *vertexSyncObject,
                            SceGxmSyncObject *fragmentSyncObject,
                            const SceGxmColorSurface *colorSurface,
                            const SceGxmDepthStencilSurface *depthStencil) {
  if (s_on) {
    const void *data = colorSurface ? sceGxmColorSurfaceGetData(colorSurface) : NULL;
    int i;
    s_cur_kind = GPU_K_OTHER;
    for (i = 0; data && i < NSURF; i++)
      if (s_surf[i].data == data) {
        s_cur_kind = s_surf[i].kind;
        break;
      }
  }
  return __real_sceGxmBeginScene(context, flags, renderTarget, validRegion, vertexSyncObject,
                                 fragmentSyncObject, colorSurface, depthStencil);
}

int __real_sceGxmEndScene(SceGxmContext *context, const SceGxmNotification *vertexNotification,
                          const SceGxmNotification *fragmentNotification);

int __wrap_sceGxmEndScene(SceGxmContext *context, const SceGxmNotification *vertexNotification,
                          const SceGxmNotification *fragmentNotification) {
  int r = __real_sceGxmEndScene(context, vertexNotification, fragmentNotification);
  uint32_t w = s_w;
  if (!s_on || !fragmentNotification || r < 0 || s_dead)
    return r;
  if (w - __atomic_load_n(&s_r, __ATOMIC_ACQUIRE) >= RINGN) {
    s_dropped++;
    return r;
  }
  s_ring[w % RINGN].addr = fragmentNotification->address;
  s_ring[w % RINGN].value = fragmentNotification->value;
  s_ring[w % RINGN].t = (uint32_t)now_us();
  s_ring[w % RINGN].kind = s_display ? GPU_K_PRESENT : s_cur_kind;
  __atomic_store_n(&s_w, w + 1, __ATOMIC_RELEASE);
  if (__atomic_load_n(&s_sleeping, __ATOMIC_SEQ_CST) &&
      __atomic_exchange_n(&s_sleeping, 0, __ATOMIC_SEQ_CST))
    sceKernelSignalSema(s_sema, 1);
  return r;
}

static int meter_main(SceSize args, void *argp) {
  uint32_t r = 0, last_done = 0;
  (void)args;
  (void)argp;
  for (;;) {
    const Scene *s;
    uint32_t t;
    if (__atomic_load_n(&s_w, __ATOMIC_ACQUIRE) == r) {
      /* nothing outstanding: sleep until the wrapper submits (it signals
       * only when it sees s_sleeping, as gpu_thread.c's ring does) */
      __atomic_store_n(&s_sleeping, 1, __ATOMIC_SEQ_CST);
      if (__atomic_load_n(&s_w, __ATOMIC_SEQ_CST) != r) {
        if (!__atomic_exchange_n(&s_sleeping, 0, __ATOMIC_SEQ_CST))
          sceKernelWaitSema(s_sema, 1, NULL); /* its signal is on the way */
      } else {
        sceKernelWaitSema(s_sema, 1, NULL);
      }
      continue;
    }
    s = &s_ring[r % RINGN];
    t = (uint32_t)now_us();
    if ((int)(*s->addr - s->value) >= 0) { /* the fence counts up; done in order */
      uint32_t start = (int32_t)(s->t - last_done) > 0 ? s->t : last_done;
      uint32_t d = t - start;
      if (start == s->t) /* didn't queue: the poll's lateness, on average */
        d = d > POLL_US / 2 ? d - POLL_US / 2 : 0;
      if (s->kind != GPU_K_PRESENT)
        __atomic_store_n(&s_busy_us, s_busy_us + d, __ATOMIC_RELEASE);
      s_kbusy[s->kind] += d;
      s_kscenes[s->kind]++;
      last_done = t;
      __atomic_store_n(&s_r, ++r, __ATOMIC_RELEASE);
      continue;
    }
    if (t - s->t > DEAD_US) {
      vita_log("gpu: no scene-done notifications from the GPU after %u s: no GPU time measure, "
               "dynres stays at its scale (D75)", DEAD_US / 1000000);
      s_dead = 1;
      __atomic_store_n(&s_r, s_w, __ATOMIC_RELEASE);
      return 0;
    }
    sceKernelDelayThread(POLL_US);
  }
  return 0;
}

void vita_gpu_meter_start(void) {
  static int started;
  SceUID th;
  if (started++)
    return;
  s_sema = sceKernelCreateSema("spyro_gpu_meter", 0, 0, 1, NULL);
  th = sceKernelCreateThread("spyro_gpu_meter", meter_main, PRIORITY, 16 << 10, 0,
                             SCE_KERNEL_CPU_MASK_USER_1, NULL);
  if (s_sema < 0 || th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("gpu: no GPU time meter (%08x): dynres stays at its scale (D75)", (unsigned)th);
    return;
  }
  s_on = 1;
  vita_log("gpu: GPU time from vitaGL's scene fences, polled every %d us on core 1 (D75)",
           POLL_US);
}

/* The GPU's busy time so far, us (counted up: take differences), and the
 * scenes done; 0 if there's no measure. Any thread. */
int vita_gpu_meter(uint32_t *busy_us, uint32_t *scenes) {
  if (!s_on || s_dead)
    return 0;
  *scenes = __atomic_load_n(&s_r, __ATOMIC_ACQUIRE);
  *busy_us = __atomic_load_n(&s_busy_us, __ATOMIC_ACQUIRE);
  return 1;
}

unsigned vita_gpu_meter_dropped(void) { return s_dropped; }

/* Per kind (GPU_K_*), the busy time and the scenes so far (counted up:
 * take differences); 0 if there's no measure. Any thread. */
int vita_gpu_meter_kinds(uint32_t busy_us[GPU_K_COUNT], uint32_t scenes[GPU_K_COUNT]) {
  int k;
  if (!s_on || s_dead)
    return 0;
  for (k = 0; k < GPU_K_COUNT; k++) {
    busy_us[k] = __atomic_load_n(&s_kbusy[k], __ATOMIC_ACQUIRE);
    scenes[k] = __atomic_load_n(&s_kscenes[k], __ATOMIC_ACQUIRE);
  }
  return 1;
}

/* Per-frame CPU time on the Vita, by part (spyro.log's "time" lines every
 * 300 vblanks): game logic, PsyCross's OT walk, the software rasterizer, the
 * SPU/CD models, the present (convert + upload + draw) and the wait for the
 * display's vblank. Time is charged to whatever part is innermost; GPU entry
 * points are wrapped at link time (-Wl,--wrap=..., src/platform/vita/
 * CMakeLists.txt), so neither PsyCross nor gpu_soft.c changes. */
#include "vita.h"

#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

static const char *const k_names[PROF_COUNT] = {"game", "ot", "raster", "spu", "present", "wait"};

static uint64_t s_acc[PROF_COUNT]; /* microseconds since the last report */
static int s_stack[8];
static int s_depth;
static uint64_t s_mark;
static uint64_t s_busy_frame, s_worst, s_frame_t0;
static int s_frames;

static unsigned s_vram_gen; /* bumped by every VRAM write */

unsigned prof_vram_gen(void) { return s_vram_gen; }
void prof_vram_touch(void) { s_vram_gen++; } /* VRAM written behind the GR_* calls (D51) */

static uint64_t now_us(void) { return sceKernelGetProcessTimeWide(); }

static void charge(void) {
  uint64_t t = now_us();
  s_acc[s_depth ? s_stack[s_depth - 1] : PROF_GAME] += t - s_mark;
  s_mark = t;
}

/* The parts are the game thread's: the parse thread (D69) runs the wrapped
 * GR_* calls too, and its own report counts its time. */
void prof_enter(int part) {
  if (vita_parse_on_walker())
    return;
  charge();
  if (s_depth < (int)(sizeof s_stack / sizeof s_stack[0]))
    s_stack[s_depth] = part;
  s_depth++;
}

void prof_leave(void) {
  if (vita_parse_on_walker())
    return;
  charge();
  if (s_depth > 0)
    s_depth--;
}

/* Once per vblank, after the wait: the slowest vblank's busy time. */
void prof_vblank(void) {
  uint64_t busy, total, t = now_us();
  int i;
  charge();
  if (!s_frame_t0)
    s_frame_t0 = t;
  for (total = 0, i = 0; i < PROF_COUNT; i++)
    total += s_acc[i];
  busy = total - s_acc[PROF_WAIT];
  if (busy - s_busy_frame > s_worst)
    s_worst = busy - s_busy_frame;
  s_busy_frame = busy;
  s_frames++;
}

void prof_report(int vblank) {
  char buf[256];
  int i, n;
  uint64_t total = 0, busy;
  if (!s_frames)
    return;
  charge();
  for (i = 0; i < PROF_COUNT; i++)
    total += s_acc[i];
  busy = total - s_acc[PROF_WAIT];
  n = snprintf(buf, sizeof buf, "time %d: per vblank ms", vblank);
  for (i = 0; i < PROF_COUNT; i++)
    n += snprintf(buf + n, sizeof buf - n, " %s %u.%02u", k_names[i],
                  (unsigned)(s_acc[i] / s_frames / 1000),
                  (unsigned)(s_acc[i] / s_frames % 1000 / 10));
  snprintf(buf + n, sizeof buf - n, "; busy %u%%, worst vblank %u.%u ms",
           (unsigned)(total ? busy * 100 / total : 0), (unsigned)(s_worst / 1000),
           (unsigned)(s_worst % 1000 / 100));
  vita_log("%s", buf);
  memset(s_acc, 0, sizeof s_acc);
  s_frames = 0;
  s_busy_frame = 0;
  s_worst = 0;
}

/* ---- link-time wrappers (-Wl,--wrap=NAME) ---- */
#define WRAP_VOID(part, name, params, args)                                    \
  void __real_##name params;                                                   \
  void __wrap_##name params {                                                  \
    prof_enter(part);                                                          \
    if (part == PROF_RASTER)                                                   \
      s_vram_gen++;                                                            \
    __real_##name args;                                                        \
    prof_leave();                                                              \
  }

WRAP_VOID(PROF_OT, DrawOTag, (unsigned long *p), (p))
WRAP_VOID(PROF_OT, DrawPrim, (void *p), (p))
WRAP_VOID(PROF_RASTER, GR_DrawTriangles, (int start_vertex, int triangles),
          (start_vertex, triangles))
WRAP_VOID(PROF_RASTER, GR_CopyVRAM,
          (unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y),
          (src, x, y, w, h, dst_x, dst_y))
WRAP_VOID(PROF_RASTER, GR_ClearVRAM,
          (int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b),
          (x, y, w, h, r, g, b))
WRAP_VOID(PROF_RASTER, GR_ReadVRAM, (unsigned short *dst, int x, int y, int dst_w, int dst_h),
          (dst, x, y, dst_w, dst_h))

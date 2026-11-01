/* Between gpu_thread.c (the game thread's side of the GPU renderer, and the
 * render thread, decisions D63) and gpu_gl.c (everything that calls vitaGL).
 * With render_thread=1 every gl_* function here and every gl_GR_* runs on
 * the render thread, in the order the game thread asked for them; with
 * render_thread=0 the game thread calls them directly, as before. */
#ifndef SPYRO_GPU_RT_H
#define SPYRO_GPU_RT_H

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdint.h>

#include "PsyX/PsyX_render.h"
#include "psx/libgpu.h" /* DISPENV */
#include "psx_vcache.h"

/* What the GL side reads of the game's display state, as it was when the
 * command being run was recorded: gpu_thread.c sets it before every command
 * that reads it (a split's clip, a capture, a present, a synchronous call). */
typedef struct {
  DISPENV disp;  /* currentDispEnv */
  int disabled;  /* g_GPUDisabledState */
  int wide;      /* g_present.widescreen */
} GlEnv;
extern GlEnv g_gl_env;

extern int g_rt_active; /* 1: the gl_* calls run on the render thread */
extern uint32_t g_gl_finish_us; /* gpu_gl.c: time in gpu_time's glFinish calls */

/* gpu_gl.c: PsyCross's GR_* calls (gpu.c), and the set-up, presents and
 * reports behind vita.h's vita_gpu_* (gpu_thread.c) */
void gl_GR_SetupClipMode(const RECT16 *rect, int enable);
void gl_GR_SetTexture(TextureID texture, TexFormat texFormat);
void gl_GR_SetBlendMode(BlendMode blendMode);
void gl_GR_UpdateVertexBuffer(const GrVertex *vertices, int count);
void gl_GR_DrawTriangles(int start_vertex, int triangles);
void gl_GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g,
                     unsigned char b);
void gl_GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y);
void gl_GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h);
/* GR_UpdateVertexBuffer with the vcache's answers from `side` */
void gl_vertices(const GrVertex *vertices, const VCacheVert *side, int count);
int gl_vcache_side_used(void); /* persp or subpixel: draws read the vcache's answers */
int gl_gpu_init(void);
void gl_present(int slot, int wide, int hd_shot); /* slot -1: the display rect */
void gl_sync_display(void);
void gl_rescale(void);
void gl_vram_loaded(void);
int gl_set_options(int res, int filter, int persp, int subpixel, int cache);
void gl_report(int vblank);
void gl_arm_sample(void);
int gl_take_sample(unsigned *frame_us, unsigned *tail_us); /* any thread */
void gl_overlay(const uint8_t *rgb, int w, int h);
void gl_pres_capture(int x, int y, int w, int h);
void gl_pres_commit(int slot);
uint16_t *gl_host_vram(void);

/* dynres.c: dynamic resolution (D75), on the vitaGL thread but for
 * dynres_room */
void dynres_picture(int menu); /* every picture; menu: the options panel is up */
int dynres_scale(void);        /* per mille: the scale targets take at their next whole clear */
int dynres_room(void);         /* any thread: 1 while dynres=auto may still go down */
void dynres_report(int vblank);
void vita_gpu_meter_display(int on); /* gpu_meter.c: around vglSwapBuffers */

#endif

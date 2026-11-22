/* The GR_* names the OT walk calls (psx_gpu.h; decisions D35, D194): on the
 * PS4 every call reaches the software rasterizer (linux32's gpu_soft.c,
 * compiled with the Vita's gpu_soft_names.h: soft_ names), which keeps the
 * CPU's VRAM exact, and then, with the GLES2 renderer up (g_ps4_gpu), the
 * GPU's side (gpu_gles.c's gles_ names), which draws the same splits at
 * 1080p. The Vita's gpu.c picks one renderer; here the GPU one needs the
 * software one's VRAM (gpu_gles.c's top). The software rasterizer's
 * triangles are drawn by raster_mt.c on several threads, in batches, so
 * every call that moves or reads VRAM waits for the batch first. */
#include "ps4.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdint.h>

#include "psx_gpu.h"

/* split tags the walk (gpu_walk.c) puts on textured/untextured splits */
TextureID g_whiteTexture = 1;
TextureID g_vramTexture = 2;
int g_cfg_bilinearFiltering = 0;

/* raster_mt.c: the software rasterizer's draws on several threads (D194) */
void ps4_raster_clip(const RECT16 *rect, int enable);
void ps4_raster_texture(TextureID tex);
void ps4_raster_blend(BlendMode m);
void ps4_raster_vertices(const GrVertex *v, int n);
void ps4_raster_draw(int start, int ntri);
void ps4_raster_sync(void);
int ps4_raster_on(void);

/* SYNC: the recorded draws into VRAM first (a transfer or read follows) */
#define SYNC() ps4_raster_sync()
#define SOFT_ONLY(name, params, args, pre)                                     \
  void soft_##name params;                                                     \
  void name params {                                                           \
    pre;                                                                       \
    soft_##name args;                                                          \
  }
#define BOTH(name, params, args, pre)                                          \
  void soft_##name params;                                                     \
  void gles_##name params;                                                     \
  void name params {                                                           \
    pre;                                                                       \
    soft_##name args;                                                          \
    if (g_ps4_gpu)                                                             \
      gles_##name args;                                                        \
  }

BOTH(GR_SetupClipMode, (const RECT16 *rect, int enable), (rect, enable),
     ps4_raster_clip(rect, enable))
SOFT_ONLY(GR_SetOffscreenState, (const RECT16 *rect, int enable), (rect, enable), )
BOTH(GR_SetTexture, (TextureID texture, TexFormat texFormat), (texture, texFormat),
     ps4_raster_texture(texture))
BOTH(GR_SetBlendMode, (BlendMode blendMode), (blendMode), ps4_raster_blend(blendMode))
SOFT_ONLY(GR_SetStencilMode, (int drawPrim), (drawPrim), )
SOFT_ONLY(GR_SetOverrideTextureSize, (int width, int height), (width, height), )
SOFT_ONLY(GR_PushDebugLabel, (const char *label), (label), )
SOFT_ONLY(GR_PopDebugLabel, (void), (), )
SOFT_ONLY(GR_UpdateVRAM, (void), (), )
SOFT_ONLY(GR_ReadFramebufferDataToVRAM, (void), (), )
BOTH(GR_UpdateVertexBuffer, (const GrVertex *vertices, int count), (vertices, count),
     ps4_raster_vertices(vertices, count))
BOTH(GR_ClearVRAM,
     (int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b),
     (x, y, w, h, r, g, b), SYNC())
/* libgpu's ClearImage calls GR_ClearVRAM and then GR_Clear with the same
 * rect: the GPU's side clears on the first */
SOFT_ONLY(GR_Clear,
          (int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b),
          (x, y, w, h, r, g, b), SYNC())
BOTH(GR_CopyVRAM, (unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y),
     (src, x, y, w, h, dst_x, dst_y), SYNC())
SOFT_ONLY(GR_ReadVRAM, (unsigned short *dst, int x, int y, int dst_w, int dst_h),
          (dst, x, y, dst_w, dst_h), SYNC())

/* The draws: recorded for raster_mt.c's threads (or gpu_soft.c's own
 * rasterizer, if they didn't start), then the GPU's. */
void soft_GR_DrawTriangles(int start_vertex, int triangles);
void gles_GR_DrawTriangles(int start_vertex, int triangles);
void GR_DrawTriangles(int start_vertex, int triangles) {
  if (ps4_raster_on())
    ps4_raster_draw(start_vertex, triangles);
  else
    soft_GR_DrawTriangles(start_vertex, triangles);
  if (g_ps4_gpu)
    gles_GR_DrawTriangles(start_vertex, triangles);
}

uint16_t *soft_host_vram(void);

/* png.c's display reader, the save states, video.c and the GPU's upload
 * read the CPU's VRAM: the recorded draws first */
uint16_t *host_vram(void) {
  ps4_raster_sync();
  return soft_host_vram();
}

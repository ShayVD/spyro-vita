/* The GR_* names the OT walk calls (psx_gpu.h; decisions D35, D194): on the
 * PS4 every call reaches the software rasterizer (linux32's gpu_soft.c,
 * compiled with the Vita's gpu_soft_names.h: soft_ names), which keeps the
 * CPU's VRAM exact, and then, with the GLES2 renderer up (g_ps4_gpu), the
 * GPU's side (gpu_gles.c's gles_ names), which draws the same splits at
 * 1080p. The Vita's gpu.c picks one renderer; here the GPU one needs the
 * software one's VRAM (gpu_gles.c's top). */
#include "ps4.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdint.h>

#include "psx_gpu.h"

/* split tags the walk (gpu_walk.c) puts on textured/untextured splits */
TextureID g_whiteTexture = 1;
TextureID g_vramTexture = 2;
int g_cfg_bilinearFiltering = 0;

#define SOFT_ONLY(name, params, args)                                          \
  void soft_##name params;                                                     \
  void name params { soft_##name args; }
#define BOTH(name, params, args)                                               \
  void soft_##name params;                                                     \
  void gles_##name params;                                                     \
  void name params {                                                           \
    soft_##name args;                                                          \
    if (g_ps4_gpu)                                                             \
      gles_##name args;                                                        \
  }

BOTH(GR_SetupClipMode, (const RECT16 *rect, int enable), (rect, enable))
SOFT_ONLY(GR_SetOffscreenState, (const RECT16 *rect, int enable), (rect, enable))
BOTH(GR_SetTexture, (TextureID texture, TexFormat texFormat), (texture, texFormat))
BOTH(GR_SetBlendMode, (BlendMode blendMode), (blendMode))
SOFT_ONLY(GR_SetStencilMode, (int drawPrim), (drawPrim))
SOFT_ONLY(GR_SetOverrideTextureSize, (int width, int height), (width, height))
SOFT_ONLY(GR_PushDebugLabel, (const char *label), (label))
SOFT_ONLY(GR_PopDebugLabel, (void), ())
SOFT_ONLY(GR_UpdateVRAM, (void), ())
SOFT_ONLY(GR_ReadFramebufferDataToVRAM, (void), ())
BOTH(GR_UpdateVertexBuffer, (const GrVertex *vertices, int count), (vertices, count))
BOTH(GR_ClearVRAM,
     (int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b),
     (x, y, w, h, r, g, b))
/* libgpu's ClearImage calls GR_ClearVRAM and then GR_Clear with the same
 * rect: the GPU's side clears on the first */
SOFT_ONLY(GR_Clear,
          (int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b),
          (x, y, w, h, r, g, b))
BOTH(GR_CopyVRAM, (unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y),
     (src, x, y, w, h, dst_x, dst_y))
SOFT_ONLY(GR_ReadVRAM, (unsigned short *dst, int x, int y, int dst_w, int dst_h),
          (dst, x, y, dst_w, dst_h))
BOTH(GR_DrawTriangles, (int start_vertex, int triangles), (start_vertex, triangles))

uint16_t *soft_host_vram(void);

/* png.c's display reader, the save states and video.c read the CPU's VRAM */
uint16_t *host_vram(void) { return soft_host_vram(); }

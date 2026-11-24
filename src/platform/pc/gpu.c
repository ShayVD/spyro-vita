/* The GR_* names PsyCross calls (D242), as the Vita's gpu.c (D35): gpu=1
 * (the default) sends them to the GPU renderer (gpu_gl.c), gpu=0 to the
 * software rasterizer (linux32's gpu_soft.c, compiled with the Vita's
 * gpu_soft_names.h, so its names are soft_*). */
#include "pc.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdint.h>

#include "psx_gpu.h"

int g_pc_gpu; /* 1: gpu_gl.c is drawing (set before the game's first draw) */

/* split tags the walk (gpu_walk.c) puts on textured/untextured splits */
TextureID g_whiteTexture = 1;
TextureID g_vramTexture = 2;
int g_cfg_bilinearFiltering = 0;

#define DISPATCH(name, params, args)                                           \
  void soft_##name params;                                                     \
  void gl_##name params;                                                       \
  void name params {                                                           \
    if (g_pc_gpu)                                                              \
      gl_##name args;                                                          \
    else                                                                       \
      soft_##name args;                                                        \
  }

DISPATCH(GR_SetupClipMode, (const RECT16 *rect, int enable), (rect, enable))
DISPATCH(GR_SetOffscreenState, (const RECT16 *rect, int enable), (rect, enable))
DISPATCH(GR_SetTexture, (TextureID texture, TexFormat texFormat), (texture, texFormat))
DISPATCH(GR_SetBlendMode, (BlendMode blendMode), (blendMode))
DISPATCH(GR_SetStencilMode, (int drawPrim), (drawPrim))
DISPATCH(GR_SetOverrideTextureSize, (int width, int height), (width, height))
DISPATCH(GR_PushDebugLabel, (const char *label), (label))
DISPATCH(GR_PopDebugLabel, (void), ())
DISPATCH(GR_UpdateVRAM, (void), ())
DISPATCH(GR_ReadFramebufferDataToVRAM, (void), ())
DISPATCH(GR_UpdateVertexBuffer, (const GrVertex *vertices, int count), (vertices, count))
DISPATCH(GR_ClearVRAM,
         (int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b),
         (x, y, w, h, r, g, b))
DISPATCH(GR_Clear,
         (int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b),
         (x, y, w, h, r, g, b))
DISPATCH(GR_CopyVRAM,
         (unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y),
         (src, x, y, w, h, dst_x, dst_y))
DISPATCH(GR_ReadVRAM, (unsigned short *dst, int x, int y, int dst_w, int dst_h),
         (dst, x, y, dst_w, dst_h))
DISPATCH(GR_DrawTriangles, (int start_vertex, int triangles), (start_vertex, triangles))

uint16_t *soft_host_vram(void);
uint16_t *gl_host_vram(void);

/* frames.c, png.c and the save states read whichever VRAM is live */
uint16_t *host_vram(void) { return g_pc_gpu ? gl_host_vram() : soft_host_vram(); }

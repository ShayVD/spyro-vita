/* The GR_* names PsyCross calls (decisions D30): config.txt gpu=1 (default)
 * sends them to the GPU renderer (gpu_gl.c), gpu=0 to the software
 * rasterizer (linux32's gpu_soft.c, compiled with gpu_soft_names.h). prof.c
 * wraps these (-Wl,--wrap=GR_DrawTriangles, ...), so its timing covers
 * both. */
#include "vita.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdint.h>

#include "PsyX/PsyX_render.h"

int g_vita_gpu; /* 1: gpu_gl.c is drawing */
int g_vita_gl;  /* 1: vitaGL owns the display (gpu_gl.c presents) */

/* split tags PsyX_GPU.cpp puts on textured/untextured splits */
TextureID g_whiteTexture = 1;
TextureID g_vramTexture = 2;
int g_cfg_bilinearFiltering = 0;

#define BACKENDS(name, params)                                                 \
  void soft_##name params;                                                     \
  void gl_##name params;
#define CALL(name, args)                                                       \
  do {                                                                         \
    if (g_vita_gpu)                                                            \
      gl_##name args;                                                          \
    else                                                                       \
      soft_##name args;                                                        \
  } while (0)
#define DISPATCH(name, params, args)                                           \
  BACKENDS(name, params)                                                       \
  void name params { CALL(name, args); }

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

/* png.c's display reader and VRAM dump read whichever VRAM is live */
uint16_t *host_vram(void) { return g_vita_gpu ? gl_host_vram() : soft_host_vram(); }

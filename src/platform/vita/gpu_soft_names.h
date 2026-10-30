/* Force-included into src/platform/linux32/gpu_soft.c in the Vita build
 * (decisions D30): the software rasterizer's GR_* entry points and globals
 * get a soft_ prefix, so it links next to the GPU renderer (gpu_gl.c's gl_*)
 * and gpu.c's dispatch owns the names PsyCross calls. */
#define GR_SetupClipMode soft_GR_SetupClipMode
#define GR_SetOffscreenState soft_GR_SetOffscreenState
#define GR_SetTexture soft_GR_SetTexture
#define GR_SetBlendMode soft_GR_SetBlendMode
#define GR_SetStencilMode soft_GR_SetStencilMode
#define GR_SetOverrideTextureSize soft_GR_SetOverrideTextureSize
#define GR_PushDebugLabel soft_GR_PushDebugLabel
#define GR_PopDebugLabel soft_GR_PopDebugLabel
#define GR_UpdateVRAM soft_GR_UpdateVRAM
#define GR_ReadFramebufferDataToVRAM soft_GR_ReadFramebufferDataToVRAM
#define GR_UpdateVertexBuffer soft_GR_UpdateVertexBuffer
#define GR_ClearVRAM soft_GR_ClearVRAM
#define GR_Clear soft_GR_Clear
#define GR_CopyVRAM soft_GR_CopyVRAM
#define GR_ReadVRAM soft_GR_ReadVRAM
#define GR_DrawTriangles soft_GR_DrawTriangles
#define host_vram soft_host_vram
#define g_whiteTexture soft_g_whiteTexture
#define g_vramTexture soft_g_vramTexture
#define g_cfg_bilinearFiltering soft_g_cfg_bilinearFiltering

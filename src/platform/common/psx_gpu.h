/* The PS1 GPU's side of the port (stage 8d, D117): the OT walk, its draw
 * splits and vertex buffer, the GPU state (gpu_walk.c), and the GR_* back
 * end the walk draws through (gpu.c / gpu_gl.c / gpu_thread.c on the Vita,
 * gpu_soft.c on the host).
 *
 * GrVertex, the enums and the GR_* declarations were PsyCross's
 * (PsyX_render.h, MIT, THIRD_PARTY_NOTICES.md), with the same layout: the
 * renderers, psx_lean.h's lanes and the reference walk (SPYRO_PSYCROSS_REF)
 * read the same bytes. Only what the port uses is declared. */
#ifndef PSX_GPU_H
#define PSX_GPU_H

#include <assert.h> /* PsyCross's psx headers use C11 static_assert */
#include <stdint.h>

#include "psx/types.h"
#include "psx/libgte.h" /* libgpu.h needs its SVECTOR */
#include "psx/libgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The vertex buffer's size: GPUDrawSplit's u_short indices */
#define MAX_VERTEX_BUFFER_SIZE (1 << 16)

/* A vertex as the walk leaves it and the renderers read it: screen x/y with
 * the draw offset, the tpage and CLUT, u/v, brightness (1 untextured, 2
 * textured), dither, the colour, and four spare bytes (the vertex cache's
 * reference, D39; the direct vertices' UV box, D99). 20 bytes. */
#pragma pack(push, 1)
typedef struct {
  short x, y, page, clut;
  u_char u, v, bright, dither;
  u_char r, g, b, a;
  char tcx, tcy, _p0, _p1;
} GrVertex;
#pragma pack(pop)

typedef enum { BM_NONE, BM_AVERAGE, BM_ADD, BM_SUBTRACT, BM_ADD_QUATER_SOURCE } BlendMode;
typedef enum {
  TF_4_BIT,
  TF_8_BIT,
  TF_16_BIT,
  TF_32_BIT_RGBA /* PsyCross's custom texture (the walk's 0xB1 packet) */
} TexFormat;
typedef unsigned int TextureID;

/* ---- the back end (the platform's) ---- */
extern TextureID g_whiteTexture;
extern TextureID g_vramTexture;
/* SPRT's texture coordinates (PsyCross's g_cfg_bilinearFiltering): 0 */
extern int g_cfg_bilinearFiltering;

void GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y);
void GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h);
void GR_UpdateVRAM(void);
void GR_ReadFramebufferDataToVRAM(void);
void GR_SetBlendMode(BlendMode blendMode);
void GR_SetStencilMode(int drawPrim);
void GR_SetOffscreenState(const RECT16 *offscreenRect, int enable);
void GR_SetupClipMode(const RECT16 *clipRect, int enable);
void GR_SetTexture(TextureID texture, TexFormat texFormat);
void GR_SetOverrideTextureSize(int width, int height);
void GR_Clear(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b);
void GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b);
void GR_UpdateVertexBuffer(const GrVertex *vertices, int count);
void GR_DrawTriangles(int start_vertex, int triangles);
void GR_PushDebugLabel(const char *label);
void GR_PopDebugLabel(void);

/* The scene's start and end (both vblank.c): before a list is walked in
 * place, the pending splits are drawn and PutDrawEnv's isbg clear done;
 * nonzero empties the splits first. PsyCross's names (PsyX_public.h). */
char PsyX_BeginScene(void);
void PsyX_EndScene(void);

/* ---- the GPU state (gpu_walk.c; libgpu.c's environment calls set it) ----
 * The draw environment the walk draws with (PutDrawEnv's copy, which the
 * packets' draw mode, area and offset commands change), the display
 * environment shown (activeDispEnv) and put last (currentDispEnv), and
 * SetDispMask(0). PsyCross's names: the save state's GPU chunk holds them. */
extern DRAWENV activeDrawEnv;
extern DISPENV activeDispEnv;
extern DISPENV currentDispEnv;
extern int g_GPUDisabledState;

/* ---- the walk (gpu_walk.c) ---- */

/* The packet after this one (walk.c: in the copy of the frame's
 * primitives); NULL in psx_gpu_walk: the tag's own link (nextPrim). */
typedef uintptr_t (*PsxGpuNext)(uintptr_t packet);

/* A split, with PsyCross's GPUDrawSplit's layout (the reference walk's
 * splits compare byte for byte). It starts with the two environments: the
 * renderers' GR_SetupClipMode gets &drawenv.clip and reads them from there
 * (gpu_soft.c, gpu_gl.c, gpu_thread.c's SplitHead). */
typedef struct {
  DRAWENV drawenv;
  DISPENV dispenv;
  BlendMode blendMode;
  TexFormat texFormat;
  TextureID textureId;
  int drawPrimMode;
  u_short startVertex;
  u_short numVerts;
  const char *debugText;
} PsxGpuSplit;

/* Packets the walk parses from other words than the list's (D122, from
 * stage 7g): n of them, by their address in the game's memory (at,
 * ascending), each with the words parsed in its place (words[i]: its tag,
 * then as many words as the tag's length says). The HUD's 2D primitives
 * narrowed for 16:9 (present.c), so the game's primitives stay as it wrote
 * them. The list's links are still the game's: only the parse changes. */
typedef struct {
  int n;
  const uintptr_t *at;
  const u_int *const *words;
} PsxGpuSubst;

#define PSX_GPU_MAX_SPLITS 4096
/* the records' converter's direct ranges (native_draw.c, D99): ND_DR_MAX */
#define PSX_GPU_DIRECT_MAX 2048

/* One walk's state (D122, for stage 7f's second walk): its vertex buffer
 * and splits, the custom texture and debug label its packets set, the GPU
 * state it draws with and changes (the draw environment, read and written
 * by the packets' E1-E5 and the records' draw modes; the display
 * environment, read), the records' converter's direct ranges over its
 * vertex buffer, and the packets it parses from other words. The game
 * thread's is one static instance (psx_gpu_game_walk), on activeDrawEnv
 * and activeDispEnv; the functions without _in work on it. */
typedef struct PsxGpuWalk {
  int nv, ns;    /* the vertices and the last split (PsyCross's g_vertexIndex, g_splitIndex) */
  int prim_mode; /* DrawPrim's 1 (g_DrawPrimMode) */
  const char *debug_text;
  TextureID override;
  int override_w, override_h;
  DRAWENV *env;
  const DISPENV *disp;
  /* where the list is walked from when not from the game's memory (walk.c's
   * copy of PSX_MEM_SIZE bytes from g_psx_arena_base), else 0: subst's
   * addresses are the game's */
  uintptr_t copy;
  const PsxGpuSubst *subst; /* NULL: none */
  uint32_t ndr;             /* direct ranges: [dr[i][0], dr[i][1]) */
  /* the buffers, the instance's own arrays (pointers, so a static
   * instance's initialiser leaves them in .bss) */
  uint32_t (*dr)[2];   /* PSX_GPU_DIRECT_MAX */
  PsxGpuSplit *splits; /* PSX_GPU_MAX_SPLITS */
  GrVertex *vb;        /* MAX_VERTEX_BUFFER_SIZE */
} PsxGpuWalk;

/* the game thread's walk */
PsxGpuWalk *psx_gpu_game_walk(void);

/* The OT from p to its end: each packet's GP0 commands into the splits and
 * the vertex buffer (PsyCross's ParsePrimitivesLinkedList). single: p is
 * one primitive (DrawPrim), parsed as a whole. */
void psx_gpu_walk(u_long *p, int single, PsxGpuNext next);
void psx_gpu_walk_in(PsxGpuWalk *w, u_long *p, int single, PsxGpuNext next);
/* The scene's start, then psx_gpu_walk: DrawOTag's (libgpu.c) and the
 * walk from the copy's (walk.c) */
void psx_gpu_walk_scene(u_long *p, int single, PsxGpuNext next);
void psx_gpu_walk_scene_in(PsxGpuWalk *w, u_long *p, int single, PsxGpuNext next);
/* The splits drawn (GR_UpdateVertexBuffer, then each split's state and its
 * triangles), then emptied (PsyCross's DrawAllSplits) */
void psx_gpu_draw_splits(void);
void psx_gpu_draw_splits_in(PsxGpuWalk *w);
/* Both emptied (PsyCross's ClearSplits); the native draw's direct ranges
 * with them */
void psx_gpu_clear_splits(void);
void psx_gpu_clear_splits_in(PsxGpuWalk *w);
/* The splits so far (PsyCross's g_splitIndex: index 1 up) */
int psx_gpu_split_count(void);

/* For the records' converter (native_draw.c): what parsing a polygon with
 * these flags does to the splits under the walk's draw environment as it
 * is now, and the vertex buffer with its fill (vertices). */
void psx_gpu_add_split(int semiTrans, int textured);
void psx_gpu_add_split_in(PsxGpuWalk *w, int semiTrans, int textured);
GrVertex *psx_gpu_vertex_buffer(int **fill);

const PsxGpuSplit *psx_gpu_split(int i);
/* split i's fields the host's checks compare (PsyCross's PsyX_SplitGet) */
void psx_gpu_split_get(int i, int out[8]);
void psx_gpu_split_get_in(const PsxGpuWalk *w, int i, int out[8]);

/* The walk's split and vertex fill, kept and put back (a check runs a
 * packet twice from the same state), as PsyCross's PsyXSplitState */
typedef struct {
  int splitIndex, vertexIndex;
  PsxGpuSplit cur;
  const char *debugText;
} PsxGpuSplitState;
void psx_gpu_split_state_save(PsxGpuSplitState *s);
void psx_gpu_split_state_restore(const PsxGpuSplitState *s);
void psx_gpu_split_state_save_in(const PsxGpuWalk *w, PsxGpuSplitState *s);
void psx_gpu_split_state_restore_in(PsxGpuWalk *w, const PsxGpuSplitState *s);
/* PsyCross's custom texture (the 0xB1 packet): texture, width, height */
void psx_gpu_override_get(int out[3]);
void psx_gpu_override_set(const int in[3]);

/* The host's --walk-check (walk_check.c, SPYRO_PSYCROSS_REF): each packet
 * with a nonzero length, before and after the walk parses it; the walk's
 * VRAM calls (op: 0 fill, 1 move, 2 load) before it makes them. NULL: no
 * check. */
typedef struct {
  void (*pre)(uintptr_t packet, int single);
  void (*post)(uintptr_t packet, int single);
  void (*vram)(int op, const RECT16 *rect, int a, int b, int c, const void *data);
} PsxGpuCheck;
extern const PsxGpuCheck *g_psx_gpu_check;
/* --walk-check's mutants (WALK_MUTATE): 1 a line one pixel longer, 2 a
 * draw mode packet's tpage ignored (the check must catch both) */
extern int g_psx_gpu_mutant;

#ifdef __cplusplus
}
#endif

#endif

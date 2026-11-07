/* The OT walk (stage 8d, D117), in place of PsyCross's PsyX_GPU.cpp: the
 * loop over a list's packets with the GP0 split of each (patch 0003's), the
 * parsers for what the records' converter doesn't take, the draw splits and
 * the vertex buffer, and the GPU state. Ported from PsyCross (MIT,
 * THIRD_PARTY_NOTICES.md) with the semantics our patches left it, so each
 * split, vertex and draw environment is the same bytes (--walk-check
 * compares every packet with PsyCross's, the reference build's).
 *
 * What PsyCross's patches hooked in are direct calls now:
 *  - a native draw marker or a packet the records' converter parses whole
 *    (0010's packet hook): native_draw.c's nd_packet, once nd_init has
 *    turned it on (g_nd_walk);
 *  - the next packet (0013, 0009): the walk's argument (walk.c's copy);
 *  - the splits emptied (0014): native_draw.c's nd_walk_cleared;
 *  - each polygon's vertices for the vertex cache (0007): the converter
 *    calls vcache.c's psx_vcache_poly, when it is on.
 * Polygons always go through the records' converter (native_draw.c
 * nd_poly), with native_draw=0 too: D92's per-packet check showed it parses
 * them as PsyCross's Process*Poly. Left here: lines, TILE and SPRT, the
 * fill (GP0 02h), DR_MOVE, DR_LOAD, the draw environment commands (E1-E6)
 * and PsyCross's own 0xB1/0xB2 packets. */
#include "psx_gpu.h"

#include <stdlib.h>
#include <string.h>

/* native_draw.c (the records' converter) */
extern int g_nd_walk;
int nd_packet(uintptr_t packet);
void nd_poly(const u_int *cmd);
void nd_walk_cleared(void);

/* the platform's log (PsyCross's eprintf and eprinterr) */
void PsyX_Log(const char *fmt, ...);
void PsyX_Log_Error(const char *fmt, ...);

DISPENV currentDispEnv;
DISPENV activeDispEnv;
DRAWENV activeDrawEnv;
int g_GPUDisabledState;

const PsxGpuCheck *g_psx_gpu_check;
int g_psx_gpu_mutant;

#define MAX_DRAW_SPLITS 4096

static GrVertex s_vb[MAX_VERTEX_BUFFER_SIZE];
static PsxGpuSplit s_splits[MAX_DRAW_SPLITS];
static int s_nv, s_ns; /* the vertices and the last split (PsyCross's g_vertexIndex, g_splitIndex) */
static int s_prim_mode; /* DrawPrim's 1 (g_DrawPrimMode) */
static const char *s_debug_text;
static TextureID s_override;
static int s_override_w, s_override_h;

void psx_gpu_clear_splits(void) {
  s_debug_text = NULL;
  s_nv = 0;
  s_ns = 0;
  s_splits[0].texFormat = (TexFormat)0xFFFF; /* the next polygon starts split 1 */
  nd_walk_cleared();
}

int psx_gpu_split_count(void) { return s_ns; }

GrVertex *psx_gpu_vertex_buffer(int **fill) {
  *fill = &s_nv;
  return s_vb;
}

const PsxGpuSplit *psx_gpu_split(int i) { return &s_splits[i]; }

void psx_gpu_split_get(int i, int out[8]) {
  const PsxGpuSplit *s = &s_splits[i];
  out[0] = s->blendMode;
  out[1] = s->texFormat;
  out[2] = (int)s->textureId;
  out[3] = s->drawPrimMode;
  out[4] = s->startVertex;
  out[5] = s->numVerts;
  out[6] = s->drawenv.dfe;
  out[7] = s->drawenv.clip.x | s->drawenv.clip.y << 16;
}

/* The splits after the current one are only written when a new one
 * starts, so the current one is all to keep. */
void psx_gpu_split_state_save(PsxGpuSplitState *s) {
  s->splitIndex = s_ns;
  s->vertexIndex = s_nv;
  s->cur = s_splits[s_ns];
  s->debugText = s_debug_text;
}

void psx_gpu_split_state_restore(const PsxGpuSplitState *s) {
  s_ns = s->splitIndex;
  s_nv = s->vertexIndex;
  s_splits[s_ns] = s->cur;
  s_debug_text = s->debugText;
}

void psx_gpu_override_get(int out[3]) {
  out[0] = (int)s_override;
  out[1] = s_override_w;
  out[2] = s_override_h;
}

void psx_gpu_override_set(const int in[3]) {
  s_override = (TextureID)in[0];
  s_override_w = in[1];
  s_override_h = in[2];
}

/* ---- the splits ---- */

/* A new split unless the current one draws the same way (AddSplit) */
void psx_gpu_add_split(int semiTrans, int textured) {
  const int tpage = activeDrawEnv.tpage;
  PsxGpuSplit *cur = &s_splits[s_ns], *s;
  const BlendMode blend = semiTrans ? (BlendMode)(((tpage >> 5) & 3) + 1) : BM_NONE;
  TexFormat fmt = (TexFormat)((tpage >> 7) & 3);
  TextureID tex = textured ? g_vramTexture : g_whiteTexture;
  if (textured && s_override != 0) {
    fmt = TF_32_BIT_RGBA;
    tex = s_override;
  }
  if (cur->blendMode == blend && cur->texFormat == fmt && cur->textureId == tex &&
      cur->drawPrimMode == s_prim_mode && cur->drawenv.clip.x == activeDrawEnv.clip.x &&
      cur->drawenv.clip.y == activeDrawEnv.clip.y && cur->drawenv.clip.w == activeDrawEnv.clip.w &&
      cur->drawenv.clip.h == activeDrawEnv.clip.h && cur->drawenv.dfe == activeDrawEnv.dfe &&
      cur->debugText == s_debug_text)
    return;
  cur->numVerts = (u_short)(s_nv - cur->startVertex);
  if (s_ns + 1 >= MAX_DRAW_SPLITS) {
    PsyX_Log_Error("[Psy-X] [%s] - MAX_DRAW_SPLITS reached (too many blend modes, texture "
                   "formats, drawEnv clip rects, dfe switches), expect rendering errors\n",
                   "AddSplit");
    return;
  }
  s = &s_splits[++s_ns];
  s->blendMode = blend;
  s->texFormat = fmt;
  s->textureId = tex;
  s->drawPrimMode = s_prim_mode;
  s->drawenv = activeDrawEnv;
  s->dispenv = activeDispEnv;
  s->debugText = s_debug_text;
  s->drawenv.tw.w = (short)s_override_w;
  s->drawenv.tw.h = (short)s_override_h;
  s->startVertex = (u_short)s_nv;
  s->numVerts = 0;
}

static void draw_split(const PsxGpuSplit *s) {
  const int on_screen = s->drawenv.dfe;
  if (s->debugText)
    GR_PushDebugLabel(s->debugText);
  GR_SetStencilMode(s->drawPrimMode);
  GR_SetTexture(s->textureId, s->texFormat);
  if (s->texFormat == TF_32_BIT_RGBA)
    GR_SetOverrideTextureSize(s->drawenv.tw.w, s->drawenv.tw.h);
  GR_SetupClipMode(&s->drawenv.clip, on_screen);
  GR_SetOffscreenState(&s->drawenv.clip, !on_screen);
  GR_SetBlendMode(s->blendMode);
  GR_DrawTriangles(s->startVertex, s->numVerts / 3);
  if (s->debugText)
    GR_PopDebugLabel();
}

void psx_gpu_draw_splits(void) {
  int i;
  GR_UpdateVertexBuffer(s_vb, s_nv);
  for (i = 1; i <= s_ns; i++)
    draw_split(&s_splits[i]);
  psx_gpu_clear_splits();
}

/* ---- the leftover parsers: lines, rectangles, the draw environment ----
 * c: a GP0 command's words (c[0] the colour word with the code byte, as a
 * primitive's words after its tag); each makes the vertices PsyCross's
 * Make* functions made, as integers (its float sums are exact here). */

/* the draw offset (DrawEnvOffset) */
static void draw_offset(int *ox, int *oy) {
  if (activeDrawEnv.dfe) {
    *ox = activeDrawEnv.ofs[0] - activeDispEnv.disp.x;
    *oy = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
  } else {
    *ox = *oy = 0;
  }
}

static inline short sx(u_int xy) { return (short)xy; }
static inline short sy(u_int xy) { return (short)(xy >> 16); }

static inline void set_rgb(GrVertex *v, const u_char *c) {
  v->r = c[0];
  v->g = c[1];
  v->b = c[2];
  v->a = 255;
}

/* the four vertices to six (TriangulateQuad: 0 1 2 3 -> 0 1 3, 1 3 2 with
 * 2 and 3 already swapped by the maker) */
static void triangulate(GrVertex *v) {
  v[4] = v[3];
  v[5] = v[2];
  v[2] = v[3];
  v[3] = v[1];
}

/* room for a quad's six vertices (PsyCross had no check: it would run past
 * its buffer) */
static inline GrVertex *room6(void) {
  return s_nv + 6 <= MAX_VERTEX_BUFFER_SIZE ? s_vb + s_nv : NULL;
}

/* A line from xy0 to xy1 with colours c0, c1, as a quad one pixel wide
 * (LineSwapSourceVerts, MakeLineArray, MakeTexcoordLineZero,
 * MakeColourLine, TriangulateQuad) */
static void line(int semi, u_int xy0, u_int xy1, const u_char *c0, const u_char *c1) {
  short x0 = sx(xy0), y0 = sy(xy0), x1 = sx(xy1), y1 = sy(xy1);
  int ox, oy, dx, dy;
  GrVertex *v;
  psx_gpu_add_split(semi, 0);
  if (!(v = room6()))
    return;
  if (x0 > x1 || (y0 > y1 && x0 == x1)) { /* left to right, top to bottom */
    const short tx = x0, ty = y0;
    const u_char *tc = c0;
    x0 = x1, y0 = y1, x1 = tx, y1 = ty;
    c0 = c1, c1 = tc;
  }
  dx = (short)(x1 - x0);
  dy = (short)(y1 - y0);
  draw_offset(&ox, &oy);
  memset(v, 0, sizeof(GrVertex) * 4);
  v[0].x = (short)(x0 + ox);
  v[0].y = (short)(y0 + oy);
  if (dx > abs(dy)) { /* horizontal */
    v[1].x = (short)(x1 + ox + 1 + (g_psx_gpu_mutant == 1));
    v[1].y = (short)(y1 + oy);
    v[2].x = v[1].x;
    v[2].y = (short)(v[1].y + 1);
    v[3].x = v[0].x;
    v[3].y = (short)(v[0].y + 1);
  } else { /* vertical */
    v[1].x = (short)(x1 + ox);
    v[1].y = (short)(y1 + oy + 1);
    v[2].x = (short)(v[1].x + 1);
    v[2].y = v[1].y;
    v[3].x = (short)(v[0].x + 1);
    v[3].y = v[0].y;
  }
  for (dx = 0; dx < 4; dx++)
    v[dx].bright = 1; /* u, v, dither, page, CLUT: 0 */
  set_rgb(&v[0], c0);
  set_rgb(&v[1], c1);
  set_rgb(&v[2], c1);
  set_rgb(&v[3], c0);
  triangulate(v);
  s_nv += 6;
}

/* GP0 40h-5Fh: the lines of LINE_F2/F3/F4 and LINE_G2 (ProcessFlatLines,
 * ProcessGouraudLines). LINE_G3/G4 draw nothing (PsyCross's TODO). Returns
 * PsyCross's length (0: unhandled). */
static int parse_lines(const u_int *c) {
  const u_int code = c[0] >> 24;
  const int semi = (code & 2) != 0;
  const u_char *col = (const u_char *)c;
  if (code & 0x10) { /* Gouraud */
    switch (code & 0x0C) {
    case 0x0:
      line(semi, c[1], c[3], col, (const u_char *)&c[2]);
      return 4;
    case 0x8:
      return 7;
    case 0xC:
      return 9;
    }
    return 0;
  }
  switch (code & 0x0C) {
  case 0x0:
    line(semi, c[1], c[2], col, col);
    return 3;
  case 0x8:
    line(semi, c[1], c[2], col, col);
    line(semi, c[2], c[3], col, col);
    return 5;
  case 0xC:
    line(semi, c[1], c[2], col, col);
    line(semi, c[2], c[3], col, col);
    line(semi, c[3], c[4], col, col);
    return 6;
  }
  return 0;
}

/* A rectangle w by h at xy, textured from uv (u, v, CLUT) or not
 * (MakeVertexRect, MakeTexcoordRect or MakeTexcoordQuadZero,
 * MakeColourQuad, TriangulateQuad) */
static void rect(int semi, int shade, u_int xy, const u_int *uv, int w, int h, const u_char *col) {
  int ox, oy, i;
  GrVertex *v;
  psx_gpu_add_split(semi, uv != NULL);
  if (!(v = room6()))
    return;
  draw_offset(&ox, &oy);
  memset(v, 0, sizeof(GrVertex) * 4);
  v[0].x = (short)(sx(xy) + ox);
  v[0].y = (short)(sy(xy) + oy);
  v[1].x = v[0].x;
  v[1].y = (short)(v[0].y + h);
  v[2].x = (short)(v[0].x + w);
  v[2].y = (short)(v[0].y + h);
  v[3].x = (short)(v[0].x + w);
  v[3].y = v[0].y;
  if (uv) {
    const u_char u0 = (u_char)*uv, v0 = (u_char)(*uv >> 8);
    const short page = (short)activeDrawEnv.tpage, clut = (short)(*uv >> 16);
    short tw = (short)w, th = (short)h;
    if (u0 + tw > 255) /* PsyCross's overflow */
      tw = (short)(255 - u0);
    if (v0 + th > 255)
      th = (short)(255 - v0);
    for (i = 0; i < 4; i++) {
      v[i].bright = 2;
      v[i].page = page;
      v[i].clut = clut;
      if (g_cfg_bilinearFiltering)
        v[i].tcx = v[i].tcy = -1;
    }
    v[0].u = u0, v[0].v = v0;
    v[1].u = u0, v[1].v = (u_char)(v0 + th);
    v[2].u = (u_char)(u0 + tw), v[2].v = (u_char)(v0 + th);
    v[3].u = (u_char)(u0 + tw), v[3].v = v0;
  } else {
    for (i = 0; i < 4; i++)
      v[i].bright = 1;
  }
  for (i = 0; i < 4; i++) {
    if (shade) {
      set_rgb(&v[i], col);
    } else {
      v[i].r = v[i].g = v[i].b = 128;
      v[i].a = 255;
    }
  }
  triangulate(v);
  s_nv += 6;
}

/* GP0 60h-7Fh: TILE, SPRT and their fixed sizes (ProcessTileAndSprt) */
static int parse_rect(const u_int *c) {
  const u_int code = c[0] >> 24;
  const int semi = (code & 2) != 0, shade = (code & 1) == 0;
  const u_char *col = (const u_char *)c;
  switch (code & 0xFD) {
  case 0x60: /* TILE */
    rect(semi, shade, c[1], NULL, sx(c[2]), sy(c[2]), col);
    return 3;
  case 0x64: /* SPRT */
    rect(semi, shade, c[1], &c[2], sx(c[3]), sy(c[3]), col);
    return 4;
  case 0x68: /* TILE_1 */
    rect(semi, 1, c[1], NULL, 1, 1, col);
    return 2;
  case 0x70: /* TILE_8 */
    rect(semi, 1, c[1], NULL, 8, 8, col);
    return 2;
  case 0x74: /* SPRT_8 */
    rect(semi, shade, c[1], &c[2], 8, 8, col);
    return 3;
  case 0x78: /* TILE_16 */
    rect(semi, 1, c[1], NULL, 16, 16, col);
    return 2;
  case 0x7C: /* SPRT_16 */
    rect(semi, shade, c[1], &c[2], 16, 16, col);
    return 3;
  }
  return 0;
}

/* GP0 E0h-EFh, len words of them (ProcessDrawEnv): draw mode, texture
 * window, drawing area, offset; the mask setting is only logged. Stops at
 * E0h (PsyCross's next primitive), returning the words taken. */
static int parse_env(const u_int *c, int len) {
  int i;
  for (i = 0; i < len; i++) {
    const u_int w = c[i];
    switch (w >> 24 & 0x0F) {
    case 0x1: /* DR_TPAGE */
      if (g_psx_gpu_mutant != 2)
        activeDrawEnv.tpage = (u_short)(w & 0x1FF);
      activeDrawEnv.dtd = (u_char)((w >> 9) & 1);
      activeDrawEnv.dfe = (u_char)((w >> 10) & 1);
      break;
    case 0x2: /* DR_TWIN */
      activeDrawEnv.tw.w = (short)(w & 0x1F);
      activeDrawEnv.tw.h = (short)((w >> 5) & 0x1F);
      activeDrawEnv.tw.x = (short)((w >> 10) & 0x1F);
      activeDrawEnv.tw.y = (short)((w >> 15) & 0x1F);
      break;
    case 0x3: /* DR_AREA */
      activeDrawEnv.clip.x = (short)(w & 1023);
      activeDrawEnv.clip.y = (short)((w >> 10) & 1023);
      break;
    case 0x4: /* DR_AREA, its second word */
      activeDrawEnv.clip.w = (short)(w & 1023);
      activeDrawEnv.clip.h = (short)((w >> 10) & 1023);
      activeDrawEnv.clip.w -= activeDrawEnv.clip.x;
      activeDrawEnv.clip.h -= activeDrawEnv.clip.y;
      break;
    case 0x5: /* DR_OFFSET */
      activeDrawEnv.ofs[0] = (short)(w & 2047);
      activeDrawEnv.ofs[1] = (short)((w >> 11) & 2047);
      break;
    case 0x6:
      PsyX_Log("[Psy-X] Mask setting: %08x\n", w);
      break;
    case 0x0:
      return i;
    }
  }
  return i;
}

/* the fill, DR_MOVE and DR_LOAD go to libgpu (the check sees them first) */
static void vram_fill(const RECT16 *r, int cr, int cg, int cb) {
  if (g_psx_gpu_check)
    g_psx_gpu_check->vram(0, r, cr, cg, cb, NULL);
  ClearImage((RECT16 *)r, (u_char)cr, (u_char)cg, (u_char)cb);
}

/* One command of words words (len: what a tag over it alone would say),
 * as PsyCross's ParsePrimitive: returns PsyCross's length, 0 for an
 * unhandled one (logged as PsyCross does). */
static int parse_command(const u_int *c, int len) {
  const u_int code = c[0] >> 24;
  int n = 0;
  switch (code & 0xF0) {
  case 0x00:
    if ((code & 0x0F) == 0x0) {
      n = 3;
    } else if ((code & 0x0F) == 0x1) {
      /* DR_MOVE (code[0..4]: c[0..4]); only DrawPrim reaches it: the GP0
       * split takes its 01h as a NOP and its 80h as unhandled */
      RECT16 r;
      const int x = (int)(c[3] & 0xFFFF), y = (int)(c[3] >> 16 & 0xFFFF);
      memcpy(&r.x, &c[2], 4);
      memcpy(&r.w, &c[4], 4);
      if (g_psx_gpu_check)
        g_psx_gpu_check->vram(1, &r, x, y, 0, NULL);
      MoveImage(&r, x, y);
      n = 5;
    }
    break;
  case 0x20:
  case 0x30: /* polygons: the records' converter */
    nd_poly(c);
    n = 1; /* nonzero: PsyCross's lengths only feed the zero test */
    break;
  case 0x40:
  case 0x50:
    n = parse_lines(c);
    break;
  case 0x60:
  case 0x70:
    n = parse_rect(c);
    break;
  case 0xA0: { /* DR_LOAD: code[0..2], then the pixels */
    RECT16 r;
    memcpy(&r.x, &c[1], 4);
    memcpy(&r.w, &c[2], 4);
    if (g_psx_gpu_check)
      g_psx_gpu_check->vram(2, &r, 0, 0, 0, &c[3]);
    LoadImage(&r, (u_long *)&c[3]);
    n = len;
    break;
  }
  case 0xB0: /* PsyCross's own: a custom texture, a debug label */
    if ((code & 0x0F) == 0x1) {
      s_override = c[0] & 0xFFFFFF;
      s_override_w = (int)(c[1] & 0xFFF);
      s_override_h = (int)(c[1] >> 16 & 0xFFF);
      n = 2;
    } else if ((code & 0x0F) == 0x2) {
      s_debug_text = (const char *)(uintptr_t)c[1];
      n = 2;
    }
    break;
  case 0xE0:
    n = parse_env(c, len);
    break;
  }
  if (!n)
    PsyX_Log_Error("[Psy-X] [%s] - Unhandled zero length %0x primitive\n", "ParsePrimitive",
                   code & 0xF0);
  return n;
}

/* The words of the GP0 command at c (psx-spx "GPU Render
 * Polygon/Line/Rectangle Commands"), or 0 if it doesn't fit before end
 * (patch 0003's Gp0CommandWords) */
static int gp0_words(const u_int *c, const u_int *end) {
  const u_int code = c[0] >> 24;
  switch (code >> 5) {
  case 1: { /* polygon: colour, then per vertex xy (+uv), Gouraud adds colours */
    const int verts = (code & 8) ? 4 : 3;
    return 1 + verts * ((code & 4) ? 2 : 1) + ((code & 0x10) ? verts - 1 : 0);
  }
  case 2: { /* line, or polyline ended by a 0x5xxx5xxx word */
    const int step = (code & 0x10) ? 2 : 1;
    const u_int *w = c + 2; /* the second vertex's xy */
    if (!(code & 8))
      return (code & 0x10) ? 4 : 3;
    while (w < end && (*w & 0xF000F000) != 0x50005000)
      w += step;
    return w < end ? (int)(w - c) + 1 : 0;
  }
  case 3: /* rectangle: colour, xy, (uv+CLUT), (wh if variable size) */
    return 2 + ((code & 4) ? 1 : 0) + (((code >> 3) & 3) == 0 ? 1 : 0);
  case 4: /* VRAM to VRAM */
    return 4;
  case 5: /* CPU to VRAM: command, xy, wh, then the pixels */
    return c + 2 < end ? 3 + (int)(((c[2] & 0xFFFF) * (c[2] >> 16) + 1) / 2) : 0;
  case 6: /* VRAM to CPU */
    return 3;
  case 7: /* environment (E1h-E6h) */
    return 1;
  default: /* 00h NOP, 01h cache clear, 02h fill, 1Fh IRQ */
    return code == 0x02 ? 3 : 1;
  }
}

/* A packet's GP0 commands back to back (hand-written renderers pack several
 * into one), each parsed as if it had a tag of its own */
static void parse_packet(const u_int *tag, int len) {
  const u_int *c = tag + 1, *const end = c + len;
  while (c < end) {
    const int words = gp0_words(c, end);
    u_int code;
    if (words <= 0 || words > 64 || c + words > end) {
      PsyX_Log_Error("[Psy-X] [%s] - bad GP0 command %08x in packet (len %d)\n",
                     "ParsePrimitivesLinkedList", c[0], len);
      break;
    }
    code = c[0] >> 24;
    if (code == 0x02) { /* fill rectangle */
      RECT16 r;
      r.x = (short)(c[1] & 0x3F0);
      r.y = (short)((c[1] >> 16) & 0x1FF);
      r.w = (short)(((c[2] & 0x3FF) + 0xF) & ~0xF);
      r.h = (short)((c[2] >> 16) & 0x1FF);
      vram_fill(&r, (int)(c[0] & 0xFF), (int)((c[0] >> 8) & 0xFF), (int)((c[0] >> 16) & 0xFF));
    } else if (code >= 0x20) { /* 00h-1Fh other than 02h are NOPs here */
      parse_command(c, words);
    }
    c += words;
  }
}

/* the current split's count, after each packet (and DrawPrim's one) */
static inline void close_split(void) {
  PsxGpuSplit *s = &s_splits[s_ns];
  s->numVerts = (u_short)(s_nv - s->startVertex);
}

void psx_gpu_walk(u_long *p, int single, PsxGpuNext next) {
  uintptr_t pk;
  if (!p)
    return;
  s_prim_mode = single;
  if (single) {
    /* the primitive as one command, its tag's length as PsyCross's
     * ParsePrimitive took it */
    const P_TAG *t = (const P_TAG *)p;
    if (g_psx_gpu_check)
      g_psx_gpu_check->pre((uintptr_t)p, 1);
    parse_command((const u_int *)p + 1, (int)t->len);
    close_split();
    if (g_psx_gpu_check)
      g_psx_gpu_check->post((uintptr_t)p, 1);
    return;
  }
  for (pk = (uintptr_t)p;; pk = next ? next(pk) : (uintptr_t)nextPrim(pk)) {
    const int len = (int)getlen(pk);
    if (len > 0) {
      if (g_psx_gpu_check)
        g_psx_gpu_check->pre(pk, 0);
      if (!(g_nd_walk && nd_packet(pk))) {
        if (len > 32)
          PsyX_Log_Error("[Psy-X] [%s] - got invalid tag length %d, code %d\n",
                         "ParsePrimitivesLinkedList", len, ((const P_TAG *)pk)->code);
        parse_packet((const u_int *)pk, len);
      }
      close_split();
      if (g_psx_gpu_check)
        g_psx_gpu_check->post(pk, 0);
    } else {
      close_split();
    }
    if (isendprim(pk))
      break;
  }
}

void psx_gpu_walk_scene(u_long *p, int single, PsxGpuNext next) {
  if (PsyX_BeginScene())
    psx_gpu_clear_splits();
  psx_gpu_walk(p, single, next);
}

/* Software PS1 GPU behind PsyCross's GR_* interface for the headless linux32
 * host (docs/porting-plan.md §5). PsyX_GPU.cpp turns OT primitives into
 * GrVertex triangles and "splits"; we rasterize them into a CPU 1024x512
 * 15-bit VRAM with PS1 rules: integer vertices, top-left fill (right and
 * bottom edges not drawn), affine UVs, 4/8/16 bpp + CLUT, texel*colour/128,
 * texel 0 transparent, semi-transparency on STP texels, draw-area clip.
 * Not modelled: mask bit, texture window, interlace, 2 MB VRAM pages. */
#include "host.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "PsyX/PsyX_render.h"
#include "gpu/PsyX_GPU.h"

#define VW 1024
#define VH 512

static uint16_t s_vram[VW * VH];

uint16_t *host_vram(void) { return s_vram; }

TextureID g_whiteTexture = 1; /* untextured splits */
TextureID g_vramTexture = 2;  /* textured splits */
int g_cfg_bilinearFiltering = 0;

static const GrVertex *s_verts;
static int s_nverts;
static TextureID s_tex;
static TexFormat s_fmt;
static BlendMode s_blend;
static int s_clip_x0, s_clip_y0, s_clip_x1, s_clip_y1; /* inclusive */
static int s_org_x, s_org_y; /* VRAM position of vertex (0,0) */

/* PsyX_GPU.cpp's DrawSplit passes &split.drawenv.clip, and GPUDrawSplit
 * starts with { DRAWENV drawenv; DISPENV dispenv; } (both packed), so the
 * rect gives us the env the split's vertices were built with. */
typedef struct {
  DRAWENV drawenv;
  DISPENV dispenv;
} SplitHead;

/* enable = drawenv.dfe. On screen, PsyX built vertices as
 * prim + ofs - disp.xy, so VRAM = vertex + disp.xy (= prim + ofs, as on a
 * PS1). Off screen it used the raw prim xy; the PS1 adds the draw offset. */
void GR_SetupClipMode(const RECT16 *rect, int enable) {
  const SplitHead *sp = (const SplitHead *)rect;
  if (enable) {
    s_org_x = sp->dispenv.disp.x;
    s_org_y = sp->dispenv.disp.y;
  } else {
    s_org_x = sp->drawenv.ofs[0];
    s_org_y = sp->drawenv.ofs[1];
  }
  s_clip_x0 = rect->x < 0 ? 0 : rect->x;
  s_clip_y0 = rect->y < 0 ? 0 : rect->y;
  s_clip_x1 = rect->x + rect->w - 1;
  s_clip_y1 = rect->y + rect->h - 1;
  if (s_clip_x1 > VW - 1)
    s_clip_x1 = VW - 1;
  if (s_clip_y1 > VH - 1)
    s_clip_y1 = VH - 1;
}

void GR_SetOffscreenState(const RECT16 *rect, int enable) {
  (void)rect;
  (void)enable; /* handled in GR_SetupClipMode */
}

void GR_SetTexture(TextureID texture, TexFormat texFormat) {
  s_tex = texture;
  s_fmt = texFormat;
}

void GR_SetBlendMode(BlendMode blendMode) { s_blend = blendMode; }
void GR_SetStencilMode(int drawPrim) { (void)drawPrim; }
void GR_SetOverrideTextureSize(int width, int height) {
  (void)width;
  (void)height;
}
void GR_PushDebugLabel(const char *label) { (void)label; }
void GR_PopDebugLabel(void) {}
void GR_UpdateVRAM(void) {}
void GR_ReadFramebufferDataToVRAM(void) {}

void GR_UpdateVertexBuffer(const GrVertex *vertices, int count) {
  s_verts = vertices;
  s_nverts = count;
}

/* ---- VRAM transfers (coordinates wrap like the PS1's) ---- */

void GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g,
                  unsigned char b) {
  uint16_t c = (uint16_t)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
  int i, j;
  for (i = 0; i < h; i++) {
    uint16_t *row = s_vram + ((y + i) & (VH - 1)) * VW;
    for (j = 0; j < w; j++)
      row[(x + j) & (VW - 1)] = c;
  }
}

/* PsyX's GL backend clears the window here; our window is VRAM. */
void GR_Clear(int x, int y, int w, int h, unsigned char r, unsigned char g,
              unsigned char b) {
  GR_ClearVRAM(x, y, w, h, r, g, b);
}

/* LoadImage (src = host data, stride w) or MoveImage (src = NULL, VRAM). */
void GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x,
                 int dst_y) {
  int i, j;
  if (w <= 0 || h <= 0)
    return;
  if (!src) {
    static uint16_t tmp[VW * VH];
    for (i = 0; i < h; i++)
      for (j = 0; j < w; j++)
        tmp[i * w + j] = s_vram[((y + i) & (VH - 1)) * VW + ((x + j) & (VW - 1))];
    src = tmp;
    x = y = 0;
  }
  for (i = 0; i < h; i++) {
    const uint16_t *s = (const uint16_t *)src + (y + i) * w + x;
    uint16_t *row = s_vram + ((dst_y + i) & (VH - 1)) * VW;
    for (j = 0; j < w; j++)
      row[(dst_x + j) & (VW - 1)] = s[j];
  }
}

void GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h) {
  int i, j;
  for (i = 0; i < dst_h; i++) {
    const uint16_t *row = s_vram + ((y + i) & (VH - 1)) * VW;
    for (j = 0; j < dst_w; j++)
      *dst++ = row[(x + j) & (VW - 1)];
  }
}

/* ---- rasterizer ---- */

static const int k_dither[4][4] = {
    {-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

static int floor_div(int a, int b) {
  int q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0)))
    q--;
  return q;
}

static int ceil_div(int a, int b) { return -floor_div(-a, b); }

static uint16_t texel(int fmt, int bx, int by, int cx, int cy, int u, int v) {
  const uint16_t *row = s_vram + ((by + v) & (VH - 1)) * VW;
  const uint16_t *clut = s_vram + cy * VW;
  uint16_t w;
  switch (fmt) {
  case 0:
    w = row[(bx + (u >> 2)) & (VW - 1)];
    return clut[(cx + ((w >> ((u & 3) * 4)) & 0xF)) & (VW - 1)];
  case 1:
    w = row[(bx + (u >> 1)) & (VW - 1)];
    return clut[(cx + ((w >> ((u & 1) * 8)) & 0xFF)) & (VW - 1)];
  default:
    return row[(bx + u) & (VW - 1)];
  }
}

static inline int clamp8(int c) { return c < 0 ? 0 : c > 255 ? 255 : c; }

/* 16.16 plane gradients of a over the triangle (x relative to vertex 0). */
static void gradients(int a0, int a1, int a2, int X1, int Y1, int X2, int Y2,
                      int area, int32_t *dx, int32_t *dy) {
  int d1 = a1 - a0, d2 = a2 - a0;
  *dx = (int32_t)((int64_t)(d1 * Y2 - d2 * Y1) * 65536 / area);
  *dy = (int32_t)((int64_t)(d2 * X1 - d1 * X2) * 65536 / area);
}

static void raster_tri(const GrVertex *v0, const GrVertex *v1,
                       const GrVertex *v2) {
  const GrVertex *v[3];
  int x[3], y[3], i, py;
  int ea[3], eb[3], ec[3], ebias[3];
  int area, ymin, ymax;
  int textured = s_tex == g_vramTexture;
  int dither = g_host_opts.dither && v0->dither;
  int fmt = 0, bx = 0, by = 0, cx = 0, cy = 0;
  int32_t drdx, drdy, dgdx, dgdy, dbdx, dbdy, dudx, dudy, dvdx, dvdy;

  v[0] = v0;
  v[1] = v1;
  v[2] = v2;
  for (i = 0; i < 3; i++) {
    x[i] = v[i]->x + s_org_x;
    y[i] = v[i]->y + s_org_y;
  }
  /* The GPU skips polygons with an edge spanning >= 1024 x or >= 512 y. */
  for (i = 0; i < 3; i++) {
    int j = (i + 1) % 3;
    int dx = x[i] - x[j], dy = y[i] - y[j];
    if (dx >= 1024 || dx <= -1024 || dy >= 512 || dy <= -512)
      return;
  }
  area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
  if (area == 0)
    return;
  if (area < 0) {
    const GrVertex *tv = v[1];
    int t;
    v[1] = v[2];
    v[2] = tv;
    t = x[1], x[1] = x[2], x[2] = t;
    t = y[1], y[1] = y[2], y[2] = t;
    area = -area;
  }

  /* E(p) = cross(b - a, p - a) >= 0 inside; on-edge pixels only for top or
   * left edges (inward normal pointing right, or down when horizontal). */
  for (i = 0; i < 3; i++) {
    int a = i, b = (i + 1) % 3;
    ea[i] = y[a] - y[b];
    eb[i] = x[b] - x[a];
    ec[i] = -ea[i] * x[a] - eb[i] * y[a];
    ebias[i] = (ea[i] > 0 || (ea[i] == 0 && eb[i] > 0)) ? 0 : 1;
  }

  ymin = y[0] < y[1] ? y[0] : y[1];
  ymin = y[2] < ymin ? y[2] : ymin;
  ymax = y[0] > y[1] ? y[0] : y[1];
  ymax = y[2] > ymax ? y[2] : ymax;
  if (ymin < s_clip_y0)
    ymin = s_clip_y0;
  if (ymax > s_clip_y1)
    ymax = s_clip_y1;
  if (ymin > ymax || s_clip_x0 > s_clip_x1)
    return;

  {
    int X1 = x[1] - x[0], Y1 = y[1] - y[0], X2 = x[2] - x[0], Y2 = y[2] - y[0];
    gradients(v[0]->r, v[1]->r, v[2]->r, X1, Y1, X2, Y2, area, &drdx, &drdy);
    gradients(v[0]->g, v[1]->g, v[2]->g, X1, Y1, X2, Y2, area, &dgdx, &dgdy);
    gradients(v[0]->b, v[1]->b, v[2]->b, X1, Y1, X2, Y2, area, &dbdx, &dbdy);
    gradients(v[0]->u, v[1]->u, v[2]->u, X1, Y1, X2, Y2, area, &dudx, &dudy);
    gradients(v[0]->v, v[1]->v, v[2]->v, X1, Y1, X2, Y2, area, &dvdx, &dvdy);
  }
  if (textured) {
    uint16_t page = (uint16_t)v[0]->page, clut = (uint16_t)v[0]->clut;
    fmt = (page >> 7) & 3;
    bx = (page & 0xF) * 64;
    by = ((page >> 4) & 1) * 256;
    cx = (clut & 0x3F) * 16;
    cy = (clut >> 6) & (VH - 1);
  }

  for (py = ymin; py <= ymax; py++) {
    int xl = s_clip_x0, xr = s_clip_x1, px;
    int32_t R, G, B, U, V;
    uint16_t *row;
    const int *drow = k_dither[py & 3];
    for (i = 0; i < 3; i++) {
      int k = ebias[i] - eb[i] * py - ec[i]; /* need ea*px >= k */
      if (ea[i] > 0) {
        int lo = ceil_div(k, ea[i]);
        if (lo > xl)
          xl = lo;
      } else if (ea[i] < 0) {
        int hi = floor_div(k, ea[i]);
        if (hi < xr)
          xr = hi;
      } else if (k > 0) {
        xr = xl - 1;
      }
    }
    if (xl > xr)
      continue;

    {
      int64_t ox = xl - x[0], oy = py - y[0];
#define START(a, dx, dy)                                                       \
  (int32_t)((int64_t)(a) * 65536 + 0x8000 + (dx) * ox + (dy) * oy)
      R = START(v[0]->r, drdx, drdy);
      G = START(v[0]->g, dgdx, dgdy);
      B = START(v[0]->b, dbdx, dbdy);
      U = START(v[0]->u, dudx, dudy);
      V = START(v[0]->v, dvdx, dvdy);
#undef START
    }

    row = s_vram + py * VW;
    for (px = xl; px <= xr; px++, R += drdx, G += dgdx, B += dbdx, U += dudx,
        V += dvdx) {
      int r = R >> 16, g = G >> 16, b = B >> 16;
      int semi = s_blend != BM_NONE;
      uint16_t out, stp = 0;
      if (textured) {
        uint16_t t = texel(fmt, bx, by, cx, cy, (U >> 16) & 0xFF,
                           (V >> 16) & 0xFF);
        if (t == 0)
          continue;
        stp = t & 0x8000;
        semi = semi && stp;
        r = ((t & 0x1F) << 3) * r >> 7;
        g = (((t >> 5) & 0x1F) << 3) * g >> 7;
        b = (((t >> 10) & 0x1F) << 3) * b >> 7;
      }
      if (dither) {
        int d = drow[px & 3];
        r += d, g += d, b += d;
      }
      r = clamp8(r) >> 3;
      g = clamp8(g) >> 3;
      b = clamp8(b) >> 3;
      if (semi) {
        uint16_t d = row[px];
        int br = d & 0x1F, bg = (d >> 5) & 0x1F, bb = (d >> 10) & 0x1F;
        switch (s_blend) {
        case BM_AVERAGE:
          r = (br + r) >> 1, g = (bg + g) >> 1, b = (bb + b) >> 1;
          break;
        case BM_ADD:
          r += br, g += bg, b += bb;
          break;
        case BM_SUBTRACT:
          r = br - r, g = bg - g, b = bb - b;
          break;
        default: /* BM_ADD_QUATER_SOURCE */
          r = br + (r >> 2), g = bg + (g >> 2), b = bb + (b >> 2);
          break;
        }
        r = r < 0 ? 0 : r > 31 ? 31 : r;
        g = g < 0 ? 0 : g > 31 ? 31 : g;
        b = b < 0 ? 0 : b > 31 ? 31 : b;
      }
      out = (uint16_t)(r | g << 5 | b << 10 | stp);
      row[px] = out;
    }
  }
}

/* --tri-dump V: one log line per triangle drawn after vblank V (DrawOTag
 * runs between vblanks; the next shot or two shows that frame). */
static void dump_tri(const GrVertex *p) {
  int k;
  char buf[512], *o = buf;
  o += sprintf(o, "tri %s blend %d page %04x clut %04x org %d,%d",
               s_tex == g_vramTexture ? "tex" : "flat", (int)s_blend,
               (uint16_t)p[0].page, (uint16_t)p[0].clut, s_org_x, s_org_y);
  for (k = 0; k < 3; k++)
    o += sprintf(o, " | %d,%d uv %d,%d rgb %d,%d,%d", p[k].x, p[k].y, p[k].u,
                 p[k].v, p[k].r, p[k].g, p[k].b);
  host_log("%s", buf);
}

void GR_DrawTriangles(int start_vertex, int triangles) {
  int i;
  if (!s_verts || start_vertex < 0 || start_vertex + triangles * 3 > s_nverts)
    return;
  if (s_tex != g_vramTexture && s_tex != g_whiteTexture) {
    static int warned;
    if (!warned++)
      host_log("gpu: PsyX custom textures (DR_PSYX_TEX) are not drawn");
    return;
  }
  for (i = 0; i < triangles; i++) {
    const GrVertex *p = s_verts + start_vertex + i * 3;
    if (g_host_opts.tri_dump && host_vblank_count() == g_host_opts.tri_dump)
      dump_tri(p);
    raster_tri(&p[0], &p[1], &p[2]);
  }
}

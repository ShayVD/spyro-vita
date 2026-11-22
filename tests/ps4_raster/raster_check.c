/* raster_mt.c (the PS4's software rasterizer on several threads, D194)
 * against gpu_soft.c (linux32's, one thread): the same random splits from
 * the same VRAM must leave the same VRAM, byte for byte. Textured (4, 8 and
 * 15-bit pages, random CLUTs, at x >= 512 as the game's) and untextured
 * triangles (drawn left of x 512), every blend mode,
 * dither off and on, random clips and offsets, several walks (vertex
 * buffers) and transfers between them.
 *   tests/ps4_raster/run.sh   (any host C compiler with pthreads) */
#include "host.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"

HostOpts g_host_opts;
int host_vblank_count(void) { return 0; }
void host_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
}
unsigned long long ps4_now_us(void) { return 0; }
TextureID g_whiteTexture = 1;
TextureID g_vramTexture = 2;

/* gpu_soft.c, soft_ names */
uint16_t *soft_host_vram(void);
void soft_GR_SetupClipMode(const RECT16 *rect, int enable);
void soft_GR_SetTexture(TextureID texture, TexFormat texFormat);
void soft_GR_SetBlendMode(BlendMode blendMode);
void soft_GR_UpdateVertexBuffer(const GrVertex *vertices, int count);
void soft_GR_DrawTriangles(int start_vertex, int triangles);
void soft_GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g,
                       unsigned char b);
/* raster_mt.c */
int ps4_raster_init(void);
void ps4_raster_clip(const RECT16 *rect, int enable);
void ps4_raster_texture(TextureID tex);
void ps4_raster_blend(BlendMode m);
void ps4_raster_vertices(const GrVertex *v, int n);
void ps4_raster_draw(int start, int ntri);
void ps4_raster_sync(void);

#define VWORDS (1024 * 512)
#define NV 3000
#define WALKS 40

static uint32_t s_seed = 1;
static uint32_t rnd(void) {
  s_seed = s_seed * 1103515245u + 12345u;
  return s_seed >> 8;
}

typedef struct {
  DRAWENV drawenv;
  DISPENV dispenv;
} SplitHead;

typedef struct {
  SplitHead head;
  int enable, textured, blend, start, ntri;
} Split;

static GrVertex s_vb[WALKS][NV];
static Split s_splits[WALKS][64];
static int s_nsplits[WALKS];

static void make(void) {
  int w, i;
  for (w = 0; w < WALKS; w++) {
    int nv = 0;
    s_nsplits[w] = 0;
    while (nv + 90 <= NV && s_nsplits[w] < 64) {
      Split *s = &s_splits[w][s_nsplits[w]++];
      /* the game's layout: textures and CLUTs at x >= 512, draws left of it
       * (a draw that samples what the same batch draws isn't modelled) */
      int ntri = 1 + rnd() % 30, fmt = (int)(rnd() % 3);
      /* a page's texels (64, 128, 256 words wide) and a CLUT (16, 256) inside
       * x 512..1023: no wrap round into the draws */
      int page = (int)(8 + rnd() % (fmt == 0 ? 8 : fmt == 1 ? 7 : 5)) | (int)(rnd() % 2) << 4 |
                 fmt << 7;
      int clut = (int)(32 + rnd() % (fmt == 0 ? 32 : 17)) | (int)(rnd() % 512) << 6;
      memset(s, 0, sizeof *s);
      s->head.drawenv.clip.x = (short)(rnd() % 300) - 40;
      s->head.drawenv.clip.y = (short)(rnd() % 300) - 20;
      s->head.drawenv.clip.w = (short)(rnd() % (512 - (s->head.drawenv.clip.x > 0 ? s->head.drawenv.clip.x : 0)));
      s->head.drawenv.clip.h = (short)(rnd() % 300);
      s->head.drawenv.ofs[0] = (short)(rnd() % 600);
      s->head.drawenv.ofs[1] = (short)(rnd() % 300);
      s->head.dispenv.disp.x = (short)(rnd() % 600);
      s->head.dispenv.disp.y = (short)(rnd() % 300);
      s->enable = rnd() & 1;
      s->textured = rnd() & 1;
      s->blend = (int)(rnd() % 5);
      s->start = nv;
      s->ntri = ntri;
      for (i = 0; i < ntri * 3; i++) {
        GrVertex *v = &s_vb[w][nv++];
        memset(v, 0, sizeof *v);
        v->x = (short)(rnd() % 700) - 100;
        v->y = (short)(rnd() % 400) - 80;
        v->page = (short)page;
        v->clut = (short)clut;
        v->u = (u_char)rnd(), v->v = (u_char)rnd();
        v->bright = s->textured ? 2 : 1;
        v->dither = rnd() & 1;
        v->r = (u_char)rnd(), v->g = (u_char)rnd(), v->b = (u_char)rnd();
        if (i % 3 == 2 && (rnd() & 7) == 0) /* a big one now and then */
          v->x = (short)(v->x * 3);
      }
    }
  }
}

static void play(int mt) {
  int w, i;
  for (w = 0; w < WALKS; w++) {
    if (mt)
      ps4_raster_vertices(s_vb[w], NV);
    else
      soft_GR_UpdateVertexBuffer(s_vb[w], NV);
    for (i = 0; i < s_nsplits[w]; i++) {
      const Split *s = &s_splits[w][i];
      const RECT16 *clip = &s->head.drawenv.clip;
      TextureID tex = s->textured ? g_vramTexture : g_whiteTexture;
      if (mt) {
        ps4_raster_clip(clip, s->enable);
        ps4_raster_texture(tex);
        ps4_raster_blend((BlendMode)s->blend);
        ps4_raster_draw(s->start, s->ntri);
      } else {
        soft_GR_SetupClipMode(clip, s->enable);
        soft_GR_SetTexture(tex, TF_16_BIT);
        soft_GR_SetBlendMode((BlendMode)s->blend);
        soft_GR_DrawTriangles(s->start, s->ntri);
      }
    }
    if (w % 7 == 3) { /* a transfer between walks, as ClearImage */
      if (mt)
        ps4_raster_sync();
      soft_GR_ClearVRAM(w * 13, w * 7, 50, 40, (unsigned char)(w * 5), 20, 200);
    }
  }
  if (mt)
    ps4_raster_sync();
}

int main(void) {
  static uint16_t init[VWORDS], ref[VWORDS];
  int i, d, fails = 0;
  if (ps4_raster_init() != 0)
    return 2;
  for (d = 0; d < 2; d++) {
    g_host_opts.dither = d;
    s_seed = 12345u + (uint32_t)d;
    for (i = 0; i < VWORDS; i++)
      init[i] = (uint16_t)rnd();
    for (i = 0; i < VWORDS; i += 37) /* some transparent texels */
      init[i] = 0;
    make();
    memcpy(soft_host_vram(), init, sizeof init);
    play(0);
    memcpy(ref, soft_host_vram(), sizeof ref);
    memcpy(soft_host_vram(), init, sizeof init);
    play(1);
    {
      int diff = 0, changed = 0;
      for (i = 0; i < VWORDS; i++) {
        diff += soft_host_vram()[i] != ref[i];
        changed += init[i] != ref[i];
      }
      printf("dither %d: %d words drawn by the reference, %d differ\n", d, changed, diff);
      {
        int shown = 0;
        for (i = 0; i < VWORDS && shown < 8; i++)
          if (soft_host_vram()[i] != ref[i])
            printf("  at %d,%d: %04x, the reference %04x (was %04x)\n", i % 1024, i / 1024,
                   soft_host_vram()[i], ref[i], init[i]), shown++;
      }
      if (diff || changed < 10000)
        fails++;
    }
  }
  printf("%s\n", fails ? "FAIL" : "OK: raster_mt.c draws what gpu_soft.c draws");
  return fails != 0;
}

/* The software rasterizer on several cores (decisions D194): linux32's
 * gpu_soft.c rasterizer (PS1 rules: integer vertices, top-left fill, affine
 * UVs, 4/8/16 bpp + CLUT, texel*colour/128, texel 0 transparent, STP
 * semi-transparency, draw-area clip) copied with its state in a record, so
 * that several threads can draw the same splits at once, each its own rows.
 *
 * Why: on the PS4's 1.6 GHz cores one 16:9 draw cost ~13 ms (GPU run 2), and
 * 60 fps draws two a tick (the logic draw and the presenter's in-between
 * frame), so the game ran ~33 pictures a second.
 *
 * How: gr_ps4.c records each GR_DrawTriangles (the split's clip, offset,
 * texture and blend, the triangles' place in the walk's vertex buffer).
 * Nothing is drawn until something needs VRAM: a new vertex buffer (the
 * next walk), a VRAM transfer or read (ClearImage, LoadImage, MoveImage,
 * StoreImage), host_vram() (the present, the shots, the save states, the
 * GPU's upload). Then the game thread and NTHREADS-1 workers draw every
 * recorded split, in order, thread k only the rows y with (y >> 2) % NTHREADS
 * == k. Every pixel is drawn by one thread, in the order the one-thread
 * rasterizer draws it, so VRAM ends up the same (tests/ps4_raster checks it
 * against gpu_soft.c). Not the same only where a draw samples texels another
 * thread's rows are writing in the same batch (the game doesn't). */
#include "ps4.h"
#include "host.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "psx_gpu.h"

#define VW 1024
#define VH 512
#define NTHREADS 4
#define MAX_RECS 8192

uint16_t *soft_host_vram(void);

typedef struct {
  int start, ntri;
  int textured, blend;
  int cx0, cy0, cx1, cy1; /* inclusive */
  int org_x, org_y;
} Rec;

static Rec s_rec[MAX_RECS];
static int s_nrec;
static const GrVertex *s_verts;
static int s_nverts;
/* the split state gr_ps4.c sets */
static Rec s_state;
static int s_on; /* the workers are up */

static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_go = PTHREAD_COND_INITIALIZER, s_done = PTHREAD_COND_INITIALIZER;
static unsigned s_gen;  /* a batch is ready when it changes */
static int s_pending;   /* workers still drawing the batch */
static int s_batch_n;   /* the batch's records */

static unsigned long long s_us_draw, s_us_wait;
static unsigned s_batches, s_tris;

/* ---- the rasterizer (gpu_soft.c's, with the band) ---- */

static const int k_dither[4][4] = {
    {-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

static int floor_div(int a, int b) {
  int q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0)))
    q--;
  return q;
}

static int ceil_div(int a, int b) { return -floor_div(-a, b); }

static uint16_t texel(const uint16_t *vram, int fmt, int bx, int by, int cx, int cy, int u,
                      int v) {
  const uint16_t *row = vram + ((by + v) & (VH - 1)) * VW;
  const uint16_t *clut = vram + cy * VW;
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

static int32_t div16(int32_t n, int area) {
  int32_t q, r;
  if (area >= 32768)
    return (int32_t)((int64_t)n * 65536 / area);
  q = n / area;
  r = n % area;
  return (int32_t)((uint32_t)q * 65536u + (uint32_t)(r * 65536 / area));
}

static void gradients(int a0, int a1, int a2, int X1, int Y1, int X2, int Y2, int area, int32_t *dx,
                      int32_t *dy) {
  int d1 = a1 - a0, d2 = a2 - a0;
  *dx = div16(d1 * Y2 - d2 * Y1, area);
  *dy = div16(d2 * X1 - d1 * X2, area);
}

static void raster_tri(uint16_t *vram, const Rec *st, int band, const GrVertex *v0,
                       const GrVertex *v1, const GrVertex *v2) {
  const GrVertex *v[3];
  int x[3], y[3], i, py;
  int ea[3], eb[3], ec[3], ebias[3];
  int area, ymin, ymax;
  int textured = st->textured;
  int dither = g_host_opts.dither && v0->dither;
  int fmt = 0, bx = 0, by = 0, cx = 0, cy = 0;
  int32_t drdx, drdy, dgdx, dgdy, dbdx, dbdy, dudx, dudy, dvdx, dvdy;

  v[0] = v0;
  v[1] = v1;
  v[2] = v2;
  for (i = 0; i < 3; i++) {
    x[i] = v[i]->x + st->org_x;
    y[i] = v[i]->y + st->org_y;
  }
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
  if (ymin < st->cy0)
    ymin = st->cy0;
  if (ymax > st->cy1)
    ymax = st->cy1;
  if (ymin > ymax || st->cx0 > st->cx1)
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
    int xl = st->cx0, xr = st->cx1, px;
    int32_t R, G, B, U, V;
    uint16_t *row;
    const int *drow = k_dither[py & 3];
    if (((py >> 2) & (NTHREADS - 1)) != band) /* another thread's rows */
      continue;
    for (i = 0; i < 3; i++) {
      int k = ebias[i] - eb[i] * py - ec[i];
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
#define START(a, dx, dy) (int32_t)((int64_t)(a) * 65536 + 0x8000 + (dx) * ox + (dy) * oy)
      R = START(v[0]->r, drdx, drdy);
      G = START(v[0]->g, dgdx, dgdy);
      B = START(v[0]->b, dbdx, dbdy);
      U = START(v[0]->u, dudx, dudy);
      V = START(v[0]->v, dvdx, dvdy);
#undef START
    }
    row = vram + py * VW;
    for (px = xl; px <= xr; px++, R += drdx, G += dgdx, B += dbdx, U += dudx, V += dvdx) {
      int r = R >> 16, g = G >> 16, b = B >> 16;
      int semi = st->blend != BM_NONE;
      uint16_t out, stp = 0;
      if (textured) {
        uint16_t t = texel(vram, fmt, bx, by, cx, cy, (U >> 16) & 0xFF, (V >> 16) & 0xFF);
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
        switch (st->blend) {
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

/* one thread's rows of the batch's first n records */
static void draw_band(int band, int n) {
  uint16_t *vram = soft_host_vram();
  int r, i;
  for (r = 0; r < n; r++) {
    const Rec *st = &s_rec[r];
    const GrVertex *p = s_verts + st->start;
    for (i = 0; i < st->ntri; i++, p += 3)
      raster_tri(vram, st, band, &p[0], &p[1], &p[2]);
  }
}

static void *worker(void *arg) {
  int band = (int)(intptr_t)arg;
  unsigned seen = 0;
  for (;;) {
    int n;
    pthread_mutex_lock(&s_mu);
    while (s_gen == seen)
      pthread_cond_wait(&s_go, &s_mu);
    seen = s_gen;
    n = s_batch_n;
    pthread_mutex_unlock(&s_mu);
    draw_band(band, n);
    pthread_mutex_lock(&s_mu);
    if (--s_pending == 0)
      pthread_cond_signal(&s_done);
    pthread_mutex_unlock(&s_mu);
  }
  return NULL;
}

int ps4_raster_init(void) {
  int k;
  for (k = 1; k < NTHREADS; k++) {
    pthread_t th;
    if (pthread_create(&th, NULL, worker, (void *)(intptr_t)k) != 0) {
      host_log("raster: worker %d didn't start: drawing on one thread", k);
      return -1; /* (the workers already started wait forever: harmless) */
    }
  }
  s_on = 1;
  host_log("raster: the software rasterizer on %d threads (D194)", NTHREADS);
  return 0;
}

int ps4_raster_on(void) { return s_on; }

/* every recorded split drawn into VRAM */
void ps4_raster_sync(void) {
  unsigned long long t0, t1;
  int n = s_nrec;
  if (!n)
    return;
  t0 = ps4_now_us();
  g_ps4_where = "the rasterizer's threads";
  pthread_mutex_lock(&s_mu);
  s_batch_n = n;
  s_pending = NTHREADS - 1;
  s_gen++;
  pthread_cond_broadcast(&s_go);
  pthread_mutex_unlock(&s_mu);
  draw_band(0, n);
  t1 = ps4_now_us();
  pthread_mutex_lock(&s_mu);
  while (s_pending)
    pthread_cond_wait(&s_done, &s_mu);
  pthread_mutex_unlock(&s_mu);
  s_nrec = 0;
  g_ps4_where = "the game";
  s_batches++;
  s_us_draw += t1 - t0;
  s_us_wait += ps4_now_us() - t1;
}

/* ---- the state gr_ps4.c passes on (gpu_soft.c's, recorded) ---- */

typedef struct {
  DRAWENV drawenv;
  DISPENV dispenv;
} SplitHead;

void ps4_raster_clip(const RECT16 *rect, int enable) {
  const SplitHead *sp = (const SplitHead *)rect;
  if (enable) {
    s_state.org_x = sp->dispenv.disp.x;
    s_state.org_y = sp->dispenv.disp.y;
  } else {
    s_state.org_x = sp->drawenv.ofs[0];
    s_state.org_y = sp->drawenv.ofs[1];
  }
  s_state.cx0 = rect->x < 0 ? 0 : rect->x;
  s_state.cy0 = rect->y < 0 ? 0 : rect->y;
  s_state.cx1 = rect->x + rect->w - 1;
  s_state.cy1 = rect->y + rect->h - 1;
  if (s_state.cx1 > VW - 1)
    s_state.cx1 = VW - 1;
  if (s_state.cy1 > VH - 1)
    s_state.cy1 = VH - 1;
}

void ps4_raster_texture(TextureID tex) {
  s_state.textured = tex == g_vramTexture ? 1 : tex == g_whiteTexture ? 0 : -1;
}

void ps4_raster_blend(BlendMode m) { s_state.blend = (int)m; }

void ps4_raster_vertices(const GrVertex *v, int n) {
  ps4_raster_sync(); /* the recorded splits point into the old buffer */
  s_verts = v;
  s_nverts = n;
}

void ps4_raster_draw(int start, int ntri) {
  Rec *r;
  if (!s_verts || start < 0 || ntri <= 0 || start + ntri * 3 > s_nverts)
    return;
  if (s_state.textured < 0) {
    static int warned;
    if (!warned++)
      host_log("gpu: PsyX custom textures (DR_PSYX_TEX) are not drawn");
    return;
  }
  if (s_nrec == MAX_RECS)
    ps4_raster_sync();
  r = &s_rec[s_nrec++];
  *r = s_state;
  r->start = start;
  r->ntri = ntri;
  s_tris += (unsigned)ntri;
}

void ps4_raster_report(int vblank) {
  unsigned n = s_batches ? s_batches : 1;
  if (!s_on)
    return;
  host_log("raster %d: %u batches, %u triangles; the game thread's rows %llu us, waiting for the "
           "others %llu us (per batch %llu + %llu)",
           vblank, s_batches, s_tris, s_us_draw, s_us_wait, s_us_draw / n, s_us_wait / n);
  s_batches = s_tris = 0;
  s_us_draw = s_us_wait = 0;
}

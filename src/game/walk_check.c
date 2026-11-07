/* The host's walk checks against PsyCross's walk (stage 8d, D117; see
 * walk_check.h).
 *
 * --walk-check: before the walk (gpu_walk.c) parses a packet, PsyCross's
 * walk (the reference library's PsyX_GPU.cpp, renamed psyx_*) is given our
 * state (walk_ref_sync) and parses the same packet; after ours has, the two
 * must have left the same bytes:
 *  - the vertex count and every vertex the packet added;
 *  - the splits from the one open before it, whole (PsxGpuSplit is
 *    PsyCross's GPUDrawSplit, layout for layout);
 *  - the draw environment, the custom texture and the split debug label;
 *  - the VRAM calls (fill, DR_MOVE, DR_LOAD): the reference's are only
 *    recorded here, ours go to libgpu as well.
 * What the reference parses: the packet itself (a copy, as the end of a
 * list, or DrawPrim's one primitive); for a native draw marker, the PS1
 * primitives its records stand for (a record holds a primitive's words
 * after its tag, native_draw.h), which is what D82 says the marker draws.
 * Direct vertices (nd_direct=1, D99): a triangle the walk listed direct is
 * compared with the reference's after psx_lean.h's lean_direct_tri, as
 * --nd-direct-check does; every other one as it is.
 * The reference's draw and the walk's are never mixed: the check reads
 * ours and writes only the reference's state, so a run with it draws as a
 * run without it. The vertex cache's counts see each polygon twice. */
#include "walk_check.h"
#include "native_draw.h"
#include "psx_lean.h"
#include "psx_vcache.h"
#include "psx_walk.h"

#include <stdlib.h>
#include <string.h>

#if SPYRO_WALK_REF

const int g_walk_ref_built = 1;

/* PsyCross's PsyX_GPU.cpp, the reference library's (cmake/psycross.cmake) */
extern DRAWENV psyx_activeDrawEnv;
extern DISPENV psyx_activeDispEnv;
extern GrVertex psyx_g_vertexBuffer[];
extern int psyx_g_vertexIndex, psyx_g_splitIndex;
extern PsxGpuSplit psyx_g_splits[];
extern TextureID psyx_overrideTexture;
extern int psyx_overrideTextureWidth, psyx_overrideTextureHeight;
extern void (*psyx_g_psyxPolyHook)(GrVertex *vertex, short *const *xy, int n);
extern uintptr_t (*psyx_g_psyxNextPacket)(uintptr_t packet);
void psyx_ParsePrimitivesLinkedList(u_long *p, int singlePrimitive);
void psyx_ClearSplits(void);
int psyx_PsyX_SplitStateSize(void);
void psyx_PsyX_SplitStateSave(void *buf);
void psyx_PsyX_SplitStateRestore(const void *buf);

/* ---- the reference's VRAM calls: recorded while the check parses ---- */

typedef struct {
  int op;
  RECT16 r;
  int a, b, c;
  u32 hash; /* DR_LOAD: its pixels */
} VramCall;
enum { VRAM_LOG = 16 };
static struct {
  VramCall c[VRAM_LOG];
  int n;
} s_vlog[2]; /* the reference's, ours */
static int s_vlog_on;

static u32 fnv(const void *p, size_t n) {
  const u8 *b = (const u8 *)p;
  u32 h = 2166136261u;
  while (n--)
    h = (h ^ *b++) * 16777619u;
  return h;
}

static void vlog(int who, int op, const RECT16 *r, int a, int b, int c, const void *data) {
  VramCall *v;
  if (!s_vlog_on || s_vlog[who].n == VRAM_LOG)
    return;
  v = &s_vlog[who].c[s_vlog[who].n++];
  memset(v, 0, sizeof *v);
  v->op = op;
  v->r = *r;
  v->a = a;
  v->b = b;
  v->c = c;
  if (data) {
    size_t n = (size_t)(u_short)r->w * (u_short)r->h * 2;
    v->hash = fnv(data, n > 61 * 4 ? 61 * 4 : n); /* a GP0 command holds 61 words at most */
  }
}

int psyx_ref_ClearImage(RECT16 *rect, u_char r, u_char g, u_char b) {
  vlog(0, 0, rect, r, g, b, NULL);
  return 0;
}

int psyx_ref_MoveImage(RECT16 *rect, int x, int y) {
  vlog(0, 1, rect, x, y, 0, NULL);
  return 0;
}

int psyx_ref_LoadImage(RECT16 *rect, u_long *p) {
  vlog(0, 2, rect, 0, 0, 0, p);
  return 0;
}

/* ---- the reference ---- */

void walk_ref_sync(void) {
  PsxGpuSplitState st;
  int ov[3];
  psyx_activeDrawEnv = activeDrawEnv;
  psyx_activeDispEnv = activeDispEnv;
  psx_gpu_split_state_save(&st);
  psyx_PsyX_SplitStateRestore(&st);
  psx_gpu_override_get(ov);
  psyx_overrideTexture = (TextureID)ov[0];
  psyx_overrideTextureWidth = ov[1];
  psyx_overrideTextureHeight = ov[2];
  psyx_g_psyxPolyHook =
      psx_vcache_polys ? (void (*)(GrVertex *, short *const *, int))psx_vcache_poly : NULL;
}

void walk_ref_parse(u_long *p, int single, PsxGpuNext next) {
  psyx_g_psyxNextPacket = next;
  psyx_ParsePrimitivesLinkedList(p, single);
  psyx_g_psyxNextPacket = NULL;
}

GrVertex *walk_ref_vertices(int *fill) {
  *fill = psyx_g_vertexIndex;
  return psyx_g_vertexBuffer;
}

int walk_ref_split_count(void) { return psyx_g_splitIndex; }
const PsxGpuSplit *walk_ref_split(int i) { return &psyx_g_splits[i]; }

void walk_ref_split_get(int i, int out[8]) {
  const PsxGpuSplit *s = &psyx_g_splits[i];
  out[0] = s->blendMode;
  out[1] = s->texFormat;
  out[2] = (int)s->textureId;
  out[3] = s->drawPrimMode;
  out[4] = s->startVertex;
  out[5] = s->numVerts;
  out[6] = s->drawenv.dfe;
  out[7] = s->drawenv.clip.x | s->drawenv.clip.y << 16;
}

void walk_ref_clear(void) { psyx_ClearSplits(); }
DRAWENV *walk_ref_env(void) { return &psyx_activeDrawEnv; }

/* ---- --walk-check ---- */

static void (*s_log)(const char *fmt, ...);
static struct {
  unsigned long long packets, markers, single, bad, verts, direct, splits, vram;
} s_wc;

/* what the reference parses: packets of GP0 commands, each with a tag */
enum { RBUF_WORDS = 1 << 18, RPACKETS = 1 << 15 };
static u32 *s_rbuf;
static u32 *s_rat[RPACKETS];
static int s_rn, s_ri;

static uintptr_t ref_next(uintptr_t packet) {
  (void)packet;
  return (uintptr_t)s_rat[++s_ri];
}

/* before the packet: what the walk has, and the reference's parse of it */
static int s_fill0, s_split0, s_n0;
static u32 s_end0;
static const char *s_why;

static int is_marker(const u32 *m) {
  return (m[0] >> 24) == 2 && (m[1] & 0xFFF000u) == ND_MAGIC && !(m[1] >> 24);
}

/* the records of the marker m as PS1 packets of their commands: 0 if they
 * don't fit */
static int build_marker(const u32 *m) {
  const NdPool *pool = &g_nd_pool[m[1] >> 11 & 1];
  u32 at, n = 0, *tag = NULL;
  s_rn = 0;
  for (at = m[2]; at < pool->cap; at = pool->w[at]) {
    const u32 kind = pool->w[at + 1], len = ND_BODY(kind);
    if (!tag || (*tag >> 24) + len > 250) {
      if (s_rn == RPACKETS || n + 1 + len > RBUF_WORDS)
        return 0;
      tag = s_rat[s_rn++] = s_rbuf + n;
      *tag = 0;
      n++;
    }
    if (n + len > RBUF_WORDS)
      return 0;
    memcpy(s_rbuf + n, pool->w + at + 2, len * 4);
    n += len;
    *tag += len << 24;
  }
  return s_rn;
}

static void wc_pre(uintptr_t packet, int single) {
  const u32 *m = (const u32 *)packet;
  const u32 len = m[0] >> 24;
  const u32 *r;
  int *fill;
  (void)psx_gpu_vertex_buffer(&fill);
  s_fill0 = *fill;
  s_split0 = psx_gpu_split_count();
  s_n0 = nd_direct_ranges(&r);
  s_end0 = s_n0 ? r[2 * s_n0 - 1] : 0;
  s_why = NULL;
  walk_ref_sync();
  if (!single && g_nd_walk && is_marker(m)) {
    s_wc.markers++;
    if (!build_marker(m)) {
      s_why = "a marker too long to rebuild";
      return;
    }
  } else {
    s_rn = 1;
    s_rat[0] = s_rbuf;
    memcpy(s_rbuf, m, (len + 1) * 4);
  }
  if (!single)
    s_rbuf[s_rat[s_rn - 1] - s_rbuf] |= 0xFFFFFFu; /* the end */
  s_rat[s_rn] = NULL;
  s_ri = 0;
  s_vlog[0].n = s_vlog[1].n = 0;
  s_vlog_on = 1;
  walk_ref_parse((u_long *)s_rat[0], single, single ? NULL : ref_next);
  /* the log stays on for ours (wc_vram) until wc_post */
}

static void wc_vram(int op, const RECT16 *r, int a, int b, int c, const void *data) {
  vlog(1, op, r, a, b, c, data);
}

static void wc_post(uintptr_t packet, int single) {
  const u32 *m = (const u32 *)packet;
  int *fill, ref_fill, i, rn = 0, bad = 0, ns;
  const u32 *r;
  GrVertex *const vb = psx_gpu_vertex_buffer(&fill);
  const GrVertex *const rb = walk_ref_vertices(&ref_fill);
  const int lean = (psx_walk_nd() & ND_WALK_DIRECT) != 0;
  const int n = nd_direct_ranges(&r);
  int ov[3];
  PsxGpuSplitState rs;
  s_vlog_on = 0;
  s_wc.packets++;
  s_wc.single += (unsigned)single;
  if (s_why) {
    bad = 1;
    goto out;
  }
  ns = psx_gpu_split_count();
  if (*fill != ref_fill || ns != walk_ref_split_count()) {
    s_why = "vertex or split counts";
    bad = 1;
    goto out;
  }
  for (i = s_split0; i <= ns; i++)
    if (memcmp(psx_gpu_split(i), walk_ref_split(i), sizeof(PsxGpuSplit))) {
      s_why = "splits";
      bad = 2;
      goto out;
    }
  s_wc.splits += (unsigned)(ns - s_split0);
  /* the vertices, a triangle at a time (each packet adds whole ones) */
  rn = s_n0 ? s_n0 - 1 : 0;
  for (i = s_fill0; i < *fill; i += 3) {
    u32 want[15];
    int k = *fill - i < 3 ? *fill - i : 3, in;
    while (rn < n && r[2 * rn + 1] <= (u32)i)
      rn++;
    in = rn < n && r[2 * rn] <= (u32)i && (rn >= s_n0 || (u32)i >= s_end0);
    memcpy(want, rb + i, (size_t)k * sizeof(GrVertex));
    if (in && k == 3) {
      s_wc.direct += 3;
      if (lean)
        lean_direct_tri(want);
    }
    if (memcmp(vb + i, want, (size_t)k * sizeof(GrVertex))) {
      s_why = "vertices";
      bad = 3;
      if (s_wc.bad < 20 && s_log) {
        int j = 0;
        const u32 *x, *y;
        while (j < k - 1 && !memcmp(vb + i + j, want + 5 * j, sizeof(GrVertex)))
          j++;
        x = want + 5 * j;
        y = (const u32 *)(vb + i + j);
        s_log("walk check:   vertex %d (direct %d), PsyCross's / ours: %08x %08x %08x %08x %08x "
              "/ %08x %08x %08x %08x %08x", i + j, in, x[0], x[1], x[2], x[3], x[4], y[0], y[1],
              y[2], y[3], y[4]);
      }
      goto out;
    }
  }
  s_wc.verts += (unsigned)(*fill - s_fill0);
  if (memcmp(&activeDrawEnv, &psyx_activeDrawEnv, sizeof(DRAWENV))) {
    s_why = "draw environments";
    bad = 4;
    goto out;
  }
  psx_gpu_override_get(ov);
  psyx_PsyX_SplitStateSave(&rs);
  {
    PsxGpuSplitState os;
    psx_gpu_split_state_save(&os);
    if ((TextureID)ov[0] != psyx_overrideTexture || ov[1] != psyx_overrideTextureWidth ||
        ov[2] != psyx_overrideTextureHeight || os.debugText != rs.debugText) {
      s_why = "custom textures or debug labels";
      bad = 5;
      goto out;
    }
  }
  if (s_vlog[0].n != s_vlog[1].n ||
      memcmp(s_vlog[0].c, s_vlog[1].c, (size_t)s_vlog[0].n * sizeof(VramCall))) {
    s_why = "VRAM calls";
    bad = 6;
    if (s_wc.bad < 20 && s_log) {
      int j = 0;
      while (j < s_vlog[0].n && j < s_vlog[1].n &&
             !memcmp(&s_vlog[0].c[j], &s_vlog[1].c[j], sizeof(VramCall)))
        j++;
      for (i = 0; i < 2; i++) {
        const VramCall *v = &s_vlog[i].c[j];
        if (j < s_vlog[i].n)
          s_log("walk check:   %s's call %d of %d: op %d at %d,%d %dx%d (%d %d %d) data %08x",
                i ? "ours" : "PsyCross", j, s_vlog[i].n, v->op, v->r.x, v->r.y, v->r.w, v->r.h,
                v->a, v->b, v->c, v->hash);
        else
          s_log("walk check:   %s made %d calls", i ? "ours" : "PsyCross", s_vlog[i].n);
      }
    }
    goto out;
  }
  s_wc.vram += (unsigned)s_vlog[0].n;
out:
  if (bad && s_wc.bad++ < 20 && s_log)
    s_log("walk check: packet %llu (len %u, first command %08x%s): %s differ (vertices %d/%d, "
          "splits %d/%d)", s_wc.packets, m[0] >> 24, m[1], is_marker(m) ? ", a marker" : "",
          s_why, *fill - s_fill0, ref_fill - s_fill0, psx_gpu_split_count() - s_split0,
          walk_ref_split_count() - s_split0);
}

static const PsxGpuCheck k_check = {wc_pre, wc_post, wc_vram};

/* ---- --walk-check-synth ---- */

static u32 s_seed;
static u32 rnd(void) { /* xorshift32 */
  s_seed ^= s_seed << 13;
  s_seed ^= s_seed >> 17;
  s_seed ^= s_seed << 5;
  return s_seed;
}
/* x, y: mostly on screen, sometimes far off it, but never where the draw
 * offset would take a vertex past 16 bits: there PsyCross's float-to-short
 * conversion differs by host (the x87's 0x8000 on linux32, a wrap through
 * 32 bits on ARM, which ours does everywhere; D117) */
static u32 rxy(void) {
  if (rnd() % 8 == 0)
    return (rnd() % 60000 - 30000) & 0xFFFF | ((rnd() % 60000 - 30000) & 0xFFFF) << 16;
  return (rnd() % 700 - 100) & 0xFFFF | ((rnd() % 600 - 100) & 0xFFFF) << 16;
}

/* one well-formed GP0 command at c (room for 64 words): its words */
static int synth_command(u32 *c) {
  static const char k_label[] = "walk check";
  const u32 col = rnd() & 0xFFFFFF;
  u32 code, k, n;
  switch (rnd() % 16) {
  case 0: case 1: case 2: case 3: /* polygon */
    code = 0x20 | (rnd() & 0x1F);
    n = 1;
    c[0] = code << 24 | col;
    for (k = 0; k < (code & 8 ? 4u : 3u); k++) {
      if ((code & 0x10) && k)
        c[n++] = rnd() & 0xFFFFFF;
      c[n++] = rxy();
      if (code & 4)
        c[n++] = rnd(); /* u, v and the CLUT or the tpage */
    }
    return (int)n;
  case 4: case 5: { /* line or polyline: F3/F4-sized ones read three or four points */
    code = 0x40 | (rnd() & 0x1F);
    n = 1;
    c[0] = code << 24 | col;
    if (!(code & 8)) { /* a line */
      c[n++] = rxy();
      if (code & 0x10)
        c[n++] = rnd() & 0xFFFFFF;
      c[n++] = rxy();
      return (int)n;
    }
    for (k = 0; k < (code & 4 ? 4u : 3u) + rnd() % 3; k++) {
      if ((code & 0x10) && k)
        c[n++] = rnd() & 0xFFFFFF;
      do
        c[n] = rxy();
      while ((c[n] & 0xF000F000u) == 0x50005000u);
      n++;
    }
    c[n++] = 0x55555555u;
    return (int)n;
  }
  case 6: case 7: { /* rectangle */
    code = 0x60 | (rnd() & 0x1F);
    if (rnd() % 4 && (code & 1)) /* mostly the handled ones */
      code &= ~1u;
    n = 1;
    c[0] = code << 24 | col;
    c[n++] = rxy();
    if (code & 4)
      c[n++] = rnd();
    if (((code >> 3) & 3) == 0)
      c[n++] = (rnd() % 300) | (rnd() % 300) << 16;
    return (int)n;
  }
  case 8: case 9: case 10: /* draw environment */
    code = rnd() % 32 ? 0xE1 + rnd() % 5 : 0xE0 + rnd() % 16;
    c[0] = code << 24 | (rnd() & 0xFFFFFF);
    return 1;
  case 11: /* fill, inside VRAM (the host's VRAM calls are real) */
    c[0] = 0x02000000u | col;
    c[1] = (rnd() % 48) * 16 | (rnd() % 256) << 16;
    c[2] = (rnd() % 256) | (rnd() % 256) << 16;
    return 3;
  case 12: { /* CPU to VRAM (DR_LOAD), and PsyCross's 0xB1, 0xB2 */
    const u32 w = 1 + rnd() % 12, h = 1 + rnd() % 10;
    switch (rnd() % 4) {
    case 0:
      c[0] = 0xB1000000u | (rnd() % 4 ? 0 : 1 + rnd() % 8);
      c[1] = rnd() & 0x0FFF0FFF;
      c[2] = 0;
      return 3;
    case 1:
      c[0] = 0xB2000000u;
      c[1] = rnd() % 2 ? (u32)(uintptr_t)k_label : 0;
      c[2] = 0;
      return 3;
    default:
      c[0] = 0xA0000000u;
      c[1] = (rnd() % 1000) | (rnd() % 500) << 16;
      c[2] = w | h << 16;
      for (k = 0; k < (w * h + 1) / 2; k++)
        c[3 + k] = rnd();
      return 3 + (int)((w * h + 1) / 2);
    }
  }
  case 13: /* NOPs, the cache clear, the IRQ (not the fill) */
    k = rnd() % 32;
    c[0] = (k == 2 ? 0 : k) << 24 | (rnd() & 0xFFFFFF);
    return 1;
  case 14: /* unhandled by both: VRAM to VRAM, VRAM to CPU */
    if (rnd() % 8)
      return synth_command(c);
    c[0] = (rnd() % 2 ? 0x80u : 0xC0u) << 24;
    c[1] = rnd();
    c[2] = rnd();
    c[3] = rnd();
    return c[0] >> 24 == 0x80 ? 4 : 3;
  default: /* a textured quad, the commonest primitive */
    c[0] = (0x2C | (rnd() & 0x13)) << 24 | col;
    n = 1;
    for (k = 0; k < 4; k++) {
      if ((c[0] & 0x10000000u) && k)
        c[n++] = rnd() & 0xFFFFFF;
      c[n++] = rxy();
      c[n++] = rnd();
    }
    return (int)n;
  }
}

static u32 *s_sp[8];
static int s_si;
static uintptr_t synth_next(uintptr_t packet) {
  (void)packet;
  return (uintptr_t)s_sp[++s_si];
}

static void synth_state(void) {
  activeDrawEnv.tpage = (u_short)(rnd() & 0x1FF);
  activeDrawEnv.dtd = (u_char)(rnd() & 1);
  activeDrawEnv.dfe = (u_char)(rnd() % 4 != 0);
  activeDrawEnv.ofs[0] = (short)(rnd() % 1024);
  activeDrawEnv.ofs[1] = (short)(rnd() % 512);
  activeDrawEnv.clip.x = (short)(rnd() % 1024);
  activeDrawEnv.clip.y = (short)(rnd() % 512);
  activeDrawEnv.clip.w = (short)(rnd() % 1024);
  activeDrawEnv.clip.h = (short)(rnd() % 512);
  activeDrawEnv.tw.w = (short)(rnd() % 32);
  activeDispEnv.disp.x = (short)(rnd() % 1024);
  activeDispEnv.disp.y = (short)(rnd() % 512);
}

unsigned long long walk_check_synth(unsigned n, unsigned seed) {
  static u32 buf[8][1 + 255];
  unsigned i;
  const unsigned long long bad0 = s_wc.bad;
  s_seed = seed ? seed : 1;
  psx_gpu_clear_splits();
  for (i = 0; i < n; i++) {
    int np = 1 + (int)(rnd() % 3), p;
    if (i % 64 == 0)
      synth_state();
    if (i % 512 == 0)
      psx_gpu_clear_splits(); /* the vertex buffer and the splits stay in room */
    for (p = 0; p < np; p++) {
      u32 *t = buf[p], len = 0;
      const int cmds = 1 + (int)(rnd() % 4);
      int k;
      for (k = 0; k < cmds; k++) {
        u32 one[64];
        const int w = synth_command(one);
        if (len + (u32)w > 250)
          break;
        memcpy(t + 1 + len, one, (size_t)w * 4);
        len += (u32)w;
      }
      t[0] = len << 24 | (p == np - 1 ? 0xFFFFFFu : 0);
      s_sp[p] = t;
    }
    s_sp[np] = NULL;
    s_si = 0;
    if (rnd() % 8 == 0) {
      /* DrawPrim: one primitive as a whole (DR_MOVE's layout too; a
       * random 01h there would be a DR_MOVE of random words, which the
       * host's software VRAM can't take) */
      u32 *t = buf[0];
      if (t[1] >> 24 == 0x01)
        t[1] &= 0x00FFFFFFu;
      if (rnd() % 4 == 0) {
        t[0] = 5u << 24 | 0xFFFFFFu;
        t[1] = 0x01000000u;
        t[2] = 0x80000000u;
        t[3] = (rnd() % 960) | (rnd() % 448) << 16;
        t[4] = (rnd() % 960) | (rnd() % 448) << 16;
        t[5] = (rnd() % 64) | (rnd() % 64) << 16;
      }
      psx_gpu_walk((u_long *)t, 1, NULL);
    } else {
      psx_gpu_walk((u_long *)s_sp[0], 0, synth_next);
    }
  }
  psx_gpu_clear_splits();
  return s_wc.bad - bad0;
}

int walk_check_init(void (*log)(const char *fmt, ...)) {
  const char *mut = getenv("WALK_MUTATE");
  s_log = log;
  if (psyx_PsyX_SplitStateSize() != (int)sizeof(PsxGpuSplitState)) {
    if (log)
      log("walk check: PsyCross's split state is %d bytes, ours %d: off",
          psyx_PsyX_SplitStateSize(), (int)sizeof(PsxGpuSplitState));
    return -1;
  }
  s_rbuf = malloc(RBUF_WORDS * sizeof(u32));
  if (!s_rbuf)
    return -1;
  if (mut)
    g_psx_gpu_mutant = atoi(mut);
  g_psx_gpu_check = &k_check;
  if (log)
    log("walk check: every packet through PsyCross's walk too (D117)%s",
        g_psx_gpu_mutant ? ", with a mutant" : "");
  return 0;
}

void walk_check_report(void) {
  if (!g_psx_gpu_check || !s_log)
    return;
  s_log("walk check: %llu packets through both walks (%llu markers, %llu single primitives), "
        "%llu differ; %llu vertices (%llu in direct triangles), %llu splits, %llu VRAM calls "
        "(D117, mutant %d)",
        s_wc.packets, s_wc.markers, s_wc.single, s_wc.bad, s_wc.verts, s_wc.direct, s_wc.splits,
        s_wc.vram, g_psx_gpu_mutant);
}

#else /* no reference walk in this build */

const int g_walk_ref_built = 0;

int walk_check_init(void (*log)(const char *fmt, ...)) {
  if (log)
    log("walk check: needs the reference build (SPYRO_PSYCROSS_REF, the arena build)");
  return -1;
}
void walk_check_report(void) {}
unsigned long long walk_check_synth(unsigned n, unsigned seed) {
  (void)n, (void)seed;
  return 0;
}
void walk_ref_sync(void) {}
void walk_ref_parse(u_long *p, int single, PsxGpuNext next) {
  (void)p, (void)single, (void)next;
}
GrVertex *walk_ref_vertices(int *fill) {
  *fill = 0;
  return NULL;
}
int walk_ref_split_count(void) { return 0; }
const PsxGpuSplit *walk_ref_split(int i) {
  (void)i;
  return NULL;
}
void walk_ref_split_get(int i, int out[8]) {
  (void)i;
  memset(out, 0, 8 * sizeof(int));
}
void walk_ref_clear(void) {}
DRAWENV *walk_ref_env(void) { return NULL; }

#endif

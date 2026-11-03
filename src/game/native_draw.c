/* Native draw lists (decisions D82): the record pools, the list turn-over and
 * the OT walk's side, which emits a marker's records as PsyCross's parse
 * would have emitted the primitives they came from. native_draw.h has the
 * format and the renderers' side. */
#include "native_draw.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdlib.h>
#include <string.h>

#include "PsyX/PsyX_render.h"
#include "psx_walk.h"

extern DRAWENV activeDrawEnv; /* PsyX_GPU.cpp */
extern DISPENV activeDispEnv;

int g_native_draw;
int g_nd_check;
int g_nd_on;
int g_nd_var;
int g_nd_ab;
NdSide g_nd_side[ND_SIDE_N];
void *(*g_nd_nc_alloc)(unsigned bytes);
/* each pool's memory: cached, and uncached (ND_ON_NC; NULL without) */
static u32 *s_mem[2][ND_POOLS];
/* the walk's cached copy of an uncached pool, and the list it holds
 * (NdPool.gen); the game thread's count of lists */
static u32 *s_walk_buf;
static u32 s_walk_gen = ~0u, s_gen;
NdPool g_nd_pool[ND_POOLS];
u32 g_nd_cur;
u32 *g_nd_base, *g_nd_wp, *g_nd_lim;
u32 g_nd_id = ND_MAGIC;
NdStats g_nd_stats;

static void (*s_log)(const char *fmt, ...);
static u32 s_peak;     /* the most words one list used */
static unsigned long long s_words;
static unsigned s_lists, s_lists_native, s_emitted;
/* packets the walk parsed itself, by GP0 class: polygons, lines, rectangles, other */
static unsigned s_other[4];

/* 1 MB a pool: a frame of the heaviest levels writes ~300 KB (D82) */
#define POOL_WORDS (256u * 1024u)

void nd_init(void (*log)(const char *fmt, ...)) {
  int i;
  s_log = log;
  nd_check_set_log(log);
  g_nd_var &= ND_ON_SIDE | ND_ON_PF | ND_ON_PROBE | ND_ON_NC | ND_ON_NCW;
  if (g_nd_var & (ND_ON_NC | ND_ON_NCW))
    g_nd_var |= ND_ON_SIDE;
  if (!g_native_draw && !g_nd_check) {
    g_nd_ab = 0;
    return;
  }
  for (i = 0; i < ND_POOLS; i++) {
    g_nd_pool[i].w = s_mem[0][i] = malloc(POOL_WORDS * sizeof(u32));
    g_nd_pool[i].cap = g_nd_pool[i].w ? POOL_WORDS : 0;
    g_nd_pool[i].n = 0;
    g_nd_pool[i].nc = 0;
  }
  if (g_nd_nc_alloc && (g_nd_ab || (g_nd_var & (ND_ON_NC | ND_ON_NCW)))) {
    s_walk_buf = malloc(POOL_WORDS * sizeof(u32));
    for (i = 0; i < ND_POOLS && s_walk_buf; i++)
      s_mem[1][i] = g_nd_nc_alloc(POOL_WORDS * sizeof(u32));
    if (!s_walk_buf || !s_mem[1][0] || !s_mem[1][1]) {
      s_mem[1][0] = s_mem[1][1] = NULL; /* the cached pools for every variant */
      if (s_log)
        s_log("native draw: no uncached memory for the pools; ND_ON_NC uses the cached ones");
    }
  }
  if (!g_nd_pool[0].cap || !g_nd_pool[1].cap) {
    g_native_draw = g_nd_check = g_nd_ab = 0;
    if (s_log)
      s_log("native draw: no memory for the pools, off");
    return;
  }
  g_nd_base = g_nd_wp = g_nd_pool[0].w;
  g_nd_lim = g_nd_base + g_nd_pool[0].cap;
  g_psyxPacketHook = nd_packet;
}

void nd_begin(void) {
  if (g_nd_check)
    nd_check_began();
  g_nd_on = g_native_draw && g_fast_render_ro == 2 ? ND_ON | g_nd_var : 0;
}

void nd_end(void) { g_nd_on = 0; }

/* nd_ab=1 (D87): the variants in turn, a block of lists each. A block's
 * first lists aren't counted: the pools take turns per list, so they only
 * bring this variant's pool into the caches. */
enum { AB_BLOCK = 8, AB_WARM = 2, AB_OFF = -1 };
/* run S1 (D87) took native_draw 0, D86's path, ND_ON_SIDE, ND_ON_PF, both,
 * ND_ON_PROBE and the probe with ND_ON_PF; this is run S2's set */
static const int k_ab_var[ND_AB_VARIANTS] = {
    AB_OFF,
    ND_ON_SIDE | ND_ON_PF,
    ND_ON_SIDE | ND_ON_NC,
    ND_ON_SIDE | ND_ON_NCW,
    ND_ON_PROBE,
    ND_ON_PROBE | ND_ON_NC,
    0,
};
static unsigned s_ab_list;
static int s_ab_slot = -1;
static unsigned s_ab_lists[ND_AB_VARIANTS];

int nd_ab_slot(void) { return s_ab_slot; }

void nd_ab_take(unsigned lists[ND_AB_VARIANTS]) {
  memcpy(lists, s_ab_lists, sizeof s_ab_lists);
  memset(s_ab_lists, 0, sizeof s_ab_lists);
}

/* nd_ab=1 */
static void ab_next(void) {
  unsigned v;
  g_psx_walk_tag = s_ab_slot; /* the list just linked, which DrawOTag gets now */
  s_ab_list++;
  v = s_ab_list / AB_BLOCK % ND_AB_VARIANTS;
  g_native_draw = k_ab_var[v] != AB_OFF;
  g_nd_var = g_native_draw ? k_ab_var[v] : 0;
  s_ab_slot = s_ab_list % AB_BLOCK >= AB_WARM ? (int)v : -1;
  if (s_ab_slot >= 0)
    s_ab_lists[v]++;
}

void nd_list_linked(void) {
  NdPool *p = &g_nd_pool[g_nd_cur];
  p->n = g_nd_base ? (u32)(g_nd_wp - g_nd_base) : 0;
  s_words += p->n;
  s_lists++;
  if (p->n) {
    s_lists_native++;
    if (p->n > s_peak)
      s_peak = p->n;
  }
  if (g_nd_ab)
    ab_next(); /* the next list's variant, which picks its pool's memory */
  g_nd_cur ^= 1;
  {
    NdPool *const q = &g_nd_pool[g_nd_cur];
    const int nc = (g_nd_var & (ND_ON_NC | ND_ON_NCW)) != 0 && s_mem[1][g_nd_cur];
    q->n = 0;
    q->gen = ++s_gen;
    if (q->w)
      q->w = s_mem[nc][g_nd_cur];
    q->nc = !nc ? 0 : (g_nd_var & ND_ON_NCW) ? 2 : 1;
  }
  g_nd_base = g_nd_wp = g_nd_pool[g_nd_cur].w;
  g_nd_lim = g_nd_base ? g_nd_base + g_nd_pool[g_nd_cur].cap : NULL;
  /* a marker of an earlier list can't be taken for this one's */
  g_nd_id = ND_MAGIC | g_nd_cur << 11 | (((g_nd_id & ND_SERIAL) + 1) & ND_SERIAL);
  /* the side table's keys carry the serial: when it comes round, its
   * entries from 2048 lists ago go */
  if (!(g_nd_id & ND_SERIAL))
    memset(g_nd_side, 0, sizeof g_nd_side);
}

/* PsyCross's DrawEnvOffset, as integers (its float sums are exact here) */
static void draw_offset(int *ox, int *oy) {
  if (activeDrawEnv.dfe) {
    *ox = activeDrawEnv.ofs[0] - activeDispEnv.disp.x;
    *oy = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
  } else {
    *ox = *oy = 0;
  }
}

/* What AddSplit compares that a record can change: the blend (from the
 * tpage, semi-transparent only), the texture format (from the tpage, for
 * every polygon), textured or not, and drawing to the display area. */
static inline u32 split_key(u32 h) {
  const u32 tp = activeDrawEnv.tpage;
  const u32 blend = (h & ND_SEMI) ? ((tp >> 5) & 3) + 1 : 0;
  return blend | ((tp >> 7) & 3) << 3 | (h & ND_TEX) << 5 | (u32)(activeDrawEnv.dfe != 0) << 6;
}

/* The records of one marker, from the one at word `at` of pool w (cap words) */
static void emit(const u32 *w, u32 at, u32 cap) {
  int *fill;
  GrVertex *const vb = PsyX_VertexBuffer(&fill);
  u32 prev = ~0u, dmask;
  int ox, oy;
  draw_offset(&ox, &oy);
  dmask = activeDrawEnv.dtd ? 1u << 24 : 0;
  for (; at < cap; at = w[at + 1]) {
    const u32 *r = w + at;
    const u32 h = r[0];
    u32 n, k, key, vs;
    u32 *o;
    if (h & ND_E1) {
      /* ProcessDrawEnv's DR_TPAGE */
      activeDrawEnv.tpage = (u_short)(h >> 16 & 0x1FF);
      activeDrawEnv.dtd = (u_char)(h >> 25 & 1);
      activeDrawEnv.dfe = (u_char)(h >> 26 & 1);
      draw_offset(&ox, &oy);
      dmask = activeDrawEnv.dtd ? 1u << 24 : 0;
      continue;
    }
    if (h & ND_TEX)
      activeDrawEnv.tpage = (u_short)(h >> 16);
    if (h & ND_NULL)
      continue;
    key = split_key(h);
    if (key != prev) {
      PsyX_AddSplit((h & ND_SEMI) != 0, (h & ND_TEX) != 0);
      prev = key;
    }
    n = (h & ND_QUAD) ? 4 : 3;
    if (*fill + 6 > MAX_VERTEX_BUFFER_SIZE)
      continue;
    o = (u32 *)(vb + *fill);
    if (h & ND_TEX) {
      const u32 pc = r[3];
      vs = ND_VW;
      r += ND_HEAD;
      for (k = 0; k < n; k++, o += 5) {
        const u32 *s = r + k * ND_VW;
        o[0] = s[0];
        o[1] = pc;
        o[2] = s[1] | dmask;
        o[3] = s[2] | 0xFF000000u;
        o[4] = 0;
      }
    } else {
      const u32 bd = (h & ND_GOUR) ? 0x1010000u : 0x10000u;
      vs = 2;
      r += ND_HEAD;
      for (k = 0; k < n; k++, o += 5) {
        const u32 *s = r + k * 2;
        o[0] = s[0];
        o[1] = 0;
        o[2] = bd;
        o[3] = s[1] | 0xFF000000u;
        o[4] = 0;
      }
    }
    if (ox | oy) {
      /* the draw offset (PsyCross's DrawEnvOffset) */
      u32 *x = (u32 *)(vb + *fill);
      for (k = 0; k < n; k++, x += 5) {
        const u32 xy = x[0];
        const u32 ax = (u32)((s32)(s16)xy + ox), ay = (u32)(((s32)xy >> 16) + oy);
        x[0] = (ax & 0xFFFFu) | ay << 16;
      }
    }
    {
      GrVertex *v = vb + *fill;
      if (g_psyxPolyHook) {
        /* the primitive's own x/y, as MakeVertex* passes them */
        short *xy[4];
        for (k = 0; k < n; k++)
          xy[k] = (short *)(uintptr_t)(r + k * vs);
        g_psyxPolyHook(v, xy, (int)n);
      }
      if (n == 4) {
        /* TriangulateQuad */
        v[4] = v[3];
        v[5] = v[2];
        v[2] = v[3];
        v[3] = v[1];
        *fill += 6;
      } else {
        *fill += 3;
      }
    }
    s_emitted++;
  }
}

int nd_packet(uintptr_t packet) {
  const u32 *m = (const u32 *)packet;
  u32 w1, pool;
  w1 = m[1];
  if ((m[0] >> 24) != 2 || (w1 & 0xFFF000u) != ND_MAGIC || (w1 >> 24)) {
    const u32 cls = w1 >> 29;
    s_other[cls == 1 ? 0 : cls == 2 ? 1 : cls == 3 ? 2 : 3]++;
    return 0;
  }
  pool = w1 >> 11 & 1;
  {
    const NdPool *const p = &g_nd_pool[pool];
    const u32 *w = p->w;
    if (p->nc == 1) {
      /* an uncached pool (D87): copied once a list, then read cached */
      if (p->gen != s_walk_gen) {
        memcpy(s_walk_buf, w, p->n * sizeof(u32));
        s_walk_gen = p->gen;
      }
      w = s_walk_buf;
    }
    emit(w, m[2], p->cap);
  }
  return 1;
}

void nd_report(int vblank) {
  NdStats st = g_nd_stats;
  const unsigned prims = st.markers + st.joined;
  if (!s_log || !g_nd_base)
    return; /* no pools (nd_ab turns g_native_draw on and off) */
  s_log("native draw %d: %u lists (%u with records), %u primitives as %u markers (%u joined), "
        "%u draw modes, %u left as primitives (pool full); %u polygons walked; "
        "%u KB of records, the most in a list %u KB; the walk parsed %u polygons, %u lines, "
        "%u rectangles and %u other packets itself",
        vblank, s_lists, s_lists_native, prims, st.markers, st.joined, st.e1, st.fallback,
        s_emitted, (unsigned)(s_words * 4 / 1024), (unsigned)(s_peak * 4 / 1024), s_other[0],
        s_other[1], s_other[2], s_other[3]);
  memset(&g_nd_stats, 0, sizeof g_nd_stats);
  s_lists = s_lists_native = s_emitted = 0;
  memset(s_other, 0, sizeof s_other);
  s_peak = 0;
  s_words = 0;
}

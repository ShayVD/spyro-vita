/* Native draw lists (decisions D82): the record pools, the list turn-over and
 * the OT walk's side, which emits a marker's records as PsyCross's parse
 * would have emitted the primitives they came from. native_draw.h has the
 * format and the renderers' side. */
#include "native_draw.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdlib.h>
#include <string.h>

#include "PsyX/PsyX_render.h"

extern DRAWENV activeDrawEnv; /* PsyX_GPU.cpp */
extern DISPENV activeDispEnv;

int g_native_draw;
int g_nd_check;
int g_nd_on;
NdPool g_nd_pool[ND_POOLS];
u32 g_nd_cur;
u32 g_nd_id = ND_MAGIC;
NdStats g_nd_stats;

static void (*s_log)(const char *fmt, ...);
static u32 s_peak;     /* the most words one list used */
static unsigned s_lists, s_lists_native, s_emitted;

/* 1 MB a pool: a frame of the heaviest levels writes ~300 KB (D82) */
#define POOL_WORDS (256u * 1024u)

void nd_init(void (*log)(const char *fmt, ...)) {
  int i;
  s_log = log;
  nd_check_set_log(log);
  if (!g_native_draw && !g_nd_check)
    return;
  for (i = 0; i < ND_POOLS; i++) {
    g_nd_pool[i].w = malloc(POOL_WORDS * sizeof(u32));
    g_nd_pool[i].cap = g_nd_pool[i].w ? POOL_WORDS : 0;
    g_nd_pool[i].n = 0;
  }
  if (!g_nd_pool[0].cap || !g_nd_pool[1].cap) {
    g_native_draw = g_nd_check = 0;
    if (s_log)
      s_log("native draw: no memory for the pools, off");
    return;
  }
  g_psyxPacketHook = nd_packet;
}

void nd_begin(void) {
  if (g_nd_check)
    nd_check_began();
  g_nd_on = g_native_draw && g_fast_render_ro == 2;
}

void nd_end(void) { g_nd_on = 0; }

void nd_list_linked(void) {
  NdPool *p = &g_nd_pool[g_nd_cur];
  s_lists++;
  if (p->n) {
    s_lists_native++;
    if (p->n > s_peak)
      s_peak = p->n;
  }
  g_nd_cur ^= 1;
  g_nd_pool[g_nd_cur].n = 0;
  /* a marker of an earlier list can't be taken for this one's */
  g_nd_id = ND_MAGIC | g_nd_cur << 11 | (((g_nd_id & ND_SERIAL) + 1) & ND_SERIAL);
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
    u32 n, k, key, dm;
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
    r += ND_HEAD;
    o = (u32 *)(vb + *fill);
    dm = (h & ND_TEX) ? dmask : 0;
    if (!(ox | oy)) {
      for (k = 0; k < n * 5; k += 5) {
        o[k] = r[k];
        o[k + 1] = r[k + 1];
        o[k + 2] = r[k + 2] | dm;
        o[k + 3] = r[k + 3];
        o[k + 4] = r[k + 4];
      }
    } else {
      for (k = 0; k < n * 5; k += 5) {
        const u32 xy = r[k];
        const u32 x = (u32)((s32)(s16)xy + ox), y = (u32)(((s32)xy >> 16) + oy);
        o[k] = (x & 0xFFFFu) | y << 16;
        o[k + 1] = r[k + 1];
        o[k + 2] = r[k + 2] | dm;
        o[k + 3] = r[k + 3];
        o[k + 4] = r[k + 4];
      }
    }
    {
      GrVertex *v = vb + *fill;
      if (g_psyxPolyHook) {
        /* the primitive's own x/y, as MakeVertex* passes them */
        short *xy[4];
        for (k = 0; k < n; k++)
          xy[k] = (short *)(uintptr_t)(r + k * 5);
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
  if ((m[0] >> 24) != 2)
    return 0;
  w1 = m[1];
  if ((w1 & 0xFFF000u) != ND_MAGIC || (w1 >> 24))
    return 0;
  pool = w1 >> 11 & 1;
  emit(g_nd_pool[pool].w, m[2], g_nd_pool[pool].cap);
  return 1;
}

void nd_report(int vblank) {
  NdStats st = g_nd_stats;
  if (!s_log || (!g_native_draw && !g_nd_check))
    return;
  s_log("native draw %d: %u lists (%u with records), %u primitives as %u markers (%u joined), "
        "%u draw modes, %u left as primitives (pool full); %u polygons walked; "
        "%u KB of records, the most in a list %u KB",
        vblank, s_lists, s_lists_native, st.prims, st.markers, st.joined, st.e1, st.fallback,
        s_emitted, (unsigned)(st.words * 4 / 1024), (unsigned)(s_peak * 4 / 1024));
  memset(&g_nd_stats, 0, sizeof g_nd_stats);
  s_lists = s_lists_native = s_emitted = 0;
  s_peak = 0;
}

/* Native draw lists (decisions D82, D86, D91): the record pools, the list
 * turn-over and the OT walk's side, which emits a marker's records as
 * PsyCross's parse would have emitted the primitives they came from.
 * native_draw.h has the format and the renderers' side. */
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
int g_nd_ab;
NdSide g_nd_side[ND_SIDE_N];
NdPool g_nd_pool[ND_POOLS];
u32 g_nd_cur;
NdCursor g_nd = {.id = ND_MAGIC};
unsigned g_nd_fallback;

static void (*s_log)(const char *fmt, ...);
static u32 s_peak; /* the most words one list used */
static unsigned long long s_words;
static unsigned s_lists, s_lists_native;
/* the walk's: markers, records (their primitives), draw modes, polygons */
static unsigned s_markers, s_records, s_e1, s_emitted;
/* packets the walk parsed itself, by GP0 class: polygons, lines, rectangles, other */
static unsigned s_other[4];

/* 1 MB a pool: a frame of the heaviest levels writes ~300 KB (D82) */
#define POOL_WORDS (256u * 1024u)

/* the pool being written from its start: g_nd's cursor and limit */
static void cursor_at(const NdPool *p) {
  g_nd.base = g_nd.wp = p->w;
  g_nd.lim = p->w ? p->w + p->cap - ND_MAXW : NULL;
}

void nd_init(void (*log)(const char *fmt, ...)) {
  int i;
  s_log = log;
  nd_check_set_log(log);
  if (!g_native_draw && !g_nd_check) {
    g_nd_ab = 0;
    return;
  }
  for (i = 0; i < ND_POOLS; i++) {
    g_nd_pool[i].w = malloc(POOL_WORDS * sizeof(u32));
    g_nd_pool[i].cap = g_nd_pool[i].w ? POOL_WORDS : 0;
    g_nd_pool[i].n = 0;
  }
  if (!g_nd_pool[0].cap || !g_nd_pool[1].cap) {
    g_native_draw = g_nd_check = g_nd_ab = 0;
    if (s_log)
      s_log("native draw: no memory for the pools, off");
    return;
  }
  cursor_at(&g_nd_pool[0]);
  g_nd.key = g_nd.id << 21;
  g_psyxPacketHook = nd_packet;
}

void nd_begin(void) {
  if (g_nd_check)
    nd_check_began();
  g_nd.on = g_native_draw == 1 && g_fast_render_ro == 2;
}

void nd_end(void) { g_nd.on = 0; }

/* nd_ab=1 (D91, D92): native_draw 0, 1 and 2 in turn, a block of lists each. A
 * block's first lists aren't counted: the pools take turns per list, so
 * they only bring this way's pool into the caches. */
enum { AB_BLOCK = 8, AB_WARM = 2 };
static unsigned s_ab_list;
static int s_ab_slot = -1;
static unsigned s_ab_lists[ND_AB_VARIANTS];

int nd_ab_slot(void) { return s_ab_slot; }

void nd_ab_take(unsigned lists[ND_AB_VARIANTS]) {
  memcpy(lists, s_ab_lists, sizeof s_ab_lists);
  memset(s_ab_lists, 0, sizeof s_ab_lists);
}

static void ab_next(void) {
  unsigned v;
  g_psx_walk_tag = s_ab_slot; /* the list just linked, which DrawOTag gets now */
  s_ab_list++;
  v = s_ab_list / AB_BLOCK % ND_AB_VARIANTS;
  g_native_draw = (int)v;
  s_ab_slot = s_ab_list % AB_BLOCK >= AB_WARM ? (int)v : -1;
  if (s_ab_slot >= 0)
    s_ab_lists[v]++;
}

void nd_list_linked(void) {
  NdPool *p = &g_nd_pool[g_nd_cur];
  p->n = g_nd.base ? (u32)(g_nd.wp - g_nd.base) : 0;
  s_words += p->n;
  s_lists++;
  if (p->n) {
    s_lists_native++;
    if (p->n > s_peak)
      s_peak = p->n;
  }
  g_psx_walk_nd = g_native_draw; /* the walk's mode for the list just linked */
  if (g_nd_ab)
    ab_next();
  g_nd_cur ^= 1;
  g_nd_pool[g_nd_cur].n = 0;
  cursor_at(&g_nd_pool[g_nd_cur]);
  /* a marker of an earlier list can't be taken for this one's */
  g_nd.id = ND_MAGIC | g_nd_cur << 11 | (((g_nd.id & ND_SERIAL) + 1) & ND_SERIAL);
  g_nd.key = g_nd.id << 21;
  /* the side table's keys carry the serial: when it comes round, its
   * entries from 2048 lists ago go */
  if (!(g_nd.id & ND_SERIAL))
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

/* a record's polygon, as the walk sees it */
enum { F_TEX = 1, F_SEMI = 2 };

/* What AddSplit compares that a record can change: the blend (from the
 * tpage, semi-transparent only), the texture format (from the tpage, for
 * every polygon), textured or not, and drawing to the display area. */
static inline u32 split_key(u32 f) {
  const u32 tp = activeDrawEnv.tpage;
  const u32 blend = (f & F_SEMI) ? ((tp >> 5) & 3) + 1 : 0;
  return blend | ((tp >> 7) & 3) << 3 | (f & F_TEX) << 5 | (u32)(activeDrawEnv.dfe != 0) << 6;
}

#define ND_RAW 0x808080u /* an unshaded texture's colour (MakeColourNoShade) */

/* The walk's state while it emits: the vertex buffer, the last split key
 * (AddSplit is only called when it changes), the dither mask and the draw
 * offset */
typedef struct {
  GrVertex *vb;
  int *fill;
  u32 prev, dmask;
  int ox, oy;
} Em;

static void em_begin(Em *e) {
  e->vb = PsyX_VertexBuffer(&e->fill);
  e->prev = ~0u;
  draw_offset(&e->ox, &e->oy);
  e->dmask = activeDrawEnv.dtd ? 1u << 24 : 0;
}

/* One primitive (kind; b: its words after the tag, as the PS1 primitive
 * holds them) turned into the vertices PsyCross's Process*Poly makes of it:
 * MakeVertex*'s order (a quad's 0, 1, 3, 2); textured: page/CLUT from
 * uv1/uv0's high halves, u/v, brightness 2, the tpage's dither and the draw
 * environment's, the colour (unshaded flat: raw); untextured: brightness 1,
 * dither 1 Gouraud and 0 flat. A draw mode (E1) is ProcessDrawEnv's. */
static void em_prim(Em *e, u32 kind, const u32 *b) {
  static const u8 k_order[4] = {0, 1, 3, 2};
  GrVertex *const vb = e->vb;
  int *const fill = e->fill;
  const u32 c0 = b[0];
  const u32 *xy[4], *uv[4], *col[4];
  u32 n, k, key, f, tp = 0, pc = 0, cstep = 0;
  u32 *o;
  switch (kind) {
  case ND_K_E1:
    /* ProcessDrawEnv's DR_TPAGE */
    activeDrawEnv.tpage = (u_short)(c0 & 0x1FF);
    activeDrawEnv.dtd = (u_char)(c0 >> 9 & 1);
    activeDrawEnv.dfe = (u_char)(c0 >> 10 & 1);
    draw_offset(&e->ox, &e->oy);
    e->dmask = activeDrawEnv.dtd ? 1u << 24 : 0;
    s_e1++;
    return;
  case ND_K_F3:
  case ND_K_F4:
    n = kind == ND_K_F4 ? 4 : 3;
    f = 0;
    for (k = 0; k < n; k++)
      xy[k] = b + 1 + k;
    break;
  case ND_K_G3:
  case ND_K_G4:
    n = kind == ND_K_G4 ? 4 : 3;
    f = 0;
    cstep = 1;
    for (k = 0; k < n; k++) {
      col[k] = b + 2 * k;
      xy[k] = b + 2 * k + 1;
    }
    break;
  case ND_K_FT3:
  case ND_K_FT4:
    n = kind == ND_K_FT4 ? 4 : 3;
    f = F_TEX;
    for (k = 0; k < n; k++) {
      xy[k] = b + 1 + 2 * k;
      uv[k] = b + 2 + 2 * k;
    }
    break;
  default: /* GT3, GT4 */
    n = kind == ND_K_GT4 ? 4 : 3;
    f = F_TEX;
    cstep = 1;
    for (k = 0; k < n; k++) {
      col[k] = b + 3 * k;
      xy[k] = b + 3 * k + 1;
      uv[k] = b + 3 * k + 2;
    }
    break;
  }
  if (c0 >> 25 & 1)
    f |= F_SEMI;
  if (f & F_TEX) {
    tp = *uv[1] >> 16;
    pc = tp | (*uv[0] & 0xFFFF0000u);
    activeDrawEnv.tpage = (u_short)tp;
    if (kind == ND_K_FT3 && (*xy[0] & *xy[1] & *xy[2]) == 0xFFFFFFFFu)
      return; /* IsNull: SCE's tpage-only hack */
  }
  key = split_key(f);
  if (key != e->prev) {
    PsyX_AddSplit((f & F_SEMI) != 0, (f & F_TEX) != 0);
    e->prev = key;
  }
  if (*fill + 6 > MAX_VERTEX_BUFFER_SIZE)
    return;
  o = (u32 *)(vb + *fill);
  {
    /* flat: one colour for all (an unshaded one raw); Gouraud: each its own */
    const u32 flat = !cstep && (c0 >> 24 & 1) ? ND_RAW : c0;
    if (f & F_TEX) {
      const u32 bd = 0x20000u | ((tp >> 9) & 1) << 24 | e->dmask;
      for (k = 0; k < n; k++, o += 5) {
        const u32 v = n == 4 ? k_order[k] : k;
        o[0] = *xy[v];
        o[1] = pc;
        o[2] = (*uv[v] & 0xFFFFu) | bd;
        o[3] = (cstep ? *col[v] : flat) | 0xFF000000u;
        o[4] = 0;
      }
    } else {
      const u32 bd = cstep ? 0x1010000u : 0x10000u;
      for (k = 0; k < n; k++, o += 5) {
        const u32 v = n == 4 ? k_order[k] : k;
        o[0] = *xy[v];
        o[1] = 0;
        o[2] = bd;
        o[3] = (cstep ? *col[v] : flat) | 0xFF000000u;
        o[4] = 0;
      }
    }
  }
  if (e->ox | e->oy) {
    /* the draw offset (PsyCross's DrawEnvOffset) */
    u32 *x = (u32 *)(vb + *fill);
    for (k = 0; k < n; k++, x += 5) {
      const u32 xy0 = x[0];
      const u32 ax = (u32)((s32)(s16)xy0 + e->ox), ay = (u32)(((s32)xy0 >> 16) + e->oy);
      x[0] = (ax & 0xFFFFu) | ay << 16;
    }
  }
  {
    GrVertex *v = vb + *fill;
    if (g_psyxPolyHook) {
      /* the primitive's own x/y, as MakeVertex* passes them */
      short *p[4];
      for (k = 0; k < n; k++)
        p[k] = (short *)(uintptr_t)xy[n == 4 ? k_order[k] : k];
      g_psyxPolyHook(v, p, (int)n);
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

/* The records of one marker, from the one at word `at` of pool w (cap
 * words), in their order. */
static void emit(const u32 *w, u32 at, u32 cap) {
  Em e;
  em_begin(&e);
  for (; at < cap; at = w[at]) {
    s_records++;
    em_prim(&e, w[at + 1], w + at + 2);
  }
}

/* native_draw 1 and 2 (D92): a packet that holds only polygons, E1 draw
 * modes and NOPs (GP0 00h-1Fh but 02h, as the walk's split skips them),
 * parsed here with the records' converter, from its words where it lies.
 * Anything else (lines, rectangles, fills, the other draw environment
 * commands, a command that runs past the packet) is left to PsyCross: 0. */
static const u8 k_poly_kind[8] = {ND_K_F3, ND_K_FT3, ND_K_F4, ND_K_FT4,
                                  ND_K_G3, ND_K_GT3, ND_K_G4, ND_K_GT4};
int g_nd_fast = 1;
int g_nd_fast_off; /* the check's reference walk: PsyCross's own */
static unsigned s_fast_packets, s_fast_polys;

/* the words of the command at c (code in its top byte), or 0 if this parse
 * doesn't take it */
static inline u32 fast_words(u32 c) {
  const u32 code = c >> 24;
  if ((code & 0xE0) == 0x20) {
    const u32 nv = code & 8 ? 4 : 3;
    return 1 + nv * (code & 4 ? 2 : 1) + (code & 0x10 ? nv - 1 : 0);
  }
  if (code == 0xE1 || (code < 0x20 && code != 0x02))
    return 1;
  return 0;
}

static int fast_packet(const u32 *m) {
  const u32 *c = m + 1, *const end = c + (m[0] >> 24), *p;
  Em e;
  for (p = c; p < end;) {
    const u32 n = fast_words(*p);
    if (!n || p + n > end)
      return 0;
    p += n;
  }
  em_begin(&e);
  for (p = c; p < end;) {
    const u32 code = *p >> 24, n = fast_words(*p);
    if ((code & 0xE0) == 0x20) {
      em_prim(&e, k_poly_kind[code >> 2 & 7], p);
      s_fast_polys++;
    } else if (code == 0xE1) {
      em_prim(&e, ND_K_E1, p);
    }
    p += n;
  }
  s_fast_packets++;
  return 1;
}

int nd_packet(uintptr_t packet) {
  const u32 *m = (const u32 *)packet;
  u32 w1, pool;
  w1 = m[1];
  if ((m[0] >> 24) != 2 || (w1 & 0xFFF000u) != ND_MAGIC || (w1 >> 24)) {
    const u32 cls = w1 >> 29;
    if (g_nd_fast && !g_nd_fast_off && psx_walk_nd() &&
        (g_nd_pcheck ? nd_pcheck_packet(m, fast_packet) : fast_packet(m)))
      return 1;
    s_other[cls == 1 ? 0 : cls == 2 ? 1 : cls == 3 ? 2 : 3]++;
    return 0;
  }
  pool = w1 >> 11 & 1;
  s_markers++;
  emit(g_nd_pool[pool].w, m[2], g_nd_pool[pool].cap);
  return 1;
}

void nd_report(int vblank) {
  if (!s_log || !g_nd.base)
    return; /* no pools (nd_ab turns g_native_draw on and off) */
  s_log("native draw %d: %u lists (%u with records), %u primitives walked as %u markers "
        "(%u joined), %u draw modes, %u left as primitives (pool full); %u polygons walked; "
        "%u KB of records, the most in a list %u KB; the walk parsed %u polygons, %u lines, "
        "%u rectangles and %u other packets itself; it parsed %u packets (%u polygons) of PS1 "
        "primitives with the records' converter (nd_fast, D92)",
        vblank, s_lists, s_lists_native, s_records, s_markers, s_records - s_markers, s_e1,
        g_nd_fallback, s_emitted, (unsigned)(s_words * 4 / 1024), (unsigned)(s_peak * 4 / 1024),
        s_other[0], s_other[1], s_other[2], s_other[3], s_fast_packets, s_fast_polys);
  g_nd_fallback = 0;
  s_fast_packets = s_fast_polys = 0;
  s_lists = s_lists_native = s_records = s_markers = s_e1 = s_emitted = 0;
  memset(s_other, 0, sizeof s_other);
  s_peak = 0;
  s_words = 0;
}

/* Native draw lists (decisions D82, D86, D91): the record pools, the list
 * turn-over and the OT walk's side, which emits a marker's records as
 * PsyCross's parse would have emitted the primitives they came from. The
 * walk (gpu_walk.c, D117) calls nd_packet for each packet and nd_poly for
 * each polygon command it parses itself.
 * native_draw.h has the format and the renderers' side. */
#include "native_draw.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "psx_lean.h"
#include "psx_vcache.h"
#include "psx_walk.h"

int g_native_draw;
int g_nd_walk; /* the walk gives nd_packet each packet (nd_init) */
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

/* Direct vertices (D99): the walk's counts: direct triangles, those the
 * GPU skips (left as they are), lists walked direct, triangles left for
 * want of a range. The ranges are the walk's (PsxGpuWalk's dr, D122): they
 * go when it empties its vertex buffer. */
int g_nd_direct;
int g_nd_direct_ok;
int g_nd_dcheck;
static unsigned s_dr_tris, s_dr_skipped, s_dr_full;

_Static_assert(ND_DR_MAX == PSX_GPU_DIRECT_MAX, "the walk's room for the direct ranges");

/* the game thread's walk's, or while the presenter draws inline through the
 * GR_* calls its own (stage 7f, D126) */
PsxGpuWalk *g_nd_ranges_walk;

int nd_direct_ranges(const u32 **r) {
  const PsxGpuWalk *const w = g_nd_ranges_walk ? g_nd_ranges_walk : psx_gpu_game_walk();
  *r = &w->dr[0][0];
  return (int)w->ndr;
}

/* the triangle at vertex i into the walk's ranges: 0 if there's no room */
static inline int dr_add(PsxGpuWalk *w, u32 i) {
  if (w->ndr && w->dr[w->ndr - 1][1] == i) {
    w->dr[w->ndr - 1][1] = i + 3;
    return 1;
  }
  if (w->ndr == ND_DR_MAX)
    return 0;
  w->dr[w->ndr][0] = i;
  w->dr[w->ndr][1] = i + 3;
  w->ndr++;
  return 1;
}

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
  g_nd_walk = 1;
}

void nd_begin(void) {
  if (g_nd_check)
    nd_check_began();
  g_nd.on = g_native_draw == 1 && g_fast_render_ro >= 2; /* 2, and 3 (D124) */
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
  /* the walk's mode for the list just linked, and whether it is walked
   * direct (D99): only when the renderer takes it now */
  g_psx_walk_nd = g_native_draw;
  if (g_native_draw && g_nd_direct && g_nd_direct_ok)
    g_psx_walk_nd |= g_nd_direct == 2 ? ND_WALK_RANGES : ND_WALK_DIRECT;
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

/* PsyCross's DrawEnvOffset, as integers (its float sums are exact here;
 * gpu_walk.c's draw_offset), under the walk's environments */
static void draw_offset(const PsxGpuWalk *w, int *ox, int *oy) {
  if (w->env->dfe) {
    *ox = w->env->ofs[0] - w->disp->disp.x;
    *oy = w->env->ofs[1] - w->disp->disp.y;
  } else {
    *ox = *oy = 0;
  }
}

/* a record's polygon, as the walk sees it */
enum { F_TEX = 1, F_SEMI = 2 };

/* What AddSplit compares that a record can change: the blend (from the
 * tpage, semi-transparent only), the texture format (from the tpage, for
 * every polygon), textured or not, and drawing to the display area. */
static inline u32 split_key(const DRAWENV *env, u32 f) {
  const u32 tp = env->tpage;
  const u32 blend = (f & F_SEMI) ? ((tp >> 5) & 3) + 1 : 0;
  return blend | ((tp >> 7) & 3) << 3 | (f & F_TEX) << 5 | (u32)(env->dfe != 0) << 6;
}

#define ND_RAW 0x808080u /* an unshaded texture's colour (MakeColourNoShade) */

/* The walk's state while it emits: the walk and its draw environment, the
 * vertex buffer, the last split key (AddSplit is only called when it
 * changes), the dither mask and the draw offset */
typedef struct {
  PsxGpuWalk *w;
  DRAWENV *env;
  GrVertex *vb;
  int *fill;
  u32 prev, dmask;
  int ox, oy;
  u32 direct; /* the list's ND_WALK_* bits (D99) */
} Em;

/* host --nd-direct-check: the bits a packet is parsed with, both ways (-1:
 * the list's) */
static int s_dc_force = -1;

/* the records' mode of the list w walks: a walk of its own (the
 * presenter's, stage 7f, D126) keeps its list's; else the handover's */
static inline int walk_nd(const PsxGpuWalk *w) { return w->own ? w->nd : psx_walk_nd(); }

static void em_begin(Em *e, PsxGpuWalk *w) {
  e->w = w;
  e->env = w->env;
  e->vb = w->vb;
  e->fill = &w->nv;
  e->prev = ~0u;
  draw_offset(w, &e->ox, &e->oy);
  e->dmask = e->env->dtd ? 1u << 24 : 0;
  /* a walk of its own (the presenter's, stage 7f, D126) keeps its list's mode */
  e->direct = (u32)(s_dc_force >= 0 ? s_dc_force : walk_nd(w)) & (ND_WALK_DIRECT | ND_WALK_RANGES);
}

/* D99: the triangles just made at vertex i (nt of them, at v), each one the
 * GPU doesn't skip in the renderer's lean form (ND_WALK_DIRECT) and in the
 * ranges, or only in the ranges (ND_WALK_RANGES, the Vita's check). A
 * skipped one, or one with no range left, stays as it is: the renderer
 * converts it, as it does PsyCross's.
 * D105: lean_skipped's and lean_box's rules on whole words (psx_lean.h's
 * lane operations), each vertex's u/v word made once, and a quad's two
 * triangles (prim vertices 0 1 2 and 1 2 3: TriangulateQuad's) sharing
 * their edge's test and their two vertices' lowest bytes. Untextured
 * polygons' u/v are 0, so their box is 0. The same boxes and the same
 * triangles as the rule: --nd-direct-check compares them, tests/lean the
 * lanes. */
static inline void em_direct_one(const Em *e, u32 *t, u32 i, u32 far, u32 box) {
  if (far) {
    s_dr_skipped++;
    return;
  }
  if (!dr_add(e->w, i)) {
    s_dr_full++;
    return;
  }
  if (e->direct & ND_WALK_DIRECT)
    t[4] = t[LEAN_VW + 4] = t[2 * LEAN_VW + 4] = box;
  s_dr_tris++;
}

static void em_direct(const Em *e, GrVertex *v, u32 i, u32 nt, u32 tex) {
  u32 *const t = (u32 *)v;
  const u32 x0 = t[0], x1 = t[LEAN_VW], x2 = t[2 * LEAN_VW];
  const u32 e12 = lean_edge_far(x1, x2);
  u32 m12 = 0, b0 = 0, b1 = 0;
  if (tex) {
    m12 = lean_min8(lean_uvw(t[LEAN_VW + 2]), lean_uvw(t[2 * LEAN_VW + 2]));
    b0 = lean_box_of(lean_min8(m12, lean_uvw(t[2])));
  }
  em_direct_one(e, t, i, e12 | lean_edge_far(x0, x1) | lean_edge_far(x2, x0), b0);
  if (nt == 2) {
    /* the second triangle's vertices 3 and 4 are 1 and 2 */
    const u32 x5 = t[5 * LEAN_VW];
    if (tex)
      b1 = lean_box_of(lean_min8(m12, lean_uvw(t[5 * LEAN_VW + 2])));
    em_direct_one(e, t + 3 * LEAN_VW, i + 3, e12 | lean_edge_far(x2, x5) | lean_edge_far(x5, x1),
                  b1);
  }
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
    e->env->tpage = (u_short)(c0 & 0x1FF);
    e->env->dtd = (u_char)(c0 >> 9 & 1);
    e->env->dfe = (u_char)(c0 >> 10 & 1);
    draw_offset(e->w, &e->ox, &e->oy);
    e->dmask = e->env->dtd ? 1u << 24 : 0;
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
    e->env->tpage = (u_short)tp;
    if (kind == ND_K_FT3 && (*xy[0] & *xy[1] & *xy[2]) == 0xFFFFFFFFu)
      return; /* IsNull: SCE's tpage-only hack */
  }
  key = split_key(e->env, f);
  if (key != e->prev) {
    psx_gpu_add_split_in(e->w, (f & F_SEMI) != 0, (f & F_TEX) != 0);
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
    if (psx_vcache_polys) {
      /* the primitive's own x/y, as MakeVertex* passes them */
      short *p[4];
      for (k = 0; k < n; k++)
        p[k] = (short *)(uintptr_t)xy[n == 4 ? k_order[k] : k];
      psx_vcache_poly(v, p, (int)n, g_vcache_side + *fill); /* (psx_vcache.h) */
    }
    if (n == 4) {
      /* TriangulateQuad */
      v[4] = v[3];
      v[5] = v[2];
      v[2] = v[3];
      v[3] = v[1];
    }
    if (e->direct)
      em_direct(e, v, (u32)*fill, n == 4 ? 2 : 1, f & F_TEX);
    *fill += n == 4 ? 6 : 3;
  }
  s_emitted++;
}

/* The records of one marker, from the one at word `at` of pool w (cap
 * words), in their order. */
static void emit(PsxGpuWalk *wk, const u32 *w, u32 at, u32 cap) {
  Em e;
  em_begin(&e, wk);
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

static int fast_packet(PsxGpuWalk *w, const u32 *m) {
  const u32 *c = m + 1, *const end = c + (m[0] >> 24), *p;
  Em e;
  for (p = c; p < end;) {
    const u32 n = fast_words(*p);
    if (!n || p + n > end)
      return 0;
    p += n;
  }
  em_begin(&e, w);
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

/* One polygon command (GP0 20h-3Fh, its words at c) the walk parses itself
 * (gpu_walk.c, D117): its vertices as PsyCross's Process*Poly made them,
 * with a split of its own as each of its polygons asked AddSplit, never
 * direct (the polygons the walk parsed weren't, D99), not counted. */
void nd_poly(PsxGpuWalk *w, const u32 *c) {
  const unsigned emitted = s_emitted;
  Em e;
  em_begin(&e, w);
  e.direct = 0;
  em_prim(&e, k_poly_kind[*c >> 26 & 7], c);
  s_emitted = emitted;
}

/* a marker's records (m: the marker) */
static int marker_packet(PsxGpuWalk *w, const u32 *m) {
  const u32 pool = m[1] >> 11 & 1;
  if (w->own && w->nd_pool) /* the presenter's own pool (stage 7f, D126) */
    emit(w, w->nd_pool, m[2], w->nd_pool_cap);
  else
    emit(w, g_nd_pool[pool].w, m[2], g_nd_pool[pool].cap);
  return 1;
}

/* host --nd-direct-check (D99): the packet parsed by fn twice from the same
 * state, without and with direct vertices. The splits, the draw
 * environment and the vertex count must be the same; each triangle in the
 * ranges the second way added must be the first way's after
 * lean_direct_tri, and each one not in them the first way's as it was, and
 * one the GPU skips (or none left for want of a range). What stays is the
 * direct way's with nd_direct on, else the reference's. */
static struct {
  unsigned long long packets, tris, direct, bad;
} s_dc;
static GrVertex *s_dc_ref;
static PsxGpuSplitState s_dc_state;

static int dcheck_packet(PsxGpuWalk *w, const u32 *m, int (*fn)(PsxGpuWalk *w, const u32 *m)) {
  int *const fill = &w->nv;
  int fill0, fill1, split0, nsplit, i, bad = 0;
  int ref_split[64][8];
  GrVertex *const vb = w->vb;
  const DRAWENV env0 = *w->env;
  DRAWENV env1;
  const u32 n0 = w->ndr, end0 = n0 ? w->dr[n0 - 1][1] : 0;
  u32 r = n0 ? n0 - 1 : 0;
  if (!s_dc_ref)
    s_dc_ref = malloc(sizeof(GrVertex) * MAX_VERTEX_BUFFER_SIZE);
  if (!s_dc_ref)
    return fn(w, m);
  fill0 = *fill;
  split0 = w->ns;
  psx_gpu_split_state_save_in(w, &s_dc_state);
  s_dc_force = 0;
  i = fn(w, m);
  s_dc_force = -1;
  if (!i)
    return 0; /* not taken (fast_packet decides before it emits) */
  fill1 = *fill;
  nsplit = w->ns - split0;
  if (nsplit > 63)
    nsplit = 63;
  for (i = 0; i <= nsplit; i++)
    psx_gpu_split_get_in(w, split0 + i, ref_split[i]);
  ref_split[nsplit][5] = 0; /* the open split's count: the walk sets it after the packet */
  env1 = *w->env;
  memcpy(s_dc_ref, vb + fill0, (size_t)(fill1 - fill0) * sizeof(GrVertex));
  psx_gpu_split_state_restore_in(w, &s_dc_state);
  *w->env = env0;
  s_dc_force = ND_WALK_DIRECT;
  fn(w, m);
  s_dc_force = -1;
  s_dc.packets++;
  if (*fill != fill1 || w->ns - split0 != nsplit)
    bad = 1;
  for (i = 0; !bad && i <= nsplit; i++) {
    int got[8];
    psx_gpu_split_get_in(w, split0 + i, got);
    if (i == nsplit)
      got[5] = 0;
    if (memcmp(got, ref_split[i], sizeof got))
      bad = 2;
  }
  if (!bad && memcmp(w->env, &env1, sizeof env1))
    bad = 3;
  /* the triangles: each in a range the direct way added, or not */
  for (i = fill0; !bad && i + 3 <= fill1; i += 3) {
    u32 want[15];
    int in;
    while (r < w->ndr && w->dr[r][1] <= (u32)i)
      r++;
    in = r < w->ndr && w->dr[r][0] <= (u32)i && (r >= n0 || (u32)i >= end0);
    memcpy(want, s_dc_ref + (i - fill0), sizeof want);
    s_dc.tris++;
    if (in) {
      lean_direct_tri(want);
      s_dc.direct++;
    } else if (!lean_skipped(want) && w->ndr < ND_DR_MAX) {
      bad = 4; /* a triangle left as it was that should have been direct */
    }
    if (!bad && memcmp(vb + i, want, sizeof want))
      bad = 5;
  }
  if (!bad && (fill1 - fill0) % 3)
    bad = 6;
  if (bad && s_dc.bad++ < 20 && s_log) {
    s_log("native draw direct check: packet %llu (len %u, first command %08x): %s (vertices "
          "%d/%d, splits %d)", s_dc.packets, m[0] >> 24, m[1],
          bad == 1   ? "counts differ"
          : bad == 2 ? "splits differ"
          : bad == 3 ? "draw environments differ"
          : bad == 4 ? "a triangle not made direct"
          : bad == 5 ? "vertices differ"
                     : "not whole triangles",
          fill1 - fill0, *fill - fill0, nsplit);
    if (bad == 5) {
      const u32 *x = (const u32 *)(s_dc_ref + (i - 3 - fill0)), *y = (const u32 *)(vb + i - 3);
      s_log("  triangle at %d, first vertex: %08x %08x %08x %08x %08x / %08x %08x %08x %08x %08x",
            i - 3, x[0], x[1], x[2], x[3], x[4], y[0], y[1], y[2], y[3], y[4]);
    }
  }
  if (!(walk_nd(w) & ND_WALK_DIRECT)) {
    /* the reference stays */
    memcpy(vb + fill0, s_dc_ref, (size_t)(fill1 - fill0) * sizeof(GrVertex));
    w->ndr = n0;
    if (n0)
      w->dr[n0 - 1][1] = end0;
  }
  return 1;
}

void nd_dcheck_report(void) {
  if (g_nd_dcheck && s_log)
    s_log("native draw direct check: %llu packets parsed both ways, %llu differ; %llu "
          "triangles, %llu of them direct (D99)",
          s_dc.packets, s_dc.bad, s_dc.tris, s_dc.direct);
}

int nd_packet(PsxGpuWalk *w, uintptr_t packet) {
  const u32 *m = (const u32 *)packet;
  u32 w1;
  w1 = m[1];
  if ((m[0] >> 24) != 2 || (w1 & 0xFFF000u) != ND_MAGIC || (w1 >> 24)) {
    const u32 cls = w1 >> 29;
    if (g_nd_fast && walk_nd(w) &&
        (g_nd_pcheck   ? nd_pcheck_packet(w, m, fast_packet)
         : g_nd_dcheck ? dcheck_packet(w, m, fast_packet)
                       : fast_packet(w, m)))
      return 1;
    s_other[cls == 1 ? 0 : cls == 2 ? 1 : cls == 3 ? 2 : 3]++;
    return 0;
  }
  s_markers++;
  if (g_nd_dcheck)
    return dcheck_packet(w, m, marker_packet);
  return marker_packet(w, m);
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
  if (s_dr_tris || s_dr_skipped || s_dr_full)
    s_log("native draw %d: %u triangles %s direct, %u the GPU skips and %u past the ranges' room "
          "left to the renderer (nd_direct=%d, D99)",
          vblank, s_dr_tris, g_nd_direct == 2 ? "listed (not made)" : "made", s_dr_skipped,
          s_dr_full, g_nd_direct);
  s_dr_tris = s_dr_skipped = s_dr_full = 0;
  g_nd_fallback = 0;
  s_fast_packets = s_fast_polys = 0;
  s_lists = s_lists_native = s_records = s_markers = s_e1 = s_emitted = 0;
  memset(s_other, 0, sizeof s_other);
  s_peak = 0;
  s_words = 0;
}

/* native_draw=1's per-call check (decisions D82; host --native-draw-check).
 *
 * Each call of a converted renderer runs twice from the same state: first
 * with its primitives linked as they are (native_draw=0), then as native
 * records (native_draw=1), whose results the game goes on with. Then:
 *  - the packets each run appended to the OT, bucket by bucket in the order
 *    the OT link walks them (the world OT's highest bucket first, then the
 *    HUD's), go through an OT walk from the same draw state, and the
 *    vertices, the splits and the draw state they leave must be the same
 *    bytes: the primitives through PsyCross's own walk (the reference
 *    library's, walk_check.h, D117), the records through ours (gpu_walk.c);
 *  - the arena outside the primitives written and the two OTs, the
 *    scratchpad buffer and every GTE register must be the same.
 * The reference walk is PsyCross's, so this checks the records against what
 * PsyCross makes of the primitives, not against a copy of its rules. A run
 * of the host's scripts checks every call they make. With nd_fast (D92) the
 * primitives are walked a third time, by our walk as native_draw=2 parses
 * them (nd_packet's own parse of each packet), which must give the same
 * bytes too. Needs the reference build (SPYRO_PSYCROSS_REF). */
#include "native_draw.h"
#include "render_hand.h"

#include <stdio.h>
#include <assert.h> /* PsyCross headers use static_assert */
#include <stdlib.h>
#include <string.h>

#include "psx_arena.h"
#include "psx_gpu.h"
#include "walk_check.h"

extern DRAWENV activeDrawEnv;
extern char g_WorldOT[], g_HudOT[]; /* the OTs' addresses (order_table.c) */
extern char D_800757B0[];

static void (*s_log)(const char *fmt, ...);
void nd_check_set_log(void (*log)(const char *fmt, ...)) { s_log = log; }

enum { OT_SLOTS = 0x800, MAX_NAMES = 16, MAX_PACKETS = 0x10000 };

static unsigned char *s_before, *s_ps1;
static unsigned char s_scr_before[sizeof g_psx_scratch], s_scr_ps1[sizeof g_psx_scratch];
static GTERegisters s_gte_before, s_gte_ps1;
static u32 s_tail0[OT_SLOTS + 1], s_head0[OT_SLOTS + 1];
/* a slot whose tail is a marker of this list: its last record (else ND_END) */
static u32 s_last0[OT_SLOTS + 1];
static int s_began;

static struct {
  const char *name;
  unsigned long calls, bad, skipped;
  unsigned long long tris, packets_ps1, packets_nd;
} s_stats[MAX_NAMES];

static int stat_index(const char *name) {
  int i;
  for (i = 0; i < MAX_NAMES && s_stats[i].name; i++)
    if (!strcmp(s_stats[i].name, name))
      return i;
  if (i == MAX_NAMES)
    return MAX_NAMES - 1;
  s_stats[i].name = name;
  return i;
}

/* slot i: the world OT's buckets 0..OT_SLOTS-1, then the HUD OT's one */
static u32 *slot_at(int i) {
  if (i == OT_SLOTS)
    return (u32 *)(uintptr_t)MIPS_KUSEG(AT32_ND(ADDR_ND(g_HudOT)));
  return (u32 *)(uintptr_t)MIPS_KUSEG(AT32_ND(ADDR_ND(g_WorldOT))) + i * 2;
}

static void slots_save(void) {
  int i;
  for (i = 0; i <= OT_SLOTS; i++) {
    s_tail0[i] = slot_at(i)[0];
    s_head0[i] = slot_at(i)[1];
    s_last0[i] = ND_END;
    if (s_tail0[i] && nd_is_mark(s_tail0[i])) {
      /* its last record: from the first, as the walk goes (D91: the side
       * table has it, the records don't) */
      const u32 *w = g_nd_pool[g_nd_cur].w;
      u32 at = AT32_ND(MIPS_KUSEG(s_tail0[i]) + 8);
      while (w[at] != ND_END)
        at = w[at];
      s_last0[i] = at;
    }
  }
}

static u32 *link_of(u32 packet) {
  return (u32 *)MEM_OT_NEXT(AT32_ND(MIPS_KUSEG(packet)));
}

/* The packets this call appended, in the OT walk's order, copied into buf
 * (walked in that order by seq_next). Returns the count. */
typedef struct {
  u32 *words;
  u32 n, cap;
  u32 *at[MAX_PACKETS];
  int count;
} Seq;
static Seq s_seq[2];

static int collect(Seq *q) {
  int i;
  q->n = 0;
  q->count = 0;
  for (i = OT_SLOTS; i >= 0; i--) {
    /* the world OT from its highest bucket down, then the HUD's */
    const int s = i == OT_SLOTS ? -1 : i;
    const int slot = s < 0 ? OT_SLOTS : OT_SLOTS - 1 - s;
    const u32 *sl = slot_at(slot);
    u32 *p, guard = 0;
    if (s_last0[slot] != ND_END) {
      /* records this call joined to the marker an earlier one left at the
       * tail: as a marker of their own, first */
      const u32 *w = g_nd_pool[g_nd_cur].w;
      const u32 next = w[s_last0[slot]];
      if (next != ND_END) {
        if (q->count == MAX_PACKETS || q->n + 3 > q->cap)
          return -1;
        q->at[q->count++] = q->words + q->n;
        q->words[q->n] = 0x2000000u;
        q->words[q->n + 1] = g_nd.id;
        q->words[q->n + 2] = next;
        q->n += 3;
      }
    }
    if (sl[0] == s_tail0[slot])
      continue;
    p = s_tail0[slot] ? link_of(s_tail0[slot]) : (u32 *)(uintptr_t)MIPS_KUSEG(sl[1]);
    for (;;) {
      const u32 len = p[0] >> 24;
      if (q->count == MAX_PACKETS || q->n + len + 1 > q->cap || ++guard > 0x40000)
        return -1;
      q->at[q->count++] = q->words + q->n;
      memcpy(q->words + q->n, p, (len + 1) * 4);
      q->words[q->n] &= 0xFF000000u;
      q->n += len + 1;
      if ((u32)(uintptr_t)p == MIPS_KUSEG(sl[0]))
        break;
      p = link_of((u32)(uintptr_t)p);
    }
  }
  return q->count;
}

/* a walk over a sequence: each packet's link is the next one */
static Seq *s_walking;
static int s_walk_i;
static uintptr_t seq_next(uintptr_t packet) {
  (void)packet;
  return (uintptr_t)s_walking->at[++s_walk_i];
}

typedef struct {
  GrVertex *v;
  int nv, nsplit;
  int split[512][8];
  u_short tpage;
  u_char dtd, dfe;
} Walked;
static Walked s_walked[3];

/* the sequence through PsyCross's walk (ref) or ours, from env0 and empty
 * splits */
static void walk_seq(Seq *q, Walked *w, const DRAWENV *env0, int ref) {
  int *fill, ref_fill, i;
  const GrVertex *vb;
  const DRAWENV *env;
  activeDrawEnv = *env0;
  if (ref)
    walk_ref_sync();
  w->nv = 0;
  w->nsplit = 0;
  if (q->count) {
    q->words[q->at[q->count - 1] - q->words] |= 0xFFFFFFu; /* the end */
    s_walking = q;
    s_walk_i = 0;
    if (ref)
      walk_ref_parse((u_long *)q->at[0], 0, seq_next);
    else
      psx_gpu_walk((u_long *)q->at[0], 0, seq_next);
  }
  if (ref) {
    vb = walk_ref_vertices(&ref_fill);
    w->nv = ref_fill;
    w->nsplit = walk_ref_split_count();
    env = walk_ref_env();
  } else {
    vb = psx_gpu_vertex_buffer(&fill);
    w->nv = *fill;
    w->nsplit = psx_gpu_split_count();
    env = &activeDrawEnv;
  }
  memcpy(w->v, vb, (size_t)w->nv * sizeof *vb);
  for (i = 1; i <= w->nsplit && i <= 512; i++) {
    if (ref)
      walk_ref_split_get(i, w->split[i - 1]);
    else
      psx_gpu_split_get(i, w->split[i - 1]);
  }
  w->tpage = env->tpage;
  w->dtd = env->dtd;
  w->dfe = env->dfe;
  if (ref)
    walk_ref_clear();
  else
    psx_gpu_clear_splits();
}

static int walked_differ(const Walked *a, const Walked *b) {
  int n = a->nsplit < 512 ? a->nsplit : 512;
  if (a->nv != b->nv || a->nsplit != b->nsplit || a->tpage != b->tpage || a->dtd != b->dtd ||
      a->dfe != b->dfe)
    return 1;
  if (memcmp(a->v, b->v, (size_t)a->nv * sizeof *a->v))
    return 2;
  if (memcmp(a->split, b->split, (size_t)n * sizeof a->split[0]))
    return 3;
  return 0;
}

static u32 s_ram_at, s_ram_lo, s_ram_hi; /* ram_differ's first difference, the primitives */

static void report(int k, int what, const Walked *a, const Walked *b) {
  int i;
  if (!s_log)
    return;
  if (what == 4) {
    s_log("native draw check: %s call %lu: RAM differs from arena offset %06x (primitives "
          "%06x-%06x)", s_stats[k].name, s_stats[k].calls, s_ram_at, s_ram_lo, s_ram_hi);
    return;
  }
  s_log("native draw check: %s call %lu: %s differ (vertices %d/%d, splits %d/%d, tpage %04x/%04x)",
        s_stats[k].name, s_stats[k].calls,
        what == 1 ? "counts" : what == 2 ? "vertices" : "splits", a->nv, b->nv,
        a->nsplit, b->nsplit, a->tpage, b->tpage);
  if (what == 2)
    for (i = 0; i < a->nv; i++)
      if (memcmp(&a->v[i], &b->v[i], sizeof a->v[i])) {
        const u32 *x = (const u32 *)&a->v[i], *y = (const u32 *)&b->v[i];
        s_log("  vertex %d: %08x %08x %08x %08x %08x / %08x %08x %08x %08x %08x", i, x[0], x[1],
              x[2], x[3], x[4], y[0], y[1], y[2], y[3], y[4]);
        break;
      }
}

/* the arena but the primitives [lo, hi) and the two OTs */
static int ram_differ(const unsigned char *a, const unsigned char *b, u32 lo, u32 hi) {
  const u32 base = (u32)g_psx_arena_base;
  const u32 wot = MIPS_KUSEG(AT32_ND(ADDR_ND(g_WorldOT))) - base;
  const u32 hot = MIPS_KUSEG(AT32_ND(ADDR_ND(g_HudOT))) - base;
  u32 cut[3][2] = {{lo - base, hi - base}, {wot, wot + OT_SLOTS * 8}, {hot, hot + 8}};
  s_ram_lo = lo - base;
  s_ram_hi = hi - base;
  u32 at = 0;
  for (;;) {
    u32 next = PSX_MEM_SIZE, skip = PSX_MEM_SIZE;
    int i;
    for (i = 0; i < 3; i++)
      if (cut[i][1] > at && cut[i][0] < next) {
        next = cut[i][0] > at ? cut[i][0] : at;
        skip = cut[i][1];
      }
    if (next > at && memcmp(a + at, b + at, next - at)) {
      u32 k = at;
      while (a[k] == b[k])
        k++;
      s_ram_at = k;
      return 1;
    }
    if (next >= PSX_MEM_SIZE)
      return 0;
    at = skip;
  }
}

void nd_check_began(void) { s_began = 1; }

u32 nd_check(const char *name, NdFn fn, u32 a0, u32 a1, u32 a2, u32 a3) {
  const int k = stat_index(name);
  const u32 sp = g_mips_sp, v1 = g_mips_v1;
  u32 r, fp0, fp1;
  int *fill, bad = 0, i;
  DRAWENV env0;
  if (!s_before) {
    s_before = malloc(PSX_MEM_SIZE);
    s_ps1 = malloc(PSX_MEM_SIZE);
    for (i = 0; i < 2; i++) {
      s_seq[i].cap = 0x100000;
      s_seq[i].words = malloc(s_seq[i].cap * 4);
      s_walked[i].v = malloc(MAX_VERTEX_BUFFER_SIZE * sizeof(GrVertex));
    }
    s_walked[2].v = malloc(MAX_VERTEX_BUFFER_SIZE * sizeof(GrVertex));
  }
  memcpy(s_before, (void *)g_psx_arena_base, PSX_MEM_SIZE);
  memcpy(s_scr_before, g_psx_scratch, sizeof s_scr_before);
  s_gte_before = gteRegs;
  fp0 = AT32_ND(ADDR_ND(D_800757B0));
  slots_save();

  g_native_draw = 0;
  s_began = 0;
  fn(a0, a1, a2, a3);
  g_native_draw = 1;
  if (!s_began)
    return 0; /* not converted: once is enough (v0 is dead at every caller) */
  if (collect(&s_seq[0]) < 0)
    s_stats[k].skipped++;
  memcpy(s_ps1, (void *)g_psx_arena_base, PSX_MEM_SIZE);
  memcpy(s_scr_ps1, g_psx_scratch, sizeof s_scr_ps1);
  s_gte_ps1 = gteRegs;

  memcpy((void *)g_psx_arena_base, s_before, PSX_MEM_SIZE);
  memcpy(g_psx_scratch, s_scr_before, sizeof s_scr_before);
  gteRegs = s_gte_before;
  g_mips_sp = sp;
  g_mips_v1 = v1;
  r = fn(a0, a1, a2, a3);
  fp1 = AT32_ND(ADDR_ND(D_800757B0));
  s_stats[k].calls++;

  /* where a bucket's tail was a marker of this list, the run with records
   * joins it, and the other links its first primitive from it: that
   * marker's tag is the one word allowed to differ (the joined records are
   * compared below) */
  for (i = 0; i <= OT_SLOTS; i++)
    if (s_last0[i] != ND_END) {
      const u32 at = MIPS_KUSEG(s_tail0[i]) - (u32)g_psx_arena_base;
      memcpy(s_ps1 + at, (const unsigned char *)g_psx_arena_base + at, 4);
    }
  if (ram_differ(s_ps1, (const unsigned char *)g_psx_arena_base, fp0, fp1) ||
      memcmp(s_scr_ps1, g_psx_scratch, sizeof s_scr_ps1) ||
      memcmp(&s_gte_ps1, &gteRegs, sizeof gteRegs)) {
    bad = 4;
    if (s_stats[k].bad++ < 20) {
      report(k, 4, &s_walked[0], &s_walked[0]);
      if (getenv("ND_DEBUG")) {
        const u32 *x = (const u32 *)(s_ps1 + (s_ram_at & ~3u) - 16);
        const u32 *y = (const u32 *)((const unsigned char *)g_psx_arena_base + (s_ram_at & ~3u) - 16);
        for (i = 0; i < 16; i++)
          s_log("  %06x: %08x %08x", (s_ram_at & ~3u) - 16 + i * 4, x[i], y[i]);
      }
    }
  }

  (void)psx_gpu_vertex_buffer(&fill);
  if (!bad && collect(&s_seq[1]) >= 0 && *fill == 0 && psx_gpu_split_count() == 0) {
    env0 = activeDrawEnv;
    walk_seq(&s_seq[0], &s_walked[0], &env0, 1); /* the primitives as PsyCross parses them */
    walk_seq(&s_seq[1], &s_walked[1], &env0, 0); /* the records, our walk */
    /* and as native_draw=2's walk parses them (D92), when nd_fast is on */
    if (g_nd_fast)
      walk_seq(&s_seq[0], &s_walked[2], &env0, 0);
    activeDrawEnv = env0;
    s_stats[k].packets_ps1 += (unsigned)s_seq[0].count;
    s_stats[k].packets_nd += (unsigned)s_seq[1].count;
    s_stats[k].tris += (unsigned)(s_walked[0].nv / 3);
    bad = walked_differ(&s_walked[0], &s_walked[1]);
    if (bad && s_stats[k].bad++ < 20)
      report(k, bad, &s_walked[0], &s_walked[1]);
    if (!bad && g_nd_fast) {
      bad = walked_differ(&s_walked[0], &s_walked[2]);
      if (bad && s_stats[k].bad++ < 20) {
        if (s_log)
          s_log("native draw check: %s call %lu: native_draw=2's parse of the primitives:",
                s_stats[k].name, s_stats[k].calls);
        report(k, bad, &s_walked[0], &s_walked[2]);
      }
    }
  } else if (!bad) {
    s_stats[k].skipped++;
  }
  return r;
}

void nd_check_report(void) {
  int i;
  if (!g_nd_check || !s_log)
    return;
  for (i = 0; i < MAX_NAMES && s_stats[i].name; i++)
    if (s_stats[i].calls)
      s_log("native draw check: %s %lu calls, %lu differ, %lu not compared; %llu triangles, "
          "%llu packets as primitives, %llu as markers",
          s_stats[i].name, s_stats[i].calls, s_stats[i].bad, s_stats[i].skipped, s_stats[i].tris,
          s_stats[i].packets_ps1, s_stats[i].packets_nd);
}

/* native_draw 1 and 2's parse of PS1 packets (D92; host
 * --native-draw-parse-check): each packet nd_packet takes is parsed by
 * PsyCross first (the reference walk, given our state: a copy of the
 * packet alone), and then by fast (nd_packet's own), whose result the walk
 * goes on with. The vertices and splits each adds and the draw environment
 * each leaves must be the same bytes. */
int g_nd_pcheck;
static struct {
  unsigned long long packets, bad, tris;
} s_pc;
static GrVertex s_pc_v[2][64 * 6];

typedef struct {
  int nv, nsplit, split[64][8];
  DRAWENV env;
} PcOut;
static PcOut s_pc_out[2];

/* what a parse added: the reference walk's (ref) or ours */
static void pc_take(PcOut *o, GrVertex *dst, int fill0, int split0, int ref) {
  int *fill, ref_fill, i;
  const GrVertex *vb;
  if (ref) {
    vb = walk_ref_vertices(&ref_fill);
    o->nv = ref_fill - fill0;
    o->nsplit = walk_ref_split_count() - split0;
  } else {
    vb = psx_gpu_vertex_buffer(&fill);
    o->nv = *fill - fill0;
    o->nsplit = psx_gpu_split_count() - split0;
  }
  if (o->nv > 64 * 6)
    o->nv = 64 * 6;
  memcpy(dst, vb + fill0, (size_t)o->nv * sizeof *vb);
  if (o->nsplit > 63)
    o->nsplit = 63;
  for (i = 0; i <= o->nsplit; i++) {
    if (ref)
      walk_ref_split_get(split0 + i, o->split[i]);
    else
      psx_gpu_split_get(split0 + i, o->split[i]);
  }
  /* the last split's count: the walk sets it after each packet (the
   * reference's list walk did); the vertices' count stands for it */
  o->split[o->nsplit][5] = 0;
  o->env = ref ? *walk_ref_env() : activeDrawEnv;
}

/* the game thread's walk only: the reference syncs from it (walk_ref_sync) */
int nd_pcheck_packet(PsxGpuWalk *w, const u32 *m, int (*fast)(PsxGpuWalk *w, const u32 *m)) {
  static u32 one[1 + 256];
  const u32 len = m[0] >> 24;
  int *fill, fill0, split0, r, bad = 0, ns;
  (void)psx_gpu_vertex_buffer(&fill);
  if (len > 256)
    return fast(w, m);
  fill0 = *fill;
  split0 = psx_gpu_split_count();
  /* PsyCross's own: the packet alone, as the end of a list, from our state */
  one[0] = len << 24 | 0xFFFFFFu;
  memcpy(one + 1, m + 1, len * 4);
  walk_ref_sync();
  walk_ref_parse((u_long *)one, 0, NULL);
  pc_take(&s_pc_out[0], s_pc_v[0], fill0, split0, 1);
  r = fast(w, m);
  if (!r)
    return 0; /* not taken: our walk parses it */
  pc_take(&s_pc_out[1], s_pc_v[1], fill0, split0, 0);
  s_pc.packets++;
  s_pc.tris += (unsigned)(s_pc_out[1].nv / 3);
  ns = s_pc_out[0].nsplit;
  if (s_pc_out[0].nv != s_pc_out[1].nv || s_pc_out[0].nsplit != s_pc_out[1].nsplit)
    bad = 1;
  else if (memcmp(s_pc_v[0], s_pc_v[1], (size_t)s_pc_out[0].nv * sizeof(GrVertex)))
    bad = 2;
  else if (memcmp(s_pc_out[0].split, s_pc_out[1].split, (size_t)(ns + 1) * sizeof s_pc_out[0].split[0]))
    bad = 3;
  else if (memcmp(&s_pc_out[0].env, &s_pc_out[1].env, sizeof(DRAWENV)))
    bad = 4;
  if (bad && s_pc.bad++ < 20 && s_log) {
    s_log("native draw parse check: packet %llu (len %u, first command %08x): %s differ "
          "(vertices %d/%d, splits %d/%d)", s_pc.packets, len, m[1],
          bad == 1 ? "counts" : bad == 2 ? "vertices" : bad == 3 ? "splits" : "draw environments",
          s_pc_out[0].nv, s_pc_out[1].nv, s_pc_out[0].nsplit, s_pc_out[1].nsplit);
    if (bad == 2) {
      int i;
      for (i = 0; i < s_pc_out[0].nv; i++)
        if (memcmp(&s_pc_v[0][i], &s_pc_v[1][i], sizeof(GrVertex))) {
          const u32 *x = (const u32 *)&s_pc_v[0][i], *y = (const u32 *)&s_pc_v[1][i];
          s_log("  vertex %d: %08x %08x %08x %08x %08x / %08x %08x %08x %08x %08x", i, x[0], x[1],
                x[2], x[3], x[4], y[0], y[1], y[2], y[3], y[4]);
          break;
        }
    }
  }
  return 1;
}

void nd_pcheck_report(void) {
  if (g_nd_pcheck && s_log)
    s_log("native draw parse check: %llu packets parsed both ways, %llu differ; %llu triangles",
          s_pc.packets, s_pc.bad, s_pc.tris);
}

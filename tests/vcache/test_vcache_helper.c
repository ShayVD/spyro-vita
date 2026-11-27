/* The vertex cache's core-2 helper (#282, docs/vcache-core2.md): a host race
 * test of its claim protocol, before the implementation (#272).
 *
 * vcache.c keeps a projection on its entry's list (lz_add) and works it out
 * when something first reads the entry (lz_need -> lz_flush). #272 adds a
 * third thread, the helper, which works the kept ones out while the game
 * thread records. This is a model of that design, the parts the protocol
 * touches, with the same shapes as vcache.c: an entry's tag (state, count,
 * T_MULTI, T_PEND, the frame's generation), four candidates, the frame's
 * order log with each place's own answer (lc, LOG_LC), the far and lfar
 * arrays, the store of kept records (LzRec, newest first through next) with
 * each list's head, and the repeat record. Its maths is integer (no x87
 * question) and its commit follows rtp_apply_in: the first projection sets
 * the state, later ones add candidates or make the key ambiguous, a key with
 * different points logs each projection's answer. So an entry's answer
 * depends on the order its projections are worked out in, as in vcache.c.
 *
 * Three threads, as on the Vita:
 *  - the game thread records each frame into one of two tables: it keeps a
 *    projection while the helper keeps up (backlog under a limit) or works
 *    it out at once (collect, then commit: today's path), and now and then
 *    reads an entry the way a renderer's hand-over does (collect, then read);
 *  - the walk takes the frame at its handover and looks entries up in its
 *    own order (collect, then read), while the game thread records the next
 *    frame into the other table;
 *  - the helper works through each frame's store in order, claiming
 *    entries, while both run.
 * Each frame's answers are compared with a single thread's (every projection
 * worked out at once, in order): the walk's and the hand-overs' reads as they
 * happen, and at the table's reuse every entry (an FNV hash of its state and
 * candidates, far places by value, as the per-polygon FNV of
 * same-answers-hash-per-polygon checks polygons) and every log place's
 * answer. Every projection must be worked out exactly once.
 *
 * Usage: test_vcache_helper [--iters N] [--frames N] [--seed S] [--stress]
 * VC_RACE_MUTANT=1|2|3 (one deliberate error the test must catch):
 *  1 an owner works an entry out while the helper holds it;
 *  2 the helper clears T_PEND as it starts (lz_flush's first step), so an
 *    owner's fast path reads the entry while it is half done;
 *  3 an owner's flush ignores cov, the helper's last record, and works its
 *    records out again. */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* vcache.c's sizes */
#define NE 8192u   /* VC_SIZE */
#define NR 8192u   /* VC_LOG: places a frame, and kept records */
#define NROW 1024u /* LZ_PROJ: registers a frame */
#define NFAR 1024u /* VC_FAR */
#define NLFAR 1024u /* VC_LFAR */
#define NC 4u       /* VC_CANDS */

/* the tag, as vcache.c's */
#define T_STATE(t) ((t) & 3u)
#define T_N(t) (((t) >> 2) & 7u)
#define T_MULTI 0x20u
#define T_PEND 0x100u
#define T_GEN_SHIFT 9
#define T_GEN(t) ((t) >> T_GEN_SHIFT)
enum { E_VALID = 1, E_INVALID = 2, E_AMB = 3 };
#define FAR_MARK INT16_MIN
#define LOG_LC 0x8000u
#define LOG_E(x) ((x) & 0x1FFFu)

/* The claim word, one per entry (docs/vcache-core2.md, "The claim"):
 *  FRESH  no kept projection offered beyond the entry's one first (the
 *         helper may take it from that record);
 *  Q|idx  kept ones offered up to record idx (the helper may take them once
 *         idx is published);
 *  OWN    the owner side (the game thread recording, the walk after the
 *         handover) works the entry out itself;
 *  H      the helper holds it;
 *  DONE   the helper worked out its records up to cov; the owner side takes
 *         it back with an acquire. */
#define CL_FRESH 0u
#define CL_OWN 1u
#define CL_H 2u
#define CL_DONE 3u
#define CL_Q 0x8000u
#define CL_IDX(c) ((unsigned)(c) & 0x1FFFu)
#define NONE 0xFFFFFFFFu

#define LD(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define ST(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define LDA(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define STR(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define LDS(p) __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define STS(p, v) __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#define CAS(p, e, v, mo) __atomic_compare_exchange_n((p), (e), (v), 0, (mo), __ATOMIC_RELAXED)
#define ADD(p, v) __atomic_fetch_add((p), (v), __ATOMIC_RELAXED)

static inline void relax(void) {
#if defined(__i386__) || defined(__x86_64__)
  __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
  __asm__ volatile("yield");
#endif
}

typedef struct {
  int16_t dx, dy; /* 1/256 pixel, or dy FAR_MARK and dx the far (lfar) index */
  int32_t w;
} Cand;

typedef struct {
  uint32_t key, tag;
  Cand c[NC];
  uint32_t rpg; /* the repeat record (#228): generation and vertex last worked out */
  int16_t rpx, rpy, rpz, rp_pad;
} Ent;

/* LzRec: 16 bytes; ent in vcache.c's pad */
typedef struct {
  int16_t vx, vy, vz;
  uint16_t sz, q, next, row, ent;
} Rec;

typedef struct {
  int state, far;
  int32_t px, py, w; /* 1/256 pixel */
} Out;

#define ALIGN64 __attribute__((aligned(64)))
typedef struct {
  /* the frame's table */
  Ent s[NE];
  uint16_t log[NR];
  Cand lc[NR];
  int32_t far[NFAR][2], lfar[NLFAR][2];
  unsigned nfar ALIGN64, nlfar;
  uint32_t gen;
  /* the store (Lazy), the game thread's */
  Rec rec[NR];
  unsigned n;
  uint16_t head[NE];
  int32_t row[NROW][4];
  /* the helper's */
  uint16_t cl[NE], cov[NE];
  unsigned pub ALIGN64; /* records published (release) */
  unsigned final;       /* recording ended (release, after the last pub) */
  unsigned hdone ALIGN64; /* the helper's next record (relaxed: the game's backlog) */
  unsigned fseq ALIGN64;  /* frame + 1 while live (release) */
  unsigned stop ALIGN64, inside ALIGN64; /* the reuse handshake (seq_cst) */
  /* the test's */
  unsigned worked[NR] ALIGN64; /* per place: times worked out */
} Table;

typedef struct {
  int n, nrow;
  Rec r[NR];
  int32_t row[NROW][4];
} Frame;

typedef struct {
  uint32_t hist[NR]; /* the entry's hash after this place, single thread */
  uint32_t ent[NE];  /* each entry's at the frame's end */
  uint32_t place[NR];
  uint16_t touched[NE];
  unsigned ntouched;
  uint16_t look[2 * NE]; /* the walk's lookups */
  unsigned nlook;
} Ref;

typedef struct {
  unsigned long long worked, skipped, steals, waits, spins, spin_max, collects, segs, reads;
} Sink;

static int s_mutant;
static unsigned s_delay; /* the helper's spins before each projection (widens the windows) */
static int s_help_on;    /* this iteration's vc_helper */
static unsigned s_hb;    /* the backlog the game thread allows */
static unsigned s_pubn;  /* keeps between publishes */

static Table *s_tab[2], *s_rtab;
static Frame *s_frame[2];
static Ref *s_ref[2];

static unsigned s_begun ALIGN64; /* frames begun (release) */
static unsigned s_quit ALIGN64;
static unsigned s_hsleep ALIGN64;
static pthread_mutex_t s_hmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_hcv = PTHREAD_COND_INITIALIZER;
/* the walk's job: frame posted / joined */
static pthread_mutex_t s_wmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_wcv = PTHREAD_COND_INITIALIZER;
static int s_wposted = -1, s_wdone = -1, s_wquit;

static Sink s_sink[3]; /* game, walk, helper */
static unsigned long long s_fail_twice, s_fail_never, s_fail_ans, s_fail_read, s_fail_gen;
static int s_fail_shown;

#define FAIL(...)                                                                              \
  do {                                                                                         \
    if (s_fail_shown++ < 12)                                                                   \
      printf("FAIL " __VA_ARGS__);                                                             \
  } while (0)

/* ---- the maths and the commit (rtp_apply_in's shape) ---- */

static void maths(const int32_t *row, const Rec *z, Out *o) {
  const int64_t x = ((int64_t)row[0] * z->vx + (int64_t)row[1] * z->vy) / 4096 + row[2];
  const int64_t y = ((int64_t)row[1] * z->vx - (int64_t)row[0] * z->vy) / 4096 + (int)z->sz - 2048;
  const int64_t w = (int64_t)row[3] + z->vz;
  int64_t px, py;
  o->far = 0;
  o->px = o->py = 0;
  o->w = (int32_t)w;
  if (w <= 0) {
    o->state = E_INVALID;
    return;
  }
  o->state = E_VALID;
  px = x * 256 * 64 / w;
  py = y * 256 * 64 / w;
  if (px > 0x3FFFFFFF || px < -0x3FFFFFFF || py > 0x3FFFFFFF || py < -0x3FFFFFFF) {
    o->state = E_INVALID; /* (as a clamped key whose point is lost) */
    return;
  }
  o->px = (int32_t)px;
  o->py = (int32_t)py;
  o->far = px > 32767 || px < -32767 || py > 32767 || py < -32767;
}

static void cand_pos(const Table *t, const Cand *c, int32_t *x, int32_t *y) {
  if (c->dy == FAR_MARK) {
    *x = t->far[(uint16_t)c->dx][0];
    *y = t->far[(uint16_t)c->dx][1];
  } else {
    *x = c->dx;
    *y = c->dy;
  }
}

/* the far index (docs/vcache-core2.md, "The far index"): taken by any
 * thread, so atomic; NONE when the frame's array is full */
static unsigned far_take(unsigned *n, unsigned cap) {
  const unsigned i = ADD(n, 1u);
  return i < cap ? i : NONE;
}

static void log_answer(Table *t, int q, const Out *o) {
  Cand *const l = &t->lc[q];
  /* (only the entry's holder writes its places' flags) */
  ST(&t->log[q], (uint16_t)(LD(&t->log[q]) | LOG_LC));
  l->dx = l->dy = 0;
  l->w = 0;
  if (o->state != E_VALID)
    return;
  if (o->far) {
    const unsigned f = far_take(&t->nlfar, NLFAR);
    if (f == NONE)
      return; /* (as vcache.c: w 0 when lfar is full) */
    t->lfar[f][0] = o->px;
    t->lfar[f][1] = o->py;
    l->dx = (int16_t)f;
    l->dy = FAR_MARK;
  } else {
    l->dx = (int16_t)o->px;
    l->dy = (int16_t)o->py;
  }
  l->w = o->w;
}

static int add_cand(Table *t, Ent *s, const Out *o) {
  uint32_t tag = LD(&s->tag);
  const unsigned n = T_N(tag);
  unsigned j;
  for (j = 0; j < n; j++) {
    int32_t ax, ay, ddx, ddy;
    cand_pos(t, &s->c[j], &ax, &ay);
    ddx = ax - o->px;
    ddy = ay - o->py;
    if (ddx > 32 || ddx < -32 || ddy > 32 || ddy < -32) { /* more than 1/8 pixel: two points */
      tag |= T_MULTI;
      ST(&s->tag, tag);
    } else {
      const int64_t hi = o->w > s->c[j].w ? o->w : s->c[j].w;
      const int64_t d = (int64_t)o->w - s->c[j].w;
      if ((d < 0 ? -d : d) * 50 <= hi) /* the same depth (within 2%) */
        return 0;
    }
  }
  if (n == NC)
    return -1;
  if (o->far) {
    const unsigned f = far_take(&t->nfar, NFAR);
    if (f == NONE)
      return -1;
    t->far[f][0] = o->px;
    t->far[f][1] = o->py;
    s->c[n].dx = (int16_t)f;
    s->c[n].dy = FAR_MARK;
  } else {
    s->c[n].dx = (int16_t)o->px;
    s->c[n].dy = (int16_t)o->py;
  }
  s->c[n].w = o->w;
  ST(&s->tag, tag + (1u << 2));
  return 0;
}

/* rtp_apply_in's commit: what the projection does to its entry */
static void commit(Table *t, unsigned i, const Out *o, int q) {
  Ent *const s = &t->s[i];
  uint32_t tag = LD(&s->tag);
  const unsigned prev = T_STATE(tag);
  if (prev == E_AMB) { /* #181: only its answer */
    log_answer(t, q, o);
    return;
  }
  if (prev == 0) {
    ST(&s->tag, (tag & ~3u) | (unsigned)o->state);
    if (o->state == E_INVALID)
      return;
  } else if (prev != (unsigned)o->state || o->state != E_VALID) {
    if (!(prev == E_INVALID && o->state == E_INVALID))
      ST(&s->tag, (tag & ~3u) | E_AMB);
    log_answer(t, q, o);
    return;
  }
  if (add_cand(t, s, o))
    ST(&s->tag, (LD(&s->tag) & ~3u) | E_AMB);
  tag = LD(&s->tag);
  if (prev != 0 && (T_STATE(tag) != E_VALID || (tag & T_MULTI)))
    log_answer(t, q, o);
}

/* One projection worked out (rp_skip, then the maths and the commit). mask:
 * the tag bits that stop a repeat's skip: T_MULTI | T_PEND on the record's
 * path (as vcache.c's rp_skip), T_MULTI inside a flush, where the entry's
 * list is worked out in order (lz_flush clears T_PEND first; the helper
 * leaves it set, so it passes T_MULTI alone). */
static void work(Table *t, unsigned i, const Rec *z, uint32_t mask, Sink *k) {
  Ent *const s = &t->s[i];
  const uint32_t g = (uint32_t)z->row + 1;
  const uint32_t tag = LD(&s->tag);
  if (T_STATE(tag) == E_VALID && !(tag & mask) && s->rpg == g && s->rpx == z->vx &&
      s->rpy == z->vy && s->rpz == z->vz) {
    k->skipped++; /* an exact repeat: it changes nothing */
  } else {
    Out o;
    maths(t->row[z->row], z, &o);
    commit(t, i, &o, z->q);
    s->rpg = g;
    s->rpx = z->vx, s->rpy = z->vy, s->rpz = z->vz;
  }
  k->worked++;
  ADD(&t->worked[z->q], 1u);
}

static uint32_t fnv(uint32_t h, uint32_t v) {
  int b;
  for (b = 0; b < 4; b++)
    h = (h ^ ((v >> (8 * b)) & 0xFFu)) * 16777619u;
  return h;
}

/* an entry's answer: its state, count and flags, and its candidates by value */
static uint32_t fnv_entry(const Table *t, unsigned i) {
  const Ent *const s = &t->s[i];
  const uint32_t tag = LD(&s->tag);
  uint32_t h = fnv(2166136261u, tag & 0xFFu);
  unsigned j;
  for (j = 0; j < T_N(tag) && j < NC; j++) {
    int32_t x, y;
    cand_pos(t, &s->c[j], &x, &y);
    h = fnv(fnv(fnv(h, (uint32_t)x), (uint32_t)y), (uint32_t)s->c[j].w);
  }
  return h;
}

static uint32_t fnv_place(const Table *t, unsigned q) {
  const uint16_t v = LD(&t->log[q]);
  uint32_t h = fnv(2166136261u, v);
  if (v & LOG_LC) {
    const Cand *const l = &t->lc[q];
    int32_t x = l->dx, y = l->dy;
    if (l->dy == FAR_MARK) {
      x = t->lfar[(uint16_t)l->dx][0];
      y = t->lfar[(uint16_t)l->dx][1];
    }
    h = fnv(fnv(fnv(h, (uint32_t)x), (uint32_t)y), (uint32_t)l->w);
  }
  return h;
}

/* ---- the owner side: lz_need's slow path (docs: "The owner side") ---- */

static void wait_note(Sink *k, unsigned long long spins) {
  k->waits++;
  k->spins += spins;
  if (spins > k->spin_max)
    k->spin_max = spins;
}

/* lz_flush: the entry's records after cov, oldest first. The owner holds
 * the entry (OWN), so the helper reads none of it. */
static void flush(Table *t, unsigned e, Sink *k) {
  uint16_t idx[NR];
  unsigned m = 0, r = t->head[e];
  const unsigned cov = s_mutant == 3 ? 0 : LD(&t->cov[e]);
  ST(&t->s[e].tag, LD(&t->s[e].tag) & ~T_PEND);
  while (r > cov) {
    idx[m++] = (uint16_t)(r - 1);
    r = t->rec[r - 1].next;
  }
  while (m)
    work(t, e, &t->rec[idx[--m]], T_MULTI | T_PEND, k);
  ST(&t->cov[e], t->head[e]);
}

/* lz_need: entry e, read next by its owner (the game thread, or the walk
 * after the handover), its kept projections first */
static void collect(Table *t, unsigned e, Sink *k) {
  unsigned long long spins = 0;
  if (!(LD(&t->s[e].tag) & T_PEND)) /* the fast path, as today */
    return;
  k->collects++;
  for (;;) {
    uint16_t c = LD(&t->cl[e]);
    if (c == CL_H) { /* the helper holds it: wait (a few us, unless it is preempted) */
      if (s_mutant == 1) {
        flush(t, e, k);
        return;
      }
      if ((++spins & 63) == 0)
        sched_yield();
      else
        relax();
      continue;
    }
    if (c == CL_DONE) { /* the helper's: take it back, with its writes (acquire) */
      if (CAS(&t->cl[e], &c, CL_OWN, __ATOMIC_ACQUIRE)) {
        if (spins)
          wait_note(k, spins);
        flush(t, e, k); /* records kept after the helper's last (cov), if any */
        return;
      }
      continue;
    }
    if (c == CL_OWN) {
      flush(t, e, k);
      return;
    }
    /* FRESH or Q: not taken yet: the owner works them out itself (today's
     * path); the helper's CAS then fails and it passes them by */
    if (CAS(&t->cl[e], &c, CL_OWN, __ATOMIC_RELAXED)) {
      k->steals++;
      if (spins)
        wait_note(k, spins);
      flush(t, e, k);
      return;
    }
  }
}

/* ---- the game thread's record (rtp_point, lz_add) ---- */

/* lz_add, then the claim word: record n of entry e kept */
static void append(Table *t, unsigned r, unsigned e, const Rec *in) {
  const unsigned n = t->n;
  Rec *const z = &t->rec[n];
  const uint32_t tag = LD(&t->s[e].tag);
  *z = *in;
  z->q = (uint16_t)r;
  z->ent = (uint16_t)e;
  z->next = (uint16_t)(tag & T_PEND ? t->head[e] : 0);
  t->head[e] = (uint16_t)(n + 1);
  t->n = n + 1;
  if (!(tag & T_PEND)) {
    /* the entry's first kept one since it was last worked out: FRESH (none
     * kept yet) or OWN, and nobody else writes it in either */
    ST(&t->s[e].tag, tag | T_PEND);
    if (LD(&t->cl[e]) == CL_OWN)
      ST(&t->cl[e], (uint16_t)(CL_Q | n));
    return; /* (FRESH stays FRESH: its one kept record is this one) */
  }
  for (;;) {
    uint16_t c = LD(&t->cl[e]);
    if (c == CL_H || c == CL_DONE)
      return; /* the helper took the ones before; this one waits after cov */
    if (c == CL_OWN) {
      ST(&t->cl[e], (uint16_t)(CL_Q | n));
      return;
    }
    if (CAS(&t->cl[e], &c, (uint16_t)(CL_Q | n), __ATOMIC_RELAXED))
      return; /* FRESH or Q: offered up to this one */
  }
}

static void helper_wake(void) {
  if (LD(&s_hsleep)) {
    pthread_mutex_lock(&s_hmx);
    pthread_cond_signal(&s_hcv);
    pthread_mutex_unlock(&s_hmx);
  }
}

/* the reuse handshake (done_hook, before a table records again): the helper
 * leaves it first */
static void table_stop(Table *t) {
  unsigned long long spins = 0;
  STS(&t->stop, 1u);
  while (LDS(&t->inside)) {
    if ((++spins & 63) == 0)
      sched_yield();
    else
      relax();
  }
}

static void table_reset(Table *t, int f, const Frame *fr) {
  t->gen++;
  t->n = 0;
  ST(&t->nfar, 0u);
  ST(&t->nlfar, 0u);
  memset(t->cl, 0, sizeof t->cl); /* FRESH */
  memset(t->cov, 0, sizeof t->cov);
  memset(t->worked, 0, sizeof t->worked);
  memcpy(t->row, fr->row, sizeof(t->row[0]) * (size_t)fr->nrow);
  ST(&t->pub, 0u);
  ST(&t->final, 0u);
  ST(&t->hdone, 0u);
  STS(&t->stop, 0u);
  STR(&t->fseq, (unsigned)f + 1);
}

static void make_entry(Table *t, unsigned e, uint32_t key) {
  Ent *const s = &t->s[e];
  ST(&s->key, key);
  ST(&s->tag, t->gen << T_GEN_SHIFT);
  s->rpg = 0;
}

static uint16_t s_last[NE]; /* the game thread's: each entry's last place this frame */

static void record_frame(Table *t, const Frame *fr, const Ref *ref, uint32_t *rng, Sink *k) {
  int r;
  unsigned kept = 0;
  for (r = 0; r < fr->n; r++) {
    const Rec *const in = &fr->r[r];
    const unsigned e = in->ent;
    uint32_t tag;
    int keep;
    unsigned j;
    /* the probe (find_in) reads neighbours' tags, which the helper may be
     * writing (relaxed loads) */
    for (j = 1; j <= 2; j++) {
      const uint32_t w = LD(&t->s[(e + j) & (NE - 1)].tag);
      if (T_GEN(w) > t->gen) {
        s_fail_gen++;
        FAIL("frame gen %u: a probe read gen %u\n", t->gen, T_GEN(w));
      }
      (void)LD(&t->s[(e + j) & (NE - 1)].key);
    }
    if (T_GEN(LD(&t->s[e].tag)) != t->gen)
      make_entry(t, e, 0x10000u * e + 7);
    ST(&t->log[r], (uint16_t)e);
    tag = LD(&t->s[e].tag);
    keep = t->n < NR;
    if (keep) {
      if (!s_help_on)
        keep = s_hb != 0; /* vc_helper=0: today's rule (here: keep all, or none) */
      else
        keep = t->n - LD(&t->hdone) < s_hb || ((tag & T_PEND) && LD(&t->cl[e]) == CL_H);
    }
    if (keep) {
      append(t, (unsigned)r, e, in);
      if (++kept % s_pubn == 0) {
        STR(&t->pub, t->n);
        helper_wake();
      }
    } else { /* at once, as today: the entry's kept ones first */
      collect(t, e, k);
      work(t, e, in, T_MULTI | T_PEND, k);
    }
    s_last[e] = (uint16_t)r;
    /* a renderer's hand-over now and then reads an entry it projected
     * (hk_src: collect, then its answer) */
    *rng = *rng * 1103515245u + 12345u;
    if (((*rng >> 16) & 31) == 0) {
      const int back = (*rng >> 8) & 1 ? 0 : (int)((*rng >> 22) & 63);
      if (r >= back) {
        const unsigned e2 = fr->r[r - back].ent;
        collect(t, e2, k);
        k->reads++;
        if (fnv_entry(t, e2) != ref->hist[s_last[e2]]) {
          s_fail_read++;
          FAIL("frame gen %u: a hand-over read entry %u after place %u differently\n", t->gen,
               e2, (unsigned)s_last[e2]);
        }
      }
    }
  }
  STR(&t->pub, t->n);
  STR(&t->final, 1u);
  helper_wake();
}

/* ---- the helper (docs: "The helper") ---- */

/* entry e's records from top down to cov, oldest first, in place */
static void segment(Table *t, unsigned e, unsigned top, Sink *k) {
  uint16_t idx[NR];
  unsigned m = 0, r = top + 1;
  const unsigned cov = LD(&t->cov[e]);
  if (s_mutant == 2)
    ST(&t->s[e].tag, LD(&t->s[e].tag) & ~T_PEND);
  while (r > cov) {
    idx[m++] = (uint16_t)(r - 1);
    r = t->rec[r - 1].next;
  }
  while (m) {
    unsigned d;
    for (d = 0; d < s_delay; d++)
      relax();
    work(t, e, &t->rec[idx[--m]], T_MULTI, k);
  }
  ST(&t->cov[e], (uint16_t)(top + 1));
  k->segs++;
}

/* the published records not yet passed; 1 when the table is finished */
static int work_table(Table *t, Sink *k, int *moved) {
  const unsigned fin = LDA(&t->final);
  const unsigned p = LDA(&t->pub);
  unsigned h = LD(&t->hdone);
  *moved = 0;
  while (h < p) {
    const Rec *z;
    unsigned e, top = NONE;
    uint16_t c;
    if (LD(&t->stop))
      return 0;
    z = &t->rec[h];
    e = z->ent;
    c = LD(&t->cl[e]);
    if (c == CL_FRESH)
      top = h; /* its one kept record */
    else if ((c & CL_Q) && CL_IDX(c) < p)
      top = CL_IDX(c);
    else if (c == CL_DONE && h + 1 > LD(&t->cov[e]))
      top = h; /* kept after the helper's last pass over it */
    if (top != NONE && CAS(&t->cl[e], &c, CL_H, __ATOMIC_RELAXED)) {
      segment(t, e, top, k);
      STR(&t->cl[e], CL_DONE);
    }
    ST(&t->hdone, ++h);
    *moved = 1;
  }
  return fin && h == p;
}

static void helper_idle(void) {
  struct timespec ts;
  STS(&s_hsleep, 1u);
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_nsec += 200000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }
  pthread_mutex_lock(&s_hmx);
  if (!LD(&s_quit))
    pthread_cond_timedwait(&s_hcv, &s_hmx, &ts);
  pthread_mutex_unlock(&s_hmx);
  ST(&s_hsleep, 0u);
}

static void *helper_main(void *arg) {
  Sink *const k = &s_sink[2];
  unsigned hf = 0; /* the frame it serves: the oldest with work */
  (void)arg;
  while (!LD(&s_quit)) {
    const unsigned begun = LDA(&s_begun);
    Table *t;
    int fin, moved;
    if (!s_help_on || begun == 0) {
      helper_idle();
      continue;
    }
    if (hf + 2 < begun)
      hf = begun - 2; /* their tables went to newer frames */
    if (hf >= begun) {
      helper_idle();
      continue;
    }
    t = s_tab[hf & 1];
    STS(&t->inside, 1u);
    if (LDS(&t->stop) || LDA(&t->fseq) != hf + 1) { /* reused: the next */
      STR(&t->inside, 0u);
      hf++;
      continue;
    }
    fin = work_table(t, k, &moved);
    STR(&t->inside, 0u);
    if (fin)
      hf++;
    else if (!moved)
      helper_idle();
  }
  return NULL;
}

/* ---- the walk (gpu_walk.c's lookups: lz_need, then the answer) ---- */

static void *walk_main(void *arg) {
  Sink *const k = &s_sink[1];
  (void)arg;
  for (;;) {
    int f;
    unsigned j;
    Table *t;
    const Ref *ref;
    pthread_mutex_lock(&s_wmx);
    while (s_wposted == s_wdone && !s_wquit)
      pthread_cond_wait(&s_wcv, &s_wmx);
    if (s_wposted == s_wdone) {
      pthread_mutex_unlock(&s_wmx);
      return NULL;
    }
    f = s_wdone + 1;
    pthread_mutex_unlock(&s_wmx);
    t = s_tab[f & 1];
    ref = s_ref[f & 1];
    for (j = 0; j < ref->nlook; j++) {
      const unsigned e = ref->look[j];
      collect(t, e, k);
      k->reads++;
      if (fnv_entry(t, e) != ref->ent[e]) {
        s_fail_read++;
        FAIL("frame %d: the walk read entry %u differently\n", f, e);
      }
    }
    pthread_mutex_lock(&s_wmx);
    s_wdone = f;
    pthread_cond_broadcast(&s_wcv);
    pthread_mutex_unlock(&s_wmx);
  }
}

static void walk_post(int f) {
  pthread_mutex_lock(&s_wmx);
  s_wposted = f;
  pthread_cond_broadcast(&s_wcv);
  pthread_mutex_unlock(&s_wmx);
}

static void walk_join(int f) {
  pthread_mutex_lock(&s_wmx);
  while (s_wdone < f)
    pthread_cond_wait(&s_wcv, &s_wmx);
  pthread_mutex_unlock(&s_wmx);
}

/* ---- frames and their single-thread answers ---- */

static uint32_t rnd(uint32_t *s) {
  *s ^= *s << 13;
  *s ^= *s >> 17;
  *s ^= *s << 5;
  return *s;
}
static int rin(uint32_t *s, int lo, int hi) { return lo + (int)(rnd(s) % (uint32_t)(hi - lo + 1)); }

/* A frame shaped like Artisans' at 60 Hz (session 9: ~4,300 projections, a
 * sixth of them exact repeats, ~3,000 keys): sectors of 16-48 vertices under
 * their own registers, most vertices new, the rest under a recent key again,
 * half of those the same point again (a shared corner), half another point
 * on that key's pixel; a few behind the eye, a few far off. */
static void make_frame(Frame *fr, uint32_t *rng, int nrec) {
  static uint16_t pool[NE];
  unsigned np = 0, i;
  for (i = 0; i < NE; i++)
    pool[i] = (uint16_t)i;
  for (i = NE - 1; i > 0; i--) {
    const unsigned j = rnd(rng) % (i + 1);
    const uint16_t x = pool[i];
    pool[i] = pool[j];
    pool[j] = x;
  }
  fr->n = 0;
  fr->nrow = 0;
  while (fr->n < nrec && fr->nrow < (int)NROW) {
    const int row = fr->nrow++, nv = rin(rng, 16, 48);
    int v;
    fr->row[row][0] = rin(rng, -4096, 4096);
    fr->row[row][1] = rin(rng, -4096, 4096);
    fr->row[row][2] = rin(rng, -300, 300);
    fr->row[row][3] = rin(rng, 300, 3000);
    for (v = 0; v < nv && fr->n < nrec; v++) {
      Rec *const z = &fr->r[fr->n];
      const int u = rin(rng, 0, 99);
      memset(z, 0, sizeof *z);
      z->q = (uint16_t)fr->n;
      if (u < 38 && fr->n > 0) { /* a recent key again */
        const int back = rin(rng, 1, fr->n < 300 ? fr->n : 300);
        const Rec *const o = &fr->r[fr->n - back];
        z->ent = o->ent;
        if (u < 19) { /* the same point (an exact repeat) */
          z->vx = o->vx, z->vy = o->vy, z->vz = o->vz, z->sz = o->sz, z->row = o->row;
          fr->n++;
          continue;
        }
      } else {
        z->ent = pool[np++ % NE];
      }
      z->row = (uint16_t)row;
      z->vx = (int16_t)rin(rng, -3000, 3000);
      z->vy = (int16_t)rin(rng, -3000, 3000);
      z->sz = (uint16_t)rin(rng, 0, 4095);
      z->vz = (int16_t)(u >= 97 ? -fr->row[row][3] - rin(rng, 0, 50) /* behind the eye */
                                : rin(rng, -200, 3000));
      fr->n++;
    }
  }
}

/* every projection at once, in order: the answers to match */
static void reference(Table *t, const Frame *fr, Ref *ref, uint32_t *rng) {
  static Sink k;
  static uint8_t seen[NE];
  int r;
  unsigned i;
  t->gen++;
  ST(&t->nfar, 0u);
  ST(&t->nlfar, 0u);
  memcpy(t->row, fr->row, sizeof(t->row[0]) * (size_t)fr->nrow);
  memset(seen, 0, sizeof seen);
  ref->ntouched = 0;
  for (r = 0; r < fr->n; r++) {
    const unsigned e = fr->r[r].ent;
    if (T_GEN(LD(&t->s[e].tag)) != t->gen) {
      make_entry(t, e, 0x10000u * e + 7);
      ref->touched[ref->ntouched++] = (uint16_t)e;
    }
    ST(&t->log[r], (uint16_t)e);
    work(t, e, &fr->r[r], T_MULTI | T_PEND, &k);
    ref->hist[r] = fnv_entry(t, e);
  }
  for (i = 0; i < ref->ntouched; i++)
    ref->ent[ref->touched[i]] = fnv_entry(t, ref->touched[i]);
  for (r = 0; r < fr->n; r++)
    ref->place[r] = fnv_place(t, (unsigned)r);
  /* the walk's lookups: most keys (a quarter to a third of the kept ones
   * are never needed in the game; here an eighth), some twice, in its own
   * order */
  ref->nlook = 0;
  for (i = 0; i < ref->ntouched; i++) {
    const unsigned u = rnd(rng) % 100;
    if (u < 87)
      ref->look[ref->nlook++] = ref->touched[i];
    if (u < 30)
      ref->look[ref->nlook++] = ref->touched[i];
  }
  for (i = ref->nlook; i > 1; i--) {
    const unsigned j = rnd(rng) % i;
    const uint16_t x = ref->look[i - 1];
    ref->look[i - 1] = ref->look[j];
    ref->look[j] = x;
  }
  (void)seen;
}

/* at the table's reuse: the keys nobody looked up worked out now, then
 * every entry and place against the single thread's, every projection once */
static void verify(Table *t, const Frame *fr, const Ref *ref, int f) {
  unsigned i;
  int r;
  for (i = 0; i < ref->ntouched; i++) {
    const unsigned e = ref->touched[i];
    collect(t, e, &s_sink[0]);
    if (fnv_entry(t, e) != ref->ent[e]) {
      s_fail_ans++;
      FAIL("frame %d: entry %u differs from the single thread's\n", f, e);
    }
  }
  for (r = 0; r < fr->n; r++) {
    const unsigned w = t->worked[r];
    if (w == 0) {
      s_fail_never++;
      FAIL("frame %d: place %d never worked out\n", f, r);
    } else if (w > 1) {
      s_fail_twice++;
      FAIL("frame %d: place %d worked out %u times\n", f, r, w);
    }
    if (fnv_place(t, (unsigned)r) != ref->place[r]) {
      s_fail_ans++;
      FAIL("frame %d: place %d's answer differs from the single thread's\n", f, r);
    }
  }
}

static Table *table_new(void) {
  Table *const t = calloc(1, sizeof *t);
  if (!t) {
    printf("FAIL no memory\n");
    exit(2);
  }
  return t;
}

int main(int argc, char **argv) {
  int iters = 40, frames = 6, stress = 0, it, a;
  uint32_t seed = 282;
  unsigned long long nproj = 0;
  const char *m = getenv("VC_RACE_MUTANT");
  for (a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "--iters") && a + 1 < argc)
      iters = atoi(argv[++a]);
    else if (!strcmp(argv[a], "--frames") && a + 1 < argc)
      frames = atoi(argv[++a]);
    else if (!strcmp(argv[a], "--seed") && a + 1 < argc)
      seed = (uint32_t)strtoul(argv[++a], NULL, 0);
    else if (!strcmp(argv[a], "--stress"))
      stress = 1;
  }
  s_mutant = m ? atoi(m) : 0;
  s_tab[0] = table_new();
  s_tab[1] = table_new();
  s_rtab = table_new();
  s_frame[0] = calloc(1, sizeof(Frame));
  s_frame[1] = calloc(1, sizeof(Frame));
  s_ref[0] = calloc(1, sizeof(Ref));
  s_ref[1] = calloc(1, sizeof(Ref));
  if (!s_frame[0] || !s_frame[1] || !s_ref[0] || !s_ref[1]) {
    printf("FAIL no memory\n");
    return 2;
  }
  for (it = 0; it < iters; it++) {
    static const unsigned hbs[] = {0, 4, 64, 512, NR};
    static const unsigned pubs[] = {1, 7, 32, 128};
    static const unsigned delays[] = {0, 0, 40, 400};
    uint32_t rng = seed * 2654435761u + (uint32_t)it * 40503u + 1u;
    pthread_t th, tw;
    int f;
    rnd(&rng);
    s_help_on = stress || s_mutant || rnd(&rng) % 10 != 0;
    s_hb = stress || s_mutant ? NR : hbs[rnd(&rng) % 5];
    s_pubn = stress || s_mutant ? 1 + rnd(&rng) % 4 : pubs[rnd(&rng) % 4];
    s_delay = stress || s_mutant ? 200 + rnd(&rng) % 400 : delays[rnd(&rng) % 4];
    ST(&s_begun, 0u);
    ST(&s_quit, 0u);
    s_wposted = s_wdone = -1;
    s_wquit = 0;
    if (pthread_create(&th, NULL, helper_main, NULL) || pthread_create(&tw, NULL, walk_main, NULL)) {
      printf("FAIL no threads\n");
      return 2;
    }
    for (f = 0; f < frames; f++) {
      Table *const t = s_tab[f & 1];
      Frame *const fr = s_frame[f & 1];
      Ref *const ref = s_ref[f & 1];
      table_stop(t); /* the table of frame f - 2, whose walk was joined */
      if (f >= 2)
        verify(t, fr, ref, f - 2);
      make_frame(fr, &rng, rin(&rng, 800, 6000));
      reference(s_rtab, fr, ref, &rng);
      table_reset(t, f, fr);
      STR(&s_begun, (unsigned)f + 1);
      helper_wake();
      record_frame(t, fr, ref, &rng, &s_sink[0]);
      nproj += (unsigned)fr->n;
      if (f >= 1)
        walk_join(f - 1); /* DrawSync */
      walk_post(f);       /* the handover */
    }
    walk_join(frames - 1);
    for (f = frames - 2; f < frames; f++) {
      if (f < 0)
        continue;
      table_stop(s_tab[f & 1]);
      verify(s_tab[f & 1], s_frame[f & 1], s_ref[f & 1], f);
    }
    STS(&s_quit, 1u);
    pthread_mutex_lock(&s_hmx);
    pthread_cond_broadcast(&s_hcv);
    pthread_mutex_unlock(&s_hmx);
    pthread_mutex_lock(&s_wmx);
    s_wquit = 1;
    pthread_cond_broadcast(&s_wcv);
    pthread_mutex_unlock(&s_wmx);
    pthread_join(th, NULL);
    pthread_join(tw, NULL);
  }
  {
    const Sink *const g = &s_sink[0], *const w = &s_sink[1], *const h = &s_sink[2];
    const unsigned long long fails = s_fail_twice + s_fail_never + s_fail_ans + s_fail_read + s_fail_gen;
    printf("vcache helper race: %d iterations of %d frames, %llu projections; worked out by the "
           "game thread %llu, the helper %llu (%llu entries), the walk %llu; %llu entries taken "
           "before the helper; %llu waits for the helper (%llu spins, worst %llu); %llu repeats "
           "skipped; %llu reads checked as they happened%s (#282)\n",
           iters, frames, nproj, g->worked, h->worked, h->segs, w->worked, g->steals + w->steals,
           g->waits + w->waits, g->spins + w->spins,
           g->spin_max > w->spin_max ? g->spin_max : w->spin_max,
           g->skipped + w->skipped + h->skipped, g->reads + w->reads,
           s_mutant ? " (mutant)" : "");
    printf("vcache helper race: %llu worked out twice, %llu never, %llu answers different, %llu "
           "reads different, %llu torn generations\n",
           s_fail_twice, s_fail_never, s_fail_ans, s_fail_read, s_fail_gen);
    if (!fails && !s_mutant && h->worked == 0) {
      printf("FAIL the helper worked nothing out: the race wasn't run\n");
      return 1;
    }
    if (g->worked + w->worked + h->worked != nproj && !fails)
      printf("FAIL %llu projections worked out in all, %llu recorded\n",
             g->worked + w->worked + h->worked, nproj);
    printf("test_vcache_helper: %s\n", fails ? "FAILED" : "passed");
    return fails != 0;
  }
}

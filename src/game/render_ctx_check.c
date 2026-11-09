/* --render-ctx-check (stage 7d, decisions D123; render_ctx.h): each call of
 * a renderer in the game's draw (fast_render=2) also runs its presenter
 * build (NAME_pres) from the same inputs, first:
 *
 *  1. The game's state outside the arena is saved: the register file, the
 *     native build's scratchpad, the labels the presenter has copies of
 *     (s_labels), the primitive pointer, the record cursor and side table,
 *     LOD's state.
 *  2. The presenter's context gets the same inputs. Its memory is a mirror
 *     in the presenter's area (render_ctx_area: after the arena, so OT links
 *     reach it) of the regions the presenter will own in 7e: the primitive
 *     buffers and OTs, the moby array, the particle space. Pointers into
 *     those, or into a label the presenter has its own copy of, are moved
 *     onto the copy ("relocated") wherever the presenter's inputs hold them:
 *     the OTs, the labels, LOD's state. Pointers into the level's data
 *     aren't, so the presenter reads the game's level data as it will in 7e.
 *  3. The presenter build runs with the arena read-only (Linux mprotect):
 *     any write to the game's memory, even one put back before the call
 *     returns, traps. The trap counts it, saves the page as it was and lets
 *     the write through.
 *  4. The saved pages and state are compared with the game's now (nothing
 *     may have changed) and put back.
 *  5. The game's build runs, as the game goes on with it.
 *  6. The two are compared: the mirror against the arena, the presenter's
 *     labels, control registers, records, side table, sector bytes,
 *     animated sector copies and LOD state against the game's, word for
 *     word, where a word may differ only as a relocated pointer (or a 24-bit
 *     OT link to one) does.
 *
 * Since 7d2 (D124) the presenter build is fast_render=3's readable code: no
 * GTE data registers, no PS1 scratchpad, spill area or hand stack. So the
 * game's build (fast_render 2 or 3) and the presenter's are compared on the
 * control registers but FLAG; the presenter's working memory starts each
 * call filled with a pattern, and the data half of its GTERegisters (only a
 * view for the shared headers) must keep the pattern it is given. At
 * fast_render=2 this compares the register-file code with the readable code
 * on every call.
 *
 * Mismatches and traps go to the log ("render ctx:" lines, the first few
 * per renderer) and to render_ctx_check_report's counts, by kind. */
#if defined(__linux__)
#define _GNU_SOURCE 1 /* REG_EIP */
#endif
#include "render_ctx.h"

#include "lod.h"
#include "psx_arena.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "buffers.h"
#include "common.h"
#include "graphics.h"
#include "moby.h"
#include "environment.h"

#if defined(__linux__)
#include <signal.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <unistd.h>
#define RC_TRAP 1
#else
#define RC_TRAP 0
#endif

int g_render_ctx_check;
static void (*s_log)(const char *fmt, ...);
void render_ctx_check_set_log(void (*log)(const char *fmt, ...)) { s_log = log; }

/* the game's labels the presenter has its own of */
extern unsigned char g_render_work[1024]; /* fast_render=3's working memory (render_hand.h) */
extern char D_8006FCF4[], D_800771C8[], g_Particles[], g_Sparkles[];
extern char g_SpyroFlame[], g_Hud[];
#define L_SHADOWS ((char *)&g_MobyShadows)
/* the decomp's (graphics.h, buffers.h) as words */
#define L_NEXT ((char *)&D_800757B0)
#define L_END ((char *)&D_80075780)
#define L_FULL ((char *)&D_800758B0)
#define L_WORLD ((char *)&g_WorldOT)
#define L_HUD ((char *)&g_HudOT)
unsigned pres_lod_state_size(void);
void pres_lod_state_get(unsigned char *p);
void pres_lod_state_set(const unsigned char *p);
extern Moby *g_pres_level_mobys;

enum { MAX_NAMES = 16, SHOW = 6, MAX_MOBYS = 1024, PARTICLE_SPACE = 0x2000, OT_BYTES = 0x4000 };
/* the presenter's GTE data half and its working memory at each call (D124) */
enum { RC_GUARD = 0xA5, RC_WORK_FILL = 0xC3 };

static struct {
  const char *name;
  unsigned long calls, bad, traps, writes;
  unsigned shown, shown_traps;
  struct {
    const char *what;
    unsigned long n;
  } kinds[8]; /* the differences by what differed */
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

/* The labels the presenter has its own copy of (render_ctx_names.h), and
 * g_Hud, whose mobys the lit-moby list points into (7e's snapshot will
 * hold its copy; here the check's). Each is saved, given to the presenter
 * with its pointers relocated, compared, and checked unchanged. */
static char s_hud_copy[0x5B8] __attribute__((aligned(4)));
static struct {
  const char *what;
  char *game, *pres;
  u32 len;
  char *saved;
} s_labels[] = {
    {"work area", D_8006FCF4, g_pres_work, PRES_WORK, 0},
    {"sector flags", D_800771C8, g_pres_seen, PRES_SEEN, 0},
    {"shadow list", L_SHADOWS, g_pres_moby_shadows, PRES_SHADOWS, 0},
    {"particle list", g_Particles, g_pres_particles, PRES_PARTICLES, 0},
    {"glows", (char *)g_Glows, g_pres_glows, PRES_GLOWS, 0},
    {"sparkles", g_Sparkles, g_pres_sparkles, PRES_SPARKLES, 0},
    {"Spyro's flame", g_SpyroFlame, g_pres_spyro_flame, PRES_FLAME, 0},
    {"HUD", g_Hud, s_hud_copy, sizeof s_hud_copy, 0},
};
#define NLABELS ((int)(sizeof s_labels / sizeof s_labels[0]))

/* ---- the regions a pointer may be relocated between ---- */
typedef struct {
  u32 lo, len, to; /* [lo, lo + len] (the end too: a buffer's end pointer) -> to */
} Pair;
enum { MAX_PAIRS = 16 + PRES_SECTORS_MAX };
static Pair s_pairs[MAX_PAIRS];
static int s_npairs;
static u32 s_delta; /* the mirror's offset from the arena */
static u32 s_buf_lo, s_buf_hi; /* the primitive buffers and OTs, live */
static int s_mirror_pairs;     /* the first s_pairs: the mirror's regions (reloc) */
static int s_sec_pair0 = MAX_PAIRS; /* the sectors the presenter animated: s_pairs from here */

static void pair(const void *lo, u32 len, const void *to) {
  if (s_npairs < MAX_PAIRS && len) {
    s_pairs[s_npairs].lo = (u32)(uintptr_t)lo;
    s_pairs[s_npairs].len = len;
    s_pairs[s_npairs].to = (u32)(uintptr_t)to;
    s_npairs++;
  }
}

static int count_mobys(void) {
  int n = 0;
  if (!g_LevelMobys)
    return 0;
  while (n < MAX_MOBYS && (signed char)g_LevelMobys[n].m_State != -1)
    n++;
  return n;
}

static int in_arena_ptr(const void *p, u32 len) {
  uintptr_t a = (uintptr_t)p;
  return a >= g_psx_arena_base && len <= PSX_MEM_SIZE && a - g_psx_arena_base <= PSX_MEM_SIZE - len;
}

/* the level's sector headers (g_Environment), up to PRES_SEEN: only for
 * the environment's calls (other draws can run with no level loaded) */
static int s_env_call;
static int sector_count(void) {
  const int n = g_Environment.m_SectorCount;
  if (!s_env_call || n <= 0 || !in_arena_ptr(g_Environment.m_SectorPointer, (u32)(n < PRES_SEEN ? n : PRES_SEEN) * 4u))
    return 0;
  return n < PRES_SEEN ? n : PRES_SEEN;
}
static u32 sector_anims(int i) {
  const u32 sec = MIPS_KUSEG(((const u32 *)g_Environment.m_SectorPointer)[i]);
  return in_arena_ptr((const void *)(uintptr_t)(sec + 0x18u), 4) ? *(const u32 *)(uintptr_t)(sec + 0x18u) : 0;
}

/* The regions the mirror stands in for (moved by s_delta), then the
 * labels and buffers the presenter has its own of. */
static void set_pairs(void) {
  const uintptr_t m = render_ctx_area();
  s_npairs = 0;
  s_sec_pair0 = MAX_PAIRS;
  s_delta = (u32)(m - g_psx_arena_base);
  s_buf_lo = s_buf_hi = 0;
  if (in_arena_ptr(g_Buffers.m_LowerPolyBuffer, 1) && in_arena_ptr(g_Buffers.m_WorldOTStart, OT_BYTES)) {
    s_buf_lo = (u32)(uintptr_t)g_Buffers.m_LowerPolyBuffer;
    s_buf_hi = (u32)(uintptr_t)g_Buffers.m_WorldOTStart + OT_BYTES;
    pair((void *)(uintptr_t)s_buf_lo, s_buf_hi - s_buf_lo, (void *)(uintptr_t)(s_buf_lo + s_delta));
  }
  if (g_LevelMobys && in_arena_ptr(g_LevelMobys, 1)) {
    const u32 len = (u32)(count_mobys() + 1) * sizeof(Moby);
    pair(g_LevelMobys, len, (char *)g_LevelMobys + s_delta);
  }
  if (in_arena_ptr(g_Buffers.m_ParticleSpaceStart, PARTICLE_SPACE))
    pair(g_Buffers.m_ParticleSpaceStart, PARTICLE_SPACE, (char *)g_Buffers.m_ParticleSpaceStart + s_delta);
  s_mirror_pairs = s_npairs;
  /* the labels the presenter has its own of */
#if !SPYRO_NATIVE_MEM
  pair((void *)g_psx_arena_base, sizeof g_pres_scratch, g_pres_scratch);
#else
  pair(g_psx_scratch, sizeof g_pres_scratch, g_pres_scratch);
#endif
  pair(g_render_work, sizeof g_pres_scratch, g_pres_scratch); /* fast_render=3's (D124) */
  {
    int i;
    for (i = 0; i < NLABELS; i++)
      pair(s_labels[i].game, s_labels[i].len, s_labels[i].pres);
  }
}

/* A pointer of the game's as the presenter's: into the mirror, or into its
 * own copy of a label. */
static u32 reloc(u32 w) {
  int i;
  for (i = 0; i < s_npairs; i++)
    if (w - s_pairs[i].lo <= s_pairs[i].len)
      return w - s_pairs[i].lo + s_pairs[i].to;
  return w;
}


/* A word of the presenter's against the game's: the same, or the same
 * pointer relocated, or an OT link (a tag's low 24 bits) to one. */
static int same_word(u32 p, u32 g) {
  int i;
  if (p == g)
    return 1;
  for (i = 0; i < s_npairs; i++) {
    const Pair *q = &s_pairs[i];
    if (g - q->lo <= q->len && p == g - q->lo + q->to)
      return 1;
  }
  /* a subdivision list's sector header, the sector's address >> 2 (psx_mem.h
   * MEM_SUBDIV_HDR on a build whose arena is the PS1's KSEG0): the
   * presenter's names its copy of an animated sector */
  for (i = s_sec_pair0; i < s_npairs; i++) {
    const Pair *q = &s_pairs[i];
    if ((g << 2) - q->lo < q->len && p << 2 == (g << 2) - q->lo + q->to && (p ^ g) >> 30 == 0)
      return 1;
  }
  if ((p ^ g) >> 24 == 0 && s_buf_hi) {
    /* a 24-bit link into the buffers: decode both as the walk does */
    const u32 base = (u32)g_psx_arena_base;
    const u32 ga = base + (((g & 0xFFFFFFu) - base) & 0xFFFFFFu);
    const u32 pa = base + (((p & 0xFFFFFFu) - base) & 0xFFFFFFu);
    if (ga - s_buf_lo <= s_buf_hi - s_buf_lo && pa == ga + s_delta)
      return 1;
  }
  return 0;
}

static void bad(int k, const char *what, u32 at, u32 game, u32 pres) {
  int j;
  s_stats[k].bad++;
  for (j = 0; j < 8 && s_stats[k].kinds[j].what && s_stats[k].kinds[j].what != what; j++)
    ;
  if (j < 8) {
    s_stats[k].kinds[j].what = what;
    s_stats[k].kinds[j].n++;
  }
  if (s_log && s_stats[k].shown < SHOW) {
    s_stats[k].shown++;
    s_log("render ctx: %s call %lu: %s +%05x: game %08x presenter %08x", s_stats[k].name, s_stats[k].calls,
          what, at, game, pres);
  }
}

/* Words of the presenter's (p) against the game's (g), n bytes. */
static void cmp_words(int k, const char *what, const void *p, const void *g, u32 n) {
  const u32 *a = (const u32 *)p, *b = (const u32 *)g;
  u32 i;
  for (i = 0; i < n / 4; i++)
    if (a[i] != b[i] && !same_word(a[i], b[i]))
      bad(k, what, i * 4, b[i], a[i]);
}

/* ---- the trap ---- */
static volatile int s_trap_k = -1;
static volatile uintptr_t s_trap_lo, s_trap_hi;
/* the pages the presenter wrote, as they were before (restore_game puts
 * them back) */
enum { MAX_SAVED = 64, PAGE = 0x1000 };
static uintptr_t s_saved_at[MAX_SAVED];
static unsigned char s_saved[MAX_SAVED][PAGE];
static int s_nsaved, s_lost; /* s_lost: pages written past MAX_SAVED */
#if RC_TRAP
static struct sigaction s_old_segv;
static void on_segv(int sig, siginfo_t *si, void *uc) {
  const uintptr_t a = (uintptr_t)si->si_addr;
  if (s_trap_k >= 0 && a >= s_trap_lo && a < s_trap_hi) {
    const uintptr_t page = a & ~(uintptr_t)0xFFF;
    unsigned long pc = 0;
#if defined(__i386__)
    pc = (unsigned long)((ucontext_t *)uc)->uc_mcontext.gregs[REG_EIP];
#endif
    s_stats[s_trap_k].traps++;
    if (s_log && s_stats[s_trap_k].shown_traps < SHOW) {
      s_stats[s_trap_k].shown_traps++;
      s_log("render ctx: %s call %lu: the presenter wrote the game's memory at arena +%06x (pc %08lx)",
            s_stats[s_trap_k].name, s_stats[s_trap_k].calls, (unsigned)(a - g_psx_arena_base), pc);
    }
    if (s_nsaved < MAX_SAVED) {
      s_saved_at[s_nsaved] = page;
      memcpy(s_saved[s_nsaved++], (const void *)page, PAGE);
    } else {
      s_lost++;
    }
    mprotect((void *)page, PAGE, PROT_READ | PROT_WRITE);
    return;
  }
  sigaction(SIGSEGV, &s_old_segv, NULL);
  (void)sig;
}
static void trap_on(int k) {
  static int installed;
  if (!installed) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &s_old_segv);
    installed = 1;
  }
  s_trap_lo = g_psx_arena_base;
  s_trap_hi = g_psx_arena_base + PSX_MEM_SIZE;
  s_trap_k = k;
  s_nsaved = s_lost = 0;
  mprotect((void *)g_psx_arena_base, PSX_MEM_SIZE, PROT_READ);
}
static void trap_off(void) {
  mprotect((void *)g_psx_arena_base, PSX_MEM_SIZE, PROT_READ | PROT_WRITE);
  s_trap_k = -1;
}
#else
static void trap_on(int k) { (void)k; }
static void trap_off(void) {}
#endif

/* ---- the saved state ---- */
static unsigned char s_scr_before[1024];   /* g_psx_scratch (the native build's scratchpad) */
static GTERegisters s_gte_before;
static u32 s_next, s_full, s_sp, s_v1;
static NdCursor s_nd;
static NdSide s_side[ND_SIDE_N];
static unsigned s_fallback;
static unsigned char *s_lod, *s_lod_pres, *s_lod_game;
static unsigned s_misses;
static u32 *s_pool; /* the presenter's record pool */
static u32 s_pool_cap;

static void save_game(void) {
  memcpy(s_scr_before, g_psx_scratch, sizeof s_scr_before);
  s_gte_before = gteRegs;
  {
    int i;
    for (i = 0; i < NLABELS; i++)
      memcpy(s_labels[i].saved, s_labels[i].game, s_labels[i].len);
  }
  s_next = *(u32 *)L_NEXT;
  s_full = *(u32 *)L_FULL;
  s_sp = g_mips_sp;
  s_v1 = g_mips_v1;
  s_nd = g_nd;
  memcpy(s_side, g_nd_side, sizeof s_side);
  s_fallback = g_nd_fallback;
  port_lod_state_get(s_lod);
}

/* 1 if the presenter changed none of it; puts it back either way */
static int restore_game(int k) {
  int same = 1, i;
  /* the arena's pages the trap saved: each word changed is a write */
  for (i = 0; i < s_nsaved; i++) {
    const u32 *a = (const u32 *)s_saved_at[i], *b = (const u32 *)s_saved[i];
    u32 j;
    for (j = 0; j < PAGE / 4; j++)
      if (a[j] != b[j])
        s_stats[k].writes++, same = 0;
    memcpy((void *)s_saved_at[i], s_saved[i], PAGE);
  }
  if (s_lost && s_log)
    s_log("render ctx: %s call %lu: more than %d pages written, %d not put back", s_stats[k].name,
          s_stats[k].calls, MAX_SAVED, s_lost);
#define BACK(cond, what, put)                                                  \
  if (cond) {                                                                  \
    same = 0;                                                                  \
    bad(k, "game's " what " changed", 0, 0, 0);                                \
    put;                                                                       \
  }
  BACK(memcmp(s_scr_before, g_psx_scratch, sizeof s_scr_before), "scratchpad",
       memcpy(g_psx_scratch, s_scr_before, sizeof s_scr_before))
  BACK(memcmp(&s_gte_before, &gteRegs, sizeof gteRegs), "register file", gteRegs = s_gte_before)
  for (i = 0; i < NLABELS; i++)
    BACK(memcmp(s_labels[i].saved, s_labels[i].game, s_labels[i].len), "label",
         memcpy(s_labels[i].game, s_labels[i].saved, s_labels[i].len))
  BACK(s_next != *(u32 *)L_NEXT || s_full != *(u32 *)L_FULL, "primitive pointer",
       (*(u32 *)L_NEXT = s_next, *(u32 *)L_FULL = s_full))
  BACK(s_sp != g_mips_sp || s_v1 != g_mips_v1, "$sp", (g_mips_sp = s_sp, g_mips_v1 = s_v1))
  BACK(memcmp(&s_nd, &g_nd, sizeof g_nd) || s_fallback != g_nd_fallback, "record cursor",
       (g_nd = s_nd, g_nd_fallback = s_fallback))
  BACK(memcmp(s_side, g_nd_side, sizeof s_side), "side table", memcpy(g_nd_side, s_side, sizeof s_side))
  port_lod_state_get(s_lod_game);
  BACK(memcmp(s_lod, s_lod_game, port_lod_state_size()), "LOD state", port_lod_state_set(s_lod))
#undef BACK
  return same;
}

/* Whether the work area's word at off may be a pointer to relocate. The
 * moby records (moby_hand.c TBL_RECORDS, 0x38 bytes each, ended by one
 * whose flags are 0) hold flags, the position, the matrix and the specular
 * colour beside their four pointers, and any of those can look like an
 * address in a pair: a clipped record's flags, 0x80020000, are in the hand
 * stack's, which made a depth scale of 8 and an endless LinkBuckets. So in
 * the records only the pointers go through reloc. The rest of the area is
 * other renderers' tables at other times (the lit list at 0x2400, inside
 * the records' space, is moby pointers), all of it relocated as before. */
enum { REC_AT = 0x1600, REC_MAX = 0x2A00, REC_LEN = 0x38 };
static u32 s_rec_end; /* past the records' ending one, from REC_AT */
static void work_records(void) {
  u32 off = REC_AT;
  while (off + REC_LEN <= REC_MAX && *(const u32 *)(D_8006FCF4 + off))
    off += REC_LEN;
  s_rec_end = off + 4 <= REC_MAX ? off + 4 : REC_MAX;
}
static int work_ptr(u32 off) {
  if (off >= REC_AT && off < s_rec_end) {
    const u32 w = (off - REC_AT) % REC_LEN / 4;
    return w == 1 || w == 2 || w == 3 || w == 13; /* m_Anim, m_Frame, m_NextFrame, m_Moby */
  }
  return 1;
}

/* The presenter's context from the game's state now. */
static void set_presenter(void) {
  const uintptr_t m = render_ctx_area();
  u32 i;
  int l;
  const u32 world = *(u32 *)L_WORLD, hud = *(u32 *)L_HUD;
  /* the mirror: the regions the presenter reaches through relocated
   * pointers (it reads the level's data in the arena itself), its OTs'
   * pointers relocated */
  (void)m;
  for (i = 0; i < (u32)s_mirror_pairs; i++)
    memcpy((void *)(uintptr_t)s_pairs[i].to, (void *)(uintptr_t)s_pairs[i].lo, s_pairs[i].len);
  if (s_buf_hi) {
    u32 *ot = (u32 *)(uintptr_t)((u32)(uintptr_t)g_Buffers.m_HudOTStart + s_delta);
    for (i = 0; i < (8 + OT_BYTES) / 4; i++)
      ot[i] = reloc(ot[i]);
  }
  /* the control registers; the data half a guard, the working memory a
   * pattern (D124) */
  memset(&g_pres_gte, RC_GUARD, sizeof g_pres_gte);
  g_pres_gte.CP2C = gteRegs.CP2C;
  memset(g_pres_scratch, RC_WORK_FILL, sizeof g_pres_scratch);
  for (l = 0; l < NLABELS; l++) {
    u32 *to = (u32 *)s_labels[l].pres;
    const u32 *from = (const u32 *)s_labels[l].game;
    const int work = s_labels[l].game == D_8006FCF4;
    if (work)
      work_records();
    for (i = 0; i < s_labels[l].len / 4; i++)
      to[i] = work && !work_ptr(i * 4) ? from[i] : reloc(from[i]);
  }
  *(u32 *)g_pres_prim_next = reloc(*(u32 *)L_NEXT);
  /* the renderers' bound on the primitive pointer (draw.c: the pointer plus
   * 0x1C000, past the OTs in a level with the reduced buffers): moved with
   * the buffers whatever it is */
  *(u32 *)g_pres_prim_end = *(u32 *)L_END + (s_buf_hi ? s_delta : 0);
  *(u32 *)g_pres_prim_full = *(u32 *)L_FULL;
  *(u32 *)g_pres_world_ot = reloc(world);
  *(u32 *)g_pres_hud_ot = reloc(hud);
  g_pres_narrow = g_present_narrow;
  g_pres_rtp_hook = NULL;
  g_pres_rtp_value_hook = NULL;
  /* the record pool: the words written so far, at the same offsets */
  g_pres_nd = g_nd;
  if (g_nd.base) {
    const u32 used = (u32)(g_nd.wp - g_nd.base), cap = g_nd_pool[g_nd_cur].cap;
    if (cap > s_pool_cap) {
      free(s_pool);
      s_pool = malloc(cap * 4);
      s_pool_cap = s_pool ? cap : 0;
    }
    if (s_pool && used <= cap) {
      memcpy(s_pool, g_nd.base, used * 4);
      g_pres_nd.base = s_pool;
      g_pres_nd.wp = s_pool + used;
      g_pres_nd.lim = s_pool + (g_nd.lim - g_nd.base);
    }
  }
  memcpy(g_pres_nd_side, g_nd_side, sizeof g_pres_nd_side);
  g_pres_nd_fallback = g_nd_fallback;
  /* the sectors' animation bytes */
  for (l = 0; l < sector_count(); l++)
    g_pres_sec_anims[l] = sector_anims(l);
  s_misses = g_pres_sector_misses + g_pres_face_misses;
  /* LOD: its limits are primitive-buffer addresses and its pass-2 mark a
   * record's in the work area, so its words go through reloc too */
  memcpy(s_lod_pres, s_lod, port_lod_state_size());
  for (i = 0; i + 4 <= port_lod_state_size(); i += 4) {
    u32 w;
    memcpy(&w, s_lod_pres + i, 4);
    w = reloc(w);
    memcpy(s_lod_pres + i, &w, 4);
  }
  pres_lod_state_set(s_lod_pres);
  g_pres_level_mobys = g_LevelMobys ? (Moby *)(uintptr_t)reloc((u32)(uintptr_t)g_LevelMobys) : NULL;
}

static void compare(int k, u32 rp, u32 rg) {
  u32 i, n;
  int l;
  if (rp != rg && !same_word(rp, rg))
    bad(k, "result", 0, rg, rp);
  /* the control registers but FLAG (D124: a command's result, as the data
   * registers) */
  if (memcmp(&g_pres_gte.CP2C, &gteRegs.CP2C, 31 * 4))
    cmp_words(k, "control registers", &g_pres_gte.CP2C, &gteRegs.CP2C, 31 * 4);
  {
    /* the data half no presenter code may use */
    const unsigned char *g = (const unsigned char *)&g_pres_gte;
    for (i = 0; i < (u32)offsetof(GTERegisters, CP2C); i++)
      if (g[i] != RC_GUARD) {
        bad(k, "GTE data half used", i, RC_GUARD, g[i]);
        break;
      }
  }
  for (i = 0; i < (u32)NLABELS; i++)
    cmp_words(k, s_labels[i].what, s_labels[i].pres, s_labels[i].game, s_labels[i].len);
  if (!same_word(*(u32 *)g_pres_prim_next, *(u32 *)L_NEXT))
    bad(k, "primitive pointer", 0, *(u32 *)L_NEXT, *(u32 *)g_pres_prim_next);
  if (*(u32 *)g_pres_prim_full != *(u32 *)L_FULL)
    bad(k, "buffer full", 0, *(u32 *)L_FULL, *(u32 *)g_pres_prim_full);
  for (i = 0; i < (u32)s_mirror_pairs; i++) {
    const Pair *q = &s_pairs[i];
    const u32 *mp = (const u32 *)(uintptr_t)q->to, *ap = (const u32 *)(uintptr_t)q->lo, n4 = q->len / 4;
    u32 j;
    for (j = 0; j < n4; j++)
      if (mp[j] != ap[j] && !same_word(mp[j], ap[j]))
        bad(k, "memory", q->lo + j * 4 - (u32)g_psx_arena_base, ap[j], mp[j]);
  }
  /* the records */
  n = g_nd.base ? (u32)(g_nd.wp - g_nd.base) : 0;
  if (g_nd.base && g_pres_nd.base == s_pool) {
    if ((u32)(g_pres_nd.wp - g_pres_nd.base) != n)
      bad(k, "records written", 0, n, (u32)(g_pres_nd.wp - g_pres_nd.base));
    else
      cmp_words(k, "records", s_pool, g_nd.base, n * 4);
  }
  if (g_pres_nd.on != g_nd.on || g_pres_nd.key != g_nd.key || g_pres_nd.id != g_nd.id)
    bad(k, "record cursor", 0, g_nd.id, g_pres_nd.id);
  if (s_delta % 0x200000u == 0) /* the side table's keys are 21-bit offsets */
    cmp_words(k, "side table", g_pres_nd_side, g_nd_side, sizeof g_pres_nd_side);
  if (g_pres_nd_fallback != g_nd_fallback)
    bad(k, "records fallback", 0, g_nd_fallback, g_pres_nd_fallback);
  for (l = 0; l < sector_count(); l++)
    if (g_pres_sec_anims[l] != sector_anims(l))
      bad(k, "sector animations", (u32)l * 4u, sector_anims(l), g_pres_sec_anims[l]);
  /* each animated copy against the sector the game animated */
  for (l = 0; s_env_call && l < g_pres_nsectors; l++)
    cmp_words(k, "animated sector", (const void *)(uintptr_t)g_pres_sectors[l].copy,
              (const void *)(uintptr_t)g_pres_sectors[l].sec, g_pres_sectors[l].len & ~3u);
  if (g_pres_sector_misses + g_pres_face_misses != s_misses)
    bad(k, "sectors or faces with no room", 0, s_misses, g_pres_sector_misses + g_pres_face_misses);
  pres_lod_state_get(s_lod_pres);
  port_lod_state_get(s_lod_game);
  cmp_words(k, "LOD state", s_lod_pres, s_lod_game, port_lod_state_size() & ~3u);
}

static int s_in;

u32 render_ctx_check(const char *name, RenderCtxFn logic, RenderCtxFn pres, u32 a0, u32 a1, u32 a2, u32 a3) {
  const int k = stat_index(name);
  u32 rp, rg;
  if (s_in || !render_ctx_area())
    return logic(a0, a1, a2, a3);
  if (!s_lod) {
    const unsigned n = port_lod_state_size();
    int i;
    for (i = 0; i < NLABELS; i++)
      if (!(s_labels[i].saved = malloc(s_labels[i].len)))
        return logic(a0, a1, a2, a3);
    s_lod = malloc(n);
    s_lod_pres = malloc(n);
    s_lod_game = malloc(n);
    if (!s_lod || !s_lod_pres || !s_lod_game || n != pres_lod_state_size()) {
      if (s_log)
        s_log("render ctx: no memory for the check (or LOD's two copies differ); off");
      g_render_ctx_check = 0;
      return logic(a0, a1, a2, a3);
    }
  }
  s_in = 1;
  s_stats[k].calls++;
  s_env_call = pres == func_800258F0_pres;
  set_pairs();
  save_game();
  set_presenter();
  trap_on(k);
  rp = pres(a0, a1, a2, a3);
  trap_off();
  restore_game(k);
  rg = logic(a0, a1, a2, a3);
  /* the sectors the presenter animated: its copies stand for them */
  {
    int i;
    s_sec_pair0 = s_npairs;
    for (i = 0; i < g_pres_nsectors && s_env_call; i++)
      pair((void *)(uintptr_t)g_pres_sectors[i].sec, g_pres_sectors[i].len,
           (void *)(uintptr_t)g_pres_sectors[i].copy);
  }
  compare(k, rp, rg);
  s_in = 0;
  return rg;
}

int render_ctx_check_report(void) {
  int i, bad = 0;
  for (i = 0; i < MAX_NAMES && s_stats[i].name; i++) {
    if (s_log)
      s_log("render ctx: %s: %lu calls, %lu differences, %lu writes to the game's memory (%lu trapped)",
            s_stats[i].name, s_stats[i].calls, s_stats[i].bad, s_stats[i].writes, s_stats[i].traps);
    if (s_stats[i].bad || s_stats[i].writes || s_stats[i].traps)
      bad = 1;
    {
      int j;
      for (j = 0; j < 8 && s_stats[i].kinds[j].what && s_log; j++)
        s_log("render ctx:   %s: %lu", s_stats[i].kinds[j].what, s_stats[i].kinds[j].n);
    }
  }
  return bad;
}

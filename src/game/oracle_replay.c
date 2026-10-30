/* Function-level oracle replay (docs/porting-plan.md §7.3, layer 1).
 * tools/oracle/capture.lua saves the PS1 machine state when a translated
 * function is entered in PCSX-Redux (NAME_K.in) and when it returns
 * (NAME_K.out). For each pair, this loads the entry state into the arena and
 * the GTE, calls the function's translated C (m2c_index.c), and compares the
 * result with the emulator's: v0, v1, RAM from 0x80010000 up (except the
 * stack below the entry $sp, which is the callee's scratch), the scratchpad
 * and the GTE registers. Each sample runs in a child process, so one that
 * crashes is reported and the rest still run (SPYRO_REPLAY_NOFORK=1 runs
 * them in process instead, for gdb; SPYRO_REPLAY_DUMP=FILE writes the host's
 * RAM after each call to FILE, for offline diffs with --only). A sample
 * still running after SPYRO_REPLAY_TIMEOUT seconds (default 60) is stopped
 * by SIGALRM and reported as a crash with signal 14.
 *
 * Output: one line per sample, then the differing words of failures:
 *   NAME K vblank PASS|FAIL|CRASH v0 emu/host v1 emu/host ram N ignored N
 *     regsave N rebased N scratch N gte N
 * Differential mode, for code paths no capture reaches: with
 * SPYRO_REPLAY_SWEEP="ADDR=LIST" (a RAM word, hex; LIST like 0-44,50), each
 * sample of a partly hand-written function (m2c_gen_index) is run once per
 * value with that word set: first through its mips2c unit, the reference
 * (itself checked against the emulator), then through the hand C, compared
 * as above. Lines end in "sweep=VALUE"; CRASH-REF means the reference
 * crashed and nothing was compared. SPYRO_REPLAY_POKE="ADDR=VALUE,..." sets
 * more words first, fixed (VALUE "@SRC" copies the word at SRC, such as a
 * moby pointer from g_LevelMobys).
 * "rebased": the emulator stored a KUSEG address (below 2 MB) and the host
 * the same address on the arena, as mips2c rebases packed 21-bit pointers
 * (`sll 11; srl 10/11`, porting-plan §4.4).
 * RAM that isn't an output of the function (the asm's register spill area;
 * what interrupt handlers write while the call runs in the emulator) is
 * listed with reasons in an ignore file (tools/oracle/ignore_ranges.txt:
 * "START END NAME" in hex, END exclusive); differences there count as
 * "ignored", not as failures. Scratchpad addresses (0x1F80xxxx) are
 * compared in the port's form, the first KB of the arena. */
#include "mips2c_rt.h"
#include "psx_arena.h"
#include "psx_hooks.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define RAM_SIZE 0x200000u
#define SCRATCH_SIZE 1024u
#define HDR_WORDS 5 /* magic, version, address, sample, vblank */
#define REG_WORDS (34 + 32 + 32)
#define CMP_FROM 0x10000u /* below: PS1 kernel; the port keeps its scratchpad there */
#define MAX_SHOW 24

typedef struct {
  u32 hdr[HDR_WORDS];
  u32 gpr[34]; /* r0..ra, lo, hi */
  u32 cp2d[32], cp2c[32];
  u8 scratch[SCRATCH_SIZE];
  u8 ram[RAM_SIZE];
} Snap;

enum { R_V0 = 2, R_V1 = 3, R_A0 = 4, R_SP = 29 };

const u32 *g_m2c_seed; /* mips2c_rt.h */

static struct {
  u32 lo, hi;
} s_ignore[256];
static int s_nignore;
/* "v1 NAME": v1 at return is dead at every call site; "ret NAME": v0 and
 * v1 are (a void function). */
static struct {
  char name[64];
  int v0;
} s_dead[64];
static int s_ndead;

static int ret_dead(const char *name, int v0) {
  int i;
  for (i = 0; i < s_ndead; i++)
    if (strcmp(s_dead[i].name, name) == 0 && (!v0 || s_dead[i].v0))
      return 1;
  return 0;
}
#define v1_dead(name) ret_dead(name, 0)

/* "dma ADDR": the word at ADDR is a DMA destination that an interrupt
 * handler advances (libcd's sector pointer); when it moved during the call,
 * RAM from its old to its new value was written by DMA. s_dyn holds those
 * ranges for the sample being compared. */
static u32 s_dma[8];
static int s_ndma;
static struct {
  u32 lo, hi;
} s_dyn[16];
static int s_ndyn;

/* "cdread STATE READTIME MAXTIME": the game's CdState (cd.h) and read
 * timers. When the emulator has a read in flight as the call returns
 * (CDLoadAsync; m_IsReading at +40 still set), its destination (m_OutBuf at
 * +12, m_Size sectors at +4) isn't loaded yet there, while the host's libcd
 * finishes reads inside CdRead (D17). When one is in flight at entry, the
 * emulator's CD interrupt finishes it during the call (CDReadDone), the
 * replay's never. Either way that range, m_IsReading and the timers that
 * CDReadDone clears are not compared. */
static u32 s_cdread[3];

static void dyn(u32 lo, u32 hi) {
  if (s_ndyn < (int)(sizeof s_dyn / sizeof *s_dyn))
    s_dyn[s_ndyn].lo = lo, s_dyn[s_ndyn++].hi = hi;
}

static int ignored(u32 a) {
  int i;
  for (i = 0; i < s_nignore; i++)
    if (a >= s_ignore[i].lo && a < s_ignore[i].hi)
      return 1;
  for (i = 0; i < s_ndyn; i++)
    if (a >= s_dyn[i].lo && a < s_dyn[i].hi)
      return 1;
  return 0;
}

static void dma_ranges(const Snap *in, const Snap *out) {
  int i;
  s_ndyn = 0;
  for (i = 0; i < s_ndma; i++) {
    u32 off = s_dma[i] - 0x80000000u;
    u32 lo = *(const u32 *)(in->ram + off), hi = *(const u32 *)(out->ram + off);
    if (hi > lo && hi - lo <= 0x80000u) /* the credits load 0x60000 bytes at once */
      dyn(lo, hi);
  }
  if (s_cdread[0]) {
    const u8 *st = out->ram + (s_cdread[0] - 0x80000000u);
    const u8 *st_in = in->ram + (s_cdread[0] - 0x80000000u);
    if (*(const u32 *)(st_in + 40)) /* in flight at entry: the emulator's ends during the call */
      st = st_in;
    if (*(const u32 *)(st + 40)) {
      u32 buf = *(const u32 *)(st + 12), size = *(const u32 *)(st + 4) * 2048u;
      dyn(buf, buf + size);
      dyn(s_cdread[0] + 40, s_cdread[0] + 44);
      dyn(s_cdread[1], s_cdread[1] + 4);
      dyn(s_cdread[2], s_cdread[2] + 4);
    }
  }
}

static void load_ignore(const char *path) {
  FILE *f = fopen(path, "r");
  char line[256];
  if (!f) {
    fprintf(stderr, "replay: can't read %s\n", path);
    exit(1);
  }
  while (fgets(line, sizeof line, f) && s_nignore < 256) {
    unsigned lo, hi;
    if (line[0] == '#')
      continue;
    if (strncmp(line, "v1 ", 3) == 0 && s_ndead < 64)
      s_dead[s_ndead].v0 = 0, sscanf(line + 3, "%63s", s_dead[s_ndead++].name);
    else if (strncmp(line, "ret ", 4) == 0 && s_ndead < 64)
      s_dead[s_ndead].v0 = 1, sscanf(line + 4, "%63s", s_dead[s_ndead++].name);
    else if (strncmp(line, "dma ", 4) == 0 && s_ndma < 8)
      sscanf(line + 4, "%x", &s_dma[s_ndma++]);
    else if (strncmp(line, "cdread ", 7) == 0)
      sscanf(line + 7, "%x %x %x", &s_cdread[0], &s_cdread[1], &s_cdread[2]);
    else if (sscanf(line, "%x %x", &lo, &hi) == 2)
      s_ignore[s_nignore].lo = lo, s_ignore[s_nignore++].hi = hi;
  }
  fclose(f);
}

/* NAME_K.in, or NAME_K.in.gz through gzip. */
static FILE *open_snap(const char *path, int *piped) {
  char cmd[4200];
  FILE *f = fopen(path, "rb");
  *piped = 0;
  if (f)
    return f;
  snprintf(cmd, sizeof cmd, "%s.gz", path);
  if (access(cmd, R_OK) != 0)
    return NULL;
  snprintf(cmd, sizeof cmd, "gzip -dc '%s.gz'", path);
  *piped = 1;
  return popen(cmd, "r");
}

#define HEAD_SIZE ((HDR_WORDS + REG_WORDS) * 4u + SCRATCH_SIZE)

/* Entry snapshot (version 1): registers, scratchpad, all of RAM. Exit
 * snapshot (version 2, base = its entry): registers, scratchpad, and the RAM
 * words that changed. */
/* PS1 function address -> host function (tools/oracle/fnmap.py, --fnmap):
 * the emulator's RAM holds code pointers (moby update tables, overlay
 * pointers) as PS1 addresses; translated code calls them directly. */
typedef struct {
  u32 psx, host;
  char name[64];
} FnMap;
static FnMap *s_fn, *s_fn_level; /* all entries; those for the sample's level */
static int s_nfn, s_nfn_level;

#define MAX_TERM 64
static FnMap s_term[MAX_TERM]; /* overlay ends ("TERM" lines); host unused */
static int s_nterm;

static void load_fnmap(const char *path) {
  FILE *f = fopen(path, "r");
  char line[256];
  int cap = 0;
  if (!f) {
    fprintf(stderr, "replay: can't read %s\n", path);
    exit(1);
  }
  while (fgets(line, sizeof line, f)) {
    FnMap e;
    if (sscanf(line, "TERM %x %63s", &e.psx, e.name) == 2) {
      if (s_nterm < MAX_TERM)
        s_term[s_nterm++] = e;
      continue;
    }
    if (sscanf(line, "%x %x %63s", &e.psx, &e.host, e.name) != 3)
      continue;
    if (s_nfn == cap)
      s_fn = realloc(s_fn, (cap = cap ? cap * 2 : 1024) * sizeof *s_fn);
    s_fn[s_nfn++] = e;
  }
  fclose(f);
}

static int by_psx(const void *a, const void *b) {
  u32 x = ((const FnMap *)a)->psx, y = ((const FnMap *)b)->psx;
  return x < y ? -1 : x > y;
}

/* Overlay variants share addresses; keep those of the snapshot's level
 * (names ending in _LEVEL_<id>) and every non-overlay function. */
static void fnmap_for_level(int level) {
  char want[24];
  int i;
  snprintf(want, sizeof want, "_LEVEL_%d", level);
  s_fn_level = realloc(s_fn_level, (s_nfn ? s_nfn : 1) * sizeof *s_fn_level);
  s_nfn_level = 0;
  for (i = 0; i < s_nfn; i++) {
    const char *v = strstr(s_fn[i].name, "_LEVEL_");
    if (v && strcmp(v, want) != 0)
      continue;
    s_fn_level[s_nfn_level++] = s_fn[i];
  }
  qsort(s_fn_level, s_nfn_level, sizeof *s_fn_level, by_psx);
}

/* Where the loaded overlay ends ("TERM" lines of the fnmap): above it is the
 * level's data from disc (models, collision, sounds), where a word can equal
 * a function address or a scratchpad address by chance. Rewriting those
 * changed what a renderer read (func_8001F798 at Stone Hill,
 * logs/run14-oracle-tour), so pointer rewriting stops there. */
static u32 s_ptr_top = RAM_SIZE;

static u32 term(const char *name) {
  int i;
  for (i = 0; i < s_nterm; i++)
    if (strcmp(s_term[i].name, name) == 0)
      return s_term[i].psx - 0x80000000u;
  return 0;
}

static void ptr_top_for(int gamestate, int level) {
  char name[40];
  u32 t;
  if (gamestate == 13) /* GS_TitleScreen */
    snprintf(name, sizeof name, "Terminator_titlescreen_code");
  else if (gamestate == 15) /* GS_Credits */
    snprintf(name, sizeof name, "Terminator_credits");
  else
    snprintf(name, sizeof name, "Terminator_LEVEL_%d", level);
  t = term(name);
  if (!t)
    t = term("main_BSS_END");
  s_ptr_top = t ? t : RAM_SIZE;
}

static u32 ptr_words(u32 size) { return (size == RAM_SIZE ? s_ptr_top : size) / 4; }

static void fn_ptrs(u8 *p, u32 size) {
  u32 *w = (u32 *)p, i, n = ptr_words(size);
  if (!s_nfn_level)
    return;
  for (i = 0; i < n; i++) {
    FnMap key, *e;
    if (w[i] < 0x80010000u || w[i] >= 0x80200000u)
      continue;
    key.psx = w[i];
    e = bsearch(&key, s_fn_level, s_nfn_level, sizeof key, by_psx);
    if (e)
      w[i] = e->host;
  }
}

static int load(const char *path, Snap *s, const Snap *base) {
  int piped, ok;
  u32 n, pair[2];
  FILE *f = open_snap(path, &piped);
  if (!f)
    return -1;
  ok = fread(s, 1, HEAD_SIZE, f) == HEAD_SIZE && memcmp(s->hdr, "SPYF", 4) == 0 &&
       s->hdr[1] == (base ? 2u : 1u);
  if (ok && !base) {
    ok = fread(s->ram, 1, RAM_SIZE, f) == RAM_SIZE;
  } else if (ok) {
    memcpy(s->ram, base->ram, RAM_SIZE);
    ok = fread(&n, 4, 1, f) == 1;
    while (ok && n--) {
      ok = fread(pair, 4, 2, f) == 2 && pair[0] < RAM_SIZE;
      if (ok)
        *(u32 *)(s->ram + pair[0]) = pair[1];
    }
  }
  if (piped)
    pclose(f);
  else
    fclose(f);
  return ok ? 0 : -1;
}

/* Words holding scratchpad addresses: the port's scratchpad is the first KB
 * of the arena (port.c scratch_relocs, MIPS_SCRATCH_HI). */
static void scratch_ptrs(u8 *p, u32 size) {
  u32 *w = (u32 *)p, i, n = ptr_words(size);
  for (i = 0; i < n; i++)
    if (w[i] - 0x1F800000u < SCRATCH_SIZE)
      w[i] = (u32)g_psx_arena_base + (w[i] - 0x1F800000u);
}

/* GTE registers as MFC2/CFC2 return them, so emulator and host compare. */
static u32 sx16(u32 v) { return (u32)(s32)(s16)(u16)v; }
static u32 zx16(u32 v) { return v & 0xFFFFu; }
static u32 sat5(u32 ir) {
  s32 v = (s32)ir >> 7;
  return (u32)(v < 0 ? 0 : v > 31 ? 31 : v);
}
static u32 lzc(u32 v) {
  u32 n = 0;
  if ((s32)v < 0)
    v = ~v;
  while (n < 32 && !(v & 0x80000000u))
    v <<= 1, n++;
  return n;
}
static void gte_norm(u32 *d, u32 *c) {
  static const int d_s16[] = {1, 3, 5, 8, 9, 10, 11}, d_u16[] = {7, 16, 17, 18, 19};
  static const int c_s16[] = {4, 12, 20, 26, 27, 29, 30};
  unsigned i;
  for (i = 0; i < sizeof d_s16 / sizeof *d_s16; i++)
    d[d_s16[i]] = sx16(d[d_s16[i]]);
  for (i = 0; i < sizeof d_u16 / sizeof *d_u16; i++)
    d[d_u16[i]] = zx16(d[d_u16[i]]);
  d[15] = d[14];
  d[28] = d[29] = sat5(d[9]) | sat5(d[10]) << 5 | sat5(d[11]) << 10;
  d[31] = lzc(d[30]); /* LZCR always reads as LZCS's leading-bit count */
  for (i = 0; i < sizeof c_s16 / sizeof *c_s16; i++)
    c[c_s16[i]] = sx16(c[c_s16[i]]);
}

static void gte_load(const Snap *s) {
  int r;
  for (r = 0; r < 32; r++)
    CTC2(s->cp2c[r], r);
  for (r = 0; r < 32; r++)
    if (r != 15 && r != 28 && r != 29 && r != 31)
      MTC2(s->cp2d[r], r); /* 30 (LZCS) also sets 31 (LZCR) */
}

static const M2cEntry *find(u32 addr) {
  const M2cEntry *e;
  for (e = m2c_index; e->fn; e++)
    if (e->psx_addr == addr)
      return e;
  for (e = m2c_hand_index; e->fn; e++) /* hand-written C replacing a unit */
    if (e->psx_addr == addr)
      return e;
  return NULL;
}

/* A word the function saved a caller's register into (hand-written asm
 * saves s0-s7, gp, fp and ra to fixed RAM such as D_80077DD8, not the
 * stack): the emulator stored the caller's value, the host its own (the
 * translated function starts with them 0, and ra at MIPS_RA_SENTINEL). */
static int reg_save(const Snap *in, u32 emu, u32 host) {
  static const int saved[] = {16, 17, 18, 19, 20, 21, 22, 23, 28, 30, 31};
  unsigned i;
  for (i = 0; i < sizeof saved / sizeof *saved; i++)
    if (emu == in->gpr[saved[i]] && host == (saved[i] == 31 ? MIPS_RA_SENTINEL : 0u))
      return 1;
  return 0;
}

static const char *s_tag = ""; /* appended to each sample's line */

static const M2cEntry *find_gen(u32 addr) {
  const M2cEntry *e;
  for (e = m2c_gen_index; e->fn; e++)
    if (e->psx_addr == addr)
      return e;
  return NULL;
}

/* Load an entry snapshot into the arena and the GTE and call e. */
static u32 call_entry(const M2cEntry *e, Snap *in) {
  u8 *arena = (u8 *)g_psx_arena_base;
  memcpy(arena, in->ram, RAM_SIZE);
  memcpy(arena, in->scratch, SCRATCH_SIZE);
  ptr_top_for(*(s32 *)(in->ram + (0x800757D8u - 0x80000000u)),   /* g_Gamestate */
              *(s32 *)(in->ram + (0x8007596Cu - 0x80000000u))); /* g_LevelId */
  scratch_ptrs(arena, RAM_SIZE);
  fnmap_for_level(*(s32 *)(in->ram + (0x8007596Cu - 0x80000000u))); /* g_LevelId */
  fn_ptrs(arena, RAM_SIZE);
  gte_load(in);
  g_mips_sp = in->gpr[R_SP];
  g_m2c_seed = in->gpr; /* the whole entry register file, not just a0-a3 */
  return e->fn(in->gpr[R_A0], in->gpr[R_A0 + 1], in->gpr[R_A0 + 2], in->gpr[R_A0 + 3]);
}

/* Child: run one sample, print its line, exit 0 (pass) or 1 (fail). */
static int run_one(const char *name, int k, Snap *in, Snap *out) {
  u8 *arena = (u8 *)g_psx_arena_base;
  const M2cEntry *e = find(in->hdr[2]);
  u32 v0, v1, sp = in->gpr[R_SP], d[32], c[32], ed[32], ec[32];
  u32 i, nram = 0, nirq = 0, nscr = 0, ngte = 0, nsave = 0, nrebased = 0, shown = 0;
  char diffs[MAX_SHOW * 64];
  int len = 0;
  if (!e) {
    printf("%s %d %u SKIP no translated entry at %08x\n", name, k, in->hdr[4], in->hdr[2]);
    return 1;
  }
  v0 = call_entry(e, in);
  v1 = g_mips_v1;
  if (getenv("SPYRO_REPLAY_DUMP")) { /* the host's RAM after the call, for offline diffs */
    FILE *df = fopen(getenv("SPYRO_REPLAY_DUMP"), "wb");
    if (df) {
      fwrite(arena, 1, RAM_SIZE, df);
      fclose(df);
    }
  }

  /* Expected state, in the host's terms. */
  dma_ranges(in, out);
  scratch_ptrs(out->ram, RAM_SIZE);
  scratch_ptrs(out->scratch, SCRATCH_SIZE);
  scratch_ptrs(in->ram, RAM_SIZE);
  fn_ptrs(out->ram, RAM_SIZE);
  fn_ptrs(in->ram, RAM_SIZE);
  for (i = CMP_FROM; i < RAM_SIZE; i += 4) {
    u32 a = 0x80000000u + i, h = *(u32 *)(arena + i), x = *(u32 *)(out->ram + i);
    if (a < sp && a >= sp - 0x10000u)
      continue; /* callee frames */
    if (h == x)
      continue;
    if (ignored(a)) {
      nirq++;
      continue;
    }
    if (reg_save(in, x, h)) {
      nsave++;
      continue;
    }
    if (x < RAM_SIZE && h == x + (u32)g_psx_arena_base) {
      nrebased++; /* mips2c rebases packed 21-bit pointers on the arena */
      continue;
    }
    nram++;
    if (shown < MAX_SHOW && len < (int)sizeof diffs - 64)
      shown++, len += snprintf(diffs + len, sizeof diffs - len, "  ram %08x emu %08x host %08x\n", a, x, h);
  }
  for (i = 0; i < SCRATCH_SIZE; i += 4) {
    u32 h = *(u32 *)(arena + i), x = *(u32 *)(out->scratch + i);
    if (h != x && reg_save(in, x, h)) {
      nsave++;
    } else if (h != x) {
      nscr++;
      if (shown < MAX_SHOW && len < (int)sizeof diffs - 64)
        shown++, len += snprintf(diffs + len, sizeof diffs - len, "  scratch %03x emu %08x host %08x\n", i, x, h);
    }
  }
  for (i = 0; i < 32; i++) {
    d[i] = MFC2(i);
    c[i] = CFC2(i);
  }
  memcpy(ed, out->cp2d, sizeof ed);
  memcpy(ec, out->cp2c, sizeof ec);
  gte_norm(d, c);
  gte_norm(ed, ec);
  for (i = 0; i < 64; i++) {
    u32 h = i < 32 ? d[i] : c[i - 32], x = i < 32 ? ed[i] : ec[i - 32];
    if (h != x) {
      ngte++;
      if (shown < MAX_SHOW && len < (int)sizeof diffs - 64)
        shown++, len += snprintf(diffs + len, sizeof diffs - len, "  gte %s%u emu %08x host %08x\n",
                                 i < 32 ? "d" : "c", i & 31, x, h);
    }
  }
  {
    u32 ev0 = out->gpr[R_V0], ev1 = out->gpr[R_V1];
    scratch_ptrs((u8 *)&ev0, 4);
    scratch_ptrs((u8 *)&ev1, 4);
    /* v0/v1 count only if the emulator's function changed them: a function
     * that returns nothing leaves the caller's values there, which the
     * host's translated C doesn't have. */
    int v0_ok = v0 == ev0 || out->gpr[R_V0] == in->gpr[R_V0] || ret_dead(name, 1);
    int v1_ok = v1 == ev1 || out->gpr[R_V1] == in->gpr[R_V1] || v1_dead(name);
    int pass = v0_ok && v1_ok && !nram && !nscr && !ngte;
    printf("%s %d %u %s v0 %08x/%08x%s v1 %08x/%08x%s ram %u ignored %u regsave %u rebased %u scratch %u "
           "gte %u%s\n%.*s",
           name, k, in->hdr[4], pass ? "PASS" : "FAIL", out->gpr[R_V0], v0,
           out->gpr[R_V0] == in->gpr[R_V0] ? "(unset)" : ret_dead(name, 1) ? "(dead)" : "", out->gpr[R_V1], v1,
           out->gpr[R_V1] == in->gpr[R_V1] ? "(unset)" : v1_dead(name) ? "(dead)" : "", nram, nirq, nsave,
           nrebased, nscr, ngte, s_tag,
           pass ? 0 : len, diffs);
    return !pass;
  }
}

/* Child, differential mode: the reference's exit state becomes the
 * expected .out (v0/v1 left at their entry values, so not compared). */
static int run_sweep(const char *name, int k, Snap *in, Snap *ref, const M2cEntry *gen) {
  u8 *arena = (u8 *)g_psx_arena_base;
  int i;
  memcpy(ref->hdr, in->hdr, sizeof ref->hdr);
  call_entry(gen, in);
  memcpy(ref->ram, arena, RAM_SIZE);
  memcpy(ref->scratch, arena, SCRATCH_SIZE);
  memcpy(ref->gpr, in->gpr, sizeof ref->gpr);
  for (i = 0; i < 32; i++) {
    ref->cp2d[i] = MFC2(i);
    ref->cp2c[i] = CFC2(i);
  }
  return run_one(name, k, in, ref);
}

/* SPYRO_REPLAY_SWEEP="ADDR=LIST" -> address and up to 256 values. */
static int parse_sweep(const char *spec, u32 *addr, s32 *vals) {
  int n = 0;
  char *end;
  *addr = strtoul(spec, &end, 16);
  if (*end++ != '=')
    return 0;
  while (*end && n < 256) {
    long lo = strtol(end, &end, 0), hi = lo;
    if (*end == '-')
      hi = strtol(end + 1, &end, 0);
    for (; lo <= hi && n < 256; lo++)
      vals[n++] = (s32)lo;
    if (*end == ',')
      end++;
    else
      break;
  }
  return n;
}

static unsigned s_timeout = 60; /* SPYRO_REPLAY_TIMEOUT, seconds per sample */

/* Fork and run one sample (sweep: the reference first); returns 1 if it
 * didn't pass. */
static int run_forked(const char *name, int k, Snap *in, Snap *out, const M2cEntry *gen, const char *tag) {
  int st;
  pid_t pid;
  fflush(stdout);
  pid = fork();
  if (pid == 0) {
    int r_;
    alarm(s_timeout); /* a call that never returns is reported, not waited for */
    r_ = gen ? run_sweep(name, k, in, out, gen) : run_one(name, k, in, out);
    fflush(stdout);
    _exit(r_);
  }
  waitpid(pid, &st, 0);
  if (!WIFEXITED(st)) {
    printf("%s %d %u CRASH signal %d%s\n", name, k, in->hdr[4], WIFSIGNALED(st) ? WTERMSIG(st) : -1, tag);
    return 1;
  }
  fflush(stdout);
  return WEXITSTATUS(st) != 0;
}

static int by_name(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Replay every NAME_K.in in dir that has a .out (only: a name filter or
 * NULL; ignore: the ignore file or NULL; fnmap: the PS1-to-host function
 * map or NULL). Returns the number of samples that didn't pass. */
void psx_overlays_init(void); /* gen/ovl_table.c */

int spyro_replay(const char *dir, const char *only, const char *ignore, const char *fnmap) {
  s_timeout = getenv("SPYRO_REPLAY_TIMEOUT") ? (unsigned)atoi(getenv("SPYRO_REPLAY_TIMEOUT")) : 60;
  /* Host globals the game's start-up sets and the samples don't carry: the
   * overlay Terminator pointers (D20), the credits overlay's buffer base. */
  psx_overlays_init();
  DIR *dp = opendir(dir);
  struct dirent *de;
  char **names = NULL, path[4096];
  size_t n = 0, cap = 0, i;
  int bad = 0, total = 0, nsweep = 0;
  Snap *in = malloc(sizeof *in), *out = malloc(sizeof *out);
  const char *sweep = getenv("SPYRO_REPLAY_SWEEP");
  u32 sweep_addr = 0;
  static s32 sweep_vals[256];
  if (sweep && (nsweep = parse_sweep(sweep, &sweep_addr, sweep_vals)) == 0) {
    fprintf(stderr, "replay: bad SPYRO_REPLAY_SWEEP %s\n", sweep);
    return -1;
  }
  if (ignore)
    load_ignore(ignore);
  if (fnmap)
    load_fnmap(fnmap);
  if (!dp || !in || !out) {
    fprintf(stderr, "replay: can't open %s\n", dir);
    return -1;
  }
  while ((de = readdir(dp))) {
    size_t l = strlen(de->d_name);
    if (l > 6 && strcmp(de->d_name + l - 6, ".in.gz") == 0)
      l -= 3;
    else if (l < 4 || strcmp(de->d_name + l - 3, ".in") != 0)
      continue;
    if (n == cap)
      names = realloc(names, (cap = cap ? cap * 2 : 256) * sizeof *names);
    names[n++] = strndup(de->d_name, l);
  }
  closedir(dp);
  qsort(names, n, sizeof *names, by_name);
  for (i = 0; i < n; i++) {
    char name[256];
    int k;
    char *us = strrchr(names[i], '_');
    if (!us || (size_t)(us - names[i]) >= sizeof name)
      continue;
    memcpy(name, names[i], us - names[i]);
    name[us - names[i]] = 0;
    k = atoi(us + 1);
    if (only && strcmp(only, name) != 0)
      continue;
    snprintf(path, sizeof path, "%s/%s_%d.in", dir, name, k);
    if (load(path, in, NULL) != 0) {
      printf("%s %d BAD %s\n", name, k, path);
      continue;
    }
    snprintf(path, sizeof path, "%s/%s_%d.out", dir, name, k);
    if (load(path, out, in) != 0) {
      printf("%s %d %u NOEXIT (no .out: the call never returned before the capture ended)\n", name, k,
             in->hdr[4]);
      continue;
    }
    if (sweep && getenv("SPYRO_REPLAY_POKE")) {
      const char *p = getenv("SPYRO_REPLAY_POKE");
      while (*p) {
        char *end;
        u32 a = strtoul(p, &end, 16) - 0x80000000u, v;
        if (*end++ != '=' || a >= RAM_SIZE)
          break;
        if (*end == '@')
          v = *(u32 *)(in->ram + ((strtoul(end + 1, &end, 16) - 0x80000000u) & (RAM_SIZE - 4)));
        else
          v = strtoul(end, &end, 0);
        *(u32 *)(in->ram + a) = v;
        p = *end == ',' ? end + 1 : end + strlen(end);
      }
    }
    if (sweep) {
      const M2cEntry *gen = find_gen(in->hdr[2]);
      u32 off = sweep_addr - 0x80000000u, keep;
      int v;
      if (!gen || off >= RAM_SIZE)
        continue;
      keep = *(u32 *)(in->ram + off);
      for (v = 0; v < nsweep; v++) {
        char tag[32];
        snprintf(tag, sizeof tag, " sweep=%d", sweep_vals[v]);
        *(s32 *)(in->ram + off) = sweep_vals[v];
        total++;
        /* A reference that crashes on this input compares nothing. */
        {
          int st;
          pid_t pid = fork();
          if (pid == 0) {
            call_entry(gen, in);
            _exit(0);
          }
          waitpid(pid, &st, 0);
          if (!WIFEXITED(st)) {
            printf("%s %d %u CRASH-REF%s\n", name, k, in->hdr[4], tag);
            total--;
            continue;
          }
        }
        s_tag = tag;
        bad += run_forked(name, k, in, out, gen, tag);
      }
      *(u32 *)(in->ram + off) = keep;
      continue;
    }
    total++;
    fflush(stdout);
    if (getenv("SPYRO_REPLAY_NOFORK")) { /* debugging under gdb: one sample, in process */
      alarm(s_timeout); /* gdb stops at the SIGALRM of a call that never returns */
      bad += run_one(name, k, in, out);
      alarm(0);
      continue;
    }
    bad += run_forked(name, k, in, out, NULL, "");
  }
  printf("replay: %d samples, %d passed, %d not\n", total, total - bad, bad);
  return bad;
}

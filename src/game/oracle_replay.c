/* Function-level oracle replay (docs/porting-plan.md §7.3, layer 1).
 * tools/oracle/capture.lua saves the PS1 machine state when a translated
 * function is entered in PCSX-Redux (NAME_K.in) and when it returns
 * (NAME_K.out). For each pair, this loads the entry state into the arena and
 * the GTE, calls the function's translated C (m2c_index.c), and compares the
 * result with the emulator's: v0, v1, RAM from 0x80010000 up (except the
 * stack below the entry $sp, which is the callee's scratch), the scratchpad
 * and the GTE registers. Each sample runs in a child process, so one that
 * crashes is reported and the rest still run.
 *
 * Output: one line per sample, then the differing words of failures:
 *   NAME K vblank PASS|FAIL|CRASH v0 emu/host v1 emu/host ram N ignored N
 *     regsave N rebased N scratch N gte N
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

static struct {
  u32 lo, hi;
} s_ignore[256];
static int s_nignore;
static char s_dead_v1[64][64]; /* "v1 NAME": v1 at return is dead at every call site */
static int s_ndead_v1;

static int v1_dead(const char *name) {
  int i;
  for (i = 0; i < s_ndead_v1; i++)
    if (strcmp(s_dead_v1[i], name) == 0)
      return 1;
  return 0;
}

static int ignored(u32 a) {
  int i;
  for (i = 0; i < s_nignore; i++)
    if (a >= s_ignore[i].lo && a < s_ignore[i].hi)
      return 1;
  return 0;
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
    if (strncmp(line, "v1 ", 3) == 0 && s_ndead_v1 < 64)
      sscanf(line + 3, "%63s", s_dead_v1[s_ndead_v1++]);
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
  u32 *w = (u32 *)p, i;
  for (i = 0; i < size / 4; i++)
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
  memcpy(arena, in->ram, RAM_SIZE);
  memcpy(arena, in->scratch, SCRATCH_SIZE);
  scratch_ptrs(arena, RAM_SIZE);
  gte_load(in);
  g_mips_sp = sp;
  v0 = e->fn(in->gpr[R_A0], in->gpr[R_A0 + 1], in->gpr[R_A0 + 2], in->gpr[R_A0 + 3]);
  v1 = g_mips_v1;

  /* Expected state, in the host's terms. */
  scratch_ptrs(out->ram, RAM_SIZE);
  scratch_ptrs(out->scratch, SCRATCH_SIZE);
  scratch_ptrs(in->ram, RAM_SIZE);
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
    int v0_ok = v0 == ev0 || out->gpr[R_V0] == in->gpr[R_V0];
    int v1_ok = v1 == ev1 || out->gpr[R_V1] == in->gpr[R_V1] || v1_dead(name);
    int pass = v0_ok && v1_ok && !nram && !nscr && !ngte;
    printf("%s %d %u %s v0 %08x/%08x%s v1 %08x/%08x%s ram %u ignored %u regsave %u rebased %u scratch %u "
           "gte %u\n%.*s",
           name, k, in->hdr[4], pass ? "PASS" : "FAIL", out->gpr[R_V0], v0,
           out->gpr[R_V0] == in->gpr[R_V0] ? "(unset)" : "", out->gpr[R_V1], v1,
           out->gpr[R_V1] == in->gpr[R_V1] ? "(unset)" : v1_dead(name) ? "(dead)" : "", nram, nirq, nsave,
           nrebased, nscr, ngte,
           pass ? 0 : len, diffs);
    return !pass;
  }
}

static int by_name(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Replay every NAME_K.in in dir that has a .out (only: a name filter or
 * NULL; ignore: the interrupt ranges file or NULL). Returns the number of
 * samples that didn't pass. */
int spyro_replay(const char *dir, const char *only, const char *ignore) {
  DIR *dp = opendir(dir);
  struct dirent *de;
  char **names = NULL, path[4096];
  size_t n = 0, cap = 0, i;
  int bad = 0, total = 0;
  Snap *in = malloc(sizeof *in), *out = malloc(sizeof *out);
  if (ignore)
    load_ignore(ignore);
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
    int k, st;
    pid_t pid;
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
    total++;
    fflush(stdout);
    pid = fork();
    if (pid == 0)
      { int r_ = run_one(name, k, in, out); fflush(stdout); _exit(r_); }
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st)) {
      printf("%s %d %u CRASH signal %d\n", name, k, in->hdr[4], WIFSIGNALED(st) ? WTERMSIG(st) : -1);
      bad++;
    } else if (WEXITSTATUS(st) != 0) {
      bad++;
    }
    fflush(stdout);
  }
  printf("replay: %d samples, %d passed, %d not\n", total, total - bad, bad);
  return bad;
}

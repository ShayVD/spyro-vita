/* Replays function snapshots from PCSX-Redux (tools/oracle/oracle.lua,
 * ORACLE_TRACE) against the translated code (tools/mips2c), docs/porting-plan
 * §7.3: load the entry state (registers, GTE, scratchpad, captured RAM pages,
 * captured globals), call the translated entry point, and compare with the
 * return state.
 * usage: test_m2c_diff <dir with trace-XXXXXXXX.bin> [-v]
 * Only the captured pages of the arena are mapped during a call; a function
 * that touches anything else faults and the case counts as "incomplete"
 * (capture too narrow), not as a mismatch. Exit 77 when there are no traces. */
#include "mips2c_rt.h"
#include "psx_arena.h"

#include <gtereg.h>

#include <dirent.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include M2C_DIFF_ENTRIES

#define MAGIC 0x32504E53u
#define PAGE 0x1000u

typedef struct {
  u32 kind, seq, vblank;
  u32 gpr[35]; /* r0..ra, lo, hi, pc */
  u32 gte[64];
  const u8 *scratch;
  u32 nglobals;
  const u8 *globals; /* addr, size, bytes ... */
  u32 npages;
  const u8 *pages; /* addr, 4096 bytes ... */
} Snap;

static u32 rd32(const u8 **p) {
  u32 v;
  memcpy(&v, *p, 4);
  *p += 4;
  return v;
}

/* Parses one record at *p; returns 0 at the end or on a bad record. */
static int parse(const u8 **p, const u8 *end, Snap *s) {
  if (end - *p < 16 + 99 * 4 + 1024 + 8 || rd32(p) != MAGIC)
    return 0;
  s->kind = rd32(p);
  s->seq = rd32(p);
  s->vblank = rd32(p);
  for (int i = 0; i < 35; i++)
    s->gpr[i] = rd32(p);
  for (int i = 0; i < 64; i++)
    s->gte[i] = rd32(p);
  s->scratch = *p;
  *p += 1024;
  s->nglobals = rd32(p);
  s->globals = *p;
  for (u32 i = 0; i < s->nglobals; i++) {
    u32 size;
    if (end - *p < 8)
      return 0;
    memcpy(&size, *p + 4, 4);
    *p += 8 + size;
    if (*p > end - 4)
      return 0;
  }
  s->npages = rd32(p);
  s->pages = *p;
  *p += s->npages * (4 + PAGE);
  return *p <= end;
}

/* The unit's data symbols (k_symbols, from psx.map): a captured global is
 * copied into its host symbol, and a PS1 pointer into one is the host
 * symbol's address in the port (the real callers pass host addresses). */
static char *global_host(u32 addr, u32 size) {
  for (unsigned i = 0; i < sizeof k_symbols / sizeof k_symbols[0]; i++)
    if (k_symbols[i].addr == addr && size <= k_symbols[i].size)
      return k_symbols[i].host;
  return NULL;
}

static u32 to_host(u32 v) {
  for (unsigned i = 0; i < sizeof k_symbols / sizeof k_symbols[0]; i++)
    if (v - k_symbols[i].addr < k_symbols[i].size)
      return (u32)(uintptr_t)k_symbols[i].host + (v - k_symbols[i].addr);
  return v;
}

/* GTE register as MFC2/CFC2 read it (as in tests/gte). */
static u32 clamp5(s32 ir) {
  ir >>= 7;
  return ir < 0 ? 0 : ir > 0x1F ? 0x1F : (u32)ir;
}
static u32 gte_view(const u32 *r, int i) {
  switch (i) {
  case 1: case 3: case 5: case 8: case 9: case 10: case 11:
    return (u32)(s32)(s16)r[i];
  case 7: case 16: case 17: case 18: case 19:
    return r[i] & 0xFFFF;
  case 15:
    return r[14];
  case 28: case 29:
    return clamp5((s16)r[9]) | clamp5((s16)r[10]) << 5 | clamp5((s16)r[11]) << 10;
  case 31: {
    u32 v = r[30], n = 0;
    if (v & 0x80000000u)
      v = ~v;
    while (n < 32 && !(v & 0x80000000u)) {
      v <<= 1;
      n++;
    }
    return n;
  }
  case 32 + 4: case 32 + 12: case 32 + 20: case 32 + 26: case 32 + 27:
  case 32 + 29: case 32 + 30:
    return (u32)(s32)(s16)r[i];
  }
  return r[i];
}

static sigjmp_buf g_fault_jmp;
static volatile int g_in_call;
static volatile uintptr_t g_fault_addr;

static void on_segv(int sig, siginfo_t *si, void *uc) {
  (void)uc;
  if (!g_in_call) {
    signal(sig, SIG_DFL);
    raise(sig);
    return;
  }
  g_fault_addr = (uintptr_t)si->si_addr;
  siglongjmp(g_fault_jmp, 1);
}

static void protect_pages(const Snap *s, int prot) {
  const u8 *p = s->pages;
  for (u32 i = 0; i < s->npages; i++, p += 4 + PAGE) {
    u32 a;
    memcpy(&a, p, 4);
    mprotect(PSX_ARENA(a), PAGE, prot);
  }
}

/* With the PS1 layout (decisions D23) the game's globals live in the arena
 * at their PS1 addresses, so the captured globals' pages need mapping too. */
static void protect_globals(const Snap *s, int prot) {
  const u8 *p = s->globals;
  for (u32 i = 0; i < s->nglobals; i++) {
    u32 a, size;
    memcpy(&a, p, 4);
    memcpy(&size, p + 4, 4);
    uintptr_t h = (uintptr_t)global_host(a, size);
    if (h >= g_psx_arena_base + PAGE && h < g_psx_arena_base + PSX_RAM_SIZE) {
      uintptr_t lo = h & ~(uintptr_t)(PAGE - 1);
      mprotect((void *)lo, (h + size - lo + PAGE - 1) & ~(uintptr_t)(PAGE - 1), prot);
    }
    p += 8 + size;
  }
}

static int verbose;
static unsigned g_mismatch_shown;

#define REPORT(...)                                                            \
  do {                                                                         \
    if (verbose && g_mismatch_shown < 40)                                      \
      printf(__VA_ARGS__);                                                     \
  } while (0)

/* 0 pass, 1 mismatch, 2 incomplete capture. */
static int run_case(const char *name, u32 (*fn)(u32, u32, u32, u32),
                    const Snap *e, const Snap *r) {
  const u8 *p;
  /* Entry state. */
  protect_pages(e, PROT_READ | PROT_WRITE);
  protect_globals(e, PROT_READ | PROT_WRITE);
  p = e->pages;
  for (u32 i = 0; i < e->npages; i++, p += 4 + PAGE) {
    u32 a;
    memcpy(&a, p, 4);
    memcpy(PSX_ARENA(a), p + 4, PAGE);
  }
  memcpy((void *)g_psx_arena_base, e->scratch, 1024);
  p = e->globals;
  for (u32 i = 0; i < e->nglobals; i++) {
    u32 a, size;
    memcpy(&a, p, 4);
    memcpy(&size, p + 4, 4);
    char *h = global_host(a, size);
    if (!h) {
      printf("test_m2c_diff: no host symbol for global %08X+%X\n", a, size);
      exit(2);
    }
    memcpy(h, p + 8, size);
    p += 8 + size;
  }
  memcpy(gteRegs.CP2D.r, e->gte, 32 * 4);
  memcpy(gteRegs.CP2C.r, e->gte + 32, 32 * 4);
  g_mips_sp = e->gpr[29];
  g_mips_v1 = e->gpr[3];

  int result = 0;
  u32 v0 = 0;
  if (sigsetjmp(g_fault_jmp, 1)) {
    g_in_call = 0;
    REPORT("  %s #%u: touched %08X outside the captured pages\n", name, e->seq,
           (u32)g_fault_addr);
    result = 2;
    goto out;
  }
  g_in_call = 1;
  v0 = fn(to_host(e->gpr[4]), to_host(e->gpr[5]), to_host(e->gpr[6]),
         to_host(e->gpr[7]));
  g_in_call = 0;

  /* v0/v1: only where the original changed them (void functions leave the
   * caller's value, the translation returns its own); a pointer to a data
   * symbol is compared as the host address. */
  if (r->gpr[2] != e->gpr[2] && v0 != to_host(r->gpr[2])) {
    REPORT("  %s #%u vblank %u: v0 want %08X got %08X\n", name, e->seq,
           e->vblank, r->gpr[2], v0);
    result = 1;
  }
  if (r->gpr[3] != e->gpr[3] && g_mips_v1 != to_host(r->gpr[3])) {
    REPORT("  %s #%u: v1 want %08X got %08X\n", name, e->seq, r->gpr[3],
           g_mips_v1);
    result = 1;
  }
  u32 got[64];
  memcpy(got, gteRegs.CP2D.r, 32 * 4);
  memcpy(got + 32, gteRegs.CP2C.r, 32 * 4);
  for (int i = 0; i < 64; i++)
    if (gte_view(got, i) != gte_view(r->gte, i)) {
      REPORT("  %s #%u: GTE %s%d want %08X got %08X\n", name, e->seq,
             i < 32 ? "d" : "c", i & 31, gte_view(r->gte, i), gte_view(got, i));
      result = 1;
    }
  for (int i = 0; i < 1024; i++)
    if (((const u8 *)g_psx_arena_base)[i] != r->scratch[i]) {
      REPORT("  %s #%u: scratchpad +%03X want %02X got %02X\n", name, e->seq,
             i, r->scratch[i], ((const u8 *)g_psx_arena_base)[i]);
      result = 1;
      break;
    }
  p = r->globals;
  for (u32 i = 0; i < r->nglobals; i++) {
    u32 a, size;
    memcpy(&a, p, 4);
    memcpy(&size, p + 4, 4);
    const char *h = global_host(a, size);
    for (u32 k = 0; k < size; k++)
      if ((u8)h[k] != p[8 + k]) {
        REPORT("  %s #%u: global %08X+%X want %02X got %02X\n", name, e->seq,
               a, k, p[8 + k], (u8)h[k]);
        result = 1;
        break;
      }
    p += 8 + size;
  }
  p = r->pages;
  for (u32 i = 0; i < r->npages; i++, p += 4 + PAGE) {
    u32 a;
    memcpy(&a, p, 4);
    const u8 *h = PSX_ARENA(a);
    for (u32 k = 0; k < PAGE; k++) {
      u32 x = a + k;
      /* Bytes of a data symbol live in the host symbol in the port (compared
       * above when captured); the 4 KB below the entry $sp is the callee's
       * dead frame. */
      if (h[k] == p[4 + k] || to_host(x) != x ||
          x - (e->gpr[29] - PAGE) < PAGE)
        continue;
      REPORT("  %s #%u: RAM %08X want %02X got %02X\n", name, e->seq, x,
             p[4 + k], h[k]);
      result = 1;
      break;
    }
  }
out:
  protect_pages(e, PROT_NONE);
  protect_globals(e, PROT_NONE);
  if (result == 1)
    g_mismatch_shown++;
  return result;
}

static u8 *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  u8 *d = malloc(*size ? *size : 1);
  if (fread(d, 1, *size, f) != *size) {
    free(d);
    d = NULL;
  }
  fclose(f);
  return d;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: test_m2c_diff <trace dir> [-v]\n");
    return 2;
  }
  verbose = argc > 2 && strcmp(argv[2], "-v") == 0;
  DIR *dir = opendir(argv[1]);
  if (!dir) {
    printf("test_m2c_diff: no traces at %s (skipped)\n", argv[1]);
    return 77;
  }
  psx_arena_init();
  /* Everything but the scratchpad page is unmapped until a case maps its
   * captured pages. */
  mprotect((void *)(g_psx_arena_base + PAGE), PSX_RAM_SIZE - PAGE, PROT_NONE);
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_segv;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);

  unsigned tot = 0, bad = 0, inc = 0, files = 0;
  struct dirent *de;
  char path[4096];
  while ((de = readdir(dir))) {
    u32 addr;
    if (sscanf(de->d_name, "trace-%8x.bin", &addr) != 1)
      continue;
    const char *name = NULL;
    u32 (*fn)(u32, u32, u32, u32) = NULL;
    for (unsigned i = 0; i < sizeof k_entries / sizeof k_entries[0]; i++)
      if (k_entries[i].addr == addr) {
        name = k_entries[i].name;
        fn = k_entries[i].fn;
      }
    if (!fn)
      continue;
    snprintf(path, sizeof path, "%s/%s", argv[1], de->d_name);
    size_t size;
    u8 *d = read_file(path, &size);
    if (!d || !size) {
      free(d);
      continue;
    }
    files++;
    /* Entries by seq; a return pairs with the entry of the same seq. */
    Snap *snaps = calloc(size / 1024 + 1, sizeof *snaps);
    unsigned n = 0, pass = 0, mis = 0, incomplete = 0;
    const u8 *p = d, *end = d + size;
    while (parse(&p, end, &snaps[n]))
      n++;
    for (unsigned i = 0; i < n; i++) {
      if (snaps[i].kind != 1)
        continue;
      int first = 1; /* only the first return per seq counts */
      for (unsigned j = 0; j < i; j++)
        if (snaps[j].kind == 1 && snaps[j].seq == snaps[i].seq)
          first = 0;
      for (unsigned j = 0; first && j < i; j++)
        if (snaps[j].kind == 0 && snaps[j].seq == snaps[i].seq) {
          int rc = run_case(name, fn, &snaps[j], &snaps[i]);
          pass += rc == 0;
          mis += rc == 1;
          incomplete += rc == 2;
          break;
        }
    }
    printf("%-24s %08X %3u cases: %3u pass, %3u mismatch, %3u incomplete\n",
           name, addr, pass + mis + incomplete, pass, mis, incomplete);
    tot += pass + mis + incomplete;
    bad += mis;
    inc += incomplete;
    free(snaps);
    free(d);
  }
  closedir(dir);
  if (!files) {
    printf("test_m2c_diff: no traces at %s (skipped)\n", argv[1]);
    return 77;
  }
  printf("test_m2c_diff: %u functions, %u cases, %u mismatched, %u incomplete\n",
         files, tot, bad, inc);
  return bad ? 1 : 0;
}

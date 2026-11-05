/* Stage 7a's read audit (--present-reads FILE, D104; debug only): which PS1
 * RAM the in-between pass reads, and which of that the game changes before
 * the next tick's draw. A presenter drawing the in-between frame on another
 * core (docs/native-plan.md stage 7, 7d-7f) would run while the logic pass
 * and the next update do, so that overlap is what its snapshot must hold.
 *
 * Every Nth in-between pass (--present-reads-every N, default 30) runs with
 * the arena's pages protected (mprotect PROT_NONE), but for the emulated
 * stack, which is the pass's own. A page's first access faults: a read
 * leaves it read-only (a later write faults once more), a write opens it.
 * The data pages (0x8006B000-0x8007B000, the game's globals) are traced
 * word for word: each access faults, is recorded from si_addr and the
 * fault's write bit (REG_ERR), and runs with the trap flag set, so the page
 * is protected again after the one instruction. The arena as it was at the
 * draw's start is kept; at the next draw's start the words that changed are
 * compared against what the pass read. One line per range to FILE:
 *   sample V GS LEVEL
 *   F LO HI REGION changed|stable   data words the pass read before writing
 *   P LO HI REGION changed|stable   other pages it read before writing
 *   C LO HI REGION                  words changed in those pages
 *   W LO HI REGION                  pages it wrote first (its own output)
 * Addresses are PS1 addresses; tools/present_reads.py names them. Nothing
 * here changes what the game computes; runs are slower. */
#define _GNU_SOURCE
#include "host.h"
#include "present.h"
#include "psx_arena.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>

#define PAGE 4096
#define STACK_P0 (0x10000 / PAGE) /* mips2c's emulated stack, 0x80010000-0x80020000 */
#define STACK_P1 (0x20000 / PAGE)
#define FINE_P0 (0x6B000 / PAGE) /* the data pages, word for word */
#define FINE_P1 (0x7B000 / PAGE)
#define FINE_WORDS ((FINE_P1 - FINE_P0) * PAGE / 4)
#define TF 0x100

enum { P_READ = 1, P_WRITE = 2 }; /* a page's or a word's first access */

static FILE *s_out;
static int s_every = 30, s_count, s_armed, s_tracing, s_pending;
static int s_vblank, s_gs, s_level;
static unsigned char s_first[PRESENT_PAGES];
static unsigned char s_fine[FINE_WORDS]; /* P_READ / P_WRITE, first access first */
static unsigned char *s_copy;
static int s_step[8], s_nstep;

static unsigned char *page_at(int i) { return (unsigned char *)g_psx_arena_base + (size_t)i * PAGE; }

static void on_segv(int sig, siginfo_t *si, void *ctx) {
  ucontext_t *uc = ctx;
  uintptr_t a = (uintptr_t)si->si_addr;
  int write = (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0, i;
  (void)sig;
  if (!s_tracing || a < g_psx_arena_base || a - g_psx_arena_base >= PSX_RAM_SIZE) {
    signal(SIGSEGV, SIG_DFL); /* a real crash */
    return;
  }
  i = (int)((a - g_psx_arena_base) / PAGE);
  if (!s_first[i])
    s_first[i] = write ? P_WRITE : P_READ;
  if (i >= FINE_P0 && i < FINE_P1) {
    unsigned char *w = &s_fine[(a - g_psx_arena_base - FINE_P0 * PAGE) / 4];
    if (!*w)
      *w = write ? P_WRITE : P_READ;
    mprotect(page_at(i), PAGE, PROT_READ | PROT_WRITE);
    if (s_nstep < 8)
      s_step[s_nstep++] = i;
    uc->uc_mcontext.gregs[REG_EFL] |= TF;
    return;
  }
  mprotect(page_at(i), PAGE, write ? PROT_READ | PROT_WRITE : PROT_READ);
}

static void on_trap(int sig, siginfo_t *si, void *ctx) {
  ucontext_t *uc = ctx;
  (void)sig;
  (void)si;
  while (s_nstep)
    mprotect(page_at(s_step[--s_nstep]), PAGE, PROT_NONE);
  uc->uc_mcontext.gregs[REG_EFL] &= ~TF;
}

static void protect(int on) {
  int i;
  for (i = 0; i < PRESENT_PAGES; i++)
    if (i < STACK_P0 || i >= STACK_P1)
      mprotect(page_at(i), PAGE, on ? PROT_NONE : PROT_READ | PROT_WRITE);
}

/* One line per run of words in [w0, w1) for which want(word) holds, split
 * where the region changes; the region is looked at every `step` words (a
 * page for the page lines). */
static void runs(char kind, uint32_t w0, uint32_t w1, int (*want)(uint32_t), const char *tail,
                 uint32_t step) {
  uint32_t w = w0, s;
  const char *r;
  while (w < w1) {
    if (!want(w)) {
      w++;
      continue;
    }
    r = port_present_region(0x80000000u + w * 4);
    for (s = w++; w < w1 && want(w); w++)
      if (w % step == 0 && strcmp(port_present_region(0x80000000u + w * 4), r) != 0)
        break;
    fprintf(s_out, "%c %08x %08x %s%s\n", kind, 0x80000000u + s * 4, 0x80000000u + w * 4, r,
            tail);
  }
}

static int changed(uint32_t w) {
  return ((uint32_t *)g_psx_arena_base)[w] != ((uint32_t *)s_copy)[w];
}
static int fine_read(uint32_t w) { return s_fine[w - FINE_P0 * PAGE / 4] == P_READ; }
static int fine_changed(uint32_t w) { return fine_read(w) && changed(w); }
static int fine_stable(uint32_t w) { return fine_read(w) && !changed(w); }
static int page_read(uint32_t w) {
  int i = (int)(w * 4 / PAGE);
  return (i < FINE_P0 || i >= FINE_P1) && s_first[i] == P_READ;
}
static int page_changed(uint32_t w) { return page_read(w) && changed(w); }
static int page_written(uint32_t w) {
  int i = (int)(w * 4 / PAGE);
  return (i < FINE_P0 || i >= FINE_P1) && s_first[i] == P_WRITE;
}

/* Pages read: changed if any word in them changed. */
static int page_any_changed(int i) {
  return memcmp(page_at(i), s_copy + (size_t)i * PAGE, PAGE) != 0;
}
static int s_cur_changed;
static int page_read_is(uint32_t w) {
  int i = (int)(w * 4 / PAGE);
  return page_read(w) && page_any_changed(i) == s_cur_changed;
}

static void report(void) {
  const uint32_t words = PSX_RAM_SIZE / 4;
  fprintf(s_out, "sample %d %d %d\n", s_vblank, s_gs, s_level);
  runs('F', FINE_P0 * PAGE / 4, FINE_P1 * PAGE / 4, fine_changed, " changed", 1);
  runs('F', FINE_P0 * PAGE / 4, FINE_P1 * PAGE / 4, fine_stable, " stable", 1);
  s_cur_changed = 1;
  runs('P', 0, words, page_read_is, " changed", PAGE / 4);
  s_cur_changed = 0;
  runs('P', 0, words, page_read_is, " stable", PAGE / 4);
  runs('C', 0, words, page_changed, "", 1);
  runs('W', 0, words, page_written, "", PAGE / 4);
  fflush(s_out);
}

static void tick(void) {
  if (s_pending) { /* the last sample's pass, against what changed since */
    report();
    s_pending = 0;
  }
  s_armed = 0;
  if (s_count % s_every == 0) {
    memcpy(s_copy, (void *)g_psx_arena_base, PSX_RAM_SIZE);
    s_armed = 1;
  }
}

static void pass(int begin) {
  if (begin) {
    if (s_count++ % s_every || !s_armed)
      return;
    memset(s_first, 0, sizeof s_first);
    memset(s_fine, 0, sizeof s_fine);
    s_vblank = host_vblank_count();
    s_gs = (int)*(uint32_t *)PSX_ARENA(0x800757D8u);    /* g_Gamestate (pad.c) */
    s_level = (int)*(uint32_t *)PSX_ARENA(0x8007596Cu); /* g_LevelId */
    s_tracing = 1;
    protect(1);
  } else if (s_tracing) {
    protect(0);
    s_tracing = 0;
    s_armed = 0;
    s_pending = 1;
  }
}

static const PresentReadAudit s_audit = {tick, pass};

int host_reads_open(const char *path, int every) {
  struct sigaction sa;
  if (!(s_out = fopen(path, "w")) || !(s_copy = malloc(PSX_RAM_SIZE)))
    return -1;
  if (every > 0)
    s_every = every;
  memset(&sa, 0, sizeof sa);
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sa.sa_sigaction = on_segv;
  sigaction(SIGSEGV, &sa, NULL);
  sa.sa_sigaction = on_trap;
  sigaction(SIGTRAP, &sa, NULL);
  g_present_reads = &s_audit;
  return 0;
}

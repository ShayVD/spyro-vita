/* The shadows of decisions D94's hand-written C (hand_shadow.h). */
#include "hand_shadow.h"

#if HAND_SHADOW
#include "mips2c_rt.h"
#include "psx_arena.h"

#include <gtereg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int g_hand_shadow, g_hand_shadow_fail;

typedef struct {
  u_char ram[PSX_RAM_SIZE];
  GTERegisters gte;
} HandState;

static int s_in_shadow;
static u32 s_sp;
static HandState s_before;
static HandState *s_unit; /* in this process, or shared with the child */

static void Save(HandState *s) {
  memcpy(s->ram, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  s->gte = gteRegs;
}

static void Load(const HandState *s) {
  memcpy((void *)g_psx_arena_base, s->ram, PSX_RAM_SIZE);
  gteRegs = s->gte;
}

/* RAM the comparison skips: the scratchpad (the arena's first KB, which
 * the unit's state replaces) and what lies below the game's code, the
 * asm's register spill area, and the emulated stack below the call. */
static int Skip(u32 off) {
  return off < 0x10000u || (off >= 0x77DD8u && off < 0x77EA0u) ||
         (off < s_sp - PSX_RAM_BASE && off >= s_sp - PSX_RAM_BASE - 0x10000u);
}

/* Compares the live state (the hand C's) with the unit's, then leaves the
 * unit's scratchpad and GTE. */
static int s_show = -1; /* SPYRO_SHADOW_SHOW: differing words printed per call (8) */

static void Compare(const char *name) {
  const u32 *h = (const u32 *)g_psx_arena_base, *g = (const u32 *)s_unit->ram;
  u32 i;
  int shown = 0;
  if (s_show < 0)
    s_show = getenv("SPYRO_SHADOW_SHOW") ? atoi(getenv("SPYRO_SHADOW_SHOW")) : 8;
  for (i = 0; i < PSX_RAM_SIZE / 4; i++)
    if (h[i] != g[i] && !Skip(i * 4)) {
      g_hand_shadow_fail++;
      if (shown++ < s_show)
        printf("  handshadow %s ram %08x unit %08x hand %08x\n", name, PSX_RAM_BASE + i * 4, g[i], h[i]);
    }
  for (i = 0; i < 31; i++) /* not FLAG (31): a command's result, like the data registers */
    if (gteRegs.CP2C.p[i].d != s_unit->gte.CP2C.p[i].d) {
      g_hand_shadow_fail++;
      printf("  handshadow %s gte c%u unit %08x hand %08x\n", name, i, s_unit->gte.CP2C.p[i].d, gteRegs.CP2C.p[i].d);
    }
  memcpy((void *)g_psx_arena_base, s_unit->ram, 0x400);
  gteRegs = s_unit->gte;
}

/* SPYRO_HAND_ONLY=NAME,...: shadow only these functions (default all). */
static int Wanted(const char *name) {
  static const char *only;
  static int read;
  const char *p;
  size_t n = strlen(name);
  if (!read) {
    only = getenv("SPYRO_HAND_ONLY");
    read = 1;
  }
  if (!only || !*only)
    return 1;
  for (p = only; (p = strstr(p, name)); p += n)
    if ((p == only || p[-1] == ',') && (p[n] == ',' || !p[n]))
      return 1;
  return 0;
}

int HandShadowBegin(const char *name) {
  if (!g_hand_shadow || s_in_shadow || !Wanted(name))
    return 0;
  if (!s_unit && !(s_unit = malloc(sizeof *s_unit)))
    return 0;
  s_in_shadow = 1;
  s_sp = g_mips_sp;
  Save(&s_before);
  return 1;
}

void HandShadowSwap(void) {
  Save(s_unit);
  Load(&s_before);
  g_mips_sp = s_sp;
}

/* Calls compared, by name (each name is one string literal). */
static struct {
  const char *name;
  unsigned int calls;
} s_count[32];

static void Count(const char *name) {
  int i;
  for (i = 0; i < 32 && s_count[i].name && s_count[i].name != name; i++)
    ;
  if (i < 32) {
    s_count[i].name = name;
    s_count[i].calls++;
  }
}

void HandShadowReport(FILE *f) {
  int i;
  for (i = 0; i < 32 && s_count[i].name; i++)
    fprintf(f, "  hand shadow: %s %u calls\n", s_count[i].name, s_count[i].calls);
}

unsigned int HandShadowEnd(const char *name, unsigned int gen, unsigned int hand) {
  Count(name);
  Compare(name);
  if (gen != hand) {
    g_hand_shadow_fail++;
    printf("  handshadow %s result unit %08x hand %08x\n", name, gen, hand);
  }
  s_in_shadow = 0;
  return hand;
}

/* The child's state comes back through a shared mapping. */
static HandState *s_shared;

int HandShadowFork(const char *name, unsigned int (*unit)(), unsigned int a0, unsigned int a1) {
  pid_t pid;
  int status = 0;
  if (!g_hand_shadow || s_in_shadow || !Wanted(name))
    return 0;
  if (!s_shared) {
    s_shared = mmap(NULL, sizeof *s_shared, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s_shared == MAP_FAILED) {
      s_shared = NULL;
      return 0;
    }
  }
  fflush(stdout);
  fflush(stderr);
  s_in_shadow = 1;
  s_sp = g_mips_sp;
  pid = fork();
  if (pid == 0) {
    /* The child mustn't write the run's files (the trace, shots, audio,
     * cards): stdio would flush the parent's buffered rows a second time.
     * Every descriptor open for writing but stdout and stderr goes to
     * /dev/null; the ones open for reading (the disc) stay. */
    int fd, null = open("/dev/null", O_WRONLY);
    for (fd = 3; fd < 1024; fd++) {
      int fl = fcntl(fd, F_GETFL);
      if (fl != -1 && fd != null && (fl & O_ACCMODE) != O_RDONLY)
        dup2(null, fd);
    }
    /* The unit's calls to other hand C are shadowed in the child as here,
     * so its GTE ends as the units would leave it (D83). */
    s_in_shadow = 0;
    unit(a0, a1);
    Save(s_shared);
    fflush(stdout);
    _exit(0);
  }
  if (pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0)
    return 1;
  printf("  handshadow: the unit's child failed (pid %d, status %#x); the hand C runs alone\n", (int)pid, status);
  s_in_shadow = 0;
  return 0;
}

void HandShadowForkEnd(const char *name) {
  HandState *keep = s_unit;
  s_unit = s_shared;
  Count(name);
  Compare(name);
  s_unit = keep;
  s_in_shadow = 0;
}

static int s_fuzz = -1;
static u32 s_rng;
static HandState *s_fuzz_save;

int HandFuzzBegin(const char *name, unsigned int seed) {
  if (s_fuzz < 0)
    s_fuzz = getenv("SPYRO_HAND_FUZZ") ? atoi(getenv("SPYRO_HAND_FUZZ")) : 0;
  if (!g_hand_shadow || s_in_shadow || !s_fuzz || !Wanted(name))
    return 0;
  if (!s_fuzz_save && !(s_fuzz_save = malloc(sizeof *s_fuzz_save)))
    return 0;
  s_rng = seed * 2654435761u + 1;
  Save(s_fuzz_save);
  return s_fuzz;
}

unsigned int HandFuzzRand(void) {
  s_rng = s_rng * 1103515245u + 12345u;
  return s_rng >> 8;
}

void HandFuzzRestore(void) {
  Load(s_fuzz_save);
}
#endif

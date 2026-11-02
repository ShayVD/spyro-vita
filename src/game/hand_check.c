/* fast_render=2's per-call check (decisions D79; host --fast-render-check).
 *
 * Each call of a hand renderer runs twice from the same state: first the
 * register-file hand C (fast_render=1), whose results are kept aside, then
 * the native-GTE build (fast_render=2), whose results the game goes on with.
 * The two must leave the same RAM (the whole 2 MB arena, which holds the
 * renderers' scratchpad, the primitive buffers and the OTs), the same
 * scratchpad buffer and every GTE register's raw storage, so a run of the
 * host's scripts checks every call they make, not only the frames a capture
 * samples. The port's hooks inside the renderers (lod.c's counters) run
 * twice; nothing they count is compared. */
#include "render_hand.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "psx_arena.h"

int g_hand_check;

static void (*s_log)(const char *fmt, ...);
static unsigned char *s_before, *s_reg;
static unsigned char s_scr_before[sizeof g_psx_scratch], s_scr_reg[sizeof g_psx_scratch];
static GTERegisters s_gte_before, s_gte_reg;

enum { MAX_NAMES = 8 };
static struct {
  const char *name;
  unsigned long calls, bad;
} s_stats[MAX_NAMES];

void hand_check_set_log(void (*log)(const char *fmt, ...)) { s_log = log; }

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

static void report_diff(const char *name, unsigned long call) {
  const unsigned char *ram = (const unsigned char *)g_psx_arena_base;
  const u32 *gr = (const u32 *)&s_gte_reg, *gn = (const u32 *)&gteRegs;
  unsigned n = 0, i;
  if (!s_log)
    return;
  s_log("hand check: %s call %lu: fast_render 1 and 2 differ", name, call);
  for (i = 0; i < PSX_RAM_SIZE && n < 8; i += 4) {
    u32 a, b;
    memcpy(&a, s_reg + i, 4);
    memcpy(&b, ram + i, 4);
    if (a != b) {
      s_log("  ram %08x: 1 %08x 2 %08x", 0x80000000u + i, a, b);
      n++;
    }
  }
  for (i = 0; i < 64; i++)
    if (gr[i] != gn[i])
      s_log("  gte %s%u: 1 %08x 2 %08x", i < 32 ? "d" : "c", i & 31, gr[i], gn[i]);
  if (memcmp(s_scr_reg, g_psx_scratch, sizeof s_scr_reg))
    s_log("  scratchpad buffer differs");
}

u32 hand_check(const char *name, HandFn reg, HandFn native, u32 a0, u32 a1, u32 a2, u32 a3) {
  const int k = stat_index(name);
  u32 sp = g_mips_sp, v1 = g_mips_v1, r;
  if (!s_before) {
    s_before = malloc(PSX_RAM_SIZE);
    s_reg = malloc(PSX_RAM_SIZE);
  }
  memcpy(s_before, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  memcpy(s_scr_before, g_psx_scratch, sizeof s_scr_before);
  s_gte_before = gteRegs;
  reg(a0, a1, a2, a3);
  memcpy(s_reg, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  memcpy(s_scr_reg, g_psx_scratch, sizeof s_scr_reg);
  s_gte_reg = gteRegs;

  memcpy((void *)g_psx_arena_base, s_before, PSX_RAM_SIZE);
  memcpy(g_psx_scratch, s_scr_before, sizeof s_scr_before);
  gteRegs = s_gte_before;
  g_mips_sp = sp;
  g_mips_v1 = v1;
  r = native(a0, a1, a2, a3);

  s_stats[k].calls++;
  if (memcmp(s_reg, (void *)g_psx_arena_base, PSX_RAM_SIZE) ||
      memcmp(s_scr_reg, g_psx_scratch, sizeof s_scr_reg) ||
      memcmp(&s_gte_reg, &gteRegs, sizeof gteRegs)) {
    if (s_stats[k].bad++ < 20)
      report_diff(name, s_stats[k].calls);
  }
  return r;
}

void hand_check_report(void) {
  int i;
  if (!g_hand_check || !s_log)
    return;
  for (i = 0; i < MAX_NAMES && s_stats[i].name; i++)
    s_log("hand check: %s %lu calls, %lu differ", s_stats[i].name, s_stats[i].calls, s_stats[i].bad);
}

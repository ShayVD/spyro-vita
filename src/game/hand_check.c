/* fast_render=2's per-call check (decisions D80; host --fast-render-check).
 *
 * Each call of a hand renderer runs twice from the same state: first the
 * register-file hand C (fast_render=1), whose results are kept aside, then
 * the native-GTE build (fast_render=2), whose results the game goes on with.
 * The two must leave the same RAM (the whole 2 MB arena, which holds the
 * renderers' scratchpad, the primitive buffers and the OTs), the same
 * scratchpad buffer and every GTE register's raw storage, so a run of the
 * host's scripts checks every call they make, not only the frames a capture
 * samples. The port's hooks inside the renderers (lod.c's counters) run
 * twice; nothing they count is compared.
 *
 * The renderers written by hand after D80 (D95, D98) are checked three
 * ways instead (hand_check_unit): the mips2c unit first, as the reference,
 * then fast_render=1's build and fast_render=2's, each from the same state
 * and each compared with the unit.
 *
 * --gte-audit N (with the check) also finds, every Nth call of each
 * renderer, which GTE registers it reads from before the call: each of the
 * 64 registers in turn has its raw storage changed (two patterns) and the
 * register-file hand C runs again from the same state. A register whose
 * change shows in RAM is read ("in"); one whose change shows only in other
 * registers the renderer leaves feeds GTE results nothing in the call reads
 * ("gte"); one left exactly as changed is untouched ("kept"); the rest are
 * written before any read ("dead" on entry). */
#include "render_hand.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_arena.h"

int g_hand_check;
int g_gte_audit; /* every Nth call, 0 off */
/* --particle-sweep (D95): no run of the game reaches the particle types 4
 * on, so before each func_800573C8 call every live particle's type is
 * set to 4, 5, 6 or 7 in turn (its subtype and data as they are), both
 * ways run from there and are compared, and the particles are put back
 * after. A type-5 particle's depth (+0x8's low half) is made non-negative:
 * read from another type's data it can put the bucket outside the OT,
 * where the asm (and so the unit) writes as it would on the PS1; a type-4
 * one gets a streak in front of the camera (below) */
int g_particle_sweep;
extern char g_Particles[];
enum { SWEEP_MAX = 0x400 };
static u32 s_sweep_at[SWEEP_MAX];
static u8 s_sweep_save[SWEEP_MAX][16];
static int s_sweep_n;
static unsigned s_sweep_turn;

static void sweep_begin(void) {
  u32 p = MIPS_KUSEG(*(u32 *)g_Particles);
  s_sweep_n = 0;
  for (;;) {
    const s16 t = *(s16 *)(uintptr_t)p;
    if ((t >> 8) == -1 || s_sweep_n == SWEEP_MAX)
      break;
    if (t >= 0) {
      const u8 type = (u8)(4 + s_sweep_turn++ % 4);
      s_sweep_at[s_sweep_n] = p;
      memcpy(s_sweep_save[s_sweep_n], (const void *)(uintptr_t)p, 16);
      *(u8 *)(uintptr_t)(p + 1) = type;
      if (type == 5)
        *(u8 *)(uintptr_t)(p + 9) &= 0x7F;
      if (type == 4) {
        /* a 3D streak's two ends are view-space translations (+0x4, +0x8,
         * +0xC): another type's data never puts them on screen, so these
         * get a short streak in front of the camera, varied a little */
        const u32 d = s_sweep_turn & 0x3F;
        *(u32 *)(uintptr_t)(p + 0x4) = d | (d << 16);
        *(u32 *)(uintptr_t)(p + 0x8) = 0x200u | ((0x20u + d) << 16);
        *(u32 *)(uintptr_t)(p + 0xC) = (0x18u + d) | (0x220u << 16);
      }
      s_sweep_n++;
    }
    p += 0x20;
  }
}

static void sweep_end(void) {
  int i;
  for (i = 0; i < s_sweep_n; i++)
    memcpy((void *)(uintptr_t)s_sweep_at[i], s_sweep_save[i], 16);
}

static void (*s_log)(const char *fmt, ...);
static unsigned char *s_before, *s_reg;
static unsigned char s_scr_before[sizeof g_psx_scratch], s_scr_reg[sizeof g_psx_scratch];
static GTERegisters s_gte_before, s_gte_reg;

enum { MAX_NAMES = 48 };
static struct {
  const char *name;
  unsigned long calls, bad, bad1, audited;
  int unit; /* checked against the mips2c unit (hand_check_unit) */
  unsigned long long in, gte, kept; /* bit i: register i (32 + control) */
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

/* GTE register i: the 32 data registers, then the 32 control registers
 * (GTERegisters starts with CP0's, which nothing here uses). */
static u32 *gte_reg(GTERegisters *g, int i) { return i < 32 ? &g->CP2D.r[i] : &g->CP2C.r[i - 32]; }

static void report_diff(const char *name, unsigned long call, const char *ref, const char *run) {
  const unsigned char *ram = (const unsigned char *)g_psx_arena_base;
  unsigned n = 0, i;
  if (!s_log)
    return;
  s_log("hand check: %s call %lu: %s and %s differ", name, call, ref, run);
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
    if (*gte_reg(&s_gte_reg, (int)i) != *gte_reg(&gteRegs, (int)i))
      s_log("  gte %s%u: 1 %08x 2 %08x", i < 32 ? "d" : "c", i & 31, *gte_reg(&s_gte_reg, (int)i),
            *gte_reg(&gteRegs, (int)i));
  if (memcmp(s_scr_reg, g_psx_scratch, sizeof s_scr_reg))
    s_log("  scratchpad buffer differs");
}

/* The state now against the reference run's (s_reg...). */
static int differs(void) {
  return memcmp(s_reg, (void *)g_psx_arena_base, PSX_RAM_SIZE) ||
         memcmp(s_scr_reg, g_psx_scratch, sizeof s_scr_reg) ||
         memcmp(&s_gte_reg, &gteRegs, sizeof gteRegs);
}

static void restore_before(u32 sp, u32 v1) {
  memcpy((void *)g_psx_arena_base, s_before, PSX_RAM_SIZE);
  memcpy(g_psx_scratch, s_scr_before, sizeof s_scr_before);
  gteRegs = s_gte_before;
  g_mips_sp = sp;
  g_mips_v1 = v1;
}

/* The live-in audit of one call (the state before it is in s_before...;
 * s_reg... holds the unchanged run's result). */
static void audit_call(int k, HandFn reg, u32 a0, u32 a1, u32 a2, u32 a3, u32 sp, u32 v1) {
  static const u32 pat[2] = {0x00010001u, 0xFFFFFFFFu};
  int i, j;
  for (i = 0; i < 64; i++)
    for (j = 0; j < 2; j++) {
      restore_before(sp, v1);
      *gte_reg(&gteRegs, i) ^= pat[j];
      reg(a0, a1, a2, a3);
      if (memcmp(s_reg, (void *)g_psx_arena_base, PSX_RAM_SIZE) ||
          memcmp(s_scr_reg, g_psx_scratch, sizeof s_scr_reg)) {
        s_stats[k].in |= 1ull << i;
      } else if (memcmp(&s_gte_reg, &gteRegs, sizeof gteRegs)) {
        GTERegisters g = gteRegs;
        *gte_reg(&g, i) ^= pat[j];
        if (memcmp(&s_gte_reg, &g, sizeof g))
          s_stats[k].gte |= 1ull << i;
        else
          s_stats[k].kept |= 1ull << i;
      }
    }
  s_stats[k].audited++;
}

static int s_in_check; /* a checked call inside another runs once (D96) */
static int s_cmp_v0;    /* hand_check_v0: the results must match too */

u32 hand_check(const char *name, HandFn reg, HandFn native, u32 a0, u32 a1, u32 a2, u32 a3) {
  const int k = stat_index(name);
  const int sweep = g_particle_sweep && !strcmp(name, "func_800573C8");
  const int cmp_v0 = s_cmp_v0;
  u32 sp = g_mips_sp, v1 = g_mips_v1, r, rr;
  s_cmp_v0 = 0;
  if (s_in_check)
    return native(a0, a1, a2, a3);
  s_in_check = 1;
  if (sweep)
    sweep_begin();
  if (!s_before) {
    s_before = malloc(PSX_RAM_SIZE);
    s_reg = malloc(PSX_RAM_SIZE);
  }
  memcpy(s_before, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  memcpy(s_scr_before, g_psx_scratch, sizeof s_scr_before);
  s_gte_before = gteRegs;
  rr = reg(a0, a1, a2, a3);
  memcpy(s_reg, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  memcpy(s_scr_reg, g_psx_scratch, sizeof s_scr_reg);
  s_gte_reg = gteRegs;
  if (g_gte_audit && s_stats[k].calls % (unsigned long)g_gte_audit == 0)
    audit_call(k, reg, a0, a1, a2, a3, sp, v1);

  restore_before(sp, v1);
  r = native(a0, a1, a2, a3);

  s_stats[k].calls++;
  if (differs()) {
    if (s_stats[k].bad++ < 20)
      report_diff(name, s_stats[k].calls, "fast_render=1", "fast_render=2");
  } else if (cmp_v0 && rr != r) {
    if (s_stats[k].bad++ < 20 && s_log)
      s_log("hand check: %s call %lu: v0 %08x (the mips2c unit) and %08x (the hand C) differ", name,
            s_stats[k].calls, rr, r);
  }
  if (sweep)
    sweep_end();
  s_in_check = 0;
  return r;
}

u32 hand_check_unit(const char *name, HandFn unit, HandFn reg, HandFn native, u32 a0, u32 a1, u32 a2,
                    u32 a3) {
  const int k = stat_index(name);
  const int sweep = g_particle_sweep && !strcmp(name, "func_800573C8");
  u32 sp = g_mips_sp, v1 = g_mips_v1, r;
  if (s_in_check)
    return native(a0, a1, a2, a3);
  s_in_check = 1;
  s_stats[k].unit = 1;
  if (sweep)
    sweep_begin();
  if (!s_before) {
    s_before = malloc(PSX_RAM_SIZE);
    s_reg = malloc(PSX_RAM_SIZE);
  }
  memcpy(s_before, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  memcpy(s_scr_before, g_psx_scratch, sizeof s_scr_before);
  s_gte_before = gteRegs;
  unit(a0, a1, a2, a3);
  memcpy(s_reg, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  memcpy(s_scr_reg, g_psx_scratch, sizeof s_scr_reg);
  s_gte_reg = gteRegs;
  if (g_gte_audit && s_stats[k].calls % (unsigned long)g_gte_audit == 0)
    audit_call(k, unit, a0, a1, a2, a3, sp, v1);

  s_stats[k].calls++;
  restore_before(sp, v1);
  reg(a0, a1, a2, a3);
  if (differs() && s_stats[k].bad1++ < 20)
    report_diff(name, s_stats[k].calls, "the mips2c unit", "fast_render=1");

  restore_before(sp, v1);
  r = native(a0, a1, a2, a3);
  if (differs() && s_stats[k].bad++ < 20)
    report_diff(name, s_stats[k].calls, "the mips2c unit", "fast_render=2");
  if (sweep)
    sweep_end();
  s_in_check = 0;
  return r;
}

/* As hand_check, for a function whose result its callers read (D96). */
u32 hand_check_v0(const char *name, HandFn reg, HandFn native, u32 a0, u32 a1, u32 a2, u32 a3) {
  s_cmp_v0 = 1;
  return hand_check(name, reg, native, a0, a1, a2, a3);
}

static const char *reg_name(int i) {
  static const char *const d[32] = {"VXY0", "VZ0", "VXY1", "VZ1", "VXY2", "VZ2", "RGBC", "OTZ",
                                    "IR0", "IR1", "IR2", "IR3", "SXY0", "SXY1", "SXY2", "SXYP",
                                    "SZ0", "SZ1", "SZ2", "SZ3", "RGB0", "RGB1", "RGB2", "RES1",
                                    "MAC0", "MAC1", "MAC2", "MAC3", "IRGB", "ORGB", "LZCS", "LZCR"};
  static const char *const c[32] = {"R11R12", "R13R21", "R22R23", "R31R32", "R33", "TRX", "TRY",
                                    "TRZ", "L11L12", "L13L21", "L22L23", "L31L32", "L33", "RBK",
                                    "GBK", "BBK", "LR1LR2", "LR3LG1", "LG2LG3", "LB1LB2", "LB3",
                                    "RFC", "GFC", "BFC", "OFX", "OFY", "H", "DQA", "DQB", "ZSF3",
                                    "ZSF4", "FLAG"};
  return i < 32 ? d[i] : c[i - 32];
}

static void log_regs(const char *name, const char *what, unsigned long long m) {
  char line[512];
  int i, n = 0;
  line[0] = 0;
  for (i = 0; i < 64; i++)
    if (m >> i & 1)
      n += snprintf(line + n, sizeof line - (size_t)n, " %s", reg_name(i));
  s_log("gte audit: %s %s:%s", name, what, n ? line : " none");
}

void hand_check_report(void) {
  int i;
  if (!g_hand_check || !s_log)
    return;
  for (i = 0; i < MAX_NAMES && s_stats[i].name; i++) {
    if (s_stats[i].unit)
      s_log("hand check: %s %lu calls, %lu differ (fast_render=2 against the mips2c unit; "
            "fast_render=1 against it: %lu differ)",
            s_stats[i].name, s_stats[i].calls, s_stats[i].bad, s_stats[i].bad1);
    else
      s_log("hand check: %s %lu calls, %lu differ", s_stats[i].name, s_stats[i].calls, s_stats[i].bad);
    if (s_stats[i].audited) {
      s_log("gte audit: %s %lu calls audited", s_stats[i].name, s_stats[i].audited);
      log_regs(s_stats[i].name, "in", s_stats[i].in);
      log_regs(s_stats[i].name, "gte", s_stats[i].gte & ~s_stats[i].in);
      log_regs(s_stats[i].name, "kept", s_stats[i].kept & ~(s_stats[i].in | s_stats[i].gte));
      log_regs(s_stats[i].name, "dead", ~(s_stats[i].in | s_stats[i].gte | s_stats[i].kept));
    }
  }
}

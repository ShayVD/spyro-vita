/* Longer draw distance (decisions D49, docs/presentation.md "Draw
 * distance"): optional, off by default. What the game draws at a distance is
 * decided by three render-side numbers, scaled here as the hand-written
 * renderers read them (tools/mips2c/hints.yaml `before`, the PORT_LOD_*
 * macros in mips2c_rt.h):
 *
 *  - the world's high-detail distance, g_Environment.m_LodDistance (0x8000,
 *    initialization.c). func_800258F0 puts each sector in the near
 *    (textured, high-poly) or far (low-poly) list by it, and the far and
 *    near draws fade between the two around it. Nothing but r_environment
 *    reads it;
 *  - each moby's detail distance, (m_Renderer & 0x3F) << 8 (r_moby
 *    func_8001F158/func_800208FC): it only goes into the draw record
 *    (+0x2E), which picks the model's detail and its animation blending;
 *  - each moby's draw distance, the camera box of m_RenderRadius << 10 that
 *    r_moby's three culling loops test (func_8001F158, func_800208FC,
 *    func_80022A2C). Those loops also set m_WasDrawn, which the logic reads
 *    (func_80051FEC updates every moby drawn last frame, and enemies check
 *    it), so it is never widened in place: the loops run first exactly as on
 *    the PS1, then once more over the same list ("pass 2") for the mobys only
 *    the longer box takes in, appending draw records without touching
 *    m_WasDrawn or the shadow lists, and stopping where the record space
 *    ends. Pass 2's mobys take the low-detail model (moby_cull_hand.c,
 *    #180): they are past the original's draw distance, and the high one
 *    there cost draw calls for no detail that shows.
 *
 * The renderers check no buffer bounds, so more geometry could overrun the
 * primitive space or the near-sector list (128 entries before the next list,
 * D_8006FCF4+0x1C00). With the option on, every draw starts at the lower
 * buffer and spans both, up to 0x600 under the HUD OT, where the HUD's
 * mobys and the sky and particle limit are (D54, D61, present.c
 * PortPolySpace): 0x37A00 bytes, 0x25A00 in reduced-space levels
 * (draw_room). Three guards, all only with the option on:
 *  - hard bounds: r_environment's six sector-list loops end their list early
 *    once the primitive pointer is within ENV_MARGIN of that top (a frame
 *    may then lack some scenery, never write past it), and r_moby's draw
 *    loops stop at the first record pass 2 added once the original's one
 *    buffer is MOBY_SHARE% full (the mobys draw before the world, which
 *    keeps the rest); the near list stops taking sectors one short of its
 *    end;
 *  - a limiter: after every shown draw the bytes used are measured and the
 *    scale backs off before the span or the world's bound gets close, and
 *    with 60 Hz logic in play before the draw passes a work budget
 *    (g_lod.budget, #180; off with the Vita's dd_budget=0 or the host's
 *    --lod-budget 0);
 *  - a new level or gamestate starts again at 1x, then grows.
 * (The first whole-game run had only the limiter and crashed entering
 * level 40 at 2x: one frame went past the buffer into the world OT.) Until
 * #203 the bound and the limiter kept to one buffer's size, as before D54:
 * a world over 65% of that bound's room (Stone Hill's 52 KB of 66) held 2x
 * at 1x even with the budget off. Only drawn pictures use the longer
 * distances: the hidden 4:3 logic draw of attract demos keeps the original
 * ones. */
#include "lod.h"

#include "mips2c_rt.h"
#include "present.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdio.h>
#include <string.h>

#include "buffers.h"
#include "common.h"
#include "environment.h"
#include "gamepad.h"
#include "graphics.h"
#include "moby.h"
#ifndef RENDER_PRES
#include "tick60.h" /* the game's copy only (logic60) */
#endif

extern char D_8006FCF4[];

LodConfig g_lod = {LOD_ONE, 0, 0, LOD_BUDGET_60};
int g_lod_k = LOD_ONE; /* this draw's scale */
int g_lod_pass;        /* r_moby's pass 2 is running */
int g_lod_stats;       /* this draw collects statistics */

#define MAX_EXT 1024 /* level mobys with a hysteresis bit */
#define GRACE 0x200  /* as r_moby gives a moby drawn last frame (in /4 units) */
#define POLY_TOP 0x1BA00 /* the HUD's mobys above (draw.c func_8001A40C...) */
#define NEAR_LIST 0x1C00
#define NEAR_END 0x1E00
/* Room left under the span's top when r_environment stops: one near
 * sector's primitives (5,868 bytes at most) plus everything drawn
 * after the world (the sky, particles, HUD: 31,604 at most), both measured
 * over the whole-game tour at 2x (port_lod_report "largest sector", "after
 * the world"; D49, measured again with the span as the room, #203), and
 * about 7.5 KB to spare. The far (low-poly) loop's sectors can be far
 * bigger (57 KB for a background sector at 1x), but with the option on it
 * writes only less than at 1x: polygons inside the longer distance are
 * skipped, and sectors wholly inside it aren't listed. */
#define ENV_MARGIN 0xB000
#define FAIRY_CARD_BUF 0x600 /* the span's top under the HUD OT: present.c PortPolySpace (D61) */
#define MOBY_SHARE 60 /* pass 2's mobys are drawn while the buffer is under this % */

static int s_k = LOD_ONE; /* the limiter's scale for the next draw */
static int s_hold;        /* draws before it may grow again */
static uint32_t s_ext[2][MAX_EXT / 32]; /* level mobys drawn by pass 2 */
static int s_ext_cur;
static int s_level = -1, s_gamestate = -1;
static int s_loaded; /* a save state was loaded: the next draw at full scale */

/* this draw's primitive buffer (draw_begin) */
static u32 s_poly_lo, s_env_limit, s_moby_limit, s_env_last, s_env_end;
static u32 s_rec2 = ~0u; /* the first draw record pass 2 wrote */

static struct {
  unsigned draws, cuts, near_full, env_cuts, moby_cuts, budget_cuts, budget_held;
  unsigned long long used, k, extra;
  unsigned used_max, cap, k_min, near_max, records_max, sector_max, after_env_max;
} s_st;

/* dd_budget=auto (#212): the last shown draw, for the platform
 * (lod_last_draw), and the budgets it set since the last report with the
 * frame times they came from (lod_set_budget). Empty on the host. */
static int s_last_bytes, s_last_60;
static struct {
  unsigned n, over, target;
  unsigned long long used, budget;
} s_bt;

/* The game's primitive buffer: 0x1C000 bytes (0x13000 in reduced-space
 * levels), up to where the HUD's mobys start. */
static int buffer_cap(void) {
  int cap = (int)((char *)g_Buffers.m_HigherPolyBuffer - (char *)g_Buffers.m_LowerPolyBuffer);
  return cap <= 0 || cap > POLY_TOP ? POLY_TOP : cap;
}

/* The room a draw starting at lo has. With the option on, PortPolySpace
 * (D54) starts every draw at the lower buffer and lets it run to 0x600 under
 * the HUD OT: 0x37A00 bytes, 0x25A00 in reduced-space levels, the real
 * buffer (#203). With it off (statistics only) the draw's own buffer, as
 * before. */
static int draw_room(u32 lo) {
  const u32 top = (u32)(uintptr_t)g_Buffers.m_HudOTStart - FAIRY_CARD_BUF;
  const int cap = buffer_cap();
  if (g_lod.scale <= LOD_ONE || top <= lo || top - lo < (u32)cap || top - lo > 2 * 0x1C000)
    return cap;
  return (int)(top - lo);
}

static int cull_distance(void) {
  int c = g_Environment.m_CullingDistance;
  return c > 0 ? c : 0x14000;
}

static int moby_index(u32 moby) {
  u32 base = (u32)(uintptr_t)g_LevelMobys, i;
  if (!g_LevelMobys || moby < base)
    return -1;
  i = (moby - base) / sizeof(Moby);
  return i < MAX_EXT && base + i * sizeof(Moby) == moby ? (int)i : -1;
}

/* ---- called by the renderers (mips2c_rt.h) ---- */

/* r_environment: m_LodDistance as each of its three reads gets it, before
 * the shift. Kept within the far geometry's own cull distance, so the near
 * draw never goes deeper than the game already draws. */
u32 port_lod_env(u32 d) {
  u32 s = (u32)(((uint64_t)d * (u32)g_lod_k) >> 8), c = (u32)cull_distance();
  if (s > c)
    s = c > d ? c : d;
  return s;
}

/* r_moby: (m_Renderer & 0x3F) << 8; the record keeps (that - Z) in 16 bits */
u32 port_lod_moby_detail(u32 d) {
  u32 s = (d * (u32)g_lod_k) >> 8;
  return s > 0x7D00 ? 0x7D00 : s;
}

/* pass 2: a moby the first pass drew (m_WasDrawn 1, as that pass left it:
 * the loop cleared it just now) is put back as it was and skipped */
int port_lod_moby_skip(u32 a0, u32 moby) {
  if (a0 & 0x100) {
    M_U8(moby + 0x51) = 1;
    return 1;
  }
  return 0;
}

/* pass 2: the box half-size, m_RenderRadius << 8 in the renderers' /4
 * units, longer; GRACE for a moby pass 2 drew last frame, as the game gives
 * its own */
u32 port_lod_moby_box(u32 t4, u32 moby) {
  int i = moby_index(moby);
  u32 s = (u32)(((uint64_t)t4 * (u32)g_lod_k) >> 8), c = (u32)cull_distance() >> 2;
  if (s > c)
    s = c > t4 ? c : t4;
  if (i >= 0 && (s_ext[s_ext_cur ^ 1][i >> 5] >> (i & 31) & 1))
    s += GRACE;
  return s;
}

/* pass 2 drew this moby: what goes into m_WasDrawn (0, as the first pass
 * left it) */
u32 port_lod_moby_drawn(u32 moby) {
  int i = moby_index(moby);
  if (i >= 0)
    s_ext[s_ext_cur][i >> 5] |= 1u << (i & 31);
  s_st.extra++;
  return 0;
}

/* At a culling loop's end: 1 = start pass 2 now (there is room for a
 * record); 0 = return (pass 2 just ended, or none). */
int port_lod_moby_pass2(int room, u32 records_end) {
  u32 used = records_end - ((u32)(uintptr_t)D_8006FCF4 + 0x1600);
  if (used / 0x38 > s_st.records_max && used < 0x1000)
    s_st.records_max = used / 0x38;
  if (g_lod_pass) {
    g_lod_pass = 0;
    return 0;
  }
  if (records_end) /* the lit loop has no records */
    s_rec2 = g_lod_k > LOD_ONE && room ? records_end : ~0u;
  if (g_lod_k > LOD_ONE && room) {
    g_lod_pass = 1;
    return 1;
  }
  return 0;
}

/* r_moby's two record-draw loops (func_8001F798, func_80020F34), at each
 * record (hi: the primitive pointer): 1 = stop, at a pass-2 record once the
 * buffer is MOBY_SHARE% full. The lit loop draws as it culls: its pass 2
 * stops the same way. */
int port_lod_records_full(u32 record, u32 prim) {
  if (record < s_rec2 || prim <= s_moby_limit)
    return 0;
  s_st.moby_cuts++;
  return 1;
}

int port_lod_lit_full(void) {
  if ((u32)(uintptr_t)D_800757B0 <= s_moby_limit)
    return 0;
  s_st.moby_cuts++;
  return 1;
}

/* r_environment's sector-list loops, before each next sector (fp: the
 * primitive pointer; textured: 0 for the far, low-poly loop): 1 = end the
 * list here. */
static void sector_size(u32 fp) {
  if (s_env_last && fp >= s_env_last && fp - s_env_last > s_st.sector_max)
    s_st.sector_max = fp - s_env_last;
  s_env_last = fp;
}

int port_lod_env_full(u32 fp, int textured) {
  if (g_lod_stats) {
    if (!textured)
      s_env_last = 0; /* the far loop isn't measured */
    sector_size(fp);
  }
  if (g_lod_k <= LOD_ONE || fp <= s_env_limit)
    return 0;
  s_st.env_cuts++;
  s_k = LOD_ONE; /* and back to the original distances */
  s_hold = 300;
  return 1;
}

/* r_environment done: what follows is the HUD */
void port_lod_env_end(u32 fp) {
  sector_size(fp);
  s_env_end = fp;
}

/* func_800258F0, before a sector goes into the near list: 1 = the list is
 * one entry short of the next one, leave it out */
int port_lod_near_full(u32 t7) {
  if (t7 < (u32)(uintptr_t)D_8006FCF4 + NEAR_END - 4)
    return 0;
  s_st.near_full++;
  s_k = LOD_ONE; /* and back to the original distances at once */
  s_hold = 300;
  return 1;
}

/* func_800258F0, sector loop done: t7 is the near list's end */
void port_lod_env_lists(u32 t7) {
  u32 n = (t7 - ((u32)(uintptr_t)D_8006FCF4 + NEAR_LIST)) / 4;
  if (n > s_st.near_max && n < 0x400)
    s_st.near_max = n;
}

/* ---- around each draw (present.c) ---- */

#ifndef RENDER_PRES
static void (*s_budget_hook)(void); /* lod_set_budget_hook (#212) */
void lod_set_budget_hook(void (*fn)(void)) { s_budget_hook = fn; }
#endif

void port_lod_draw_begin(int shown) {
  const DB *db = g_CurDB == &g_DB[0] ? &g_DB[1] : &g_DB[0]; /* GamestateDraw's */
  u32 lo = (u32)(uintptr_t)port_poly_start(db->m_PolyBuf); /* D54 */
  int cap = buffer_cap(), room = draw_room(lo);
  g_lod_pass = 0;
  g_lod_k = LOD_ONE;
  g_lod_stats = shown && g_lod.stats;
  s_poly_lo = lo;
  s_env_limit = lo + (u32)room - ENV_MARGIN;
  s_moby_limit = lo + (u32)(cap / 100 * MOBY_SHARE);
  s_env_last = 0; /* the world's first loop head starts the measure */
  s_env_end = 0;
  s_rec2 = ~0u;
#ifndef RENDER_PRES
  if (s_budget_hook && shown && g_lod.scale > LOD_ONE)
    s_budget_hook(); /* the Vita's dd_budget=auto (lod_time.c, #212) */
#endif
  if (g_lod.scale <= LOD_ONE || !shown)
    return;
  if (g_lod.guards_only || s_loaded) {
    s_k = g_lod.scale;
    s_hold = 0;
    s_level = g_LevelId;
    s_gamestate = g_Gamestate;
    s_loaded = 0;
  } else if (g_LevelId != s_level || g_Gamestate != s_gamestate) {
    s_level = g_LevelId;
    s_gamestate = g_Gamestate;
    s_k = LOD_ONE;
    s_hold = 60;
  }
  if (s_k > g_lod.scale || s_k < LOD_ONE)
    s_k = s_k < LOD_ONE ? LOD_ONE : g_lod.scale;
  g_lod_k = s_k;
  memset(s_ext[s_ext_cur], 0, sizeof s_ext[0]);
}

void port_lod_reset(void) {
  s_loaded = 1;
  memset(s_ext, 0, sizeof s_ext);
}

/* This draw follows a 60 Hz update: tick60.c's wants_60 (play, not an
 * attract demo); elsewhere the logic and its draw keep 30 Hz. The game's
 * copy decides it on the game thread as each shown draw ends, before the
 * limiter (port_lod_draw_end), and keeps it in s_logic60, part of the state.
 * The presenter's copy (lod_pres.c) never reads tick60's state, which is
 * the game thread's (D164, D167; tests/render_ctx link_check.sh, #194): it
 * has only the game's last decision, in the state it is handed. */
static int s_logic60;
#ifndef RENDER_PRES
static int logic60(void) {
  return g_tick60_mode && tick60_built() && g_Gamestate == GS_Playing &&
         g_DemoMode == DEMO_MODE_NONE;
}
#endif

/* The bytes of primitives a draw wrote, against the room it has (the span,
 * draw_room), or the world's against the room its bound leaves it (the
 * span less ENV_MARGIN: what is drawn after the world, one sector and some
 * to spare), whichever is fuller: above 80% the extra distance halves,
 * above 90% it goes; under 65% it grows back by 1/32 of 1x per draw. So the
 * bound stays a backstop. (Until #203 both were one buffer's: Stone Hill's
 * world alone, 52 KB at 1x, was 78% of its bound's 66 KB, and 2x never grew
 * there even with the budget off.)
 *
 * With 60 Hz logic (in play) a tick's draw has half the time, and the
 * cost of a draw follows its primitives (the owner's hardware log, #180:
 * about 0.2 ms of logic draw a KB; every picture shown up to about 47 KB,
 * pictures lost from about 52 KB). So the longer distance also keeps to a
 * budget there (g_lod.budget, `work` the bytes the draw wrote): above it
 * the extra distance halves, above 9/8 of it it goes, and it grows only
 * under 7/8 of it. Where the original's own draw is over the budget, 2x
 * draws as 1x: it spends only the time the scene leaves. The host's budget
 * is fixed (--lod-budget); the Vita's dd_budget=auto (#212) moves it every
 * logic frame from the game thread's measured time (lod_time.c, through
 * lod_set_budget), and the rule here stays the same. */
static void limit(int used, int cap, int work, int budget) {
  const int over = budget > 0 && work > budget, far_over = over && work > budget + budget / 8;
  const int room = budget <= 0 || work < budget - budget / 8;
  /* The budget's back-offs: over it while the buffer (or the world's bound)
   * is at most 80% full, so only the budget backs off. At 1x already (#200)
   * there is nothing left to take: the budget holds the longer distance at
   * 1x there, and that draw is counted in budget_held as well. */
  if (over && used <= cap / 10 * 8) {
    s_st.budget_cuts++;
    if (s_k <= LOD_ONE)
      s_st.budget_held++;
  }
  if (used > cap / 10 * 9 || far_over) {
    s_k = LOD_ONE;
    s_hold = 60;
    s_st.cuts++;
  } else if (used > cap / 10 * 8 || over) {
    s_k = LOD_ONE + (s_k - LOD_ONE) / 2;
    s_hold = 30;
    s_st.cuts++;
  } else if (s_hold > 0) {
    s_hold--;
  } else if (used < cap / 100 * 65 && room && s_k < g_lod.scale) {
    s_k += LOD_ONE / 32;
  }
}

/* With stats and the option off, only measured. */
void port_lod_draw_end(int shown) {
  int used, cap, k = g_lod_k, on = g_lod.scale > LOD_ONE;
  g_lod_pass = 0;
  g_lod_k = LOD_ONE;
  g_lod_stats = 0;
  s_last_60 = 0;
  if (!shown || !g_CurDB || !(on || g_lod.stats))
    return;
  s_ext_cur ^= 1;
  used = (int)((u32)(uintptr_t)D_800757B0 - s_poly_lo);
  cap = draw_room(s_poly_lo); /* the span with the option on (#203) */
  if (used < 0 || used > 0x100000)
    return;
  if (on) {
    /* the world's share of the room its bound leaves, as a share of the span */
    int world = s_env_end > s_poly_lo ? (int)(s_env_end - s_poly_lo) : 0, room = cap - ENV_MARGIN;
    int world_eff = room > 0 ? (int)((long long)world * cap / room) : cap;
#ifndef RENDER_PRES
    s_logic60 = logic60();
#endif
    limit(world_eff > used ? world_eff : used, cap, used, s_logic60 ? g_lod.budget : 0);
    s_last_bytes = used;
    s_last_60 = s_logic60 && g_lod.budget > 0;
  }
  if (s_env_end && (u32)(uintptr_t)D_800757B0 >= s_env_end &&
      (u32)(uintptr_t)D_800757B0 - s_env_end > s_st.after_env_max)
    s_st.after_env_max = (u32)(uintptr_t)D_800757B0 - s_env_end;
  s_st.draws++;
  s_st.used += (unsigned)used;
  s_st.k += (unsigned)k;
  if ((unsigned)used > s_st.used_max)
    s_st.used_max = (unsigned)used;
  s_st.cap = (unsigned)cap;
  if (!s_st.k_min || (unsigned)k < s_st.k_min)
    s_st.k_min = (unsigned)k;
}

#ifndef RENDER_PRES
int lod_last_draw(int *bytes, int *budget) {
  *bytes = s_last_bytes;
  *budget = g_lod.budget;
  return s_last_60;
}

/* The platform's budget (dd_budget=auto, #212): never under the fixed
 * default, never over the room the draw has (the limiter backs off at 80%
 * of it anyway). */
int lod_set_budget(int bytes, int used_us, int target_us) {
  const int top = draw_room(s_poly_lo);
  if (bytes > top)
    bytes = top;
  if (bytes < LOD_BUDGET_60)
    bytes = LOD_BUDGET_60;
  g_lod.budget = bytes;
  if (used_us < 0)
    used_us = 0;
  s_bt.n++;
  s_bt.over += used_us > target_us;
  s_bt.used += (unsigned)used_us;
  s_bt.target = (unsigned)target_us;
  s_bt.budget += (unsigned)bytes;
  return bytes;
}
#endif

/* tenths of a millisecond, as "12.4" */
#define MS10(us) (unsigned)((us) / 1000), (unsigned)((us) % 1000 / 100)

/* The auto budget's part of the lod line (#212): why it backed off and what
 * the budget was, short (the Vita's line buffer is 400 bytes and the rest of
 * the line takes up to about 350). Held at 1x for most draws: the window's
 * average frame time against the target; otherwise the average budget, the
 * time left under the target (or over it), and the frames over it, each of
 * which cut the budget at once. */
static void budget_report(char *out, unsigned len) {
  const unsigned n = s_bt.n, avg = (unsigned)(s_bt.used / n), t = s_bt.target;
  if (s_st.budget_held * 2 > s_st.draws)
    snprintf(out, len, "; budget held at 1x: %u.%u ms used of %u.%u", MS10(avg), MS10(t));
  else
    snprintf(out, len, "; budget %u KB (auto, %u.%u ms %s; %u over)",
             (unsigned)(s_bt.budget / n / 1024), MS10(avg > t ? avg - t : t - avg),
             avg > t ? "over" : "left", s_bt.over);
}

void port_lod_report(char *out, unsigned len) {
  unsigned d = s_st.draws ? s_st.draws : 1, cap = s_st.cap ? s_st.cap : 1;
  if (!len)
    return;
  out[0] = 0;
  if (s_st.draws)
    snprintf(out, len,
             "%u draws at x%u.%02u average (lowest x%u.%02u); primitives %u KB average, "
             "%u KB peak of %u KB (%u%%); %u back-offs (%u for the budget, of them %u held at 1x); "
             "%u.%u extra mobys per draw; "
             "near sectors peak %u of 127 (%u times full); moby records peak %u of 53; "
             "largest sector %u bytes, after the world %u bytes; stopped early: world %u, "
             "mobys %u",
             s_st.draws, (unsigned)(s_st.k / d / 256),
             (unsigned)(s_st.k / d % 256 * 100 / 256), s_st.k_min / 256,
             s_st.k_min % 256 * 100 / 256, (unsigned)(s_st.used / d / 1024),
             s_st.used_max / 1024, cap / 1024, s_st.used_max * 100 / cap, s_st.cuts, s_st.budget_cuts, s_st.budget_held,
             (unsigned)(s_st.extra / d), (unsigned)(s_st.extra * 10 / d % 10), s_st.near_max,
             s_st.near_full, s_st.records_max, s_st.sector_max, s_st.after_env_max,
             s_st.env_cuts, s_st.moby_cuts);
  if (s_st.draws && s_bt.n) {
    const size_t n = strlen(out);
    if (n + 1 < len)
      budget_report(out + n, len - (unsigned)n);
  }
  memset(&s_st, 0, sizeof s_st);
  memset(&s_bt, 0, sizeof s_bt);
}

/* ---- the presenter's copy (lod_pres.c, stage 7d, D123) ----
 * The same file compiled again on the presenter's names: its own state,
 * which --render-ctx-check sets to the game's before each renderer call and
 * compares after. All of this file's state, in one block. */
#define LOD_STATE(X)                                                           \
  X(g_lod) X(g_lod_k) X(g_lod_pass) X(g_lod_stats) X(s_k) X(s_hold) X(s_ext)   \
  X(s_ext_cur) X(s_level) X(s_gamestate) X(s_loaded) X(s_poly_lo)              \
  X(s_env_limit) X(s_moby_limit) X(s_env_last) X(s_env_end) X(s_rec2) X(s_st) \
  X(s_logic60) X(s_last_bytes) X(s_last_60) X(s_bt)
#define LOD_SIZE(v) + sizeof v
#define LOD_GET(v) memcpy(p, &v, sizeof v), p += sizeof v;
#define LOD_SET(v) memcpy(&v, p, sizeof v), p += sizeof v;
unsigned port_lod_state_size(void) { return 0 LOD_STATE(LOD_SIZE); }
void port_lod_state_get(unsigned char *p) { LOD_STATE(LOD_GET) }
void port_lod_state_set(const unsigned char *p) { LOD_STATE(LOD_SET) }

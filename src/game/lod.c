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
 *    ends.
 *
 * The renderers check no buffer bounds, so more geometry could overrun the
 * primitive buffer (0x1C000 bytes, with the HUD's mobys at its top,
 * draw.c GamestateDraw) or the near-sector list (128 entries before the
 * next list, D_8006FCF4+0x1C00). Three guards, all only with the option on:
 *  - hard bounds: r_environment's six sector-list loops end their list early
 *    once the primitive pointer is within ENV_MARGIN of the buffer's end
 *    (a frame may then lack some scenery, never write past the buffer), and
 *    r_moby's draw loops stop at the first record pass 2 added once the
 *    buffer is MOBY_SHARE% full; the near list stops taking sectors one short
 *    of its end;
 *  - a limiter: after every shown draw the bytes used are measured and the
 *    scale backs off before the buffer gets close to full;
 *  - a new level or gamestate starts again at 1x, then grows.
 * (The first whole-game run had only the limiter and crashed entering
 * level 40 at 2x: one frame went past the buffer into the world OT.) Only
 * drawn pictures use the longer distances: the hidden 4:3 logic draw of
 * attract demos keeps the original ones. */
#include "lod.h"

#include "mips2c_rt.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdio.h>
#include <string.h>

#include "buffers.h"
#include "common.h"
#include "environment.h"
#include "graphics.h"
#include "moby.h"

extern char D_8006FCF4[];

LodConfig g_lod = {LOD_ONE, 0, 0};
int g_lod_k = LOD_ONE; /* this draw's scale */
int g_lod_pass;        /* r_moby's pass 2 is running */
int g_lod_stats;       /* this draw collects statistics */

#define MAX_EXT 1024 /* level mobys with a hysteresis bit */
#define GRACE 0x200  /* as r_moby gives a moby drawn last frame (in /4 units) */
#define POLY_TOP 0x1BA00 /* the HUD's mobys above (draw.c func_8001A40C...) */
#define NEAR_LIST 0x1C00
#define NEAR_END 0x1E00
/* Room left at the end of the buffer when r_environment stops: one near
 * sector's primitives (5,868 bytes at most) plus everything drawn
 * after the world (the sky, particles, HUD: 31,604 at most), both measured
 * over the whole-game tour at 2x (port_lod_report "largest sector", "after
 * the world"; D49), and about 7.5 KB to spare. The far (low-poly) loop's
 * sectors can be far bigger (57 KB for a background sector at 1x), but
 * with the option on it writes only less than at 1x: polygons inside the
 * longer distance are skipped, and sectors wholly inside it aren't listed. */
#define ENV_MARGIN 0xB000
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
  unsigned draws, cuts, near_full, env_cuts, moby_cuts;
  unsigned long long used, k, extra;
  unsigned used_max, cap, k_min, near_max, records_max, sector_max, after_env_max;
} s_st;

/* The game's primitive buffer: 0x1C000 bytes (0x13000 in reduced-space
 * levels), up to where the HUD's mobys start. */
static int buffer_cap(void) {
  int cap = (int)((char *)g_Buffers.m_HigherPolyBuffer - (char *)g_Buffers.m_LowerPolyBuffer);
  return cap <= 0 || cap > POLY_TOP ? POLY_TOP : cap;
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

void port_lod_draw_begin(int shown) {
  const DB *db = g_CurDB == &g_DB[0] ? &g_DB[1] : &g_DB[0]; /* GamestateDraw's */
  u32 lo = (u32)(uintptr_t)db->m_PolyBuf;
  int cap = buffer_cap();
  g_lod_pass = 0;
  g_lod_k = LOD_ONE;
  g_lod_stats = shown && g_lod.stats;
  s_poly_lo = lo;
  s_env_limit = lo + (u32)cap - ENV_MARGIN;
  s_moby_limit = lo + (u32)(cap / 100 * MOBY_SHARE);
  s_env_last = 0; /* the world's first loop head starts the measure */
  s_env_end = 0;
  s_rec2 = ~0u;
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

/* The bytes of primitives a draw wrote, against what the buffer holds, or
 * the world's against where its bound would stop it, whichever is fuller:
 * above 80% the extra distance halves, above 90% it goes; under 65% it
 * grows back by 1/32 of 1x per draw. So the bound stays a backstop. */
static void limit(int used, int cap) {
  if (used > cap / 10 * 9) {
    s_k = LOD_ONE;
    s_hold = 60;
    s_st.cuts++;
  } else if (used > cap / 10 * 8) {
    s_k = LOD_ONE + (s_k - LOD_ONE) / 2;
    s_hold = 30;
    s_st.cuts++;
  } else if (s_hold > 0) {
    s_hold--;
  } else if (used < cap / 100 * 65 && s_k < g_lod.scale) {
    s_k += LOD_ONE / 32;
  }
}

/* With stats and the option off, only measured. */
void port_lod_draw_end(int shown) {
  int used, cap, k = g_lod_k, on = g_lod.scale > LOD_ONE;
  g_lod_pass = 0;
  g_lod_k = LOD_ONE;
  g_lod_stats = 0;
  if (!shown || !g_CurDB || !(on || g_lod.stats))
    return;
  s_ext_cur ^= 1;
  used = (int)((char *)D_800757B0 - (char *)g_CurDB->m_PolyBuf);
  cap = buffer_cap();
  if (used < 0 || used > 0x100000)
    return;
  if (on) {
    /* the world's share of where its bound is, as a share of the buffer */
    int world = s_env_end > s_poly_lo ? (int)(s_env_end - s_poly_lo) : 0, room = cap - ENV_MARGIN;
    int world_eff = room > 0 ? (int)((long long)world * cap / room) : cap;
    limit(world_eff > used ? world_eff : used, cap);
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

void port_lod_report(char *out, unsigned len) {
  unsigned d = s_st.draws ? s_st.draws : 1, cap = s_st.cap ? s_st.cap : 1;
  if (!len)
    return;
  out[0] = 0;
  if (s_st.draws)
    snprintf(out, len,
             "%u draws at x%u.%02u average (lowest x%u.%02u); primitives %u KB average, "
             "%u KB peak of %u KB (%u%%); %u back-offs; %u.%u extra mobys per draw; "
             "near sectors peak %u of 127 (%u times full); moby records peak %u of 53; "
             "largest sector %u bytes, after the world %u bytes; stopped early: world %u, "
             "mobys %u",
             s_st.draws, (unsigned)(s_st.k / d / 256),
             (unsigned)(s_st.k / d % 256 * 100 / 256), s_st.k_min / 256,
             s_st.k_min % 256 * 100 / 256, (unsigned)(s_st.used / d / 1024),
             s_st.used_max / 1024, cap / 1024, s_st.used_max * 100 / cap, s_st.cuts,
             (unsigned)(s_st.extra / d), (unsigned)(s_st.extra * 10 / d % 10), s_st.near_max,
             s_st.near_full, s_st.records_max, s_st.sector_max, s_st.after_env_max,
             s_st.env_cuts, s_st.moby_cuts);
  memset(&s_st, 0, sizeof s_st);
}

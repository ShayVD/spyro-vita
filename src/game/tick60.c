/* 60 Hz logic: the dispatch, on the 30 Hz side (decisions D130; tick60.h,
 * docs/tick60.md). Compiled once, into every build. The 60 Hz rules
 * themselves are src/game/tick60/ and the hunks behind SPYRO_TICK60,
 * built a second time by cmake/tick60.cmake. */
#include "tick60.h"
#include "present.h"
#include "psx_state.h"

#include "common.h"
#include "gamepad.h"
#include "gamestates/update.h"

int g_tick60_mode;
Tick60State g_t60;
Tick60Stats g_t60_stats;
int g_t60_stats_on;

/* draw.c's pacer (`while (pre - post < 2) VSync(0)`): the vblank counts of
 * this draw and the last flip. "HAS to be local to this file" in the decomp;
 * it is a label like the others. */
extern struct {
  int post;
  int pre;
} D_80075950;

#if SPYRO_TICK60_RULES
void GamestateUpdate_60(void); /* tick60.cmake's object: the update path at 60 Hz */
int tick60_built(void) { return 1; }

/* The game's own UpdateMoby of the pilot's levels (Artisans and its
 * levels), which the 60 Hz rules' copies stand in for: t60_rules.c, where
 * these names are its copies, compares g_UpdateMoby with these. */
void UpdateMoby_LEVEL_10(void), UpdateMoby_LEVEL_11(void), UpdateMoby_LEVEL_12(void);
void UpdateMoby_LEVEL_13(void), UpdateMoby_LEVEL_14(void), UpdateMoby_LEVEL_15(void);
void *tick60_game_update_moby(int pIndex) {
  static void (*const k[])(void) = {UpdateMoby_LEVEL_10, UpdateMoby_LEVEL_11, UpdateMoby_LEVEL_12,
                                    UpdateMoby_LEVEL_13, UpdateMoby_LEVEL_14, UpdateMoby_LEVEL_15};
  return pIndex >= 0 && pIndex < 6 ? (void *)k[pIndex] : NULL;
}
#else
int tick60_built(void) { return 0; }
#endif

/* An update runs the 60 Hz rules in play, outside the attract demos (they
 * replay 30 Hz input, §7.2). */
static int wants_60(void) {
  return g_tick60_mode && tick60_built() && g_Gamestate == GS_Playing &&
         g_DemoMode == DEMO_MODE_NONE;
}

static long long now_us(void) { return g_present_clock_us(); }

void PortGamestateUpdate(void) {
  long long t = g_t60_stats_on ? now_us() : 0;
#if SPYRO_TICK60_RULES
  if (wants_60()) {
    /* The 30 Hz tick this vblank belongs to: the vblank that starts one
     * takes its once-a-tick steps (and the 30 Hz parts run, with the
     * vblanks since the last start as their g_DeltaTime, 2..4); the next
     * vblank is its second half. A late update (2+ vblanks) ends the tick
     * it is in, so the next update starts one. */
    g_t60.active = 1;
    g_t60.upper = g_t60.acc;
    g_t60.tick = g_t60.acc == 0;
    g_t60.since += g_DeltaTime;
    if (g_t60.tick) {
      g_t60.tick_delta = g_t60.since < 2 ? 2 : g_t60.since > 4 ? 4 : g_t60.since;
      g_t60.since = 0;
    }
    g_t60.acc += g_DeltaTime;
    if (g_t60.acc >= 2)
      g_t60.acc = 0;
    g_t60_stats.ticks60++;
    g_t60_stats.half_ticks += g_t60.tick;
    g_t60_stats.late += g_DeltaTime > 1;
    GamestateUpdate_60();
    g_t60.active = 0;
    if (g_t60_stats_on)
      g_t60_stats.us60 += now_us() - t;
    return;
  }
#endif
  /* The original's update, at 30 Hz. A save state loaded at the loop top
   * (savestate_point) may have left a 60 Hz delta for another gamestate. */
  g_t60.active = 0;
  g_t60.acc = 0;
  g_t60.since = 0;
  g_t60.spyro_carry[0] = g_t60.spyro_carry[1] = g_t60.spyro_carry[2] = 0;
  g_t60.tick = 1;
  if (g_DeltaTime < 2)
    g_DeltaTime = 2;
  g_t60.tick_delta = g_DeltaTime;
  g_t60_stats.ticks30++;
  {
    int play = g_Gamestate == GS_Playing;
    GamestateUpdate();
    if (g_t60_stats_on) {
      g_t60_stats.us30 += now_us() - t;
      if (play) {
        g_t60_stats.ticks30_play++;
        g_t60_stats.us30_play += now_us() - t;
      }
    }
  }
}

/* main.c's clamp: 2..4 vblanks, as the original's, or 1..4 when the next
 * update is a 60 Hz one; then this tick's draw, which is about to run,
 * waits one vblank since the last flip instead of two (draw.c's play
 * pacer, through its count of the last flip: the play draw's only reader
 * of it, and it sets it again as it flips). The 30 Hz path is the
 * original's: with the setting off nothing here changes a value. */
int PortTickDelta(int pUnprocessed) {
  int lo = 2;
  g_t60.next = 0;
  if (wants_60() && !g_StateSwitch) {
    g_t60.next = 1;
    lo = 1;
    D_80075950.post -= 1;
  }
  if (pUnprocessed < lo)
    pUnprocessed = lo;
  if (pUnprocessed > 4)
    pUnprocessed = 4;
  return pUnprocessed;
}

/* Save states (§7.2): the mode the state was made in, and the half tick.
 * Game state is in the original's units in either mode, so a state loads
 * into either rule set; it switches the mode to the one it was made in,
 * until the next start. */
void tick60_state(struct PsxStateIO *io) {
  int mode = g_tick60_mode;
  psx_state_io(io, &mode, sizeof mode);
  psx_state_io(io, &g_t60.acc, sizeof g_t60.acc);
  psx_state_io(io, &g_t60.since, sizeof g_t60.since);
  psx_state_io(io, g_t60.spyro_carry, sizeof g_t60.spyro_carry);
  if (io->mode == PSX_STATE_LOAD)
    g_tick60_mode = mode;
}

void tick60_report(void (*log)(const char *fmt, ...)) {
  const Tick60Stats *s = &g_t60_stats;
  if (!g_tick60_mode && !g_t60_stats_on)
    return;
  log("tick60: %lld updates at 60 Hz (%lld started a 30 Hz tick, %lld late: 2+ vblanks), "
      "%lld at 30 Hz", s->ticks60, s->half_ticks, s->late, s->ticks30);
  if (g_t60_stats_on && s->ticks60)
    log("tick60: update CPU time per update: 60 Hz %lld us, 30 Hz %lld us",
        s->us60 / s->ticks60, s->ticks30 ? s->us30 / s->ticks30 : 0);
  if (g_t60_stats_on && s->ticks30_play)
    log("tick60: update CPU time per 30 Hz update in play: %lld us (%lld updates)",
        s->us30_play / s->ticks30_play, s->ticks30_play);
}

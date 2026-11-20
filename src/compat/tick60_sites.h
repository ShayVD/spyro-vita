/* 60 Hz logic's sites (decisions D130; docs/tick60.md): the per-tick code
 * the 60 Hz rules change, marked where it is, in the decomp (patches/
 * spyro-1-port/0013-0015) and in our hand C.
 *
 * In every build but the 60 Hz rules' (cmake/tick60.cmake, -DSPYRO_TICK60=1)
 * each macro is its argument's own tokens, so the 30 Hz code preprocesses to
 * the original's text, token for token (tests/tick60/cpp_check.py, ctest
 * tick60_cpp). Included by port_prefix.h (the decomp's files) and by the
 * hand C files that have sites. It defines macros only.
 *
 * The 60 Hz rules keep game state in the original's units: a counter still
 * counts 30 Hz ticks, a speed is still per 30 Hz tick. A site is judged as
 * one of (docs/logic-60hz-audit.md §5):
 *   - a duration or a sequence step taken once a tick: T60_TICK_DO(step),
 *     run on the vblank that starts a 30 Hz tick;
 *     T60_TICK_END_DO(step) on the vblank that ends one, for a step the
 *     original takes after the tick's move (move, then slow down or fall
 *     faster), so the tick's second half still moves by the old velocity;
 *   - a condition that may come true only once a tick (a counter's test
 *     with its step, a random roll, so the odds a second stay the
 *     original's): T60_TICK_AND(condition);
 *   - a counter that a test zeroes every vblank and that is read as "not
 *     zero" (airborne, off the floor): T60_TICK_INC_FROM0(counter, k),
 *     stepped on the vblank it leaves zero too, so it means what it means
 *     in the original on either half of a tick, then once a tick (D160).
 *     k names the counter (T60_FROM0_AIR, T60_FROM0_FLOOR): every site of
 *     it in that vblank steps once the first did, as the original's do;
 *   - Spyro's state transitions, which the original runs once a tick after
 *     the tick's move and collision: T60_SPYRO_TRANSITIONS(call), on the
 *     vblank that ends a tick, seeing the presses of both its vblanks and
 *     the damage flags of both (t60_transitions_begin/end; D160);
 *   - a counter the transitions step by g_DeltaTime (the supercharge's
 *     held cross): T60_TICK_VBLANKS(g_DeltaTime), the vblanks of the tick
 *     they end, not of its last update;
 *   - the pad lockout's test (m_noGamepadUpdateFrames, a count of ticks):
 *     T60_PAD_LOCKED(test), which holds for the tick's second vblank when
 *     its first was locked out, as the original's one test a tick does;
 *   - an amount per tick (a speed, a turn, a drift): T60_SHARE(amount),
 *     this vblank's share of it (half, the two halves summing to it);
 *     T60_SHARE8 for a turn held in a byte (-funsigned-char: 250 is -6).
 * A late update (2+ vblanks) takes a whole tick at once, as the original
 * does. The 60 Hz rules' definitions are in src/game/tick60/tick60_rules.h. */
#ifndef SPYRO_TICK60_SITES_H
#define SPYRO_TICK60_SITES_H
/* T60_TICK_INC_FROM0's counters (g_t60.from0[k]) */
#define T60_FROM0_AIR 0   /* g_Spyro.m_airTime: two sites, pete.c */
#define T60_FROM0_FLOOR 1 /* g_Spyro.m_floorIdleTime, pete_state_hand.c */

#if SPYRO_TICK60
#define T60_TICK_DO(e) (g_t60.tick ? (void)(e) : (void)0)
/* this update ends a 30 Hz tick: a tick's second vblank, or a late update, which is a whole tick */
#define T60_TICK_ENDS (g_t60.upper || g_DeltaTime != 1)
#define T60_TICK_END_DO(e) (T60_TICK_ENDS ? (void)(e) : (void)0)
#define T60_TICK_AND(c) (g_t60.tick && (c))
#define T60_TICK_INC_FROM0(x, k)                                                       \
  ((x) == 0 ? (void)(g_t60.from0[k] = 1, (x)++)                                       \
            : (g_t60.tick || g_t60.from0[k]) ? (void)((x)++) : (void)0)
#define T60_SHARE(x) t60_share(x)
#define T60_SPYRO_TRANSITIONS(call) \
  (T60_TICK_ENDS ? (t60_transitions_begin(), (void)(call), t60_transitions_end()) : (void)0)
/* the vblanks of the tick this update ends: its own and, on a tick's second update, the first's one */
#define T60_TICK_VBLANKS(d) ((d) + g_t60.upper)
#define T60_PAD_LOCKED(c) t60_pad_locked(c)
#define T60_SHARE8(x) t60_share((signed char)(x))
#else
#define T60_TICK_DO(e) e
#define T60_TICK_END_DO(e) e
#define T60_TICK_AND(c) c
#define T60_TICK_INC_FROM0(x, k) x++
#define T60_SHARE(x) x
#define T60_SPYRO_TRANSITIONS(call) call
#define T60_TICK_VBLANKS(d) d
#define T60_PAD_LOCKED(c) c
#define T60_SHARE8(x) x
#endif

#endif

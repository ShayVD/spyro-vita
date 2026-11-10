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
 *   - an amount per tick (a speed, a turn, a drift): T60_SHARE(amount),
 *     this vblank's share of it (half, the two halves summing to it);
 *     T60_SHARE8 for a turn held in a byte (-funsigned-char: 250 is -6).
 * A late update (2+ vblanks) takes a whole tick at once, as the original
 * does. The 60 Hz rules' definitions are in src/game/tick60/tick60_rules.h. */
#ifndef SPYRO_TICK60_SITES_H
#define SPYRO_TICK60_SITES_H

#if SPYRO_TICK60
#define T60_TICK_DO(e) (g_t60.tick ? (void)(e) : (void)0)
#define T60_TICK_END_DO(e) (g_t60.upper || g_DeltaTime != 1 ? (void)(e) : (void)0)
#define T60_TICK_AND(c) (g_t60.tick && (c))
#define T60_SHARE(x) t60_share(x)
#define T60_SHARE8(x) t60_share((signed char)(x))
#else
#define T60_TICK_DO(e) e
#define T60_TICK_END_DO(e) e
#define T60_TICK_AND(c) c
#define T60_SHARE(x) x
#define T60_SHARE8(x) x
#endif

#endif

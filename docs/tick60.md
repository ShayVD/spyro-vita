# 60 Hz logic: the pilot (D130)

The 60 fps option's third value. **Off** is 30 Hz logic at 30 fps; **interpolated** is 30 Hz logic with the presenter's in-between frame (`docs/presentation.md`); **60 Hz** runs a tick in play every vblank. The first two are today's code, compiled from unchanged sources. The third is opt-in and allowed to differ from the PS1. This is the pilot that `docs/logic-60hz-audit.md` §7 proposed: the clock, the second compile, Spyro, the camera, the moby helpers, and the moby classes of one homeworld (Artisans and its five levels), with each class judged by hand. Decision: D130 in `docs/decisions-2.md`.

## Turning it on

| Where | How | When |
|---|---|---|
| linux32 host | `--tick60` (`--tick60-stats` adds the updates' CPU time to the log at exit). Add `--interp` for the in-between frame in the gamestates that stay at 30 Hz | at start |
| Vita | `config.txt` `interp=4`, or the menu's 60 FPS row, value **60 HZ** | read at start: a change in the menu takes effect at the next start |

The setting is chosen at boot (§7.1's fallback). Moving it to a switch at a level load needs §7.1's list of carried-over state checked first. Because the rules keep game state in the original's units (below), that list should be short, but it isn't checked yet.

## What runs at 60 Hz

**Only play** (`g_Gamestate == GS_Playing`) **outside the attract demos.** Every other gamestate runs the original's update at 30 Hz: the pause menu, the inventory, the transitions, the cutscenes, the dragons, the fairy, the balloonist, the title, the credits, and the flight results. So do the attract demos (§7.2: they replay 30 Hz input). On the Vita, `interp=4` gives those gamestates the in-between frame, so the picture stays at 60 fps throughout.

In a 60 Hz update:

| Part | Rate | How |
|---|---|---|
| The clock | 1 vblank an update | `main.c`'s clamp (`PortTickDelta`, 0012): 1..4 before a 60 Hz update, 2..4 otherwise. The play draw's pacer waits one vblank, not two (`PortTickDelta` moves its count of the last flip back by one; `draw.c` is untouched) |
| Spyro | 60 Hz | His physics already sub-stepped per vblank. Per-tick parts now run every vblank, with their counters marked (below) |
| The camera | 60 Hz | Its springs already ran per vblank. Its per-tick counters and look-mode speeds are marked |
| The environment's animations, the fade, the HUD's timers in vblanks | 60 Hz | already scaled by `g_DeltaTime` |
| The moby classes judged for 60 Hz, in the pilot's levels | 60 Hz | 59 class bodies; `src/game/tick60/t60_classes.c` |
| Every other moby class, and every moby in the other levels | 30 Hz | the original's rules, on the vblank that starts a 30 Hz tick, with `g_DeltaTime` 2 |
| Particles, the HUD | 30 Hz | the same, every other vblank (not in the pilot's scope) |
| Sounds (`SoundsUpdate`, `SpuUpdate`) and the XA poll (`CDMusicUpdate`) | 60 Hz | the game's own, every update: positional volumes are recomputed from distances (their smoothing converges sooner), `KillOrphans` on `g_GameTick % 64` runs on both vblanks of that tick (idempotent), the music is polled twice as often (§7.2) |
| The moby animation step | by class | half a step a vblank for a 60 Hz class (`moby_lists_hand.c` `Animate`, `T60_ANIM_LOWER`/`UPPER`, at most one frame a tick as the original), the original's step every other vblank for the rest |

### Units stay the original's

The 60 Hz rules never change what a value means. A counter still counts 30 Hz ticks, a speed is still per 30 Hz tick, and each vblank applies its share. Three things follow from that:
- the two rule sets can hand over at any gamestate change (play → pause → play), and a moby can run at either rate;
- a save state loads into either rule set;
- a class that isn't converted runs exactly the original's code, every other vblank.

A 30 Hz tick is two vblanks. The first **starts** the tick and the second ends it (`g_t60.tick`, `g_t60.upper`; `src/game/tick60.c`). A late update (2+ vblanks) takes a whole tick at once, as the original does at delta 3 and 4.

### The site macros

`src/compat/tick60_sites.h`. Each per-tick site in the update path is marked where it is. In every build but the 60 Hz rules', each macro is its argument's own tokens:

| Macro | For | 60 Hz meaning |
|---|---|---|
| `T60_TICK_DO(step)` | a duration counter, a velocity change before the move, a spawn or sound taken once a tick | runs on the vblank that starts a tick |
| `T60_TICK_END_DO(step)` | a step the original takes **after** the tick's move (move, then slow down or fall faster) | runs on the vblank that ends a tick, so the tick's second half still moves by the old velocity |
| `T60_TICK_AND(cond)` | a condition that may come true only once a tick: a counter's test with its step, a random roll (same odds a second), a test of a once-a-tick counter that triggers a one-shot action | `tick && cond` |
| `T60_SHARE(x)`, `T60_SHARE8(x)` | an amount per tick: a speed, a turn, a phase, a drift (`SHARE8`: a signed delta held in a byte) | this vblank's share: `x >> 1`, then `x - (x >> 1)` |

With the velocity change at the tick's start (or end, as the original orders it) and the move shared, a moby's position at the end of every tick is exactly the original's, given the same inputs. Where a fix needed new code, it's an `#if SPYRO_TICK60 … #else <original> #endif` block (5 in the classes, 1 in `func_8003FE40`'s `>> 6`, the helpers' 7 speed sites, and the loop and animation hooks).

### The second compile

`cmake/tick60.cmake` compiles the update path a second time with `-DSPYRO_TICK60=1` and `-include src/game/tick60/tick60_rules.h`:
- the decomp's `update.c`, `pete.c`, `camera.c` and `moby_helpers.c`;
- our `pete_hand.c`, `pete_dispatch_hand.c`, `pete_transition_hand.c`, `pete_state_hand.c`, `camera_hand.c` and `moby_lists_hand.c`;
- the six pilot levels' `moby_update.c`;
- the rules' own `src/game/tick60/t60_rules.c` and `t60_classes.c`.

`tools/tick60_link.py` links them into one object, `tick60_update.o`:
- **Functions:** every function becomes local except the entry, `GamestateUpdate`, renamed `GamestateUpdate_60`. Inside the object, each call goes to the set's own copy; the game's 30 Hz code never reaches one.
- **Data:** unlike the presenter, the rules write the game's own memory. So every global data definition is weakened (the game's wins).
- **Writable statics:** the tool refuses any of the set's own unless it is listed. Two are, `s_t60_*` (the rules' per-update scratch) and `camera_hand.c`'s `s_tilt` (set and cleared within one call).

The report is `build/*/tick60_update.o.txt`: 157 functions made local, 1 definition weakened, 4 of its own.

The dispatch is `src/game/tick60.c` (`PortGamestateUpdate`), which `main.c` calls in place of `GamestateUpdate` (0012). It picks the 60 Hz object for an update in play, and the original's otherwise. In `t60_rules.c`, `t60_update_moby` compares `g_UpdateMoby` with the game's six pilot-level functions and calls the 60 Hz copy, or runs the game's own at 30 Hz.

### The proof that off and interpolated are unchanged

- **`tools/tick60_cpp_check.py`** (run from the Mac; it needs git) preprocesses the files the pilot touches, as this tree has them and as `origin/main` has them, with build.ninja's own flags. The decomp's five files are checked through every patch; the overlay's `moby_update.c` is checked with the overlay preprocessor's flags. The hand-C files are our six. The token streams must be the same. Result: **11 of 11 files** (`update.c`, `pete.c`, `camera.c`, `moby_helpers.c`, `moby_update.c`, `pete_hand.c`, `pete_dispatch_hand.c`, `pete_transition_hand.c`, `pete_state_hand.c`, `camera_hand.c`, `moby_lists_hand.c`).
- **What changes on the 30 Hz path, by design:**
  - `main.c`'s dispatch (0012);
  - `present.c` (no in-between frame after a 60 Hz update: `&& !g_t60.next`, always true when off);
  - `savestate.c` (a `T60 ` chunk: the mode, the half tick, Spyro's carry);
  - `trace.c` (`--moby-trace`, off unless asked);
  - the Vita's menu row (a fifth value).

## The moby classes: what was judged, and how

Artisans (level 10), Stone Hill, Dark Hollow, Town Square, Toasty and Sunny Flight (11–15) hold **71 classes with update code in 64 case bodies, ~7,400 lines** of `moby_update.c`. Four subagents read every body in batches of ~1,850 lines, from one brief, and judged each per-tick site as a duration, a velocity, a speed, a probability, a spawn or sound, or a sequence step. Their edits are checked mechanically: read at 30 Hz, each `new` must be `old`'s tokens. This session reviewed the risky ones, made the end-of-tick order a macro and converted a helper they all hit (`MobyMoveWithGravity`).

| Verdict | Bodies | Classes |
|---|---:|---|
| 60, with sites | 31 | 10, 16, 17, 23, 33, 34, 49, 110, 113, 114, 115, 120, 121, 165, 174, 250, 300, 309, 329, 335, 339, 349, 362, 390, 391, 392, 395, 502; the chest and basket fragments (69/151/257/425/480); the flight HUD icons (387–389, 393–396, 490–491); the letters (76, 426–451) |
| 60, no sites | 28 | 1, 9, 11, 13, 18, 78, 129, 157, 166, 173, 187, 194, 195, 234, 251, 299, 314, 323, 331–333, 350, 353, 358, 412, 413, 416, 421; the tulips (336/342); the water splashes and bubbles (363/364/400/405/477/507/508) |
| 30 | 5 | 286 ambient sound player (its path step), 308 flight plane, 407 train front, 408 train carriage (all three `func_8003BFC0`, a frame-counted path walker not converted), 398 portal path moby (a script Spyro's 60 Hz side already shares) |

"No sites" means everything per tick goes through what's already right at 60 Hz: the converted helpers, `TICK_TIMER`, `g_DeltaTime`-scaled code, and animation events, which fire once.

### What it cost

| | |
|---|---|
| Lines read | ~7,400 (the 64 bodies) + the 2,447 of `moby_helpers.c` |
| Edits in the classes | 90 edits in 31 bodies: 141 marked sites (74 `SHARE`, 7 `SHARE8`, 34 `TICK_DO`, 14 `TICK_END_DO`, 12 `TICK_AND`), 5 `#if` blocks; **172 changed lines** (`0014-tick60-artisans-mobys.patch`, 298 diff lines with context comments) |
| Per body | 2.2 sites and 2.7 changed lines on average; 0 for 28 of 64; the most, the Egg Thief: 20 lines, 13 edits |
| The helpers | 10 speed sites (7 changed: three helpers pass their speed to helpers that already scale), wander's 4 counters, the path walker's 2 direct turns, the flame heat (2 helpers), the dragon fragment, `MobyMoveWithGravity` (`0013`) |
| Effort | ~30 minutes of wall time for the four batches in parallel, 1,800 agent-seconds, ~0.6 M tokens; then the review, the helper and the re-judging of 6 classes it unblocked |
| Extrapolated to all 218 classes (31k lines) | ~700–800 changed lines, ~600 sites; the audit's §5 estimated 2,000–4,000. The difference is the helpers: once they share by delta, a third of the bodies need nothing |

So **the per-class work is about a quarter of the audit's estimate**. What isn't cheaper is **checking**: no reference exists for the 60 Hz mode (§4), only statistics and play.

### Each class

The subagents' tables, merged (sites by kind; "TICK_DO" and the rest are the macros above):

| class | name | verdict | sites, by kind | lines changed |
|---|---|---|---|---:|
| 1 | Portal text | 60, no sites | none | 0 |
| 9 | Exit vortex | 60, no sites | none | 0 |
| 10 | Sheep fodder | 60 | velocity: `m_0x20 += 0xA` ramp (TICK_DO), `m_0x20 -= 0xF` death deceleration (TICK_DO); speed: `m_0x1c -= turn` / `+= turn` target-angle drift (SHARE); duration: `m_0x2c++` blocked-ticks counter (TICK_DO) | 5 |
| 11 | Waterfall mist emitter | 60, no sites | none | 0 |
| 13 | Gem spawner | 60, no sites | none | 0 |
| 16 | Butterfly | 60 | speed: z drift `m_0x10` (a signed value in a `char`, split by hand as `-256`) ×2 in state 0 and ×2 in state 1 | 4 |
| 17 | Unused flight projectile | 60 | speed: the per-tick step `VecAdd(&position, &moby->m_Position, &props->m_0x00)` split per axis with `T60_SHARE` (in a `#if SPYRO_TICK60` block, since the step is a `VecAdd` call) | 1 → 8 |
| 18 | Sunny Flight entrance stones | 60, no sites | none | 0 |
| 23 | Bull | 60 | velocity: `m_0x14 -= 8` (state 2, T60_TICK_END_DO), `m_0x14 -= 0xF` (state 4, T60_TICK_DO); speed: spin `m_Rotation.z += m_0x18` (state 4) | 3 |
| 33 | Egg Thief | 60 | probability: the two frame-gated sound rolls (`NextFrame == 2` / `== 0xC` and `(rand() & 3) == 0`) → `T60_TICK_AND` on the roll. Duration: `m_0x18++` (ticks spent circling, `>= 0x1E` → substate 1) → `T60_TICK_DO`. Velocity: state 30's ramp `m_0x14 += / -= … | \ |
| 34 | Dragon Egg | 60 | spawn: `g_SpawnParticle(1,12,…)` every update in states 2–4 | 1 |
| 49 | Toasty Entrance Control | 60 | sound / gate: the condition that unpauses the head and calls PlaySound | 2 |
| 69, 151, 257, 425, 480 | Fragments (spring, fireworks chest, large chest, life basket, flight chest) | 60 | speed: x/y/z position by velocity; velocity: gravity `-= 6`; speed: rotation x/y/z by `m_AngularVelocity`; duration: `m_Lifetime--` | 9 |
| 76, 426–451 | Text letters (apostrophe, A–Z) | 60 | speed (phase): `m_Substate += 8` ×3 | 3 |
| 78 | TNT Barrel Flights | 60, no sites | — | 0 |
| 110 | Dragon Pad Fairy | 60 | Duration: state 2 `m_0x08--` (respawn delay 16) and state 4 `m_0x20--` → `T60_TICK_DO`. Sequence: state 5 `m_0x20++` (eased-move step 1..16) → `T60_TICK_DO`. Phase: `m_0x08++` (the bob, states 4/5) → `T60_TICK_DO`. Speed: the fade `m_ScaleOverride -= 4` / … | 8 edits, ~12 lines |
| 113 | Shepherd | 60 | speed: death spin `m_Rotation.z += m_0x10` (SHARE; int, ±2..5); velocity: death deceleration `m_0x0c -= 0x1E` (TICK_DO) | 2 |
| 114 | Gnorc | 60 | turn ×2: `moby->m_Rotation.z += angleDelta;` (states 3 and 5, clamped to ±8) → `T60_SHARE(angleDelta)`; speed: `moby->m_Rotation.y += 4;` (state 7, the fling spin) → `T60_SHARE(4)` | 3 |
| 115 | Club Gnorc | 60 | Velocity: state 6's knockback drag `m_KnockbackSpeed -= 0x10` (after `MobyMove`), at the tick's end (`T60_TICK_END_DO`). Sequence: `m_LaughCounter = 2` → `#if SPYRO_TICK60 … = 1; #else … = 2; #endif`. | 2 edits, ~9 lines |
| 120 | Sparx | 60 | spawn: the two wing-trail `g_SpawnParticle(2, 66, …)` (TICK_DO); speed: glow radius `+= 0x20` (SHARE); speed: butterfly chase step `targetSpeed ± 5` (SHARE, ×2); speed: gem-fetch step 310/290 (SHARE, ×2); speed: idle-follow lerp `pos += (target − pos) >> … | ~16 (one 1-line `VecAdd` became a 9-line `#if` block) |
| 121 | Ram | 60 | speed: spin `m_Rotation.z += m_0x18` (state 2); velocity: `m_0x14 -= 0xF` (state 2, T60_TICK_DO, slows then moves), `m_0x14 -= 5` (state 3, T60_TICK_END_DO: it moves first, then slows; changed by the coordinator); sequence: `else if (m_0x20 == 1)` (the … | 4 |
| 129 | Lamp (Dark Hollow, Dark Passage) | 60, no sites | none | 0 |
| 157 | Bonfire | 60, no sites | none | 0 |
| 165 | Gnorc (Dark Hollow) | 60 | speed: the death spin `m_Rotation.x/.y += m_0x1c/m_0x1e` (SHARE8: shorts in −15..16 added to `u_char`) | 2 |
| 166 | Large Armoured Gnorc | 60, no sites | none | 0 |
| 173 | Key | 60, no sites | — | 0 |
| 174 | Locked Chest | 60 | spawn ×2: the key's trail `g_SpawnParticle(1, 12, keyMoby, …)` (both colours) → `T60_TICK_DO` | 2 |
| 187 | Artisans Balloonist | 60, no sites | — | 0 |
| 194 | Basket | 60, no sites | none | 0 |
| 195 | Metal Chest | 60, no sites | none | 0 |
| 234 | Barrier effect | 60, no sites | — | 0 |
| 250 | Crystal Dragon | 60 | probability ×3: the sparkle rolls `(rand() & 3) == 0` at `(m_ShakeTimer >> 1) == 16/48/80` → `T60_TICK_AND` | 3 |
| 251 | Crystal Dragon fragment | 60, no sites | none | 0 |
| 286 | Ambient sound player | 30 | (if converted later) sequence/speed: substate 1's `if (m_0x24-- > 0) { pos += m_0x18 } else { snap to next node }` | 0 |
| 299 | Flight Chest | 60, no sites | none | 0 |
| 300 | Whirlwind | 60 | probability/spawn: `if ((rand() & 1) == 0) g_SpawnParticle(1, 6, moby, 0);` → `T60_TICK_AND` | 1 |
| 308 | Flight plane | 30 | — | 0 |
| 309 | Large metal fragment | 60 | Speed: x/y/z `+= m_Velocity` → `T60_SHARE`. Rotation: `+= m_AngularVelocity` (Vector3D16, signed short) → `T60_SHARE`, not SHARE8. Velocity: `m_Velocity.z -= 6` → `T60_TICK_DO` (before the z move, so exact). Spawn: the trail particle (3 rand assignments, … | 3 edits, 13 lines |
| 314 | Toasty and Toasty Wood | 60, no sites | none | 0 |
| 323 | Fodder Respawner | 60, no sites | — | 0 |
| 329 | Spring Chest | 60 | spawn: the gem-rise sparkle test `(m_0x04 & 3) <= ((m_0x04 + g_DeltaTime) & 3)` → `#if SPYRO_TICK60` form `T60_TICK_AND((m_0x04 & 3) <= ((m_0x04 + 2) & 3))` | 1 → 7 |
| 331..333 | Dragon pads | 60, no sites | none | 0 |
| 335 | Sheepdog (Toasty) | 60 | velocity: the state-2 hop `moby->m_Position.z += props->m_0x54;` → `T60_SHARE(props->m_0x54)` and `props->m_0x54 -= 0x32;` (taken after the move) → `T60_TICK_END_DO`; the −600 clamp is idempotent | 2 |
| 336, 342 | Tulips (single/double) | 60, no sites | none | 0 |
| 339 | Thief Gnorc | 60 | Speed: states 2–4 `moby->m_Rotation.z += previousAngle;` (clamped ±8) → `T60_SHARE(previousAngle)` | 1 edit, 1 line |
| 349 | Shepherd (Toasty) | 60 | speed: spin `m_Rotation.z += m_0x10` (state 7); velocity: `m_0x0c -= 0x1E` (state 7) | 2 |
| 350 | Sunny Flight entrance control | 60, no sites | none | 0 |
| 353 | Flight arches | 60, no sites | none | 0 |
| 358 | Flight controller | 60, no sites | none | 0 |
| 362 | Train wheel fragment | 60 | speed: `pos.x/.y += vel` (SHARE); velocity: gravity `vel.z -= 6` (TICK_DO; −128 clamp kept); speed: `pos.z += vel.z` (SHARE); speed: spin `m_Rotation.x/y/z += m_AngularVelocity` (SHARE8: the targets are `u_char`, the velocities `short`); duration: … | 7 |
| 363, 364, 400, 405, 477, 507, 508 (body opened by 507) | Water bubbles / splashes | 60, no sites | none | 0 |
| 387–389, 393–394, 396, 490–491 | Flight HUD icons (ring, arch, chest, plane, copter, boat, barrel, lighthouse) | 60 | speed: spin `m_Rotation.z += 8` (states 1, 3, 6, 7); speed (phase): `m_0x10 -= 0x40` (states 3, 6), `m_0x10 += 0x80` (state 7), radius `m_0x0c -= 2` (state 7) | 8 |
| 390 | Fan Chest | 60 | duration: re-boost cooldown `m_0x0c--` (TICK_DO); velocity: fan spin decay `m_0x08--` (TICK_DO) | 2 |
| 391 | Smoke Particle Spawner | 60 | spawn: `!(g_GameTick % 4)` → `T60_TICK_AND` (`g_GameTick` holds its value over two vblanks) | 1 |
| 392 | Fan Chest Fan | 60 | Velocity: `m_0x00.z -= (g_DeltaTime * 5) >> 1` → `#if SPYRO_TICK60 … -= g_DeltaTime == 1 ? T60_SHARE(5) : (g_DeltaTime * 5) >> 1; #else … #endif` | 1 edit, 5 lines |
| 395 | Matador Gnorc | 60 | speed: rotation x/y `+= m_0x0a/m_0x0b` (u_char, signed: SHARE8); speed: `m_Position.z += m_0x06`; velocity: `m_0x06 -= 0xF` (state 3, the death toss, T60_TICK_END_DO) | 4 |
| 398 | Portal Path Moby | 30 | none | 0 |
| 407 | Train Front | 30 | none | 0 |
| 408 | Train Carriage | 30 | none | 0 |
| 412 | Fodder Frog | 60, no sites | none | 0 |
| 413 | Fodder Chicken | 60, no sites | — | 0 |
| 416 | Balloon | 60, no sites | none | 0 |
| 421 | Life Basket | 60, no sites | none | 0 |
| 502 | Chicken Feather | 60 | Speed: x/y/z `+= m_Velocity` and the rock `rot.y += m_0x0e`, `pos.y += m_0x0e`, `rot.z += 12` → `T60_SHARE`. Velocity: `m_Velocity.z -= 5` → `T60_TICK_DO` (before the z move). Duration: `if (--m_0x0d == 0)` → `T60_TICK_AND`; `m_Lifetime--` → `T60_TICK_DO`. … | 4 edits, ~10 lines |

## Measurements

All on the linux32 host (logs in `logs/t60/`, gitignored).

### Spyro, against 30 Hz (`tools/oracle/pads/tick60_moves.txt`, open loop, `tools/tick60_stats.py --spyro-paths`)

- **Open ground (the first 312 vblanks of play: idle, a standing jump, a straight run):** the two runs stay 72 units apart on average, at most 211. The standing jump gains 547 vs 545 units and lasts 38 vs 37 vblanks.
- **The running jump:** height gained +788 vs +778.
- **Running along a wall** is **~12% slower** at 60 Hz (55.6 vs 63.2 units a vblank):
  - Spyro grinds a wall at x ≈ 84,435 here. `UpdateRunning` resets his speed from the wall-corrected acceleration (`PushOffWall`).
  - At 60 Hz the collision updates the wall contact and normal between his two sub-steps; the 30 Hz tick collides once for both.
  - He then reaches the ledge late and the run diverges (a few thousand units by the end). Not fixed in the pilot: it needs `func_8003FE40`'s wall state carried across the tick, as the `>> 6` carry is.
- While running, his z zig-zags by a few units a vblank: floor snaps between half steps. It isn't visible.

### The moby classes, against 30 Hz

`--tour 11,12,13,14,15 --tour-stay 1800` after a new game: Spyro stands still for 1,800 vblanks in Artisans and each of its levels, so both runs see the same situation. Per class, the rate per second of a moby seen (moved distance, height change, turn, state and substate changes, animation frames), 60/30:

| Level | Classes compared | Ratios |
|---|---:|---|
| Artisans | 28 | all 1.00 ± 0.02 but Sparx (moved 0.91, z 1.22) and the fodder respawner (z 1.40) |
| Stone Hill | 20 | 1.00 ± 0.04 but Sparx (z 1.38) and the respawner (z 1.49) |
| Dark Hollow | 18 | 1.00 ± 0.09 (165 Gnorc turn 1.09, Sparx z 1.11) |
| Town Square | 16 | 1.00 ± 0.1, Bull turn 1.20, Sparx z 1.72; a life basket broken in the 30 Hz run only (0.00) |
| Toasty | 9 | 0.91–1.00 |
| Sunny Flight | 5 | 0.94–1.06 (the plane and trains at 30 Hz: 1.00) |

**Sparx's height** follows a target fixed to Spyro's body, so the zig-zag above shows up in its summed height change; its distance moved is 0.91–1.09. **The respawner's** spawned fodder land from a different height (a sample of a few respawns). **The Bull's turn** is a clamped turn: at 60 Hz the last unit or two lands a vblank later. None of these ran twice as fast or half as fast, which is what a missed site would give.

### The game thread's cost

`--tick60-stats --present-stats` on the tour (qemu-i386 on the Mac, CPU time, so compare the two runs, not with the Vita):

| | 30 Hz | 60 Hz |
|---|---:|---|
| Update, per update in play | 468 µs | 406 µs |
| Update, per second of play | 14.0 ms | 24.4 ms (×1.74) |
| Logic draw, per update | 18.6 ms | 18.8 ms (twice as many a second) |

The update is a small part of the cost. The draw runs every vblank, which doubles the game thread's draw time a second, as the audit's §7.3 expected (Toasty about 43% busy on the Vita, an estimate). The proposed hardware runs measure it.

## Known differences and open items

- **Spyro:**
  - running along a wall (above);
  - the coyote time, air time and the like count 30 Hz ticks, so they're exact; jump heights match.
- **Classes:**
  - Decelerations guarded by a threshold (Bull state 4, Ram state 2, Shepherd state 7, sheep fodder, Shepherd) can lose half a move step once, on the tick that crosses it.
  - Fragments are freed one vblank early (their lifetime reaches 0 at a tick's start).
  - The fairy's eased moves and the clamped turns (Gnorc, Thief Gnorc, Bull) converge a vblank later.
  - Club Gnorc's laugh counter (`TICK_TIMER` gated by an animation event) is 1 at 60 Hz for the original's 2.
  - The Fan Chest Fan's gravity takes 5 a tick split 2 + 3, where the delta form would give 2 + 2.
- **At 30 Hz in a 60 Hz update** (correct speed, 30 fps motion):
  - particles;
  - the HUD;
  - the five classes above;
  - every moby in the 31 levels outside the pilot (their classes were never judged).
  - Cutscene cameras stay recorded at 30 Hz (§7.2); the cutscenes themselves are 30 Hz gamestates.
- **Not done:**
  - the switch at a level load (§7.1);
  - the 152 other classes;
  - particles and spawns;
  - a 60 Hz copy of the HUD;
  - a Vita3K run of the mode (the hardware runs are below).

## Checks (2026-11-10, this branch)

- **Off, at 30 Hz:**
  - `tools/host_checks.sh` against main `bb62f38`'s binary (built in this worktree first): **279 of 279 files** byte-identical (demo 26, the tour 141, routes A and B 56 each). Main's demo was run again with no memory card present: 26/26.
  - The oracle replay (D127's `s8e-cap` set): **75 PASS, 0 FAIL, 20 NOEXIT** on both binaries, summaries identical.
  - `tools/tick60_cpp_check.py`: 11 of 11.
  - ctest: 28 passed and `gte` skipped of 29, as on main (`savestate_globals` now also checks the 60 Hz object).
- **Interpolated:** `tools/pres_check_ab.sh MODE=chk` (demo, wsi, gems, routes A and B) against main's binary: **31,229 of 31,229 frames identical** (demo 6,169, wsi 6,169, gems 4,553, routes A and B 6,669 each; every gamestate they reach), and **183 of 183 files** byte-identical to main's runs (`logs/t60pc-*`, `logs/t60/pc.out`).
- **60 Hz, host:** the runs above all reach their `--frames` with exit 0. With `--tick60 --interp --pres-check --widescreen` on the gems script, `--pres-check` compares 5,763 frames, 5,763 identical, 2,420 of them 60 Hz play frames (`logs/t60/pc60`).
- **Vita:** the VPK builds (VitaSDK, native memory) and the link check passes on ARM (157 functions made local; its 3 extra "own" entries are ARM section anchors); `.text` 3,814,684 → 4,018,820 bytes, +204 KB for the second compile. Not run in Vita3K: Vita3K was busy with another project's queued runs during this session.

## Hardware runs (2026-11-10, the owner's yes in chat)

The VPK `logs/hw-t60/t60.vpk` (SHA-1 `8c51c866…`, eboot `8bc2750d…`), with `REALTIME=1` and `DEVICE_DEFAULTS=1`, on the owner's Vita. Nobody held the kit's lock. Every run reached `frames=` with exit 0, no hang line and no crash dump. Logs are gitignored, under `logs/hw-t60-{1,2,3}`. The configs are `tools/vita/fps-runs/t60-*.txt`, with each command in its file's header.

| | 7G1 (main, interpolated) | **T60-2** (this VPK, interpolated) | **T60-1** (this VPK, 60 Hz) |
|---|---:|---:|---:|
| Game thread, ms a vblank (the route from vblank 900) | 3.30 | 3.31 | 4.14 |
| The four renderers, ms a vblank | 1.97 | 1.98 | 2.89 |
| Late vblanks per 300 | 1.12 | 1.16 | 1.02 |
| Falls to 30 fps | 0 | 0 | 0 |
| Toasty: busy % (max) | 24.9 (33) | 25.0 (33) | **48.2 (62)** |
| Toasty: late per 300 | 0.0 | 0.0 | 0.0 |
| Toasty: GPU ms a picture | 6.28 | 6.35 | 6.79 |
| Updates at 60 Hz (late) | — | — | 7,233 (7) |

| The K route | 7G2 (main, interpolated) | **T60-3** (this VPK, 60 Hz) |
|---|---:|---:|
| Game thread, ms a vblank | 3.85 | 5.08 |
| The four renderers, ms a vblank | 2.37 | 3.76 |
| Late vblanks per 300 | 1.05 | 1.03 |
| Falls to 30 fps | 0 | 0 |
| Busy % (max): title | 19.4 | 19.6 |
| Artisans | 22.0 (38) | 26.5 (67) |
| Stone Hill (its attract demo at 30 Hz in some windows) | 38.1 (76) | 52.4 (76) |
| Dark Hollow | 38.8 (39) | **69.9 (70)** |
| Late per 300 in Stone Hill and Dark Hollow | 0.1, 0.0 | 0.0, 0.0 |
| Updates at 60 Hz (late) | — | 7,253 (3) |

How to read these:
- **T60-2 is 7G1 within noise** in every game-thread figure. The 60 Hz rules compiled into the VPK cost nothing when they are off.
  - Its render thread's ms a picture in Toasty, 14.2 against 10.0, includes the thread's waits for the GPU and the display. Its GPU time and late vblanks are unchanged.
- **At 60 Hz the game thread is busy about 1.3× to 1.8× as much in play as interpolated.** Toasty goes from 25% to 48%, Dark Hollow from 39% to 70%. That's above the audit's estimate of ~43%, because every play vblank is now a whole draw.
- **60 fps still holds.** No late vblank in the pilot's levels, no fall to 30, and only 3 to 7 updates of 2+ vblanks over ~7,200.
- **Dark Hollow at 70% leaves the least headroom measured.** The levels outside the pilot run their mobys at 30 Hz but still draw every vblank, so they cost about the same.

At the owner's request, this VPK's eboot stays installed on the Vita (`8bc2750d…`, read back after T60-3); the owner's earlier one (`66f57eff…`) is in `logs/hw-t60/eboot-before.bin`.

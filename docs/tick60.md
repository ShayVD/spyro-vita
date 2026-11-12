# 60 Hz logic (D130, D144)

The 60 fps option's third value. **Off** is 30 Hz logic at 30 fps; **interpolated** is 30 Hz logic with the presenter's in-between frame (`docs/presentation.md`); **60 Hz** runs a tick in play every vblank. The first two are today's code, compiled from unchanged sources. The third is opt-in and allowed to differ from the PS1. It started as the pilot that `docs/logic-60hz-audit.md` §7 proposed: the clock, the second compile, Spyro, the camera, the moby helpers, and the moby classes of one homeworld (Artisans and its five levels), with each class judged by hand (D130 in `docs/decisions-2.md`). D144 judged every other class the same way, so every moby class with code in `moby_update.c` but one runs at 60 Hz in every level ("Every class", below).

## Turning it on

| Where | How | When |
|---|---|---|
| linux32 host | `--tick60` (`--tick60-stats` adds the updates' CPU time to the log at exit). Add `--interp` for the in-between frame in the gamestates that stay at 30 Hz | at start |
| Vita | `config.txt` `interp=4`, or the menu's 60 FPS row, value **60 HZ** (the default since D150) | read at start. A change in the menu into or out of 60 HZ asks: **RESTART NOW** (the options saved, the app started again) or **UNDO** (the row put back); ○ leaves it for the next start, and a **60 HZ LOGIC** row says ON AT RESTART or OFF AT RESTART until then (D136) |

**Which discs** (D150): the US disc and the PAL disc, which runs at US timing (PAL 60, D138). PAL's own code in what the rules compile (its `REGION_IS(PAL)` blocks and `RV()` sites in `moby_update.c`, `update.c`, `pete.c`, `moby_helpers.c` and the hand C) was judged as the classes were: one site needed marking, `MobyMoveWithGravity`'s wall-slide slowdown (`patches/spyro-1-tick60/0003`). Not PAL at its own 50 Hz (the host's `--pal50`, the Vita's `pal50=1`: the rules' shares assume 30 Hz ticks) and not JP (not supported yet); there the option runs as interpolated.

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
| The moby classes, in every level | 60 Hz | 215 of the 216 class bodies of `moby_update.c`: the pilot's 59 (D130) and 156 more (D144), among them four the pilot left at 30 and sixteen whose GCC range labels (`case 83 ... 87:`, the gems) its count had missed; `src/game/tick60/t60_classes.c`. A class's body is the same in every overlay, which differ only in the classes they keep |
| The portal path moby (398) | 30 Hz | the original's rules, on the vblank that starts a 30 Hz tick, with `g_DeltaTime` 2: it steers Spyro through a portal, and its hold on him lasts the tick (D141) |
| Particles, the HUD | 30 Hz | the same, every other vblank (not in the pilot's scope) |
| Sounds (`SoundsUpdate`, `SpuUpdate`) and the XA poll (`CDMusicUpdate`) | 60 Hz | the game's own, every update: positional volumes are recomputed from distances (their smoothing converges sooner), `KillOrphans` on `g_GameTick % 64` runs on both vblanks of that tick (idempotent), the music is polled twice as often (§7.2) |
| The moby animation step | by class | half a step a vblank for a 60 Hz class (`moby_lists_hand.c` `Animate`, `T60_ANIM_LOWER`/`UPPER`, at most one frame a tick as the original), the original's step every other vblank for the rest |

### Scripts that move Spyro (D141)

A moby that drives Spyro (a portal's path, an exit vortex, a whirlwind, a ride, a cutscene's pad lock) sets `m_ControlFlags` with bit 31 in its update, and a vortex or whirlwind also sets `m_DamageFlags` 0x800 (its lift: state 17). Spyro's update (`func_8004A200`) lets go of bit 31 and clears the damage flags each time; the moby sets them again in its next update, which runs before his. In the 60 Hz rules **a hold lasts the 30 Hz tick** (`0015-tick60-script-hold.patch`, `t60_rules.c`):
- `t60_spyro_release` lets go of bit 31 on the vblank that ends a tick, or after a late update, and keeps it for the tick's second half;
- `t60_spyro_damage_end` clears the damage flags on the vblank that ends a tick and, on the first, keeps only 0x800; one-shot hits are still taken once a tick.

A moby at 30 Hz sets them once a tick. Without this the second vblank found bit 31 clear, `func_8003FE40` zeroed the flags and Spyro was the player's again for half of every tick: a portal never carried him in and its camera kept starting its path over (the owner's Gnorc Gnexus report), and a vortex's lift dropped him from state 17 to 15 every other vblank, so it never took him home. A tick cut short by a gamestate change (the portal's or vortex's gamestate 1, the pause menu) lets go of both in `PortGamestateUpdate`, so the 30 Hz update after it finds Spyro as the original's tick left him. No moby reads `g_Spyro.m_ControlFlags`, so a 60 Hz class sees no difference.

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
- every level's `moby_update.c` (35 overlays, `src/game/tick60/t60_levels.h`; the six pilot levels' only, before D142), with the class sites of `patches/spyro-1-port/0014` (the pilot's) and `patches/spyro-1-tick60/0001` (D144). The `spyro-1-tick60` series is applied last, after the region patches, so its edits are written on PAL's `RT()` text and the region patches stay as they are;
- the rules' own `src/game/tick60/t60_rules.c` and `t60_classes.c`.

`tools/tick60_link.py` links them into one object, `tick60_update.o`:
- **Functions:** every function becomes local except the entry, `GamestateUpdate`, renamed `GamestateUpdate_60`. Inside the object, each call goes to the set's own copy; the game's 30 Hz code never reaches one.
- **Data:** unlike the presenter, the rules write the game's own memory, so the tool refuses a global data definition in the set: each global the set uses is the game's, through an extern or a common symbol. Until D142 it weakened them instead, so that the game's definition would win at link time. That holds on x86 but not on ARM, where GCC reaches a global defined in the same file through a section anchor (the section's base plus an offset), never through its symbol. So the Vita's 60 Hz camera read its own copy of `g_port_controls`, which nothing wrote, and the free camera was off at 60 Hz whatever the menu said. `g_port_controls` is now defined in `port.c`, outside the set.
- **Writable statics:** the tool refuses any of the set's own unless it is listed. Two are, `s_t60_*` (the rules' per-update scratch) and `camera_hand.c`'s `s_tilt` (set and cleared within one call).

The report is `build/*/tick60_update.o.txt`: 189 functions made local (157 at D130; D141's two, and since D142 every level's `UpdateMoby`), 5 of its own, no data defined.

The dispatch is `src/game/tick60.c` (`PortGamestateUpdate`), which `main.c` calls in place of `GamestateUpdate` (0012). It picks the 60 Hz object for an update in play, and the original's otherwise. In `t60_rules.c`, `t60_update_moby` compares `g_UpdateMoby` with the game's `UpdateMoby` of each level in `t60_levels.h` and calls that level's 60 Hz copy, where every class judged for 60 Hz runs every vblank (before D144 only in the pilot's levels, and only Sparx elsewhere, D142). A level not in the list (none today) runs the game's own at 30 Hz.

### The proof that off and interpolated are unchanged

- **`tools/tick60_cpp_check.py`** (run from the Mac; it needs git) preprocesses the files the pilot touches, as this tree has them and as `origin/main` has them, with build.ninja's own flags. The decomp's five files are checked through every patch; the overlay's `moby_update.c` is checked with the overlay preprocessor's flags. The hand-C files are our six. The token streams must be the same. Result: **11 of 11 files** (`update.c`, `pete.c`, `camera.c`, `moby_helpers.c`, `moby_update.c`, `pete_hand.c`, `pete_dispatch_hand.c`, `pete_transition_hand.c`, `pete_state_hand.c`, `camera_hand.c`, `moby_lists_hand.c`).
- **What changes on the 30 Hz path, by design:**
  - `main.c`'s dispatch (0012);
  - `present.c` (no in-between frame after a 60 Hz update: `&& !g_t60.next`, always true when off);
  - `savestate.c` (a `T60 ` chunk: the mode, the half tick, Spyro's carry);
  - `trace.c` (`--moby-trace`, off unless asked);
  - the Vita's menu row (a fifth value).

## The moby classes: what was judged, and how (the pilot, D130)

Artisans (level 10), Stone Hill, Dark Hollow, Town Square, Toasty and Sunny Flight (11–15) hold **71 classes with update code in 64 case bodies, ~7,400 lines** of `moby_update.c`. Four subagents read every body in batches of ~1,850 lines, from one brief, and judged each per-tick site as a duration, a velocity, a speed, a probability, a spawn or sound, or a sequence step. Their edits are checked mechanically: read at 30 Hz, each `new` must be `old`'s tokens. This session reviewed the risky ones, made the end-of-tick order a macro and converted a helper they all hit (`MobyMoveWithGravity`).

| Verdict | Bodies | Classes |
|---|---:|---|
| 60, with sites | 31 | 10, 16, 17, 23, 33, 34, 49, 110, 113, 114, 115, 120, 121, 165, 174, 250, 300, 309, 329, 335, 339, 349, 362, 390, 391, 392, 395, 502; the chest and basket fragments (69/151/257/425/480); the flight HUD icons (387–389, 393–396, 490–491); the letters (76, 426–451) |
| 60, no sites | 28 | 1, 9, 11, 13, 18, 78, 129, 157, 166, 173, 187, 194, 195, 234, 251, 299, 314, 323, 331–333, 350, 353, 358, 412, 413, 416, 421; the tulips (336/342); the water splashes and bubbles (363/364/400/405/477/507/508) |
| 30 | 5 | 286 ambient sound player (its path step), 308 flight plane, 407 train front, 408 train carriage (all three `func_8003BFC0`, a frame-counted path walker not converted), 398 portal path moby (a script Spyro's 60 Hz side shares: its hold on him lasts the 30 Hz tick, D141) |

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

## Every class (D144)

The owner tested Sparx's fix (D142) and found the enemy they chased for a key stuttering: outside the pilot's levels every moby but Sparx still ran at 30 Hz. D144 runs every class at 60 Hz in every level but one.

### How

- **Step A:** the pilot's 59 class bodies are the same in every overlay (an overlay keeps a subset of `moby_update.c`'s classes; no `IS_LEVEL_*` switch is in a body), so their judgment holds everywhere. `t60_levels.h` lost its pilot column and `t60_rules.c` its `s_t60_pilot`: `t60_class_60` decides alone.
- **Step B:** the other 157 bodies (~25,000 lines) were judged from one brief by twelve subagents, a batch each (~1,500–2,700 lines, by homeworld; Gnasty's World first). Each wrote its edits as data (`old`, `new`, the class), and a checker applied them only inside that class's body, refusing any whose `new`, read at 30 Hz (macros as their arguments, `#if SPYRO_TICK60` blocks as their `#else`), isn't `old`'s tokens. 261 edits; `tools/tick60_cpp_check.py` then proves the whole file.
- **The pilot's count had missed GCC range labels** (`case 83 ... 87:`): 16 bodies, among them the gems, the life orb, the digits and the Tree Tops thieves, which ran at 30 Hz even in Artisans. A twelfth batch judged them.
- **The edits are a new patch series, `patches/spyro-1-tick60/`, applied after the region patches** (`CMakeLists.txt`, `tools/tick60_cpp_check.py`), so they are written on PAL's `RT()` text: the region patches and their retune sites are untouched. `0001-tick60-all-mobys.patch` is the classes (213 hunks); `0002-tick60-path-helpers.patch` the helpers below.

### Helpers converted centrally

The batches found four helpers that move or turn by a fixed amount a call, with no delta. Converted once, in the 60 Hz build only, so no call site shares their amounts (the batches' four call-site shares were dropped in the merge):

| Helper | Callers | 60 Hz form |
|---|---|---|
| `func_8003BFC0`, the frame-counted path step (`moby_helpers_hand.c`) | trains, planes, boat Gnorcs, the Thief Rocket, Blowhard, Jacques, the Timer Fool, Lofty fairies | `t60_path_step` (`t60_rules.c`; `tick60_rules.h` renames every call in the set): the frame is taken whole on a tick's first vblank (its counter, node, return value and heading), the moby moved by half of it and the rest kept in `g_t60.path`; `t60_moby_begin` moves the rest before the moby's second-vblank update, whether or not its class calls the step again, and a second call in the tick returns 0. One step a tick a moby. A 30 Hz class takes the original's step |
| `MoveMobyAlongPath` | 227 Kiss fairy, 285 Ice Wizard, 304 Tornado | the turn shared; the smoothed velocity (`pVelocity`, ¾ of velocity + step) updated once a tick with the tick's whole step, and each vblank moving its share of it; without `pVelocity`, the step shared |
| `func_8003A16C` | 285 Ice Wizard | the step and the turn shared |
| `MobyMoveTowardTarget` | 62 Puffer Bird, 213 Ice Cavern bat | the step and both turns shared |

And one on Spyro's side, which the pilot hadn't met: pushing a cannon (`pete_hand.c` `UpdateCircleMoby`, state 26) turned it and Spyro by a tick's step every call. Its step is shared and its speed-up taken once a tick.

### Verdicts

| Verdict | Bodies |
|---|---:|
| 60, with sites | 104 (two of them through a converted helper only) |
| 60, no sites | 52 |
| 30 | 1: 398, the portal path moby (D141: it aims Spyro, and run every vblank it would aim him from his half-moved position) |

Sites in `0001`, by macro: `T60_SHARE` and `T60_SHARE8` for the moves, turns and spins, `T60_TICK_DO` and `T60_TICK_END_DO` for velocities before and after the move, durations and spawns, `T60_TICK_AND` for rolls and `g_GameTick` gates, and 21 `#if SPYRO_TICK60` blocks where a call or a loop needed new code. Four bodies take a whole tick's step on the tick's first vblank instead of sharing it, because the step can't be split without new state: the Fireworks Rocket's flight (402), the storm cloud's outward step (37, its distance from `OctDistance`), the ambient sound emitter's path (286, invisible) and the cannon shot's tracer points (104). They run at 60 Hz, but move in 30 Hz steps there.

Moves that hold Spyro were each looked at (D141): the classes that set `m_ControlFlags` bit 31 every update while they hold him (Gnasty, the Thief Rocket, the thieves, Jacques, Doctor Shemp, the Fat Lady, the Dragon-Eating Plant, the cannons, Metalhead, the Hog) set it on both vblanks, and his side lets go at the tick's end. The High Caves fairies (109) pull him a share of the way each vblank; the big devil dog's carry (50) is set once a tick.

### Each class (D144)

The batches' tables, merged (classes; the body's name; the verdict; its sites; edits):

| classes | name | verdict | sites | edits |
|---|---|---|---|---:|
| 14–15, 83–87 | Life statue and orb / Gems | 60 | speed: substate 2's move cap RT(220,264) (SHARE, test and length); speed: substate 3's spin (r - 7) x3 (#if block, SHARE8); speed: the life orb's spin rot.z += RT(8,10), rot.y -= RT(6,7) (SHARE) | 3 |
| 28 | Misty Bog Gnorcs | 60 | speed: state 10 sink m_Position.z -= 100 (SHARE) | 1 |
| 32 | Laser effect for Large Gnorc in Terrace | 60, no sites | none | 0 |
| 35 | Gunner Gnorc | 60 | speed: state 3 death tumble m_Rotation.x/.y += (u_char)m_0x2c/m_0x2e (SHARE8) | 1 |
| 36 | Magic Crafters Mountain Goat Fodder | 60, no sites | none: MobyRotateToAngle + func_80039398 (converted), path node choice is an event | 0 |
| 37 | Blowhard Storm Cloud | 60 | sequence: substate 1's outward step (distance = OctDistance + m_0x04, re-derived each update) held to the tick's first vblank (#if gate); speed: substates 2/3 x/y step FIXED_MUL(m_0x04, cos/sin) (SHARE of the whole product) | 2 |
| 38 | Storm Wizard Lightning | 60, no sites | none: TICK_TIMER, MobyMoveForward (converted); the trail `!(m_0x00 & 7)` reads a TICK_TIMER counter | 0 |
| 39 | Blowhard Storm Cloud Lightning | 60, no sites | none: position copied from its cloud, rotation from the camera (absolute), TICK_TIMER sound (m_0x08 = 1 then 99999: once) and lifetime | 0 |
| 45–46 | Armored Horrors and Giant Pansies | 60, no sites | none | 0 |
| 47 | Gift projectile | 60 | speed: state 9 x/y by FIXED_MUL(m_0x04, cos/sin), z by m_0x08, tumble rot.x/.y by m_0x01/m_0x02 (SHARE) | 1 |
| 48 | Metalhead | 60 | probability: state 0 attack pick while m_0x20 == 0 (TICK_AND); spawn: state 2 zap every update in frames 5..20 (TICK_AND) with its m_0x24 += g_DeltaTime made the tick's vblanks (#if, g_t60.tick_delta); spawn: state 3 fireball test (TICK_AND, makes the delta… | 4 |
| 50 | Devil dog (big) | 60 | Spyro's carry: state 1's m_portalEndPos lerp point (TICK_DO on its VecAdd). Everything else: TICK_TIMER, MobyRotateToSpyro/ToAngle, MobyMove, func_80039398 (converted), one-shot state changes, ColMoby | 1 |
| 51 | Devil dog (small) | 60, no sites | none | 0 |
| 52 | Cupid | 60 | velocity: state 2 m_0x08 -= RT(0x10,0x13) after MobyMove (TICK_END_DO) | 1 |
| 53 | Armored Turtle (big) | 60, no sites | none | 0 |
| 54 | Armored Turtle (small) | 60 | speed: state 2 m_Rotation.y += 4 (SHARE) | 1 |
| 55 | Cupid Arrow | 60 | spawn: trail g_SpawnParticle(1, 28, ...) (TICK_DO) | 1 |
| 56 | Bat projectile (PROTOTYPE) | 60, no sites | none | 0 |
| 57 | Banana projectile | 60 | velocity: state 0 z velocity from the floor (TICK_DO on its assignment; clamps idempotent); sequence: state 0 m_0x0a >= RT(0xB,9) floor-follow cutoff (TICK_AND); speed: state 0 pos x/y/z += vel (SHARE), rot x/y/z += spin (SHARE8, u_char); state 1 rot x/y/z … | 5 |
| 61 | Lofty Castle Fairy Cage | 60 | velocity: m_FallSpeed += 12 (TICK_DO, before the fall); speed: the fall m_FallSpeed (SHARE in the floor search range and in the z step) | 3 |
| 62 | Puffer Bird | 60 (helper, 0002) | speed: state 0 pitch levelling rot.y += angleDelta (clamped +-2, SHARE) | 1 |
| 65 | Metalhead poles | 60, no sites | none | 0 |
| 66 | Snowman Gnorc | 60 | velocity: state 5 death slide `m_0x04 -= RT(0x10, 0x13)` after MobyMove (TICK_END_DO) | 1 |
| 70 | Metalhead pole effect | 60 | speed: x/y FIXED_MUL(m_0x00, cos/sin) (SHARE); speed: z += m_0x04 (SHARE); velocity: m_0x04 += m_0x08 after the move (TICK_END_DO) | 2 |
| 77, 168 | Dream Weavers Cannon Effect / Gnasty Projectile Effect | 60 | duration: m_Substate-- (the start delay the animation is held at frame 0; TICK_DO) | 1 |
| 82 | Metalhead Zap Projectile | 60 | speed: VecAdd(pos, pos, m_0x00) split per axis with SHARE (#if block) | 1 |
| 88 | Lofty fairies | 60 | speed: z approach += zDelta (clamped +-0x40) x2 (SHARE); speed: state 4 rise (m_0x20 - 0x20) >> 3 (SHARE); spawn: sparkle g_SpawnParticle(2, 66, ...) every update (TICK_DO) | 4 |
| 91 | Dream Weavers Cannon | 60 | spawn: state 2's 5-particle burst while m_0x10 >= 0xB (TICK_AND on the test); placement: state 3's func_80039398(projectile, 600, ...) spawn offset (#if: 1200 at delta 1, which the converted helper halves back to 600) | 2 |
| 92 | Carrot-Topped Monk | 60 | speed: death spin `m_Rotation.z += m_0x54` (SHARE); duration: `m_Substate++` (TICK_DO); velocity: spin decay `m_0x54--`/`++` after the spin (TICK_END_DO x2) | 3 |
| 67–68, 93, 133, 215, 255–256, 423–424, 478–479 | Spring chest fragments / Metal fragment / Door fragment / Tin soldier fragment / Chest fragments / Life basket fragments / Flight chest fragments | 60 | speed: x/y/z += m_Velocity (SHARE x3); velocity: m_Velocity.z -= RT(6, 7) before the z move (TICK_DO); speed: rotation += m_AngularVelocity (short: SHARE x3); spawn: the smoke at !(m_Lifetime & 3) (TICK_AND); duration: m_Lifetime-- (TICK_END_DO) | 3 |
| 100, 102 | Banana Boy / Banana Boy (armored) | 60, no sites | none: ApplyFlameHeat, MobyMoveWithGravity, func_80039E94, MobyRotateToSpyro/ToAngle (converted); state 41's func_80038638 (orbit) passes its speed and turn to MobyMove/func_80039398/MobyRotateToAngle, which share, and its target angle (+-3) is recomputed fr… | 0 |
| 101, 170 | Strongarms / Strongarms (Metalhead) | 60, no sites | none | 0 |
| 104 | Dream Weavers Cannon target? | 60 | speed/velocity/sequence: the do-while flight step (#if block: catch-up steps whole, own step SHARE, gravity m_0x0a += m_0x0e and step budget m_0x00 at TICK_END); spawn: tracer point (#if block: tick end or stop) | 2 |
| 109 | High Caves fairies | 60 | speed: Spyro's pull target m_portalEndPos = Spyro + share(gap/4, at most 0x200) (#if block replacing the VecAdd); speed: Spyro's turn m_portalAngle.z = body + share(clamp +-4 / +-3) (x2); speed: the leader's z steps/lerps in bits 4, 0x10, 0x20, 0x40 (x2) an… | 11 |
| 122 | Lamp Fool | 60 | speed: glow m_Radius += 0x20 and -= 0x40 (SHARE) | 2 |
| 124 | Lamp Fool transition | 60, no sites | none | 0 |
| 126 | Timer Fool | 60 | speed: thrown x/y FIXED_MUL(m_0x58) (SHARE), z += m_0x5c (SHARE); velocity: m_0x5c += m_0x60 after the move (TICK_END_DO); speed: state 8 sink z -= 0x40 (SHARE) | 3 |
| 130 | Slap-Happy Armored Monk | 60 | speed: death spin rot.z += m_0x5c (SHARE); duration: m_Substate++ (TICK_DO); velocity: spin decay m_0x5c-- / ++ after the spin (TICK_END_DO) | 4 |
| 134 | Gnorc-Adier | 60, no sites | none | 0 |
| 135 | Gnorc-Adier grenade | 60 | velocity: m_0x04 += m_0x0c (TICK_DO, before the move); spawn: trail g_SpawnParticle(1, 1, ...) (TICK_DO); speed: x/y/z += m_0x00/02/04 (SHARE), tumble rot += m_0x10..12 (SHARE8) | 3 |
| 136 | Door | 60, no sites | none | 0 |
| 137 | Hog | 60 | speed: state 2 death spin m_Rotation.z += m_0x28 (SHARE8; 0 in practice); velocity: state 2 m_0x24 -= RT(0xF,0x12) before MobyMove (TICK_DO), state 3 m_0x24 -= RT(0x1E,0x24) after func_80039398 (TICK_END_DO); sequence: state 3 else if (m_0x30 == 1) (TICK_AND) | 4 |
| 142, 227, 241 | Kiss Fairy / Gold Kiss Fairy / Kiss Fairy (High Caves) | 60 | speed: z by heightDifference (+-0x32) in state 1 and state 2 (SHARE x2); sequence/spawn: substate 1's sparkle counter m_0x24++ and spawn from frame 6 (TICK_AND on the frame test); speed: state 3 MoveMobyAlongPath maxSpeed RT(150, 180) and turnRate RT(6, 7) … | 4 |
| 143 | Gnasty Laser Explosion | 60, no sites | none: growth g_DeltaTime*14, TICK_TIMER, hitbox test | 0 |
| 145 | Machine Gunner | 60 | speed: state 20 sink z -= 100 (SHARE) | 1 |
| 146 | Peacekeepers Tent | 60, no sites | none | 0 |
| 147 | Gnorc Commando | 60 | speed: aim turn func_800179F0(.., 5, 3) (SHARE at the call, as the pilot); speed: sweep m_0x2c += m_0x28 (SHARE); spawn: g_GameTick & 1 bullet gate (TICK_AND); speed: state 20 sink (SHARE) | 4 |
| 148 | Gnorc Survivalist | 60 | speed: state 20 sink (SHARE) | 1 |
| 150 | Jacques | 60, no sites | none | 0 |
| 152–153 | Fireworks Chest Fragments | 60 | duration: m_Substate-- (TICK_DO); speed: VecAdd(pos, vel) (#if block, SHARE per axis); velocity: vel.z -= RT(12,15) after the move (TICK_END_DO); speed: spin += Vector3D8 (SHARE8 x3); spawn: the trail on !(m_Substate % 2) (TICK_AND) | 4 |
| 154 | Barrel Dispenser | 60, no sites | none: ApplyFlameHeatExternal converted, spawn on damage is one-shot (state 2) | 0 |
| 155 | Turtle Projectile | 60 | spawn: trail g_SpawnParticle(1, 1, ...) (TICK_DO) | 1 |
| 156 | Bullet Projectile | 60 | speed: VecAdd(pos, pos, velocity) split per axis with SHARE in an #if SPYRO_TICK60 block | 1 |
| 159 | Key Thief (Gnasty) | 60 | probability: 2 frame-gated sound rolls (TICK_AND); velocity: m_0x10 -= 10 run slow-down under an expired TICK_TIMER (TICK_DO); velocity: gravity m_0x1c -= 0x1E (TICK_DO), floor test and z move (SHARE) | 4 |
| 160 | Mushroom Fodder | 60 | velocity: knockback m_KnockbackSpeed -= RT(15, 18) before MobyMove (TICK_DO) | 1 |
| 161 | Gnasty Gnorc | 60 | velocity x6 (states 4, 5, 6 twice, 11, 12): gravity m_0x1c -= 0x19 (TICK_DO), floor test and z move by m_0x1c (SHARE) | 6 |
| 162 | Boat Gnorc | 60 | velocity: state 0 rock spring m_0x24 += (...) >> 6 (TICK_DO); speed: roll m_0x20 += m_0x24 (SHARE) | 2 |
| 163 | Copter Gnorc | 60, no sites | none | 0 |
| 169 | Gnasty Projectile Centre | 60 | speed: x/y/z flight step (SHARE); duration: m_0x08-- steps to target (TICK_DO); sequence: tracer point append per update (TICK_AND on its State == 0 operand) | 3 |
| 171 | Twilight Harbor Bridge Crank | 60, no sites | none: TICK_TIMER; the sound plays per flame hit (Spyro's side) | 0 |
| 176 | Gnorc Survivalist Grenade | 60 | speed: x/y/z throw step (SHARE); velocity: m_0x06 += m_0x08 after the move (TICK_END_DO); spawn: smoke g_SpawnParticle (TICK_DO); speed: tumble rot x/y/z += u_char m_0x0d..0f (SHARE8) | 3 |
| 177 | Thief Rocket | 60 | speed: height approach z += temp in m_0x44 states 0-5 and the scripted rocket's substate 1 (SHARE x7); velocity: bank spring m_0x0c/m_0x08 (TICK_DO x2) | 7 |
| 179 | Key Thief (Gnasty's Loot) | 60 | probability: the two frame-gated voice rolls (NextFrame == 2 / 0xC, (rand() & 3) == 0) -> TICK_AND on the roll; duration: m_0x1c++ (ticks circling, >= RT(0x1E,0x19) -> substate 1) -> TICK_DO | 3 |
| 181 | Thief Key | 60 | spawn: state 2's trail g_SpawnParticle(1, 12, moby, 0x8080 / 0x5008080) every update -> TICK_DO (x2) | 2 |
| 184 | Vulture-wielding Gnorc | 60 | velocity: state 6 m_0x10 -= RT(0x14,0x18) before MobyMove (TICK_DO) | 1 |
| 186 | Musket Gnorc (Dry Canyon) | 60, no sites | none | 0 |
| 188 | Peacekeepers Balloonist | 60, no sites | none | 0 |
| 189 | Magic Crafters Balloonist | 60, no sites | none: the talk trigger sets state 0 (re-armed only after Spyro leaves 0x1000), then ControlFlags 0x80002000, func_8003DFA4, D_800757A0 (the balloonist gamestate) and the dialogue id once | 0 |
| 190 | Beast Makers Balloonist | 60, no sites | none | 0 |
| 191 | Dream Weavers Balloonist | 60, no sites | none | 0 |
| 192 | Gnorc Gnexus Balloonist | 60, no sites | none | 0 |
| 193 | Fodder (Rabbit - Peace Keepers) | 60 | velocity: state 1 knockback -= RT(15,18) before MobyMove (TICK_DO) | 1 |
| 197 | Musket Gnorc Projectile (Dry Canyon) | 60 | speed: rot.y AngleAdd(.., ROTDEG8(5)) (SHARE); spawn: trail g_SpawnParticle(1,1,...) (TICK_DO) | 2 |
| 198 | Ice Gnorc | 60 | sequence: m_LaughCounter = 3 -> 2 at 60 Hz (#if); velocity: state 6 knockback -= RT(0x10,0x13) after MobyMove (TICK_END_DO) | 2 |
| 199 | Engineer | 60, no sites | none | 0 |
| 200 | Dockworker | 60, no sites | none | 0 |
| 201 | TNT Wrangler | 60, no sites | none | 0 |
| 202 | TNT Barrel | 60 | duration: state 3's explosion step m_0x00++ (radius, ends at 8) -> TICK_DO; substate (thrown, rolling): speed: m_Rotation.x += 6 -> SHARE; velocity: m_0x0c -= RT(0xA,0xC) before the z move -> TICK_DO; speed: m_Position.z += m_0x0c -> SHARE | 4 |
| 203 | Tin Soldiers | 60 | speed: the slide along its line VecScaleToLength(..., RT(200, 240)) (SHARE) | 1 |
| 204 | Haunted Wizards | 60 | spawn: aura g_SpawnParticle(1, 65, ...) every update (TICK_DO); spawn: state 1 cast particle x2 alternating by g_GameTick & 1 (TICK_DO) | 3 |
| 205 | Lamp On Pole | 60 | speed: the held gem's m_Rotation.z += RT(8,10) (SHARE) | 1 |
| 206, 239, 242 | Small Cactus / Tall Cactus / Round Cactus | 60, no sites | none | 0 |
| 207 | Gnorc Cove Barrel | 60 | duration: state 3's explosion step m_0x00++ -> TICK_DO; state 4 (charged, flying): speed: m_Position.z += m_0x0c -> SHARE, tumble m_Rotation.x += 7 / y -= 4 / z += 3 -> SHARE, velocity: m_0x0c -= RT(0xA,0xF) after the move -> TICK_END_DO; substate (thrown, … | 6 |
| 208 | Gnorc Gnexus Dragon Head Opener | 60, no sites | none | 0 |
| 209 | Snowball Projectile | 60 | spawn: trail g_SpawnParticle(1,75,...) (TICK_DO) | 1 |
| 210 | Metalhead Pole Red Effect | 60 | speed: spin rot.z += m_Substate (SHARE8) | 1 |
| 211–212 | Green and Purple Ski Gnorcs | 60 | velocity: state 2 recoil m_0x20 -= RT(0x14,0x18) and state 3 knockback m_0x20 -= RT(0x0C,0xF), both after MobyMove (TICK_END_DO) | 2 |
| 213 | Fodder (Bat - Ice Cavern) | 60 | duration: m_0x0c-- in states 0 and 1 (TICK_DO); speed: MobyMoveTowardTarget speed 0x40/0x60 and turn rates 8/8 (SHARE at the call, x2); phase: m_0x14 += 0x10 (SHARE) | 2 |
| 214, 216 | Blue Hat Gnorc (Peace Keepers) / Red Hat Gnorc (Peace Keepers) | 60 | velocity: m_0x40 += in state 1 (m_0x24 2) and state 10 before func_80039398 (TICK_DO x2); velocity: state 3 m_0x58 -= after MobyMove (TICK_END_DO); sequence: the shells' one-shot placement func_80039398(projectile, 1500/1350) at the full distance (#if x2) | 5 |
| 217 | Gnasty's Loot True ending control | 60, no sites | none | 0 |
| 218 | Cauldron | 60 | speed: m_0x00 += m_0x04 (SHARE); velocity: m_0x04 -= 4 after the move (TICK_END_DO); spawn: (g_GameTick & 1) particle gate (TICK_AND) | 3 |
| 222–224 | Stone Hill Trees | 60, no sites | none | 0 |
| 225 | Cannon | 60 | duration: m_0x0c-- (TICK_DO); spawn: the fuse's 5 particles + rand()s (TICK_AND on the loop test); sequence: the cannonball's one-shot placement func_80039398(projectile, 0x546) at the full distance (#if) | 3 |
| 226 | Pueblo | 60, no sites | none | 0 |
| 229 | Fat Lady | 60 | velocity: state 7 knockback -= RT(0x10,0x13) after MobyMove (TICK_END_DO) | 1 |
| 230 | Vulture | 60 | speed: z descent/height chase -=100 (state 0), +/-0x78 (state 1), +/-100 (state 2), sink -=0x32 (state 4) (SHARE); velocity: dive ramp m_0x00 += 0xA (TICK_DO), slow m_0x00 -= 5 after func_80039398 (TICK_END_DO), death drag -= 0x10 after MobyMove (TICK_END_D… | 9 |
| 231 | Armoured Ice Gnorc | 60 | duration: laugh count m_Scratch = 3 (#if: 2 at 60 Hz; TICK_TIMER counts laughs by the delta); speed: dying drift x/y FIXED_MUL(m_FallKnockbackSpeed) (SHARE); velocity: fall m_Scratch += 12 (TICK_DO) then z -= m_Scratch (SHARE) | 4 |
| 232 | Gnorc Copter Blades | 60 | speed: spin rot.z += RT(8, 10) (SHARE) | 1 |
| 236 | Fodder (Rat) | 60 | velocity: the death slide's m_0x20 -= RT(15, 18) before MobyMove -> TICK_DO | 1 |
| 237 | Cannonball | 60 | speed: tumble AngleAdd(rot.y, 3), x/y flight (SHARE); duration: m_0x0c-- (TICK_DO) with its ==0 free (TICK_AND), m_0x14-- (TICK_DO); velocity: z += m_0x04 (SHARE) then m_0x04 += m_0x08 (TICK_END_DO); spawn: trail particle (TICK_DO) | 7 |
| 238 | Fodder (Peace Keepers) | 60 | velocity: state 5 drag m_0x20 -= 0xF before MobyMove (TICK_DO) | 1 |
| 253 | Large Beast (Alpine Ridge) | 60 | velocity: state 3 death drag m_0x18 -= RT(0xE, 0x11) after MobyMove (TICK_END_DO) | 1 |
| 260–269 | Digits | 60 | speed: rot.z += 1 (SHARE); velocity: vel.z -= RT(6,7) before the move (TICK_DO); speed: pos += vel x3 (SHARE); duration: m_Lifetime-- (TICK_DO) | 4 |
| 270 | Druid (Magic Crafters, Alpine) | 60 | probability/spawn: (rand() & 1) particle roll (TICK_AND); sound: beast PlaySound under m_0x14 < 3 && env paused (TICK_AND); probability: (rand() & 0xFF) >= 0xD3 roll in m_0x10 == 3 (TICK_AND); velocity: state 3 drag m_0x38 -= RT(0x14, 0x18) after MobyMove (… | 4 |
| 271 | Armored Druid | 60 | state 16: z += m_0x30 (SHARE), gravity m_0x30 -= RT(0x14, 0x18) (TICK_END_DO); state 18: nextHeight = z + m_0x30 (SHARE), gravity after the move (TICK_END_DO), drag m_0x18 -= RT(0x14, 0x18) after MobyMove (TICK_END_DO) | 4 |
| 283 | Storm Wizard | 60 | spawn: g_SpawnParticle(1, 65, ...) every update (TICK_DO); duration: state 0 m_0x10-- (TICK_DO) | 2 |
| 284 | Armoured Druid (Wizard Peak) | 60 | duration: state 0 cooldown m_0x08-- (TICK_DO) | 1 |
| 285 | Ice Wizard (Wizard Peak) | 60 | speed: MoveMobyAlongPath's maxSpeed RT(0x80, 0x9A) and turnRate 8 (SHARE at the call site); speed: state 5 turn m_Rotation.z += angleDelta (SHARE); speed: func_8003A16C's maxMag 0x80 and clampRange 8 (SHARE at the call site); spawn: state 5 g_SpawnParticle(… | 2 |
| 286 | Ambient sound player | 60 | sub-case m_0x00 2 / m_0x10 1: the path step (m_0x24-- count, pos += m_0x18, node snap and reload) and its every-48-steps sound cue, gated to a tick's first vblank (#if SPYRO_TICK60 break) | 1 |
| 288–289 | Bird Feathers | 60 | speed: pos.x/.y += m_0x00/02, pos.z += m_0x04 (SHARE); velocity: m_0x04 -= RT(5,6) before the z move (TICK_DO); duration: --m_0x0d == 0 (TICK_AND), m_0x0c-- (TICK_DO); speed: 288's tumble rot += m_0x06/08/0a (SHARE, shorts), 289's rot.z += RT(0xC,0xF), rock… | 5 |
| 293–294 | Tree Tops Green and Red Thieves | 60 | probability: the two frame-gated voice rolls (TICK_AND); velocity: states 0 and 4's m_0x14 -= RT(0x1E,0x24) / RT(0xA,0xD) before the move (TICK_DO); speed: the z move and its floor test (SHARE, x2 each); spawn: state 4's 3 dust particles at speed >= RT(0x11… | 5 |
| 295 | Ice Wizard Projectile | 60 | speed: homing turn m_Rotation.z += angle (SHARE); spawn: trail g_SpawnParticle(1, 5, ...) every update (TICK_DO) | 2 |
| 296 | Gnorc Balloonist Balloon | 60, no sites | none | 0 |
| 298 | Lofty Gnorc Balloonist | 60, no sites | none | 0 |
| 301 | Ice Wizard (High Caves) | 60, no sites | none: TICK_TIMER (m_0x08 cast spacing, idle timer), MobyRotateToAngle/ToSpyro, one-shot state changes; the projectile spawn is gated by TICK_TIMER(m_0x08)=4 vblanks and m_0x0c<3 | 0 |
| 302 | Druid (High Caves) | 60 | probability/spawn: the (rand() & 1) particle roll in states 0, 1, 2, 4 (TICK_AND x4) | 4 |
| 303 | Tornado Wizard | 60, no sites | none: TICK_TIMER(m_0x0c / m_0x08) in vblanks; the tornado spawn is latched by m_0x04 bit 1 | 0 |
| 304 | Tornado | 60 (helper, 0002) | speed/turn: MoveMobyAlongPath(..., maxSpeed RT(0x40,0x4D), turnRate RT(0x20,0x27), ..., &props->m_0x08) every update: a per-call move, turn and velocity smoothing | 0 |
| 305 | Metalback Spiders | 60 | speed: state 2's death fling pos.x/.y += FIXED_MUL(m_0x2c, Cos/Sin(m_0x24)) (SHARE x2) | 2 |
| 308 | Flight plane | 60 | bank spring: m_0x10 velocity and m_0x0c angle steps (TICK_DO x2); path walk by func_8003BFC0 (treated as converted); falling states MobyMoveWithGravity (converted); damage and landing one-shots | 2 |
| 310–311 | Small metal fragments | 60 | speed: pos += vel x3 (SHARE); velocity: vel.z -= RT(6,7) before the z move (TICK_DO); speed: spin += Vector3D16 (SHARE x3); spawn: the 3-particle trail (TICK_AND in the for's condition); duration: m_Lifetime-- (TICK_DO) | 4 |
| 312 | Fireworks Chest | 60 | spawn: the two particle loops of the burn (loop bound TICK_AND); sequence: explosion one-shot on m_Timer == 0 (TICK_AND); duration: m_Timer++ in the explosion (TICK_DO) | 4 |
| 315 | Yellow Beast Snack (Alpine Ridge) | 60 | velocity: z += m_ZVelocity (SHARE), m_ZVelocity -= 20 after the move (TICK_END_DO) | 1 |
| 318 | Ice Wizard Projectile Sparks | 60 | velocity: m_0x00.z -= RT(6,7) before the move (TICK_DO); speed: VecAdd(pos, pos, m_0x00) (#if block, SHARE per axis); speed: rot.x += 15, rot.z += 9 (SHARE) | 2 |
| 326 | Ice Wizard | 60, no sites | none: TICK_TIMER(m_0x00) for cast spacing and the 96/97 waits, func_80039E94 (converted path walk), MobyRotateToSpyro/ToAngle, one-shot state changes (10, 98, 99) | 0 |
| 328 | Druid moveable geometry helper? | 60, no sites | none: snaps to the centroid of its collision triangle every update (tracking, idempotent) | 0 |
| 340 | Flight Ring | 60 | speed: spin AngleAdd(rot.x, 8) (SHARE); sound: rot.x < 8 test (TICK_AND) | 1 |
| 343 | Large Gnorc Weapon Effect | 60, no sites | none: m_0x04 counts by g_DeltaTime; position is a blend of the parent's animated vertices | 0 |
| 345–347 | +1, +2, +3 flight model | 60 | speed: state 2's step toward the HUD point, capped at RT(0x80,0x9A) a tick, VecAdd'ed (SHARE on the cap, both uses) | 1 |
| 359–361 | Wood fragments | 60 | speed: pos += vel (SHARE x3); velocity: vel.z -= RT(6,7) before the z move (TICK_DO); speed: spin (SHARE x3, Vector3D16); duration: m_Lifetime-- (TICK_DO) | 2 |
| 370 | Kamikaze Tribesmen | 60 | velocity: speed ramps m_0x1c += 0x14 (states 1, 4; TICK_DO, before func_80039398), braking m_0x1c -= 0x10 (state 6, TICK_DO, before func_80039398) | 3 |
| 371 | Doctor Shemp | 60 | spawn: state 41's 3 exhaust particles a update (loop bound TICK_AND) | 1 |
| 372 | Fat Lady (Doctor Shemp) | 60 | velocity: state 7 death drag m_0x04 -= 0x10 after MobyMove (TICK_END_DO) | 1 |
| 375–386 | Arch fragments | 60 | speed: VecAdd(pos, vel) (#if block, SHARE per axis); speed: spin (SHARE8 x3, Vector3D8) | 1 |
| 397, 399 | Night/Icy Flight Lighthouse Light / Lit version | 60, no sites | none: one-shot on flame (class becomes 399) | 0 |
| 398 | Portal Path Moby | 30 | (if converted) state 1's unk_0x208 computation and its mag/progress test once a tick | 0 |
| 401 | Safe | 60, no sites | none: shake timer += g_DeltaTime (table index >> 1 is vblanks), ApplyFlameHeatExternal converted, destruction one-shot | 0 |
| 402 | Fireworks Rocket | 60 | spawn: substate 1 fuse spark (TICK_DO); spawn: the every-4-vblanks delta test (#if, as class 329: TICK_AND with + 2); sequence/speed: substate 2's frame-counted path flight gated to tick starts (#if block, break on the second vblank) | 3 |
| 403 | Attack Frog | 60 | speed: state 4 turn rot.z += RT(5,6) (SHARE); state 30 sink pos.z -= RT(100,120) (SHARE) | 2 |
| 407 | Train Front | 60 | spawn: the smoke puff under g_GameTick % 4 (TICK_AND); path walk by func_8003BFC0 (treated as converted); TICK_TIMER fuse; one-shot break-up spawns | 1 |
| 408 | Train Carriage | 60, no sites | none beyond func_8003BFC0 (treated as converted); TICK_TIMER fuse; one-shot break-up spawns | 0 |
| 414 | "You're Toast Gnorc!" Spyro | 60, no sites | none | 0 |
| 417 | TODO: return when PK Cannon and Cannon ball Mobys are | 60, no sites | none: one-shot break on m_Substate == 1, then MobyFree | 0 |
| 453 | Beast Makers chicken 2 (duplicate of 466 added after July) | 60, no sites | none: TICK_TIMER sound timers (re-armed with rand once), MobyRotateToAngle/MobyMove/MobyMoveWithGravity/func_80039E94 converted | 0 |
| 454–456 | Breakable fragment | 60 | speed: pos x/y/z += vel (SHARE), rot += angular vel (SHARE, short); velocity: vel.z -= RT(0x10,0x13) before the z move (TICK_DO); duration: m_Lifetime-- (TICK_DO; TICK_TIMER on it already counts the delta) | 2 |
| 458–459 | Cannonball fragment | 60 | speed: pos x/y/z (SHARE), rot (SHARE, short); velocity: vel.z -= RT(6,7) before the z move (TICK_DO); duration: m_Lifetime-- (TICK_DO) | 2 |
| 461 | Electro Gnorcs | 60, no sites | none: TICK_TIMER timers (== 2 one-shots), EnvAnim flag sets are idempotent, rot.y += g_DeltaTime * 2, MobyMoveWithGravity | 0 |
| 466 | Beast Makers chicken | 60, no sites | none (same as 453); state 4's animation loop reset (NextFrame >= RT(0xF,0xD) -> frame 0/1) is idempotent | 0 |
| 467 | Big Laser Gnorc | 60 | velocity: state 6 knockback m_0x20 -= RT(16,19) after MobyMove (TICK_END_DO) | 1 |
| 475 | Small Bush | 60, no sites | none: damage-flag state machine, animation events | 0 |
| 476 | Dragon-Eating Plant | 60 | probability: state 0's attack roll randomValue = rand() & 0xFF (TICK_DO) and its test (TICK_AND); speed: state 4 tumble rot.x/.y += 4 (SHARE) | 3 |
| 481–483 | Flight Train, Wagon and Plane Fragments | 60 | speed: pos x/y/z (SHARE), rot (SHARE, short); velocity: vel.z -= RT(6,7) before the z move (TICK_DO); spawn: smoke particle every 4th tick (TICK_AND); duration: m_Lifetime-- (TICK_DO) | 3 |
| 487 | Chicken Cage | 60, no sites | none: frame-gated state changes (one-shot) | 0 |
| 493 | Cactus Charred Particles | 60, no sites | none: freed when its animation finishes | 0 |
| 494 | Zap Effect | 60 | speed: m_0x12 == 2 homing turns rot.z/rot.y += angleDelta (clamped +-2; SHARE) | 2 |
| 495 | Blowhard | 60 | speed: the eased rise/sink z += zDifference >> 3 (SHARE, approximate) | 1 |
| 497 | Druid Blowhard | 60 | spawn: (g_GameTick & 1) != 0 particle gate (TICK_AND) | 1 |

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
  - a standing jump with up held, from the same spot, goes elsewhere at 60 Hz (routes A and B's jumps, D141: ~280 units forward instead of ~820 in 24 vblanks; ~430 units to the side). Not investigated: the air steering or the camera that "up" is relative to. It's why routes A and B as written don't reach their portal and vortex at 60 Hz.
- **Classes:**
  - Decelerations guarded by a threshold (Bull state 4, Ram state 2, Shepherd state 7, sheep fodder, Shepherd) can lose half a move step once, on the tick that crosses it.
  - Fragments are freed one vblank early (their lifetime reaches 0 at a tick's start).
  - The fairy's eased moves and the clamped turns (Gnorc, Thief Gnorc, Bull) converge a vblank later.
  - Club Gnorc's laugh counter (`TICK_TIMER` gated by an animation event) is 1 at 60 Hz for the original's 2.
  - The Fan Chest Fan's gravity takes 5 a tick split 2 + 3, where the delta form would give 2 + 2.
  - D144's batches noted the same kinds (each in its class's notes): bounces and landings that can now happen on a tick's second vblank (gems, digits, fragments, barrels: never twice for one contact); jump arcs that start on a tick's second vblank begin half a tick late (the key thieves, Gnasty); the Blowhard's eased rise covers 31/256 of the way a tick for 32/256; the High Caves fairies' pull closes 23.4% of the gap a tick for 25%; the Ice Gnorc's laugh count is 2 for the original's 3, as the Club Gnorc's; a gem still bouncing against a slope may ding on more vblanks than the original's ticks.
  - A save state taken between a tick's two vblanks drops the rest of a path step (`t60_path_step`): half a frame's move.
- **At 30 Hz in a 60 Hz update** (correct speed, 30 fps motion):
  - particles;
  - the HUD;
  - the portal path moby (398);
  - inside 60 Hz classes, the four steps taken whole a tick (above, "Verdicts");
  - Cutscene cameras stay recorded at 30 Hz (§7.2); the cutscenes themselves are 30 Hz gamestates.
- **Not done:**
  - the switch at a level load (§7.1);
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

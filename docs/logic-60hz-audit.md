# Could the logic run at 60 Hz? (audit, 2026-11-09)

A read-only feasibility audit. Nothing was changed or run. The question: could Spyro's game logic tick once per vblank (60 Hz), so 60 fps needs no in-between frame, and what would that cost in faithfulness?

Paths are under `third_party/spyro-1/` (the decomp) unless they start with `src/game/` (our hand C). Counts are `grep` counts on today's `main` (`213e808`); they size the work and are not exact.

## Short answer

**No. Not without rewriting the timing of most of the game's world, and the result couldn't be checked against the emulator.**

- **What already runs per vblank:** Spyro's physics, his own animation, the camera's sphere and the environment animations. The game runs them `g_DeltaTime` times a tick, one sub-step per vblank, with that vblank's buffered pad input. At delta 1 they mostly just work.
- **What counts ticks:** almost everything else. That's the moby AI (218 classes in 31k lines of `moby_update.c`), moby movement, gravity, the moby animation step, particles, per-tick random rolls, and a handful of counters in Spyro and the camera. At delta 1 all of that runs **twice as fast**.
- **Halving the per-tick constants** fixes the speed but not faithfulness:
  - many of them are small integers (gravity 6, turn 3, `(rand() & 3) == 0`) that don't halve exactly;
  - Euler integration gives different arcs at half the step;
  - twice the `rand()` calls give a different random sequence.
- **Checking:** the attract demos would desync, and no trace could compare tick for tick with PCSX-Redux.
- **Estimate:** a few thousand changed lines across ~45k lines that must be reviewed, at very high risk.
- **Recommendation:** keep 30 Hz logic and stage 7's interpolated 60 fps. It already shows the real frames on the original's vblanks, adding no latency (`docs/presentation.md`).

## 1. How the clock works today

`src/main.c` `main`: after `GamestateUpdate`, `g_DeltaTime = g_UnprocessedFrames`, clamped to **2..4**. `PadVSync` (`src/gamepad.c:508`) adds 1 to `g_UnprocessedFrames` per vblank. It also buffers that vblank's pad state in `g_Pad.m_BufferedInputs[0..3]` (`gamepad.c:490–500`). `GamestateDraw` holds the loop to 2 vblanks (`src/gamestates/draw.c:856–863`, `while (pre - post < 2) VSync(0)`).

So **delta 2 is the design rate, and 3 and 4 are slowdown compensation.** The code never sees 1:
- `main.c:26` clamps it;
- `initialization.c:409` sets 2;
- the attract demo forces `g_UnprocessedFrames = 2` (`gamepad.c:214`, "No lag frames in demo recordings").

The decomp has no PAL path (the only "PAL" is a comment in `spu.c:755`). Whether the PAL release ran delta 1 or changed constants can't be checked from this tree.

The clearest sign that 2 is the base: the moby helpers scale speeds **only** for 3 and 4. Ten sites in `src/moby_helpers.c` (307, 463, 506, 556, 605, 641, 733, 909, 1035, 2087) each read:

```c
if (D_800756C4 == 3) speed += speed >> 1;   // 1.5x
else if (D_800756C4 == 4) speed <<= 1;      // 2x
```

At delta 1 they fall through to 1×, which is the 2-vblank speed. `D_800756C4` is a copy of `g_DeltaTime` that `moby_update.c:101` sets before each moby.

## 2. System by system

| System | Where | How it uses time | At delta 1 |
|---|---|---|---|
| **Spyro's physics** | `src/pete.c:1546`, `2090`; `src/game/pete_hand.c:1595` `func_80043FE4(i)` | One call per vblank, with `m_BufferedInputs[i]` as the pad; velocities are per vblank, summed, then `>> 6` and added to the position once per tick (`pete.c:2093–2101`) | Works. Rounding changes: `>> 6` floors twice as often, so slow negative motion drifts up to 1 unit per vblank instead of per tick. |
| **Spyro's rotation and animation** | `pete.c:1554`, `1923`; `func_8004888C` (`m_idleTimer++`, `pete.c:1515`); `func_8003CB24` (16 progress units a frame) | Per-vblank loops | Works. |
| **Spyro's per-tick parts** | `func_80047B60` (`src/game/pete_dispatch_hand.c:155`), `func_80041670` (`pete_transition_hand.c:745`), the collision step `func_8003FE40` (`pete_state_hand.c`), `func_8004A200` | Once a tick, with counters in ticks: `m_airTime++` (`pete.c:826`, `1027`; thresholds `>= 4`, `>= 8` in `pete_transition_hand.c:66`, `430`, `565`, `728`), `m_noGamepadUpdateFrames--` (`pete.c:1906`), `m_floorIdleTime++` (`pete_state_hand.c:904`), `D_80075700 >= 10` (`pete_transition_hand.c:716`), `m_flameableFrames++`, `unk_0x1a0++`; the collision step's step count from the tick's distance (`pete_state_hand.c:920`) | **2× faster:** the coyote time (`m_airTime >= 4`) becomes 4 vblanks instead of 8. About 10–20 sites, each needing a doubled threshold. The thresholds are integers, so doubling is exact, but each one changes feel if one is missed. |
| **Camera** | `src/game/camera_hand.c:818`, `1071` (`func_80034CE8` per vblank); `camera.c:362` (scaled) | The sphere simulation runs per vblank. Per tick: the move to the destination in steps of at most 0x100 (`func_80034480`, `camera_hand.c:155`), the 30-tick hidden and off-centre counters (`:240`), look yaw ±0x30 (`:869`), `D_80075938--` (`:1013`) | Mostly works. ~6 per-tick sites converge 2× faster unless halved; 0x30 and 0x100 halve exactly. |
| **Moby AI and movement** | `src/overlays/moby_update.c` (30,973 lines, 218 classes) | 74 `g_DeltaTime` sites (timers and spins). The rest is per tick: **311** position or rotation `+=`/`-=` without delta, **287** `++`/`--` counters, gravity such as `m_Velocity.z -= 6` (cap −128) on the tin soldier fragment (`:6934`) | **Everything without delta runs 2× fast.** Enemies walk, fall, turn and attack twice as fast. |
| **Moby helpers** | `src/moby_helpers.c` | Timers (`func_80037F90`, `:27`) count down by delta, in vblanks: these work. Move and turn speeds go through the ten 3/4 sites above | Timers work. Speeds need a delta-1 branch, which halves them: `AngleAdd(angle, 3)` (`:297`) and small turn speeds don't halve. |
| **Moby animation** | `func_800522C0`, `src/game/moby_lists_hand.c` `Animate` | Per tick, one step of `m_PerFrameProgress` (64 units a frame). Delta 3 adds a half step (`>> 1`), and delta 4 a full one (`moby_update.c:86–91`). At most one frame per call (`progress & 0x3F`) | A half step per vblank is the game's own catch-up form, but `P >> 1` drops the low bit. An odd speed P runs at (P−1)/P, and **P = 1 freezes.** A speed above 64 can advance two frames per tick where the original showed one. A remainder carry fixes the first, not the second. |
| **Random rolls** | `rand()`: 243 in `moby_update.c`, 153 in `particle_spawn.c`, 68 in `moby_spawn.c`, 38 in `particle_update.c`, 15 in `moby_helpers.c`, plus Spyro and the camera | Per tick, often gated by state, e.g. `NextFrame == 2 && (rand() & 3) == 0` → sound (`moby_update.c:1371`) | Twice the rolls: per-tick odds double per second, and frame-gated rolls get two tries per frame. The sequence is no longer the original's, so no trace matches. |
| **Particles** | `src/overlays/particle_update.c` `UpdateParticle(pDelta)` | Mixed: size, colour and age scale by `pDelta`; position `+= velocity` and gravity `z++` per call (`:20–37`) | Ages work. Motion runs 2× fast. |
| **Environment animation** | `func_8002A6FC(g_DeltaTime)`, `src/game/env_update_hand.c` | The frame delays count down by delta | Works. |
| **Sparkles (in the draw)** | `asm/renderers/r_particles.s:1640`, `src/game/particles_hand.c:1431` | Aged by `g_DeltaTime` in `GamestateDraw` | Works. |
| **HUD, fades, level transition, credits, balloonist** | `hud.c:160`, `220`; `update.c:51`, `78`, `452`, `1281`, `1511`; `src/game/credits_hand.c:190`; `balloonist.c` | Mostly `+= g_DeltaTime`. Balloonist's "first tick" tests `unk_0x04 == g_DeltaTime` | Mostly works. Some fixed per-tick constants remain, such as `func_8003CB24(3)` in the cutscene states (`update.c:673`, `705`, `1120`). |
| **Sounds** | Animation frames (`AnimateList` queue), `MobyPlaySound` in the AI, Spyro's per-tick sound triggers (`pete.c` after 2000) | Follow animation frames and AI events | Follow whatever those do. Frame-gated `rand()` sounds become more frequent. |
| **Attract demo** | `PadDemoUpdate` (`gamepad.c:141`) | One input word per tick, copied into all four buffered slots | Each word would need feeding for 2 vblanks, with `m_Down` only on the first (~20 lines). Even then the demo desyncs (§4). |
| **Pacers and fixed waits** | `draw.c:858` and the other draws, title (`titlescreen.c:1440`), flight (`flight.c:603`), boot logos (`initialization.c:268–377`) | `< 2` vblanks per frame, and waits in vblanks | Each pacer becomes `< 1`. The waits in vblanks stay right. |

The PS1 itself is inconsistent here. At delta 3 Spyro, the camera and the helper speeds catch up, but the hand-written per-tick moby code doesn't. So enemies slow down in real time when the PS1 lags, and Spyro doesn't. A 60 Hz port would have to choose a "correct" speed for code that never had one.

## 3. Halving a tick: what precision costs

For a tick-counting site, the choices are:

1. **Leave it** (2× speed). Wrong.
2. **Halve the constant.** Exact for even integers and for thresholds you double instead (`m_airTime >= 8`). Inexact for odd and small values: gravity 6 → 3 is fine, but the circle move's angle step 3, turn speeds of 1–3, `(g_DeltaTime * 5) >> 1` (`moby_update.c:27846`), animation speed 1 and the 1-unit `z++` particle drift are not.
3. **Split with a carry** (`a >> 1`, then `a - (a >> 1)` on the next vblank). The sums per 2 vblanks match exactly, but only for quantities that are only added. It doesn't help with:
   - integration: `v -= 6; z += v` at half the step gives a different arc (Euler's error depends on the step), and the −128 cap is hit at a different time;
   - lerps and clamps such as "move at most 0x100 toward the target", or "turn at most `rotSpeed`, then stop within `withinAngle`";
   - every distance check against a radius tuned to 2-vblank steps: collision, "within 0x400 of Spyro", ledge and floor snaps;
   - `rand()`.

So even a careful conversion reaches "plays about the same", not "is the same". Positions differ from the first jump or fall onward, and AI decisions branch on positions and random rolls. The game can then diverge in visible ways: an enemy reaches a ledge one tick earlier and falls, or a gem bounces elsewhere.

## 4. How it would be checked (and what stops working)

Today's checks (D28, D30, `docs/whole-game-host.md`, `tools/oracle/`):
- function replay against PCSX-Redux;
- both attract demos tick for tick;
- route screenshots;
- byte-identical host routes.

At 60 Hz:
- **Attract demos:** they're fixed stick recordings that steer Spyro open-loop. D30 showed how fragile that is: a wider frustum in the logic alone (`--widescreen-logic`) desynced the Stone Hill demo at tick 123 of 541. A 60 Hz world would desync almost at once, and the attract mode would show Spyro walking into walls. Keeping it right would mean running demos on the 30 Hz path, so both paths would live in one binary.
- **Traces and oracle:** no 60 Hz reference exists. PCSX-Redux runs the original at 30 Hz (with drops), and the random sequence differs from the first extra `rand()`.
- **What could still be checked:**
  - **A switch that keeps 30 Hz exact:** every change written as a function of the rate, so `rate=30` stays byte-identical to today on every existing check. That's the only way to keep the oracle useful, and it doubles the review.
  - **Function replay for pure functions** (collision, maths, renderers): unchanged.
  - **Statistics against the emulator:** speeds in units per second, jump heights and distances, timer lengths in seconds, enemy patrol periods, measured on both sides per class. That's 218 classes and a new harness, slow to build. It catches gross errors, not subtle ones.
  - **Side-by-side video by eye.**

## 5. Estimate

| Piece | Lines read | Lines changed (estimate) | Risk |
|---|---|---|---|
| Clock, pacers, demo feed | ~100 | ~50 | low |
| Spyro per-tick counters and thresholds, the `>> 6` rounding | ~6k (pete, our hand C) | 100–300 | medium (feel) |
| Camera per-tick sites | ~1.7k | 50–100 | medium |
| Moby helpers (10 speed sites) | 2.4k | ~100 | medium |
| Moby animation step (carry) | 50 | ~20 | low (P > 64 stays different) |
| **Moby AI, 218 classes** | **31k** | **2,000–4,000** (311 moves, 287 counters, 243 random rolls, gravity and velocities, each judged by hand) | **very high** |
| Spawn and particles | ~4.4k | 300–600 | high |
| Gamestates, cutscenes, HUD, flight, balloonist, titles | ~5k | 100–300 | medium |
| A rate switch so 30 Hz stays exact, and a statistics harness | — | 1,000–2,000 | medium |
| **Total** | **~45k** | **~4,000–7,500** | **very high** |

The effort is mostly per-class judgment, not mechanical: for each counter, decide whether it's a duration (double it), a speed (halve or split it), a probability (halve the odds) or a sequence step (leave it alone). Each wrong call shows up only in the level where that class lives.

## 6. Recommendation

**Don't do it.** Keep the logic at 30 Hz and finish stage 7's interpolated presentation (7f/7g). Why:
- Faithfulness is the project's hard constraint. 60 Hz logic gives up the oracle, the demos and byte-identical routes for every system that counts ticks, which is most of the world.
- The visible gain over interpolation is small:
  - The real frames already land on the original's vblanks with no added latency (`docs/presentation.md`, "Real frames: byte-identical … no added latency").
  - Spyro's controller already reads every vblank's input (the buffered pad), so no presses are lost.
  - 60 Hz logic would cut at most one vblank (16.7 ms) off the response on half the frames.
- At ~4–7.5k changed lines across 218 enemy classes, the cost is several times all of stage 7 (~2–4k lines), and it couldn't be verified the way the rest of the port is.

This audit found no smaller form of "real 60 Hz" worth having. Running only Spyro at 60 Hz would gain little: his physics already sub-steps per vblank, and his position is applied once a tick. It would also put him out of step with the 30 Hz mobys he collides with and stands on.

## 7. As an opt-in third setting: off, interpolated, 60 Hz logic

The owner's follow-up: the 60 fps option gets three values. **Off** and **interpolated** (today's `interp`) stay faithful, and **60 Hz logic** is opt-in and allowed to differ. This section answers four questions about that. It doesn't change §1–§5. The work to write the 60 Hz rules is the same; what changes is that the faithful modes must stay exactly as they are.

### 7.1 When it can switch: not mid-level; at a level load, yes

**Not at any moment, as `interp` does.**
- `interp` can change from the menu (`src/platform/vita/menu.c`; the menu runs inside the game's VSync wait, `vblank.c:229`) because it changes only the picture: the logic never sees it.
- A 60 Hz switch would change the units of live game state, and most of that state can't be converted:
  - Every moby's `m_Props` holds velocities, timers and phases in the units of the rules that wrote them, for example the tin soldier fragment's `m_Velocity` (`moby_update.c:6932–6937`). The layouts differ per class (218 of them, mostly untyped `m_0x..` fields), so nothing generic can rescale them.
  - The same goes for the particles (`particle_update.c`: velocities in `m_Raw[20..]`).
  - The moby animation progress, and Spyro's and the camera's per-tick counters (§2).
- Switching anyway would make every moby alive at that moment move at the wrong speed until its state happened to reset.

**At a level load: yes. This is the switch point to plan for.**
- A load spawns every moby and particle fresh (`moby_spawn.c` and `particle_spawn.c`, which the 60 Hz rules would include). The camera is reset, and Spyro's state is set by the arrival.
- The port already picks each level's code at that point: `SetOverlayPointers()` dispatches by level (`cmake/overlays.cmake`). It could pick the 60 Hz copy there too (§7.4).
- The menu row would take effect "at the next level". The title screen and the start of a new or loaded game are loads too.
- Two things to check before relying on it:
  - **What crosses a load.** HUD counters and `g_LevelTransTicks` count `g_DeltaTime`, which is in vblanks, so they're safe. Spyro's struct needs checking field by field for tick counters that survive the arrival (`m_airTime`, `m_noGamepadUpdateFrames`, …).
  - **The transition gamestates** (the portal fly-out, the loading screen, the arrival) span the load. They must run under one set of rules from start to finish. Switch when the load begins, before the fly-in.
- If that check finds a long list, fall back to **at boot only**: the setting is read from `config.txt` at start. That's safe by construction and costs nothing more.

### 7.2 What has to stay at 30 Hz, and what else depends on the tick rate

**Must run at 30 Hz** (recorded input that steers open-loop):

| What | Where | Why |
|---|---|---|
| Attract demos | `PadDemoUpdate` (`gamepad.c:141`), demo levels loaded from the title | One input word per tick (§2). They're loads, so §7.1's switch puts them at 30 Hz naturally. Back on the title, the setting applies again. |
| Our own scripted runs | host pad scripts (vblank-timed, `tools/steer.py`, `tools/anchor_pad.py`), the Vita's `tools/vita/hw-routes/*.pad`, the oracle's routes and pokes (`docs/whole-game-host.md`), `menu_test` | Routes land somewhere else at another rate. Scripted runs should force 30 Hz, as `tools/vita/vita3k_run.sh` writes `interp=0` today. |
| Save states (quick save) | `src/game/savestate.c` (header `format`), `savestate_vita.c` | A state holds every moby's props in its mode's units. It must record the mode, and loading it must switch to that mode. States without the field are 30 Hz. |

**Clocked in vblanks already: these work at either rate.**
- **Cutscenes:**
  - The intro and ending (`GamestateCutsceneUpdate`, `update.c:1206–1210`: `m_CurrentTick += g_DeltaTime`, against `m_Duration << 1`).
  - The dragon rescues (`src/game/gamestates_hand.c`: `dc.m_CutsceneTicks += g_DeltaTime`, `:936`). The dragon's voice is an SPU sound started at a tick threshold (`DragonSound`, `:556`), so it stays in sync.
  - Moby positions blend between recorded samples per vblank (`cutscene.c` `func_8002BFE0`).
  - The cutscene **camera** is recorded once per 2 vblanks (`m_CurrentTick >> 1`; `CutsceneCamera(dc.m_CutsceneTicks >> 1)`). In 60 Hz mode it would still step at 30 Hz unless it's interpolated, so cutscenes would look smoother in the interpolated mode than in the 60 Hz one.
- **The exception is the title screen's attract scene:** `m_CurrentTick += 2` per tick (`titlescreen.c:100`) would play at double speed. It needs `+= g_DeltaTime` in the 60 Hz rules, or the title runs at 30 Hz.
- **XA music:** the CD streams in real time. `CDMusicUpdate` (`music.c:10`) only polls once per tick: at 60 Hz it polls twice as often. Its 8-minute easter egg uses `g_LevelTicks` (vblanks), and its `rand() % 3` shifts the random sequence (§2). No sync to the logic exists.
- **CD loads:** `g_CDReadTime` counts vblanks (`PadVSync`). Loads tick through `g_LoadStage` once per game tick, so they'd poll faster. That's harmless, and maybe a little quicker.
- **The memory card:**
  - The save format is unchanged (`include/save_file.h`): progress, settings and the flight levels' course records.
  - Those records count vblanks (`D_800758F4 += g_DeltaTime`, `moby_update.c:26350`; `flight.c:61–104`), so the units match.
  - But a record set in 60 Hz mode was set under different enemy and plane rules, and it lands in the same card as 30 Hz ones. The card can't tell them apart. If that matters, the port could keep 60 Hz records beside the card (a port file), not in it.

### 7.3 The game thread's cost per tick at 60 Hz

At 60 Hz each vblank runs one whole tick: the logic and the full draw path. Draws have game-state side effects (`m_WasDrawn` picks which mobys update, shadows, sparkles, particle aging; D30, D104), so the logic thread can't hand them to the presenter.

So the game thread's cost **per vblank** at 60 Hz is about today's cost **per tick** at 30 Hz. The table below doubles the per-vblank means of `docs/native-plan.md` §3.5, which are taken at 30 fps (one tick per 2 vblanks).

The game-thread layers in today's shipped settings are logic, renderers, draw/other and present:
- the OT walk is on the parse thread (`parse_thread=1`);
- GL is on the render thread;
- the mix is on the mixer thread;
- disc reads go to `cd_native`'s worker.

**X4** (2026-11-04, K route, every layer on the game thread, 30 fps), ms:

| Part | logic + renderers + draw/other + present, per vblank at 30 Hz | ≈ per vblank at 60 Hz | with the OT walk too (if `parse_thread=0`) | of 16.7 ms |
|---|---:|---:|---:|---|
| title | 2.24 | 4.5 | 7.5 | fits |
| Stone Hill | 4.00 | 8.0 | 12.3 | fits |
| Dark Hollow | 4.34 | 8.7 | 14.4 | fits; tight without the parse thread |
| the route (51 windows) | 3.52 | 7.0 | 11.2 | fits |
| attract demo (a flight) | 5.84 | 11.7 | 15.8 | fits; demos run at 30 Hz anyway (§7.2) |

**Toasty, shipped settings** (X6/X7, `REALTIME=1`, the game thread's busy share):
- 21.5% at 30 fps (≈ 3.6 ms per vblank, so ≈ 7.2 ms per tick).
- 39.5% at 60 fps interpolated, with the pass still on the game thread.

60 Hz logic would be about **43%** (≈ 7.2 ms per vblank, *estimate*): X7's level, with ~9 ms to spare on average.

**The render thread and GPU** see 60 real pictures a second, the same load as interpolated 60 fps today:
- X7 Toasty: 10.6 ms a picture on the render thread, 6.3 on the GPU;
- the Stone Hill flight: 15.4 ms a picture in X7, and 7.2 after D111 (Y12).

**So: it fits in 16.7 ms on these means, with room to spare in play.** Caveats:
- **These are 300-vblank means.** The logs don't give the worst tick, and a tick over 16.7 ms raises `g_DeltaTime` to 2. The 60 Hz rules must accept delta 1–4, as the original accepts 2–4. A late tick would then play like the original's normal tick. Otherwise it slows the game, as the PS1's lag does.
- **The logic itself** is 0.13–0.30 ms per vblank at 30 Hz (X4, 8H1). At 60 Hz the moby AI runs twice as often and Spyro's sub-steps halve. It stays well under a millisecond: noise beside the draw.
- **After stage 7f/7g the interpolated mode gets cheaper on the game thread** (Toasty about 21.5% at 60 fps, *estimate*, `docs/native-plan.md:1302`). The 60 Hz mode can't: it would be the heavier of the two on the game thread (~43% against ~22%), and the same on the render thread and GPU.

### 7.4 Can the oracle still check off and interpolated with the 60 Hz code compiled in?

**Yes, if the 60 Hz rules are a second compile of the update path, the way stage 7e builds the presenter.**

`cmake/presenter.cmake`:
- compiles `draw.c` and the files it calls a second time, with `-include src/game/pres/pres_names.h` renaming what they write;
- links them into one relocatable object whose functions are all local but its entry (`GamestateDraw_pres`);
- has `tests/pres/link_check.py` check the result.

The same pattern for 60 Hz:
1. **The 60 Hz changes as `#if SPYRO_TICK60 … #else <original> #endif` hunks** in `patches/spyro-1-port/` and in our hand C. The 30 Hz compile then preprocesses to exactly today's text.
2. **The update path compiled twice:** the 30 Hz objects as today, and the 60 Hz objects with `-DSPYRO_TICK60` and a names header (`GamestateUpdate_60`, each overlay's `UpdateMoby_60` / `UpdateParticle_60`, the spawns).
3. **One dispatch point:** `main()`'s update call, the clamp, and the draws' `< 2` pacers choose by a mode set at the load (§7.1). In off and interpolated modes those branches take the original path.

**What keeps off and interpolated checkable unchanged:**
- **The 30 Hz objects' code is today's code.** A new ctest can prove it: the preprocessed 30 Hz sources (or the objects' hashes) against a build without the 60 Hz patch.
- **Existing checks keep passing unchanged:**
  - both attract demos tick for tick (they run at 30 Hz by §7.2);
  - host routes byte-identical;
  - oracle function replay;
  - Vita3K shots;
  - the hand-C shadow and fuzz checks (`HAND_SHADOW`), whose references are the 30 Hz mips2c units.
- **What has to be guarded:**
  - **Game state the 60 Hz rules add**, such as the carries of §3 for animation progress. It must live outside the 30 Hz layout and be saved, or `tools/savestate_globals.py` and the arena layout change for everyone. Prefer state that exists only in the 60 Hz objects and is listed by `savestate_globals`.
  - **The dispatch's few shared branches** (`main.c`, `draw.c`, which the presenter build also compiles). They're the only change on the 30 Hz path, and every existing check covers them.
  - **Code size:** the 60 Hz path's code, mostly the 37 overlays' moby classes, is in the ELF twice.
- **What can't be checked** is the 60 Hz mode itself (§4): no reference, no demos. Only the statistics harness and playing it.

**A single compile with run-time `if (g_tick60)` at each site is the alternative.** It's smaller, but it changes the 30 Hz path's machine code at a few thousand sites, so off and interpolated would be checked by behaviour only, not by construction. The second compile is the one that keeps the oracle's guarantees.

### 7.5 What this changes in the recommendation

As an opt-in with the faithful modes compiled from unchanged sources, 60 Hz logic no longer costs **those modes** their faithfulness. That removes the strongest objection in §6. Three costs remain:
- **The work:** §5's ~4–7.5k changed lines, judged class by class, plus ~1–2k for the second compile, the dispatch, the save-state field and the checks.
- **No check of the 60 Hz mode itself** beyond statistics and play. Bugs in it would be found by players, one enemy class at a time.
- **Less to show for it than interpolation:**
  - a mode that's heavier on the game thread than 7f/7g's interpolated one;
  - cutscene cameras still stepping at 30 Hz;
  - a response gain of at most one vblank on half the frames.

If the owner wants it anyway: finish stage 7 first, so interpolation is the default 60 fps. Then build 60 Hz logic as a separate, later track behind a boot-time setting, and move it to the level-load switch once §7.1's carried-state list is checked. Start with one homeworld (Artisans, its levels' classes) to measure the real cost per class before committing to all 218.

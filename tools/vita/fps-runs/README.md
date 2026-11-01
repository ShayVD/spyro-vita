# Hardware runs for the render thread, the XA read-ahead and the opaque path (D63–D66)

These replace the "measure first" runs of `docs/fps-plan.md` (Phase 0). Each file here is a whole
`ux0:data/spyro/config.txt`: copy one over it (keep a copy of your own first), start the game, do
what the run says, quit, and send back `ux0:data/spyro/spyro.log` renamed after the run
(`a1-off.log`, ...). Don't open the options menu during a run: it rewrites `config.txt` with its
rows (the lines here stay, but the run's picture settings would be the menu's).

All runs use the same VPK, the one from this PR. Since D68 the three switches are on by default
(`render_thread=1`, `xa_ahead=1`, `gpu_opaque=1`) and `gpu_time` is 0. The files set each switch
the run needs, and they carry `config_version=3`: without it a `=0` for one of the switches is
taken as an old file's and ignored (D68).

| File | Switches | Do |
|---|---|---|
| `a1-off.txt` | `gpu_time=2`, all three off: as main | 3–5 minutes of play: Artisans (around the castle and the hedges), then one busy level (Stone Hill, or Town Square in Peace Keepers). The same route in A2 and A3 |
| `a2-threads.txt` | `gpu_time=2`, `render_thread=1`, `xa_ahead=1` | the same route |
| `a3-all.txt` | as A2, plus `gpu_opaque=1` | the same route |
| `b1-off.txt` | `bench=2`, 60 fps forced (`interp=1`, `interp_budget=0`), all three off | leave it alone from boot for ~3.5 minutes: it cycles 18 picture option sets through the intro, title and attract demos, then puts the options back |
| `b2-all.txt` | as B1, with all three on | the same |
| `c1-all.txt` | 60 fps with the default budget, `gpu_time=2`, all three on | 2 minutes of play in Artisans |
| `c2-all-nobudget.txt` | as C1 with `interp_budget=0`: every tick gets its in-between frame | the same |
| `d1-60fps.txt` | 60 fps as the menu gives it since D68: the defaults, the budget's hold, no GPU timing | the C1 route: Artisans, then the heavier level |
| `e1-passes-off.txt` | D69 off (`gpu_direct=0`, the passes as before), 60 fps forced (`interp_budget=0`), `gpu_time=2` | the C1 route: Artisans, then the heavier level, 2–3 minutes each. The same route in E2–E4 |
| `e2-passes-on.txt` | as E1 with D69 on (`gpu_direct=1`, the default) | the same route |
| `e3-30fps-off.txt` | D69 off, 30 fps, `gpu_time=2` | the same route |
| `e4-30fps-on.txt` | D69 on, 30 fps, `gpu_time=2` | the same route |
| `f1-cheap-off.txt` | D70 baseline: as C2 (every tick gets its in-between frame), `gpu_lean=0` | 2 minutes in Artisans, the same route as C1 |
| `f2-cheap-on.txt` | as F1 with `gpu_lean=1` | the same route |
| `f3-cheap-60fps.txt` | 60 fps as the menu gives it (D1), with `gpu_lean=1` | the D1 route: Artisans, then the heavier level |
| `h1-parse-30.txt` | the defaults at 30 fps, plus `parse_thread=1` (D71) | the D1 route: Artisans, then the heavier level. Run D1 again first if you have no D1 log from this VPK |
| `h2-parse-60.txt` | D1 (the menu's 60 FPS), plus `parse_thread=1` | the D1 route |
| `h3-parse-core3.txt` | H2 with `parse_core=3`: the parse thread on core 3, if the start-up check allows it | the D1 route; only worth it if the `cores:` line says core 3 is usable |

## What each run shows

The lines are every 300 vblanks (5 s). Compare windows of the same place on the route.

**A1 against A2: the render thread and the read-ahead at 30 fps.**
- `time N:` (the game thread). `raster` should fall from ~4.8 ms per vblank to almost nothing, `ot` by about `expand()`'s share (the `atlas` line's expand ms divided by 300), and `spu` by most of the file-system time. `busy %` should fall from 93–94% to about 50–60% in the heavy windows (*estimate*). In A2 the game's waits for the render thread are inside `wait`.
- `render N:` (A2 only). The render thread's busy ms per vblank and per picture, and the game thread's waits by reason. "previous frame not shown yet" is the time the game waited because the GL thread or the GPU was behind. "sync calls" should be small, except around loads and the pause menu.
- `xa N:` (A2 only). Almost every music sector should come from the read-ahead. The `cd:` line's file-system ms then grows only with level loads.
- `vblank N: ... pictures, ... vblanks late`. At least as many pictures, and no more late vblanks than A1.
- `gpu N: gpu_time=2: the timed frame's scene took the GPU S ms`: the GPU's own time for the picture (Run A of `docs/fps-plan.md`). It shouldn't change between A1 and A2. It decides how far 60 fps can go: ≤ 12 ms, the GPU has room; 12–16, close; > 16, the GPU is the limit.
- `mixer N:`. The mix is unchanged, and the mismatches stay 0.

**A2 against A3: the opaque path.**
- `atlas N: gpu_opaque=1: K of those triangles drawn without discard` (A3 only): how many it applies to.
- The scene time S and the `timed frame`'s tail. If S doesn't fall by at least ~1 ms in the heavy windows, the opaque path doesn't pay and stays off.
- The picture must look the same. If anything looks different with A3 (a hole, a dark edge, a wrong colour), say where.

**B1 against B2: the bench at 60 fps.**
- `bench final:` lines: pictures/s (60 at most) for each option set, with an in-between frame every tick. 16:9 `ps1` against 16:9 native is the same CPU with 4.3 times fewer pixels: what's left between them in B2 is the GPU's share.

**C1 and C2: 60 fps in play, everything on.**
- `present N:`: in-between frames per window, and how many the budget skipped. With the render thread, the budget also counts the render thread's time per picture (D63). C1 shows how often 60 fps fits.
- C2: with every in-between frame forced, `render N:`'s "previous frame not shown yet" and the late vblanks give the real overrun. That's whether the GPU or the render thread is the limit at 60 fps.

**D1: 60 fps as shipped (D68).**
- `present N:`: "falls to a held 30 fps" counts the misses. In a place where 60 fits (Artisans in C1) there should be few or none, and almost every tick has its in-between frame. Where it doesn't fit (C1's heavier level, GPU scene 19–31 ms) the picture should stay at a steady 30: the in-between frames near 0 and a fall or two per window, not C1's mix of 20–120 in-between frames per window.
- No hitch every 5 seconds: `gpu_time` is 0, so there's no `timed frame` line and no `glFinish`.
- The start-up's `gpu: shaders compiled in N ms` line says how long the boot's shader compile takes (C1's two "still waiting (sync calls)" lines at vblank 0 were that compile, more than 10 s).
- The first D1 run (VPK from `131dde2`) never came back from its hold after the first level change: the release test held the render thread to the game thread's budget. Fixed in D68's follow-up, so rerun D1 with a VPK built after it.

**E1 against E2, E3 against E4: fewer full-screen passes (D69).**
- E1–E4 use the VPK built from the D69 branch (`claude/fps-gpu-passes-d958f0`); `gpu_direct=0` gives main's passes in that same VPK.
- `gpu N: ... presentation pictures; C copies (L before a later write), H handed their target's texture, P presents from a target; A atlas decode passes; vitaGL free KB ...` (new in D69). With D69 on, C and L should be near 0 in play: every picture is either shown from its target (P) or takes the target's texture when the game clears it (H). With it off, C equals the pictures.
- `gpu N: gpu_time=2: the timed frame's scene took the GPU S ms`: the game's own target scene. D69 doesn't touch that scene, so S should be the same in both runs at the same place (it is measured before the passes D69 removes). What D69 removes is in the `timed frame`'s `GPU tail after the CPU` (the capture and present passes; 2.6–3.7 ms per picture in C1): it should fall by roughly half (*estimate*: one 960×544 scene of the two).
- At 60 fps (E1, E2): with an in-between frame every tick, each tick had two staging copies and two cleared presents. E2 should show fewer `render N:` "previous frame not shown yet" waits and fewer late vblanks than E1 at the same place, and a lower `render thread busy` per picture.
- The `gpu: target N: ... (CDRAM)` and `gpu: presentation frames ... (K of 7 in CDRAM)` lines at start say where vitaGL put the textures. If some slots are in main RAM, D69 copies into them instead of handing them to a target, so the targets stay in CDRAM; the counts above show how often.
- **At the end of E1 and E2, save a state once** (options menu, SAVE section, `SAVE STATE`), then play 20 more seconds. A save remakes the targets, and in Vita3K they came back in main RAM (`gpu: target N: ... (main RAM)`, D69). The `T of N targets in main RAM` count should stay above 0 in E1 (main's behaviour) and return to 0 within a window in E2. If E1's S rises after the save, main RAM targets cost real GPU time.
- The picture must look the same in all four: the screen, the pause and inventory backgrounds, the menu panel, the save card, the game-over freeze frame.

| Place | S, E1 → E2 (ms) | Tail, E1 → E2 (ms) | Tail, E3 → E4 (ms) | 60 fps late vblanks, E1 → E2 |
|---|---|---|---|---|
| Artisans | C1: 5.5–8.4 → | C1: 2.5–3.6 → | | |
| The heavier level | C1: 18.9–25.8 → | C1: 2.7–2.8 → | | |

(The C1 numbers are main's before D69, `logs/hw-pr40-c1`: Artisans the windows 600–1800, the heavier level 6300–7800. The rest is filled in from E1–E4.)

**F1 against F2: the 28-byte vertex (D70).**
- `gpu N: gpu_time=2: ... vertex uploads K KB in T ms`: K should fall to 70% (28 bytes a vertex instead of 40), and T with it.
- `atlas N: ... expand E ms`: E should fall a little (the second triangle of a quad reuses the first one's lookup).
- `render N:` per picture: the render thread's time, which the two above are part of (*estimate*: −0.2 to −0.5 ms a picture), and the `gpu_time=2` scene time S (the GPU reads less vertex data; any change is likely within the noise).
- The picture must look the same in F2 as in F1.

**F3: 60 fps as shipped, with D70.** Against D1: the `present N:` lines' in-between frames and falls to a held 30 in the same places.

**H1, H2 and H3: the parse thread (D71), against D1 (and a 30 fps run without it).**
- `cores:` at start (every run, not just H): whether a thread of ours may run on core 3 (`SCE_KERNEL_CPU_MASK_SYSTEM`), on which CPU it ran, and what share of a 50 ms spin it had. Expected: refused, or not usable (the system keeps that core).
- `parse:` at start: the thread's core and priority, or why it's off.
- `parse N:` (H only): lists walked on the parse thread, its busy ms per vblank, the copy (KB, and ms per vblank on the game thread), how long the game thread waited for the walk at its joins, and packets outside the copy. **Packets outside the copy must be 0.**
- `time N:` against D1 at the same place: `ot` is now the copy plus the waits for the walk (it was the walk itself: 1.5–2.6 ms per vblank in C1's busy windows). `busy %` should fall by about that much less the copy.
- `present N:` (H2, H3): in-between frames. The parse thread only helps the game thread; where the GPU is the limit (C1's heavier level) 60 fps still won't hold.
- `mixer N:` and `xa N:`: with the walk on core 1 beside the mixer and the read-ahead, the mixer's worst ms and `audio queue dry` mustn't rise, and the read-ahead should still cover every music sector.

## What to look for that would mean a bug

- Anything on screen that differs between the runs at the same place: a missing or late picture, flicker, a wrong pause or inventory background, a menu panel that doesn't show.
- A `render: still waiting` line: the game thread waited 5 s for the render thread. (At start-up the
  shader compile logs `render: GPU start-up still running after N s` instead: expected.)
- `render N: ... unknown` above 0.
- A crash or hang at start (the shaders now compile on the render thread), at a level load or when the pause menu opens.
- With `parse_thread=1`: a `parse: still waiting` line, `packets outside the copy` above 0, a picture that's a frame behind or shows the wrong frame's HUD, or a pause or inventory background that's wrong.
- With `render_thread=1`, a save state saved and loaded (the options menu's SAVE section) not putting you back where you were.

## Runs G1–G3: the hand-written renderers and the inline GTE commands (D72)

The same VPK for all three. `fast_render=1` is the default since D72; G1 turns it off, which puts back the renderers as translated from the asm and PsyCross's GTE decoder. The pictures and the game must be the same in all three: every host and Vita3K check was byte-identical.

| File | Switches | Do |
|---|---|---|
| `g1-fast-off.txt` | `fast_render=0`, 30 fps | 3–5 minutes: Artisans (the castle and the hedges), then one busy level (Stone Hill, or Town Square in Peace Keepers). The same route in G2 and G3 |
| `g2-fast-on.txt` | `fast_render=1`, 30 fps | the same route |
| `g3-fast-60.txt` | `fast_render=1`, the menu's 60 fps | the same route |

**G1 against G2.** The log's start says which: `fast_render=0: translated renderers, ...` or `fast_render=1: hand-written renderers, ...`.
- `draw N:` (per vblank). `env`, `mobys`, `spyro` and `sky` should fall. Today in the heavy scene, per tick (twice the per-vblank numbers): env 4.96, mobys 3.06, sky 0.90, spyro 0.60 ms, 9.7 ms with fx and particles (`docs/fps-plan.md`). `sky` now also counts the sky's direct calls (Artisans'), so G1's `sky` is higher than the older runs'.
- `update` should fall a little: collision's GTE commands are inline too.
- `time N:`: `game` falls by what `draw` lost, and `busy %` with it.

**G2 against G3.** At 60 fps the in-between frame runs the renderers again, so the gain counts twice. `present N:`: more in-between frames per window, and fewer falls to a held 30 in the places where G3 before this change held 30.

**A bug would look like:** anything on screen that differs between G1 and G2 at the same place (a missing or torn polygon, a wrong colour, a flickering sky, Spyro or a moby drawn wrong), or a crash in a level G1 runs.

## Runs I1–I3: the budget's hold lets go (D74)

The main `2a0691c` log (`logs/hw-main-2a0691c`, Toasty) fell to a held 30 and never came back, even where 60 fitted. One VPK, built from the D74 branch, for all three. It logs a new `budget N:` line under each `present N:` line.

| File | Switches | Do |
|---|---|---|
| `i1-60fps.txt` | the menu's 60 FPS: the defaults, budget 26 ms | 2 minutes in Artisans (the castle and the hedges), then 2–3 minutes in a heavier level: Toasty (WARP TO in the menu's GAME section, or walk), or Stone Hill. Don't open the menu |
| `i2-parse-60fps.txt` | as I1, plus `gpu_lean=1 parse_thread=1`: the setup of the log that never came back | the same route, Toasty for the heavier level. Don't open the menu |
| `i3-menu-budget.txt` | as I2 | in Toasty: play a minute, then in the options menu set `60 FPS BUDGET` to 30 MS, play a minute, NEVER SKIP, a minute, 26 MS, a minute. Each change is one menu visit |

**What each run shows.**
- `budget N: B ms; H ticks held, hold L of T at the last; ...`:
  - H is how many of the window's ticks were held at 30. L is how many of the hold's T ticks are left to run (0: it can end once the costs fit).
  - `held again by` counts held ticks over the miss line: cpu avg (the game thread's average over ~4 ticks over B), cpu twice (two ticks in a row over B), render (two pictures over 15/16 of a tick on that tick).
  - `paused by` counts held ticks that fit without the eighth to spare. `counted down` counts ticks that fit with it.
  - Since D74 none of these restarts the hold. The hold ends once it has run T ticks, its ticks' average fits with an eighth to spare, and the tick isn't over the line itself.
  - `falls by` gives the tests that were over at each fall to 30.
  - `held ticks' us` are the held ticks' average costs: cpu (the game thread's time plus the logic pass, standing in for the in-between frame), its parts busy and logic, and render x2 (twice the render thread's time per picture). `most` is the window's largest cpu and render x2. A `most` far above the average is the kind of tick that held main's budget at 30.
- `present N:`: where 60 fits (Artisans; the Toasty places where main's log ran a clean 60 at budget 0), almost every tick has its in-between frame. After a fall, 60 is back within a few seconds, at most two holds of 8 s. Where it doesn't fit, the in-between frames stay near 0. A fall about every 8 s is expected only where the GPU is the limit, which shows only at 60 (`falls by ... late`).
- I3: each budget change starts again (the next `budget` line's T is 30, unless a fall came after the change). NEVER SKIP gives no `budget` line, only `present` lines.

**A bug would look like:**
- a held stretch of more than ~20 s (4 windows) whose `held ticks' us` fit, meaning cpu under 7/8 of B and render x2 under 29.2 ms;
- in-between frames that come and go every window in one place (flicker);
- falls in Artisans.

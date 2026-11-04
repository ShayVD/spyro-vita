# Hardware runs for the render thread, the XA read-ahead and the opaque path (D63–D66)

These replace the "measure first" runs of `docs/fps-plan.md` (Phase 0). Each file here is a whole
`ux0:data/spyro/config.txt`: copy one over it (keep a copy of your own first), start the game, do
what the run says, quit, and send back `ux0:data/spyro/spyro.log` renamed after the run
(`a1-off.log`, ...). Don't open the options menu during a run: it rewrites `config.txt` with its
rows (the lines here stay, but the run's picture settings would be the menu's).

All runs use the same VPK, the one from this PR. Since D68 the three switches are on by default
(`render_thread=1`, `xa_ahead=1`, `gpu_opaque=1`) and `gpu_time` is 0. The files set each switch
the run needs, and they carry `config_version=3`: without it a `=0` for one of the switches is
taken as an old file's and ignored (D68). Since D74 `gpu_lean=1` (D70) and `parse_thread=1` (D71)
are on by default too: the files no longer set them, and a file that wants one off says `=0`.

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
| `f2-cheap-on.txt` | as F1 with `gpu_lean=1` (the default since D74) | the same route |
| `f3-cheap-60fps.txt` | 60 fps as the menu gives it (D1), with `gpu_lean=1` (the default since D74) | the D1 route: Artisans, then the heavier level |
| `h1-parse-30.txt` | the defaults at 30 fps, with `parse_thread=1` (D71; the default since D74) | the D1 route: Artisans, then the heavier level. Run D1 again first if you have no D1 log from this VPK |
| `h2-parse-60.txt` | D1 (the menu's 60 FPS), with `parse_thread=1` (the default since D74) | the D1 route |
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

The same VPK for all three. `fast_render=1` was the default from D72 to D81 (2 since); G1 turns it off, which puts back the renderers as translated from the asm and PsyCross's GTE decoder. The pictures and the game must be the same in all three: every host and Vita3K check was byte-identical.

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

## Runs I1–I3: a cheaper GPU picture (D74)

One VPK, built from the D74 branch, for all three. Main's Toasty log (`logs/hw-main-2a0691c`) had the render thread at 16–23 ms a picture in the heavy views, and C1's `gpu_time=2` timed the GPU's scene at 19–31 ms there. D74 found that the pixels the GPU shades don't separate Toasty from Artisans. What does is the number of draws and program changes: 150–230 shader runs a picture in Toasty, against about 80 in Artisans. Textured and untextured triangles alternate in the game's draw order, and PsyCross starts a new draw at each change.

| File | Switches | Do |
|---|---|---|
| `i1-bench.txt` | 60 fps forced, `gpu_bench=1` (main's setup otherwise) | Go to Toasty: WARP TO in the options menu's GAME section, or walk. Find the heaviest view you can (the one with the most of the level in sight), then **stand still** and leave the pad alone for 2 minutes. Then do the same for 1 minute in Artisans. Don't open the menu while standing |
| `i2-join.txt` | 60 fps as the menu gives it, the default budget, the defaults (`gpu_join=1`, and since I1 `gpu_lean=1` and `parse_thread=1`) | the route of main's log: Artisans, then Toasty, 2–3 minutes each |
| `i3-uber.txt` | as I2, plus `gpu_uber=1` | the same route |

**I1.**
- **The variant lines:** each 5-s window draws one of five ways and logs `gpu N: gpu_bench=1: VARIANT: the GPU's scene X ms a picture, K pictures timed`. X is the GPU's time for the game's picture after its last draw (gpu_time=2's measure), averaged over every 16th picture. The five, in turn:
  1. `main's draws (gpu_join=0)`: as main.
  2. `gpu_join=1`: consecutive draws joined.
  3. `gpu_join=1 gpu_uber=1`: plain and untextured triangles in one program.
  4. `gpu_join=1, half the pixels (diagnostic)`: the scissor cut to the left half. **The picture changes**: half the screen isn't redrawn.
  5. `gpu_join=1 gpu_uber=1, one program for every opaque triangle (diagnostic)`: no discard, no program changes. **The picture changes**: transparent texels show black.
- **What the numbers tell, at the same place:**
  - 1 against 2 is what joining saves.
  - 2 against 3 is what one program saves, net of the untextured pixels now sampling a texture.
  - 4 against 2 is the share that scales with pixels: if X falls by nearly half, the GPU is fill-bound; if it hardly moves, it's bound by the draws and their state.
  - 5 is the floor that fewer program changes could reach.
  - The `render N:` line's "busy ... per picture" and the `vblank N:` line's late vblanks, window by window, give the same comparison from the render thread's side.

**I1's result** (`hw-pr49-i1`, PR #49 at `8b59312`; Toasty from vblank 2509, 48 s standing; no Artisans windows). The GPU's scene a picture, two windows of each variant:

| Variant | Scene ms | Render thread ms a picture | Late vblanks in 300 |
|---|---|---|---|
| 1. main's draws | 14.65, 20.77 | 13.93, 18.04 | 65, 116 |
| 2. joined (the default) | 7.30, 6.10 | 8.60, 11.33 | 30, 11 |
| 3. joined, one program | 7.79, 8.84 | 11.13, 17.42 | 12, 20 |
| 4. half the pixels | 3.28, 4.46 | 15.43, 9.71 | 16, 6 |
| 5. one program for all | 7.92, 7.11 | 15.78, 16.31 | 29, 12 |

- Joining halves to thirds the GPU's scene in Toasty: the change that holds 60 better.
- One program (`gpu_uber`) saves nothing once draws are joined (3 is no faster than 2, and 5, the floor, isn't either): it stays off.
- Half the pixels takes off 40–50%: what's left of the GPU's time now scales with pixels more than with draws.
- The run stutters by design: every 5 s it switches to another variant (main's draws among them), and every 16th picture it waits for the GPU to time it. Variant 4 leaves the right half of the screen unredrawn (it showed orange), and 5 shows transparent texels black.

**I2 and I3, against main's log.**
- `render N:`: the render thread's time per picture in Toasty (16–23 ms in main's 60-fps windows) and its busy %.
- `vblank N:`: late vblanks.
- `present N:`: in-between frames and falls to a held 30.
- The picture must look the same as main's everywhere: Toasty's distant ground, the sky, Spyro, gems, the HUD, the pause and inventory screens.

**A bug would look like:** a triangle missing or drawn in the wrong place, a wrong colour on the ground, or a menu or pause background that differs from main. Variants 4 and 5 of I1 are meant to look wrong.

## Runs J1–J5: dynamic resolution (D75)

The same VPK for all five (built from the D75 branch). `dynres=1` (auto) was the default until J6 (now `0`, off; the J files set what they need); `dynres=0` puts back native resolution always, drawn exactly as main draws it. The route is the one in `logs/hw-main-2a0691c`: a minute in Artisans, then **Toasty** (level 14) for 3–5 minutes, the same paths each time, ending with the heavy views that ran late there (vblanks ~2100–3600 and 7500–9300 of that log: the open areas with the most on screen). Don't open the options menu during J1–J3 (the menu's windows are paused and their timings mean nothing).

| File | Switches | Do |
|---|---|---|
| `j1-dynres-off.txt` | 60 fps forced (`interp=1 interp_budget=0`), `dynres=0`, `gpu_meter=1` (the meter on with dynres off), `gpu_lean=1 parse_thread=1` as in `hw-main-2a0691c` | the route |
| `j2-dynres-auto.txt` | as J1 with `dynres=1`: the scale is the only thing that reacts to the GPU | the same route |
| `j3-dynres-75.txt` | as J1 with `dynres=75`: always 720×408 | the same route |
| `j4-menu-60fps.txt` | the menu's defaults with 60 FPS on: `dynres=1`, and the game thread's safety net (`interp_budget` on) | the same route, then open the menu once and look at the DYNAMIC RES row |
| `j5-menu-30fps.txt` | the menu's defaults (30 fps), `dynres=1` | the same route |
| `j6-one-run.txt` | if only one run: J4 with `gpu_lean=1 parse_thread=1` as in `hw-main-2a0691c` (the meter measures at whatever scale auto picks; `dynres_sim.py` scales it to 960×544) | the same route, then open the menu once |

**What each log line should show:**
- At start: `gpu: GPU time from vitaGL's scene fences, polled every 200 us on core 1 (D75)` in J1–J5. Then, at the first picture, `dynres: off, scale 100%` (J1), `auto, scale 100%` (J2, J4, J5) or `fixed, scale 75%` (J3). **If `gpu: no scene-done notifications from the GPU` appears, the meter doesn't work on hardware: J2 and J4 stay at 100% and the rest of these runs measure nothing.**
- `dynres N: GPU time per picture 100%: P pictures, X ms; ...; D down, U up, now S%`: every 300 vblanks. X is the GPU's busy time per picture at each scale in that window, measured from the scene fences with no `glFinish`.
  - **J1** gives the GPU's cost at 960×544 in every window. In `hw-main-2a0691c`'s late windows the render thread took 16–24 ms a picture (busy × 300 / pictures), so X there should be about 16–24 ms, and 7–9 ms in the windows that held 60 (4200–4500). If X is far below the render thread's per-picture time where pictures were late, the GPU isn't the wall there and dynres can't help.
  - **J3** against J1 at the same place: X at 75% should be about 0.56 of J1's plus a fixed part (*estimate*: 1–2 ms). Those two numbers per window give the model behind the steps (D75's table): send both logs.
  - **J2**: the scale is a last resort (D75): native wherever J1's X plus 1.5 ms fits in 16.7 ms, even with no room to spare. In the heavy ones `now 88%` or `75%`, rarely `67%`, with `vblanks late` near 3 per window (J1 had 40–100 there) and `render N:` per picture under 16.7 ms. `tools/vita/dynres_sim.py j1.log` says which windows should drop and how far: J2 should drop no more than that. `D down, U up` should be a handful per minute at most: more than one down and one up in most windows is pumping (say where).
  - `dynres: GPU ... ms a picture at A% (with the present's 1.5 ms), over 16.68 ms for 2 windows: B%` and `... predicted to fit: B%` lines: each change, with the cost that caused it.
- `vblank N: ... pictures, ... vblanks late` and `render N: ... per picture`: J2 against J1 at the same place. That's the gain.
- **J4**: as J2, with the game thread's safety net on. **60 fps should never fall to 30 now** (D75): `present N: ... 0 skipped for the budget (0 falls to a held 30 fps)` in every window, where D1 and H2 held 30 in the heavy views. A fall would mean the game thread itself took more than 31.3 ms a tick (`time N:` busy over ~94%): say where. The menu shows `DYNAMIC RES < AUTO >` and has no `60 FPS BUDGET` row any more.
- **J5**: a picture may take two vblanks at 30 fps, so dynres should stay at `now 100%` with 0 down in every window (Toasty's heaviest views took 16–24 ms, under 94% of 33.4).

**The picture:** with dynres on, the heavy views turn a little softer (bilinear upscaling from 840×476, 720×408 or 641×363) and sharpen again within a few seconds of the view getting lighter. A bug would look like: a picture shifted, stretched or with a band of an older picture at its right or bottom edge, the in-between frames jumping against the real ones, a flash at the moment of a change, the pause or inventory background looking wrong, or the scale changing back and forth every second or so in one place.

## Run J7: where the GPU's time goes (after J6)

J6 (`j6-one-run.txt`, the logs in `logs/hw-j6/` of the main checkout) showed that at 67% the GPU still took 11–17 ms a picture in Toasty's heavy views, and a 60–150 ms hitch in nearly every 5 s window. J7 runs the build after it: dynres off (the new default), the log written by its own thread, and the meter splitting the GPU's time by kind of scene.

| File | Switches | Do |
|---|---|---|
| `j7-gpu-split.txt` | 60 fps on (`interp=1`, the safety net on), `dynres=0`, `gpu_meter=1`, `gpu_lean=1 parse_thread=1` | J6's route: a minute in Artisans, then Toasty 3–5 minutes ending in the heavy views. Don't open the menu |

**What the log should show:**
- `log: written by a thread on cores 1-2` near the start.
- `time N: ... worst vblank X ms`: X under ~40 ms in most windows if the log's writes were J6's hitch. Still 60+ in every window means they weren't: say so, and whether you still feel a hitch every few seconds.
- `gpu meter N: GPU time per picture over P pictures: the game's frames A ms in a scenes; texture decodes B ms in b; other C ms in c; the present D ms in d (with its wait for the display); the most finished between two pictures M ms`. In the heavy views A + B + C over ~15 ms is the GPU over its vblank. Which of A and B is big says where the next saving is: B large, the atlas's decode passes; A large with few scenes, the targets' own scenes.
- The picture is native throughout (dynres off).

## Run M1: the XA read-ahead after level changes (D78)

K1–K3 (`docs/native-plan.md` §3.4) showed the read-ahead (D64) serving no music sector after the first level load: the music's sectors went back to the game thread for the rest of the run. D78 lets it reuse its windows after the stream jumps back on the disc.

| File | Switches | Do |
|---|---|---|
| `m1-xa-ahead.txt` | 30 fps (`interp=0`), the shipped threads, `xa_ahead=1`, `dynres=0` | the K route: leave it alone from boot through the title, the flythrough and the Stone Hill attract demo (about 1.5 minutes), then start or load a game: 2 minutes in Artisans, then into Stone Hill for 2 minutes. More level changes (back to Artisans, another level) are welcome. Don't open the menu. Send it as `m1.log` |

**Unattended** (D77's runner, no one at the Vita): `DEVICE_DEFAULTS=1 PAD=tools/vita/hw-routes/k-route.pad tools/vita/hw_run.sh logs/hw-m1 1500 @tools/vita/fps-runs/m1-xa-ahead.txt @tools/vita/hw-routes/m1-xa-k.cfg`. It plays the same route, with New Game from a pad script and the menu's LEVEL WARP to Stone Hill and then Dark Hollow, and quits at vblank 18000. It ran on 2026-11-02 (D78, `logs/d78-xa/hw-m1-*` in the main checkout).

**What the log should show:**
- `xa N: A music sectors from the read-ahead, B read on the game thread; read-ahead W windows, ...; C asks found no window` every 300 vblanks. After every level change (the `where:` lines), A should be back near 750 and B near 0 within one window; K1–K3 had A = 0 and W = 0 from the first load on. B is a handful (2–10) in the window where a new track starts. C should stay 0: a non-zero C for several windows in a row is the old stall.
- `mixer N: ... 0 position mismatches` throughout.
- The `time` lines' `spu` part in play back to ~0.05 ms per vblank (K1–K3 had 0.54–0.58 after the first load).

## Runs O1 and O2: does 60 fps hold, with everything merged? (D81)

Main's defaults with the menu's 60 FPS on (`interp=1`, nothing else changed), D75's GPU meter on (`gpu_meter=1`), and `fast_render` 1 and 2 (stage 1 of `docs/native-plan.md`, D80). Unattended only (D77's runner); each run needs the owner's yes first.

| File | Switches | Route |
|---|---|---|
| `o1-60-fr1.txt` | `interp=1 gpu_meter=1 fast_render=1` | the K route (`hw-routes/k-route.pad`, `m1-xa-k.cfg`: title, attract demo, New Game, Artisans, Stone Hill, Dark Hollow; 18,000 vblanks), then the Toasty route (`hw-routes/o-toasty.pad`, `o-toasty.cfg`: the same start, LEVEL WARP to Toasty, a walk with the camera turned all the way round both ways; 21,000 vblanks) |
| `o2-60-fr2.txt` | O1 with `fast_render=2` | the same two routes |

**The timing runs** (`REALTIME=1`, real vblanks, no shots):
```
REALTIME=1 PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-o1-k 900 @tools/vita/fps-runs/o1-60-fr1.txt @tools/vita/hw-routes/m1-xa-k.cfg
REALTIME=1 PAD=$PWD/tools/vita/hw-routes/o-toasty.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-o1-t 900 @tools/vita/fps-runs/o1-60-fr1.txt @tools/vita/hw-routes/o-toasty.cfg
```
and the same two with `o2-60-fr2.txt` (`logs/hw-o2-k`, `logs/hw-o2-t`).

**The same-pictures runs** (virtual vblanks, the K route's shots and GPU pictures): `PAD=... DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-o1-shots 1500 @tools/vita/fps-runs/o1-60-fr1.txt @tools/vita/hw-routes/n-gte-k.cfg`, then O2. The 18 pictures and the memory card should be identical, as N1/N2's were at 30 fps.

**To read them:** `python3 tools/vita/fps60_summary.py logs/hw-o1-k/spyro.log` gives per level the late vblanks per 300, the falls to 30, the render thread's ms per picture, the GPU meter's ms per picture and the game thread's busy %; `--all` lists every window. Compare with `logs/hw-main-2a0691c` (Toasty, before D74) and `logs/hw-j6` in the main checkout.

**Ran on 2026-11-02** (D81; the logs `logs/hw-o*` of the `stage1-finish` worktree): 60 fps held in play on both routes. The attract demo's flight through Stone Hill is the one place it slips, bound by the render thread. The same-pictures pairs were identical. The NEON experiment used N2's set-up (`n2-gte-2.txt`, the K route with `n-gte-k.cfg`), with two VPKs built from the same tree, without and with D81's NEON variant of `gte_native.h` (not kept): the pictures identical, the renderers 5% slower with NEON, so NEON isn't kept.

## Runs P1–P6: native draw lists (D82)

Stage 2 of `docs/native-plan.md`: the hand renderers' primitives reach PsyCross's OT walk as the vertices it would make of them (`native_draw=1`), so the walk copies records instead of parsing primitives. All six run `fast_render=2` (D80) on the same VPK; the pictures and the memory card must be the same with `native_draw` 0 and 1.

| File | Switches | Route |
|---|---|---|
| `p1-nd-off-cpu.txt` | K1's: 30 fps, every layer on the game thread (`render_thread=0`, `parse_thread=0`), `native_draw=0` | the K route (`hw-routes/k-route.pad`, `n-gte-k.cfg`) |
| `p2-nd-on-cpu.txt` | P1 with `native_draw=1` | the same |
| `p3-nd-off-threads.txt` | K3's: the shipped threads at 30 fps, `gpu_meter=1`, `native_draw=0` | the same |
| `p4-nd-on-threads.txt` | P3 with `native_draw=1` | the same |
| `p5-nd-off-60.txt` | the menu's 60 fps (`interp=1`), `gpu_meter=1`, `native_draw=0` | the Toasty route (`hw-routes/o-toasty.pad`, `o-toasty.cfg`), `REALTIME=1` |
| `p6-nd-on-60.txt` | P5 with `native_draw=1` | the same |

**Unattended** (D77): `VPK=... PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-p1 1500 @tools/vita/fps-runs/p1-nd-off-cpu.txt @tools/vita/hw-routes/n-gte-k.cfg` (P2–P4 the same with their file), and `REALTIME=1 VPK=... PAD=$PWD/tools/vita/hw-routes/o-toasty.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-p5 1500 @tools/vita/fps-runs/p5-nd-off-60.txt @tools/vita/hw-routes/o-toasty.cfg` (P6 the same).

**What the logs should show:**
- The log's start: `native_draw=1: the hand renderers' primitives as native draw lists (D82)` in P2, P4 and P6.
- `native draw N:` every 300 vblanks (P2, P4, P6): primitives as markers, how many joined a marker, and 0 left as primitives (a full pool).
- P1 against P2: the `time` lines' `ot` (the walk, on the game thread) falls; the `draw` lines' renderers may rise a little (they now build the vertices).
- P3 against P4: the `parse` lines' busy ms per vblank and the game thread's waits for the walk fall.
- P5 against P6: late vblanks, in-between frames and the threads' ms per vblank.
- K1/K3 runs' shots at the same vblanks: identical pictures and card between each pair.

## Runs P7 and P8: native draw lists without the PS1 stores (D86)

D82's P6 showed the walk's thread 0.46 ms per vblank faster and the renderers 0.52 ms slower: they built every PS1 primitive and then its record. D86 builds the records from the values in the hottest sites and skips the primitives' stores, with a smaller record. P7 and P8 are P5 and P6 again on D86's VPK.

| File | Switches | Route |
|---|---|---|
| `p7-nd2-off-60.txt` | P5's: the menu's 60 fps, `gpu_meter=1`, `native_draw=0` | the Toasty route, `REALTIME=1` |
| `p8-nd2-on-60.txt` | P7 with `native_draw=1` | the same |

**Unattended** (D77): `REALTIME=1 VPK=... PAD=$PWD/tools/vita/hw-routes/o-toasty.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-p7 1500 @tools/vita/fps-runs/p7-nd2-off-60.txt @tools/vita/hw-routes/o-toasty.cfg` (P8 the same with its file and `logs/hw-p8`).

**What the logs should show:**
- `native draw N:` every 300 vblanks in P8: 0 left as primitives (a full pool); the most in a list about 30% under P6's 71–114 KB.
- P7 against P8: the `draw` lines' renderers no slower (P6 had them +0.52 ms), the `parse` line's busy time and the game thread's wait for the walk lower, the `time` line's game thread busy time lower; late vblanks no worse.
- The memory card identical.

**Ran 2026-11-03** (`logs/hw-p7`, `logs/hw-p8`; D86): both reached `frames=21000`. P7 matches P5. P8's renderers are +0.53 ms per vblank over P7 (as P6 over P5), the game thread 4.39 → 4.97 ms, late vblanks 2.6 → 3.4 per 300. From vblank 16,912 P8 took another course (more late vblanks, so different steps under `realtime=1`) and ended at game over, so the cards differ.

## Runs T1–T3, P9 and P10: where the records' renderer cost goes, and the lean records (D91)

`nd_ab=1` makes variants of the record path take turns by blocks of 8 lists (the first 2 not counted), so one run times them all in the same scenes. Its `draw ab N:` and `parse ab N:` lines are summed by `tools/vita/nd_ab_summary.py`.

| File | Switches | Route |
|---|---|---|
| `t1-nd-ab-60.txt` | P8's with `nd_ab=1` | T1 on D91's VPK 1 (seven variants: off, D86, the side table, `pldw`, both, a pool-write probe with and without `pldw`), T2 on VPK 2 (off, side + `pldw`, uncached pools copied or walked in place, the probe cached and uncached, D86); the Toasty route, `REALTIME=1` |
| `t3-nd-lean-ab-60.txt` | the same, on the lean records' VPK: `native_draw` 0 and 1 in turn | the same |
| `p9-nd3-off-60.txt` | P7's: the menu's 60 fps, `gpu_meter=1`, `native_draw=0` | the same |
| `p10-nd3-on-60.txt` | P9 with `native_draw=1` | the same |

**Unattended:** `REALTIME=1 VPK=... PAD=$PWD/tools/vita/hw-routes/o-toasty.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-t3 1500 @tools/vita/fps-runs/t3-nd-lean-ab-60.txt @tools/vita/hw-routes/o-toasty.cfg` (the others the same with their file and log folder).

**Ran 2026-11-03** (`logs/hw-t1` … `hw-t3`, `hw-p9`, `hw-p10`; D91): all reached `frames=21000`. T1/T2: a second write stream is most of D86's extra renderer cost; uncached pools and `pldw` don't help the records. T3: the lean records cost the renderers +0.16 ms per list (D86 +0.28–0.33). P9 → P10: the game thread 5.79 → 5.73 ms per vblank, the parse thread 2.48 → 1.76, late vblanks 2.53 → 2.78 per 300 (the attract demos'; Toasty play 24 → 23), the same course. `native_draw=1` is the Vita's default since.

## Run U1: native_draw 0, 1 and 2, and the render thread's two ways (D92)

One run, two A/Bs in the same scenes. `nd_ab=1` now takes `native_draw` 0, 1 (the records) and 2 (the renderers' PS1 primitives, parsed by the walk with the records' converter) in turn by blocks of 8 lists; `gpu_fused=2` takes the render thread's way before D92 (f0) and `gpu_fused=1`'s (f1) in turn, a vertex buffer each.

| File | Switches | Route |
|---|---|---|
| `u1-nd-parse-fused-ab-60.txt` | T3's with `gpu_fused=2` | the Toasty route, `REALTIME=1` |

**Unattended:** `REALTIME=1 VPK=... PAD=$PWD/tools/vita/hw-routes/o-toasty.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-u1 1500 @tools/vita/fps-runs/u1-nd-parse-fused-ab-60.txt @tools/vita/hw-routes/o-toasty.cfg`

**Ran 2026-11-04** (`logs/hw-u1`; D92): it reached `frames=21000`. Per list, against `native_draw=0`:
- records: game thread −0.19 ms, parse thread −0.95;
- `native_draw=2`: −0.18 and −0.51.

So records stay the default. `gpu_fused=1`: −0.275 ms of render thread per vertex buffer (9%). Main's eboot was put back after.

**What the logs should show:** `tools/vita/nd_ab_summary.py logs/hw-u1/spyro.log`: v2's renderers against v0 within noise (it builds no records), its walk and wait against v1's, and the "game" column (renderers plus wait) of v1 against v2. `tools/vita/fused_ab_summary.py logs/hw-u1/spyro.log`: f1's per-buffer render-thread time (vertices and issue) against f0's. `frames=21000` with no hang line.

## Run Q: collision as hand-written C (D83)

Stage 3 of `docs/native-plan.md` replaces `asm/collision.s`'s mips2c unit with readable C (`src/game/collision_hand.c`). Its results feed gameplay, so the check is the same route played on main's VPK and on this one: every picture and the memory card must come out identical.

| File | Switches | Do |
|---|---|---|
| `q1-collision.txt` | N2's: 30 fps (`interp=0`), `render_thread=0`, `parse_thread=0`, `fast_render=2`, `dynres=0` | unattended, the K route, once with main's VPK (Q1) and once with this branch's (Q2) |

**Unattended:** `VPK=MAIN.vpk DEVICE_DEFAULTS=1 PAD=$PWD/tools/vita/hw-routes/k-route.pad tools/vita/hw_run.sh logs/hw-q1 1500 @tools/vita/fps-runs/q1-collision.txt @tools/vita/hw-routes/n-gte-k.cfg`, then the same with this branch's VPK into `logs/hw-q2`. The pictures (`shot-*`, and `gpu-*` with `shot_hd=1`) and `save/` are compared with `cmp`.

**Ran** on 2026-11-02 (D83): Q1 with main's VPK (`6128154`), Q2 with this branch's; all 18 pictures and the memory card identical, and the `update` lines 0.278 → 0.230 ms per vblank. Again after merging main (D81, D82) into the branch: Q3 with main's CI VPK (`5403b03`), Q4 with the merge's (`6855c41`): all 18 pictures and the memory card identical, `update` 0.279 → 0.228 ms per vblank.

## Run U: the cyclorama set-up, the moby lists, the particle slots, the images, `func_8003BFC0` and the credits as hand-written C (D94)

Stage 3's third round, checked as Q and S were. The K route ran on main's VPK (U1) and then on this branch's (U2). Every picture and the memory card must come out identical.

| File | Switches | Do |
|---|---|---|
| `u1-stage3-round3.txt` | Q's: 30 fps (`interp=0`), `render_thread=0`, `parse_thread=0`, `fast_render=2`, `dynres=0` | unattended, the K route, once with main's VPK (U1) and once with this branch's (U2) |

**Unattended:** `VPK=$PWD/MAIN.vpk DEVICE_DEFAULTS=1 PAD=$PWD/tools/vita/hw-routes/k-route.pad tools/vita/hw_run.sh logs/hw-u1 1500 @tools/vita/fps-runs/u1-stage3-round3.txt @tools/vita/hw-routes/n-gte-k.cfg`, then the same with this branch's VPK into `logs/hw-u2`. Give `VPK` as an absolute path: the script can't open a relative one.

**Ran** on 2026-11-04 (D94): U1 with main's VPK (`3bfe4f7`), U2 with this branch's.
- All 18 pictures and the memory card identical; no crash dump.
- `update` went from 0.204 to 0.189 ms per vblank.
- The `sky` line counts the sky twice now: the hand set-up calls `func_8004EBA8` by name, and the profile wraps both functions.


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

## What to look for that would mean a bug

- Anything on screen that differs between the runs at the same place: a missing or late picture, flicker, a wrong pause or inventory background, a menu panel that doesn't show.
- A `render: still waiting` line: the game thread waited 5 s for the render thread. (At start-up the
  shader compile logs `render: GPU start-up still running after N s` instead: expected.)
- `render N: ... unknown` above 0.
- A crash or hang at start (the shaders now compile on the render thread), at a level load or when the pause menu opens.
- With `render_thread=1`, a save state saved and loaded (the options menu's SAVE section) not putting you back where you were.

## Runs F1–F3: the hand-written renderers and the inline GTE commands (D70)

The same VPK for all three. `fast_render=1` is the default since D70; F1 turns it off, which puts back the renderers as translated from the asm and PsyCross's GTE decoder. The pictures and the game must be the same in all three: every host and Vita3K check was byte-identical.

| File | Switches | Do |
|---|---|---|
| `f1-fast-off.txt` | `fast_render=0`, 30 fps | 3–5 minutes: Artisans (the castle and the hedges), then one busy level (Stone Hill, or Town Square in Peace Keepers). The same route in F2 and F3 |
| `f2-fast-on.txt` | `fast_render=1`, 30 fps | the same route |
| `f3-fast-60.txt` | `fast_render=1`, the menu's 60 fps | the same route |

**F1 against F2.** The log's start says which: `fast_render=0: translated renderers, ...` or `fast_render=1: hand-written renderers, ...`.
- `draw N:` (per vblank). `env`, `mobys`, `spyro` and `sky` should fall. Today in the heavy scene, per tick (twice the per-vblank numbers): env 4.96, mobys 3.06, sky 0.90, spyro 0.60 ms, 9.7 ms with fx and particles (`docs/fps-plan.md`). `sky` now also counts the sky's direct calls (Artisans'), so F1's `sky` is higher than the older runs'.
- `update` should fall a little: collision's GTE commands are inline too.
- `time N:`: `game` falls by what `draw` lost, and `busy %` with it.

**F2 against F3.** At 60 fps the in-between frame runs the renderers again, so the gain counts twice. `present N:`: more in-between frames per window, and fewer falls to a held 30 in the places where F3 before this change held 30.

**A bug would look like:** anything on screen that differs between F1 and F2 at the same place (a missing or torn polygon, a wrong colour, a flickering sky, Spyro or a moby drawn wrong), or a crash in a level F1 runs.

# Hardware runs for the render thread, the XA read-ahead and the opaque path (D63–D66)

These replace the "measure first" runs of `docs/fps-plan.md` (Phase 0). Each file here is a whole
`ux0:data/spyro/config.txt`: copy one over it (keep a copy of your own first), start the game, do
what the run says, quit, and send back `ux0:data/spyro/spyro.log` renamed after the run
(`a1-off.log`, ...). Don't open the options menu during a run: it rewrites `config.txt` with its
rows (the lines here stay, but the run's picture settings would be the menu's).

All runs use the same VPK, the one from this PR. Every switch is off by default. The files only
turn on what each run compares.

| File | Switches | Do |
|---|---|---|
| `a1-off.txt` | `gpu_time=2`, all three off: as main | 3–5 minutes of play: Artisans (around the castle and the hedges), then one busy level (Stone Hill, or Town Square in Peace Keepers). The same route in A2 and A3 |
| `a2-threads.txt` | `gpu_time=2`, `render_thread=1`, `xa_ahead=1` | the same route |
| `a3-all.txt` | as A2, plus `gpu_opaque=1` | the same route |
| `b1-off.txt` | `bench=2`, 60 fps forced (`interp=1`, `interp_budget=0`), all three off | leave it alone from boot for ~3.5 minutes: it cycles 18 picture option sets through the intro, title and attract demos, then puts the options back |
| `b2-all.txt` | as B1, with all three on | the same |
| `c1-all.txt` | 60 fps with the default budget, `gpu_time=2`, all three on | 2 minutes of play in Artisans |
| `c2-all-nobudget.txt` | as C1 with `interp_budget=0`: every tick gets its in-between frame | the same |

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

## What to look for that would mean a bug

- Anything on screen that differs between the runs at the same place: a missing or late picture, flicker, a wrong pause or inventory background, a menu panel that doesn't show.
- A `render: still waiting` line: the game thread waited 5 s for the render thread.
- `render N: ... unknown` above 0.
- A crash or hang at start (the shaders now compile on the render thread), at a level load or when the pause menu opens.
- With `render_thread=1`, a save state saved and loaded (the options menu's SAVE section) not putting you back where you were.

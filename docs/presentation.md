# Presentation: 16:9 widescreen and 60 fps over 30 Hz logic

Prototype on the linux32 host (2026-10-30). Both features are optional, and off is the default. With both off, nothing in this document runs: the attract demos match the oracle tick for tick, and every screenshot is byte-identical to a build without this code. With them on, the logic trace is still identical; only the picture changes. Code: `src/game/present.c`, plus hooks listed under [What changed](#what-changed). Decision: D30 in `docs/decisions.md`.

## Switches (linux32 host)

| Flag | Effect |
|---|---|
| `--widescreen` | 16:9 picture. The logic still sees the original 4:3 frustum (faithful). |
| `--widescreen-logic` | 16:9 in one pass: cheaper, but the wider frustum reaches gameplay (see [Why a logic pass](#why-a-logic-pass)). Diagnostic only. |
| `--interp` | 60 fps: one extra frame between ticks, with the camera, Spyro, mobys, their animation frames and the particles blended. |
| `--interp-no-anim` | 60 fps, transforms only (no animation frames or particles); for comparisons. |
| `--interp-camera` | 60 fps, camera only. |
| `--out960` | Pictures (shots, `--serve`) at 960×544: full width for widescreen frames, 4:3 pillarboxed otherwise. Turned on by any of the above. |
| `--present-stats` | Times update, draw, passes, snapshot and blending; logs a summary at exit. `--present-dirty` also checks the region snapshot against a full copy of the arena and lists any page it would have missed. |

`PresentConfig g_present` (`src/game/present.h`) holds the same switches for other platforms.

## The idea in one paragraph

The game's own `GamestateDraw` is the render path. A **presentation pass** snapshots everything the game can see, adjusts that state (a narrower projection; a blended camera and blended mobys), runs `GamestateDraw` with time frozen, captures the picture and restores the snapshot. None of its side effects reach the game. The **logic pass** is the original `GamestateDraw`, unchanged, run once per tick as before. When widescreen is on, the logic pass's `DrawOTag` is skipped, so it only leaves behind its side effects, computed with the original 4:3 frustum. The logic never runs twice, and it never sees a presentation-only value.

```
           vblank 2N            2N+1                2N+2
logic   ── Update(N) ─┬──────────────────────────── Draw(N) [logic pass] ── Update(N+1) ...
                      ├ snapshot
                      ├ blend(N-1, N) → pass → capture I(N-1,N) → restore   (--interp)
                      └ pass (16:9) → capture F(N) → restore               (--widescreen)
shown          ...F(N-1)          I(N-1,N)            F(N)  ...
```

The presenter queue shows one captured frame per vblank. Real frames land on the same vblanks as the original's, byte for byte, so interpolation adds no latency (`logs/run3-interp` vs `logs/run3-off960`, vblanks 4400–4431).

## Widescreen

### Projection: the camera matrix, not the GTE

D2 planned to scale X in RTPS/RTPT. The prototype found a better place: the projection matrix.

- `CameraUpdateMatrices` (`camera.c`) builds `g_Camera.m_ProjectionMatrix` from the view matrix, scaling the Y row by 320/512 for the 4:3 TV. Widescreen scales the X row by 3/4 the same way (`PortProjectionHook`). That squeezes the 16:9 view into the 512 PS1 columns, which the display then stretches to 16:9.
- Projecting in that space also widens every frustum test done in it. `r_moby` culls mobys with `MVMVA` against the projection matrix and tests `4|X| < 3Z` (`r_moby.s` around `0x8001F2E0`, and `func_80022A2C`). With the X row at 3/4, the same test accepts `|X| < Z`, the 16:9 frustum. Screen-space culls (after RTPS) need nothing: everything visible still lands in 0–511.
- A GTE-level squeeze would also hit gameplay. RTPS projects Spyro for the camera logic (`func_80017AA4`, called by `camera.c:433`, `func_80034480`, `func_800357A4`; `moby_update.c:25866`) through the same matrix, and the culls would still need widening one by one.
- The level-transition spin (`draw.c` `func_8001A050`) builds its own projection matrix; it goes through the same hook.

### HUD and 2D

- **HUD models.** The 3D letters, counters and icons are mobys drawn in screen space: `func_80022A2C`'s HUD path (render radius sign bit) sets `OFX` to the moby's screen x and loads a fixed diag(1, 0.625, 1) matrix. Two `before` hints in `tools/mips2c/hints.yaml` narrow both (`port_ws_hud_ofx`, `port_ws_hud_r11`, identity when off). HUD items keep their shape and move toward the centre by 3/4, so the HUD stays inside the 4:3 safe area.
- **C-built 2D primitives.** The pause and inventory boxes, the dialog lines, title sprites and the like go into the HUD OT through `func_800168DC` (`order_table.c`). Only C code calls it. In widescreen passes, `present.c` records them. Just before `DrawOTag`, it narrows their x about the centre, then restores the words afterwards, so RAM stays as the game wrote it. Two kinds are left alone:
  - full-width primitives (span ≥ 511: fades, letterbox bars, separator lines);
  - the grayscale screen copy behind the pause, inventory and flight-results menus (POLY_FT4 with the CLUT at VRAM (512,224)). It is already a widescreen frame.
- **C primitives in the world OT** (`func_800168A0`): the tracers are projected through the camera matrix already, and the 0x3FF grayscale strips are the screen copy. Neither is touched.

### Output

The capture is the drawn 512×240 area. The host scales it bilinearly to 960×544, full width for widescreen frames. 960×544 is 30:17, 0.7% wider than 16:9, which is close enough not to correct.

### Why a logic pass

The renderers write gameplay state:

- `r_moby` sets `Moby.m_WasDrawn` (`0x51`) only for mobys that pass its frustum test.
- `func_80051FEC` (`moby_lists.s`) uses that flag to decide which mobys update. Many enemies also check it before they attack.
- `r_environment` writes per-sector visibility (`D_800771C8`), which `func_800521C0` reads to clear `m_WasDrawn`.

A wider frustum therefore means more mobys updating, more `rand()` calls, and a different game. `--widescreen-logic` measures this (`logs/run5-wslogic`). In the Stone Hill attract demo, the rand seed diverges from the oracle at tick 123, the gem count at tick 418, and Spyro's position at tick 541: the demo desyncs. The faithful mode keeps the 4:3 culling for logic, so the demo plays exactly as on the PS1. The cost is one render path without rasterisation per tick (below).

A visible side effect of faithful mode: a moby that's only visible in the extra side strips, and that's beyond its update distance from Spyro, doesn't update. It may stand still where the 4:3 game would never have shown it.

## 60 fps

### What the render path reads

`GamestateDraw` (`draw.c:2651`) for gameplay is: `CameraUpdateMatrices`, then
- `func_800521C0` (sorts mobys into the render lists),
- the collectable HUD,
- `func_80019698`,
- `func_8002B9CC` (environment),
- `func_80050BD0` (sky),
- `func_800573C8` (particles),
- the fade and border,
- the tracers,
- the VSync wait,
- `DrawOTag`.

It reads:

| State | Where | Interpolated |
|---|---|---|
| Camera position `g_Camera+0x28`, rotation `+0x4C` | `CameraUpdateMatrices`; every renderer loads the position and the matrices | yes (both builds) |
| Spyro position, body/head/tail rotation `+0x0C/+0x10/+0x14` | `r_pete` | yes (`--interp`) |
| Moby position `+0x0C`, rotation `+0x44` | `r_moby` builds each matrix from the rotation bytes | yes (`--interp`) |
| Moby animation: animation, next animation, frame, next frame, progress (`+0x3C`–`+0x40`) | advanced by `func_800522C0` in the update (64 progress units per frame); `r_moby` blends frame to next frame by progress | yes (`--interp`): halfway along the track, across at most one frame step |
| Spyro's body, head and tail tracks (`+0x18`–`+0x26`) | advanced in `pete.c` (`func_8003CB24` and kin, 16 units per frame); read by `r_pete` | yes (`--interp`), as for mobys |
| Particle positions (`Particle.m_Data.m_Position`) | `particle_update.c` moves them; `r_particles` draws | yes (`--interp`): by slot, when class, render type and a forward-moving timer match |
| Line-particle end points, shadows, flame, HUD counters, texture animation | `r_particles`, `r_shadows`, `r_flame`, `draw.c`, `r_environment` | no (30 Hz). Shadows follow their moby's blended position. |

Saved per tick for blending (`PortPresentBeforeUpdate`, before `GamestateUpdate`): the camera position and rotation; Spyro's position, three rotations and three animation tracks; every moby's class, state, position, rotation and animation track (≤1024 slots); and the 8 KB particle space. The blend is halfway, measured from the current tick, so values stay in the game's own ranges. The 12-bit camera and 8-bit moby angles take the short way round. An animation track that started a new animation (anything but "same frames" or "one frame on") keeps this tick's pose. There is no blend when:

- the gamestate or level changed (pause entry, level load: the frame is skipped);
- the camera moved more than 0x2000 in one axis (a cut: the frame is skipped);
- a moby changed class or died, or jumped more than 0x2000 (that moby uses its current value).

### Keeping the game out of it

| Effect of re-running `GamestateDraw` | Handled by |
|---|---|
| Writes to RAM: render lists in `g_SonyImage`, `D_800757B0`, `g_CurDB` flip, `m_WasDrawn`, the sector cache, border and transition counters, `D_80075950` | region snapshot (below), restored after each pass |
| GTE registers (renderers keep data in control registers) | `gteRegs` saved and restored |
| PsyCross draw and display env (`PutDrawEnv`/`PutDispEnv`) | saved and restored |
| `$v1` across translated calls | `g_mips_v1` saved and restored |
| VSync waits (`while (VSync(-1) - post < 2) VSync(0)`, the demo-mode extra VSync) | `g_psyxVSyncHook`: inside a pass, time stands still and `VSync(-1)` answers far ahead, so no vblank passes and `g_DeltaTime` is untouched |
| Sound (`func_800522C0` plays animation sounds; title menus play sounds) | `libspu` drops writes while `g_psx_spu_frozen` is set (0 dropped in the runs below) |
| VRAM: the pass draws into the back buffer | the logic pass redraws the same buffer (or, under widescreen, the presentation frame stays there, which is what the pause screen copies) |

**Region snapshot.** The first version copied the whole 2 MB arena in and out. `--present-dirty` showed a pass writes 22–40 pages of 4 KB (`logs/run5-dirty`). The snapshot now saves and restores only what the game reads again:

| Region | Why | Size |
|---|---|---|
| Scratchpad (first KB of the arena) and `g_psx_scratch` | renderers keep matrices and lists there | 2 KB |
| Game data, sdata, sbss, bss, 0x8006BBE0–0x8007AA38 (`psx.map`) | camera, Spyro, globals, `g_SonyImage`, `D_80075950`, the `g_CurDB` flip | 60 KB |
| The level's moby array (`g_LevelMobys`, up to the 0xFF state) | `m_WasDrawn`, the blended transforms and animation | ~10–30 KB |
| Particle space (`g_Buffers.m_ParticleSpaceStart`, 8 KB) | the blended particles | 8 KB |
| Bytes +0x18–0x1B of every sector header (`g_Environment.m_SectorPointer`) | a cache `func_800258F0` reads (`lw $gp, 0x18($s1)`) and rewrites (`sb $sp, 0x18..0x1B($s1)`) | 4 B × sectors |

What a pass writes beyond that is scratch the next real draw rewrites before anything reads it: the two primitive buffers and both OTs (linking clears every bucket), and the emulated stack below the caller's `$sp`. `--present-dirty` keeps a full copy as well and, after the region restore, reports every page that still differs outside those areas, byte for byte. Result: **0** over both attract demos, pause/inventory and the Artisans gem script (`logs/run11-check-*`, `logs/run14-check`). Its first run found two things that shaped the list: the sector cache, and the primitive buffer starting mid-page. With the region snapshot only, all traces stay identical, and 110 widescreen+interp shots are byte-identical to the full-snapshot build (`logs/run12-*`).

### Results

- `logs/run3-interp`, `logs/run3-off960`, vblanks 4400–4431 (Stone Hill demo):
  - **30 fps:** 16 new pictures in 32 vblanks.
  - **60 fps:** 32 in 32.
  - **Real frames:** byte-identical to the 30 fps frame on the same vblank (all odd vblanks, by md5). So there's no added latency.
  - **In-between frames:** each is closer to each of its neighbours than they are to each other (mean absolute difference 6.6 and 7.1 vs 8.9). Crops around Spyro step once per vblank instead of every other one (`logs/run7-compare`).
- **Animation and particles** (`logs/run15-gems-*`, Artisans; `logs/run7-compare/anim-zoom-7010.png`):
  - With `--interp`, Spyro's wings and legs and Sparx take an intermediate pose in the in-between frame. With `--interp-no-anim`, the in-between frame shows the next tick's pose at a halfway position.
  - Over 31 in-between frames, a pixel-distance score (each in-between frame against the average of its neighbours) separates camera-only from object blending more than it separates the animation modes. The difference animation blending makes is a few limb pixels per frame.
  - Counts over the demos: 342,494 animation tracks blended, 95 kept as they were (a new animation started); 64,530 particle blends.
- Still at 30 Hz: the flame, line-particle end points and the HUD counters' spin.

## CPU cost

Measured with `--present-stats` on the linux32 host: CPU time (`clock()`) per 30 Hz tick, under qemu-i386 on the Mac, with no screenshots. These are the title, intro and the Stone Hill attract demo (`logs/run6-cost/*.log`, 5,600 vblanks, one mode at a time). "Logic draw" leaves out the time inside `VSync(0)`, where the host mixes audio.

Round 2 is the quieter run (`logs/run6-cost/round2`). Round 1 (`logs/run6-cost`) ran beside other sessions' containers and reads 5–30% higher, which is the noise level of these numbers. The vs-off ratios are the useful part.

These numbers predate the region snapshot (the Snapshot column is the full 2 MB copy); the passes themselves are unchanged.

| Mode | Update | Logic draw | Presentation passes | Snapshot | Total per tick | vs off |
|---|---|---|---|---|---|---|
| off | 0.35 ms | 14.8 ms (render + raster) | none | none | 15.2 ms | 1.00 |
| `--widescreen-logic` | 0.33 | 17.0 (wider view: more to draw) | none | none | 17.4 | 1.14 |
| `--widescreen` | 0.34 | 5.3 (no raster) | 17.6 (1 pass) | 0.42 | 23.6 | 1.56 |
| `--interp-camera` | 0.37 | 14.7 | 14.5 (1 pass) | 0.40 | 30.0 | 1.97 |
| `--interp` | 0.37 | 15.8 | 15.6 (1 pass) | 0.42 | 32.2 | 2.12 |
| `--widescreen --interp` | 0.36 | 5.6 (no raster) | 35.6 (2 passes) | 0.68 | 42.3 | 2.78 |

Blending itself is cheap: 6 µs per tick for the camera only, 15 µs with object transforms, 26 µs with animation frames and particles (`logs/run16-cost`). The region snapshot costs 34–130 µs per tick against 0.42–0.68 ms for the full copy, and moves 171 KB per tick (`--widescreen` or `--interp`) or 257 KB (both) instead of 4–6 MB (`logs/run13-cost`, `logs/run16-cost`; the machine load differed between rounds, so compare within a round). A pass costs about what the original draw costs. The logic pass without rasterisation shows the split: the render path R is ~5.5 ms here and the soft rasteriser ~9.5 ms.

How to read it for the Vita:

- **Cost units.** The draw-side cost is the render path R (GTE, culling, OT building; ~6 ms here) plus rasterisation (~9 ms here, in the soft renderer). The original game spends R + raster per tick.
  - `--widescreen`: R (logic pass, no raster) + R + raster.
  - `--interp`: R + raster, plus R + raster for the in-between frame.
  - Both: R + 2 × (R + raster).
  - With a GPU backend (vitaGL), rasterisation leaves the CPU, and the cost is roughly 2R or 3R per tick.
- **Snapshot.** Now ~170–260 KB per tick (region snapshot, above), down from 4–6 MB. On the Vita that's a fraction of a millisecond even at a few hundred MB/s.
- **Hardware.** First real-Vita numbers (GPU renderer, build `c40c9cf`, `docs/vita-testing.md`; features off). The CPU is 43–78% busy per 30 Hz tick, so 14–26 ms of the 33 ms. Per vblank in levels: game 3–5 ms, raster 3–4, SPU ~2.5, OT walk 1–1.5.
  - **Estimate (not measured):** an in-between frame costs one more render path, OT walk and GPU submission, about 12–17 ms per tick. That fits in light scenes and overruns heavy ones. Faithful widescreen adds one render path without raster, ~4–6 ms.
  - The region snapshot (<0.3 MB per tick) and the blending (~26 µs) are not the limit any more.
  - What would make 60 fps hold everywhere:
    - drop the in-between frame when the previous tick ran long (adaptive: the game then shows 30 fps for that tick, never slower);
    - a cheaper render path (readable C for the renderers);
    - cheaper submission in the GPU backend.

## Verification

| Check | Result | Logs |
|---|---|---|
| Features off: trace vs baseline build and oracle | identical; both attract demos match the oracle (Stone Hill 866 ticks, Doctor Shemp 768, 11 columns) | `logs/run1-baseline`, `logs/run4-off-stats` |
| Features off: 110 screenshots vs baseline build | byte-identical | `logs/run4-off-stats` vs `logs/run1-baseline` |
| `--widescreen`, `--interp`, `--interp-camera`, both: trace over 11,000 vblanks | identical to baseline (so the demos still match the oracle) | `logs/run5-*` |
| `--widescreen-logic` | diverges (rand at tick 123, Spyro at 541): why the default needs a logic pass | `logs/run5-wslogic` |
| SPU writes inside passes | 0 dropped (none attempted) | `logs/run5-*/spyro.log` |
| Artisans: new game, pause, inventory (`logs/pads/inventory.txt`, from M2's run6) | trace with `--widescreen --interp` identical to off | `logs/run8-inv-*` |
| Artisans: walking and four red gems (`logs/pads/gems.txt`, from M2's run9) | trace with `--widescreen --interp` identical to off; 4 gems at the end | `logs/run8-gems-*` |
| Real frames under `--interp` vs 30 fps | byte-identical on the same vblanks (4400–4431) | `logs/run3-interp`, `logs/run3-off960` |
| ctest | 4/4: cd, gte (9,314 vectors), m2c_diff_math, platform | |
| Final build (PR #5) | off: trace and 110 shots byte-identical to the baseline; `--widescreen --interp` and `--interp`: trace identical | `logs/run9-final-*` |
| Region snapshot | `--present-dirty`: 0 pages missed (demos, pause/inventory, gems); region-only runs: all traces identical, 110 wide+interp shots byte-identical to the full-snapshot build | `logs/run11-check-*`, `logs/run12-*`, `logs/run14-check` |
| Animation and particle blending | traces identical (demos, gems); real frames unchanged, only in-between frames differ | `logs/run14-*`, `logs/run15-gems-*` |
| Final build (region snapshot + animation) | off: trace and 110 shots byte-identical to the baseline; `--interp`, `--widescreen --interp`, pause/inventory and gems: traces identical | `logs/run17-final-*` |

Pictures (`logs/run7-compare`, made with `tools/side_by_side.py`; the left half is 4:3 at 960×544, the right half the same vblank in 16:9):
- `ws-vs-43-*.png`: intro (2000), level transition (3800), Stone Hill demo (4500), Doctor Shemp demo with HUD (9000–10000).
- `inv-*.png`: pause menu and inventory over the grayscale screen copy.
- `gems-*.png`: Artisans with the gem counter.
- `seq-30fps-vs-60fps-4404.png`: eight vblanks cropped around Spyro, 30 fps on top, `--interp` below.
- `seq-ws60-9000.png`: eight vblanks of `--widescreen --interp` in Doctor Shemp.

## What changed

| Where | Change | Off |
|---|---|---|
| `patches/spyro-1-port/0005-presentation-hooks.patch` | `main.c`: `PortPresentBeforeUpdate()` before `GamestateUpdate`, `PortPresentDraw()` instead of `GamestateDraw()`. `camera.c` `CameraUpdateMatrices` and `draw.c` `func_8001A050`: `PortProjectionHook` on the projection matrix. | `PortPresentDraw` calls `GamestateDraw`; the hook returns at once |
| `patches/PsyCross/0005-presentation-hooks.patch` | `g_psyxVSyncHook` (LIBETC `VSync`); `g_psyxDrawOTagPre/Post` (LIBGPU `DrawOTag`) | NULL |
| `tools/mips2c/hints.yaml`, `mips2c.py` | `before` hints: `func_80022A2C` HUD path at `0x80022D2C` (OFX) and `0x80022D78` (R11) | `PORT_WS_HUD_*` is the identity |
| `src/game/order_table.c` `func_800168DC` | records HUD-OT primitives for the 2D squeeze | records nothing |
| `src/platform/common/libspu.c` | drops writes while `g_psx_spu_frozen` | never set |
| `src/game/present.c/.h` | everything else | |
| `src/platform/linux32` | flags, 960×544 output (`png.c`), presenter per vblank (`vblank.c`) | `--out960` off: old output path |

## On the Vita

Wired in on 2026-10-30, **on by default** on the Vita (the linux32 host keeps both off, so its oracle checks stay exact). `config.txt` keys: `widescreen=0/1`, `interp=0/1` (`2` without animation, `3` camera only) and `interp_budget=MS` (default 26). The user's guide is `docs/vita-testing.md`. Scripted Vita3K runs (`tools/vita/vita3k_run.sh`) write `widescreen=0` and `interp=0` first, so they still compare with the host unless a run turns them on.

**Options menu** (`src/platform/vita/menu.c`, panel drawn by `src/platform/common/menu_panel.c`): hold SELECT and press START.
- **Pausing:** the menu runs inside the game's VSync wait (`vblank.c`), so the game is paused. Afterwards the real-time count starts again, with no catch-up, so `g_DeltaTime` doesn't jump.
- **Buttons:** `pad.c` hides the opening combo, and whatever closed the menu, from the game until they're released.
- **Changes:**
  - they apply at once (`vita_present_apply`; `present.c` takes them at its next tick, drops its queue when switched off, and starts blending afresh when switched on);
  - widescreen re-scales the GPU targets (`vita_gpu_rescale`);
  - closing writes the three keys back to `config.txt` and keeps its other lines.
- **Drawing:** the panel is drawn on the CPU, uploaded once, and drawn 1:1 over the last picture by either renderer.
- **Tests:** `tests/menu_panel` renders the panel on the host; `menu_test=V` drives the menu in scripted runs.

- **gpu=1 (vitaGL):** a presentation pass draws into the renderer's targets as usual. Then `present.c`'s capture interface (`g_present_capture`) has `gpu_gl.c` copy those targets on the GPU into one of 7 textures (6 slots plus staging; filing a frame swaps handles). There's no readback and no replay, and the present draws the slot texture full width or 4:3. With widescreen on, the targets render 960 wide (`set_scale`), so frames are captured at 960×544.
- **gpu=0 (software rasteriser):** the capture is `StoreImage`, as on the host. `video.c` uploads the 512×240 frame and SDL scales it.
- **Pacing:** the presenter takes one frame per picture shown (`vita_video_present`). Real frames land where the original's would.
- **Budget:** `interp_budget` uses the platform clock (`sceKernelGetProcessTimeWide`). Before a tick's in-between pass, it checks that the last tick's game CPU time without the in-between frame, plus that frame's last cost, fits. VSync waits (SPU mix, present, display wait) don't count. If it doesn't fit, the tick shows its real frame twice instead of running late. Running late would make the game's own `g_DeltaTime` jump (D28).

Checked in Vita3K (not hardware):

| Run | Result |
|---|---|
| `gpu=0 widescreen=1 interp=1 interp_budget=0`, Stone Hill demo, vblanks 4396–4419 (`logs/run21-vita3k-soft-nobudget`) | every Vita frame, real and in-between, byte-identical to the host's (`--present-native`, `logs/run19-host-native`), one vblank apart: the Vita's shot is taken before that VSync's present |
| the same with the default budget (`logs/run20-vita3k-soft`) | real frames identical to the host's; the in-between frames were all skipped, because Vita3K runs the CPU path far slower than a Vita |
| `gpu=1 widescreen=1 interp=1` (`logs/run22-vita3k-gpu`, `run23-vita3k-gpu-wide`) | runs to vblank 9,600: 300 presents per 300 vblanks, an in-between frame in every tick (150/150), no VRAM uploads or readbacks added, no GL errors, frames 960×544 |

**Not yet seen:** the GPU-path picture itself. Vita3K returns zeros for anything read back from the GPU, and `shot_hd=1` only works on hardware. So orientation, edges and the HUD under `gpu=1` wait for the hardware test in `docs/vita-testing.md`. The software path's frames are the host's, byte for byte.

## Follow-ups

1. Hardware test of the Vita build (`docs/vita-testing.md`): picture under `gpu=1`, the in-between frame rate per scene, and a tuned `interp_budget` default.
3. Flame, line-particle end points and the HUD counters' spin at 60 fps.
4. Optional: anchor HUD items to the 16:9 edges instead of the 4:3 safe area.
5. If a tick takes 3–4 vblanks (`g_DeltaTime` 3–4), blend at ⅓ and ⅔ instead of ½.

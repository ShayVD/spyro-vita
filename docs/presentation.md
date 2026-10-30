# Presentation: 16:9 widescreen and 60 fps over 30 Hz logic

Prototype on the linux32 host (2026-10-30). Both features are optional, and off is the default. With both off, nothing in this document runs: the attract demos match the oracle tick for tick, and every screenshot is byte-identical to a build without this code. With them on, the logic trace is still identical; only the picture changes. Code: `src/game/present.c`, plus hooks listed under [What changed](#what-changed). Decision: D30 in `docs/decisions.md`.

## Switches (linux32 host)

| Flag | Effect |
|---|---|
| `--widescreen` | 16:9 picture. The logic still sees the original 4:3 frustum (faithful). |
| `--widescreen-logic` | 16:9 in one pass: cheaper, but the wider frustum reaches gameplay (see [Why a logic pass](#why-a-logic-pass)). Diagnostic only. |
| `--interp` | 60 fps: one extra frame between ticks, with the camera, Spyro and mobys blended. |
| `--interp-camera` | 60 fps, camera only. |
| `--out960` | Pictures (shots, `--serve`) at 960×544: full width for widescreen frames, 4:3 pillarboxed otherwise. Turned on by any of the above. |
| `--present-stats` | Times update, draw and passes; logs a summary at exit. `--present-dirty` also maps the arena pages each pass writes. |

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
- `func_800521C0` (moby render queue and animation frame advance),
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
| Spyro position, body rotation | `r_pete` | yes (`--interp`) |
| Moby position `+0x0C`, rotation `+0x44` | `r_moby` builds each matrix from the rotation bytes | yes (`--interp`) |
| Animation frame, next frame, progress (`+0x3C`) | `func_800521C0`, `r_moby` keyframe blend | no: they advance inside the draw (see below) |
| Particles, shadows, flame, HUD counters, texture animation | `r_particles`, `r_shadows`, `r_flame`, `draw.c`, `r_environment` | no (30 Hz) |

Snapshotted per tick for blending (`PortPresentBeforeUpdate`, before `GamestateUpdate`): the camera position and rotation, Spyro's position and body rotation, and every moby's class, state, position and rotation (≤1024 slots, ~24 KB). The blend is halfway, measured from the current tick, so values stay in the game's own ranges. The 12-bit camera and 8-bit moby angles take the short way round. There is no blend when:

- the gamestate or level changed (pause entry, level load: the frame is skipped);
- the camera moved more than 0x2000 in one axis (a cut: the frame is skipped);
- a moby changed class or died, or jumped more than 0x2000 (that moby uses its current value).

### Keeping the game out of it

| Effect of re-running `GamestateDraw` | Handled by |
|---|---|
| Writes to RAM: render lists in `g_SonyImage`, `D_800757B0`, `g_CurDB` flip, `m_WasDrawn`, animation advance, border and transition counters, `D_80075950` | full snapshot of the 2 MB arena (scratchpad included) plus `g_psx_scratch`, restored after each pass |
| GTE registers (renderers keep data in control registers) | `gteRegs` saved and restored |
| PsyCross draw and display env (`PutDrawEnv`/`PutDispEnv`) | saved and restored |
| `$v1` across translated calls | `g_mips_v1` saved and restored |
| VSync waits (`while (VSync(-1) - post < 2) VSync(0)`, the demo-mode extra VSync) | `g_psyxVSyncHook`: inside a pass, time stands still and `VSync(-1)` answers far ahead, so no vblank passes and `g_DeltaTime` is untouched |
| Sound (`func_800522C0` plays animation sounds; title menus play sounds) | `libspu` drops writes while `g_psx_spu_frozen` is set (0 dropped in the runs below) |
| VRAM: the pass draws into the back buffer | the logic pass redraws the same buffer (or, under widescreen, the presentation frame stays there, which is what the pause screen copies) |

`--present-dirty` (`logs/run5-dirty`) maps what a pass really changes. Over 11,334 passes: 22 pages of 4 KB on average, 40 at most. They are the scratchpad page, the emulated stack, `g_SonyImage`, the game globals at 0x80075000–0x80078FFF, two level-data pages (the moby array), and the primitive buffers and OTs at the top of RAM. So a release build can snapshot ~100–160 KB instead of 2 MB (below).

### Results

- `logs/run3-interp`, `logs/run3-off960`, vblanks 4400–4431 (Stone Hill demo):
  - **30 fps:** 16 new pictures in 32 vblanks.
  - **60 fps:** 32 in 32.
  - **Real frames:** byte-identical to the 30 fps frame on the same vblank (all odd vblanks, by md5). So there's no added latency.
  - **In-between frames:** each is closer to each of its neighbours than they are to each other (mean absolute difference 6.6 and 7.1 vs 8.9). Crops around Spyro step once per vblank instead of every other one (`logs/run7-compare`).
- Not interpolated yet (visible as 30 Hz motion inside a 60 fps picture): animation keyframes, particles, the flame and the HUD counters. Animations advance in `func_800521C0`, so blending them needs that function's frame-progress logic run with a half step; that's the next piece of readable C to write.

## CPU cost

Measured with `--present-stats` on the linux32 host: CPU time (`clock()`) per 30 Hz tick, under qemu-i386 on the Mac, with no screenshots. These are the title, intro and the Stone Hill attract demo (`logs/run6-cost/*.log`, 5,600 vblanks, one mode at a time). "Logic draw" leaves out the time inside `VSync(0)`, where the host mixes audio.

Round 2 is the quieter run (`logs/run6-cost/round2`). Round 1 (`logs/run6-cost`) ran beside other sessions' containers and reads 5–30% higher, which is the noise level of these numbers. The vs-off ratios are the useful part.

| Mode | Update | Logic draw | Presentation passes | Snapshot | Total per tick | vs off |
|---|---|---|---|---|---|---|
| off | 0.35 ms | 14.8 ms (render + raster) | none | none | 15.2 ms | 1.00 |
| `--widescreen-logic` | 0.33 | 17.0 (wider view: more to draw) | none | none | 17.4 | 1.14 |
| `--widescreen` | 0.34 | 5.3 (no raster) | 17.6 (1 pass) | 0.42 | 23.6 | 1.56 |
| `--interp-camera` | 0.37 | 14.7 | 14.5 (1 pass) | 0.40 | 30.0 | 1.97 |
| `--interp` | 0.37 | 15.8 | 15.6 (1 pass) | 0.42 | 32.2 | 2.12 |
| `--widescreen --interp` | 0.36 | 5.6 (no raster) | 35.6 (2 passes) | 0.68 | 42.3 | 2.78 |

Blending itself is cheap: camera-only and full interpolation differ by the moby loop (~1024 slots) and run-to-run noise. A pass costs about what the original draw costs. The logic pass without rasterisation shows the split: the render path R is ~5.5 ms here and the soft rasteriser ~9.5 ms.

How to read it for the Vita:

- **Cost units.** The draw-side cost is the render path R (GTE, culling, OT building; ~6 ms here) plus rasterisation (~9 ms here, in the soft renderer). The original game spends R + raster per tick.
  - `--widescreen`: R (logic pass, no raster) + R + raster.
  - `--interp`: R + raster, plus R + raster for the in-between frame.
  - Both: R + 2 × (R + raster).
  - With a GPU backend (vitaGL), rasterisation leaves the CPU, and the cost is roughly 2R or 3R per tick.
- **Snapshot.** 2 MB copied in and out per pass (0.5–0.8 ms here; the Vita's memcpy is much slower than the Mac's). Next step: copy only the regions the dirty map found (game globals, `g_SonyImage`, the moby array, stack and scratchpad, ~100 KB), and treat the primitive buffers and OTs as scratch that the next real draw overwrites. Keep `--present-dirty` as the check that nothing outside the list changes.
- **Hardware.** 60 fps needs two renders per 33 ms tick, so R must stay well under ~12 ms on the Vita's A9 at 444 MHz. M3 runs at ~60 vblanks/s in Vita3K, which says nothing about hardware. The first hardware measurement (M3/M4) decides whether the in-between frame is affordable in all levels, or should drop to camera-only (`--interp-camera`, same cost, less state) or turn itself off when a tick runs long.

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
| Final build | off: trace and 110 shots byte-identical to the baseline; `--widescreen --interp` and `--interp`: trace identical | `logs/run9-final-*` |

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

## For the Vita (M3/M4)

- The Vita target builds with this code (VPK checked after merging M3); nothing there calls the presenter yet, so its picture is unchanged.
- The platform calls `port_present_frame()` once per vblank. When it returns a frame, show that instead of the PS1 display rect, 16:9 if `port_present_frame_is_wide()`. Set `g_present` from a menu or config file.
- With the CPU rasteriser (`gpu_soft.c`), the capture is a `StoreImage` of the drawn area, as on the host. With a vitaGL backend, a presentation pass should render into its own target instead (no readback), and 960×544 can be the render resolution. Integer PS1 coordinates then show as wobble at high resolution; that's the existing "sub-pixel later" item.
- `present.c` reads `g_LevelMobys` and `g_Camera` as C pointers into the arena, as `trace.c` does. Check that this holds with the Vita arena (D25) on the Vita.
- Keep the presenter's real-frame-first ordering. If a tick takes more than 2 vblanks (`g_DeltaTime` 3–4), the in-between frame is still at ½, and the last frame repeats. Blending at ⅓ and ⅔ would need the tick length ahead of time.

## Follow-ups

1. Region snapshot instead of 2 MB (above), checked by `--present-dirty`.
2. Interpolate animation keyframes (half-step frame progress through `func_800521C0`), particles and the flame.
3. Optional: anchor HUD items to the 16:9 edges instead of the 4:3 safe area.
4. Wire the presenter into the Vita platform (owned by the M3 session) and measure on hardware.

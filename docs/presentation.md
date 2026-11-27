# Presentation: 16:9 widescreen and 60 fps over 30 Hz logic

Prototype on the linux32 host (2026-10-30). Both features are optional, and off is the default. With both off, nothing in this document runs: the attract demos match the oracle tick for tick, and every screenshot is byte-identical to a build without this code. With them on, the logic trace is still identical; only the picture changes. Code: `src/game/present.c`, plus hooks listed under [What changed](#what-changed). Decision: D30 in `docs/decisions.md`.

## Switches (linux32 host)

| Flag | Effect |
|---|---|
| `--widescreen` | 16:9 picture, drawn once: the wider view is also what the logic sees (mobys visible at the 16:9 edges count as on screen). Attract demos keep the original 4:3 logic, a hidden 4:3 draw plus the presenter's 16:9 frame (D30's 16:9 pass until D129), so they replay exactly. See [One draw or two](#one-draw-or-two). |
| `--widescreen-exact` | the two-draw form always: the logic identical to 4:3 in play too, at the cost of one more render path per tick. |
| `--widescreen-logic` | one draw always, attract demos too. Diagnostic only: the demos desync. |
| `--interp` | 60 fps: one extra frame between ticks, with the camera, Spyro, mobys, their animation frames and the particles blended. |
| `--interp-no-anim` | 60 fps, transforms only (no animation frames or particles); for comparisons. |
| `--interp-camera` | 60 fps, camera only. |
| `--out960` | Pictures (shots, `--serve`) at 960×544: full width for widescreen frames, 4:3 pillarboxed otherwise. Turned on by any of the above. |
| `--tick60` | 60 Hz logic in play, the 60 fps option's third value (D130, `docs/tick60.md`): no in-between frame after a 60 Hz update; the 30 Hz gamestates keep it with `--interp`. |
| `--present-stats` | Times update, draw, the presenter's frames and blending; logs a summary at exit. (`--present-dirty`, which checked D30's region snapshot against a full copy of the arena, went with the pass: D129.) |

`PresentConfig g_present` (`src/game/present.h`) holds the same switches for other platforms.

## The idea in one paragraph

**Since stage 7g (D128, D129)** the frames the logic draw doesn't draw (the in-between frame, and the attract demos' 16:9 frame) are the **presenter's**: the game's own draw code compiled a second time over a snapshot of what it reads (`src/game/pres/`), in every gamestate, blended for the in-between frame, on a thread of its own on the Vita (D126). It writes none of the game's memory, so nothing needs putting back. D30's presentation pass, described in the rest of this paragraph and in "Keeping the game out of it" below, is how it was done from D30 until D129; its snapshot, its freezes of the SPU and VRAM calls and its frozen `VSync` are gone (the presenter build drops those calls by name, `pres_names.h`). The picture below still holds with "presenter" for "pass", and no restore.

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

Since stage 7c (D112) each frame carries the vblank it's due at: a frame filed at vblank count c behind k waiting ones is due at c + 1 + k. The host takes the frame due at each vblank (`port_present_frame_due`), the same frame the queue gave. On the Vita with `present_due=1` and real time, the render thread shows each frame when the display reaches its due vblank, without the game thread presenting it.

## Widescreen

### Projection: the camera matrix, not the GTE

(The environment renderer's sector tests use the view matrix instead; they're narrowed separately, see [Fixes from the first hardware test](#fixes-from-the-first-hardware-test-2026-10-30).)

D2 planned to scale X in RTPS/RTPT. The prototype found a better place: the projection matrix.

- `CameraUpdateMatrices` (`camera.c`) builds `g_Camera.m_ProjectionMatrix` from the view matrix, scaling the Y row by 320/512 for the 4:3 TV. Widescreen scales the X row by 17/21 the same way (`PortProjectionHook`; 3/4 before D67). That squeezes the wide view into the 512 PS1 columns, which the display then stretches to the screen's 960×544 (see [Output](#output)).
- Projecting in that space also widens every frustum test done in it. `r_moby` culls mobys with `MVMVA` against the projection matrix and tests `4|X| < 3Z` (`r_moby.s` around `0x8001F2E0`, and `func_80022A2C`). With the X row at 17/21, the same test accepts `|X| < 63Z/68`, the frustum of the 960×544 picture (at 3/4 it was `|X| < Z`, 16:9 over 240 lines). Screen-space culls (after RTPS) need nothing: everything visible still lands in 0–511.
- A GTE-level squeeze would also hit gameplay. RTPS projects Spyro for the camera logic (`func_80017AA4`, called by `camera.c:433`, `func_80034480`, `func_800357A4`; `moby_update.c:25866`) through the same matrix, and the culls would still need widening one by one.
- The level-transition spin (`draw.c` `func_8001A050`) builds its own projection matrix; it goes through the same hook.

### HUD and 2D

- **HUD models.** The 3D letters, counters and icons are mobys drawn in screen space: `func_80022A2C`'s HUD path (render radius sign bit) sets `OFX` to the moby's screen x and loads a fixed diag(1, 0.625, 1) matrix. Two `before` hints in `tools/mips2c/hints.yaml` narrow both (`port_ws_hud_ofx`, `port_ws_hud_r11`, identity when off). HUD items keep their shape and move toward the centre by 17/21, so the HUD stays inside the 4:3 safe area.
- **C-built 2D primitives.** The pause and inventory boxes, the dialog lines, title sprites and the like go into the HUD OT through `func_800168DC` (`order_table.c`). Only C code calls it. In widescreen passes, `present.c` records them. At `DrawOTag` it copies each one, narrows the copy's x about the centre, and hands the copies to the OT walk, which parses a copy in place of its primitive (`PsxGpuSubst`, `gpu_walk.c`; D122). The game's primitives are never edited. (Before D122 they were narrowed in place and the words restored after `DrawOTag`.) Two kinds are left alone:
  - full-width primitives (span ≥ 511: fades, letterbox bars, separator lines);
  - the grayscale screen copy behind the pause, inventory and flight-results menus (POLY_FT4 with the CLUT at VRAM (512,224)). It is already a widescreen frame.
- **C primitives in the world OT** (`func_800168A0`): the tracers are projected through the camera matrix already, and the 0x3FF grayscale strips are the screen copy. Neither is touched.

### Primitive space (D54)

A 16:9 draw writes more primitives than the original 4:3 one: more sectors and mobys pass the wider frustum. The game's primitive buffers are fixed (`AllocateBuffers`: lower, upper, then the HUD OT and the world OT), 0x1C000 bytes each, or 0x13000 in reduced-space levels. Only the sky and particle renderers stop at a limit (`D_80075780 - 0x400`, buffer + 0x1BC00). The world and moby renderers check none. The original 4:3 view fits, but only just: in ending 3 (level 64, reduced space) it reaches 73 KB of 76 (`logs/run6-state-43` in the fix's worktree). With one-draw widescreen the upper buffer overran by 124 bytes into the HUD OT and the first world-OT buckets, and linking the OT crashed (`func_80016784`, core in `logs/run1-ws`).

The port's `DrawOTag` uses up the primitives before it returns (D51), so neither buffer holds anything once a draw is done. With any presentation option or draw distance on, `PortPolySpace()` (from `GamestateDraw`, `patches/spyro-1-port/0010`) starts every draw at the lower buffer and lets it span both. The HUD's mobys and the sky and particle limit go to the top, 0x600 below the HUD OT. That gives 0x37A00 bytes, or 0x25A00 in a reduced-space level, about twice the original's room. The game's own limits move with it. The pause and exit-level screens already draw from the lower buffer (`func_8001A40C`, `func_8001C694`). The 0x600 bytes under the HUD OT are the save fairy's card buffer: `func_800314B4` reads the save's header there and builds the save there. The original's fairy draw keeps them free by moving the HUD mobys down to +0x1BA00 (`func_8001D718`), but only when the top is the HUD OT itself, so the port keeps them free in every draw (D61). D54's first top, 0x200 below the HUD OT, let the fairy's text land on the header it had just read, and saving said NO SAVE FILE. `lod.c` measures from the same start. Until #203 its bounds and limiter kept one buffer's size (D49); since then they take this span as the room ([Draw distance](#draw-distance-d49-off-by-default), "Safety"). With everything off, nothing changes.

### Output

The capture is the 512×240 display area. Spyro draws only 224 of its lines (`initialization.c`: draw areas 512×224, 8 lines into each 240-line display); the other 16 are black, in a TV's overscan. The screen shows those 224 lines over its full 544 (D67): the host scales them bilinearly to 960×544, full width for widescreen frames, 777 wide for 4:3; the Vita does the same (778 wide). The X row's 17/21 is what keeps that picture unstretched: a 4:3 TV gives a PS1 pixel 0.625 of its height in width, 960×544 from 512×224 gives 1.875/2.4286 = 0.772, and 0.625/0.772 = 17/21. Before D67 the whole 240 lines were shown (3/4: 16:9 over 240 lines, 960×544 being 0.7% wider), and the 16 black lines were 18-pixel bars at the top and bottom.

### One draw or two

**Revised 2026-10-30 after the first hardware test (D30).** By default, widescreen draws the scene once, in 16:9, and that draw is also the game's logic draw. Only attract demos (`g_DemoMode`), or `--widescreen-exact` / `widescreen_exact=1`, keep the two-draw form described below. On the Vita, the hidden 4:3 draw cost 4–8 ms per tick and pushed busy scenes to 20–26 fps (`logs/hw-3b4126a-user`, in the main checkout). The user chose one draw: enemies visible only in the side strips then act as on-screen ones do. In the Artisans gem script that changes the random seed from tick 19 and four rows of gem-count timing; Spyro's path, health and the gems collected are the same (`logs/run32-gems-wsi`). The attract demos still match the oracle tick for tick (`logs/run32-wsi`).

Why the two-draw form exists at all (it's what keeps the demos replaying):

The renderers write gameplay state:

- `r_moby` sets `Moby.m_WasDrawn` (`0x51`) only for mobys that pass its frustum test.
- `func_80051FEC` (`moby_lists.s`) uses that flag to decide which mobys update. Many enemies also check it before they attack.
- `r_environment` writes per-sector visibility (`D_800771C8`), which `func_800521C0` reads to clear `m_WasDrawn`.

A wider frustum therefore means more mobys updating, more `rand()` calls, and a different game. `--widescreen-logic` measures this (`logs/run5-wslogic`). In the Stone Hill attract demo, the rand seed diverges from the oracle at tick 123, the gem count at tick 418, and Spyro's position at tick 541: the demo desyncs. The faithful mode keeps the 4:3 culling for logic, so the demo plays exactly as on the PS1. The cost is one render path without rasterisation per tick (below).

A visible side effect of faithful mode: a moby that's only visible in the extra side strips, and that's beyond its update distance from Spyro, doesn't update. It may stand still where the 4:3 game would never have shown it.

## 60 fps

**Stage 7e (D125): the presenter.** With `interp_mode=2` (host `--interp-mode 2`) the in-between frame in play isn't drawn by the pass below over the game's own memory, but by the presenter: `draw.c` and the files its play branch calls, compiled a second time with `-include src/game/pres/pres_names.h` onto a snapshot (`src/game/pres/`: the labels the draw reads, each at its own offset in a mirror of the data region; the moby array and the particle space copied, pointers into them moved onto the copies; its own primitive buffers and OTs), blended as below. Its frame is the pass's, record for record (`--pres-check`), so every shot is the same. The other gamestates keep the pass (7g).

**Stage 7f (D126): the presenter on its own core.** The presenter's output calls (`DrawOTag`, `PutDrawEnv`, `PutDispEnv`, `DrawSync`, the 2D notes for the 16:9 narrowing) are its own (`present.c`'s `pres_*`): libgpu's environments' copies, a second OT walk (`PsxGpuWalk` with `own`, its own records' pool) and its own substitutions. So it shares nothing with the logic draw but read-only data. The game thread takes the snapshot after update N and starts the job; on the Vita (`pres_thread=1`, the default with `interp_mode=2`, itself the Vita's default since 7F1–7F5: the owner's decision, D126) the job runs on a thread of its own (`pres_thread.c`, core 1, or 3 with `pres_core=3`) beside the logic draw, recording into one of three buffers in the render thread's command format (`gpu_thread.c`). The game thread puts a `C_BUF` in the command ring before the logic draw's commands, so the render thread draws the in-between frame first, as before; one not closed by its due vblank (with `present_due=1`) is passed by and never shown. The game thread joins the job before update N+1 (`port_present_pres_join`): the update changes the level data the presenter reads. In play `interp_budget` no longer applies while the presenter has its thread. The host runs the job inline at the same point, through the GR_* calls. With the vertex cache on, the walk says where each polygon's answers go (D152; before it both walks drew with stale answers), and the presenter's frame records its projections into a table of its own and its polygons are looked up there (D164, #158), so its in-between frames get their sub-pixel positions and depths as the logic draw's frames do. Since D167 (#162) that holds on its thread too, so on the Vita the wobble fix (`persp`, `subpixel`) no longer keeps the presenter on the game thread: after its first frame (drawn inline, which makes its table and its answers' array) the game thread hands the frame over as it places the buffer, a polygon outside the game's walk's buffer is the presenter's, and its answers go to that array, at the index its walk gives, which the worker copies into its buffer beside the vertices. With `tex_filter` alone (polygons only marked) it stays inline, and `pres_thread=0` puts it back on the game thread.

**Stage 7g, part 1 (D128): every gamestate.** The presenter build now holds every gamestate's draw: the rest of `draw.c`, the overlays' draws (the title's `TitlescreenDraw`; each flight level's results, `Flight5` and `Flight4`; each balloon level's talk, `Balloonist4`, which the game calls through the pointers the level sets: `pres_ovl.c` points those calls at the presenter's copies), `credits_hand.c`'s draw, `spu.c` for the sounds the flight results play (on the snapshot's copies, the SPU writes dropped) and `completion.c`. The snapshot copies the whole data region (the game reads past a table's end into the next label: the title's border animation, the rescued dragon's name), rebases `g_Buffers`' buffer pointers and the dragon rescue's mobys, and refuses to run if a named label isn't in that region. With `interp_mode=2` the presenter draws the in-between frame in every gamestate and the attract demos' 16:9 frame (inline: it starts from the visibility state the in-between frame leaves); on the Vita the in-between frame goes to its thread in every gamestate but the credits' first stage, which files no frame. `--pres-check` compares each of them with D30's pass, record for record, and logs the counts by gamestate. D30's pass stays the reference and what `interp_mode` 0 draws.

**Stage 7g, part 2 (D129): D30's pass retired.** No gamestate needs the pass after D128, so it went: `presentation_pass`, its region snapshot and restore, `pass_vsync`, libspu's and libgpu's freezes, the live-state blend, and the debug switches that measured it (`--present-dirty`, `--present-audit`, `--present-reads`). `interp_mode` went too: the presenter always draws, and its area is always allocated. Without a presenter (the moved arena; `fast_render` 0) there is no in-between frame and the attract demos are 4:3. `--pres-check` now compares the presenter's frame from the same snapshot unblended with the logic draw's own list, every tick.

**The attract demos' 16:9 frames on the Vita (D211, #227).** In an attract demo at 16:9 the presenter draws two frames a tick: the in-between frame and the 16:9 real frame (the logic draw is the hidden 4:3 one). The presenter has one snapshot, so the real frame can start only once the in-between frame is done (it also starts from the visibility state that frame leaves). Since D211 the real frame runs on the presenter's worker beside the hidden logic draw, and the game thread files its capture after that draw, at the vblank the inline frame had. Until #227 the in-between frame ran on the worker too, and the game thread waited for it at the start of the real frame's pass: on hardware that wait was most of the 11.8–12.0 ms a vblank the `time` line shows (D219). Since #227 the in-between frame runs inline on the game thread (`present.c` `g_present_demo_inline`, 1 by default; 0 puts D211's order back: the Vita's config key `pres_demo_inline=0/1`, #232, shown on the `threads:` line and in the `pres:` start-up line), and only the real frame goes to the worker. Both frames still run one after the other, so a tick still costs the update plus two presenter frames (the logic draw is hidden under the real frame). On hardware a demo frame takes about 20–24 ms, which is more than the 33 ms tick allows for two, whichever thread draws them. In Vita3K's `demo60` (demo windows 4,200–5,700) the game thread's waits on the presenter fall from 11.3–13.9 ms a tick to 0.01–0.04 ms. Its wall time per 300 vblanks is within the noise: 5,809–6,247 ms against 5,819–6,581.

**The attract demos draw at 1× whatever the draw distance (#234).** The demos' presenter frames take their scale from the game's copy of `lod.c`: `pres_draw.c` `lod_begin` runs the game's `port_lod_draw_begin(1)` on the game's state, keeps what it leaves, and puts the game's state back. In a demo the game's own draws are the hidden 4:3 logic draws (`shown` 0), so the game's limiter never runs there, and the presenter's (`lod_pres.c`) result is never handed back. The last shown draw before the demo (entering Stone Hill, gamestate 0) resets the scale to 1× with a hold of 60 draws, and nothing counts that hold down again. So every demo frame draws at 1×: a temporary line in `port_lod_draw_begin` read 1× with the hold at 60 in every window of 150 begins from 4,080 to 5,580, on the host and in Vita3K `demo60` (`runs/auto-234-1`). The hardware's 20–28 ms a demo frame (`logs/hw-ap18t`, 4,500–6,000: the worker's busy time over its jobs) is 16:9 at 1×, so a draw-distance budget for the demos has nothing to cut. A temporary build that forced the full scale in the demos (measurement only) shows what 2× would cost there: in Vita3K `demo60` (4,500–5,700, two runs each) GL draws +34%, triangles +15%, the presenter's projections a frame +18% (4,398 to 5,185), and the in-between frame 7.9 ms a tick in both 1× runs against 8.3 and 10.0 ms at 2× (+20–45% in 5,100–5,700, where 2× reaches further). On the host (title and demos to 6,600, `--widescreen --interp --draw-distance 2 --vcache`, the two builds side by side) the presenter's time a tick goes from 30,064 to 32,331 µs over the whole run, about 4.3 ms more for each of the demos' roughly 1,730 frames (about +18%), with identical traces. What would fit a demo tick on hardware is one presenter frame, not two: skipping the in-between frame when the last two frames and the update don't fit gives about 1 + max(10–13, 20–28) ≈ 21–29 ms a tick, under the 33.4 ms tick, so about 150 ticks per 300 (the original speed) at 30 fps, against 85–124 ticks per 300 now. That changes what the demos show, so it waits for the owner. The larger share of a demo frame is the wobble fix's vertex cache, not the draw distance: in Vita3K `demo60` with `persp=0 subpixel=0` (two runs) the in-between frame takes 2.5–2.6 ms a tick against 7.9, the worker's real frame 2.0–2.1 ms against 7.3–7.4, and the hidden logic draw 1.2–1.3 ms against 4.8–4.9. Vita3K's timing is relative only; a hardware run of the demo windows with and without the cache would size it on the A9.

**The particle space's end word (D136).** The snapshot copies the 0x2000-byte particle space and, since D136, the word after it: the `-1` that `loaders.c` writes as a 257th slot (`*(int *)(&g_Particles[256]) = -1`). When all 256 slots are in use there is no `-1` inside the space (Alloc takes a slot over, `particle_alloc_hand.c`), and the game's walk (`func_800573C8`) stops at that word. 7e's copy stopped at 0x2000, so the presenter's walk went on through the area's zeros into whatever lay past them, read it as a type 13 particle and loaded a NULL texture-table entry (`particles_hand.c`): the Vita's data abort on the `spyro_pres` thread in Gnasty Gnorc's fight, with 60 fps on (`logs/t60-crash-20261110`). The region stays 0x2000 bytes, so rebasing and the `+0x18` moby fields are unchanged; `--render-ctx-check`'s mirror copies the word too. `--pres-particles-full` (host, tests) makes every free slot of the copy a hole, as with all 256 in use.

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

*(D30's pass, until D129: the presenter writes none of the game's memory, `--pres-write-check`, D128.)*

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

### Fixes from the first hardware test (2026-10-30)

| Seen on the Vita | Cause | Fix |
|---|---|---|
| Every texture red and green, after the pause menu | The pause screen saves the texture pages at VRAM x=512–767 into RAM and overwrites them with a grayscale copy of the frame (`draw.c` `func_8001A40C`); unpausing loads them back. The 16:9 presentation pass ran that pause-entry code too. The snapshot restores RAM but not VRAM, so the real draw then saved the grayscale picture as the "textures". Reproduced on the host: 57,344 of 57,600 texture words differ after pause and unpause (`logs/run31-pause-ws`). | PsyCross patch 0008: `g_psyxVramFrozen` (since D115 our libgpu's `g_psx_vram_frozen`, `src/platform/common/libgpu.c`; the patch went with D118) makes `ClearImage`, `LoadImage`, `StoreImage` and `MoveImage` no-ops during presentation passes, so the game's VRAM bookkeeping happens once, in the real draw. After the fix: 0 words differ, in both widescreen modes (`logs/run32-pause-ws`, `run32-pause-exact`). |
| The picture snapping between 16:9 and 4:3 | The presenter fell back to the PS1 display (drawn 4:3) whenever 4 vblanks passed without a new frame, which on the Vita happens on every slow tick. | It falls back only when the game drew outside `GamestateDraw` since the last frame (loading screens). A slow tick keeps the last frame. Vita3K in real time, with ~200 late vblanks per 300: 0 pictures from the 4:3 display (`logs/run33-vita3k-rt`). |
| Scenery popping at the edges | With a hidden 4:3 logic draw, `func_800521C0` sorted mobys by the 4:3 draw's sector visibility (`D_800771C8`), so mobys in sectors only the 16:9 view shows were dropped. | Gone in normal play (one draw: logic, culling and picture share one view). In the two-draw form, the 16:9 passes keep their own copy of `D_800771C8` and the sector caches. No demo shot changed with it (39 compared). |
| 20–26 fps with widescreen on | The hidden 4:3 draw (above). | One draw. |
| Textures and geometry "loading in" at the left and right edges (second hardware test, main `d67e6fc`) | `r_environment` (`func_800258F0`) runs its per-sector frustum tests on the camera's **view** matrix (`g_Camera+0x14`), which widescreen didn't narrow. Its draw uses the projection matrix, which it did. So sectors only in the 16:9 strips stayed culled to the 4:3 view: their textured near geometry was missing, and parts of buildings floated over holes, until they reached the 4:3 area. | A mips2c `before` hint at `0x8002594C` (`PORT_WS_VIEW_ROW0`) narrows the X row of that view matrix by the same 3/4 as it goes into the GTE, so every X test in the sector loop sees the 16:9 frustum. Host: Artisans' castle tower at the right edge is whole again, and Stone Hill's wall keeps its detail (`logs/run41-compare/edge-zoom-before-after.png`). With features off, the trace and 110 shots are still byte-identical; with both on, the demos still match the oracle (`logs/run40-*`). |
| "Stall" after each menu use | Not a stall: the game is paused while the menu is open, and the next stats window counts that time. Its extra presents are the menu redrawn at 60 Hz. | The log now says `menu: closed after N ms`. |

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

## 60 fps on the Vita: where a frame's CPU goes (2026-10-30)

From the hardware benchmark log (`logs/hw-be37191-bench`, main `be37191`, 16:9 native, texture cache on; free play after vblank 12120). The GPU is no longer the limit there: GPU tail 12–16 ms, 30 pictures/s. Per vblank: game 3.65 ms, OT 2.95, raster 3.49, SPU 0.02 (it has its own core). A tick is two vblanks, and update is ~0.5 ms of "game", so one 16:9 frame costs:

| Part | ms per frame | What it is |
|---|---|---|
| Render path | ≈ 6.8 | the translated renderers, mostly the GTE model they call |
| OT walk | ≈ 5.9 | PsyCross's primitive parse and split set-up (`DrawOTag` minus GL) |
| Raster | ≈ 7.0 | `gpu_gl.c`'s vertex handling and GL submission (`GR_DrawTriangles`) |
| **Frame** | **≈ 19.7** | `present`'s "logic draw" reads 17–27 ms per tick |

"Logic draw" was 4–8 ms before PR #19 because then it held no OT walk and no raster: the logic draw skipped `DrawOTag`, and a second, 16:9 pass paid for them (15–30 ms per tick in total). Now it's the one and only draw.

**What 60 fps needs:** update + the real frame + the in-between frame + snapshot ≈ 0.5 + 2 × frame + 0.3 ms, with room left for presenting and for heavy scenes in a 33 ms tick. So a frame must cost at most ~14 ms. Today it's ~19.7 ms at 16:9. That's why the budget skipped the in-between frame in 49 of 52 windows: it never slows the game down, it just keeps 30 fps.

**Host profile** (gprof, linux32 at `-O2` like the Vita, the Artisans gem script at 16:9; `logs/run45-gprof-O2`). Leaving out the host-only software rasteriser, x86 artifacts and audio:

| Share of the game's own CPU work | Functions |
|---|---|
| **≈ 50%: the GTE model** | `GTE_operator`, `GTE_RotTransPers`, `MTC2`/`MFC2`/`CTC2` (66 million `MTC2` calls in the run), `A3`, `gte_divide` |
| ≈ 25%: the translated renderers' own code | `func_80022A2C` (shaded mobys), sky, collision |
| ≈ 20%: PsyCross's OT parse | `MakeVertexTriangle`/`Quad`, `ParsePrimitive`, `AddSplit` |

**Done here:**
- **Inline GTE register moves.** `src/game/mips2c_rt.h` now does `MTC2`/`MFC2`/`CTC2`/`CFC2` itself, register for register as PsyCross's `INLINE_C.C`. With a constant register number, a plain move becomes one load or store; IRGB/ORGB/LZCS/LZCR/FLAG still go to PsyCross. On the host it removes ~3.7 s of ~16.6 s of the game's own CPU work (`logs/run47-gprof-O2-inline`), and the shaded-moby renderer runs in less than half the time.
- **Checked:**
  - with features off, trace and 110 shots byte-identical to the baseline;
  - with both features on, demos match and the 110 shots are byte-identical to the previous build;
  - gem script identical;
  - `tests/gte`, `m2c_diff_math` and the rest of ctest pass.
- **On the Vita:** expect a share of the 6.8 ms render path back (estimate ~1–1.5 ms per frame). It's not measured on hardware; Vita3K's timings vary 3–4× from run to run and can't tell.
- **Per-renderer timing:** spyro.log now has `draw N:` lines, per vblank: update, env, mobys, spyro, fx, particles, sky (`src/platform/vita/prof_draw.c`); the bracket gives the env calls and, since #190 (D181), `sky main`. The next hardware run says which renderer to cut.

**Verdict (estimates from the numbers above, to confirm on hardware):**

| Mode | Frame CPU | 60 fps |
|---|---|---|
| 16:9 native | ≈ 19.7 ms; ≈ 18.3 with the inline moves | **doesn't fit**. It needs about 4 ms more cut from the OT walk and the GPU submission (13 ms of the frame), e.g. the texture session's vertex-cache work when persp/subpixel are off, and a faster PsyCross parse |
| 4:3 native | 11–18 ms in the first hardware log (`logs/hw-3b4126a-user`, widescreen off): ≈ 25% less geometry than 16:9 | **borderline**: fits in lighter scenes, and the budget already turns it on and off tick by tick |
| `res=ps1` | the same CPU as native (the bench's timed frame minus GPU tail is the same: resolution only moves GPU time) | no better than native for 60 fps |

Nothing is removed. With the budget, the 60 fps option can't make the game slower than 30 fps; at 16:9 it just rarely fires yet.

**Confirmed on hardware (main `26f3138`, `logs/hw-26f3138-bench/spyro.log`):** at 30 fps the game thread is already 93–94% busy in free play. OT walk plus raster are 7.9–8.4 ms per vblank, so ~16 ms of each 16:9 frame goes to the OT walk and GPU submission alone. With 60 fps switched on, the in-between frame fitted in 0–41% of ticks, and the picture switched between 60 and 30 fps. **So 60 fps is experimental and off by default (D44)**; widescreen stays on. Making it the default again needs that ~16 ms brought under ~8.

## Verification

| Check | Result | Logs |
|---|---|---|
| Features off: trace vs baseline build and oracle | identical; both attract demos match the oracle (Stone Hill 866 ticks, Doctor Shemp 768, 11 columns) | `logs/run1-baseline`, `logs/run4-off-stats` |
| Features off: 110 screenshots vs baseline build | byte-identical | `logs/run4-off-stats` vs `logs/run1-baseline` |
| `--widescreen`, `--interp`, `--interp-camera`, both: trace over 11,000 vblanks | identical to baseline (so the demos still match the oracle) | `logs/run5-*`; after the one-draw change, `logs/run32-wsi` |
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
| `patches/PsyCross/0005-presentation-hooks.patch` | `g_psyxVSyncHook` (`VSync`, our own `src/platform/common/libetc.c` since D104; the patch still declares it in `libetc.h`); `g_psyxDrawOTagPre/Post` (LIBGPU `DrawOTag`); since D115 our own `libgpu.c`'s `DrawOTag` calls `present.c`'s `port_present_ot_pre`/`_post` directly; since D118 the patch is only the `libetc.h` declaration | NULL; since D115 the calls do nothing before the first presentation tick |
| `tools/mips2c/hints.yaml`, `mips2c.py` | `before` hints: `func_80022A2C` HUD path at `0x80022D2C` (OFX) and `0x80022D78` (R11) | `PORT_WS_HUD_*` is the identity |
| `patches/spyro-1-port/0010-primitive-space-hook.patch` | `GamestateDraw`: `PortPolySpace()` after it points the draw at its buffer (D54) | returns at once |
| `src/game/order_table.c` `func_800168DC` | records HUD-OT primitives for the 2D squeeze | records nothing |
| `src/platform/common/libspu.c` | dropped writes while `g_psx_spu_frozen` (until D129) | never set |
| `src/game/present.c/.h` | everything else | |
| `src/platform/linux32` | flags, 960×544 output (`png.c`), presenter per vblank (`vblank.c`) | `--out960` off: old output path |

## On the Vita

Wired in on 2026-10-30. On the Vita, **widescreen is on by default; 60 fps is experimental and off by default** (D44: it doesn't fit the CPU at 16:9 yet; see "60 fps on the Vita" above). The linux32 host keeps both off, so its oracle checks stay exact. `config.txt` keys: `widescreen=0/1`, `interp=0/1/4` (OFF, INTERPOLATED, 60 HZ; D44's `2` without animation and `3` camera only are read as `1` since D148, and stay the host's `--interp-no-anim` and `--interp-camera`; `interp=1` counts only with `config_version=2` or later; the menu writes `config_version=3` since D68) and `interp_budget` (default on; `0` turns off the game thread's safety net, D75). The user's guide is `docs/vita-testing.md`. Scripted Vita3K runs (`tools/vita/vita3k_run.sh`) write `widescreen=0` and `interp=0` first, so they still compare with the host unless a run turns them on.

**Options menu** (`src/platform/vita/menu.c`, panel drawn by `src/platform/common/menu_panel.c`): hold SELECT and press START.
- **Pausing:** the menu runs inside the game's VSync wait (`vblank.c`), so the game is paused. Afterwards the real-time count starts again, with no catch-up, so `g_DeltaTime` doesn't jump.
- **Buttons:** `pad.c` hides the opening combo, and whatever closed the menu, from the game until they're released.
- **Changes:**
  - they apply at once (`vita_present_apply`; `present.c` takes them at its next tick, drops its queue when switched off, and starts blending afresh when switched on);
  - widescreen re-scales the GPU targets (`vita_gpu_rescale`);
  - closing writes every row's key back to `config.txt`, with `config_version=2` (`3` since D68), and keeps its other lines (the rows are a table, D45).
- **Labels:** `WIDESCREEN` and `60 FPS` (OFF, INTERPOLATED, 60 HZ; 60 HZ by default since D150, OFF before; until D148 `60 FPS (EXPERIMENTAL)` with ON, `NO ANIM`, `CAMERA`: the label is short so `< INTERPOLATED >` fits beside it in the panel's 32 columns). One row for every disc since D150 (PAL runs at US timing); D148 had another disc's row offer OFF and INTERPOLATED under a `60 HZ LOGIC: US DISC ONLY` row, and a `PAL 60` row for the PAL disc. `60 FPS BUDGET` was removed in D75: with 60 fps on, the in-between frame always runs, unless the game thread itself would run late.
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

## Draw distance (D49, off by default)

With widescreen on, players see scenery and objects appear at a distance, most visibly at the sides. This option draws them further out. It changes only what is drawn: the game logic sees exactly what it sees with the option off.

**The menu row (#260, the owner's decision of 2026-11-25):** `DRAW DISTANCE` with `NORMAL` (`draw_distance=100`, the original) and `MAX (ADAPTIVE)` (`400` since #261, `200` before: `DD_MAX_PCT` in `picture.c` and `picture_ps4.c`, also the most `draw_distance=` gives; "Past 2×" below), on the Vita and the PS4. Until #260 it was `DRAW DISTANCE (EXP.)` with three values (100, 150, 200). "Adaptive" is the limiter below: MAX's full scale where the picture leaves room, less or the original where it doesn't. On the Vita, with 60 Hz logic in play, the budget follows the game thread's measured time (D212, `dd_budget=auto`); hardware session 8 (D252) saw ×2.00 in Toasty and 1× held in Artisans, with 60 fps kept. Since #271 (`dd_free`, on by default) a scene whose 1× draw is over the budget still grows wherever a draw writes no more than the last 1× draw ("Growing where a draw writes no more than 1×" below). The PS4 has no timed budget (`lod_time.c` is the Vita's): its limiter keeps the buffer's limits and, with 60 Hz logic, the fixed 48 KB work budget (`LOD_BUDGET_60`, #180). `config.txt` keeps the percent, so `draw_distance=150` (1.5×, as `bench.c` sets) still works: the row then shows `150%`, and left or right goes to the nearer value, `NORMAL` on a tie; the file keeps 150 until the row is changed. So does a `draw_distance=200` the menu saved before #261 (MAX then): it stays 2× and shows `200%`, and left or right from it goes to `NORMAL` (100 is nearer than 400); choosing MAX again writes 400.

### How Spyro decides what to draw at a distance

- **The world** (`asm/renderers/r_environment.s`, `func_800258F0`):
  - Each sector goes through a frustum test on its bounding sphere. A visible sector marks `D_800771C8[sector]` (which `func_800521C0` uses to keep mobys in visible sectors). That mark doesn't depend on distance.
  - Then the sector loop sorts sectors by `g_Environment.m_LodDistance` (`+0x24`, set once to `0x8000` in `initialization.c`):
    - a sector that starts within it goes into the **near list** (`D_8006FCF4+0x1C00`), drawn textured and high-poly;
    - a sector that reaches past it goes into the **far list** (`+0x2000`), drawn low-poly and Gouraud-shaded.
  - The far draw (from `0x80026200`) drops polygons beyond `m_CullingDistance` (`0x14000`, `0x28000` with an occlusion group, `environment.c` `func_8002B9CC`). It also fades out low-poly polygons inside `m_LodDistance` (`s3 = (LOD >> 7) - 0x20`). The near draw (from `0x800267B0`) fades high-poly vertices toward it (`s5`).
  - `m_LodDistance` is read in those three places only (`grep` of `asm/` and `src/`). Nothing in the logic reads it.
  - **"Scenery popping in"** is the near list's textured geometry replacing the low-poly version at `0x8000` units. The far-geometry cull distance is a separate, larger distance; this option leaves it alone.
- **Mobys** (`asm/renderers/r_moby.s`):
  - Three culling loops: `func_8001F158` (normal), `func_800208FC` (specular, gems among them) and `func_80022A2C` (lit; also the HUD's models).
  - Each loop tests a camera-centred **box** of half-size `m_RenderRadius << 10` world units (`+0x50`), with grace of `0x800` for a moby drawn last frame, and the view depth against the same size. Then come the frustum planes.
  - A moby that passes gets `m_WasDrawn` (`+0x51`) set to 1 and a draw record. Its **detail distance**, `(m_Renderer & 0x3F) << 8` (`+0x4B`), goes into the record (`+0x2E`). `func_8001F798` reads it as `ZSF4` to choose the model's detail and whether to blend animation frames.
  - `m_WasDrawn` feeds the logic: `func_80051FEC` (`asm/moby_lists.s`) updates every moby drawn last frame, whatever its update distance, and enemy code checks it. So the draw box **is** logic, and widening it in place would change the game (as the 16:9 frustum does in one-draw widescreen, D30).
  - Normal mobys have a record bound (53 records, `D_8006FCF4+0x1600..+0x2200`). Specular ones have none of their own.
- **Widescreen**: the sector frustum and the moby frustum already use the 16:9 view (`PORT_WS_VIEW_ROW0`, the narrowed projection; "Fixes from the first hardware test"). The distances above are the same at 4:3 and 16:9; at 16:9 more of the distant edge is on screen.

### What the option changes (`src/game/lod.c`, `tools/mips2c/hints.yaml`)

`draw_distance=N` (Vita and PS4, a percent up to 400; the menu's `DRAW DISTANCE`, `MAX (ADAPTIVE)` for 400) or `--draw-distance X` (host, any X) scales three things:

- **World detail:** the three reads of `m_LodDistance` (`PORT_LOD_ENV`). It's capped at `m_CullingDistance`, so near geometry is never deeper than the game already draws.
- **Moby detail:** `(m_Renderer & 0x3F) << 8` as it goes into the record, capped to fit the record's 16 bits.
- **Moby draw distance**, without touching the logic: each culling loop first runs exactly as on the PS1. At its end, a hint runs the same list once more ("pass 2") with the longer box:
  - mobys pass 1 drew are skipped, and their `m_WasDrawn` (which the loop clears at each moby) is put back to 1;
  - the others get draw records but keep `m_WasDrawn` 0 and add no shadow;
  - pass 2 stops where the record space ends;
  - a port-side bit per moby gives pass-2 mobys the same grace the game gives its own;
  - pass 2's mobys take the low-detail model (their record's detail is −1 where it would be positive, with no fade and no extra fog): the original's look past its detail distance, only for mobys past its draw distance (`moby_cull_hand.c`, D175).

**Safety.** The renderers have no bounds checks, and more geometry can overrun the primitive buffer (`0x1C000` bytes, `0x13000` in reduced-space levels, with the HUD's mobys at the top; `draw.c` `GamestateDraw`). The original game already fills it to 85–91% in places (Artisans' entrance, the intro cutscene). With the option on, every draw spans both buffers ([Primitive space](#primitive-space-d54)): the room is 0x37A00 bytes from the lower buffer to 0x600 under the HUD OT, or 0x25A00 in a reduced-space level (`draw_room()`; the `lod` line's "of C KB": 222 or 150). Since #203 the guards measure against that span. Before, they kept one buffer's size: in Stone Hill the world alone (52 KB at 1×) was 78% of the 66 KB its bound left it, so 2× never grew there, even with the budget off. With the option on:

- **Hard bounds.** These only ever end a loop early, so a frame can lack some scenery but never writes past the span:
  - the six sector-list loops of `func_800258F0` take "end of list" once the primitive pointer is within `0xB000` (45,056) bytes of the span's top. That room is what the game draws after the world (sky, particles, fade, HUD: at most 31,604 bytes) plus one near sector (at most 5,868 bytes), both measured over the whole-game tour at 2× (D49, and again with the span as the room in #203), with about 7.5 KB to spare. The far loop's sectors can be much bigger (57 KB for a background sector at 1×), but with the option on that loop writes only less than at 1×: polygons inside the longer distance are skipped, and sectors wholly inside it aren't listed;
  - the record-draw loops (`func_8001F798`, `func_80020F34`) and the lit loop's pass 2 stop at pass 2's mobys once 60% of the original's one buffer is used. The mobys draw before the world, which keeps the rest;
  - the near list stops taking sectors one entry before the next list.
- **A limiter.** After each shown draw it measures the bytes used against the span, and the world's bytes against the room its bound leaves (the span less `0xB000`), and takes the fuller of the two. Over 80% it halves the extra distance, over 90% it drops to 1×, and under 65% it grows back by 1/32 per draw. So the bound stays a backstop rather than a regular cut.
- **A 60 Hz work budget** (D175, #180; on by default). With 60 Hz logic in play (tick60's `wants_60`: not the title, the attract demos or 30 Hz), a tick's draw has half the time, and on the Vita the logic draw's cost follows its primitives (about 0.2 ms a KB in the owner's hardware log; pictures lost from about 52 KB). So the limiter also keeps the bytes the draw wrote under `g_lod.budget` (`LOD_BUDGET_60`, 48 KB): above it the extra distance halves, above 9/8 of it it drops to 1×, and it grows again only under 7/8 of it. Where the original's own draw is over the budget, 2× draws as 1×. `dd_budget=0` (Vita) or `--lod-budget 0` (host) turns it off, leaving only the buffer's limits, as before #180 (#185); pass 2's low-detail mobys stay either way.
- **The Vita sizes that budget from time** (`dd_budget=auto`, the default since #212; `src/platform/vita/lod_time.c`). A fixed byte budget can't see that a draw got cheaper (D202, D204, D206): on hardware Stone Hill at 2× held at 1× with 271–300 budget back-offs a window while the game thread was busy 71.5% (D209). So once a logic frame, at the start of the next shown draw (lod.c calls the platform through `lod_set_budget_hook`; with 60 Hz logic the update is `GamestateUpdate_60`, so a wrap of `GamestateUpdate` would never see it), the platform takes the game thread's work for the frame just done: the update plus the logic draw without its VSync waits (GamestateDraw's `DrawSync(0)`, where the game thread joins the walk, comes before its `VSync`), plus any presenter frame on the game thread (`g_present_stats`' `us_update`, `us_logic`, `us_pass`, timed with `stats=1`). Only frames whose draw kept to the budget count (`lod_last_draw`: 60 Hz logic in play, the option on).
  - **Target 14.5 ms** of the 16.68 ms frame. The 2.2 ms left cover what that measure doesn't see but the frame holds: VSync's vblank work on the game thread (the pad, the CD and mixer hand-offs, posting the picture), the loop between the draw and the next update, and the vblank's and the scheduler's jitter (the walk and presenter threads share the cores).
  - **Cut fast, grow slowly.** A frame over the target sets the budget at once to the draw's bytes (or the budget, if smaller) × target / used, so the limiter backs off at the next draw. After 8 frames in a row at least 1 ms under the target the budget grows 2 KB a frame while that lasts, and only while it is under 5/4 of the draw's bytes plus 8 KB (the limiter grows the distance only under 7/8 of the budget, and the budget never runs far ahead of what is drawn). Frames in between hold it.
  - **lod.c still limits**: the platform calls `lod_set_budget()`, which clamps the budget between `LOD_BUDGET_60` (48 KB) and the span the draw has (`draw_room`), and the limiter above applies it as before. `dd_budget=1` keeps the fixed 48 KB (the default until #212), `dd_budget=N` (2–1024) a fixed N KB, `dd_budget=0` no budget. With `stats=0` there is no timing and auto keeps 48 KB. The host never sizes it: `--lod-budget` stays a fixed byte budget, so the host's traces and dumps don't change.
  - **Where the 1× frame goes (#228).** In hardware session 7's Artisans play (D219, 60 HZ, 1×) the game thread took 15.9 ms a vblank: the world 7.8, the mobys 4.2, the sky 1.8 (`draw N:`), with the vertex cache's record most of each renderer's cost (D199). About a fifth of the projections the record works out are exact repeats: the same registers (by generation) and the same vertex under one key, such as a point two sky sectors share or a world grid's corner projected again (host Artisans at 60 Hz: 4.3 M of 17.3 M). Since #228 the record keeps each entry's last projection worked out (`Rp`, 12 bytes an entry) and skips a repeat's maths while the entry is valid with one point, where the repeat would change nothing; the same answers by construction (`vcache repeats:` line). The sky's 1.20 → 1.77 ms between sessions 6 and 7 is not in its code: the portal sky's part (D213) stayed 0.13–0.14 ms, and the main sky's work and time are the same in Vita3K and on the host at 85a7b2c and after D216–D220.
  - **Where Artisans' 1× frame goes on hardware (#259).** Session 8 (D252, `logs/hw-ap19t`, 11,400): game 15.84 ms a vblank, the draw 14.7 of it (env 7.91, mobys 4.49, sky 1.15, Spyro 0.83). The game thread recorded 5,624 projections a vblank and worked out 5,128 of them at once (91%); its walk on core 1 was 85% busy, so the idle rule kept few for it. Over the session's 60 Hz windows the draw's ms follow the projections worked out at once best (about 2.5–2.8 µs each, the renderer's own work for the vertex included); bytes alone explain less. The `expand 1094 ms` on the `vcache` and `atlas` lines is the render thread's vertex expand (`gpu_gl.c`, core 2), not the game thread's (as D199 found); the lookups are the walk's (join 0.27 ms). On the host (owner60's pad, Artisans 6,600–9,000, every logic projection worked out at once as on the Vita, `VC_LAZY_LOGIC=0`) the record is 79% of the renderers' time under qemu, whose software floats overstate it; with no vertex cache the renderers take a fifth of the time. By part, a draw records env 1,929 (28% exact repeats), mobys 1,097, sky 418, Spyro 242, fx 51, particles 30. **2× costs Artisans no more than 1×**: with the budget off the host's 6,900–7,800 draws at ×2.00 write 62–74 KB against 59–77 KB at 1×, and record as many projections (7.71 M against 7.69 M); the world writes no more (48.5 against 50.2 MB over the window; why it writes less wasn't looked into) while pass 2 adds 2–11 mobys a draw. So the timed budget holds 1× there because the 1× frame is over 14.5 ms, not because 2× would cost more.
  - **The record's cuts (#259, same answers).** The repeat's record (#228) moves into the entry: 8 bytes where candidate 3 would go, while the entry has at most three (`RpE`: the generation's distance from the table's frame's first plus 1 in 15 bits, the clamped flag, the vertex; a generation 0x7FFF or more past the frame's first keeps and skips none), so a record reads no line of the 96 KB `rp` array (`vc_repeat` 2, the default; 1 is D221's array, 0 none). And the logic draw's native record (`ngte_vcache`) takes `find`'s and `rtp_apply`'s bodies inlined, with no ten-argument call and stack copy of the sums per projection (`psx_vcache_set_fast_record`, default on). Host switches `VC_REPEAT`, `VC_FAST_REC`, and `VC_LAZY_LOGIC` (0–3) for the record checks. The Vita's `draw vc N:` line (after `draw kb`) gives each part's records a vblank as recorded/at once/kept and the repeats skipped; the host's `--prof-draw` gives the same at exit, from vblank `PROF_DRAW_FROM` when set.
  - **Two more record cuts (#259 attempt 2, same answers, behind `vc_fast_rec`).** Hardware session 9 (D264) put the vertex cache at ~4.8 ms a vblank of Artisans' game thread; per part (session 9's `ap23a1`/`a3` against `ap23x1` with the fix off, 6,900–9,000), each projection worked out at once costs the world ~1.25 µs, the mobys ~1.2 µs and Spyro ~1.35 µs, about 550 cycles at 444 MHz for ~400 instructions, so part of it is waiting for memory. The 60 Hz frame needs ~0.9 ms more (15.4 ms of 14.5 in the heaviest stretch), about 13% of the vertex cache's share there. `vc_fast_rec` became bits (`psx_vcache_set_fast_record`, host `VC_FAST_REC=0`–`7`; 1, the default, is D261's inlined record):
    - **2, lean:** the game thread's record without D168's cost model (the `touch()` line counts of each probe and candidate, the probe and division counts, the store's lines and the worked count). Those are diagnostics only: `vc.div`, `vc.probe`, `vc.lines` and the game side of `vc.rec.div/probe/lines/store/game` drop (owner60 in Vita3K: −31% to −50% and −97% to −100%); the counts the draw distance's `dd_free` rule reads (`VCW_PROJ`), the `draw vc` line and the vcache lines stay.
    - **4, one ahead:** each native record is done one record late, after the next one has prefetched its entry's three lines. The probe is a random read in a 320 KB table (two of them, taking turns), which the A9's L1 never holds; now that miss overlaps the record before. The record's inputs wait in `Pend` (registers copied only when their generation changes), and everything on the game thread that reads or adds to the logic table first does the one waiting (`pend_flush`: the other record paths, hand-overs, renderer ranges, HUD notes, `psx_vcache_game_recs`, the frame's end and hand-over to the walk, an in-place walk's lookup, the presenter's begin, the switches), so the table sees the same records in the same order.
    - Checked: host FNV per polygon `--strict` identical to develop on the 11 routes with the default and with 7 (lean and ahead) at `VC_LAZY_LOGIC=0` (every logic projection at once, as on the Vita's game thread), and on four routes with 7 and the host's default keeping; the host's vcache lines are identical with the default. Vita3K: the 8 routes identical to `base-e603aaa` with the default, owner60, stone60 and toasty60 with 7, owner60 with 3. Neither the host nor Vita3K models the A9's caches, so the ahead mode's gain (and the lean mode's) is the hardware A/B's to show (`runtime/autopilot/runs/vita/issue-259-2/hw/`, one VPK, `vc_fast_rec=1/3/5/7` twice each on owner60's Artisans with virtual vblanks, then Toasty in real time 1 against 7). The default stays 1 until then.
  - **The walk's lookup (#233, D226, same answers).** In hardware session 7's Toasty at 60 Hz (D219) the walk on core 1 was 93% busy, and the vertex cache's lookup is most of its time. About 99% of the walk's polygons have every vertex found valid; `psx_vcache_poly` looks their vertices up first and finishes such a polygon on a short path of its own (the depth choice, its counts, the references and the answers at side), and hands anything else to the full body (`poly_full`), which looks the polygon up again with the first pass's counts dropped. `psx_vcache_set_found_fast(0|1|2)`, the Vita's config key `found_fast=0/1/2` (#274, shown on the `threads:` line): 2 (the default) the short path with each vertex's first probe read up front, so a polygon's misses on the table can overlap on the A9; 1 the short path alone; 0 the full body for every polygon, as before #233. All three draw the same pictures; Vita3K shows no time difference between them, so only a hardware A/B on the Toasty route can tell.
- **Growing where a draw writes no more than 1× (`dd_free`, #271, on by default; the owner's decision of 2026-11-26).** MAX means at least 1× and farther wherever it can. With the budget alone, a scene whose 1× draw is already over the 60 Hz budget held 1×, even where a longer distance costs nothing more: in Artisans hardware session 8 (D252) held 1× because the 1× frame was over 14.5 ms, while on the host, with the budget off, 2× writes 62–74 KB a draw against 59–77 KB at 1× and records as many vertex-cache projections (D261; the record is what the game thread's time follows there). So with `dd_free=1` a draw over the budget is compared with the **last 1× draw** instead (`lod.c` `limit()`, only with a 60 Hz budget, fixed or timed):
  - a 1× draw is the reference: its bytes of primitives and the vertex-cache records the game thread made in it (`records_now()`: `g_vcache_work`'s projections plus `psx_vcache_game_recs`' frame so far, so the difference across a draw holds wherever the frame's end falls; with no vertex cache both stay 0 and only bytes compare). Over the budget it is let through, so the scale may grow by the usual rules (the buffer's 65%, the near list past 2×, the hold, 1/32 a draw);
  - a longer draw over the budget that writes **no more bytes and no more records** than the reference is let through too, and the scale keeps growing;
  - one that writes **more of either** backs off at once, with the reference as the budget: the extra halves (hold 30), or over 9/8 of the reference in bytes or records it drops to 1× (hold 60), and the next 1× draw is the new reference;
  - a draw under the budget takes the budget's rules alone, and a new level, gamestate or loaded save state drops the reference (a save state's first draw at full scale then backs off by the budget's rules until a 1× draw sets one).
  - The reference is one draw, kept until the next 1× draw: while the scale stays up it is not refreshed, so a longer draw is held to what 1× wrote when growth began, not to what 1× would write at the same moment. In Vita3K (below) that let 2× write up to 3–5 KB a draw more than the same frames at 1×, with as many records (−8% to +3%).
  - `dd_free=0` (Vita `config.txt`) or `--dd-free 0` (host) is the rule before #271: the same lod lines and pictures as before. A build with `-DSPYRO_DD_FREE_DEFAULT=0` starts with it off. On the Vita a start-up line says which (`picture: draw distance over the 60 Hz budget ...`, `lod_time.c`). The PS4 and PC builds have no key and keep the default (on), with their fixed 48 KB budget.
  - **Measured in Vita3K** (`runtime/autopilot/runs/vita/issue-271-1/`, the 8 routes against `base-20d3514`; no time claim from Vita3K): with `dd_free=0` owner60, stone60 and toasty60 give the same shots (91, 95, 150) and the same lod lines as the base. With the default the title, pause-ws, pause-ws-wobble, wobble60 and demo60 (no 60 Hz budget there: 30 Hz, 1×, or an attract demo) are identical; owner60's Artisans goes from 1× held (vblanks 6,900–7,500) to ×2.00 (74, 70, 69 KB a draw against 77, 67, 66 at 1×; 3,959–4,414 records a draw against 4,284–4,336), shots 6,600–7,700 differ (11 of 91, the far band); Toasty's 13,500–14,100 go from 1× to ×2.00 (56–57 KB against 52–53, records −1% to −2%), 15 of 150 shots differ; Stone Hill grows a little (×1.12–1.16 where 1× held; back-offs past the reference 15–38 a window), 4 of 95 shots differ. Shots after each stretch are identical again: the logic is untouched (picture only, D49). The host (O60, S60 and T with `--tick60 --draw-distance 2 --lod-stats 300`, the fixed 48 KB budget) gives the same lod lines as Vita3K's owner60 (Artisans ×2.00 at 6,900–7,800, 74/70/69/62 KB against 77/67/66/59), Stone Hill ×1.12–1.16 at 8,700–9,000, and Toasty ×2.00 at 9,000–9,900 (50–60 KB a draw against 48–56); with `--dd-free 0` its lod lines are the base's on all three (32 lines each). The host's lod line buffer (400 bytes, `vblank.c`) cuts the `free` part short; the Vita's is 640.
  - **Hardware A/B (queued, needs the owner's yes; `runtime/autopilot/runs/vita/issue-271-1/hw/`):** one VPK, `dd_free=1` against `dd_free=0`, each twice in alternating order, on owner60's route (Artisans 6,600–9,000) and the Toasty route, the owner's settings (MAX 400, `dd_budget=auto`), real-time vblanks: late vblanks per 300 and the `lod` lines. The default stays on unless `dd_free=1` shows more late vblanks than `dd_free=0`.
- **Past 2× (#261, D256).** MAX was 2× until #261; the owner asked for more wherever the frame has room, as long as 60 fps holds. What decides how far a longer distance can still add anything:
  - **The world** stops at `m_CullingDistance`: `PORT_LOD_ENV` never goes past it. That is `0x28000` (5×) wherever the camera is in an occlusion group, which is most of play (201 of the tour's 268 windows of 100 vblanks), `0x14000` (2.5×) elsewhere in play, and `0x1C000` (3.5×) on the title and in the demos (`environment.c` `func_8002B9CC`).
  - **Mobys** stop where pass 2's box reaches the culling distance's or 0x7FFF (below).
  - **Measured** (host, the whole-game tour's 35 levels 10–64, 300 vblanks each, 16:9, `--lod-guards-only` so every draw is at the scale; the table in `runtime/autopilot/runs/vita/issue-261-1/host/tour-final.txt`). A level gains when a step adds at least half an extra moby or half a near sector a draw, or 1 KB of primitives: 2.5× over 2× in 30 of the 35 levels, 3× over 2.5× in 24, 4× over 3× in 14 (up to 12 more mobys and 12 more near sectors a draw, in levels 22, 23, 40 and 42), 5× over 4× in 7, and there only pass 2's far, low-detail mobys (at most 4 more a draw, in 20 and 42) and one near sector (40). So **MAX is 4×**: past it the world draws nothing more anywhere but in one level, and the mobys a few far specks. The sky, the far list's cull and the fog stay the original's at any scale.
  - **What fills first:** the near list. At 2× it peaks at 125 of 127 (level 60); with every draw at 2.5× it fills there (680 sectors left out over its 350 draws), at 3× also in level 35, at 4× in 42 and 43 as well. The primitive span comes next: with every draw at 4× the world's loops stopped early in levels 40 and 41 (the bound working; 181 KB of 222 at most). Pass 2's moby records fill from 3× in 8 levels (12, 13, 22, 23, 40–43), where pass 2 simply stops.
  - **How the limiter goes past 2×:** the same rules (a new level at 1×, growth by 1/32 a draw, the buffer's and the budget's back-offs), and three more, all in `lod.c`:
    - *only where it is measured*: the scale passes 2× only after a draw that kept to a 60 Hz budget (60 Hz logic in play and `g_lod.budget` on: on the Vita `dd_budget=auto`, the game thread's time, D212; the PS4's fixed 48 KB). Where nothing measures the draw's cost (the title, 30 Hz logic, `dd_budget=0`) MAX is 2×, as before (`scale_top`). `--lod-guards-only` takes the scale as given;
    - *the near list*: above 2× the scale grows only while the last draw's near list is under 104 sectors (`NEAR_GROW`), and the part above 2× halves when it is over 116 (`NEAR_HIGH`), before the hard bound has to drop a sector. At 2× and below nothing changes;
    - *pass 2's box at most 0x7FFF* (`BOX_MAX`): `r_moby` rotates the moby's offset from the camera, in its /4 units, as 16-bit values, so a box past 0x7FFF can let in a moby whose offset wraps there, to be drawn somewhere else. With `m_CullingDistance` at `0x28000` the longer box could reach 0xA000: at 2× already in 16 of the 35 tour levels (up to 2,864 boxes in level 55's stay), at 4× in all of them. On the tour no moby took that path (pass 2's mobys a draw are the same with and without the cap at 2×, 3× and 4×), so it changes no picture there; it keeps the longer box inside what `r_moby` can hold.
  - The `lod` line's near part reads `(N times full, B back-offs)`, B the near list's.
- **Each new level or gamestate starts at 1×.** A loaded save state (D51, `port_lod_reset()`) instead goes straight to the chosen scale, with the limiter backing off after one draw if the spot is heavy.

The first whole-game run had only the limiter, and it crashed entering level 40 at 2×: one frame wrote past the second buffer into the world OT (`logs/run4-whole-dd2`, core read with gdb: `fp = 0x801BFDA4`, past `0x801BFBB0`).

Only drawn pictures use the longer distances. The hidden 4:3 logic draw of attract demos (D30) keeps the original ones.

**The `lod` line** (the Vita's `stats=1` with the option on, every 300 vblanks; the host's `--lod-stats N`): `lod V: D draws at xA average (lowest xL); primitives P KB average, K KB peak of C KB (U%); N back-offs (M for the budget, of them H held at 1x); E extra mobys per draw; near sectors peak S of 127 (F times full, B back-offs); moby records peak ...; largest sector ..., after the world ...; stopped early: world W, mobys B`. C is the room the draw has: with the option on, the span both buffers give (222 KB, 150 in reduced-space levels; since #203), and with it off (baseline numbers) the draw's own buffer (110 or 76 KB). A back-off is a draw after which the limiter halved the extra distance or dropped it to 1×. M of the N are the budget's alone: draws that went over the budget while the buffer (or the world's bound) was at most 80% full, so only the budget made the limiter back off. H of the M were at 1× already: the budget holds the longer distance at 1× there, so a window whose H is close to D draws 2× as 1× (the owner's Artisans and Stone Hill at 60 HZ, #200: `x1.00 average` with H about 240–300 of 300). Before #200 M counted only draws above 1×, so a draw held at 1× showed only in N. Before #203 the world's one-buffer bound was often over 80% where the budget held 1× (Stone Hill at 60 Hz: N 300, M 0), so those draws showed only in N too; with the span as the room the same draws read N 300, M 300, H 300. With `dd_budget=0`, or at 30 Hz, M and H are always 0. With `dd_free` (#271) a window where a draw went over the budget ends with `; free F, past 1x P (1x R KB N rec; Q rec/draw)`: F draws over the budget let through (no more than the last 1× draw wrote), P back-offs past that draw, R and N the reference's KB and records, Q the window's records a draw; such draws no longer count in M or H unless they backed off, and windows with neither (and every window with `dd_free=0`) read as before. With the Vita's `dd_budget=auto` (#212) the line ends with the budget and why it backed off: `budget B KB (auto, LO-HI KB, worst W ms, L ms left; O over)`, or, where the budget held most draws at 1×, `budget held at 1x: U ms used of 14.5, worst W ms (LO-HI KB)`. B is the window's average budget, LO and HI its lowest and highest (#224), W the longest game-thread frame measured, L the time the average frame left under the 14.5 ms target (`ms over` past it), U the game thread's average work per logic frame, and O the frames over the target, each of which cut the budget at once. A W far above the rest with O small is one long frame (a level entry), not a scene over the target. The Vita's line buffer is 640 bytes since #223 (D214); keep the line under 600.

### Measured (host and Vita3K; hardware not yet)

- **Logic:**
  - Gems script (8,600 vblanks, 16:9): traces at 1.5× and 2× byte-identical to 1×, which is byte-identical to the build before this change (`logs/run19-gems-*`, `logs/run1-base`).
  - Whole-game tour (`--tour`, every level 10–64, both endings): at 2× byte-identical to 1× and to main's binary for the 40,129 vblanks up to ending 3's credits. Main's binary crashes there with `--widescreen` too, so this isn't caused by the change (D49). Since fixed: the 16:9 draw overran the primitive buffer ([Primitive space](#primitive-space-d54), D54).
  - Oracle replay of the six hooked renderers, option off: 213/213 samples match.
- **Pictures:** at 2× the title flyby keeps the cave textured where 1× shows flat low-poly walls. In Artisans the far hills and walls keep their textures (`logs/run3-dd2/cmp-*.png`).
- **Buffer:**
  - Gems script at 2×: the limiter held 2× in Artisans' open parts (up to 12 extra mobys per draw) and backed off to 1× where the original already fills 85–91% (the intro cutscene, Artisans' entrance).
  - Stone Hill's attract demo stays at 1×. This line once gave the reason as its world alone coming close to the bound; since the presenter draws the demos' frames (D128) the reason is that the game's limiter never runs in a demo, so its hold never counts down (#234, "The attract demos draw at 1×" above).
  - Whole tour at 2× with only the hard bounds (`--lod-guards-only`, no limiter; `logs/run19-whole-guards`): no crash in 100,000 vblanks. The world bound stopped a loop 125,760 times and the near list filled 55 times; the peak stayed at 95 KB of 110.5.
  - With the limiter (`logs/run19-whole-dd2`), the bound never fired. The scale averaged ×1.23 over the tour and was at least ×1.5 in 30 of 132 five-second windows: the limiter is cautious, by choice.
  - **With the span as the room (#203)**, the same tour at `--widescreen --draw-distance 2` (100,000 vblanks; `runtime/autopilot/runs/auto-203-1/host`). No crash. The trace and the memory card are byte-identical to the build before. The scale averaged ×1.81 per draw, against ×1.23 before, and was at least ×1.5 in 267 of 332 windows, against 72. The limiter backed off 0 times, against 25,400. No world or moby loop stopped early, against 5 world stops. The peak was 149 KB of 222 (67%, ending 3), or 105 KB of 150 in a reduced-space level (70%, level 64's ending, which reaches 101% of one buffer at 1× there). The near list peaked at 125 of 127 and never filled. The largest sector was 5,776 bytes and the most drawn after the world 30,852, both inside `0xB000`'s measured terms. With `--lod-guards-only` (2× throughout, only the bounds): no crash, the same trace, and the same peaks, with no loop stopped.
  - **Stone Hill (#203, #200's host route: new game, the tour to level 11, `--tick60`).** With `--lod-budget 0` the scale goes from ×1.00 to ×2.00 from vblank 6,900 on: 86–88 KB of 222, about 29 extra mobys a draw, no loop stopped. With the default budget the scale and the primitives are the same as before (×1.00, 77 KB; the budget holds 1×). Only the line's M and H count those draws now. The traces are identical either way. In Vita3K `owner60` with `dd_budget=0`, vblanks 6,900–7,800 go from ×1.00 to ×2.00 (GL draws +12% at 6,900–7,500). With the default budget, the 60 Hz stretch (6,600–9,000) gives the same scale, the same counters and the same shots. The title flyby and the 30 Hz stretch of Artisans (3,300–6,000, a reduced-space level) now grow to ×2.00, and their shots differ.
- **Cost:** see D49. More primitives (+35% at 1.5×, +49% at 2× where it holds) mean more OT walk and GPU submission on the Vita's CPU. Expect fewer than 30 pictures a second in open areas at 2×; `bench=1` measures it.
- **With 60 Hz logic** (D175, Vita3K `owner60`, vblanks 6,600–9,000): the budget and pass 2's low-detail mobys took 2×'s GL draw calls down 6%; the pictures differ only in the far band. On hardware (D179, Artisans) `owner60`'s route had no late vblank; the budget backed off twice. Scenes over about 52 KB at 1× still lose frames with 60 Hz logic whatever the draw distance.

## Follow-ups

1. Second hardware test after the fixes above (`docs/vita-testing.md`): no snapping, no red/green after pausing, 30 fps held with widescreen, how often the in-between frame fits, and a tuned `interp_budget` default.
3. Flame, line-particle end points and the HUD counters' spin at 60 fps.
4. Optional: anchor HUD items to the 16:9 edges instead of the 4:3 safe area.
5. If a tick takes 3–4 vblanks (`g_DeltaTime` 3–4), blend at ⅓ and ⅔ instead of ½.

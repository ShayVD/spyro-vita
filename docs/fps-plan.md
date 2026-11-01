# Above 30 fps on the Vita: measured costs and a ranked plan (D62)

Written 2026-11-01 from the hardware logs and the code on main `987c435`. Nothing here changes the game; the only code in the same PR is measurement (marked below). Numbers from logs are measured; numbers marked *estimate* are reasoning from them, with the reasoning given.

## Summary

- **Where it stands.** 30 fps holds on hardware (142–150 pictures per 300 vblanks in play: `logs/hw-4f26bc7-user`, `logs/hw-26f3138-bench`). In the heaviest scene logged (16:9 native, `logs/hw-26f3138-bench/spyro.log`, vblanks 12600–18300) the game thread is 93–94% busy. With 60 fps on, the in-between frame fitted in 0–41% of ticks (D44).
- **There are two limits, not one.** D44 found the CPU too slow for two frames a tick. Reading the same logs against the frame's timeline (below) shows that **the GPU is close to full as well**, at native 16:9 in heavy scenes: roughly 25–33 ms of GPU work per frame (*estimate*), against the ~13 ms a frame that 60 fps allows. No log measures this directly yet, so this PR adds that measurement (`gpu_time=2`).
- **Clocks are already at the maximum.** `main.c` has set ARM 444 MHz, bus 222, GPU 222 and crossbar 166 since M3 (`498c9e5`). No gain is left there; every number in this document is at those clocks.
- **What's realistic** (*estimates*, to be firmed up by Phase 0):
  - After Phase 1 (quick wins): heavy-scene 30 fps drops from 93% to about 75–80% busy. 60 fps still only holds in light scenes.
  - After Phase 2 (a render thread, plus GPU shader work): the 30 fps game thread is about 50% busy in heavy scenes. 60 fps holds in most normal play (roughly 70–90% of play time), and drops to a *steady* 30 rather than flickering between the two where it can't.
  - **A locked 60 at 16:9 native everywhere** needs the GPU's work per frame roughly halved. That's only possible if Phase 0 shows the GPU has more room than inferred, or if the opaque fast path (G1) pays off in full. Otherwise a locked 60 needs a lower render scale in 60 fps mode, which is the user's call (open question 2).
- **Top three recommendations:**
  1. **Measure first (Phase 0, three short hardware runs, below).** It decides whether the GPU work (G1–G4) has to come before the deeper CPU work.
  2. **A render thread** (T1): vitaGL submission, vertex expansion and uploads on core 2, with `DrawOTag` handing the frame over the way the PS1's GPU took it (the game already joins at `DrawSync(0)`). That's about 12 ms a frame off the game thread, the biggest single CPU win. It has no data hazard with the game's memory, because PsyCross still parses the primitives before `DrawOTag` returns.
  3. **Cheaper pixels and calls:** an opaque draw path without `discard` where a triangle's texels have no transparent ones (G1), shader trims (G2), and on the CPU side XA reads off the game thread (C1) and GL state caching and batching (C2).

## Done since: the render thread, the XA read-ahead, the opaque path (D63–D66)

Implemented on 2026-11-01, all three behind `config.txt` switches that are off by default until the hardware runs in `tools/vita/fps-runs/` (they replace Phase 0 below):

| Item | Switch | What moved or changed | Decision |
|---|---|---|---|
| T1, the render thread | `render_thread=1` | every vitaGL call (start-up, shaders, `expand()` and the atlas, uploads, draws, captures, presents, swaps, the readback replay) on core 2; the game thread records commands and is pinned to core 0 (C7) | D63 |
| P1, in part | with `render_thread=1` | `interp_budget` also counts the render thread's time per picture, so 60 fps isn't tried where that thread or the GPU can't keep up | D63 |
| C1, XA reads | `xa_ahead=1` | the XA sectors read ahead in 32-sector windows on core 1 | D64 |
| G1, the opaque path | `gpu_opaque=1` | triangles whose atlas blocks have no transparent or STP texel drawn without `discard` | D65 |

**Not done, and why** (D65, D66): shader trims that change arithmetic, GL state caching and batching (C2: off the game thread now, and PsyCross's splits leave little to merge), vertex buffer reuse, vitaGL compiler settings, the in-between frame on its own core (B2), the parse thread (T2, since done: D71), the GTE dispatch (C5).

**Checked** (Vita3K is not hardware; results with log paths in D63–D65):
- Vita3K: every scripted run is byte-identical to the unchanged build with the switches on (shots, WAV, memory card): gems, pause and inventory, attract demo, 60 fps forced, disc image, save and load. So are the window captures of the GPU's own picture.
- Host: the host checks, widescreen demos and oracle replays are unchanged.
- Vita3K's own GPU emulation failed in 3 of ~45 runs, with and without the thread. With the thread it left the render thread blocked (D63).
- Vita3K's timings aren't the Vita's. The only firm relative number is the game thread's `raster`, which went from 0.6 to 0.0 ms per vblank.

The threads and cores with everything on are in D66's table.

**After the first hardware run (C1, D68):** the game thread fell from 93% busy to 23–53% at 60 fps, and Artisans held 60 in most ticks. A heavier level's GPU scene took 19–31 ms, so 60 doesn't fit there, and the budget flipped between 60 and 30 tick by tick (the stutter). Since D68:
- P1's hysteresis is in: after a miss, 30 fps is held until the costs fit with an eighth to spare for a whole second, longer after repeated misses. Late pictures while the in-between frame runs count as a miss too.
- `render_thread`, `xa_ahead` and `gpu_opaque` are on by default (`config_version=3` makes old files' `0`s give way), and `gpu_time` is 0 (C4).
- What's left for a locked 60 in the heavy levels is GPU work (G2, G3, or a lower render scale at 60 fps), now measured by S in C1's windows.

**Since D69 (G3):** the presentation passes are down to one full-screen scene per picture (the present), from two. The capture no longer copies when the picture is one target whole (always, in Spyro's play), and the back buffer isn't cleared under a 16:9 picture. That's about half of C1's 2.5–3.7 ms GPU tail per picture (*estimate*), so ~1.3–1.8 ms per tick at 30 fps and twice that at 60. S, the game's own scene (18.9–25.8 ms in C1's heavy windows), is untouched. D69 also found that a target remade after a save state or an option change can land in main RAM on main (vitaGL frees CDRAM late), and repairs that at the target's next clear. Hardware runs E1–E4 in `tools/vita/fps-runs/` measure it.

## The in-between frame: why it stays a full draw (D70)

The four parallel sessions after D68 (cheaper in-between frames, fewer full-screen GPU passes, the spare cores, hand C for the hot renderers) took the next items. For the in-between frame, the idea was to reuse the real frame's geometry and apply only the new camera. It doesn't fit the PS1's data flow, and it wouldn't touch the GPU's share:

| Per in-between frame (C1, Artisans) | Cost | Reusable? |
|---|---|---|
| Game thread: mobys, environment, OT walk, snapshot | 3.3 + 2.1 + 2.6 + 0.5 ms | Mobys and Spyro move and animate. The environment reaches PsyCross already projected to 2D, depth-sorted and culled at the screen's edges, so it can't be reprojected without gaps at the edges and silhouettes |
| Render thread | ~10 ms a picture (expand 0.75, upload 0.43) | the vertices are new in every frame |
| GPU | the scene again: 5–13 ms (Artisans), 19–31 ms (heavier levels) | the same triangles whatever reuses them |

What D70 adds instead, behind `gpu_lean=1` (off by default until `tools/vita/fps-runs/f1`–`f3` run on a Vita): a 28-byte vertex instead of 40 for every picture (upload −30%), and one atlas lookup per quad instead of two. The same pictures. Skipping the in-between frame where nothing moved was tried and dropped: the game draws after waiting for the vblank, so a skip holds the previous frame one vblank longer instead of showing the same picture (D70).

It doesn't make the in-between frame "much cheaper". The big CPU wins for it are the other sessions' (hand C for `r_moby`/`r_environment`, the OT walk on core 3). Where the GPU is the limit, only fewer pixels (G4, dynamic resolution, deferred) or fewer full-screen passes help.

## Done since: the parse thread, and core 3 (D71)

T2 is in, behind `parse_thread=1` (off until the hardware runs H1–H3 in `tools/vita/fps-runs/`):
- **What moved:** PsyCross's OT walk, the splits and the vertex copy into the render thread's slabs. In C1's busy 60 fps windows that was 1.8–2.6 ms per vblank of the game thread's 7.0–9.7 (21–27%). At 30 fps in D68's run it was 0.5–1.7 of 3.4–7.7.
- **Where:** a thread on core 1, beside the mixer and the XA reader, walking a copy of the frame's primitives and the OT terminator in a second 16 MB-aligned arena. That's 45 KB per list on the host with widescreen and 60 fps, 98 KB at 4:3.
- **Overlap:** the game thread joins at its next PsyCross call or `VSync`: GamestateDraw's `DrawSync(0)`, after the next pass's render path. `present.c`'s capture, commit and snapshot of the draw environment are deferred to the join, not joined for.
- **What it should buy** (*estimate*): `ot` falls to the copy (~0.1–0.2 ms per list) plus any wait at the join. In C1's busy windows that's −1.5 to −2.3 ms per vblank of game-thread time. It doesn't touch the GPU's limit in the heavy levels (C1, D68).
- **Core 3:** VitaSDK's `SCE_KERNEL_CPU_MASK_SYSTEM` is the system's core. Homebrew reports say apps don't get it without a kernel plugin. Every start now logs a `cores:` line from a 50 ms probe there. `parse_core=3` uses it only if that probe ran on CPU 3 with at least half the time.

Updated threads and cores, with everything on (D66's table plus D71):

| Thread | Core | Priority | Runs |
|---|---|---|---|
| Game (main) | 0 | 0xA0 | logic, the translated renderers, the copy of the primitives, recording the GL commands outside the walk, the present decision, the CD model |
| Render (`spyro_render`, D63) | 2 | 0x80 | every vitaGL call |
| Parse (`spyro_parse`, D71, `parse_thread=1`) | 1 (3 with `parse_core=3`, if usable) | 0x98 | PsyCross's OT walk from the copy, and the walk's GL commands |
| Mixer (`spyro_mixer`, D37) | 1 | 0x70 | SPU mix and XA decode |
| XA reader (`spyro_xa_ahead`, D64) | 1 | 0x90 | XA sector reads |

## Where a frame's time goes (measured)

### Threads and cores

| Thread | Core | Priority | Runs |
|---|---|---|---|
| Game (main) | any of user cores 0–2 (affinity `0x70000`, logged at start) | 0xA0 | everything below except the mix: logic, the renderers, PsyCross's OT walk, all GL calls, present, the CD model and XA sector reads |
| Mixer (`mixer.c`, D37) | user core 1 (pinned) | 0x70 | SPU mix and XA decode, 0.79–0.82 ms per vblank, worst 1.25 ms: core 1 is ~95% idle |
| GPU (SGX543MP4+, vitaGL) | — | — | the target scene (the game's picture), the capture copy into a presentation slot, the present to the display |

Core 2 does nothing, and core 1 is nearly idle.

### One tick on the game thread (heavy scene)

Main `26f3138`, 16:9 native, `tex_cache=1`, 30 fps, free play after the bench (`logs/hw-26f3138-bench/spyro.log`, the 20 windows from 12600 to 18300; their numbers vary by less than 3%). A tick is two vblanks, 33.3 ms. The `time` lines are per vblank, so the tick's figures are twice those.

| Part | ms per tick | Source (log columns; code) | Waits on |
|---|---|---|---|
| Game logic (`GamestateUpdate`) | 0.86 | `draw` update 0.42–0.44 | — |
| Render path: env 4.96, mobys 3.06, spyro 0.60, sky 0.90, fx + particles 0.16 | 9.7 | `draw` env/mobys/spyro/sky/fx/particles (`prof_draw.c`): the translated renderers, about half of it the GTE model (host gprof, `docs/presentation.md`) | — |
| Rest of "game" (loop, pad, VSync callback, profiling) | 0.8 | `time` game 5.63–5.75 minus the above | — |
| OT walk | 6.8 | `time` ot 3.40–3.43. It holds PsyCross's parse (`ParsePrimitivesLinkedList`, `AddSplit`, `MakeVertex*`), **and** `GR_UpdateVertexBuffer` (unwrapped, so charged here): the vertex copy, the collapse loop, `expand()`'s atlas lookups (`atlas` line: 297–300 ms per 300 vblanks, so **2.0 ms per tick**, about 1,770 textured triangles a frame) and the `glBufferData` | — |
| Raster (GPU submission) | 9.6 | `time` raster 4.77–4.82: `gl_GR_DrawTriangles` per split, ~178 a frame (26,049–26,786 draws per ~148 pictures) and ~3,110 triangles; each sets viewport, scissor, program, uniforms, 3–7 attribute pointers and blend state, copies its vertices into the target's replay log (`log_add`), then one `glDrawArrays` or more | vitaGL's command buffers (unmeasured: back-pressure from a busy GPU would show here) |
| CD and SPU models | 3.3 | `time` spu 1.65–1.67: `psx_cd_vblank` (XA sectors read from the file, one `fseek`+`fread` of 2,336 bytes per sector on the extracted-files path, `libcd.c` `cdio_read_at`), `psx_spu_step`, the mixer packet | **file I/O**: likely most of it (*estimate*; see C1) |
| Present | 0.2 | `time` present 0.09 | — |
| Wait | 2.0 | `time` wait 0.93–1.07: `vglSwapBuffers` and the vblank wait (`vblank.c`) | display, GPU |
| **Total** | **33.3** | busy 93–94% | |

`present` confirms it: "logic draw" 26.6–26.8 ms per tick, which is render path + OT + raster (9.7 + 6.8 + 9.6 = 26.1).

**Normal play** (user, main `4f26bc7`, `logs/hw-4f26bc7-user/spyro.log`): busy up to 88%. Per vblank: ot up to 3.4 ms, raster up to 4.9, spu up to 1.65. GPU tail median 12.3 ms, maximum 25.8 (56 samples).

### The GPU (inferred from the timeline; Phase 0 measures it)

The order of one frame N on the game thread, from `draw.c` `GamestateDraw` and `vblank.c`:

```
DrawOTag(N): parse + expand [6.8]  -> per-split GL draws [9.6] -> capture (ends the target scene)
Update(N+1) [0.9] -> render path(N+1) [9.7] -> VSync wait -> present(N): capture slot -> screen, swap
```

The timed frame (`gpu_gl.c` `sample_begin`/`swap`) starts at the first GL draw and ends when `glFinish` after the swap returns. Its CPU part is raster + update + render path ≈ 9.6 + 0.9 + 9.7 + 0.8 ≈ 21 ms, which matches the measured frame minus tail (42.5–46.0 minus 17.7–27.0 ms ≈ 19–25 ms). A tile-based GPU only starts a scene's pixels when the scene ends, here at the capture, ~9.6 ms in. From there to the end of the timed frame is 33–36 ms, capture and present included. The GPU can't need more than the 33 ms tick, or 30 fps wouldn't hold, so: **roughly 25–33 ms of GPU work per frame** in that scene (*estimate*). It assumes two things. First, vitaGL ends the target scene at the capture's framebuffer switch, which is what its `glBindFramebuffer` + `glClear` do. Second, the GPU doesn't sit idle between the capture and the present in that span.

That's consistent with what the logs show at 30 fps: with ~30 ms of GPU work a frame, the GPU barely keeps up (148 pictures per 300 vblanks, 10–15 vblanks late per window). And it would explain why the in-between frame rarely fit even when the CPU budget allowed it.

- **Resolution:** at 16:9 in the bench scenes, `res=ps1` (512×224, 4.3 times fewer pixels) had a tail of 1.5 ms against 13.9 ms at native (D41's bench table). The cost scales with pixels: the fragment shader (`k_fs_atlas_body`: a dependent atlas fetch, two `discard`s, about 15 float ops) times overdraw.
- **For 60 fps** a tick holds two target scenes, two captures and two presents: about 2 × (F + P) ≤ 30 ms, so F ≲ 13 ms, against 25–33 (*estimate*).

### The in-between frame's own cost

`logs/hw-26f3138-bench/spyro.log` after 60 fps was switched on (vblank 27770). The pass time per tick (`passes`) divided by the in-between frames in the window gives the cost per frame:

| Window | In-between frames | Pass per in-between frame | Snapshot | Blend |
|---|---|---|---|---|
| 27900 | 60 of 146 ticks | 10.3 ms | 0.40 ms | 0.08 ms per tick |
| 28800 | 59 of 149 | 10.2 ms | 0.39 ms | 0.13 ms per tick |

These are the lighter ticks, the only ones the budget let through: logic draw 13.5–16.4 ms there. A pass is the whole draw again (render path + OT + raster), so in a heavy tick it's ~26 ms (*estimate*). The snapshot and blend are small (`present.c` region snapshot; D30).

### Already present but unused, or not yet run on hardware

- **`bench=2`** keeps `interp=`, but has never been run on hardware with 60 fps on. With `interp_budget=0` ("NEVER SKIP") it measures the true cost of two frames per tick for each of the 18 option sets (Run B).
- **The `cd:` line** (D60, main): reads, KB and ms in the file system per window. It tests C1's premise directly.
- **`res=ps1`**: the same CPU as native, much less GPU (D41). A candidate "60 fps performance" preset if the GPU is the limit.
- **`widescreen=0`**: ~25% less geometry. 60 fps at 4:3 was already borderline (`docs/presentation.md`).
- **The vcache side channel** (D39) is not a speed lever: when on it costs CPU (`persp`/`subpixel` ~6 ms per timed frame, D41). It stays off.
- **Core 1** runs only the mixer: it can host another thread.

## Measurement added in this PR (measurement only)

Neither changes what the game or the picture does; the default log only gains the `clocks` line.

- **`clocks: ARM … MHz, bus …, GPU …, GPU crossbar …`** at start (`main.c`, `scePowerGet*ClockFrequency`): confirms the clocks `main.c` asks for are the ones the Vita gives.
- **`gpu_time=2`** (`gpu_gl.c`, off by default). On the frame already timed once per 300 vblanks, it also runs a `glFinish` at the capture, right after the frame's last draw. Two more lines per window:
  - `gpu N: gpu_time=2: the timed frame's scene took the GPU S ms after its last draw`: S is the GPU's own time for the game's picture (F above), apart from the CPU. The `tail` on that window's timed-frame line is then the capture and present passes (P).
  - `gpu N: gpu_time=2: G GL draw calls for D game draws; vertex uploads K KB in T ms`: how many `glDrawArrays` a game draw turns into (`draw()` splits by atlas run), and what `glBufferData` costs.
  - The timed frame is held up by one more `glFinish` once per 300 vblanks. Don't combine it with `bench=`, whose frame times would then include it.

## Candidates

Gains are for the heavy scene above, per frame unless it says per tick (a tick at 60 fps holds two frames). "Faithfulness" means the game's logic and the PS1-exact pictures (replay shots, `s_vram`); "picture" means what the GPU shows.

| # | Change | Expected gain | Confidence | Effort | Risk | Helps |
|---|---|---|---|---|---|---|
| C1 | XA/CD reads off the game thread: a reader thread with read-ahead (or the image path's 32-sector windows for the extracted files too) | −2.5 to −3 ms per tick of game-thread wall time | medium (premise checked by the `cd:` line) | small | none to the logic: the same bytes, the same sectors, decided on the game thread | both |
| C2 | GL state caching and draw batching in `gpu_gl.c`: issue only changed state; merge consecutive splits with identical GL state and contiguous vertices into one `glDrawArrays` | −2 to −4 ms of the 9.6 ms raster | medium | small–medium | none: draw order unchanged, pixel-identical | both |
| C3 | Leaner expand: remember the last atlas lookup (a quad's two triangles share it), a 24–28-byte vertex instead of 40 when `persp`/`subpixel` are off, write straight into the buffer vitaGL uploads | −1 to −1.5 ms (expand is 2.0) | medium | small | none (pixel-identical) | both |
| C4 | Release defaults that don't change the picture: `gpu_time=0` (no `glFinish` stall every 5 s, which at 60 fps is a visible hitch) | removes one stall per 300 vblanks | high | trivial | none | 60 |
| C7 | Pin the game thread to core 0 (its mask is `0x70000` today, so it can land on core 1 under the higher-priority mixer), as the EDuke32 port does | small, mainly fewer worst-case vblanks | low | trivial | none | both |
| T1 | **Render thread**: every `GR_*` call (gpu_gl.c's GL work, `expand`, uploads, captures, present, swap) becomes a command for a GL thread on core 2. `DrawOTag` returns after PsyCross's parse; `StoreImage`/readbacks, shots and the menu sync | −12 ms per frame off the game thread (raster 9.6 + expand and upload ~2.6) | medium–high | medium–large | logic none (the game never reads the GPU's results except through `StoreImage`, which syncs); threading bugs; vitaGL must be used from that one thread only, `vglInit` included | both |
| G1 | **Opaque fast path**: a triangle whose UV box has no transparent texel (known on the CPU from `s_vram` when its atlas block is decoded) is drawn with a shader without `discard`. A `discard` shader makes every textured triangle's pixels get shaded; without it, the GPU's deferred hidden-surface removal may skip pixels a later opaque triangle covers. Whether that works with the depth test off (as here) is what a probe has to show first | 0–40% of F (est. 0–12 ms) | low (has to be measured first) | medium | none: same pixels by construction, since the box clamp keeps every fetch inside the box | both |
| G2 | Shader trims: the per-triangle floors (`v_box`, `v_off`) in the vertex shader; per-blend programs (opaque draws drop the STP `mix` and the second `discard`); `mediump` where exact (colour) | 10–25% of F | medium–low | small | none if pixel-identical (checked by window captures and `shot_hd=1`) | both |
| G3 | One fewer full-screen pass: render the 30 fps frame without the staging copy (the capture is needed only for queued frames), or present from the targets. **Done in D69** (`gpu_direct=1`, default): pictures shown from their target, the in-between one taking the target's texture at the next whole clear, no clear under a 16:9 picture; at 30 and 60 fps. Measured by runs E1–E4 | −1 to −2 ms of GPU | medium–low | small–medium | none | both |
| P1 | Pacing: hysteresis instead of per-tick skipping (60 fps for at least ~1 s once it fits, back to a steady 30 after a few misses); a GPU-aware budget (from S or the swap wait); triple buffering at 60 fps | no ms; removes the 60/30 flicker D44 saw | high | small | none (presentation only) | 60 |
| T2 | **Parse thread**: PsyCross's OT walk on core 1, pipelined with the next tick's render path. With any presentation option on, a draw spans both primitive buffers (D54), so the walk reads a copy: the frame's used primitive span and its OTs copied (~80–110 KB) into a second 16 MB-aligned arena at the same offsets, walked with a per-walk base (`nextPrim`, PsyCross patch 0001) | −4 to −5 ms per frame off the game thread; adds the copy, ~0.1–0.2 ms | medium | large | the 2D squeeze (D30) and the vcache table (D39) must be per frame; logic none | 60 mostly |
| C5 | GTE: an inline dispatcher for constant opcodes (`GTE_operator(0x…)` from the translated renderers: 73 call sites in `r_moby`, 44 in `r_environment`, 66 in `collision`) calling functions specialised for `sf`/`lm`, with no runtime switch | −1.5 to −2.5 ms per tick | medium–low | medium | none if bit-exact: `tests/gte`'s 9,314 PCSX-Redux vectors, the oracle traces, byte-identical shots | both |
| C6 | Compiler: `-O3` and `-marm` tried per hot library (`psycross`, `spyro_game_m2c`, `gpu_gl.c`), LTO for PsyCross + the platform layer | 0–10% of their time | low | trivial to try | UB in the decomp's C exposed (already seen at `-O2`: `-fno-aggressive-loop-optimizations`); verified by Vita3K replays being byte-identical | both |
| G4 | 60 fps at a lower render scale (e.g. ~0.75×, 720×408, or `res=ps1`) when the GPU can't keep up | up to ~50% of F | high | small–medium | **changes the picture** in 60 fps mode (the user's call) | 60 |
| B1 | Hand C for the hot renderers (`r_environment`, `r_moby`, translated asm today, every access through `mips2c_kuseg`) | −30 to −50% of the 9.7 ms render path (*estimate*) | low | large | each function oracle-checked (D33 method) | both |
| B2 | The in-between frame on its own core, in parallel with the next logic tick | up to −10 to −26 ms per tick | — | very large | not recommended: see below | 60 |

**Not recommended:**
- **Overclock:** already at the maximum (above). Our EDuke32 port sets the same four values at start (`duke-nukem-advance-tc-vita/source/build/src/vitabits.cpp` `vita_preinit`). That port also pins its game thread to core 0 and presents frames from core 1, a precedent for T1. Battery and heat: none measured. After T1 the 30 fps game thread would fit at 333 MHz (~16 ms × 444/333 ≈ 22 ms of a 33 ms tick), so a **battery-saver option** becomes possible (open question 4).
- **`-ffast-math`:** the game's logic is integer, and the only float code (`gpu_gl.c`'s vertex set-up, `present.c` blending) is a small cost. No gain worth a determinism question.
- **PGO:** it needs a gcov runtime writing to `ux0:` on the Vita: tooling cost for a few percent.
- **NEON GTE:** the GTE's per-op flags and division serialise it. Try the scalar specialisation (C5) first.
- **B2:** the translated renderers address all memory through one `g_psx_arena_base` (held in a register, `mips2c_rt.h`). They also keep state in the global `gteRegs`, `g_mips_v1`, the emulated stack, the scratchpad at the arena base and PsyCross's draw state. A second, concurrent render needs a second copy of all of that and either per-thread bases or a second build of ~44,000 lines of translated code. That's on top of copying the state it reads every tick. T1 and T2 already put the in-between frame's parse and submission on other cores; only its render path (~9.7 ms) stays on the game thread.

### Notes on the bigger items

**T1, the render thread.** What crosses threads is our own `GR_*` interface (`gpu.c` dispatches it, `gpu_gl.c` implements it), so PsyCross needs no change:
- `GR_UpdateVertexBuffer` copies PsyCross's vertex buffer anyway (`s_vbuf`); double-buffer that copy.
- Split state (`SetupClipMode`, `SetTexture`, `SetBlendMode`) goes into the command with each `DrawTriangles`.
- `GR_CopyVRAM`/`GR_ClearVRAM` become ordered commands. `s_vram` belongs to the GL thread; LoadImage's source is copied into the command.
- `GR_ReadVRAM` (StoreImage: the pause screen's grayscale copy, shots, save states' `vita_gpu_vram_take`) flushes and waits.
- Presentation captures (`pres_capture`/`pres_commit`) and `vita_gpu_present` are commands too. The game thread's vblank wait stays where it is, and waits for the GL thread if it's more than one frame behind.

Latency stays as it is today: frame N is shown at the VSync after the render path of N+1, and the GL thread has the ~11 ms of Update + render path in between to finish it.

Determinism: the logic gets nothing new from the GL thread. Scripted runs (`realtime=0`) compare as now (Vita3K replay shots byte-identical to the host's). In real time the game is late less often, so `g_DeltaTime` 3 happens less: closer to the PS1, which held 30 in most places.

**Budget after each phase, heavy scene, 60 fps (two frames a tick).** All *estimates*, from the measured parts above and the gains in the table.

| | Game thread per tick | GL thread per tick | GPU per tick (2 × (F + P)) |
|---|---|---|---|
| Today (one thread) | ≈ 58 ms: 0.9 + 0.8 + 3.3 + 2 × (9.7 + 6.8 + 9.6) + 0.8 | — | ≈ 55–70 ms |
| After Phase 1 (C1–C4, C6) | ≈ 48 ms | — | unchanged |
| After T1 + G2/G3 | ≈ 30.6 ms: 0.9 + 0.8 + 0.3 + 2 × (9.7 + 4.3 parse) + 0.6. Borderline | ≈ 17 ms | ≈ 45–60 ms, or less with G1 |
| + T2 (parse on core 1) and C5 | ≈ 18–20 ms | ≈ 17 ms; parse thread ≈ 9 ms + mixer 1.6 | needs G1 or G4 in heavy scenes |

At 30 fps after T1, the heavy scene's game thread is ≈ 16 ms of 33 (≈ 50% busy): room for draw distance or texture filtering at 30.

## Roadmap

### Phase 0: measure (this PR, then three hardware runs)

**Superseded:** the render thread, the XA read-ahead and the opaque path are in (D63–D65), so the runs now compare them on and off: `tools/vita/fps-runs/` (A1–A3, B1–B2, C1–C2, with a README of what each line shows). Runs A, B and C below are those runs' A1, B1 and C2.

Build: the VPK from this PR (CI artifact `spyro-vita-vpk`), or `logs/vpk-fps-plan-measure/spyro-vita.vpk` in the main checkout (built from this branch, SHA-1 `79b1946b…`; not yet run in Vita3K or on hardware). Send back `ux0:data/spyro/spyro.log` from each run, renamed.

**Run A, GPU time per frame at 30 fps.** `config.txt`:
```
gpu_time=2
```
Play 3–5 minutes: Artisans (around the castle and the hedges), then one busy level. It shows:
- the `clocks` line;
- per window, S (the GPU's time for the picture) and the tail (capture + present);
- GL draw calls per game draw, and the vertex upload cost;
- the `cd:` lines: file-system ms per window.

Decision:

| S at 16:9 native, heavy spots | Meaning | Next |
|---|---|---|
| ≤ 12 ms | the GPU has room for 60 fps; the inference above was pessimistic | CPU work only (Phases 1–3) |
| 12–16 ms | close | G2 and G3 with Phase 2 |
| > 16 ms | GPU-bound for 60 fps | G1 measured first, and G4 as the fallback (open question 2) |

`cd:` above ~300 ms per window (1 ms per vblank) confirms C1.

**Run B, the bench with 60 fps forced.** `config.txt`:
```
bench=2
interp=1
config_version=2
interp_budget=0
```
Leave it untouched from boot for ~3.5 minutes; it cycles its 18 option sets through the intro, title and attract demos, then restores the options. The `bench final:` lines give pictures/s (60 at most) per set, now with an in-between frame every tick:
- 16:9 `ps1` against 16:9 native: the same CPU, 4.3 times fewer pixels. The difference is the GPU's share.
- 4:3 native against 16:9 native: the geometry's share.

Without the budget, slow ticks make the demos run late (the game catches up on missed vblanks, D28). The sets still compare with each other.

**Run C (optional), 60 fps in play.** `config.txt`:
```
interp=1
config_version=2
interp_budget=0
gpu_time=2
```
Two minutes in Artisans. The `present` lines (in-between frames per tick), late vblanks and S give the real overrun with both frames.

### Phase 1: quick wins (one or two sessions)

C1, C2, C3, C4, C7 and P1's hysteresis. C6 is tried by building and benchmarking, and kept only where Run A/B numbers improve.

- **Target:** heavy-scene busy at 30 fps from 93% to ≤ 80%; the in-between frame fits more often in normal play.
- **Verify:**
  - Vita3K `realtime=0`: replay shots and memory card byte-identical to the host (gems script, attract demo).
  - Window captures pixel-identical to main.
  - Hardware: Run A and Run B again, and compare raster, ot, spu and busy.

### Phase 2: the render thread and the GPU (the main step)

T1, then G2, G3 and (if Run A says so) G1.

- **Target:**
  - at 30 fps, game thread ≤ 60% busy in heavy scenes;
  - at 60 fps, the in-between frame in ≥ 90% of ticks in normal play (Artisans, Peace Keepers' worlds);
  - a steady 30 where it doesn't fit.
- **Verify:**
  - the Phase 1 checks;
  - a `mixer`-style mismatch counter for the command queue (commands posted = commands run, 0 errors);
  - the pause/inventory screens (`StoreImage` sync) and save states with the thread on;
  - hardware Runs A–C.

### Phase 3: the rest of 60 fps

T2 and C5; G4 if the user wants it. B1 only if the numbers still fall short.

- **Target:** 60 fps in ≥ 95% of ticks over a 10-minute session across home worlds. That's the bar for revisiting D44's "off by default".
- **Verify:**
  - the Phase 2 checks;
  - `tests/gte` and the oracle traces for C5;
  - the whole-game tour on the host for anything that touches the walk (T2's copy and squeeze).

## Open questions for the user

1. **Latency:** if T2's pipelining ever needs it, is one more 60 Hz frame (16.7 ms) of display latency acceptable in 60 fps mode? T1 adds none.
2. **Resolution in 60 fps mode:** if the GPU is the limit (Run A), is a lower render scale acceptable for 60 fps only (G4: fixed ~0.75×, or dynamic)? Or should 60 fps stay "where it fits" at native?
3. **When should 60 fps be on by default?** The proposal: the Phase 3 target (≥ 95% of ticks over 10 minutes, no audible or visible regressions).
4. **Battery:** clocks are at the maximum today. Is a battery-saver option (333 MHz ARM, lower GPU clock) wanted once T1 makes 30 fps fit?
5. **Order:** is 30 fps headroom (for draw distance and filtering) or 60 fps the priority? It changes whether G1–G3 come before T2.
6. Can you do Runs A and B (and C) on the Vita with this PR's VPK?

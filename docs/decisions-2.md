# Decisions, part 2

Decisions from D103 on; D1–D102 are in [decisions.md](decisions.md). Record each decision with its date, what was chosen, and why.

## 2026-11-05 — hardware runs

### D103. Hardware runs use vita-port-kit's scripts; this game's details are in `tools/vita/hw.game`
- **Why and what:** see `docs/vita-testing.md` "Hardware runs from the Mac". The scripts are vendored in `third_party/vita-port-kit`, and `tools/vita/hw_*` wrap them. The lock moved to `~/.cache/vita-port-kit` and is shared with kh1-decomp. Checked against the mock (2026-11-05); not run on hardware yet.

## 2026-11-05 — direct vertices, the walk's box as SIMD

### D105. The walk's UV box and skip test as ARMv6 SIMD lanes: `nd_direct=1` is now a net gain, with identical pictures; the clears are three quarters of what is left of the PS1 GPU → vitaGL layer
- **Why:** D99's `nd_direct=1` moved the lean vertex's UV box from the render thread (`expand_lean`) into the walk (`em_prim`), but the box cost the same there. On hardware (Y1–Y4) the GL side's vertex buffers fell 1.11 → 0.65 ms per vblank and the walk grew by as much (0.91 → 1.35). At 60 fps the parse thread took +0.72 ms per vblank and the game thread waited +0.33 ms longer for it. Compiled for the Vita, D99's rule runs about 130 instructions a triangle, in loops: the skip test's `(k + 1) % 3` became a multiply, and each of the box's four bounds was its own compare and select.
- **Decided** (`src/platform/common/psx_lean.h`, `src/game/native_draw.c`'s `em_direct`):
  - **The skip test on whole words.** An edge is too long when `qsub16(a, b)` (x and y differences, saturated) plus `1023 | 511 << 16` (`uadd16`) is above `2046 | 1022 << 16` in either lane (`uqsub16` nonzero). The saturation keeps a difference past 16 bits past the limit, so the test is exactly `lean_skipped`'s for every input, including the wraps (32767 against −32768).
  - **The box as byte minima.** Each vertex's u/v word becomes `u | v << 8 | (255 − u) << 16 | (255 − v) << 24` once (`lean_uvw`). The byte-wise minimum of three of them (`usub8` + `sel`, two instructions a pair) is the box with its high half inverted, so a 16-bit `~` turns it into `u0 | v0 << 8 | u1 << 16 | v1 << 24`. The tile snap (D39) is a mask test on that word (`lean_box_of`).
  - **A quad's two triangles share their work.** TriangulateQuad's triangles are prim vertices 0 1 2 and 1 2 3: the shared edge's test and the two shared vertices' minimum are made once. Untextured polygons' u/v are 0, so their box is 0 with no work. About 100 straight-line instructions for both of a quad's triangles, against about 260 for D99's loops.
  - **Off ARM** (the host, i686) the same lane operations are written in C (`LEAN_SIMD_C` forces them on ARM too). So the host's `--nd-direct-check`, which compares every packet's triangles with D99's rule (`lean_direct_tri`), runs this form against the rule. NEON isn't used: the work is a few words per triangle in core registers, and moving between NEON and core registers costs more than it saves on the Cortex-A9.
  - **The rule stays** as `lean_skipped`, `lean_box` and `lean_direct_tri`, for the renderer's conversion (`lean_convert`) and the checks. The Vita's `nd_direct=2` check now makes its copy with the lane form (`lean_box_lanes`), so on the device it compares the real SIMD instructions with `expand_lean`'s rule.
  - **`gpu_parts=1` gains a second line:** the whole time of each entry point the "PS1 GPU → vitaGL" layer is (`GR_DrawTriangles` and the batches' GL calls inside it, `GR_ClearVRAM`, `GR_CopyVRAM`, `GR_ReadVRAM`; prof.c's `raster` on the game thread).
- **Checked:**
  - **`tests/lean`** (ctest `lean`, `lean_mutant`): the lane form against the rule, for a triangle and for a quad's second triangle, over 4.2 M random quads (x/y over the whole 16 bits, near the 1024/512 limits, and small; u/v anywhere, inside one tile, on the snap's spans 13–17 and 30–32, 0 and 255; random brightness and dither bits) and the edge cases (degenerate triangles, every pair of 15 coordinates on one edge, including ±1023/1024, ±511/512 and the 16-bit wraps, every four of 12 u/v words, 256 one-point quads): 8,432,392 triangles, **0 differ**. The mutant (`qsub16` without its saturation) fails.
  - **The ARM instructions themselves** (`tests/lean/arm_check.sh`; not in CI, since this session can't change the workflow file: a step running it after the host tests is the owner's to add): the test built with the Vita's compiler (vitasdk GCC 15.2, `-O2`, Cortex-A9 Thumb-2) with `usub8`/`sel`/`qsub16`/`uadd16`/`uqsub16`, run under qemu-arm: 33,598,216 triangles, **0 differ**. Two hand-made mutants are caught: `sel`'s operands swapped (28.6 M differ) and `ssub16` for `qsub16` (7,171 differ, all wraps). In the game's ELF, `em_prim` has the 21 SIMD instructions inlined.
  - **Every packet both ways** (`MODE=dir tools/native_draw_ab.sh d105`, the per-packet check against D99's rule): the demos 2,002,208 packets, gems 1,466,746, `wsi` 4,234,736, `lod` 1,513,317, route A 2,386,082, route B 2,289,935, **0 differ**, with the same triangle counts as D99; files 26/26, 19/19, 25/25, 19/19, 55/55, 56/56 identical.
  - **`tools/host_checks.sh`** against a build of main `37a50f0` (`build/base-src`): **279/279 files byte-identical** (demo 26, the whole tour 141, routes A and B 56 each). ctest 17 of 17 (`gte` skipped, as on main).
  - **Oracle replay** (`build/linux32-ref`, D99's four capture sets; `logs/d105-replay`): cap3 329 PASS and 9 NOEXIT, cap5-fairy 261 PASS and 7 NOEXIT, the labels run 1,850 PASS and 2 NOEXIT, the tour 1,840 PASS, 1 NOEXIT and 17 CRASH. The crashes are all in the credits overlay's `func_credits_8007BFD0`, out of time under the Mac's load as in D99; its 18 samples pass with `SPYRO_REPLAY_TIMEOUT=600`. **0 FAIL.** No game function changed.
  - **Vita3K** (VPK `logs/d105-vpk-1.vpk`, SHA-1 `751098f5…`; `nd_direct` 0 against 1 from that one VPK; logs `logs/d105-v3k-*`):

    | Run | Shots | Window captures | Card |
    |---|---:|---:|---|
    | gems script (8,768 vblanks) | 17/17 | 17/17 (see below) | identical |
    | gems, every layer on the game thread (`render_thread=0 parse_thread=0`) | 17/17 | 17/17 | identical |
    | gems with `tex_filter=1` | 17/17 | 17/17 | identical |
    | attract demos (12,000 vblanks) | 12/12 | 11/11 | — |
    | gems in 16:9, 60 fps forced (`interp=1 interp_budget=0`, to 6,600, `gpu_time=2`) | 13/13 | 13/13 | identical |
    | the pause route (16:9, 60 fps forced) with a save state at 7,000 and its load at 7,200 | 76/76 | — | identical |

    - In the gems run `win-3500` differed (164 pixels in a band near the bottom), the capture D92 and D99 saw change between runs of one build. Three reruns to 3,600 vblanks, two with `nd_direct=0` and one with `=1`, gave the same `win-3500` as the first `=0` run.
    - **`nd_direct=2`**, the device check with the lane form: gems and gems with `tex_filter=1`, each 4,516 vertex buffers, 21,949,809 vertices, **0 differ**.
- **Hardware** (2026-11-05, the owner's Vita over `hw_run.sh`, approved by the owner in this session's chat; VPK `logs/d105-vpk-1.vpk`, SHA-1 `751098f5…`, built from this PR's code before one host-only C expression was parenthesised; logs `logs/hw-y5`…`y8`, gitignored). The runs used the runner from before D103 (its lock in `~/.cache/spyro-vita`); kh1-decomp's last run had ended at 12:44 and no lock was held. All four reached `frames=`, with no crash dump, and the owner's files were put back each time. Afterwards main's eboot (`987363e`, CI run 42042300815, `139c6b26…`) went back on the Vita and read back identical. The app's `sce_sys` is the same as main's VPK's.
  - **The same pictures:** Y5 (`nd_direct=0`), Y6 (`=1`) and D99's Y1: all 18 pictures (9 shots and the GPU's own frame at each) identical, and Y5's and Y6's memory cards identical.
  - **The K route, every layer on the game thread** (X4's switches; `native_share.py`, ms per vblank, means of the route's 50 windows without the last one, where the app quits: Y6's has a 22 ms `present` there):

    | | OT walk | PS1 GPU → vitaGL | sum | of the OT walk: the walk itself |
    |---|---:|---:|---:|---:|
    | Y1, `nd_direct=0` (D99) | 2.08 | 3.60 | 9.84 | 0.91 |
    | Y2, `nd_direct=1` (D99's box) | 2.06 | 3.56 | 9.74 | 1.35 |
    | Y5, `nd_direct=0` | 2.11 | 3.54 | 9.81 | 0.93 |
    | Y6, `nd_direct=1` (D105's box) | 1.77 | 3.50 | 9.39 | 1.07 |

    - **The sum fell 0.42 ms per vblank (4%)**, against D99's 0.10. The walk itself is the OT walk less the GL side's vertex buffers (with `render_thread=0` they are inside it: 1.12 → 0.65 ms). It grew 0.14 ms, against D99's 0.44. The box now costs a third of what it did.
  - **Toasty at 60 fps, the shipped threads** (Y7 against Y8; the `gpu parts`, `parse`, `time` and `render` lines, means from vblank 900; `fps60_summary.py`):

    | per vblank | Y3 | Y4 (D99) | Y7 | Y8 (D105) |
    |---|---:|---:|---:|---:|
    | render thread: vertex buffers (their expand) | 2.01 ms (1.70) | 1.26 ms (0.84) | 2.09 ms (1.75) | 1.34 ms (0.87) |
    | render thread: the draws' work, logs, GL calls, in `sceGxm*Scene` | 1.91 ms | 1.83 ms | 1.83 ms | 1.75 ms |
    | parse thread busy | 1.85 ms | 2.57 ms | 1.88 ms | 2.10 ms |
    | game thread's wait for the walk | 0.98 ms | 1.31 ms | 1.01 ms | 1.11 ms |
    | game thread busy | 37.3% | 38.7% | 38.1% | 38.1% |
    | late vblanks per 300: the Stone Hill demo, Artisans, Toasty | 9.9, 0.6, 0.1 | 2.9, 0.6, 0.0 | 9.7, 0.5, 0.0 | 3.0, 0.4, 0.0 |
    | late vblanks in the load windows; falls to 30 fps | 169 in 13; 5 | 97 in 13; 4 | 195 in 15; 5 | 108 in 15; 4 |

    - **The render thread** (the wall at 60 fps, D81) does 0.83 ms per vblank less, as with D99.
    - **The parse thread** does 0.22 ms more (D99: 0.72). The game thread's busy share is unchanged and its wait for the walk is 0.10 ms longer (D99: 0.33). The box's work is on a thread with room: the parse thread is 12–13% busy.
    - **Late vblanks fell as with D99:** the Stone Hill demo 9.7 → 3.0 per 300, the loads 195 → 108. One run each, so read them as a direction.
  - **What the "PS1 GPU → vitaGL" layer is** (the new `gpu parts` line, Y5 over the route; Y6 alike):

    | prof.c's `raster`, 3.66 ms per vblank (`render_thread=0`; means from vblank 900) | ms per vblank |
    |---|---:|
    | `GR_ClearVRAM` | **2.70** |
    | `GR_DrawTriangles` | 0.85 (the batches' GL calls in it 0.36, the work before them 0.15, the logs 0.19) |
    | `GR_ReadVRAM`, `GR_CopyVRAM` | 0.01, 0.00 |

    - The parts sum to 3.56; the rest is the wrappers' own time.
    - **The clears are three quarters of the layer.** On the render thread at 60 fps they are 5.7 ms per vblank (Y7 and Y8 alike), against 1.5 for the draws. They are the walk's fill packets turned into `ClearImage` (`walk.c`). `gpu_gl.c` runs each as a per-pixel wrapped loop over the 1024×512 software VRAM, then a `glClear` per target it touches, with the batch sent first. Which half costs what isn't split yet.
    - D99's question (most of `GR_DrawTriangles`' time was in none of the parts) is answered: it wasn't in `GR_DrawTriangles`; the layer was mostly clears.
- **Proposed default: `nd_direct=1` on the Vita; the owner decides.** For: the same pictures and card everywhere checked; with every layer on one thread 0.42 ms per vblank less; at 60 fps the render thread 0.83 ms less and late vblanks fewer. Against: the parse thread does 0.22 ms more and the game thread waits 0.10 ms more at its join, both small and on threads with room. The PR leaves the default at 0; making it 1 is one line (`g_nd_direct`'s default in `native_draw.c`, plus the config comment).
- **Not done:**
  - **The clears** (next): row by row without the wrap mask where a clear doesn't wrap (a `memset`-style fill of `s_vram`), and finding what the targets' `glClear` costs. A loop that writes the same pixels gives identical VRAM, which the shots' readback replay (Vita3K, hardware) compares. One more `gpu parts` split (the VRAM loop against `clear_targets`) settles which half to take first.
  - D99's other two items (the walk writing straight into the slab; the side stream in vitaGL's scratch memory) are unchanged.

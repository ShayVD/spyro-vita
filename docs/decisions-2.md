# Decisions, part 2

Decisions from D103 on; D1–D102 are in [decisions.md](decisions.md). Record each decision with its date, what was chosen, and why.

## 2026-11-05 — hardware runs

### D103. Hardware runs use vita-port-kit's scripts; this game's details are in `tools/vita/hw.game`
- **Why and what:** see `docs/vita-testing.md` "Hardware runs from the Mac". The scripts are vendored in `third_party/vita-port-kit`, and `tools/vita/hw_*` wrap them. The lock moved to `~/.cache/vita-port-kit` and is shared with kh1-decomp. Checked against the mock (2026-11-05); not run on hardware yet.

## 2026-11-05 — stage 7's first steps

### D104. Stage 7a and 7b: the port owns `VSync` over one clock module; the host audits say the in-between frame reads little, its HUD is the real frame's, and play is the largest share of it. 7c next; 7d–7g wait for a reason other than CPU
- **7b, the port's own `VSync`** (`src/platform/common/libetc.c`, `clock.c`, `psx_clock.h`):
  - `libetc.c` replaces PsyCross's `LIBETC.C`, which `cmake/psycross.cmake` now leaves out, as it does `LIBCD.C`, `LIBSPU.C` and `LIBMCRD.C`. It has `VSync`, `VSyncCallback`, `ResetCallback`, `StopCallback`, `SetVideoMode` and `GetVideoMode`. Their semantics are `LIBETC.C`'s, with our patches' two hooks (0009's `g_psyxGpuGate` first, then 0005's `g_psyxVSyncHook`). `VSync(-1)` and `VSync(n > 0)` return the count without waiting, as before. `VSync(0)` is the platform's `psx_platform_vsync()`.
  - `clock.c` holds the vblank count and the Vita's real-time arithmetic: the base against the display's vcount, the wait, the catch-up and `MAX_CATCH_UP`'s drop. Both `vblank.c` files use it; the Vita passes its widened vcount (D90) and `sceDisplayWaitVblankStart`.
  - The callbacks' order and bursts are unchanged. So are virtual time (D17), `realtime=0` and the resync after the menu and after a save-state load.
  - `PsyX_WaitForTimestep`, `PsyX_Sys_GetVBlankCount`, `PsyX_Sys_SetVMode` and the platforms' `g_vmode` are gone. `present.c` and `savestate.c` call `psx_clock_count()` and `psx_platform_vsync()`.
  - `tests/vcount` now links `clock.c` itself, where it used to replay a copy of the wait's arithmetic. The mutant (the raw 16-bit counter) still fails.
  - `tools/native_audit.py` counts libetc as ours (E).
  - Patches 0002, 0005 and 0009 still patch `LIBETC.C`. Those hunks are dead now, left for stage 8 so no patch is renumbered under other sessions' branches.
- **7a, the host audits** (debug only, off by default; `docs/host-build.md` has the options):
  - **`--present-audit`** (with `--interp`): per gamestate, the in-between frames, their cost (qemu's CPU time and OT packets), and whether the in-between pass's HUD list is the real frame's. The check hashes each HUD packet's length and words, but not its link, at `func_80016784` (`order_table.c` calls `port_present_hud_list`).
  - **`--present-reads FILE`** (`src/platform/linux32/readset.c`, every Nth in-between pass, `--present-reads-every`):
    - The arena is `mprotect`ed for the pass, apart from the emulated stack.
    - The data pages (0x8006B000–0x8007B000) are traced word for word: each access faults, is recorded from `si_addr` and the fault's write bit, and steps one instruction with the trap flag.
    - The other pages are traced by their first access: read first, or written first.
    - What the pass read is compared with the words that changed by the next draw's start (the logic pass and the next update), which is what would run beside a presenter in 7f.
    - `tools/present_reads.py` names the words from `tools/layout/psx.map`.
  - **Not seen:** `g_psx_scratch` (the 0x1F800000 scratchpad), the GTE registers, PsyCross's state and our C statics. None of these is in the arena; 7d's context covers them.
  - **The runs:** `host_checks.sh`'s four runs (both attract demos, routes A and B, the whole tour) with `--interp`, the audit and the read trace (`tools/present_audit.sh s7a` → `logs/s7a-{demo,routeA,routeB,whole}`; the runs used no `--shot-dir`, which the script now passes). Their traces and memory cards are byte-identical to the same runs without any of it (`logs/s7br-*`), so the audits change nothing the game computes.
- **What 7a found:**
  - **Coverage:** 69,468 in-between frames over 69,681 ticks.

    | Gamestate | Ticks | In-between frames (share) | Skipped | Host µs a pass (qemu) | OT packets a pass | HUD identical |
    |---|---:|---:|---:|---:|---:|---:|
    | 0 play | 31,828 | 31,815 (45.8%) | 10 | 15,617 | 1,574 | all |
    | 15 credits | 13,177 | 13,144 (18.9%) | 33 | 23,759 | 1,514 | all |
    | 13 title | 9,811 | 9,793 (14.1%) | 17 | 9,940 | 912 | all |
    | 14 cutscenes | 6,218 | 6,195 (8.9%) | 23 | 14,244 | 1,389 | all |
    | 2 pause | 2,983 | 2,938 (4.2%) | 45 | 9,580 | 703 | all |
    | 3 inventory | 2,920 | 2,880 (4.1%) | 40 | 13,600 | 1,373 | all |
    | 1 level transition | 1,050 | 1,012 (1.5%) | 38 | 4,056 | 729 | all |
    | 8 dragon | 695 | 695 (1.0%) | 0 | 22,029 | 1,367 | 673 of 695 |
    | 12 balloonist | 684 | 682 (1.0%) | 2 | 13,586 | 1,217 | all |
    | 11 fairy | 217 | 217 (0.3%) | 0 | 27,138 | 1,345 | all |
    | 10 exit, 9 entrance | 98 | 97 | 1 | 4,000–4,800 | 314–684 | all |

    - Skips are the cuts: a gamestate, level or camera jump. Ticks less in-between frames less skips is the run's last tick, whose draw ends in `--frames`'s exit.
    - The runs over-weight the title and the opening cutscene (every one starts there) and the tour the credits. Play is 30–37% of the in-between frames in the attract demos and routes A and B, and 46% over all four. A player's session is mostly play.
    - Every gamestate's pass is the same order of cost as play's: 4–27 ms of qemu time against play's 15.6, and 314–1,574 packets. The pause screens, which 7g says may keep D30's pass "where that is cheap", cost 61% of play's time (9.6 ms, 703 packets). Keeping them on D30's pass saves code, not CPU.
  - **The HUD:** the in-between pass's HUD list is byte-identical to the real frame's in 69,446 of 69,468 frames. The exceptions are 22 frames of the dragon cutscene (gamestate 8, "Rescued …"), out of its 695. The packets that differ are the dragon rescue's ring (`func_80058864`, `particles_hand.c`): flat semi-transparent triangles (GP0 0x22, its grey brightening 0x48 → 0xB8) that are projected through the camera and put in the HUD OT. They are 1 pixel apart between the two lists because the in-between pass's camera is blended; the in-between frame's own ring is the right one (`logs/s7a-dragon`, the tour to vblank 62,310 with each differing packet's words). So the presenter can take the HUD from the logic pass of the same tick everywhere but the dragon cutscene, which keeps D30's pass (7g).
  - **The read set:** 1,024 sampled passes (`logs/s7a-reads-summary.txt`).
    - **In play:** the pass reads 73 of the game's globals, about 380 words a tick (13 KB of symbols, against the 61 KB data region today's snapshot copies in and out).
    - **What changes before the next draw** is 16 of them, about 3.9 KB:
      - every tick: `g_Spyro`, `g_CurDB`, `D_80075950` (the pacer's vblanks) and `D_80075858`;
      - some ticks: `g_LevelTicks`, `g_Sparkles`, `g_Camera`, `D_8007AA10`, `g_Hud`, `D_800770C8`, `D_800758FC`, `g_Glows`, `g_SpyroFlame`, `g_Fade`, `D_800756C0` and `D_800771C8` (the sector flags the logic draw writes).
    - **Outside the globals,** per tick:
      - the moby array: about 2.6 pages read that change, 159 bytes changed;
      - the particle space: about 1 page, 758 bytes;
      - the level data: 3 pages read that change, 150 bytes changed (D101's known case: the environment animations' texture V coordinates and the sector bytes 0x18–0x1B);
      - the OT's bucket pages, which the next draw rewrites.
    - **The title, the cutscenes and the credits** read 34–40 globals (5–7 KB), of which 1–4 change every tick.
    - So **a presenter's published snapshot is small and nameable**: the 73 read globals, or only the 16 that change, plus the moby array, the particle space and the animated level bytes. 7e can take its list from `present_reads.py` per gamestate.
- **The plan corrected:**
  - **Stage 8's list:** after 7b, `g_psyxVSyncHook` is our own `libetc.c`'s, not a PsyCross hook. So stage 8 needs only `g_psyxVramFrozen` and the `DrawOTag` hooks gone from stage 7. 7c's presentation leaving `VSync` is now entirely our code.
  - **7g:** the order comes from the counts above (credits, title, cutscenes, pause and inventory, transitions, dragon, balloonist, fairy, exits). The dragon cutscene's HUD can't be reused.
  - **7b's size** was right: ~140 new lines, ~90 fewer in the two `vblank.c` files, and the test now covers the real arithmetic. **7a's** was ~550 lines with the tool, more than its ~200: the word-by-word trace needed a fault-and-step handler, and the HUD check keeps the differing packets for its report.
- **The call on 7d–7g, sharpened:**
  - **For:** the presenter needs much less than feared. The snapshot is a few KB of named state, the HUD comes for free, and play is the largest share of the in-between frames (46% here, more in a player's session).
  - **Against, unchanged:** 60 fps already holds in play (D81, X7). The one place it misses, the attract demo's flight, is the render thread. 7d–7f is *estimate* ~3–5k lines over the renderers, the riskiest code in the port, for headroom (Toasty's game thread 39.5% → about 21.5% busy at 60 fps, *estimate*).
  - **So:** do **7c** next, after D99 (done) and with the owner's yes for its hardware pair. It makes a late render thread cost pictures, not ticks, at ~300–500 lines.
  - **7d–7g wait for a need the CPU numbers don't give.** That could be stage 8 wanting `g_psyxVramFrozen` gone, a heavier mode (higher resolution, D49's draw distance) that runs the game thread late at 60 fps, or the hardware measurement after 7c showing game-thread waits that 7c doesn't remove.
  - If one of those comes, **7d's first piece is the snapshot from this list** (cheap, testable against D30's pass), not the re-entrant renderers.
- **Checks** (this branch merged with main `dcbe47d`; main's code is `37a50f0`'s, so main's binary is `37a50f0`'s):
  - `tools/host_checks.sh` against main's binary: 287 of 287 files identical (traces, shots, WAVs, cards; the logs differ only in their own folder's name).
  - Both attract demos tick for tick against the emulator (`trace_diff.py --key tick` against `logs/caps/cap3`): Stone Hill 866 ticks and Doctor Shemp 768, identical on all 11 columns.
  - ctest 15 of 15 (`gte` skipped, as on main; `vcount` and its mutant on `clock.c`).
  - Oracle replay of the four capture sets on the default build: 4,316 samples with the same verdicts and fields as main's binary. Only stale register values that are host addresses differ.
  - On the REF build (`-DSPYRO_M2C_REF=ON`): 4,297 PASS, 0 FAIL, and 19 NOEXIT (calls the capture ended inside) over the four sets. Three credits draws (`func_credits_8007BFD0`) first hit the replay's timeout under load (SIGALRM). With `SPYRO_REPLAY_TIMEOUT=600` they pass, 18 of 18 in both tour sets (`logs/s7ref-replay`).
  - Vita3K, the gems script (`frames=8768 shot_every=512`, `CAPTURE=1`), main's CI VPK (run 42010046653, SHA-1 `041d8086…`) against this branch's (`logs/s7-branch.vpk`, `ebb1dd38…`): 17 of 17 shots, 17 of 17 window captures and the memory card identical. With `REALTIME=1` (the display's clock, `clock.c`'s catch-up): 59.9 vblanks a second in both, late vblanks 14 against main's 25 (Vita3K's timing), both to `frames=8768`.
- **Hardware:** none run. 7b changes the Vita's real-time path only by moving it, and tests/vcount and Vita3K cover it. 7c's hardware pair needs the owner's yes first.

## 2026-11-05 — direct vertices, the walk's box as SIMD

### D105. The walk's UV box and skip test as ARMv6 SIMD lanes: `nd_direct=1` a net gain with identical pictures, and the Vita's default; the clears are three quarters of what is left of the PS1 GPU → vitaGL layer
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
- **Default: `nd_direct=1` on the Vita** (the owner's decision, 2026-11-05; the host stays 0). For: the same pictures and card everywhere checked; with every layer on one thread 0.42 ms per vblank less; at 60 fps the render thread 0.83 ms less and late vblanks fewer. Against: the parse thread does 0.22 ms more and the game thread waits 0.10 ms more at its join, both small and on threads with room. Set in `src/platform/vita/main.c` beside `native_draw`'s default; the options menu doesn't save it, so only a config line `nd_direct=0` gives D92's path back. Checked on that build after merging main's D104 and D106 (VPK `logs/d105-vpk-3.vpk`, SHA-1 `25602b86…`): host every packet both ways on the demos, gems and route A, 0 differ, 26/26, 19/19 and 55/55 files identical; Vita3K gems with the default against `nd_direct=0`, plain, with `persp=1` and with `subpixel=1` (where the lean path is off, so the walk is not made direct: 0 buffers drawn direct), each 17 shots, 17 captures and the card identical; 0 buffers walked direct and drawn another way.
- **Not done:**
  - **The clears** (next): row by row without the wrap mask where a clear doesn't wrap (a `memset`-style fill of `s_vram`), and finding what the targets' `glClear` costs. A loop that writes the same pixels gives identical VRAM, which the shots' readback replay (Vita3K, hardware) compares. One more `gpu parts` split (the VRAM loop against `clear_targets`) settles which half to take first.
  - D99's other two items (the walk writing straight into the slab; the side stream in vitaGL's scratch memory) are unchanged.

## 2026-11-05 — native memory, the rest of stage 4

### D106. Stage 4f–4i: the data labels as ordinary globals in a section laid out as PS1 RAM; the game's memory one heap block from main_BSS_END's page to the top of RAM; native save states format 2; the Vita on native memory
- **Why:** steps 4f–4i of the stage 4 plan (`docs/native-plan.md`, D97). 4a–4e are merged (D97, D100): the native build (`-DSPYRO_NATIVE_MEM=ON`) had no mips2c code and ran at any base, but its data labels were still in the 2 MB arena (absolute symbols, or `arena_syms` pointers), and the arena still stood for all of PS1 RAM. The brief named this D103; main's D103 (vita-port-kit's hardware scripts, #81) came first, so this is D106 (D104 and D105 are the sibling sessions'). One PR: 4f–4h, then 4i once the hardware pair had run (the owner's yes, 2026-11-05).
- **4f, the data labels as ordinary globals** (native only):
  - `tools/data2s.py --image` (with `--map psx.map`) puts all 516 labels in **one writable section, `spyro_labels`, in PS1 address order, each at its PS1 offset** (`.org`), so the section is an exact image of PS1 RAM from the first label (0x8006BBE0) to main_BSS_END (0x8007AA38, 61,016 bytes), holes included. Its start is congruent to 0x8006BBE0 modulo a page. The labels stay relocatable: a data word naming a label is relocated by the linker, so the native build has no `--map` absolute symbols, no `s1_psx_image` copy and no `arena_syms` pointers.
  - **Why an image and not D97's table** (PS1 address → label + offset): with the labels at their PS1 offsets the table is one range, `s1_labels + (address - 0x8006BBE0)`, and a read past a label's end finds what it finds on the PS1, as in the arena build. The holes are PsyQ's data, which PsyCross replaces; they are dead memory in both builds.
  - **PsyQ's `rand` seed** (0x80075AC0) lives in one of those holes. `--extra D_80075AC0=0x80075AC0` names it there, and `rand_psyq.c` refers to it by name.
  - **The OT terminator** (`D_8006BBE0`) is the one label the game links into the OT, and the OT's links decode only inside the game's memory (psx_mem.h, D100). Its copy stays in the block (`PSX_OT_TERMINATOR`, `order_table.c`), as the non-PS1-layout build always did.
  - **Was it the only one?** `-DSPYRO_MEM_AUDIT=ON` (new, default build) records every decode of a packed address (OT links, through a walk of every list `func_80016784` links; frame words; Spyro's halved frames; the face-save list; the subdivision headers) whose address lies outside overlay space. Over the four host-check runs (`logs/s4f-audit-*`): **one site, one address**, `order_table.c` reaching 0x8006BBE0 (6,165, 6,666, 6,666 and 50,166 times). No other label primitive is linked, and no frame pointer or subdivision header points below main_BSS_END. The audit build's own 279 files are main's.
  - **The host's PS1 addresses** (`--poke`, `--peek`, pad-script `poke:`, `step.c`, the tour, the Vita's `poke=`) go through `PSX_ARENA`, which in the native build is `psx_mem_host()` (`port.c`): a label, or overlay space.
  - `tools/savestate_globals.py` accepts a symbol in `[s1_labels, s1_labels_end)` where it accepted an absolute one. Save states get a `LABL` chunk, and the presenter snapshots the section where it snapshotted 0x8006BBE0–0x8007AA38 of the arena.
- **4g, the game's memory from the heap** (native only):
  - **The block** is overlay space, main_BSS_END to 0x80200000 (1,594,824 bytes, the PS1's), **behind the 0xA38 bytes of main_BSS_END's page** below it, which hold the OT terminator: `PSX_MEM_PSX` 0x8007A000, `PSX_MEM_SIZE` 1,597,440 bytes, from `memalign(4 KB)`. linux32 maps it at `SPYRO_ARENA_AT` when that is given (any page), for tests. The block keeps PS1 RAM's offsets modulo a page, and the 24- and 21-bit decodes (D100) need it under 2 MB, which it is.
  - `g_psx_arena_base` is the block's start. `PSX_ADDR` (the decomp's `0x80200000`, `port_prefix.h`) is relative to it, so the top of RAM is the block's end; `g_OverlaySpacePointer` and the `Terminator_*` come from `psx_mem_host` (`ovl_table.c`, which now checks psx.map's main_BSS_END against the header's).
  - **The scratchpad** is `g_psx_scratch` (1 KB, aligned to 1 KB), which save states (`SCRP`), the presenter and the checks already held: in the arena builds it was an unused copy beside the arena's first KB. `SCRATCH_BASE` (the hand renderers) and the data words that hold scratchpad addresses (`s1_scratch_relocs`) point at it.
  - **The D42 stores through small integers** (`PSX_KUSEG`): below 0x10000 they still go to `g_psx_kernel_ram`; from 0x10000 up, through `psx_mem_host` to a label or overlay space, and into the executable's image (where the arena build left them in an unused part of the arena) to a 256-byte sink nothing reads. None is known there: D42's are all below 0x10000.
  - PS1 addresses outside the labels and overlay space map to nothing, and the host checks them up front (`PSX_MEM_OK`): `--poke`/`--peek` refuse them, as they refused addresses outside the arena. The pad scripts' and `step.c`'s pokes and the Vita's `poke=` now check too (in every build; no script poked outside RAM).
  - The presenter's page-level debug (`--present-dirty`), the walk's copy and the debug checks (`hand_check.c`, `native_draw_check.c`) work on `PSX_MEM_SIZE`, the block. In the native build those two checks no longer compare the labels' section; they are debug-only.
- **4h, save states format 2** (native only): chunks `BLCK` (the block) and `LABL` (the labels' section) where format 1 has `ARNA`; the header's `layout[0]` is the block's address and `layout[1]` the labels'. The header has the same size, so a format-1 state reaches the format check and is refused as another build ("OTHER BUILD"); the same build ID and layout must match, as D51 asks. The arena and moved builds stay format 1.
- **4i, the Vita on native memory, by default:** `SPYRO_NATIVE_MEM` defaults to ON for `SPYRO_HOST=vita`, which turns the moved arena off (no `arena_syms`, no `port_arena_symbols`); the log names the block and the labels (`game memory at 0x83105000, data labels at 0x8140abe0` on hardware). The moved configuration is one switch away, `-DSPYRO_NATIVE_MEM=OFF` (`tools/ci/build_vpk.sh` takes `SPYRO_CMAKE_ARGS`). linux32's default stays the arena build, for the oracle. Players' quick-save states from an earlier VPK were already refused as another build (D51); a native VPK's are format 2.
- **Checked** (logs gitignored, in this worktree's `logs/`; main is `37a50f0`, its arena binary `build/base`, its host checks `logs/s4f-base-*`):
  - **The memory audit** (`logs/s4f-audit-*`, the default build with `SPYRO_MEM_AUDIT`): one site, 0x8006BBE0 only, in all four runs (above). Its 279 files are main's.
  - **4f, native with the labels as globals,** block below 2 GB at an unaligned address (`SPYRO_ARENA_AT=0x52F81000`, `logs/s4f-n4f-*`): **279 of 279 host-check files identical** to main's (the attract demos, routes A and B, the 100,000-vblank whole-game tour with both endings and the credits: every trace, shot and WAV, and the memory cards). The logs are identical too. ctest 15 of 15 (`gte` skipped, as on main), `savestate_globals` included.
  - **4g, native with the block from the heap** (0x3FBED000 under qemu-i386, `logs/s4g-n4g-*`): **279 of 279 identical**, logs identical. ctest: `savestate_globals` flagged the new KUSEG sink, a function-local static whose name GCC suffixes unpredictably; it is file-scope now and listed, and the test passes.
  - **4h, save states on native** (D97's recipe, `logs/state_rt.sh`: the gems script, save at 7,000, load at 7,600; `logs/s4h-n4h-H`, `-H2`): in process, 300 loop tops and 10 s of audio after the load identical to the first pass; a new process loading the state at its first loop top: 800 loop tops and 20 shots identical, and its 1,174,530 audio frames after the load identical to H's after its load (`savestate_compare.py` flags the audio only because H's WAV holds the replayed stretch, as in D97 and D100). The state is format 2, block at 0x3FBED000 and labels at 0x082EEBE0 in its header, so the heap gave the block the same address in both launches. Main's arena-build state is refused: "STATE 1 IS FROM ANOTHER BUILD" (`logs/s4h-v1`).
  - **This branch's arena build:** `objdump -d` with addresses and in-function offsets blanked against main's differs only in `main` (`--poke`/`--peek`), the pad script's `load` and `host_step_vblank` (the pokes' new range checks), and alignment padding (`logs/s4-disdiff-arena2.txt`). ctest 15 of 15, the changed `savestate_globals.py` included. Its host checks: `logs/s4h-arena-*`.
  - **Vita3K** (the gems script, 8,768 vblanks, `CAPTURE=1`): main's VPK (`37a50f0`, built from a clean checkout) twice (`logs/s4i-v3k-main`, `-main-b`) and the native VPK twice (`-native`, `-native-b`). **All 17 shots and the memory card identical in all four runs.** The window captures, compared below the window's top row (row 1 differs by 1 in most grabs of every run): 16 of 17 identical between any two runs, and each run has at most one odd capture, a different one each time (main-b at 512, a whole-picture difference; native at 8704, a 30×60 patch, the one D100 saw; native-b at 4608). Every capture that differs between main and native also differs between two runs of the same VPK, so it is the grab, not the code.
  - **Save states in Vita3K** (native VPK, `state_test=`): save at 7,000 and load at 7,600 in one process (`logs/s4h-v3k-S1`): all 17 shots identical to the plain native run, the four after the load included; save 153 ms, load 83 ms. A fresh launch loading that state at its first loop top (`-S2`): its 4 shots after the load identical. The block was at 0x88005000 in every launch.
  - The VPK builds without a warning in `src/game` or `src/platform`.
- **The hardware pair** (the owner's yes, 2026-11-05; the owner's Vita, vitacompanion 1.05, the disc image; `tools/vita/hw_run.sh`, the gems script, 8,768 vblanks, `shot_every=512 shot_hd=1`): main's VPK (`37a50f0`, `logs/hw-s4i-main`) and the native VPK (`logs/hw-s4i-native`), each to `frames=` in 90 s. **17 of 17 shots, 17 of 17 HD shots (the real GPU's picture) and the memory card identical**; the shots are also Vita3K's. No hang or crash. Main's current eboot (`ecdbce6`) was put back afterwards with the title route (`logs/hw-s4i-restore-main`).

## 2026-11-05 — vita-port-kit runtime modules

### D107. The audio output ring and its thread are vita-port-kit's `vita/runtime/audioout`
- **What:** `src/platform/common/audio_ring.c` moved to vita-port-kit as `vpk_audio_ring` (vendored in `third_party/vita-port-kit/vita/runtime/audioout`), merged with kh1-decomp's simpler ring. D88's held level is the ring's `rate_control 1`; its constants stayed in `psx_audio_ring.h`, which now maps the `psx_audio_ring_*` names to the kit's and passes them as the ring's config. `audio.c`'s output thread (the BGM port, 512-frame grains, core 1, priority 0x68) is the kit's `vpk_audio_out` with those settings.
- **Why:** kh1-decomp had a second copy of the same output thread and a ring without the level control; one copy in the kit, used by both games (vita-port-kit roadmap K4).
- **Checked:** the clock model (`tests/audio`, 10 minutes) prints the same numbers as before, byte for byte; the VPK builds. Device behaviour: a hardware run before merging.

### D108. The disc image's reader is vita-port-kit's `vita/runtime/discimage`
- **What:** `cdimage.c`'s sector reading (the two 32-sector windows), its sniffing of the file (sync pattern, sector size, first LBA; .iso, CHD, PBP, ECM), the ISO9660 walk and SHA-1 moved to vita-port-kit as `vpk_discimage` (vendored in `third_party/vita-port-kit/vita/runtime/discimage`), merged there with kh1-decomp's reader. `cdimage.c` keeps what is Spyro's: the search of the data folder, the cue sheet, every message to the player, the cut-short and SCUS-94228 / WAD.WAD checks, the marker. `psx_sha1_*` map to the kit's.
- **Why:** kh1-decomp had a second reader of the same sectors (vita-port-kit roadmap K4). The kit's opens with 64-bit offsets (sceIo on the Vita, `fseeko` on hosts); images here are under 2 GB, so nothing changes for this game.
- **Checked:** `ctest` and `tests/cd/image_variants.py` (every good dump and every refusal) on the host; the VPK builds. Device behaviour: a hardware run before merging.

### D109. The touch panels and the menu tap are vita-port-kit's `vita/runtime/touch`
- **What:** `pad.c`'s panel areas (with the front panel's range as the fallback), the fingers as 0..1, and D46's tap test (0.3 s, under 0.04 of the panel, one finger) are vita-port-kit's `vpk_touch` / `VpkTap` (vendored in `third_party/vita-port-kit/vita/runtime/touch`), merged there with kh1-decomp's rear quarters and tap. What counts as a tap here (the middle third, start and end), the rear halves as L2 / R2 and the `pad=` gestures stay in `pad.c`.
- **Why:** kh1-decomp had its own rear mapping and tap (vita-port-kit roadmap K4).
- **Checked:** the kit's touch test covers this game's tap settings; the VPK builds. Hardware (2026-11-06, the PR's CI VPK, `pad=` gestures): a tap at the left edge does nothing, a tap in the middle opens the menu and the next one closes it. Rear touch not run (it needs hands on the panel).

### D110. The options table and panel are vita-port-kit's `vita/runtime/modmenu`
- **What:** D45's `menu_opts.c` (the table: text, stepping, layout, load, save) and `menu_panel.c` (the CPU-drawn panel and its 5x7 font) moved to vita-port-kit as `vpk_menu` / `vpk_menu_panel` (vendored in `third_party/vita-port-kit/vita/runtime/modmenu`). `menu_opts.h` and `menu_panel.h` map this game's names to the kit's; the sections (`MenuSection`, `g_menu_section_names`) stay here, since the kit takes a game's section names. The save is the kit's append form of `vpk_config_rewrite`, which kh1-decomp uses in place.
- **Why:** vita-port-kit roadmap K4 (one menu framework for the ports).
- **Checked:** `tests/menu_panel` against the kit writes the same three PPMs and the same `config.txt`, byte for byte, as main's code; the VPK builds. Hardware (2026-11-06, the PR's CI VPK, the K route with `menu_test`): both scripted visits opened the menu, warped, saved `config.txt` and closed.

## 2026-11-06 — the clears

### D111. The clears: the two CPU loops are most of `GR_ClearVRAM`; `gpu_clear=1` fills them row by row with NEON and keeps the log's base as a colour, `gpu_clear=2` also leaves a target's whole clear to its read-back. Identical pictures; off by default until hardware
- **Why:** D105 found `GR_ClearVRAM` to be 2.70 of the PS1 GPU → vitaGL layer's 3.66 ms per vblank (Y5, the K route with every layer on the game thread), and 5.7 ms per vblank of the render thread at 60 fps (Y7, Y8), but not which part of it.
- **What a clear is** (`src/platform/vita/gpu_gl.c` `clear_vram`, `clear_targets`; a host count over the attract demos, `logs/clr-count`): Spyro clears once a frame. The clear is the draw buffer it is about to draw, 512×224 at (0, 8) or (0, 248), whole and unwrapped, in the level's sky colour (black only at loads; 2,985 clears in 6,000 vblanks, plus a few 512×480 and 512×512 at start-up). For each one `gpu_gl.c`:
  1. sent the held batch;
  2. set the rect's 114,688 pixels of `s_vram` one at a time through the wrap (`row[(x + j) & 1023]`);
  3. sent pending uploads and copies (`flush_pending`);
  4. per target: started the log again, writing its base whole with the colour (`log_begin`, another 114,688 pixels); then the presentation slots (`pres_touch`), the bind and a scissored `glClear`.
- **The split:** `gpu_parts=1` gains a third `gpu parts` line with those parts, plus how many clears and pixels, and how many pixels were left to the read-back. In Vita3K (VPK `logs/d111-vpk-1.vpk`; the gems script, every layer on the game thread; means of the 26 windows from vblank 900, ms per vblank; `logs/d111-v3k-t0`…`t2`):

  | `gpu_clear` | `GR_ClearVRAM` | the batch | `s_vram` | uploads | log base | slots, bind | `glClear` |
  |---|---:|---:|---:|---:|---:|---:|---:|
  | 0 (as before) | 0.325 | 0.000 | 0.121 | 0.000 | 0.156 | 0.000 | 0.033 |
  | 1 | 0.093 | 0.000 | 0.040 | 0.000 | 0.000 | 0.001 | 0.041 |
  | 2 | 0.055 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.045 |

  - **The two CPU loops are 85% of the clear**; the `glClear` is 10%, and the rest nothing (in play no batch is held at the frame's start, no upload is pending and every presentation picture was handed its target's texture). Vita3K's GPU calls are not the Vita's, and its CPU is about 9× the Vita's here (0.33 against Y5's 2.70), so the hardware split is run Y9's to give. If the `glClear` turns out larger on the Vita, the next step is merging or skipping it; the loops' share says start with them.
- **Decided** (`src/platform/common/psx_fill.h`, `gpu_gl.c`, `src/platform/linux32/gpu_soft.c`; `gpu_clear=` in `config.txt`, `g_gpu_clear` in `gpu_soft.c`, so both rasterizers read the one switch):
  - **`gpu_clear=1`:**
    - The rect is split where it wraps, and each row's piece filled in one go (`psx_fill_rect`, `psx_fill16`): 16 pixels a step with two NEON stores on the Vita (after up to 7 pixels to align), two a step as 32-bit words elsewhere. The same pixels as the plain loop, also for rects wider than 1024 or higher than 512.
    - A whole clear's log base isn't written: the log keeps "the colour" (`solid`), and a replay fills the rect with it. The replay's own `LOG_CLEAR` entries use the same fill.
    - `s_vram`, the GPU's pictures and every reader see the same pixels; only the log's internal copy is not made.
  - **`gpu_clear=2`:** 1, and a clear whose unwrapped piece is a target's whole rect doesn't write `s_vram` there. The target is marked as drawn (`cpu_stale`), which is already the rule for every draw into a target: whatever reads that VRAM first (`sync_rect` for a texture page, `MoveImage` or `StoreImage`, a screenshot, `drop_target` at a save state or a rescale) gets the target's pixels back. The read-back rewrites the whole rect, from the log by the software rasterizer (the replay). This clear has just started that log, so the replay's base is the colour and then whatever was drawn after it.
    - Only with `gpu_readback=1` (the default; with 0 every read-back is the GPU's) and at full scale (`pm` 1000: dynres off, the default).
    - The one case where 2 reads back where 0 did not is a target cleared and then given more than 8 MB of `LoadImage` before any draw. The log overflows (`LOG_MAX_DATA`) and the read-back is the GPU's. It would still be the same pixels there: the clear colour comes back exact (a 5-bit `v` is `v · 8 / 255` to GL and `v · 8` in the texture), and so do the copies, drawn nearest at full scale and drawn down nearest. Spyro never does it.
  - **`gpu_clear=3`**, 2's audit: the pixels 2 leaves unwritten get `0xFC1F` instead (STP set, red and blue full). A reader that skipped the read-back would show it in a picture, a capture or a texture.
  - **The host's `--gpu-clear`** is 1's fill in the software rasterizer (`gpu_soft.c`, which the Vita also links for the replays). The host has no targets, so 2 and 3 are the Vita's.
  - **Off, nothing changes:** `gpu_clear=0` runs the old loops. The timers follow D99's rule: the clock is read only with `gpu_parts=1`, and the counts are two additions per clear.
- **Checked:**
  - **`tests/fill`** (ctest `fill`, `fill_mutant`): `psx_fill_rect` against the plain loop. Random rects at every alignment: wraps right and down, wider than 1024, higher than 512, empty and negative, each in its own colour. Plus the edge cases (Spyro's clears, whole VRAM, wraps by one, runs either side of NEON's 24-pixel threshold). After each rect, every row it reached is compared whole. The host's C path: 32,976 rects, 315.5 M pixels, **0 differ**. The mutant (the wrapped piece one pixel short) fails.
  - **The NEON path** (`tests/fill/arm_check.sh`; not in CI, since this session can't change the workflow file: a step after the host tests is the owner's to add): built with the Vita's compiler (vitasdk GCC, `-O2`, its default Cortex-A9 Thumb-2 with NEON; 4 `vst1.16` in the test) and run under qemu-arm. 1,048,784 rects, 9.36 G pixels, **0 differ**.
  - **`tools/host_checks.sh`** against a build of main `0c3fff4` (`build/main-src`): **287/287 files byte-identical** for main, this branch, and this branch with `EXTRA=--gpu-clear` (`logs/clr-main-*`, `clr-off-*`, `clr-on-*`: demo 28, the whole tour 143, routes A and B 58 each; the logs with the prefix replaced).
  - **The host's VRAM** (`--vram-shot`), main against `--gpu-clear` (`logs/clrv-*`): the demos every 250 vblanks, gems with the pause every 100, routes A and B every 250. **239 VRAM dumps and their 239 shots identical.**
  - ctest 19/19 (`gte` skipped, as on main); `tools/build_matching.sh` `PSX.EXE SHA-1 OK`.
  - **Vita3K against main's VPK** (`logs/main-0c3fff4.vpk`, SHA-1 `ced71363…`, against `logs/d111-vpk-1.vpk`, `2553c8b6…`; shots are the read-back replays, captures the GPU's own window, `CAPTURE=1`; logs `logs/d111-v3k-*`):

    | Run | `gpu_clear=1` | `gpu_clear=2` | `gpu_clear=3` (poison) |
    |---|---|---|---|
    | gems script (8,768 vblanks, every 500) | 17/17 shots, 17/17 captures, card | 17/17, 17/17, card | 17/17, 17/17, card |
    | gems, then pause and inventory (to 9,100: 10 shots from 6,000, the pause screens' `StoreImage` read-back) | 10/10, 10/10, card | 10/10, 10/10, card | 10/10, 10/10, card |
    | attract demos (12,000 vblanks, every 1,000) | 12/12, 10/11; a second run 11/11 (see below) | 12/12, 11/11 | 12/12, 11/11 |
    | gems in 16:9, 60 fps forced (`interp=1 interp_budget=0`, to 6,600, every 500) | 13/13, 4/13 (see below) | 13/13, 12/13 (see below) | 13/13, 13/13 |
    | the same with `gpu_time=2` (a `glFinish` at each capture, as D105) | 6/6, 6/6 up to Vita3K's crash at 3,000 (twice; see below) | 13/13, 13/13, card | 13/13, 12/13 (see below), card |

    - **The audit is clean.** With poison in every pixel `gpu_clear=2` leaves unwritten, every shot, capture and card is main's, the pause route's `StoreImage` read-backs included, and that route reads back as often as main (11 times, all replayed). Nothing read those pixels without the target's read-back.
    - **The captures that differ are Vita3K's, not pictures.** Every shot (the replays: what the game's VRAM holds) is identical in every run, and the cards too.
      - At 60 fps main's own VPK differs from itself: of three runs with `gpu_time=2` (`logs/d111-v3k-g60r-main`, `g60r2`, `g60r3`), two agree and the third differs from them in 7 of 13 captures.
      - The audit's one differing capture (`win-6500`) is pixel-identical to the one `gpu_clear=1` took without `gpu_time=2`, so the window showed one of several pictures. It holds no poison-coloured pixel.
      - In the demos, `gpu_clear=1`'s `win-11000` differed from main in the screen's top line only (1,106 of the grab's pixels, row 1). A second run (`logs/d111-v3k-demo2-c1`) is identical to main in all 23 files, and main's own second run (`demo2-main`) matches its first.
    - **Vita3K crashed itself** (`EXC_BAD_ACCESS`, its handler's "write protected region" at `0x606A5018`) at vblank 3,000 of the 60 fps runs with `gpu_time=2`: once in two runs of main's VPK, and in both runs with `gpu_clear=1`. It is the same crash, with the same address, on main's VPK. Up to it, `gpu_clear=1`'s 12 files are main's. D86 saw Vita3K crash the same way in pause runs on main's path.
    - **At 60 fps on the render thread** (Vita3K, the shipped threads, 16:9, `interp=1 interp_budget=0`, gems; means of 27 windows from vblank 900; `logs/d111-v3k-t60-0`, `t60-2`): the clear's parts sum to 0.98 ms per vblank with `gpu_clear=0` (`s_vram` 0.37, the log's base 0.53, `glClear` 0.08) and 0.05 with `=2`. The render thread is 16.6 ms busy per vblank either way: in Vita3K it is held by Vita3K's own GPU, so its gain is run Y12's to give.
    - **Repeats** (the gems timing run three times each, interleaved, `logs/d111-v3k-t0*`, `t2*`): `GR_ClearVRAM` 0.32–0.46 ms per vblank with `gpu_clear=0`, 0.03–0.08 with `=2`. `GR_DrawTriangles` moved 0.12–0.39 with the Mac's load (other sessions' runs), in both.
- **Hardware:** not run. Proposed: Y9–Y12 (`tools/vita/fps-runs/README.md`), Y6 and Y8 again with `gpu_clear` 0 and 2. The K route with every layer on the game thread gives the split on the Vita and the gain; Toasty at 60 fps gives the render thread's. Each needs the owner's yes.
- **Default: 0** until the hardware pair; the owner decides. For 2: identical pictures and VRAM in every check, and in Vita3K the clear at a sixth of its cost. Against: until Y9 it isn't known how much of the hardware's 2.70 ms is the loops.
- **Not done:** merging or skipping the targets' `glClear` (10% in Vita3K; Y9 says whether it matters on the Vita). The presentation's own clears and `present.c` are stage 7c's, untouched here.
## 2026-11-06 — stage 7c: presents at the due vblank

### D112. Stage 7c: every frame carries its due vblank; the host shows the frame due at each vblank (the same frames as before); on the Vita, `present_due=1` lets the render thread show frames at their display vblank without the game thread presenting. Off by default until its hardware pair has run
- **Why:** step 7c of the stage 7 plan (`docs/native-plan.md` §5, D101, D104). The aim is that a late render thread costs pictures, not ticks. Until now the game thread posted every present from `VSync` and waited for the previous one to have run ("previous frame not shown yet"): in X7's Stone Hill attract-demo flight that wait was 477 ms in every 300-vblank window (`fps60_summary.py`'s new `wfr` column on `logs/hw-x7`). D108–D110 went to merged PRs and D111 to the clears, so this is D112.
- **Frames carry their due vblank** (`present.c`, every platform):
  - The queue is taken one frame per vblank, so a frame filed at vblank count c behind k waiting ones is due at c + 1 + k. When the queue is full (4 waiting), its oldest frame goes and the rest each come due a vblank sooner, as the queue always behaved.
  - `PresentFrame` (slot, due, wide, in-between or real, and the slot dropped for it) goes to the platform with each commit. A new optional `begin(due, interp)` comes at a capturing pass's first `DrawOTag`.
  - `port_present_frame_due(v)` takes the newest frame due at vblank v. The host calls it at each virtual vblank in place of `port_present_frame()`. By the arithmetic above it is the frame the old pop gave, and the host checks below confirm that byte for byte. `--present-stats` counts frames taken late, never taken, and dropped: 0, 0 and 768 in the widescreen attract demos (the drops are the gamestates that tick every vblank, as before).
  - Shots on the host come from the frame due at their vblank, as they did. The Vita's `realtime=0` path is unchanged: its one pop per virtual vblank is the same frame.
- **`present_due=1` on the Vita** (`gpu_thread.c`, `vblank.c`; only with `realtime=1`, `gpu=1` and `render_thread=1`, otherwise logged and off):
  - **The game thread** takes the frame due at each vblank it runs (the catch-up's too) and posts only the PS1 display's pictures (loads, boot logos, both features off), without waiting for the previous present. A shot at a vblank (`shot_hd`) posts the frame due there, synchronously, as before.
  - **The render thread** keeps the filed frames and shows the newest one whose due vblank the display has reached. It reads the display's 16-bit vcount against the clock's base (`psx_clock_display_base`, `vita_vblank_past_due`).
  - **When it presents:** at a frame boundary (a frame filed) or when it has nothing to run (then it looks every millisecond). Between two draws it presents only a frame already a vblank late (it looks every 16 commands). A present swaps, and in Vita3K a swap holds the render thread until the next flip, so presenting the moment a frame fell due left it almost no time to draw. Presented at a frame boundary within its vblank, a frame still flips at the same vblank.
  - **Falling behind:**
    - A frame's `C_BEGIN` already a vblank or more past its due skips that frame's draws, capture and commit. For an in-between frame that's always (its real frame is due too). For a real frame, only when a later frame is filed already or the game waits for the ring or a vertex slab.
    - Uploads (`C_COPY`, `C_CALL`), clears and the state commands always run. The clears are the other session's (D111), untouched here.
    - A PS1 display present with a later present already posted isn't drawn.
    - While the game thread has waited for the pools since the last picture, the render thread shows at most one picture every two display vblanks.
  - **No new thread:** a first version had a vblank thread waking the render thread at each display vblank. It is gone, so there is no second waiter on `sceDisplayWaitVblankStart` beside vitaGL's flip.
  - **The `present N:` line** gains a `7c:` part: frames shown at their vblank, how many late and by how many vblanks, how many never shown, the draws skipped (in-between and real frames), the PS1 display pictures dropped, and the presents held. The game thread's own view (frames taken late, never taken, dropped from the full queue) gets a second line when it isn't all zero.
  - **The `render N:` line:** "previous frame not shown yet" is 0 with `present_due=1`.
- **Behind a switch, default 0:** `present_due=0` is main's code path. 7c changes when pictures reach the screen, and only hardware can say whether that is better. In Vita3K, real time is too noisy to decide it (below). `config.txt` key `present_due`, `docs/vita-testing.md`.
- **Two bugs found in Vita3K, fixed before the checks:**
  - **The queue's drops weren't followed.** In the gamestates that tick every vblank, two frames are filed a vblank, so the queue drops its oldest and moves the rest up. The render thread didn't know: slots came back before any frame was due, and nothing was shown for 300 vblanks. Each filed frame now names the frame dropped for it.
  - **Presents inside a command** were timed twice in the render line.
- **Size:** ~670 lines with comments, over 18 files (`gpu_thread.c` ~380 of them). The plan said ~300–500.
- **Checks** (logs gitignored, in this worktree's `logs/`). The host runs are this branch on main `0c3fff4`; the host binary is `869ba2f`'s, and the commits after it change only Vita files. The Vita3K runs are this branch merged with main `172b8ec` (`logs/s7c-branch-8.vpk`, SHA-1 `c0ac8e98…`), against main's CI VPK (run 42200382743, `a2ff7c01…`):
  - **Host, against main's binary** (`build/s7c-ref/spyro-main`; `tools/host_checks.sh`):
    - plain: 279 of 279 files identical (`logs/s7c-main-*`, `logs/s7c-br-*`);
    - `EXTRA=--interp`: 279 of 279 identical, the shots taken by vblank from the presentation frames (`logs/s7c-main-i-*`, `logs/s7c-br-i-*`);
    - the attract demos with `--widescreen --interp --present-stats`: 50 of 50 identical (shots every 250, trace, WAV; `logs/s7c-*-wsdemo`).
  - **ctest:** 17 of 17 (`gte` skipped, as on main). **Matching build:** green (`logs/s7c-matching.txt`).
  - **Vita3K `realtime=0`** (the gems script, `frames=8768 shot_every=512`, `CAPTURE=1`; `present_due` is off in `realtime=0`, so these show the default is main's):
    - **30 fps:** 17 of 17 shots, 17 of 17 window captures and the memory card identical (`logs/s7c-v3k-m2-gems`, `-b8-gems`).
    - **60 fps forced** (`interp=1 interp_budget=0`; main's VPK three times, this branch's three times):
      - In every run, 17 of 17 shots and the card are identical to main's.
      - Main's three runs have identical captures.
      - The branch runs have at most one capture each that differs from them below the window's top row, a different one each time: none in one run; the 30×60 patch at 8704 that D100 and D106 saw in another; a 94×18 patch at 8192 in the third. Row 1 differs by 1–2 in some grabs, as D106 recorded. That is the grab.
      - The earlier branch builds of this PR gave the same: 17 of 17 everything against main `0c3fff4`'s CI VPK (`91313cb5…`) at both rates.
    - **Vita3K crashed twice** on the branch's 60 fps gems runs, on VPKs 4 and 8, of 7 such runs; 0 of 4 on main's. The crash is inside Vita3K's renderer thread: `free()` of a bad pointer in `renderer::cmd_handle_set_context`, in its macOS crash report. With `realtime=0` the branch issues the same GL calls in the same order as main. Read as a Vita3K race the timing exposes, but noted: the hardware runs will say whether anything like it happens on a Vita.
  - **Vita3K with `REALTIME=1` and `present_due=1`:** the gems script and the K route (`k-route.pad` with `m1-xa-k.cfg`'s menu warps) reach `frames=` with no hang. The menu warps, loads, title and intro work. The present line's counts add up.
    - Timing in Vita3K says little. Late vblanks per 300 differ by 3× between two runs of the same VPK and settings (the Mac was loaded by host checks and another session's Vita3K runs). And in Vita3K a swap costs the render thread about a whole vblank, where X7 measured 10.6 ms a picture on hardware.
    - In the gamestates that tick every vblank, the render thread is the wall in Vita3K with either setting. With `present_due=0` the game absorbs that in `VSync`; with `present_due=1`, before the hold, it waited for the vertex slabs mid-tick and ran up to 120 vblanks late per 300.
    - With the hold (VPK 7), that was gone: on the K route, late vblanks per 300 were 1.4 in the attract demo and 1.5 in Artisans, the game thread at most 39% busy.
    - On the merged VPK, `present_due` 1 against 0, late vblanks per 300 on the K route: title 3.4 against 8.2, the attract demo's flight 17.0 against 59.3, Artisans 11.5 against 7.6. "Previous frame not shown yet" went from 1.4–1.9 s a window to 0.
    - Over the route, the render thread showed 15,894 frames at their vblank: 444 late (509 vblanks), 666 never shown, draws skipped for 655 in-between and 569 real frames, and 427 presents held (`logs/s7c-v3k-b8-k60-rt-*`).
    - The route's level warps (`menu_test`) took or didn't from run to run with either setting; the menu opened, saved and closed in every run.
- **The hardware pair** (proposed, not run; the owner's yes first for each run): runs 7C1–7C4 in `tools/vita/fps-runs/README.md`. On one VPK: X7's settings on the K route and on the Toasty route, `present_due=0` against `=1`.
  - **What decides it:** late vblanks per 300 (above all in the attract demo's flight), the game thread's waits (`wfr` should be 0 with 7c, `wpool` small), the `7c:` skips, and the pictures looking right.
  - **If it passes,** `present_due=1` becomes the Vita's default in a follow-up. If it doesn't, the switch stays off and 7c's host side (frames by due vblank) stays, as it changes nothing.

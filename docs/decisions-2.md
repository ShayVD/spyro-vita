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

### D109. The touch panels and the menu tap are vita-port-kit's `vita/runtime/touch`
- **What:** `pad.c`'s panel areas (with the front panel's range as the fallback), the fingers as 0..1, and D46's tap test (0.3 s, under 0.04 of the panel, one finger) are vita-port-kit's `vpk_touch` / `VpkTap` (vendored in `third_party/vita-port-kit/vita/runtime/touch`), merged there with kh1-decomp's rear quarters and tap. What counts as a tap here (the middle third, start and end), the rear halves as L2 / R2 and the `pad=` gestures stay in `pad.c`.
- **Why:** kh1-decomp had its own rear mapping and tap (vita-port-kit roadmap K4).
- **Checked:** the kit's touch test covers this game's tap settings; the VPK builds. Device behaviour: a hardware run before merging (a tap opens the menu, rear touch with `reartouch=1`).

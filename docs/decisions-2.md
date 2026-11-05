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

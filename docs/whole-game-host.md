# The whole game on the linux32 host

Groundwork for M4, which needs the whole game to be finishable. The question is whether the host build runs everything from Artisans to the end credits the way the PS1 does. Method: decisions D31. Short version: the same scripted tour runs in PCSX-Redux and on the host, and three things are compared: the translated functions (oracle replay of samples from every level), a screenshot of each level from each side, and how the tour went (visits, sequence lengths, crashes).

**Result (2026-10-30):** every level overlay (10-64) loads, plays and draws on the host. So do both endings with their cutscenes, both credits sequences (short and full) and the credits cheat. A save made after all of that loads again. One host run does the whole thing without a crash (`logs/run21-host-whole`). Function replay over the whole game: 3,707 of 3,707 completed samples match. Three bugs were found and fixed on the way: two crashes and one drawing bug.

## The tour

Each side starts a new game, then takes these entries in order. The host uses `--tour`, the emulator `capture.lua ORACLE_TOUR`; both are in `src/platform/linux32/tour.c` / `tools/oracle/capture.lua`.

1. Levels 11-15, 20-25, ..., 60-63 with the game's level-warp cheat. Spyro stays 300 vblanks in each level (60 in flight levels, where he drowns if left alone), then a screenshot is taken (`tour-K-LEVEL.png` / `.raw`).
2. `end2`: what Gnasty Gnorc's moby does when he is beaten. Cutscene 2 plays, then the short credits, then the game goes back to Gnasty's World (60).
3. Level 64, then `end3`: what Gnasty's Loot's vortex moby does with 14000 gems. Cutscene 3 plays, then the full credits, then the game goes back to Artisans.
4. The credits cheat (inventory), then the LEVELS OPEN and 99 LIVES cheats (host run only).
5. `--pad-after tools/oracle/pads/fairy_after.txt`: free Nestor and save at the fairy.

Rerun:

```bash
logs/run21-host-whole/cmd.sh      # host, about 100,000 vblanks (10-40 min under qemu)
logs/run14-oracle-tour/cmd.sh     # PCSX-Redux, 100,000 vblanks (about 45 min) with function samples
tools/oracle/replay.sh logs/run14-oracle-tour
python3 tools/oracle/tour_report.py logs/run21-host-whole logs/run14-oracle-tour
python3 tools/oracle/tour_pairs.py logs/run21-host-whole logs/run14-oracle-tour pairs.png
```

(The `cmd.sh` files live in the gitignored `logs/`; each is a copy of the commands above with all the flags.)

## Per level

Columns:
- **Plays at:** the first vblank in gamestate 0. The host's counts start at `main`, the emulator's at reset, and the PS1 loads from the CD in real time, so the two columns don't line up.
- **Drew:** the share of non-black pixels in the screenshot taken when the tour left the level.
- **Picture difference:** the mean absolute difference of 32×15 block averages, 0-255. Spyro and the camera are still settling after the glide in, and the host stays about 110 vblanks longer per level (its loads are instant, so it waits out the tour step's 400-vblank window). So 5-12 is the same view, and flight levels, where Spyro keeps flying, come out at 20-33.
- **Oracle replay in level:** samples taken in that level that replay matching through the host's translated C (`logs/run14-oracle-tour/replay.txt`; up to one sample per function per level, plus the credits samples, which count under the level being flown over).

| Level | Name | Host plays at | Oracle plays at | Host drew | Oracle drew | Picture difference | Oracle replay in level |
|---|---|---|---|---|---|---|---|
| 10 | Artisans | 6347 | 8689 | 93% | 93% | 6.3 | 72/72 |
| 11 | Stone Hill | 6933 | 9720 | 93% | 93% | 11.6 | 49/49 |
| 12 | Dark Hollow | 7635 | 10691 | 93% | 92% | 5.1 | 48/48 |
| 13 | Town Square | 8337 | 11723 | 93% | 93% | 6.8 | 48/48 |
| 14 | Toasty | 9039 | 12731 | 93% | 93% | 6.8 | 48/48 |
| 15 | Sunny Flight | 9741 | 13752 | 93% | 93% | 22.7 | 56/56 |
| 20 | Peace Keepers | 10203 | 14523 | 93% | 93% | 5.7 | 53/53 |
| 21 | Dry Canyon | 10905 | 15526 | 93% | 93% | 6.1 | 49/49 |
| 22 | Cliff Town | 11607 | 16538 | 93% | 93% | 7.4 | 48/48 |
| 23 | Ice Cavern | 12309 | 17553 | 93% | 93% | 17.3 | 48/48 |
| 24 | Doctor Shemp | 13011 | 18550 | 93% | 92% | 5.7 | 48/48 |
| 25 | Night Flight | 13713 | 19501 | 93% | 92% | 21.2 | 52/52 |
| 30 | Magic Crafters | 14175 | 20287 | 93% | 93% | 7.2 | 61/61 |
| 31 | Alpine Ridge | 14877 | 21333 | 93% | 93% | 7.7 | 57/57 |
| 32 | High Caves | 15579 | 22353 | 93% | 93% | 7.7 | 54/54 |
| 33 | Wizard Peak | 16281 | 23403 | 93% | 93% | 10.7 | 51/51 |
| 34 | Blowhard | 16983 | 24361 | 93% | 93% | 7.1 | 56/56 |
| 35 | Crystal Flight | 17685 | 25332 | 93% | 93% | 31.8 | 55/55 |
| 40 | Beast Makers | 18147 | 26105 | 93% | 93% | 4.7 | 57/57 |
| 41 | Terrace Village | 18849 | 27125 | 92% | 90% | 12.0 | 51/51 |
| 42 | Misty Bog | 19551 | 28147 | 93% | 93% | 9.3 | 47/47 |
| 43 | Tree Tops | 20253 | 29167 | 93% | 93% | 5.3 | 48/48 |
| 44 | Metalhead | 20955 | 30192 | 90% | 85% | 5.2 | 48/48 |
| 45 | Wild Flight | 21657 | 31134 | 93% | 93% | 20.6 | 55/55 |
| 50 | Dream Weavers | 22119 | 31905 | 93% | 93% | 7.8 | 53/53 |
| 51 | Dark Passage | 22821 | 32942 | 93% | 93% | 7.1 | 54/54 |
| 52 | Lofty Castle | 23523 | 33926 | 93% | 93% | 7.0 | 49/49 |
| 53 | Haunted Towers | 24225 | 34939 | 93% | 93% | 7.6 | 49/49 |
| 54 | Jacques | 24927 | 35930 | 93% | 93% | 6.3 | 48/48 |
| 55 | Icy Flight | 25629 | 36909 | 93% | 93% | 33.0 | 56/56 |
| 60 | Gnasty's World | 26091 | 37625 | 93% | 93% | 6.0 | 60/60 |
| 61 | Gnorc Cove | 26793 | 38649 | 93% | 93% | 9.0 | 52/52 |
| 62 | Twilight Harbor | 27495 | 39659 | 92% | 88% | 6.3 | 48/48 |
| 63 | Gnasty Gnorc | 28197 | 40670 | 93% | 93% | 5.8 | 60/60 |
| 64 | Gnasty's Loot | 39293 | 53043 | 93% | 93% | 6.0 | 59/59 |

Screenshot pairs, host left and emulator right, one row per tour entry: `logs/run21-host-whole/pairs.png` (`logs/run15-host-tour-crash/pairs.png` is the same before the fixes). Every level shows the same scene on both sides, and no geometry is missing.

Cutscenes and credits (gamestates 14 and 15) after the intro, in vblanks. Each credits roll ends with a short gamestate 15 stretch while the next level loads, which is why credits 2 and 4 are so short. The PS1 reads the cutscene and credits data from the CD in real time and drops frames, so its sequences take longer:

| Sequence | Host vblanks | Oracle vblanks |
|---|---|---|
| cutscene 1 | 2940 (level 10) | 2992 (level 10) |
| cutscene 2 | 1309 (level 63) | 1687 (level 63) |
| cutscene 3 | 2303 (level 64) | 2837 (level 64) |
| credits 1 | 8760 (level 63) | 9018 (level 63) |
| credits 2 | 24 (level 60) | 386 (level 60) |
| credits 3 | 8760 (level 64) | 9175 (level 64) |
| credits 4 | 24 (level 10) | 454 (level 10) |
| credits 5 | 8784 (level 10) | 9471 (level 10) |

## Runs

| Log | What |
|---|---|
| `logs/run21-host-whole` | **The whole game on the host in one run** (final binary): the tour above, LEVELS OPEN, 99 LIVES, Nestor freed, "GAME SAVED". Its `report.md` is the table above; `pairs.png`, the screenshot pairs. |
| `logs/run23-host-load` | The run21 save loaded from the title: Artisans, 1 dragon, 99 lives, every homeworld page in the inventory through Gnasty's World (`load-sheet.png`). The peeked `g_VisitedFlags` and homeworld unlocks match the values before saving. |
| `logs/run14-oracle-tour` | The emulator's tour, with function samples (1,858), label hits, a trace and a screenshot per level; `replay.txt`: 1,857 match, 1 never returned (NOEXIT). |
| `logs/run19-oracle-labels` | The same tour again, with label hits credited only to samples that complete; `replay.txt`: 1,850 match, 2 NOEXIT. |
| `logs/run17-oracle-icy` | The emulator at Icy Flight with a write watch on PS1 RAM 0x100-0x3FF (the flight-results store below). |
| `logs/run15-host-tour-crash` | The host tour before the credits fix: every level 10-63 and ending 2's cutscene, then the crash in the credits (core dump read with gdb). |
| `logs/run13-icy-hang` | The Icy Flight crash on its own (new game, warp to 55), with the gdb backtrace. |
| `logs/run18-host-endings` | New game, then 63, `end2`, 64, `end3`, the credits cheat and the save, on the host after the credits fix. |
| `logs/run20-host-whole` | Like run21 before the PsyCross fix; it stopped in the pause menu after LEVELS OPEN (Triangle leaves the inventory for the pause menu, not the game; the tour now presses Start too). |
| `logs/run22-demo` | Final binary, attract demos: both still identical to the emulator tick for tick (Stone Hill 866 ticks, Doctor Shemp 768, all 11 fields, against `logs/run8-oracle/cap3`). |

## What broke, and the fixes

1. **Icy Flight crashed once Spyro drowned** (`logs/run13-icy-hang`: SIGSEGV in `UpdateMoby_LEVEL_55`, `moby_update.c:26526`). This is a game bug the decomp keeps faithfully. In the flight results, the objective moby clears `m_RenderRadius` through `m_ResultMobys[i]`, but elements 4-7 alias the objective class IDs (0x1EB, ...). The PS1 stores a byte into its own kernel RAM. The emulator proves it (`logs/run17-oracle-icy`): PC `0x800811A8` stores to `0x23B`, `0x1D5`, `0x23A` and `0x1DA` in gamestate 7, and `0x23B` is where the host faulted. Fix: `patches/spyro-1-port/0006` sends that store through `PSX_KUSEG()` into the arena, as the PS1 does. Entry in `docs/upstream-notes.md`.
2. **The credits crashed the host** (`logs/run15-host-tour-crash`: `fseek` on a garbage `FILE*`). mips2c declared `Terminator_credits` as `extern char[]`, but the port defines it as a pointer to the overlay's end in the arena (D20). So the credits update took the pointer variable's own address, a host `.bss` address, as its buffer, and read 384 KB of credits data over the host's heap, including `libcd`'s file table. Fix: `tools/mips2c/mips2c.py` declares `Terminator_*` as `extern void *`. Only this function uses one.
3. **PsyCross's `setDrawMode` gave DR_MODE length 3**, where PsyQ gives 2. The OT walker then read the next packet's tag as a GP0 command, and a following tag of length 2 would be a fill rectangle. Found by replaying the credits draw: the emulator's tag is `021A3BBC`, the host's was `031A3BBC`. Fix: `patches/PsyCross/0006`.

The host and oracle tooling needed fixes too, before these results could be trusted:
- The replay's pointer rewriting (function and scratchpad addresses) changed level-data words that equal such pointers by chance. It made one r_moby sample (`func_8001F798` at Stone Hill) look wrong: one 4×1-pixel quad missing. Rewriting now stops at the loaded overlay's end (`fnmap.py` `TERM` lines), and that sample matches.
- The replay can now run samples that read the CD (`--disc`, the game's read callback set, no read latency) and fills in the overlay pointers. A sample that never returns is now stopped after 60 s.
- `tools/oracle/ignore_ranges.txt`, each entry with its reason, now also covers:
  - the pads' interrupt-filled receive buffers;
  - the memory-card library's event state;
  - all of PsyQ's own data except the rand seed (the port's C libraries keep their state elsewhere);
  - libgpu's `dr_env` packet cache;
  - the draw code's `VSync(-1)` timestamps;
  - a CD read that is in flight when a call starts or ends (`cdread`).
- `--moby-dump` read half-loaded tables during level loads and the credits flyover, and crashed (`trace.c`). It now dumps only in the level's own gamestates.

## The 47 entry points without a sample after M2

Each row of `docs/translation-log.md` now says what happened. In short:

| Kind | Count | Status |
|---|---|---|
| Callable, now sampled | 12 | All samples match: `ColSpyroInHitbox` (12), `func_80058B60` (2), `func_8003BFC0` (24), `func_80052F38` (30), `func_80017E98` (70), `EnvAnimSetFlags` (42), `EnvAnimSetFrame` (4), `func_80050240` (6), `func_80017894` (30), `func_80017C24` (24), `func_credits_8007AA50` (36), `func_credits_8007BFD0` (36). |
| Computed-jump labels (`D_…`, entered by `jr`, never called) | 25 | 7 ran inside matching samples of their own function. 4 ran, but never during a completed sample. 14 never ran in the tour (`logs/run19-oracle-labels/labels.txt`). |
| No caller anywhere (dead) | 4 | `func_80017188`, `func_80017548`, `func_8001771C`, `func_80017854`: no reference in the asm or C, and no word in `SCUS_942.28` equal to their address. |
| Not called in the tour | 6 | `func_8004F000` (the sky in the level-exit and balloonist gamestates, which warps skip), `func_8004F4BC` and `func_8004F7E8` (the cyclorama's other branch); all three are now sampled and match, see "Portals, exits and the balloonist" below; `func_80016FD0` and `func_80017614` (rare moby and pete paths), `func_8005060C` (3 calls, all entered by tail jumps, so no sample completes; it ran inside matching samples of its callers). |

## Portals, exits and the balloonist

The warps skip four things: entering a level through a homeworld portal (gamestate 1 with its transition, `g_HasLevelTransition = 1`), leaving through the pause menu's EXIT LEVEL (gamestate 10, `GS_ExitLevel`), leaving through a level's exit vortex (moby class 9, which goes straight to gamestate 1 with the transition home), and the balloonist rides between homeworlds (gamestate 12). Two scripted routes cover them, the same on both sides (decisions D36). Each is the pad-after script of a new game and the LEVELS OPEN cheat:

- **Route A** (`tools/oracle/pads/route_portal_exit.txt`, after `--tour levels`): Artisans. Spyro runs and jumps into Stone Hill's portal, then pauses in Stone Hill and takes EXIT LEVEL back to Artisans.
- **Route B** (`tools/oracle/pads/route_vortex_balloon.txt`, after `--tour levels,21`): Dry Canyon's exit vortex ("Returning home..." to Peace Keepers), then Gosnold the balloonist (dialogue, destination menu, the ride) to Magic Crafters.

**How the routes were made.** The routes were steered on the host in `--step` sessions (`tools/steer.py`, `--jump` over curbs; `--moby-dump` now lists the portals and each moby's facing). `tools/anchor_pad.py` then turned each session's `pad.txt` into state-anchored lines. A long walk doesn't replay the same in the emulator. The PS1 runs Artisans at a variable frame time (`g_DeltaTime` 2 or 3: 25 game ticks per 60 vblanks against the host's 30), so the first version of route A drifted, missed the portal and never left Artisans (`logs/run7-oracle-routeA-long`). Each route therefore starts its walks from a `poke:` of Spyro's position (0x80078A58): on the lawn 6,000 units before the portal, on the vortex's ledge, and on the balloonist's dock. From there Spyro walks for a few seconds. Everything that follows is the game's own code: the portal surface (type 6), the vortex moby, the balloonist moby (it arms only once Spyro has been 0x1000 away) and the gamestates. The vortex ledge is 3,600 units above Dry Canyon's start, and reaching it by play takes a long detour. The balloonist is 60,000 units from where the vortex drops Spyro in Peace Keepers.

**Result (2026-10-30):** both routes play through on the host and in PCSX-Redux, with the same sequence of gamestates and levels. Oracle replay of every translated function sampled per level and gamestate along them: 946 of 946 completed samples match (385 on route A, 561 on route B). Among them are the three functions no earlier run had reached:

| Function | Where it ran | Samples |
|---|---|---|
| `func_8004F000` | the sky in EXIT LEVEL (gamestate 10, Stone Hill) and on the balloon ride (12, Peace Keepers and Magic Crafters) | 16/16 |
| `func_8004F4BC` | a portal's sky when Spyro is near it (gamestate 0, Artisans and Peace Keepers); `func_80050BD0` draws it for portals within 0x3000, and `func_80050240` for those farther away | 22/22 |
| `func_8004F7E8` | inside `func_8004F4BC`: the portal sky's triangles | 22/22 |

Samples by gamestate (completed function samples, labels not counted): route A 26 in gamestate 1, 26 in 9 (entrance animation), 27 in 10, 51 in 2 (pause); route B 45 in 1, 126 in 12. The 63 NOEXIT lines are computed-jump labels (`D_…`), which are entered by jumps and never return. `D_8004F890`, the label inside `func_8004F7E8`, has 2 matching samples too.

| Phase (route) | Host vblanks | Oracle vblanks | Picture difference (+30, +90, +180, +300, +420) |
|---|---|---|---|
| Portal flight to Stone Hill (A, gamestate 1) | 402 | 468 | 13.0, 10.6, 7.9, 6.7, 7.8 |
| Entrance animation (A, 9) | 68 | 67 | 6.8 |
| EXIT LEVEL (A, 10) | 128 | 128 | 0.9, 2.6 |
| Flight home to Artisans (A, 1) | 418 | 454 | 10.5, 32.6, 31.0, 31.2, 31.9 |
| Vortex flight home to Peace Keepers (B, 1) | 402 | 448 | 6.2, 44.0, 49.8, 49.7, 26.2 |
| Balloonist in Peace Keepers (B, 12) | 666 | 669 | 4.7, 4.9, 4.6, 5.8, 6.0 |
| Balloon arrival in Magic Crafters (B, 12) | 702 | 820 | 4.2, 4.6, 4.5, 13.8, 24.3 |

The picture difference works as in the level table above. It is taken at the same vblank offset from the start of each phase: the host was rerun with `--shot` at the emulator's offsets (`tools/oracle/route_pairs.py --host-shots`). Sheets, host left: `logs/run18-host-routeA-merged/pairs.png`, `logs/run19-host-routeB-merged/pairs.png`.
- In EXIT LEVEL, the dialogue and the ride, the pictures match.
- **The flights home differ in the sky only.** The scene, Spyro, the camera and the HUD are the same. The host's sky turns into the homeworld's about 150 vblanks into the flight, the PS1's near the end. Load stage 9 (`src/loaders.c`) swaps `g_Cyclorama` for the destination's sky, and the stages before it wait for the CD (`CdSync`, `g_CdState.m_IsReading`). The host's CD reads finish at once, the PS1's take real time, which also makes its phases longer (468 against 402 vblanks). This explanation comes from the code, not from an experiment. A host rerun with `--cd-latency 15` never got past the title (its absolute new-game presses no longer land), so it wasn't pursued.
- Arrival in Magic Crafters (+300, +420) differs for the same reason: the PS1's ride lasts 118 vblanks longer.

Nothing broke in the game or the port. One oracle bug was found and fixed. `func_8004F7E8` returns through `.L8004FC3C`, whose delay slot does `addi $sp, $fp, 0`: this hand-written renderer uses `$sp` as its primitive cursor and returns with another `$sp`. `capture.lua` ends a sample at the first return to `$ra` with the entry `$sp`, so it missed the real return and took a much later one, 5 to 325 vblanks later. 10 of its 12 samples "failed" that way in `logs/run11-oracle-routeA`, whose `func_8004F4BC` samples (the code around it) all matched. A new `$ORACLE_FUNCS` option `anysp` ends a sample at the first return to `$ra`; with it, all 22 match.

Rerun:

```bash
logs/run18-host-routeA-merged/cmd.sh; logs/run19-host-routeB-merged/cmd.sh   # host, about 5 min each
logs/run13-oracle-routeA/cmd.sh; logs/run14-oracle-routeB/cmd.sh             # PCSX-Redux, about 6 min each
tools/oracle/replay.sh logs/run13-oracle-routeA; tools/oracle/replay.sh logs/run14-oracle-routeB
python3 tools/oracle/shot2png.py logs/run13-oracle-routeA/shot-*.raw
python3 tools/oracle/route_pairs.py logs/run18-host-routeA-merged logs/run13-oracle-routeA pairs.png --offsets 30,90,180,300,420
```

| Log | What |
|---|---|
| `logs/run1-step-portal` | First step session: Artisans to Stone Hill's portal by steering from the start, EXIT LEVEL (`exit-sheet.png`) |
| `logs/run3-step-vortex`, `logs/run4-step-balloon` | Step sessions for route B: the poke onto the vortex ledge and the vortex (`vortex-sheet.png`), the balloonist and the ride (`balloon-sheet.png`) |
| `logs/run9-step-portal` | Step session for the final route A (poke before the portal) |
| `logs/run6-host-routeB`, `logs/run10-host-routeA` | Host replays of the two pad scripts: the same vblanks as the step sessions |
| `logs/run7-oracle-routeA-long` | The emulator on the first route A (walk from the start): Spyro drifts and never reaches the portal |
| `logs/run8-oracle-routeB`, `logs/run11-oracle-routeA` | Emulator captures before `anysp`: route A's 11 failures are the `func_8004F7E8` sample ends above |
| `logs/run13-oracle-routeA`, `logs/run14-oracle-routeB` | **Final emulator captures**: trace, shots every 30 vblanks, samples per level and gamestate; `replay.txt` and `summary_replay.txt` |
| `logs/run18-host-routeA-merged`, `logs/run19-host-routeB-merged` | **Final host runs** on the tree merged with main (hand-written pete and camera C, presentation): the same gamestate sequence at the same vblanks; shots at the emulator's offsets; `pairs.png` |

## Limits

- **The endings are reached by pokes.** A poke writes what the game's own code writes on beating Gnasty Gnorc and on entering Gnasty's Loot's vortex with 14000 gems (D31). From the cutscene on, everything is the game's code. Beating the game by play (pad scripts for the boss fight, every gem) is M4 work. The ending pokes don't set `g_LevelVortexExitFlags` as the real moby code does, so the saved "levels completed" flags are all 0.
- **Traces can't be compared tick for tick here**, as they are for the attract demos. The PS1 drops frames while Spyro glides into a level and the host doesn't (D31); PCSX-Redux `a5d621af` has no CPU overclock setting that would stop it. `tools/oracle/level_diff.py` lines visits up by tick and shows where they part, typically on the first ticks.
- **The late-game save was only checked on the host.** The emulator's fresh memory card is unformatted, and the scripted presses don't leave it a save slot. So its fairy scene is short (185 vblanks against the host's 434, as in M2's `cap5-fairy`) and nothing is written. Comparing a save across both sides needs a formatted card image for the oracle.
- **Not covered by the tour:** gameplay beyond standing at each level's entrance. Portal entries, EXIT LEVEL, vortex exits and the balloonist are covered by the routes above, once each: one portal, one vortex, one ride, with pokes before the walks.

# PAL timing: stage R3 of the regions plan

Stage R3 of `docs/regions-plan.md` (§3.1, §6.3, §7.4, §9). The PAL release (SCES-01438) keeps NTSC's tick (two vblanks) at 50 Hz, so its logic runs at 25 Hz, and its programmers rescaled the game's constants to keep real-time speed: speeds ×1.2, accelerations ×1.44, durations ×5/6. R3 puts every one of those sites in the shared source as `RT(us, pal)` (`include/region.h`, D133), and gives the platform a 50 Hz vblank clock. Decisions: D135 (the method, the main executable, the hand C), D137 (the level overlays); the platform's 50 Hz clock and PAL 60 (D138) are their own PR.

## The tool: `tools/regions/retune.py`

| Command | What it does |
|---|---|
| `sites exe\|wad TARGET REGION` | Aligns every US function of the matching build's ELF with its counterpart in the release (`compare_code.py`'s matcher, then difflib on the masked words) and compares the aligned instructions **strictly**: only immediates that are or may be addresses are masked (j/jal, `lui` unless an `ori` follows, and `addiu`/`ori`/loads/stores whose base came from a `lui` or is `$gp`). So `addiu v0, v0, 192` (a speed added) is compared, which `compare_code.py --exact` doesn't. It lists each differing immediate (a **site**: both values and the ratio class) and the instructions difflib couldn't pair (**unpaired ranges**: inserted or changed sequences). US against itself: 580 of 581 functions strictly the same (the 581st is a 2-word stub the matcher places at the wrong duplicate). |
| `lines --dis DIR` | Every instruction's source line, from `objdump -dl` of the build's objects (the decomp compiles with `-g3`, so the line tables are there). |
| `propose` | For each site, the literal on its line whose value (or negation, or ±1, for `x <= n` and `x -= n`) the instruction holds. A literal changes only if every instruction it compiles to is a site with one PAL value; one that is a site somewhere and unchanged elsewhere is a conflict, listed. |
| `apply` | Writes the proposals into a tree as `RT(us, pal)`, the US literal spelled as it was. |
| `patch` | Writes an edited tree as patches both builds can apply (below), checking placement. |
| `rtcheck A B` | The `RT(us, pal)` pairs, file by file, in two trees: the same, or what differs. |
| `context FUNCTION` | A function's sites and unpaired ranges with the US asm and the release's disassembly around each, for mapping sites into hand C by reading. Its output holds disassembly of the player's disc: it stays out of the repo. |

`tools/build_matching.sh REGION=pal` now also writes `compare-exe-strict.txt` and `compare-wad-strict.txt` (the `sites` lists of the PAL build against the PAL disc).

## Three patch folders

The region edits are written once for the decomp's C, as D133 set up: `patches/spyro-1-region/` goes after `patches/spyro-1/` in the matching builds and after `patches/spyro-1-port/` in the port. But the port's own patches rewrite some of the lines R3 edits: the 60 Hz pilot's `T60_TICK_DO(props->m_0x14 -= 8)` and `T60_SHARE(8)` (D130), and the skipped logos' `while (LOGOS && …)`. Such an edit is written twice:

- `patches/spyro-1-region-matching/` — `tools/build_matching.sh` only, on the decomp's line;
- `patches/spyro-1-region-port/` — CMake only, on the port's own line (inside the `T60_*` macro: `T60_SHARE(RT(8, 10))`).

`retune.py patch` decides which: a change whose lines and context are the same in the port's tree (paired by difflib) is shared; the rest are split. It then applies every patch to copies of both trees and requires exactly the expected files, and `rtcheck` holds both trees to the same `RT` pairs. In the main executable 6 edits are on such lines (`initialization.c` 2, `update.c` 1 with its include, `moby_helpers.c` 3).

## The main executable (D135)

### Decomp C (`patches/spyro-1-region*/0004-pal-timing-main.patch`)

| Function | Sites |
|---|---|
| `Initialize` (`initialization.c`) | the two logo holds, 210 → 160 vblanks |
| `CDLoadSync`, `CDLoadAsync` (`cd.c`) | the read timeout, PAL's new code: `maxTime` below 6, else `(maxTime * 5 + 3) / 6`, written as PAL's `if`/`else` (`if (RT(1, maxTime < 6))`) |
| `func_800189F0` (`draw.c`) | a tracer's age, `<= 80` → `<= 67` |
| `func_800334D4` (`update.c`) | the ending cutscenes' start ticks, 720 → 600 and 1168 → 972 |
| `HudTick` (`hud.c`) | the gem counter's steps: `>= 3` → 2, `> 200` → 160, `> 20` → 16 |
| `func_80035F58`, `func_80037714` (`camera.c`) | L2/R2's turn ±1024 → ±1229; the fly-in's −8960 → −10752 |
| `MobyMoveWithGravity` (`moby_helpers.c`) | 500 → 600, terminal velocity −260 → −311 |
| `MobySpawnDrop` (`moby_helpers.c`) | initial vertical speed 300 → 360 and three of the four 140s → 168 (the mode 3/4 branch keeps 140, PAL's code shows), the drop's −10 → −12 |
| `func_8003CCE4`, `func_8004888C`, `func_800499C0`, `func_8004A200` (`pete.c`) | 13 literals: timers ×5/6, a speed, the gravity ramp |
| `func_8003E90C` (`pete.c`) | Spyro's push off a ledge: `x >> 4` → `(x * 307) >> 12`, ×1.2 (found by R2, D134) |
| `func_8002BFE0` (`cutscene.c`) | the cutscene's animation index `/ 60`, `% 60` → 50: PAL's cutscene data holds 50 frames an animation (2 s at 25 Hz), a data layout that stays PAL's in PAL 60, so `RV(60, 50, 60)`, not `RT` (found by R2, D134) |

With R1's five sites in `HandleSpyroDamage` and `func_80041270`, those are every timing site of the main executable's decomp C. Against the PAL disc, with R2's first part (D134) on main, **16 of these 18 functions are strictly PAL's**; `func_800334D4` (the 512×256 clear) and `HudTick` (the egg model's Y) keep only R2's changes.

### Hand C

These functions are hand-written C in the port (`src/game/hand_index.c`), from asm the decomp hasn't matched, so no compiler can map their sites; each was mapped by reading the aligned asm (`retune.py context`). The maps, site by site, are in `docs/pal-retune/`:

| Function | File | Sites | Changed sequences |
|---|---|---|---|
| `func_80043FE4` (Spyro's controller) | `pete_hand.c` | 176 of 178 | 34 of 60 unpaired ranges are code changes, all implemented (a new `PalSpeed`: PAL multiplies a square root by 4915/4096; changed multipliers); 26 are branch offsets |
| `func_8003EA68` (state changes) | `pete_state_hand.c` | 64 of 64 | 20 of 24 ranges are noise; 4 are PAL behaviour changes (below) |
| `func_80041670` (state transitions) | `pete_transition_hand.c` | 89 of 89 | none |
| `func_8003FE40` | `pete_state_hand.c` | 3 of 3 | none |
| `func_80034CE8`, `func_800357A4`, `func_80035FB4` (camera) | `camera_hand.c` | 17 of 17 | none |

The two sites of `func_80043FE4` left alone (116 and 117) aren't timing: the wing trail's vertex indices differ (US 9 and 9 + 0x5B, PAL 89 and 89 + 11), a model difference that needs `RV()`, not `RT()`.

## The level overlays (D137)

`patches/spyro-1-region*/0005-pal-timing-overlays.patch`: 1,134 literals in `moby_update.c` (1,106), `moby_spawn.c` (26) and `particle_spawn.c` (2), the code every level overlay is built from (`overlays/overlays.yaml`). A moby class is one source for every level that has it, so one literal is often the site in several overlays, and it changes only if they all agree. 65 of the edits are on lines the 60 Hz pilot rewrote (`T60_SHARE(RT(8, 10))`).

Two passes of `retune.py propose`, taking only the proposals whose PAL/US ratio is a timing one (×1.2, ×1.44, ×5/6, ×3/4). The first pass used the US build's line tables. The second used the first pass's `REGION=pal` build and looked two lines either side.

| Over the 35 level overlays, against the PAL disc | Before R3 | After |
|---|---|---|
| Timing sites in R3's three sources | 3,519 | **542** |
| Constant sites, every kind, all 37 overlays | 5,501 | 2,535 |
| Unpaired instructions (US side) | 17,823 | 18,225 |

No function gained a site. No overlay function is strictly the same yet: every `UpdateMoby` keeps PAL's inserted code (the unpaired instructions), which is the next piece of work. The 542 sites left are listed by line in `docs/pal-retune/overlays-left.md`; each needs reading (a macro, a value computed from others, or a literal PAL changed in some levels only).

**Not taken, for R2:** `balloonist.c` (its dialogue is rewritten for the five languages; 70 timing sites) and `flight.c`'s results screen (its text layout), and the proposals with no timing ratio (the 256-line picture's layout, alignments in rewritten code).

## Not R3's

| What | Where | Whose |
|---|---|---|
| `InitFairyCutscene`'s menu offset, view distance and angle | `init.c` | R2 (the PAL fairy menu's layout) |
| `HudTick`'s egg model Y 1008 → 1208; `func_800334D4`'s 512×256 clear | `hud.c`, `update.c` | R2 (the 256-line picture) |
| PAL's three added sounds (the jump, state 8, the knockback) and a supercharge gravity of −3840 when the level is 43 | `func_8003EA68` (`pete_state_hand.c`, marked with comments) | R2's shared fixes, or R4: not timing |
| The wing trail's vertex indices | `func_80043FE4` (`pete_hand.c`, marked) | R2: `RV()` |

## The platform

The 50 Hz vblank clock, the CD and SPU at 50 Hz, and PAL 60 are D138 (their own PR).

## Checks

- **The decomp's C:** `REGION=pal tools/build_matching.sh`, each edited function against the PAL disc (D135, D137 list the counts). The US build is `PSX.EXE SHA-1 OK` with every R3 patch.
- **The hand C:** the maps in `docs/pal-retune/`. The host runs the PAL disc to its title (R2's first part, D134) but not yet into a level (PAL's title overlay is R2's), so the PCSX-Redux traces of Spyro's speed against the host at 25 Hz (plan §9) come with R4.
- **US unchanged:** `tools/host_checks.sh`, 279 files byte-identical to main's.

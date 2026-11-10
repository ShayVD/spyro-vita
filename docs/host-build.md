# Host debug build (linux32)

The 32-bit Linux build runs the whole game headless in Docker: virtual vblanks, a pad script, PNG screenshots, gdb. Decisions D14–D19 explain the choices.

## One-time set-up

```bash
git submodule update --init --recursive
ln -s /path/to/main/checkout/disc disc   # in a worktree; disc/ is gitignored
tools/build_matching.sh                  # must print "PSX.EXE SHA-1 OK"; generators read its psx.map
docker build -t spyro-host -f tools/docker/host.Dockerfile tools/docker
tools/tc cmake -S . -B build/linux32 -G Ninja -DSPYRO_HOST=linux32 -DCMAKE_TOOLCHAIN_FILE=cmake/linux32.toolchain.cmake
```

## Build, test, run

```bash
tools/tc cmake --build build/linux32
tools/tc ctest --test-dir build/linux32 --output-on-failure
tools/tc build/linux32/src/platform/linux32/spyro --disc disc/files --frames 2600 \
    --shot-every 60 --shot-dir logs/runN-x --pad logs/runN-x/pad.txt \
    --save logs/runN-x/save --log logs/runN-x/spyro.log
python3 tools/contact_sheet.py logs/runN-x
```

- `--disc DIR`: the extracted files (`disc/files`, with `manifest.json`) or a folder holding the disc image (`disc/image`: a `.cue` and its `.bin`, or a `.bin`/`.img` alone; decisions D60). Both give identical runs. A refused image exits 1 with the Vita's panel text on stderr, and with `--shot-dir` writes the panel as `disc-error.png`. `--disc-marker FILE` also hashes `WAD.WAD` the first time and remembers the image in FILE, as the Vita does (without it only `SCUS_942.28` is checked). `tests/cd/image_variants.py` checks the dumps players have and every refusal, and with the PAL and Japanese dumps given (`disc/image-pal/…cue:disc/files-pal`) their "NOT SUPPORTED YET" panels (see its docstring; D132); `ctest -R '^cd$'` checks the image's file table against `disc/files/manifest.json` and every file's SHA-1 through both layouts. `DISC=disc/image tools/host_checks.sh PREFIX` and `DISC=disc/image tools/oracle/replay.sh DIR` run from the image.
- Time is virtual: `--frames N` counts vblanks (60 per second; the game ticks every 2). Runs are deterministic.
- Pad script lines are `<vblank> <button>[+<button>...]` or `none`. Buttons: `select l3 r3 start up right down left l2 r2 l1 r1 triangle circle cross square`. Each state holds until the next line.
- `--save DIR`: memory cards live in `DIR/card0` (port A) and `DIR/card1` (port B, present only if that directory exists). For a PAL or Japanese disc they live in `DIR/pal/` or `DIR/jp/` instead, as on the Vita (D132; those discs are refused until their stages land, `docs/regions-plan.md`).
- `--cd-latency N`: CD reads complete N ticks later instead of inside `CdRead` (D17).
- `--cd-native`: the native disc reads (D93, the Vita's `cd_native=1`): each `CdRead` is a read of a file at an offset on a worker thread, and the XA stream is read ahead from its start; the same bytes at the same ticks, so every output is the same as without it. `--cd-stats` logs the Vita's `cd:` line at exit (file-system reads, KB and ms, `CdRead`s and the ms the game thread waited for them; with `--cd-native` also the stream's chunks): the container's times, so off by default to keep the logs deterministic.
- `--trace FILE`: one TSV row per vblank (gamestate, level, tick, Spyro's position, state and health, gems, dragons, lives, rand seed). It's the host side of the oracle's frame-trace diff.
- `--step DIR`: closed-loop driving. The game stops and writes `DIR/now.png`; `tools/step.sh DIR "up+cross 30"` holds buttons for N vblanks and waits for the next stop. Every step is appended to `DIR/pad.txt`, so `--pad DIR/pad.txt` replays the session exactly. `tools/step.sh DIR quit` ends it.
- `--poke V:ADDR=VALUE`: writes a 32-bit word to PS1 RAM at vblank V (hex address and value), for test set-ups. With the PS1 layout (D23) addresses come straight from `psx.map`: `g_SpyroLifeCount` is `8007582c`, `g_Spyro` `80078a58` (+8 is z).
- `--wav FILE`: the SPU mix (44.1 kHz s16 stereo) as a WAV file.
- `--analog`: the pad is a DualShock in analog mode (decisions D46), as on the Vita. Pad script lines may then also set the sticks, `lx=N ly=N rx=N ry=N` (0..255, 128 centred) among the buttons (`100 up+rx=255`); sticks a line doesn't name are centred. `--free-camera` turns on the right-stick camera (D47), with `--cam-invert-x`, `--cam-invert-y` and `--cam-speed PERCENT`. All off by default, so replays and oracle checks are the original's.
- `--state V:save:N` / `--state V:load:N` (repeatable): at vblank V, ask for a save state of slot N, or a load (decisions D51); it happens at the next main loop top, in the `--save DIR` (`DIR/stateN.bin`). A load sets the vblank counter back to the state's, and the pad script's absolute lines play again from there. `--state-hash FILE` writes a hash of each save-state chunk at every main loop top (slow: the whole state each tick). `tools/savestate_compare.py REF RUN` compares a run that loaded a state with the run that saved it: hashes per loop top, `audio.wav` after the load, and shots.
- `--tri-dump V`: logs every triangle drawn after vblank V; `tools/tri_at.py LOG X,Y` lists those covering a screen point. `--vram-shot` also writes the raw VRAM (`vram-N.bin`) for `tools/vram.py`.
- `--peek ADDR:N`: logs N words of PS1 RAM from hex ADDR with every shot (up to 8 of them).
- `--tour LIST` (`--tour-stay N`): after the pad script, visits levels with the game's level-warp cheat, types the credits, LEVELS OPEN or 99 LIVES cheat, or sets up an ending (`end2`, `end3`), as the oracle's `ORACLE_TOUR` does (`src/platform/linux32/tour.c`, decisions D31). Leaving a level writes `tour-K-LEVEL.png`. `--pad-after FILE` is a pad script that starts once the tour is done.
- `--cheat V:NAME` (`lives`, `levels`, `credits`, `warp:LEVEL`): at vblank V, what the Vita menu's GAME rows do: queue the game's own cheat, which the game then takes in its inventory (decisions D50). The log says `cheats: ... queued` and `... applied in the inventory`, or `refused` outside a level (title, demos, loads). Up to 8.
- `--skip-logos`: start at the title, as the Vita's `skip_logos=1` (no Sony or Universal logo, no first title flythrough; D50).
- Pad scripts may also have state-anchored lines, `@gs=G,level=L +N buttons`, with the same meaning as in the oracle's (`src/platform/linux32/pad.c`). In place of the buttons, `poke:ADDR=VALUE` (hex) writes a word of PS1 RAM when the line fires and leaves the buttons alone; `capture.lua` reads it the same way.
- Crash backtrace: `tools/tc tools/gdb_run.sh "" -- --disc disc/files --frames N` (add `GDB_AFTER="frame 1;x/8wx \$sp"` for more). A guest crash under plain `tools/tc` leaves the container idle in `sigsuspend` while qemu writes `qemu_spyro_*.core` into the worktree; `gdb-multiarch` with `core FILE` reads it.

## The Vita's memory model on the host (D97)

`-DSPYRO_ARENA_AT=0x8F000000` (any 16 MB-aligned address above 2 GB other than 0x80000000) builds the host with the Vita's moved arena (D25): the data labels through pointers, the three aliases of PS1 RAM in translated code, and `MIPS_ARENA_TAG` with another top byte. Keep it in its own build folder:

```bash
tools/tc cmake -S . -B build/linux32-moved -G Ninja -DSPYRO_HOST=linux32 -DCMAKE_TOOLCHAIN_FILE=cmake/linux32.toolchain.cmake -DSPYRO_ARENA_AT=0x8F000000
tools/tc cmake --build build/linux32-moved
BIN=build/linux32-moved/src/platform/linux32/spyro tools/host_checks.sh PREFIX
```

Its runs must give the default build's traces, shots, audio and memory cards byte for byte. Logs differ only by the `arena at 0x8f000000` line. The aliases' regions (0x80000000 and the arena's address with bit 31 cleared) are mapped `PROT_NONE`, so nothing else can be placed where the aliases point. `--replay` needs the default build, with RAM at 0x80000000. A state saved by one configuration is refused by the other: the default build said "STATE 1 IS FROM ANOTHER BUILD" to the moved build's state (`logs/s4-state-cross`). The build ID is checked before the arena base. This is the host side of stage 4's checks (`docs/native-plan.md`).

## Native memory (D100, D106)

`-DSPYRO_NATIVE_MEM=ON` builds the native configuration: no mips2c units, the data labels as ordinary globals (one section laid out as PS1 RAM from 0x8006BBE0 to main_BSS_END), the game's memory one block from the heap (overlay space, main_BSS_END to the top of RAM, behind the rest of main_BSS_END's page), and the scratchpad its own array:

```bash
tools/tc cmake -S . -B build/linux32-native -G Ninja -DSPYRO_HOST=linux32 -DCMAKE_TOOLCHAIN_FILE=cmake/linux32.toolchain.cmake -DSPYRO_NATIVE_MEM=ON
tools/tc cmake --build build/linux32-native
BIN=build/linux32-native/src/platform/linux32/spyro tools/host_checks.sh PREFIX
```

Its runs must give the default build's files byte for byte. `-DSPYRO_ARENA_AT=0x52F81000` (any page) puts the block at that address instead of the heap's. `--poke`, `--peek` and pad-script `poke:` still take PS1 addresses, in the labels or overlay space. `--replay` and `--fast-render 0` need the default build. Native save states are format 2 (D106).

`-DSPYRO_MEM_AUDIT=ON` (on the default build) lists, at exit, every site that decoded a packed address (an OT link, a frame pointer, a subdivision header) outside overlay space, with the PS1 addresses it found: the things the native block would have to hold. Over the host checks it finds only the OT terminator, 0x8006BBE0 (D106).

## Draw distance (D49)

Optional, off by default (`docs/presentation.md` "Draw distance"):
- `--draw-distance X`: the world's detail and the mobys drawn X times further (1.5, 2), picture only.
- `--fast-render 0|3`: `3`, the default since D127 (the Vita's since D124), the hand-written renderers as readable C: no GTE data registers or commands, no PS1 scratchpad, spill area or hand stack (`src/game/render_hand.h`, D124); `0` (the arena build) the renderers as translated from the asm and PsyCross's GTE decoder. The register-file builds of `1` (D72) and `2` (D80, with `--ngte-fx`, D98) went in D127, with `--render-poison` (D124's audit of them) and `--gte-audit`. `--fast-render-check` (the arena build) runs each call of a hand-written renderer, or of a unit function written by hand, after its mips2c unit from the same state and compares them: RAM but the scratchpad, spill area and stack, the control registers but FLAG (the B1 functions: everything); with `--render-ctx-check` too the hand C's call is the presenter check, so one run does both. `--particle-sweep` (with the check, D95) gives the live particles the types 4 to 7, which no run reaches. The oracle replay runs each renderer call against its mips2c unit first (`rendershadow` lines, `SPYRO_REPLAY_RENDERSHADOW=0` turns it off), and `SPYRO_SHADOW=render` does the same over a whole run. `tools/native_draw_ab.sh` takes the modes `frc`, `frc0`, `plain` and `all3` (the per-call, presenter and walk checks in one run). `tools/fast_render_ab.sh PREFIX` runs scenarios with `--fast-render 0` and `3` and compares every file; `EXTRA_B` goes to the second run of each pair (`EXTRA_B=--fast-render-check`). `--prof-draw` logs each renderer's CPU time at exit (relative only under qemu). A `-DSPYRO_M2C_PROF=ON` build counts every translated instruction's executions; `tools/mips2c/prof_report.py` ranks them. `--native-draw`: the hand renderers' primitives go to the OT walk as native draw lists (`src/game/native_draw.h`, decisions D82); `--native-draw-check` runs each of their calls both ways and puts both packet streams through PsyCross's own walk, which must give the same vertices and splits ("native draw check" lines at exit; a call whose RAM differs names the first differing arena offset and the primitives' range). Since D86 the hottest sites build the records from values and skip the primitives' stores; since D91 a record is the primitive's own words, which the walk converts. The host's default stays `native_draw` 0 (the Vita's is 1 since D91), so the scripts' runs are unchanged. `--nd-ab`: `native_draw` 0 and 1 in turn by blocks of 8 lists (the Vita's `nd_ab=1` timing run). `tools/native_draw_ab.sh PREFIX [SCENARIO...]` runs scenarios without it and with the check, from one binary, and compares every file; each run gets its own card folder (`--save`), and `ND_ARGS` goes to the second run of each pair. `--nd-direct` (D99, with `--native-draw` or `--native-draw-parse`): the walk leaves its triangles in the Vita GL's lean form (`src/platform/common/psx_lean.h`); the software rasterizer doesn't read what changes, so every file is the same. `--nd-direct-check` parses every packet the walk parses itself both ways from the same state and compares ("native draw direct check" lines at exit; what stays is the direct way's with `--nd-direct`); not with `--vcache` or the other two checks. `MODE=dir` (`native_draw=1`) and `MODE=dir2` (`native_draw=2`) of `tools/native_draw_ab.sh` run them against the plain run. `--gpu-clear` (D111): `ClearImage`'s rect filled row by row (`src/platform/common/psx_fill.h`, the Vita's `gpu_clear=1`), the same pixels; `tests/fill` compares the fill with the plain loop (`tests/fill/arm_check.sh`: with the Vita's compiler, NEON, under qemu-arm).
- `--lod-stats N`: a `lod N:` line every N vblanks: the scale reached, primitive-buffer use (average, peak, of what size), back-offs, extra mobys per draw, the near-sector list's peak, the moby record peak, the largest single sector and what is drawn after the world (the hard bound's margin), and how often a bound stopped a loop. Also with the option off (baseline numbers).
- `--lod-guards-only` (tests): no limiter and no reset at a new level, so only the hard bounds keep the buffer.

## Widescreen and 60 fps (D30)

Optional, off by default (`docs/presentation.md`):
- `--widescreen`: 16:9, with the logic kept at the 4:3 frustum.
- `--interp`: an extra in-between frame per tick, so 60 fps. `--interp-camera` blends the camera only.
- `--widescreen` draws once in 16:9 (the wider view reaches the logic) except in attract demos, which keep the exact 4:3 logic. `--widescreen-exact`: the exact two-draw form always. `--widescreen-logic`: one draw always (diagnostic: the demos desync).
- Any of these turns on `--out960`, which writes 960×544 pictures (shots and `--serve`).
- `--present-stats` logs per-tick costs at exit. (`--present-dirty` and stage 7a's audits, `--present-audit` and `--present-reads`, D104, measured D30's pass and went with it: D129.)
- The presenter (stages 7e-7g, D125, D126, D128, D129): the game's draw code built a second time over a snapshot (`src/game/pres/`, `cmake/presenter.cmake`) draws the in-between frame (`--interp`) in every gamestate and the attract demos' 16:9 frame (`--widescreen`); D30's pass over the game's memory did until D129, and `--interp-mode 2` chose the presenter before that. `--pres-check` (since D129): each tick the presenter also draws from the snapshot unblended, and its list is compared with the logic draw's record for record ("pres check" lines at exit, by gamestate); every file is the same as without it, with or without `--interp`. `--pres-check-thread` records the presenter's list on a thread beside the logic draw, `--pres-check-late` after it (D128). `--pres-write-check` copies the game's memory around each presenter frame and logs any byte it changed ("pres write check" lines, `src/platform/linux32/pres_watch.c`, D128). `--pres-audit` counts the snapshot's words that point into the moby array or the particle space ("pres audit" lines). `tools/pres_check_ab.sh PREFIX [SCENARIO...]` runs a reference (`I_BIN=` another binary, e.g. one from before D129 for D30's frames) and a run with the check (`MODE=chk`, `EXTRA=` more options; `ND=1` with `--native-draw`); scenarios include `pause` (the pause menu and inventory, 16:9), `die` (a respawn, a dragon rescue, a game over) and `flight` (Sunny Flight's results). The presenter isn't built with the moved arena (`-DSPYRO_ARENA_AT`, D97): there is no in-between frame there, and the attract demos are 4:3.
- `tools/side_by_side.py OUT.png A.png B.png … [--cols N] [--scale F] [--crop X,Y,W,H]` puts shots next to each other.

## Playing it yourself (D22)

```bash
TC_PORT=8642 tools/tc build/linux32/src/platform/linux32/spyro --disc disc/files --serve 8642 --save save
```

Then open http://localhost:8642/ . Keys: arrows (D-pad), X (cross), Z (square), C (circle), S (triangle), Enter (start), Backspace (select), Q/E (L1/R1), 1/3 (L2/R2). A gamepad works too (browser Gamepad API). Click "Sound on" for audio. Stop it with Ctrl-C, or `docker stop` on the container.

## Finding and reaching things in a level

`--moby-dump` writes the level's mobys (index, class, position, state, facing `rot_z`), Spyro's position, the gem count, the special surface types and the portals (`# portal` lines: centre and level): `mobys-V.tsv` next to every shot, and `DIR/now.tsv` at every `--step` stop. `tools/steer.py DIR X Y` steers Spyro there in a step session (UP, plus LEFT or RIGHT toward the target; `--jump` jumps when he stops moving, over curbs); other jumps go through `tools/step.sh` directly, and `tools/step.sh DIR poke:ADDR=VALUE` writes RAM at the stop (putting Spyro somewhere). The session's `DIR/pad.txt` replays it. `tools/anchor_pad.py TRACE PAD --from V --to V` turns that absolute script, with the session's `--trace`, into state-anchored lines that replay on both the host and the emulator (`tools/oracle/pads/route_*.txt`, docs/whole-game-host.md "Portals, exits and the balloonist").

## Oracle: PCSX-Redux (D8)

The original game runs in PCSX-Redux, out of process, driven by Lua scripts in `tools/oracle/`. The emulator is the official macOS Arm build in the gitignored `tools/oracle/bin/` (version and hashes in `docs/decisions.md` D8). It runs at real time, about a minute per 3,600 vblanks, and boots the BIOS first: its title starts 1,520 vblanks after the host's.

```bash
# Sample translated functions and trace the game state through the attract demos
D=logs/runN-oracle; mkdir -p $D
python3 tools/oracle/funcs.py build/linux32/gen/mips2c/m2c_index.c > $D/funcs.txt
ORACLE_OUT=$D ORACLE_FUNCS=$D/funcs.txt ORACLE_TRACE=$D/trace.tsv ORACLE_SAMPLES=4 \
  ORACLE_GAP=150 ORACLE_FROM=1100 ORACLE_TO=14500 tools/oracle/run.sh tools/oracle/capture.lua
# Replay the samples through the host's translated C and compare
tools/oracle/replay.sh $D
# Compare an attract demo tick by tick with a host --trace
python3 tools/oracle/trace_diff.py HOST/trace.tsv $D/trace.tsv --key tick --where gamestate=0,level=11
```

- `capture.lua` also takes `ORACLE_TOUR` entries `credits`, `levels`, `lives`, `end2`, `end3` (decisions D31), `ORACLE_PAD_AFTER`, `ORACLE_LABELS` (count hits on computed-jump labels inside completed samples, `labels.txt`), `ORACLE_WATCH=ADDR:LEN` (log game stores into a RAM range) and `ORACLE_SAMPLE_TIMEOUT`; `$ORACLE_FUNCS` lines may end in `n=K` (sample budget) and `gs=G` (only in that gamestate, for overlay code); `anysp` ends a sample at the first return to its `$ra` even if `$sp` differs (hand-written asm that uses `$sp` as data, `func_8004F7E8`).
- `capture.lua` (environment variables): `ORACLE_FUNCS`/`ORACLE_SAMPLES`/`ORACLE_GAP`/`ORACLE_FROM` sample function calls; `ORACLE_TRACE` writes the same TSV as the host's `--trace`; `ORACLE_SHOTS`/`ORACLE_SHOT_EVERY` save the display (`tools/oracle/shot2png.py`) and VRAM; `ORACLE_PAD` drives pad 1, with absolute lines shifted by `ORACLE_PAD_SHIFT` and gameplay lines anchored to the game state (`@gs=0,level=10 +71 up`). Scripts are in `tools/oracle/pads/`. `ORACLE_TOUR=11,12,...` (with `ORACLE_TOUR_STAY`) then warps from level to level with the game's level-warp cheat, and `ORACLE_PER_LEVEL=1` makes `ORACLE_SAMPLES` a per-level budget (`ORACLE_PER_STATE=1`: per level and gamestate).
- `spyro --replay DIR [--only NAME] [--ignore FILE] [--fnmap FILE] [--disc DIR]` loads each sample's entry state into the arena, the GTE and (host build only) all the registers of the call, calls the function's translated C and compares v0/v1, RAM, scratchpad and GTE registers. `--fnmap` (`tools/oracle/fnmap.py`, built by `replay.sh`) turns PS1 function pointers in the emulator's RAM into the host's functions. `SPYRO_REPLAY_NOFORK=1` runs samples in process, for gdb (`handle SIGALRM stop print` to catch one that hangs); `SPYRO_REPLAY_TIMEOUT` (default 60 s) reports a sample that never returns as a crash; `SPYRO_REPLAY_DUMP=FILE` writes the host's RAM after the call. `--disc` lets samples that read the CD (the credits overlay) do so. Function and scratchpad pointers in the emulator's RAM are rewritten only below the loaded overlay's end (`TERM` lines of the fnmap): above it is level data, where a word can equal such a pointer by chance. `tools/oracle/ignore_ranges.txt` lists, with reasons, the RAM that isn't a function output (the asm spill area, state written by interrupt handlers, libcd's DMA'd sectors) and the functions whose return registers are dead.
- Reaching a function's other paths: a line of `ORACLE_FUNCS` can take options after the name. `n=N` and `gap=G` override the sample count and spacing. `key=ADDR:SIZE` gives each value of that RAM word its own budget (`80078AD0:4`, Spyro's state, samples every state that occurs); several `key=` budget each combination. `if=ADDR:SIZE!=V` (or `=V`) samples only calls where the condition holds (`80078760:1!=0`, the flame on); several `if=` must all hold. During level-tour stays, `ORACLE_TOUR_PAD` plays a pad pattern on repeat (`tools/oracle/pads/tour_moves.txt`: run, flame, charge, jump, glide, look, camera buttons). `ORACLE_TOUR_POKE=ADDR=VALUE,...` writes words 60 vblanks into each stay (`80078768=300` starts a fairy kiss). The command for `logs/run10-handcov` is in its `cmd.txt`. The tour can stall in a flight level, so put those last. Flight-level stays are capped at 60 vblanks unless `ORACLE_FLIGHT_STAY` raises it; `tools/oracle/pads/flight_moves.txt` keeps Spyro flying there (loops, turn-arounds); one flight level per capture is the reliable way (`logs/run8-fly*/cmd.sh`).
- `tools/oracle/sample_fields.py --func NAME DIR... SYMBOL[+OFF][:SIZE]...` prints game fields at each sample's entry, to see which paths the samples cover.
- Differential sweeps, for paths no capture reaches (decisions D33): `SPYRO_REPLAY_SWEEP=ADDR=LIST` (e.g. `80078AD0=0-44`) replays each sample of a hand-written function once per value, with its mips2c unit as the reference: that needs a build configured with `-DSPYRO_M2C_REF=ON` (`build/linux32-ref`), which links the units as `<name>_gen`. `SPYRO_REPLAY_POKE=ADDR=VALUE,...` sets other words first (`80078C7C=@80075828` copies `g_LevelMobys` into `m_mobyInUseBySpyro`). `SPYRO_REPLAY_SWEEP_REG=REG=LIST` sweeps an entry register instead (`a0=0-44`: `func_8003EA68`'s new state), with POKE as above. Pass them through `tools/tc env ...` (unset, not empty, when unused); the suites on that build: `tools/oracle/sweep_pete.sh CAPTURE OUTDIR [BUILDDIR]` (`func_80043FE4`, `func_80047B60`; `SETS="base fly disp"`), `tools/oracle/sweep_camera.sh CAPTURE OUTDIR [BUILDDIR]`, `tools/oracle/sweep_state.sh OUTDIR` (`func_8003EA68`, `func_8003FE40`, over `logs/caps/`; totals by `tools/oracle/sweep_totals.py OUTDIR`) and `tools/oracle/sweep_transition.py CAPTURE OUTDIR` (`func_80041670`). `tools/oracle/sweep_all.sh` runs them all.
- The hand-written collision (`src/game/collision_hand.c`, decisions D83) keeps nothing in the scratchpad or the GTE, so on a build with the units linked (`build/linux32-ref`) the replay runs a shadow: every collision call also runs its mips2c unit, RAM and the result must agree (`colshadow` lines and a FAIL otherwise), and the unit's scratchpad and GTE are left behind so samples still compare whole (`SPYRO_REPLAY_COLSHADOW=0` turns it off). `SPYRO_COL_FUZZ=N` adds N jittered variants of each call, half aimed at a nearby moby's collision model. `tools/oracle/replay_all.sh OUTDIR [BUILDDIR]` replays every set in `logs/caps`; `tools/oracle/col_fuzz.sh OUTDIR [N]` fuzzes every collision sample. The same holds for `math.s` and `42CC4.s` (`math_hand.c`, `42CC4_hand.c`, decisions D87): `mathshadow` lines compare each math call's result, output and the GTE's rotation and translation (`SPYRO_REPLAY_MATHSHADOW=0` turns it off), and `mobyshadow` lines each 42CC4 call's RAM, result and GTE control registers (`SPYRO_REPLAY_MOBYSHADOW=0`); a sample's `shadow N` counts all three. `SPYRO_MATH_FUZZ=N` runs N rounds of every math entry with random arguments at each sample's first math call; `SPYRO_MOBY_FUZZ=N` N variants of each shadowed `MobyUpdateShadow`, `func_800529E4`, `MobyGetAnimatedVertex` and `func_80052F38` call. Outside the replay, `SPYRO_SHADOW=col,math,mobys,gs` (any of them) runs those shadows over a whole host run on the same build, every call, and prints `shadow: col N, math N, mobys N, gs N mismatches` at exit; `gs` is the gamestates 8 and 11 update (`gamestates_hand.c`), whose unit runs in a forked child (`gsshadow` lines), since they act outside RAM too. `hand` is decisions D94's: the cyclorama set-up, the moby lists, the particle slots, the images, `func_8003BFC0` and the credits overlay (`src/game/hand_shadow.{h,c}`, `handshadow` lines; on by default in the replay, `SPYRO_REPLAY_HANDSHADOW=0` turns it off). Those that act outside RAM (`func_800522C0`, the cyclorama set-up, the credits) run their unit in a forked child. At exit it lists the calls compared per function. `SPYRO_HAND_ONLY=NAME,...` limits it to some functions, `SPYRO_SHADOW_SHOW=N` prints N differing words per call (default 8), and `SPYRO_HAND_FUZZ=N` runs N random variants of each shadowed call first.
- A/B against the generated C: configure another build directory with `-DSPYRO_M2C_FUNCS=all` (or a list of function names) and replay with its binary and its own `fnmap.txt` (`logs/run11-ab`).
- Each run gets fresh memory cards (`$ORACLE_OUT/home`): what's on the card changes the title menu.
- Breakpoint handles must stay referenced from a global: PCSX-Redux removes a breakpoint when Lua collects its handle.

## Checks

- `tools/layoutcheck.sh`: decomp struct layouts, mipsel + PsyQ vs i686 + PsyCross (must report 0 mismatches).
- `tools/check_no_assets.sh`: no game data in git. The pre-commit hook runs it through a wrapper (text in the script's header comment), not a symlink.
- After changing anything in `patches/`, the next build reconfigures and rebuilds everything that uses the patched copy.

## Milestone runs

- `logs/run1-final3`: boot → title → Start → create save → New Game / Load Game, cursor moved by the pad script.
- `logs/run1-demo4`: no input for 12000 vblanks. The attract demos play two levels.
- M2 (Artisans, all on the PS1-layout build):
  - `logs/run4-layout`: boot → New Game → intro cutscene → Artisans; the title intro canyon now renders.
  - `logs/run3-step1` (step session; `flame.png`, `charge.png`, `glide.png` are from before the D23 fix), `logs/run3-replay/exp2/shot-6569.png` (the same glide after it).
  - `logs/run2-artisans2`: pause menu and options, Nestor freed (dragon cutscene).
  - `logs/run6-inventory`: pause → inventory ("ARTISANS Home 0/100 0/4").
  - `logs/run6-fairy2`, `logs/run6-fairy3`: the save fairy on Nestor's pad, "GAME SAVED", `save/card0/BASCUS-94228SPYRO`.
  - `logs/run6-load`: the title lists the saved slot (Artisans, 1 dragon).
  - `logs/run7-load` (step session; replay with its `pad.txt` and a copy of `logs/run6-fairy3/save`): Load Game → slot 1 → Artisans with 1 dragon, Nestor freed, the fairy at his pad.
  - `logs/run6-respawn`, `logs/run6-gameover2`: z poked below 1024 → respawn (lives 4→3) or, with 0 lives, game over.
  - `logs/run4-demo`: attract demos with `trace.tsv` (Stone Hill 5 gems, Doctor Shemp 45).
  - `logs/run5-audio/audio.wav`: 120 s of title, intro and Artisans audio.
  - `logs/run9-gems/steer` (step session steered by `tools/steer.py`; `logs/run9-gems/replay_pad.txt` replays it in `logs/run9-gems/replay`): over the ledge to the waterfall pool, onto the stone platform, four red gems collected (`gems.png`, trace gems 0 → 4).
  - Supercharge isn't part of Artisans: its special surfaces are types 0 and 6 (portals) only, no type 4 (`logs/run9-gems/dump2/mobys-6400.tsv`, `src/special_surfaces.c`).
- Oracle (PCSX-Redux, `logs/run8-oracle`, emulator version in `EMULATOR.txt`):
  - `cap3`: function samples through boot, title and both attract demos; `replay.txt` 329/329 match.
  - `cap5-fairy`: new game, Nestor freed, the save fairy (`tools/oracle/pads/artisans_fairy.txt`); `replay.txt` 261/261 match.
  - `cap3/trace.tsv` vs `demo-host/trace.tsv`: both attract demos identical tick for tick.
  - `intro`, `intro-host`: title intro screenshots, emulator and host.
  - `wizard6`: Wizard Peak via the level-warp cheat; MobyRotateToAngle's 5th argument (`docs/upstream-notes.md`).
  - `cap6-tour`: level tour (`ORACLE_TOUR`, the level-warp cheat) through 30 levels, pete and camera functions; `replay.txt` 358/358 match.
- Hand-written pete and camera C (decisions D33):
  - `logs/run10-handcov`: targeted samples for the hand C (one budget per Spyro state, per camera state and per hidden-tick count; the flame sampled only while on; pad pattern and fairy kiss poke in the tour stays; levels 10-15); `replay.txt` 292/292 match.
  - `logs/run10-demo-host`: host `--trace` of both attract demos with the hand C; identical to `run8-oracle/cap3/trace.tsv` tick for tick (Stone Hill 866, Doctor Shemp 768).
  - `logs/run11-sweep`: `func_80043FE4` hand C against its mips2c unit on the run10-handcov samples (`tools/oracle/sweep_pete.sh`): every state, and the state-dependent cases; 8,547 runs, all match except state 26 with no moby in use (translation log).
  - `logs/run11-ab`: the `SPYRO_M2C_FUNCS=all` build replaying cap3, cap5-fairy, cap6-tour and run10-handcov: 1,240/1,240 match.

- Hand-written C, round 2 (every pete and camera function; `docs/translation-log.md`):
  - `logs/run1-handcov2`, `logs/run3-handcov3`, `logs/run4-handcov4`: samples of the round's functions with a budget per Spyro state, from the warp tour with the pad pattern (levels 10-14, 20, then 21-54 and 60). A stay of 1000 vblanks stalls the tour: the pattern keeps Spyro dying, which restarts the stay. `ORACLE_TOUR_STAY=400` with `ORACLE_TOUR_POKE=...,8007582C=99` gets through.
  - `logs/run5-activecam`: the camera functions with the active camera (`D_80075914` = 2 poked in each stay), a budget per camera state (`g_Camera` is 0x80076DD0, so `m_State` is at 0x80076E28).
  - `logs/hc2-*`: each function's own branch (captures of flight levels, demo traces, A/B builds, sweeps).
  - `logs/run6-replay-all.txt`: every capture replayed on the merged binary, 7,155 PASS and 0 FAIL. `logs/run8-sweeps`: every sweep suite on the merged REF build.
  - `logs/run2-base-*` vs `logs/run7-hand-*` (`tools/host_checks.sh PREFIX`, `BIN=` for a binary): the attract demos, the whole-game tour and routes A/B, on a build before this round and on the final one. Traces, screenshots and saves are byte-identical.

- Whole game (`docs/whole-game-host.md`): `logs/run14-oracle-tour` and `logs/run21-host-whole`, the same tour on both sides; per-level table, screenshot pairs and replay results there.

## Oracle, M1 runner (PCSX-Redux, D24)

A second runner for the same emulator build, from M1, with GTE vectors and per-function snapshots for `tests/gte` and `tests/m2c_diff`. The M2 runner (function replay, demo traces, pad scripts) is "Oracle: PCSX-Redux (D8)" above.

`tools/oracle/redux OUTDIR [ORACLE_VAR=value …]` runs the original game headless and writes into OUTDIR. On first use it fetches the pinned build into `.tools/` and checks its SHA-1 (decision D24). The variables are documented at the top of `tools/oracle/oracle.lua`.

- **VRAM dumps:**
  ```bash
  tools/oracle/redux logs/oracle-x ORACLE_END=2800 ORACLE_SHOTS=2400,2700
  ```
  Then convert them with `tools/oracle/raw2png.py logs/oracle-x --rect 0,0,512,240`.
- **GTE vectors for `tests/gte`:**
  ```bash
  tools/oracle/gte_sites.py disc/files/SCUS_942.28 logs/gte/sites.txt
  ```
  Then run `tools/oracle/redux logs/gte ORACLE_END=9000 ORACLE_GTE=$PWD/logs/gte/sites.txt` (about 4 min).
- **Function snapshots for `tests/m2c_diff`:**
  ```bash
  tools/oracle/redux logs/m2c-math ORACLE_END=9000 ORACLE_TRACE_MAX=20 ORACLE_TRACE=$(tools/oracle/asm_entries.py third_party/spyro-1/asm/math.s --trace) ORACLE_GLOBALS=80076DD0:110,800785A8:30
  ```
- **Vblank numbering:** the oracle counts from reset, and the game's main loop starts about 1,400 vblanks in. The host's vblank 0 is the game's `main`, so the host is about 1,380 vblanks ahead (the Insomniac sign is at host ~420, oracle ~1800).
- **Without vectors:** `tests/gte` and `tests/m2c_diff` read their vectors from `logs/gte` and `logs/m2c-math`. They are skipped when those are missing, because the vectors hold game RAM and stay out of git.

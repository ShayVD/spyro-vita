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

- Time is virtual: `--frames N` counts vblanks (60 per second; the game ticks every 2). Runs are deterministic.
- Pad script lines are `<vblank> <button>[+<button>...]` or `none`. Buttons: `select l3 r3 start up right down left l2 r2 l1 r1 triangle circle cross square`. Each state holds until the next line.
- `--save DIR`: memory cards live in `DIR/card0` (port A) and `DIR/card1` (port B, present only if that directory exists).
- `--cd-latency N`: CD reads complete N ticks later instead of inside `CdRead` (D17).
- `--trace FILE`: one TSV row per vblank (gamestate, level, tick, Spyro's position, state and health, gems, dragons, lives, rand seed). It's the host side of the oracle's frame-trace diff.
- `--step DIR`: closed-loop driving. The game stops and writes `DIR/now.png`; `tools/step.sh DIR "up+cross 30"` holds buttons for N vblanks and waits for the next stop. Every step is appended to `DIR/pad.txt`, so `--pad DIR/pad.txt` replays the session exactly. `tools/step.sh DIR quit` ends it.
- `--poke V:ADDR=VALUE`: writes a 32-bit word to PS1 RAM at vblank V (hex address and value), for test set-ups. With the PS1 layout (D23) addresses come straight from `psx.map`: `g_SpyroLifeCount` is `8007582c`, `g_Spyro` `80078a58` (+8 is z).
- `--wav FILE`: the SPU mix (44.1 kHz s16 stereo) as a WAV file.
- `--tri-dump V`: logs every triangle drawn after vblank V; `tools/tri_at.py LOG X,Y` lists those covering a screen point. `--vram-shot` also writes the raw VRAM (`vram-N.bin`) for `tools/vram.py`.
- Crash backtrace: `tools/tc tools/gdb_run.sh "" -- --disc disc/files --frames N` (add `GDB_AFTER="frame 1;x/8wx \$sp"` for more).

## Playing it yourself (D22)

```bash
TC_PORT=8642 tools/tc build/linux32/src/platform/linux32/spyro --disc disc/files --serve 8642 --save save
```

Then open http://localhost:8642/ . Keys: arrows (D-pad), X (cross), Z (square), C (circle), S (triangle), Enter (start), Backspace (select), Q/E (L1/R1), 1/3 (L2/R2). A gamepad works too (browser Gamepad API). Click "Sound on" for audio. Stop it with Ctrl-C, or `docker stop` on the container.

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

- `capture.lua` (environment variables): `ORACLE_FUNCS`/`ORACLE_SAMPLES`/`ORACLE_GAP`/`ORACLE_FROM` sample function calls; `ORACLE_TRACE` writes the same TSV as the host's `--trace`; `ORACLE_SHOTS`/`ORACLE_SHOT_EVERY` save the display (`tools/oracle/shot2png.py`) and VRAM; `ORACLE_PAD` drives pad 1, with absolute lines shifted by `ORACLE_PAD_SHIFT` and gameplay lines anchored to the game state (`@gs=0,level=10 +71 up`). Scripts are in `tools/oracle/pads/`.
- `spyro --replay DIR [--only NAME] [--ignore FILE] [--fnmap FILE]` loads each sample's entry state into the arena, the GTE and (host build only) all the registers of the call, calls the function's translated C and compares v0/v1, RAM, scratchpad and GTE registers. `--fnmap` (`tools/oracle/fnmap.py`, built by `replay.sh`) turns PS1 function pointers in the emulator's RAM into the host's functions. `SPYRO_REPLAY_NOFORK=1` runs samples in process, for gdb. `tools/oracle/ignore_ranges.txt` lists, with reasons, the RAM that isn't a function output (the asm spill area, state written by interrupt handlers, libcd's DMA'd sectors) and the functions whose return registers are dead.
- Each run gets fresh memory cards (`$ORACLE_OUT/home`): what's on the card changes the title menu.
- Breakpoint handles must stay referenced from a global: PCSX-Redux removes a breakpoint when Lua collects its handle.

## Checks

- `tools/layoutcheck.sh`: decomp struct layouts, mipsel + PsyQ vs i686 + PsyCross (must report 0 mismatches).
- `tools/check_no_assets.sh`: no game data in git.
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
- Oracle (PCSX-Redux, `logs/run8-oracle`, emulator version in `EMULATOR.txt`):
  - `cap3`: function samples through boot, title and both attract demos; `replay.txt` 329/329 match.
  - `cap5-fairy`: new game, Nestor freed, the save fairy (`tools/oracle/pads/artisans_fairy.txt`); `replay.txt` 261/261 match.
  - `cap3/trace.tsv` vs `demo-host/trace.tsv`: both attract demos identical tick for tick.
  - `intro`, `intro-host`: title intro screenshots, emulator and host.
  - `wizard6`: Wizard Peak via the level-warp cheat; MobyRotateToAngle's 5th argument (`docs/upstream-notes.md`).

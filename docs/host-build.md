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
- Crash backtrace: `tools/tc tools/gdb_run.sh "" -- --disc disc/files --frames N` (add `GDB_AFTER="frame 1;x/8wx \$sp"` for more).

## Checks

- `tools/layoutcheck.sh`: decomp struct layouts, mipsel + PsyQ vs i686 + PsyCross (must report 0 mismatches).
- `tools/check_no_assets.sh`: no game data in git.
- After changing anything in `patches/`, the next build reconfigures and rebuilds everything that uses the patched copy.

## Milestone runs

- `logs/run1-final3`: boot → title → Start → create save → New Game / Load Game, cursor moved by the pad script.
- `logs/run1-demo4`: no input for 12000 vblanks. The attract demos play two levels.

## Oracle (PCSX-Redux, native macOS)

`tools/oracle/redux OUTDIR [ORACLE_VAR=value …]` runs the original game headless and writes into OUTDIR. On first use it fetches the pinned build into `.tools/` and checks its SHA-1 (decision D22). The variables are documented at the top of `tools/oracle/oracle.lua`.

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

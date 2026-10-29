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

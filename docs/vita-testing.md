# Testing Spyro Vita on a PS Vita (and in Vita3K)

This is the first Vita build (the start of M3). It aims to boot to the title screen and play
what the linux32 host plays. Read "What to expect" before you test.

## What you need

- A PS Vita or PS TV with HENkaku/Ensō (any firmware that runs homebrew), and VitaShell.
- Your own copy of *Spyro the Dragon* (USA, SCUS-94228), dumped to a `.bin`/`.cue`
  (MODE2/2352, one data track). The VPK contains no game data at all.
- A computer with Python 3, to extract the files from the dump.
- **`libshacccg.suprx` is not needed** for this build. The picture is drawn on the CPU and shown
  through SDL2's own GXM renderer, which uses precompiled shaders (decisions D26). The vitaGL
  GPU renderer that comes later compiles shaders on the device, and will then need
  `ur0:data/libshacccg.suprx` (the usual file for vitaGL ports, extracted with ShaRKBR33D).

## 1. Build the VPK (or use one you were given)

```bash
cmake -S . -B build/vita -G Ninja -DSPYRO_HOST=vita -DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake -DPython3_EXECUTABLE=/usr/bin/python3
```

```bash
cmake --build build/vita
```

The result is `build/vita/spyro-vita.vpk` (title "Spyro Vita", title ID `SPYV00001`). Notes:

- The build needs the matching build's `psx.map` (`tools/build_matching.sh`, or point
  `-DSPYRO_MATCHING_DIR=` at another checkout's `build/s1-matching`).
- The code generators need a Python 3 with PyYAML. On macOS `/usr/bin/python3` has it, which is
  why `-DPython3_EXECUTABLE=/usr/bin/python3` is passed.
- VitaSDK lives at `/usr/local/vitasdk`, with vitasdk's SDL2 and vitaGL packages.

## 2. Extract the game files from your disc dump

```bash
python3 -I tools/extract_disc.py "Spyro the Dragon (USA).bin" spyro-files
```

This writes `SCUS_942.28`, `WAD.WAD`, `PETEXA0.STR` … `PETEXA5.STR`, `SYSTEM.CNF`, the `S0/` and
`SOURCE/` folders and `manifest.json` (about 590 MB). The port finds every file through
`manifest.json`, so copy the whole folder, manifest included. The `.STR` music streams are stored
as 2336-byte sectors: copy them as they are.

## 3. Install

1. Copy `spyro-vita.vpk` to the Vita (VitaShell: SELECT starts the FTP server, or use USB) and
   install it from VitaShell (press X on the file). A bubble called "Spyro Vita" appears.
2. Copy the *contents* of `spyro-files/` to `ux0:data/spyro/`, so that the Vita has
   `ux0:data/spyro/manifest.json`, `ux0:data/spyro/WAD.WAD` and so on.
3. Start "Spyro Vita".

Without `ux0:data/spyro/manifest.json` the screen turns red; START quits.

Files the app writes:

| Path | What |
|---|---|
| `ux0:data/spyro/spyro.log` | the log. **Please send it back after a test**, whatever happened |
| `ux0:data/spyro/save/card0` | memory card 1 (created when the game first saves) |
| `ux0:data/spyro/shots/` | PNGs, only if `config.txt` asks for them |

## 4. Optional `ux0:data/spyro/config.txt`

`key=value` lines, `#` starts a comment:

| Key | Meaning |
|---|---|
| `disc=PATH` | the extracted files are somewhere other than `ux0:data/spyro` |
| `frames=N` | quit after N vblanks (60 per second); 0, the default, never quits |
| `shot=V1,V2,...` | save a PNG of the picture at these vblanks, in `ux0:data/spyro/shots/` |
| `shot_every=N` | and every N vblanks |
| `stats=N` | 0: no timing lines; 1 (default): one line every 300 vblanks; 2: also each pad change |
| `filter=N` | 0: nearest-neighbour scaling; 1 (default): linear |
| `reartouch=1` | the rear touch pad's left/right half is L2/R2 (the right stick always is) |
| `realtime=0` | virtual vblanks: every VSync is exactly one vblank however long the frame took (scripted tests; the game runs slow when the CPU can't keep up) |
| `pad=PATH` | play a pad script (the linux32 host's `--pad` format) on top of the controller |
| `wav=1` | also write the audio to `ux0:data/spyro/audio.wav` |

The log has two kinds of line every 300 vblanks (5 seconds at full speed):

- `vblank N: 300 vblanks in T ms (R per second), P pictures, L vblanks late`. R should be about
  60: the game keeps real time and catches up after slow frames. P is how many new pictures were
  shown, so P/5 is the frame rate (the PS1 game draws at most 30 a second). L counts vblanks the
  game was late for.
- `time N: per vblank ms game … ot … raster … spu … present … wait …; busy B%, worst vblank W ms`
  is where the CPU time went, per vblank (16.7 ms at 60 Hz): game logic, PsyCross's OT walk, the
  software rasterizer, the SPU/CD models, the picture upload, and waiting for the display.

## Controls

The PS1 pad maps one to one: ✕ ○ □ △, START, SELECT, L, R, the d-pad. The left stick also works
as the d-pad (the pad is reported as digital). **L2/R2 turn the camera: push the right stick left
(L2) or right (R2).** With `reartouch=1` in `config.txt` the rear touch pad's left/right half does
the same. A PS TV's DualShock has real L2/R2/L3/R3 (decisions D27).

## What to expect (be direct)

- **The picture is drawn on the CPU** (the linux32 host's software rasterizer), at the PS1's own
  resolution, then scaled 4:3 into the middle of the 960×544 screen. It should look like the PS1,
  not better. The vitaGL GPU renderer, widescreen and 60 fps come later (M4).
- **Speed on hardware (first test, 2026-10-30, build `a60dd0b`): too slow.** 14–20 vblanks per
  second in 3D scenes (60 is full speed): about 120 ms of CPU per game frame against 33 ms. Builds
  from `D28` on keep real time instead (the game catches up, the audio stays whole), but they still
  show fewer pictures than the PS1 until the GPU renderer lands (decisions D29).
- **Audio**: the software SPU and XA music go through SDL2. In Vita3K the WAV the build writes
  (`wav=1`, the same samples SDL2 gets) is byte-identical to the linux32 host's.
- What the linux32 host can do (M2: Artisans, saving and loading) is what this build can do at
  best. Anything the host hasn't reached hasn't been tried.

## Testing in Vita3K

Vita3K is not a Vita: something that runs there may still fail on hardware (memory limits,
stack size, timing, the real GPU).

1. Copy the extracted files into Vita3K's `ux0`:
   `~/Library/Application Support/Vita3K/Vita3K/fs/ux0/data/spyro/` on macOS.
2. `tools/vita/vita3k_run.sh OUT_DIR [TIMEOUT_S] [config line]...` writes `config.txt`, installs
   and runs the VPK (`Vita3K --frontend sdl -l 4 build/vita/spyro-vita.vpk`), waits for
   `frames=` or the timeout, and copies `spyro.log` and the shots to `OUT_DIR`:

```bash
tools/vita/vita3k_run.sh logs/run2-vita-title 150 shot_every=120 frames=2400
```

Keep Vita3K's log level at ERROR (`-l 4`): its TRACE default fills the disk.

### Results so far (2026-10-30, Vita3K on an M3 Mac, firmware installed, NTSC-U disc)

Vita3K v0.2.1 build 4232, Vulkan via MoltenVK. Logs (gitignored) in `logs/`:

| Run | What | Result |
|---|---|---|
| `run1-vita-title` | boot to 2400 vblanks, before the tag fix | Title reached; picture right, but 352k PsyCross "bad GP0 command" errors from the sky renderer on (decisions D25, OT tags XORed with pointers). Kept as the record of the bug. |
| `run2-vita-title` | boot to the title, a shot every 120 vblanks | Title at ~1700 vblanks. **All 15 shots up to 1800 byte-identical** to the linux32 host's (`logs/run4-layout` on the M2 branch). No PsyCross error. ~60 vblanks/s. |
| `run3-vita-input` | START and ✕ pressed in the Vita3K window | `pad: buttons 0008` then `4000` in the log: START opens "create save file?", ✕ creates it (`save/card0`) and shows New Game / Load Game. Window captures `vita3k-window-*.jpg` show the 4:3 picture pillarboxed in 960×544. The run ended before New Game reached a level. |
| `run4-vita-demo` | no input, 12000 vblanks (title, attract demos in two levels) | **All 40 shots (every 300 vblanks) byte-identical** to the linux32 host's `logs/run4-demo`. No PsyCross error. 59.9 vblanks/s throughout. |

### On the M2 base (`b9492d7`), scripted with the host's pad scripts

Each run was replayed on the linux32 host from the same commit with the same script
(`logs/run6-host-base`, `logs/run7-host-*`), and compared file by file:

| Run | What | Result |
|---|---|---|
| `run5-vita-base` | no input, 12000 vblanks, shot every 120, `wav=1` | 100/100 shots and the 35 MB WAV byte-identical to the host. |
| `run6-vita-gems` | M2's gems script: New Game → intro → Artisans, walk to the waterfall pool, four gems | 36/36 shots and the memory card byte-identical. |
| `run7-vita-fairy` | M2's fairy script: free Nestor, save at the fairy ("GAME SAVED") | 37/37 shots and `save/card0/BASCUS-94228SPYRO` byte-identical. |
| `run8-vita-load` | M2's load script, starting from `run7-vita-fairy`'s card | 14/14 shots byte-identical: Load Game → Artisans with Nestor freed. |
| `run9`–`run11` | after each speed change (D29) | still byte-identical (shots, WAV). |
| `run12-vita-realtime` | `REALTIME=1`, no input, 7200 vblanks | ~60 vblanks/s kept, audio continuous (120 s for 7200 vblanks), no errors. |

Not checked here: audio by ear (the SDL2 device opens, 44.1 kHz stereo); real-time play by hand
for long. Speed in Vita3K says little about the Vita's own speed (about 11× slower on hardware).

## On hardware

| Build | Who | Result |
|---|---|---|
| `a60dd0b` | user, 2026-10-30 | Boots, runs about 9 minutes with no errors (arena at 0x82000000). Too slow: 14–20 vblanks/s in 3D, 19–24 on the title (decisions D28, D29). |

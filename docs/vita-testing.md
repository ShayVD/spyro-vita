# Testing Spyro Vita on a PS Vita (and in Vita3K)

This is the first Vita build (the start of M3). It aims to boot to the title screen and play
what the linux32 host plays. Read "What to expect" before you test.

## What you need

- A PS Vita or PS TV with HENkaku/Ensō (any firmware that runs homebrew), and VitaShell.
- Your own copy of *Spyro the Dragon* (USA, SCUS-94228), dumped to a `.bin`/`.cue`
  (MODE2/2352, one data track). The VPK contains no game data at all.
- A computer with Python 3, to extract the files from the dump.
- **`libshacccg.suprx` is not needed** for this build. The picture is drawn on the CPU and shown
  through SDL2's own GXM renderer, which uses precompiled shaders (decisions D25). The vitaGL
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

## Controls

The PS1 pad maps one to one: ✕ ○ □ △, START, SELECT, L, R, the d-pad. The left stick also works
as the d-pad (the PS1 game is digital). L2/R2/L3/R3 only exist on a PS TV with a DualShock; on a
handheld Vita they're not mapped yet.

## What to expect (be direct)

- **The picture is drawn on the CPU** (the linux32 host's software rasterizer), at the PS1's own
  resolution, then scaled 4:3 into the middle of the 960×544 screen. It should look like the PS1,
  not better. The vitaGL GPU renderer, widescreen and 60 fps come later (M4).
- **Speed is unknown on hardware.** The game is paced by the Vita's vblank. If a frame takes
  longer than it should, the game runs slower (it never skips). The log's `vblank N: ... per
  second` lines show the rate: 60 is full speed.
- **Audio**: the software SPU and XA music go through SDL2. If the game runs slow, the audio
  will have gaps.
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
| `run1-vita-title` | boot to 2400 vblanks, before the tag fix | Title reached; picture right, but 352k PsyCross "bad GP0 command" errors from the sky renderer on (decisions D24, OT tags XORed with pointers). Kept as the record of the bug. |
| `run2-vita-title` | boot to the title, a shot every 120 vblanks | Title at ~1700 vblanks. **All 15 shots up to 1800 byte-identical** to the linux32 host's (`logs/run4-layout` on the M2 branch). No PsyCross error. ~60 vblanks/s. |
| `run3-vita-input` | START and ✕ pressed in the Vita3K window | `pad: buttons 0008` then `4000` in the log: START opens "create save file?", ✕ creates it (`save/card0`) and shows New Game / Load Game. Window captures `vita3k-window-*.jpg` show the 4:3 picture pillarboxed in 960×544. The run ended before New Game reached a level. |
| `run4-vita-demo` | no input, 12000 vblanks (title, attract demos in two levels) | **All 40 shots (every 300 vblanks) byte-identical** to the linux32 host's `logs/run4-demo`. No PsyCross error. 59.9 vblanks/s throughout. |

Not checked here: audio by ear (the SDL2 device opens, 44.1 kHz stereo); a level played by hand on
the Vita build; loading a save; anything on real hardware. Speed in Vita3K says nothing about
the Vita's own speed.

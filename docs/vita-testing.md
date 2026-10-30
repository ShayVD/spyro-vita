# Testing Spyro Vita on a PS Vita (and in Vita3K)

This is the M3 Vita build. It aims to boot to the title screen and play what the linux32 host
plays, drawn by the Vita's GPU. Read "What to expect" before you test.

## What you need

- A PS Vita or PS TV with HENkaku/Ensō (any firmware that runs homebrew), and VitaShell.
- Your own copy of *Spyro the Dragon* (USA, SCUS-94228), dumped to a `.bin`/`.cue`
  (MODE2/2352, one data track). The VPK contains no game data at all.
- A computer with Python 3, to extract the files from the dump.
- **`ur0:data/libshacccg.suprx`, for the GPU renderer.** The GPU renderer (`gpu=1`, the default,
  decisions D30) compiles its shaders on the device with vitaGL, which needs Sony's shader compiler
  module. It's the usual file for vitaGL ports, extracted from your own Vita with ShaRKBR33D.
  `ur0:data/external/libshacccg.suprx` works too. Without it the app logs
  `gpu: no ur0:data/libshacccg.suprx ...` and draws on the CPU instead (`gpu=0`, decisions D26):
  correct, but too slow on hardware.

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
| `gpu=0` | draw on the CPU (the software rasterizer) instead of the GPU; `gpu=1` is the default |
| `gpu_res=N` | GPU: draw at N times the PS1's resolution (1 = the PS1's own pixels, cheapest); 0, the default, draws at the screen's own resolution |
| `shot_hd=1` | GPU: each shot also as a 960×544 PNG of the screen (`shot-V-hd.png`; black in Vita3K) |
| `gpu_readback=0` | GPU: read drawn pixels back with `glReadPixels` instead of replaying the draws on the CPU (decisions D30; for testing) |
| `gpu_dump=V` | GPU: log every batch of triangles drawn at vblanks V and V+1 (debugging) |

The log has two kinds of line every 300 vblanks (5 seconds at full speed):

- `vblank N: 300 vblanks in T ms (R per second), P pictures, L vblanks late`. R should be about
  60: the game keeps real time and catches up after slow frames. P is how many new pictures were
  shown, so P/5 is the frame rate (the PS1 game draws at most 30 a second). L counts vblanks the
  game was late for.
- `time N: per vblank ms game … ot … raster … spu … present … wait …; busy B%, worst vblank W ms`
  is where the CPU time went, per vblank (16.7 ms at 60 Hz): game logic, PsyCross's OT walk, the
  rasterizer (with the GPU: handing the triangles to vitaGL, VRAM uploads, readbacks), the SPU/CD
  models, the picture upload, and waiting for the display (with the GPU this includes waiting for
  the GPU to finish).
- With the GPU, also `gpu N: D draws, T triangles, P presents, U VRAM uploads (X ms), C copies
  into targets, R readbacks (… replayed; Y ms), K targets`, counted over the same 300 vblanks.
  The upload and readback times are CPU time.

At start-up the log says which renderer runs: `renderer: GPU (vitaGL, gpu=1)` then
`gpu: GPU renderer ready (gpu=1)`. Anything else on a `gpu:` line (`shader compiler error`,
`does not compile`, `using the software renderer`) is what to send back.

## Controls

The PS1 pad maps one to one: ✕ ○ □ △, START, SELECT, L, R, the d-pad. The left stick also works
as the d-pad (the pad is reported as digital). **L2/R2 turn the camera: push the right stick left
(L2) or right (R2).** With `reartouch=1` in `config.txt` the rear touch pad's left/right half does
the same. A PS TV's DualShock has real L2/R2/L3/R3 (decisions D27).

## What to expect (be direct)

- **The picture is drawn by the GPU** (decisions D30), 4:3 in the middle of the 960×544 screen, at
  the screen's resolution: sharper than the PS1, with the PS1's own textures, colours and
  semi-transparency. There is no dithering. The pause and inventory screens show the game frame
  at the PS1's resolution, as the game itself copies it. Widescreen and 60 fps come later (M4).
  It hasn't run on hardware yet. Shader compilation adds a few seconds before the first picture.
  `gpu=0` gives the CPU picture of the earlier builds.
- **Speed on hardware (first test, 2026-10-30, build `a60dd0b`): too slow.** 14–20 vblanks per
  second in 3D scenes (60 is full speed): about 120 ms of CPU per game frame against 33 ms. Builds
  from `D28` on keep real time instead (the game catches up, the audio stays whole). The GPU
  renderer takes the rasterizer, about 60% of the CPU time, off the CPU (decisions D29, D30). Game
  logic is still on the CPU, so how close the GPU build gets to 30 pictures a second is what the
  next hardware log will tell.
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
   and runs the VPK (`Vita3K --frontend sdl -c OUT_DIR/vita3k-config.yml -w
   build/vita/spyro-vita.vpk`, with a copy of Vita3K's config at log level ERROR), waits for
   `frames=` or the timeout, and copies `spyro.log` and the shots to `OUT_DIR`:

```bash
tools/vita/vita3k_run.sh logs/run2-vita-title 150 shot_every=120 frames=2400
```

Keep Vita3K's log level at ERROR (the default; `VITA3K_LOG=N` changes it): TRACE fills the disk.

**With the GPU (`gpu=1`) in Vita3K:**

- `libshacccg.suprx` has to be in Vita3K's `fs/ur0/data/`.
- Vita3K returns zeros for anything read back from what the GPU drew (`glReadPixels`, so
  `shot_hd=1` and `gpu_readback=0`). The normal shots still work. They come from the readback
  replay, the PS1-exact picture the game sees through `StoreImage`, so they compare byte for byte
  with `gpu=0` and the host. They don't show the GPU's own output.
- To see the GPU's picture, capture the Vita3K window while it runs:
  `screencapture -x -o -l<window id>`, with the window id from `CGWindowListCopyWindowInfo`
  (owner "Vita3K").

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

### The GPU renderer (D30, on main `8662485`)

Compared with the same host runs (`run6-host-base`, `run7-host-gems` from the M3 session; this
change touches nothing the linux32 host builds), and with window captures of the GPU's own picture
(`logs/runN-*-win*.png`):

| Run | What | Result |
|---|---|---|
| `run44-vita-gpu-title`, `run45-vita-soft-title` | title, 2400 vblanks, shot every 120, `gpu=1` and `gpu=0` | 20/20 shots identical between the two. Window: the title and its intro at 725×544. |
| `run46-vita-gpu-gems` | gems script, `gpu=1` | 36/36 shots and the card byte-identical to the host. Window: Artisans textured, Spyro, Sparx, gems, semi-transparent waterfalls. |
| `run47-vita-gpu-pause` | gems script, then START, down, down, ✕ (inventory), `gpu=1` | The pause and inventory screens show the game frame in the game's green tint behind the menu (the `StoreImage` readback, replayed). |
| `run48-vita-gpu-demo` | no input, 12000 vblanks (attract demos), `gpu=1` | 100/100 shots byte-identical to the host. No PsyCross error. |
| `run49-vita-soft-demo` | no input, 12000 vblanks, `wav=1`, `gpu=0` | 100/100 shots and the 35 MB WAV byte-identical to the host. |
| `run50-vita-soft-gems` | gems script, `gpu=0` | 36/36 shots and the card byte-identical to the host. |
| `run51-vita-gpu-realtime`, `run52-vita-soft-realtime` | gems script, `REALTIME=1` | 60 vblanks/s in both. Busy CPU per vblank (`tools/vita/time_summary.py`): 2.79 ms with `gpu=1`, 6.82 ms with `gpu=0`. Raster 0.90 vs 4.85 ms, present 0.05 vs 0.55 ms. Vita3K's times, not the Vita's. |

`run14`–`run43` were the bring-up (decisions D30's vitaGL findings) and are kept only as records.

Not checked here: audio by ear (the SDL2 device opens, 44.1 kHz stereo); real-time play by hand
for long. Speed in Vita3K says little about the Vita's own speed (about 11× slower on hardware).

## On hardware

| Build | Who | Result |
|---|---|---|
| `a60dd0b` | user, 2026-10-30 | Boots, runs about 9 minutes with no errors (arena at 0x82000000). Too slow: 14–20 vblanks/s in 3D, 19–24 on the title (decisions D28, D29). |

**Testing the GPU build:** put `libshacccg.suprx` in `ur0:data/` first. In `spyro.log`, check:

1. `renderer: GPU (vitaGL, gpu=1)` and `gpu: GPU renderer ready (gpu=1)`. Any `gpu: shader
   compiler error` or `does not compile` line means the shaders failed on the device.
2. `vblank N: ... P pictures`: P/5 is the frame rate (30 is the PS1's).
3. `time N:` lines, in particular `busy` and `worst vblank`. Compare them with the CPU build by
   running once with `gpu=0` in `config.txt`.
4. `gpu N:` lines: the `VRAM uploads (X ms)` cost per 300 vblanks (Spyro writes VRAM every frame).
5. Open the pause menu and the inventory: the background should be the game's frame in green.

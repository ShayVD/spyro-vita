# Testing Spyro Vita on a PS Vita (and in Vita3K)

This is the M3 Vita build. It aims to boot to the title screen and play what the linux32 host
plays, drawn by the Vita's GPU. Read "What to expect" before you test.

## What you need

- A PS Vita or PS TV with HENkaku/Ensō (any firmware that runs homebrew), and VitaShell.
- Your own copy of *Spyro the Dragon* (USA, SCUS-94228), dumped to a `.bin`/`.cue`
  (MODE2/2352, one data track). The VPK contains no game data at all.
- A computer with Python 3, to extract the files from the dump.
- **`ur0:data/libshacccg.suprx`, for the GPU renderer.** The GPU renderer (`gpu=1`, the default,
  decisions D35) compiles its shaders on the device with vitaGL, which needs Sony's shader compiler
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
| `res=native` | GPU: the resolution it draws at (decisions D40). `native`, the default: the screen's own pixels (the 4:3 picture is 726×544, 16:9 is 960×544). `ps1` (or `1`): the PS1's 512×240, scaled up when shown, as on a TV (cheapest). `2`…`4`: N times the PS1's, scaled down when shown (smoother edges, more GPU work). `gpu_res=` is the old name |
| `gpu_time=0` | GPU: no GPU-timed frame in the log (default 1: one frame per 300 vblanks, see below) |
| `shot_hd=1` | GPU: each shot also as a 960×544 PNG of the screen (`shot-V-hd.png`; black in Vita3K) |
| `gpu_readback=0` | GPU: read drawn pixels back with `glReadPixels` instead of replaying the draws on the CPU (decisions D35; for testing) |
| `gpu_dump=V` | GPU: log every batch of triangles drawn at vblanks V and V+1 (debugging) |
| `mixer=N` | where the sound is mixed (decisions D37): 1 (default) on its own CPU core; 2 the same, but each vblank waits for its mix (debugging); 0 on the game thread, as before |
| `widescreen=0` | 4:3 instead of the default 16:9 (a wider view filling the screen; the game itself plays exactly as in 4:3; `docs/presentation.md`) |
| `interp=0` | 30 fps instead of the default 60 (`interp=1`: an extra in-between picture each tick: camera, Spyro, enemies and objects, their animation, particles). `interp=2`: without animation blending; `interp=3`: camera only |
| `widescreen_exact=1` | keep the original 4:3 game logic in normal play too (one extra hidden draw per tick: slower). By default the 16:9 view is drawn once and enemies at its edges act as on-screen ones; attract demos always keep the 4:3 logic |
| `shot_hold=MS` | after each shot, hold the game until `hold.ack` appears in `ux0:data/spyro/`, at most MS ms (Vita3K window captures) |
| `poke=V:ADDR=VALUE` | at vblank V, write a 32-bit word into PS1 RAM (hex; tests, like the linux32 host's `--poke`; up to 16) |
| `interp_budget=MS` | skip the in-between picture while the game's CPU time per tick, plus one, would pass MS (default 26; `0` never skips). Skipped ticks show 30 fps instead of slowing the game down |
| `menu_test=V1,V2,...` | tests without a controller: open the options menu at these vblanks (up to 4) and press down, left, up, right, circle |
| `tex_filter=N` | GPU: smooth textures; 1: 3-point (N64 style), 2: bilinear; 0, the default, keeps the PS1's sharp texels (decisions D39) |
| `persp=1` | GPU: perspective-correct textures on 3D polygons (no PS1 texture warping; D39) |
| `subpixel=1` | GPU: 3D vertices at their exact positions instead of whole pixels (no PS1 wobble; D39) |
| `tex_cache=0` | GPU: decode textures from VRAM per pixel instead of the decoded atlas (`tex_cache=1`, the default, gives the same pixels faster; D41) |
| `bench=1` | GPU: measure the picture options (leave the game alone for ~4 minutes; `bench final` lines in the log; D41) |

**Options menu:** hold SELECT and press START, in any screen. The game stops while the menu is open. Up/down chooses a line, left/right (or ✕) changes it, ○ (or START, △) closes. Widescreen, 60 fps and the 60 fps budget change at once and are written back to `config.txt` when the menu closes; other lines in the file stay as they were. The game never sees the buttons that open or close the menu.

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
  into targets (M moves between them), R readbacks (… replayed; Y ms), K targets at W×H`, counted
  over the same 300 vblanks. W×H is the size the GPU draws a 512×224 game buffer at (`res=`).
  The upload and readback times are CPU time.
- With the GPU, also `gpu N: timed frame F ms, GPU tail after the CPU G ms`: one frame in the
  window is timed with `glFinish` before its first draw and before its swap. F is that frame from
  its first draw to the GPU being done (CPU and GPU work overlapped); G is how long the GPU still
  worked after the CPU had sent the whole frame. A G near 0 means the GPU keeps up at this
  resolution; the frame is ~G ms late on screen at most, once per 5 seconds (`gpu_time=0` turns it
  off).
- With `widescreen=1` or `interp=1`, also `present N: T ticks; per tick us: logic draw …, passes
  …, snapshot …, blend …; I in-between frames, S skipped for the budget, O otherwise`. I/T is
  how often the 60 fps picture made it; S counts ticks the budget turned it off.
- With `tex_filter`, `persp` or `subpixel`, also `vcache N: … per frame P projected …, Q polygons,
  A% with every vertex found, B% of vertices found; expand E ms`. A is the share of 3D polygons
  drawn perspective-correct (the rest are drawn as on the PS1); E is the CPU time spent preparing
  the vertices over the 300 vblanks.
- `mixer N: M vblanks mixed, X ms each, worst Y ms; game thread waited W ms in K waits; E position
  mismatches; audio queue dry D, cut C` is the sound mixer's own core (D37). X is its time per
  vblank (not counted in `time`'s `spu`, which is now only what the game thread spends on the CD
  and SPU models). W/K are the game thread waiting for it: at loads (SPU RAM writes), otherwise 0.
  E (counted from start-up) must be 0. D counts mixes that found the audio queue empty (a gap you can hear), C the times
  it was cut back to 150 ms (the game ran ahead).

At start-up the log says which renderer runs: `renderer: GPU (vitaGL, gpu=1)` then
`gpu: GPU renderer ready (gpu=1)`. Anything else on a `gpu:` line (`shader compiler error`,
`does not compile`, `using the software renderer`) is what to send back.

## Controls

The PS1 pad maps one to one: ✕ ○ □ △, START, SELECT, L, R, the d-pad. The left stick also works
as the d-pad (the pad is reported as digital). **L2/R2 turn the camera: push the right stick left
(L2) or right (R2).** With `reartouch=1` in `config.txt` the rear touch pad's left/right half does
the same. A PS TV's DualShock has real L2/R2/L3/R3 (decisions D27).

## What to expect (be direct)

- **The picture is drawn by the GPU** (decisions D35), 4:3 in the middle of the 960×544 screen, at
  the screen's resolution: sharper than the PS1, with the PS1's own textures, colours and
  semi-transparency. There is no dithering. The pause and inventory screens show the game frame
  at the PS1's resolution, as the game itself copies it. Widescreen and 60 fps come later (M4).
  It hasn't run on hardware yet. Shader compilation adds a few seconds before the first picture.
  `gpu=0` gives the CPU picture of the earlier builds.
- **Speed on hardware (first test, 2026-10-30, build `a60dd0b`): too slow.** 14–20 vblanks per
  second in 3D scenes (60 is full speed): about 120 ms of CPU per game frame against 33 ms. Builds
  from `D28` on keep real time instead (the game catches up, the audio stays whole). The GPU
  renderer takes the rasterizer, about 60% of the CPU time, off the CPU (decisions D29, D35). Game
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
- To see the GPU's picture, capture the Vita3K window: `CAPTURE=1 tools/vita/vita3k_run.sh …`
  writes `win-V.png` for every shot V, the same frame as the shot. After each shot the game
  holds (`shot_hold=`), writes `hold.req`, and waits for the `hold.ack` the script writes once
  `tools/vita/vita3k_window.swift` has grabbed the window. (Not the log: Vita3K writes it to the
  host in 4 KB blocks, so a log line can show up long after the game wrote it.) A failed grab
  brings that Vita3K to the front and tries once more; failures go to `capture.log`. The holds
  count as game time, so don't take timings from capture runs.

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

### The GPU renderer (D35, on main `8662485`)

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

### The mixer on its own core (D37)

Same host references (`run6-host-base`, `run7-host-gems`; this tree's linux32 build reproduces
`run6-host-base`'s WAV byte for byte, `logs/run63-host-base`):

| Run | What | Result |
|---|---|---|
| `run62-vita-gpu-base`, `run64`–`run66` | first try | WAV wrong from vblank 438 and a hang at 5640: the mixer's XA reset went through `--wrap` back into the queue (D37). Kept as the record. |
| `run67-vita-mixer1-short` | fixed, 1500 vblanks, `wav=1` | WAV byte-identical. |
| `run68-vita-gpu-base`, `run69-vita-soft-base` | no input, 12000 vblanks, shot every 120, `wav=1`, `gpu=1` / `gpu=0` | 100/100 shots and the 35 MB WAV byte-identical to the host, both. 0 position mismatches. |
| `run72-vita-gpu-gems`, `run71-vita-soft-gems` | gems script, `gpu=1` / `gpu=0` | 36/36 shots and the card byte-identical to the host, both. |
| `run73-vita-gpu-realtime`, `run74-vita-gpu-realtime-before` | gems script, `REALTIME=1`, `wav=1`, with and without this change | Both ~60 vblanks/s, WAV 8768 vblanks long. Game-thread `spu` 0.47 → 0.09 ms per vblank, busy 3.16 → 2.73 ms; mixer thread 0.4–0.6 ms per vblank. |
| `run75`–`run78` | the four runs above again after merging main (Vita widescreen and 60 fps) | all byte-identical again (shots, WAV, card); 0 position mismatches |
| `run79-vita-merged-realtime-ws` | gems script, `REALTIME=1`, `wav=1 widescreen=1 interp=1` | 58.6 vblanks/s, WAV 8768 vblanks long; `spu` 0.15 ms per vblank, mixer 0.5–0.7 ms; audio queue dry 0–11 times per 300 vblanks |

`run14`–`run43` were the bring-up (decisions D35's vitaGL findings) and are kept only as records.

### Texture filtering and perspective (D39, on main `3b4126a`)

Gems script plus the pause and inventory screens, window captures at the same 8 vblanks in every run
(`win-V.png`; the app holds each picture until it is captured):

| Run | What | Result |
|---|---|---|
| `run15-vita-mbase`, `run16-vita-moff` | main's `gpu_gl.c`, and this build with all three switches off | 8/8 captures pixel-identical (0 of 2,196,480 pixels differ). Replay shots byte-identical. |
| `run28-vita-mall2` | `tex_filter=1 persp=1 subpixel=1` | Textures smooth and straight; cut-outs keep clean edges; the waterfalls, sky, HUD, 3D font and menus are right. Replay shots byte-identical to off: the game sees the same pictures. 79–85% of Artisans' polygons perspective-correct. |
| `run18-vita-mfilt2`, `run19-vita-mpersp`, `run20-vita-mws-all` | bilinear only; perspective and sub-pixel only; all on with widescreen and 60 fps | as expected; with the presentation passes the cache follows each pass. |
| `run29`–`run33` | `REALTIME=1`, CPU cost | about +0.6 ms busy per vblank at 4:3 with all on, +0.2 with widescreen and 60 fps; `tex_filter` alone ~0 (D39). |

Not checked here: audio by ear (the SDL2 device opens, 44.1 kHz stereo); real-time play by hand
for long. Speed in Vita3K says little about the Vita's own speed (about 11× slower on hardware).

## On hardware

| Build | Who | Result |
|---|---|---|
| `a60dd0b` | user, 2026-10-30 | Boots, runs about 9 minutes with no errors (arena at 0x82000000). Too slow: 14–20 vblanks/s in 3D, 19–24 on the title (decisions D28, D29). |
| `2661415` | user, 2026-10-30 | GPU renderer (D35). Works well: about 3.5 minutes from boot through Artisans and the pause menu. 60 vblanks/s throughout, mostly 29–30 pictures/s (22–26 in the heaviest stretch). Busy CPU 60–93% of each vblank: game 2–5 ms, raster 5–6 ms (2.2 of it the per-frame VRAM upload), SPU 0.8–2.7 ms. About 8 s of shader compilation at start-up. Uploads cut afterwards (D35). |
| `c40c9cf` | user, 2026-10-30 | GPU renderer with uploads cut (D35). About 2.5 minutes through the title, Artisans and the pause menu. VRAM uploads 0 in nearly every 300-vblank window (4, 2, 1 at loads and the pause menu); raster 2.7–4 ms per vblank (was 5–6); busy CPU 43–78%; 143–150 pictures per 300 vblanks in every gameplay window, late vblanks mostly 0–15, worst vblank usually 24–29 ms. Per vblank in levels: game 3–5 ms, raster 3–4, SPU ~2.5, OT walk 1–1.5. Still ~8 s of shader compilation at start-up. |

**Testing the GPU build:** put `libshacccg.suprx` in `ur0:data/` first. In `spyro.log`, check:

1. `renderer: GPU (vitaGL, gpu=1)` and `gpu: GPU renderer ready (gpu=1)`. Any `gpu: shader
   compiler error` or `does not compile` line means the shaders failed on the device.
2. `vblank N: ... P pictures`: P/5 is the frame rate (30 is the PS1's).
3. `time N:` lines, in particular `busy` and `worst vblank`. Compare them with the CPU build by
   running once with `gpu=0` in `config.txt`.
4. `gpu N:` lines: `VRAM uploads` should be a handful per 300 vblanks (level loads, menus), not
   one per picture.
5. Open the pause menu and the inventory: the background should be the game's frame in green.
6. `mixer: thread on core 1` at start-up, then `mixer N:` lines: `position mismatches` 0,
   `audio queue dry` near 0 in gameplay, and the mixer's ms per vblank (the time `spu` had before).
   `spu` in the `time` lines should be well under 1 ms. If the sound is wrong, try `mixer=2`
   and `mixer=0` and say which ones are.

**Measuring the picture options: `bench=1`** (decisions D41), with the GPU build:

1. `config.txt`: just `bench=1` (keep `gpu=1`, the default). Start the game, don't touch the controller, and
   leave it about 4 minutes: it plays the title intro, the title and both attract demos while
   16 option sets take turns, at 4:3 and 16:9, native and PS1 resolution, at 30 fps
   (`bench=2` keeps your 60 fps setting).
2. Send `spyro.log` back. The `bench final:` lines (at vblank 12120, about 3.5 minutes in) give
   each option set's pictures per second and its GPU-timed frame and tail; `bench round N:` lines
   come every 4 rounds in case the run stops early. The `tex_cache=0` / `tex_cache=1` pairs are
   the default picture before and after the decoded-texture atlas.
3. After the bench the configured options are back: you can then play a minute with
   `tex_filter=1 persp=1 subpixel=1` (no `bench=`) and note any glitches (what, where, which
   option), ideally with a photo.

**Testing texture filtering and perspective** (decisions D39, D41, all off by default), with the GPU build:

1. Play a minute of Artisans as usual, then again with `tex_filter=1`, `persp=1` and `subpixel=1`
   in `config.txt` (then `tex_filter=2` once, if there is time).
2. In the log: `gpu: tex_filter=1 persp=1 subpixel=1 (D39)`, no shader errors, and `vcache N:`
   lines (the share of polygons with every vertex found should be about 80%).
3. Compare the runs' `time N:` lines (game and ot columns: the cost of persp and subpixel) and
   pictures per 300 vblanks (the GPU cost of the filter shows as fewer pictures and more wait).
4. Look for textures swimming or warping on the ground near Spyro (gone with `persp=1`), for
   flickering or stretched polygons (a wrong depth would show as one), and for dark or pink
   edges on plants and fences (there should be none).

**First hardware test (main `3b4126a`, user):** red and green textures after the pause menu, the picture snapping between 16:9 and 4:3, scenery popping at the edges, 20–26 fps with widescreen. Causes and fixes: `docs/presentation.md` "Fixes from the first hardware test".

**Testing widescreen and 60 fps** (`docs/presentation.md`, decisions D30; both on by default; second test, after the fixes), with the GPU build:

1. `config.txt`: only `stats=1` (and `shot_hd=1`, `shot=1800,1801,4400,4401,4402` for 960x544
   pictures of the title and the first attract demo). Leave it at the title until the demo plays
   (about 75 s), then start a game.
2. **Red/green:** in Artisans, pause, open the inventory, close it, unpause, several times. The
   textures must stay right (this broke every texture before).
3. **Snapping:** play for a few minutes through busy spots. The picture must stay 16:9 the
   whole time (before, it jumped to 4:3 and back when the game slowed down). In the log,
   `present N:` lines end with `0 pictures from the 4:3 display` (a few are fine around level
   loads).
4. **Popping:** objects and scenery at the left and right edges should not flicker in and out.
5. **Speed:** `vblank N: … P pictures`: with widescreen on, P should be near 150 per 300 vblanks
   (30 fps) in gameplay, like 4:3 (before: 100-130). `present N:` lines show how many
   in-between frames fit (60 fps needs about 300 pictures).
6. **Menu:** hold SELECT, press START; switch widescreen off and on and 60 fps off and on; the
   log says `menu: closed after N ms`, and the picture follows each change.
7. Send back `spyro.log`, the shots, and anything that looked wrong (when, and what you did
   just before).

**Testing the render resolution** (`res=`, decisions D40), with the GPU build:

1. No `res=` line (the default, `res=native`): in the log, `gpu N: … 2 targets at 960x508` (16:9,
   the default) or `726x508` (`widescreen=0`), and a `gpu N: timed frame F ms, GPU tail after the CPU G
   ms` line every 300 vblanks. G is the GPU's cost beyond the CPU's; near 0 means the GPU keeps up.
2. Look at the boot logos (Sony, Universal: are they there, sharp and centred?), the title's
   PRESS START and the HUD in Artisans: letters should have hard edges, no soft or doubled rows,
   and no light line along the left or top edge of the 4:3 picture during fades.
3. Lose all lives once (or skip it): the frozen frame that darkens before GAME OVER should be as
   sharp as the game was, not blocky.
4. `res=ps1` in `config.txt`: the picture should look exactly like builds before D40 with
   `gpu_res=1` (softer, PS1 pixels scaled up). Compare `busy`, `P pictures` and the `timed frame`
   line with the default.
5. `res=2`: smoother edges; check that `P pictures` stays near 150 per 300 vblanks in Artisans
   and how big G gets.

### The render resolution (D40, branch `claude/recursing-wilbur-baad6f`)

Window captures (`CAPTURE=1`, the same frame as each shot) and replay shots, "old" = main
`4ced09a` (with PR #12) and "new" = this branch, `realtime=0` unless noted. Logs in `logs/`:

| Runs | What | Result |
|---|---|---|
| `run90`, `run91` | title, old vs new at `res=native` | Same frames (Spyro's pose matches); replay shots byte-identical. New: PRESS START's letters have hard row edges where old had in-between shaded rows (the 18.13-line offset), and the picture sits ~1 px differently (half a target pixel). |
| `run92`, `run93` | title, new `res=ps1` vs old `gpu_res=1` | Window captures pixel-identical (0 of 2,088,960 differ); replay shots byte-identical. |
| `run94`, `run95` | Artisans, pause, inventory (M2 pause script) | Replay shots and memory card byte-identical; pause and inventory backgrounds right in both (green frozen frame, sharp menu). |
| `run96`, `run97` | game over (`poke=` lives 0, Spyro under the floor) | Replay shots byte-identical; the log shows `1 moves between them`. The frozen frame is blocky (PS1 pixels) in old and at full resolution in new. |
| `run98`, `run99` | widescreen pause script, `res=native` vs `res=ps1` | `targets at 960x508`; the 16:9 picture fills the screen at native resolution; `res=ps1` softer as expected. |
| `run100`–`run103` | `REALTIME=1`, 6000 vblanks of title and attract demo: native, ps1, 2, widescreen+interp | 60 vblanks/s and 134–150 pictures per 300 vblanks at every resolution. Vita3K's GPU timings are not the Vita's: hardware decides the cost. |
| `run104` | `REALTIME=1`, boot logos, old and new | The 24-bit logos are black in the window in both builds (the replay shots show them): not caused by D37. |
| `run106`–`run110` | after merging main #13/#14: title native and `res=ps1`, game over, title with `widescreen=1 interp=1` | Replay shots byte-identical to `run91`/`run97`; title windows pixel-identical to `run91`/`run92`; ws+60 fps: `targets at 960x508`, `presentation frames 960x544`. One capture of the game-over freeze frame (`run108`, 6504) had a 1-px light column at the picture's right edge that `run97` (same frame, same path) didn't; reruns at that moment caught Vita3K's own "compiling shaders" overlay instead (`run110`). Settled in `run111`–`run117`, below. |
| `run111`–`run117` | captures now present the held frame again 3 times first (Vita3K could show an older swap buffer); 4:3 area 726 wide | Captures repeatable (two game-over runs pixel-identical). The light column was real: a 725-wide target never drew its last column (black at native in every capture, main's build too) and the game-over move left a stray colour there. 726 and 728 both fill it; with 726 every capture is lit to the last column, which matches the PS1 replay's column 511 at the freeze frame. Replay shots unchanged. |
| `run119`, `run120` | gems step script, `widescreen=1 interp=1`, `menu_test=6420,6520,6620` (three widescreen toggles in Artisans), before and after flushing pending copies in `drop_target()` | No red/green textures in either (the hardware report on `3b4126a` is not reproduced); shots and windows identical between the two. |

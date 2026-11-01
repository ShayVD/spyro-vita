# Testing Spyro Vita on a PS Vita (and in Vita3K)

This is the M3 Vita build. It aims to boot to the title screen and play what the linux32 host
plays, drawn by the Vita's GPU. Read "What to expect" before you test.

## What you need

- A PS Vita or PS TV with HENkaku/Ensō (any firmware that runs homebrew), and VitaShell.
- Your own copy of *Spyro the Dragon* (USA, SCUS-94228), dumped to a `.bin`/`.cue`
  (MODE2/2352, one data track). The VPK contains no game data at all.
- Optionally a computer with Python 3, to extract the files from the dump instead (step 2).
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

The result is `build/vita/spyro-vita.vpk` (title "Spyro Vita", title ID `SPYV00001`, version
01.01). Besides `eboot.bin` and `param.sfo` it holds the bubble icon and LiveArea (drawn at build
time by `tools/vita/make_livearea.py`, decisions D55, or, as now, converted from `assets/livearea/` (the committed official art), or from your own art in the gitignored `disc/livearea/` when that holds `icon0.png`, `bg.png` and `startup.png`: `tools/vita/livearea_local.sh LOGO TITLE [GATE]` makes them, then re-run cmake; D59), `README.md` and `licenses/`. CI builds the
same VPK for every pull request and push to `main` (`.github/workflows/build.yml`; locally:
`docker run --rm -v "$PWD:/work" -w /work vitasdk/vitasdk:latest tools/ci/build_vpk.sh`, D57). Notes:

- The game's PS1 layout comes from the matching build's `psx.map` when there is one
  (`tools/build_matching.sh`, or point `-DSPYRO_MATCHING_DIR=` at another checkout's
  `build/s1-matching`), else from the reduced copy `tools/layout/psx.map` (D57). After a change
  to the decomp or its patches, refresh the copy with
  `tools/reduce_map.py build/s1-matching/build/psx.map tools/layout/psx.map`. CMake warns when it
  is stale.
- The code generators need a Python 3 with PyYAML. On macOS `/usr/bin/python3` has it, which is
  why `-DPython3_EXECUTABLE=/usr/bin/python3` is passed.
- VitaSDK lives at `/usr/local/vitasdk`, with vitasdk's SDL2 and vitaGL packages.

## 2. Install

1. Copy `spyro-vita.vpk` to the Vita (VitaShell: SELECT starts the FTP server, or use USB) and
   install it from VitaShell (press X on the file). A bubble called "Spyro Vita" appears.
2. Copy the dump's `.bin` and `.cue` (or a CloneCD `.img`, or the `.bin` alone) to
   `ux0:data/spyro/`. The game reads the image directly (decisions D60): 2352- or 2448-byte
   sectors, any pregap. With several images it takes the first `.cue` by name, then the first
   `.bin`/`.img`; `spyro.log` names the one it used (`libcd: disc image ...`).
3. Start "Spyro Vita". The first start with an image hashes its `WAD.WAD` once (a panel shows the
   progress) and writes `ux0:data/spyro/save/disc-verified.txt`; `SCUS_942.28` is hashed on every
   start. The log has `disc: <image>, opened in N ms (R reads, K KB)`.

**Or the extracted files:** `python3 -I tools/extract_disc.py "Spyro the Dragon (USA).cue"
spyro-files` (decisions D58) and copy the *contents* of `spyro-files/` to `ux0:data/spyro/`
(`manifest.json` included, the `.STR` files as they are). When `manifest.json` is there it wins over
any image (`libcd: extracted files ...` in the log).

When the game can't start it shows a panel saying why, and X, O or START quits (the same text is in
`spyro.log`, after `disc:`):

| Panel title | When |
|---|---|
| `NO DISC IMAGE FOUND` | no `manifest.json` and no image in the folder |
| `AN ISO HAS NO MUSIC` | a 2048-byte `.iso` |
| `CONVERT THE CHD FIRST`, `UNPACK THE ECM FIRST`, `A PBP CAN'T BE USED` | `.chd`, `.ecm`, PSP `EBOOT.PBP` |
| `WRONG DISC` | no `SCUS_942.28` (the serial and region from `SYSTEM.CNF`, e.g. `SCES-00900 (PAL, Europe)`), or no PlayStation file system |
| `WRONG VERSION OF THE GAME` | `SCUS_942.28`'s SHA-1 isn't `84e3728a…` |
| `BAD DUMP` | `WAD.WAD`'s SHA-1 isn't `ab680013…` (first start only) |
| `THE IMAGE IS CUT SHORT`, `THE IMAGE IS DAMAGED`, `CAN'T READ THE IMAGE` | a truncated image, an unreadable file list, an unknown format |
| `THE .BIN IS MISSING`, `CAN'T READ THE CUE FILE` | a `.cue` naming a file that isn't there, or whose track 1 isn't MODE2 |
| `MANIFEST.JSON IS DAMAGED` | the extracted files' manifest can't be read |

Files the app writes:

| Path | What |
|---|---|
| `ux0:data/spyro/spyro.log` | the log. **Please send it back after a test**, whatever happened |
| `ux0:data/spyro/save/card0` | memory card 1 (created when the game first saves) |
| `ux0:data/spyro/save/state1.bin` … `state3.bin` | quick-save states from the menu (3.8 MB each; decisions D51) |
| `ux0:data/spyro/shots/` | PNGs, only if `config.txt` asks for them |

## 3. Optional `ux0:data/spyro/config.txt`

`key=value` lines, `#` starts a comment:

| Key | Meaning |
|---|---|
| `disc=PATH` | the disc image or the extracted files are somewhere other than `ux0:data/spyro` |
| `frames=N` | quit after N vblanks (60 per second); 0, the default, never quits |
| `shot=V1,V2,...` | save a PNG of the picture at these vblanks, in `ux0:data/spyro/shots/` |
| `shot_every=N` | and every N vblanks |
| `stats=N` | 0: no timing lines; 1 (default): one line every 300 vblanks; 2: also each pad change |
| `filter=N` | 0: nearest-neighbour scaling; 1 (default): linear |
| `reartouch=1` | the rear touch pad's left/right half is L2/R2 (so is the right stick's X, unless `free_camera=1`) |
| `analog=0` | a digital pad: the left stick works as the d-pad, 8 directions. The default, 1, is a DualShock in analog mode, so Spyro moves in any direction and at any speed (decisions D46) |
| `free_camera=0` | the right stick is L2/R2 again. The default, 1, is the free camera: the right stick turns the camera (the further you push it, the faster it turns) and tilts it up and down. Let go and the game's own camera takes over again. Needs `analog=1` (D47, D53) |
| `cam_invert_x=0`, `cam_invert_y=0` | flip the free camera's left/right and up/down back. Both default to 1 (D53). Invert Y also flips Triangle look mode's up/down: on (the default), up looks up in both; off, up looks down, as in the original's look mode. `tools/vita/vita3k_run.sh` writes all three as 0 so scripted runs match the host |
| `touch_menu=0` | a tap in the middle of the front touch screen no longer opens the options menu (default 1: it does, as SELECT+START; D46) |
| `cam_speed=N` | the free camera's turn speed, 50 to 200 percent (default 100: as fast as L2/R2) |
| `realtime=0` | virtual vblanks: every VSync is exactly one vblank however long the frame took (scripted tests; the game runs slow when the CPU can't keep up) |
| `pad=PATH` | play a pad script (the linux32 host's `--pad` format) on top of the controller |
| `wav=1` | also write the audio to `ux0:data/spyro/audio.wav` |
| `gpu=0` | draw on the CPU (the software rasterizer) instead of the GPU; `gpu=1` is the default |
| `res=native` | GPU: the resolution it draws at (decisions D40; also the menu's `RESOLUTION`, D48). `native`, the default: the screen's own pixels (the 4:3 picture is 726×544, 16:9 is 960×544). `ps1` (or `1`): the PS1's 512×240, scaled up when shown, as on a TV (cheapest). `2`…`4`: N times the PS1's, scaled down when shown (smoother edges, more GPU work). `gpu_res=` is the old name |
| `gpu_time=1` | GPU: one GPU-timed frame per 300 vblanks in the log (see below). Default 0 since D68: its two `glFinish` calls are a hitch every 5 s at 60 fps. `gpu_time=2` also times that frame's scene on its own and counts GL draw calls and vertex uploads: measurement only, for `docs/fps-plan.md`'s Run A; don't combine it with `bench=` |
| `shot_hd=1` | GPU: each shot also as a 960×544 PNG of the screen (`shot-V-hd.png`; black in Vita3K) |
| `gpu_readback=0` | GPU: read drawn pixels back with `glReadPixels` instead of replaying the draws on the CPU (decisions D35; for testing) |
| `gpu_dump=V` | GPU: log every batch of triangles drawn at vblanks V and V+1 (debugging) |
| `mixer=N` | where the sound is mixed (decisions D37): 1 (default) on its own CPU core; 2 the same, but each vblank waits for its mix (debugging); 0 on the game thread, as before |
| `render_thread=0` | GPU: every GL call on the game thread, as before D63. Default 1 since D68: a render thread of its own on core 2 and the game on core 0; the same pictures, the game thread 23–53% busy at 60 fps on hardware instead of 93% at 30 (C1 run). A `render N:` line every 300 vblanks says how busy that thread was and how long the game waited for it. A `0` here needs `config_version=3` too, as for the next two (D68) |
| `xa_ahead=0` | the XA music's sectors read one at a time on the game thread. Default 1 since D68: read ahead of the CD head on core 1 (D64), the same sectors. An `xa N:` line every 300 vblanks |
| `gpu_opaque=0` | GPU: every atlas triangle drawn with the `discard` shader. Default 1 since D68: with `tex_cache=1`, triangles whose texture has no transparent or semi-transparent texel are drawn by a shader without `discard`, the same pixels (D65). The `atlas N:` lines count them |
| `gpu_direct=0` | GPU: each widescreen or 60 fps picture copied into its own slot, and the screen cleared under it, as before D69. Default 1: a picture that is one of the game's draw areas whole is shown straight from it, and when the game clears that area for a later frame the slot takes its texture instead of a copy (one full-screen GPU pass fewer per picture, the same pixels). The `gpu N: ... presentation pictures` line every 300 vblanks counts them |
| `gpu_lean=1` | GPU: with `tex_cache=1` or `tex_filter` and neither `persp` nor `subpixel`, a 28-byte vertex instead of 40 (the same pixels), and one atlas lookup for a quad's two triangles (D70). Default 0 until the hardware runs. The `gpu_time=2` line's vertex upload KB shows it |
| `widescreen=0` | 4:3 instead of the default 16:9 (a wider view filling the screen; the game itself plays exactly as in 4:3; `docs/presentation.md`) |
| `interp=1` | experimental 60 fps (default 0: 30 fps; decisions D44): an extra in-between picture each tick (camera, Spyro, enemies and objects, their animation, particles). It doesn't fit the Vita's CPU at 16:9 yet, so most ticks fall back to 30 fps. `interp=2`: without animation blending; `interp=3`: camera only. Written by hand, `interp=1` needs a `config_version=2` line too; without one it's taken as the old default an older build's menu wrote, and read as 0 (the menu writes both) |
| `widescreen_exact=1` | keep the original 4:3 game logic in normal play too (one extra hidden draw per tick: slower). By default the 16:9 view is drawn once and enemies at its edges act as on-screen ones; attract demos always keep the 4:3 logic |
| `shot_hold=MS` | after each shot, hold the game until `hold.ack` appears in `ux0:data/spyro/`, at most MS ms (Vita3K window captures) |
| `poke=V:ADDR=VALUE` | at vblank V, write a 32-bit word into PS1 RAM (hex; tests, like the linux32 host's `--poke`; up to 16) |
| `interp_budget=MS` | skip the in-between picture while the game's CPU time per tick plus one, averaged over a few ticks, would pass MS (default 26; `0` never skips). Two pictures taking the render thread more than 15/16 of a tick, or late pictures, skip it too. Skipped ticks show 30 fps instead of slowing the game down. After a miss 30 fps is held until the costs have fitted with room to spare for at least a second, and longer after repeated misses (D68) |
| `pad=` touch | in a pad script, `tap=X,Y`, `hold=X,Y` and `swipe=X,Y` (percent of the screen) play a gesture on the front touch screen (tests of the tap that opens the menu) |
| `menu_test=V1,V2,...` | tests without a controller: open the options menu at these vblanks (up to 4) and press down, right, up, right, circle (60 fps one step on, widescreen toggled, close) |
| `menu_keys=B1,B2,...` | with `menu_test`: press these instead (`up down left right cross circle triangle start`, one per 4 pictures; end with `circle` unless an action closes the menu), to test any row (D50) |
| `skip_logos=1` | start at the title: no Sony or Universal logo and no title flythrough (the menu's `SKIP INTRO LOGOS`; everything they load still loads; D50) |
| `tex_filter=N` | GPU: smooth textures; 1: bilinear (cheap); 2: 3-point, N64 style (costs more GPU); 0, the default, keeps the PS1's sharp texels (decisions D39, D43; the menu's `TEX FILTER`) |
| `persp=1` | GPU: perspective-correct textures on 3D polygons (no PS1 texture warping; D39; the menu's `PERSPECTIVE TEXTURES`) |
| `subpixel=1` | GPU: 3D vertices at their exact positions instead of whole pixels (no PS1 wobble; D39; the menu's `SUB-PIXEL VERTICES`) |
| `draw_distance=N` | experimental: the world's detail and the objects drawn further away, percent: `150` or `200` (default `100`, the original; D49, the menu's `DRAW DISTANCE (EXP.)`). Picture only; the game plays the same. Costs frame rate in open areas, see "Testing the draw distance" |
| `tex_cache=0` | GPU: decode textures from VRAM per pixel instead of the decoded atlas (`tex_cache=1`, the default, gives the same pixels faster; D41) |
| `state_slot=N` | the menu's state slot, 1–3 (the menu writes it) |
| `state_test=V:save:N,V:load:N,...` | tests without a controller: save or load state slot N at vblank V (D51; up to 8) |
| `bench=1` | GPU: measure the picture options (leave the game alone for ~4 minutes; `bench final` lines in the log; D41) |

**Options menu:** hold SELECT and press START, or tap the middle of the front touch screen, in any screen. The game stops while the menu is open. The rows are grouped under section headers (PICTURE, CONTROLS, GAME, SAVE); with more rows than fit, the list scrolls and arrows at the right show there is more. Up/down chooses a row, left/right (or ✕) changes it, ○ (or START, △) closes. Changes apply at once and every row is written back to `config.txt` (with `config_version=2`) when the menu closes; other lines in the file stay as they were. Under PICTURE: widescreen (on by default), 60 fps (`60 FPS (EXPERIMENTAL)`, off by default) the 60 fps budget, then (D48) `TEX FILTER` (OFF, BILINEAR, `3-POINT (SLOW)`), `PERSPECTIVE TEXTURES`, `SUB-PIXEL VERTICES`, `RESOLUTION` (NATIVE, PS1, 2X–4X) and `DRAW DISTANCE (EXP.)` (OFF, 1.5X, 2X; D49). The four GPU rows are hidden with `gpu=0`. The first time a filter, perspective or sub-pixel setting is chosen, its shaders compile: the menu pauses a few seconds (the log says how long). Under GAME (decisions D50): `SKIP INTRO LOGOS` (off by default, from the next start), then the game's own cheats as one-shot actions, never saved: `99 LIVES`, `LEVELS OPEN` (all levels and homeworlds open; kept in your save once you save), `CREDITS`, and `WARP TO` (choose a level) with `LEVEL WARP` (an unused developer cheat left in the game). Press ✕ once to ask and again to apply; the menu closes, the game opens its inventory and takes the cheat there, as if its code had been typed. The cheats work only in a level (playing, paused or in the inventory), never at the title or in a demo: there they read `IN A LEVEL`. The game never sees the buttons that open or close the menu. The rows are a table that each feature file adds to (decisions D45).

**Quick save (the menu's SAVE section, decisions D51):** `STATE SLOT` picks slot 1, 2 or 3; `SAVE STATE` saves the whole game there, `LOAD STATE` (its value names the level the slot holds, or `EMPTY`) puts it back exactly as it was. Choosing either closes the menu; the save or load happens a moment later, at the start of the game's next tick, and a panel says what happened for about 2 seconds (`STATE 1 SAVED`, `STATE 2 LOADED`, `NO STATE IN SLOT 3`). States are separate from the game's own memory-card saves, which work as before; loading a state doesn't change the memory card. A state only loads in the build that made it: after installing a new VPK, old states show `OTHER BUILD` and are refused. There is no automatic save when the app is suspended or closed.

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
  resolution; the frame is ~G ms late on screen at most, once per 5 seconds. Only with
  `gpu_time=1` or `2` (default 0 since D68).
- With `gpu_time=2`, also `gpu N: gpu_time=2: the timed frame's scene took the GPU S ms after its
  last draw` (S: the GPU's own time for the game's picture, timed with a `glFinish` at the capture;
  the timed frame's tail is then only the capture and present passes) and `gpu N: gpu_time=2: G GL
  draw calls for D game draws; vertex uploads K KB in T ms`. See `docs/fps-plan.md`.
- At start, `clocks: ARM … MHz, bus …, GPU …, GPU crossbar …` gives the clocks the Vita actually set
  (`main.c` asks for 444, 222, 222 and 166).
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

The PS1 pad maps one to one: ✕ ○ □ △, START, SELECT, L, R, the d-pad. The pad is reported as
a DualShock in analog mode (decisions D46), so the left stick moves Spyro in any direction, as on a
PS1 with a DualShock; the d-pad still works and wins while pressed. **L2/R2 turn the camera: push
the right stick left (L2) or right (R2).** With `reartouch=1` in `config.txt` the rear touch pad's
left/right half does the same (read with `sceTouch` since D46; before, it did nothing with the
GPU renderer). A PS TV's DualShock has real L2/R2/L3/R3 (decisions D27).

In the options menu (SELECT+START), CONTROLS:
- `ANALOG STICK` OFF goes back to the digital pad (the left stick is the d-pad, 8 directions).
- `FREE CAMERA` ON makes the right stick turn the camera in proportion and tilt it up and down.
  Let go, and the game's camera swings back behind Spyro as after L2/R2. Look mode, cutscenes and
  scripted cameras are unchanged (D47).
- `INVERT CAMERA X` flips the turn. `INVERT CAMERA Y` flips the tilt and Triangle look mode
  together. OFF: up looks down, the game's own way in look mode and flight. ON: up looks up in both.
  Flight itself is unchanged.
- `CAMERA SPEED` sets the turn speed, 50–200%.
- `REAR TOUCH L2/R2` is `reartouch=`.
- `TOUCH TO OPEN MENU` (on by default): a short tap in the middle of the front touch screen opens
  the menu, and another tap closes it. Swipes, held fingers and touches near the edges don't count.

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
   `~/Library/Application Support/Vita3K/Vita3K/fs/ux0/data/spyro/` on macOS. Runs from a disc
   image need them too: `DISC=DIR` (below) moves `manifest.json` aside for the run.
2. `tools/vita/vita3k_run.sh OUT_DIR [TIMEOUT_S] [config line]...` writes `config.txt`, installs
   and runs the VPK (`Vita3K --frontend sdl -c OUT_DIR/vita3k-config.yml -w
   build/vita/spyro-vita.vpk`, with a copy of Vita3K's config at log level ERROR), waits for
   `frames=` or the timeout, and copies `spyro.log` and the shots to `OUT_DIR`:

```bash
tools/vita/vita3k_run.sh logs/run2-vita-title 150 shot_every=120 frames=2400
```

Keep Vita3K's log level at ERROR (the default; `VITA3K_LOG=N` changes it): TRACE fills the disk.

`DISC=DIR` runs from a disc image (D60): the files directly in `DIR` are hard-linked into
`ux0:data/spyro` and `manifest.json` is moved aside, both undone when the script ends (and by the
next run if this one was SIGKILLed). `DIR` may hold a refused format, or nothing, to see a disc
screen; `frames=N` also ends the disc screen after N vblanks, and `GRAB=S` grabs the window S
seconds in as `win-grab.png`:

```bash
DISC=disc/image tools/vita/vita3k_run.sh logs/run2-vita-image 400 shot_every=600 frames=4200 wav=1
```

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

### Picture rows and draw distance (D48, D49, branch `claude/vita-picture-lod-423bd2`)

Vita3K, `realtime=0` unless noted. Logs in `logs/`:

| Run | What | Result |
|---|---|---|
| `run5`, `run12`, `run16`, `run21-vita3k-menu` | Stone Hill demo, `menu_test=4500 menu_keys=dddrdrdrdrdrro` (this branch's letters, before D50's names): bilinear, perspective, sub-pixel on, resolution PS1, draw distance 1.5X then 2X, from the menu | Bilinear shaders compiled on demand (2.7–3.2 s); `menu: saved ... tex_filter=1 persp=1 subpixel=1 res=1 draw_distance=200`; window captures show the filtered PS1-resolution picture at once. Shots at 4300 and 4700 byte-identical across the four builds; at 5300 they differ because the later builds' limiter (watching the world's bound too) holds a lower scale in that heavy Stone Hill stretch (`lod 5400`: x1.46, x1.03, x1.00). |
| `run6-vita3k-boot` | the same options in `config.txt` from boot | replay shots byte-identical to the menu run; window capture at 5300 byte-identical (at 4700 the draw distance was still growing after the menu) |
| `run13-vita3k-bench` | `bench=1` to vblank 13800 | 18 sets, `bench final` at 13560, configured options back. Vita3K's timings are not the Vita's. |
| `run17`, `run20-vita3k-gems-dd2` | gems script, 16:9, `draw_distance=200` | all 27 `lod` lines identical to the linux32 host's (`logs/run19-gems-dd2`: scale, buffer use, back-offs, extra mobys); memory card byte-identical to the host's at 1x |
| `run7-vita3k-rt-dd100/200` | `REALTIME=1`, title and demos | no usable cost difference: Vita3K's CPU is not the Vita's |
| `run24-vita3k-menu` | after merging main (#28 controls, #29 cheats, #30 quick save): as `run21`, with D50's `menu_keys=down,down,down,right,down,right,down,right,down,right,down,right,right,circle` and `widescreen=1` (the runner now writes `widescreen=0`) | shots and window captures at 4300, 4700, 5300 byte-identical to `run21`; the same `lod` line; shaders compiled in 2.8 s; the saved line also carries the CONTROLS, GAME and SAVE keys |
| `run28-vita3k-state-dd2` | gems, `draw_distance=200`, `state_test=8100:save:1,8250:load:1` | saved in 23 ms, loaded in 23 ms; the `lod` window across the load at ×2.00, lowest ×2.00 (no ramp after a load) |

On the host after the same merge (`logs/run22-*`, `run26-*`, `run27-*`, `run29-*`): oracle replay of the six renderers 213/213; gems script (8,601 rows) with traces byte-identical at 1×, at 2× and with main's binary, the 28 shots at 1× identical to main's, memory cards identical, the 27 `lod` lines identical to before the merge; a tour of levels 10, 11, 15, 23, 40, 44, 61 (11,500 vblanks) with traces byte-identical at 1×, 2× and 2× guards-only (2×: peak 86 KB, no bound fired; guards-only: the world bound fired 13,145 times, peak 88 KB, no crash); save and load at 2× as in D49 (light spot identical in every chunk; heavy spot differs only in stale primitive bytes).

### Cheats and skipping the logos (D50, 2026-10-31)

| Run | What | Result |
|---|---|---|
| `run413-vita3k-skip-title` | `skip_logos=1 widescreen=0`, 4800 vblanks, shot every 120; the menu opened on the title (`menu_test=600`) and ✕ ✕ on 99 LIVES | 40/40 shots byte-identical to the host's `--skip-logos` run (`run410`): PRESS START at once, then the first attract demo. The log refuses the cheat (`cheats only in a level`) and saves `skip_logos=1`. |
| `run420-vita3k-menu-lives` | new game (skip on), menu at 6300 in Artisans, ✕ ✕ on 99 LIVES | `99 LIVES queued` at 6300, `applied in the inventory` at 6311. 120/120 shots byte-identical to the host queuing it at the same vblank (`run418`, lives 4 → 99). |
| `run421-vita3k-menu-warp` | the same, WARP TO → TOWN SQUARE, ✕ ✕ on LEVEL WARP | applied at 6311, Spyro glides into Town Square. 120/120 shots byte-identical to the host (`run419`). |
| `run422-vita3k-default-boot` | no `skip_logos`, 1200 vblanks | the Sony and Universal logos as before: 10/10 shots identical to the host. |

Again after merging the controls (#28, D46/D47; the Vita now reports an analog DualShock and CONTROLS has seven rows, so GAME's rows are 7 further down: 99 LIVES is 11 downs from the top, WARP TO 14):

| Run | What | Result |
|---|---|---|
| `run444-m28-vita3k-skip-title` | as `run413`, `menu_keys` 11 downs | 40/40 shots byte-identical to the host (`run433`); both ✕ refused (`cheats only in a level`). |
| `run445-m28-vita3k-menu-lives` | as `run420` | queued at 6300, applied at 6311; 120/120 shots byte-identical to the host (`run436`). |
| `run446-m28-vita3k-menu-warp` | as `run421` | applied at 6311; 120/120 shots byte-identical to the host (`run437`). |
| `run443-m28-vita3k-default-boot` | as `run422` | 10/10 shots identical to the host (`run438`). |

(`run440`–`run442` used the old key sequences, which now land on CONTROLS rows; not results.)

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
   18 option sets take turns, at 4:3 and 16:9, native and PS1 resolution, and the draw distance
   at 1.5× and 2× (D49), at 30 fps (`bench=2` keeps your 60 fps setting).
2. Send `spyro.log` back. The `bench final:` lines (at vblank 13560, about 3.8 minutes in) give
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

**Testing the picture rows and the draw distance** (decisions D48, D49), with the GPU build:

1. `config.txt`: only `stats=1`. Start a game and go to Artisans.
2. **Picture rows:** open the menu (hold SELECT, press START). Under PICTURE, step `TEX FILTER` to BILINEAR,
   then `3-POINT (SLOW)`, then OFF; switch `PERSPECTIVE TEXTURES` and `SUB-PIXEL VERTICES` on and off;
   step `RESOLUTION` through PS1, 2X and back to NATIVE. Each change should show behind the menu at once.
   The first filter, perspective or sub-pixel change pauses a few seconds while its shaders compile; the
   log says `gpu: programs for ... compiled in N ms`. Any `don't compile` line is what to send back.
   Close the menu: the log's `menu: saved ...` line lists the values, and they come back after a restart.
3. **Draw distance:** set `DRAW DISTANCE (EXP.)` to 2X and close the menu. Walk and glide around Artisans
   and one open level (Stone Hill or Town Square). Look at what appears in the distance and at the left and
   right edges: textured hills and walls should reach further, and gems and objects should appear sooner. A
   frame with a missing patch of scenery can happen (a hard bound stopped the world draw before the
   buffer ran out); note where.
4. In the log, `lod N:` lines every 5 seconds:
   - `x2.00 average` means the full distance held. Lower means the limiter backed off; it starts again
     at 1x at each new level.
   - `primitives ... peak ... of 110 KB`: how full the game's primitive buffer got.
   - `stopped early: world W, mobys M`: how often a hard bound stopped a loop.
   - Compare the `vblank N: ... P pictures` and `time N:` lines with the same walk at OFF. The cost is
     CPU (more polygons to walk and submit) at a moment when 16:9 is already about 93% busy.
5. `bench=1` now also measures `16:9 native, draw_distance=150` and `=200` (the `bench final:` lines).

**First hardware test (main `3b4126a`, user):** red and green textures after the pause menu, the picture snapping between 16:9 and 4:3, scenery popping at the edges, 20–26 fps with widescreen. Causes and fixes: `docs/presentation.md` "Fixes from the first hardware test".

**Testing widescreen and 60 fps** (`docs/presentation.md`, decisions D30, D44; widescreen on by default, 60 fps experimental and off by default: turn it on in the menu for steps 5 and 6), with the GPU build:

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

**Testing quick save** (decisions D51; the menu's SAVE section):

1. Play into Artisans, open the menu (SELECT+START), go to SAVE (the last section: ↑ twice from the top lands on it), `SAVE STATE` (✕). The menu closes and `STATE 1 SAVED` shows for about 2 seconds. In the log: `savestate: save took N ms` and `savestate: STATE 1 SAVED`. `ux0:data/spyro/save/state1.bin` is 3,806,132 bytes.
2. Move somewhere else (collect a gem, lose some health), then `LOAD STATE`. Spyro, the camera, the gem count, enemies and the music must be exactly as they were at the save, and the game must go on normally. Note how long the picture stops (the log's `load took N ms`).
3. Save in one level, go through a portal to another one, load: back in the first level as it was.
4. Save while the music plays and while flying (Sunny Flight), load later: the music goes on from where it was. Also save while running with the left stick and turning the camera with the right one (`FREE CAMERA` on): after the load the camera is where it was, and the sticks work at once.
5. **Quit the app (PS button, close) and start it again**, then `LOAD STATE` straight from the title screen. This is the important one: it works only if the Vita puts the game's memory at the same addresses every launch (the log's `arena at 0x...` line must be the same in both launches). If the menu shows `OTHER LAYOUT` instead of the level name, send the logs of both launches.
6. Check the game's own saving still works: save at a fairy, quit, Load Game from the title.
7. Send back `spyro.log` and anything that looked or sounded wrong after a load.

**Measuring what 60 fps costs** (`docs/presentation.md` "60 fps on the Vita"):

1. Play two minutes of Artisans free roam with 60 fps on (the menu's `60 FPS (EXPERIMENTAL)`: ON; widescreen stays on by default).
2. Then two minutes in the same places with `widescreen=0`.
3. Send `spyro.log`. Its `draw N:` lines split the game's CPU time per renderer (update, env, mobys, spyro, fx, particles, sky), next to the `time` lines (ot, raster) and the `present` lines (how many in-between frames fit).

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

### Quick save (D51, branch `claude/vita-quicksave-a54846`)

The gems script (M2's `run9-gems/replay_pad.txt`), with a state saved at vblank 7000 (Artisans, music playing) and loaded later; host references from the same tree (`--state-hash`: a hash of every chunk at every main loop top; `tools/savestate_compare.py`). Logs in `logs/`:

| Run | What | Result |
|---|---|---|
| `run1-state-A`, `run1-state-B` | host: save at 7000; B also loads it at 7602 | B after the load against A: 899 loop tops identical in every chunk (arena, VRAM, SPU, XA, CD, …), 30.0 s of audio byte-identical, 88/88 shots identical, same final trace row (4 gems). |
| `run1-state-C` | host: a new process loads A's `state1.bin` at its first loop top (vblank 437, during the boot logos) | 899 loop tops identical to A, 30.0 s of audio identical, all shots identical. |
| `run4-state-G` | host: save at 4003, in the New Game intro cutscene; load at 4603 | The replayed stretch (4003–4601) identical to the first pass: 300 loop tops, 10.0 s of audio. |
| `run12-state-F3` | host: Sunny Flight (`--tour 15`), save at 7602 while flying, load at 8002 | Replayed stretch identical: 200 loop tops, 6.7 s of audio. |
| `run12-state-E2` | host: save on the title screen (1503), play New Game → intro → Artisans, load at 7703 | Back on the title; the replayed stretch (title, New Game, intro cutscene, loading Artisans, the walk to the gems) identical to the first pass: 3,102 loop tops, 103 s of audio. Started with `run1-state-A`'s memory card, so the card was the same in both passes (see below). |
| `run9-ws-ref`, `run9-ws-load` | host, `--widescreen --interp`: save on the title, load from inside Artisans (after its loading screen) | The 960×544 pictures after the load (1600–1800) identical to the run without the load, in 16:9 (before `port_present_reset()`, the presenter would have stayed on the 4:3 display until the counter passed the loading screen's vblank). |
| `run2-vita-state` | Vita3K, `gpu=1`, mixer thread: save at 7000, load at 7602 | 88/88 shots byte-identical to host A; the WAV identical to A's before the load and for the 30 s after it. Save 45 ms, load 39 ms (Vita3K's times). |
| `run6-vita-relaunch` | Vita3K: a new launch loads `run2-vita-state`'s `state1.bin` at vblank 437 | Accepted (arena at 0x89000000 in both launches, same layout); 22/22 shots identical to host A, 30 s of audio identical; the memory card unchanged. |
| `run8-vita-soft-state` | Vita3K, `gpu=0`: save at 7000, load at 7602, to 8000 | 80/80 shots identical to host A, 16.6 s of audio after the load identical. Save 6 ms, load 5 ms. |
| `run14-state-*` | host, after merging the controls (#28) and cheats (#29): A, B, C, G, F3 and E2 again (as `run1`, `run4`, `run12`) | All identical again: B 899 loop tops, 30 s of audio, 88/88 shots; C (new process) 900 loop tops, 30 s; G 300, 10 s; F3 200, 6.7 s; E2 3,102 loop tops, 103 s. `state1.bin` is now 3,806,132 bytes (the cheat queue in `GLOB`). |
| `run14-state-H`, `run14-state-H2` | host, `--analog --free-camera`, `logs/pads/analog_cam.txt` (the gems script to 7000, then both sticks: running, jumping, charging, the camera turned and tilted): save at 7120 with both sticks held, load at 7700; H2, a new process, loads H's state at its first loop top | H: the replayed stretch identical, 290 loop tops, 9.7 s of audio. H2: 640 loop tops identical to H, 34/34 shots, the audio identical to both H's first pass and its replay (`savestate_compare.py` flags H2's audio only because H's own WAV restarts at its load). The free camera was turning at the save: `--peek 80075924` (`D_80075924`, the turn) reads 0x1F8, −0x3F8, 0x3F0 around it, and 0 throughout `run14-state-H0` (the same sticks without the free camera). |
| `run15-vita-state` | Vita3K, `gpu=1`, as `run2` | 80/80 shots and 16.6 s of audio after the load identical to `run14-state-A`. Save 23 ms, load 21 ms. |
| `run16-vita-analog` | Vita3K, `analog=1 free_camera=1`, `analog_cam.txt`, save at 7120, load at 7700 | 168/168 shots and the whole WAV (150 s, both sides of the load) identical to `run14-state-H`. |
| `run17-vita-menu-save`, `run18-vita-menu-load` | Vita3K, the real menu rows: `menu_test=7000 menu_keys=up,up,cross` (`SAVE STATE`); then a new launch from run17's save folder, `menu_test=7600 menu_keys=up,cross` (`LOAD STATE`) | run17: 77/77 shots identical to `run14-state-A`, the state saved at 7002 as with `state_test`. run18: started with run17's memory card, so from 2000 it went its own way (the title skipped "create save file?", below); after the load, shots 7100–8000 and 16.6 s of audio identical to `run14-state-A`. Arena at 0x89000000 in both launches. |
| `run7-vita-ws-notice` | Vita3K, `widescreen=1 interp=1`, window captures | Save and load work with the presenter on; the notice panel shows over the 16:9 picture (`win-7010.png`); textures right after the load. |

Two things the runs showed, neither a fault of the states:

- **The memory card isn't part of a state.** `run3-state-E` (the same as `run12-state-E2` but starting with no card) diverged at 1979 after the load: the first pass had created the card's save file at the title, so after loading the title-screen state the game found it and skipped its "create save file?" prompt, and the pad script went out of step. That is the intended behaviour (D51): loading never undoes or redoes card saves.
- **The mips2c stack holds host values.** Two separate processes can differ in a few bytes of the arena's emulated stack (0x80010000–0x8001FFFF) at the loop top, where it is dead: translated code leaves host values there (`0x407ffb18`, a qemu stack address, next to the one byte that differed in `run10-diag-E`). The game never reads them (host and Vita runs are byte-identical everywhere it matters). `--state-hash` hashes that range as `STAK`, apart from the rest of the arena, and `tools/savestate_compare.py` reports it but doesn't fail on it.

Not checked: on hardware (above). The menu rows are driven in Vita3K through `menu_keys=` (`run17`, `run18`), not by hand.

### The render thread, the XA read-ahead and the opaque path (D63–D66, branch `claude/fps-render-thread-dbf563`)

`realtime=0`, `widescreen=1` unless noted. "base" is the VPK built from the branch's base (`logs/vpk-base`). The switches are `render_thread=1`, `xa_ahead=1` and `gpu_opaque=1`; "all" is all three. Logs in `logs/`:

| Runs | What | Result |
|---|---|---|
| `vbase-gems-ws`, `vrt1-`, `vrt0-`, `vall-`, `vfin-gems-ws2`/`ws3` | gems script, shot every 200, `wav=1`: base, render thread, the same VPK with it off, all (two VPKs) | 44 shots, the WAV and the card byte-identical in every run |
| `*-gems-wsi0` | as above with `interp=1 interp_budget=0` (60 fps forced) | byte-identical; the same in-between frames in every window |
| `*-pause-ws` | the pause route (StoreImage readback for the pause and inventory backgrounds) | 70 shots and the card byte-identical |
| `*-demo` | 12,000 vblanks, the attract demos, `wav=1` | 24 shots and the 35 MB WAV byte-identical |
| `*-gems-state`, `vfin-gems-state-off` | save at 8100, load at 8250 | 85 shots identical; the state files with the thread on and off (same VPK) differ only in 3 dead bytes at the top of the emulated stack (0x8001FFFC) |
| `*-gems-cap`, `vfin-gems-cap2`, `vbase-gems-cap2` | `CAPTURE=1`, six window captures | byte-identical in all of them (all on, twice; base twice) |
| `*-gems-img` | from the disc image (`DISC=disc/image`) | byte-identical to the extracted files' run; with all on, 750 of 750 music sectors per window from the read-ahead |
| `vall-bench`, `vbase-bench` | `bench=1` to 14000 | 18 sets, shaders compiled through the sync call, options put back |
| `vall-menu`, `vbase-menu` | `menu_test=4500`: 60 fps on, widescreen off, close | captures after the menu identical (the base capture at 4400 shows Vita3K's own shader-compile overlay) |

Vita3K's GPU emulation failed in three runs ("Unimplemented command opcode", "already recording"): `vall-gems-cap` and `vfin-gems-ws` with the render thread, `vfin-gems-cap-off` with everything off. With the thread the game then waited for it (`render: still waiting`) instead of Vita3K exiting. Reruns passed. Vita3K's timings are not the Vita's.

### The new defaults and the 60 fps hold (D68, the same branch, after the D67 merge)

The defaults (`render_thread=1`, `xa_ahead=1`, `gpu_opaque=1`) against all three off in the same VPK, `realtime=0`, widescreen, 60 fps forced (`interp=1 interp_budget=0`). Logs in `logs/`:

| Runs | What | Result |
|---|---|---|
| `v68-gems-i0-on`, `-off` | gems script, shot every 200, `wav=1` | 44 shots, the WAV and the card byte-identical |
| `v68-pause-i0-on`, `-off` | the pause route | 70 shots and the card byte-identical |
| `v68-cap-i0-on`, `-off` | `CAPTURE=1`, six window captures | 4 byte-identical; at 5000, 732 pixels differ, all in the window's 16-pixel edge; at 1800 the "off" grab caught Vita3K's shader-compile overlay |
| `v68-gems-budget` | the default budget (26 ms) | Vita3K's render thread takes 15–35 ms a picture: one fall to a held 30 at the start, none after, no flipping |
| `v68-migrate` | `config_version=2 render_thread=0 gpu_opaque=0`, then the menu's save | both `0`s ignored with a log line; the save writes `config_version=3` without them |

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

### The 28-byte vertex (D70, branch `claude/fps-cheap-interp-52b4c3`)

`gpu_lean=0` against `gpu_lean=1` in the same VPK, `realtime=0`, widescreen, 60 fps forced (`interp=1 interp_budget=0`). Logs in `logs/`:

| Runs | What | Result |
|---|---|---|
| `v69-gems-off`, `-on` | gems script, shot every 200, `wav=1` | 44 shots, the WAV and the card byte-identical; the same in-between frames in every window |
| `v69-pause-off`, `-on` | the pause route | 70 shots and the card byte-identical |
| `v69-cap-off`, `-on` | `CAPTURE=1`, six window captures | all six byte-identical |
| `v69-menu-persp-on`, `-off` | `menu_test=6000` turns `persp` on (28-byte → 40-byte vertex), or off | the other program set compiled in ~3 s; after the switch back, the captures byte-identical to `v69-cap-on` |
| `v69-rt-off`, `-on` | `REALTIME=1`, `gpu_time=2` | 40.0 → 28.0 bytes a vertex (1,389 → 967 MB); Vita3K's upload and `expand()` times unchanged (its own GL keeps the render thread 99% busy) |

# PC build (Linux, Windows, macOS; D240, D241, D242, D243, D265)

**Status (2026-11-26):** macOS on Apple silicon builds and plays natively (D265; the owner's play is next). Linux and Windows build and play (the US disc from boot to Artisans; Windows checked under Wine). CI builds both and starts each without a disc (Windows on a Windows runner). The GPU renderer draws the game at the window's resolution (`gpu=1`, the default, D243); the software rasterizer stays as `gpu=0`. Next: a person with a real GPU and gamepad.

A native desktop build of the game: a window, keyboard and gamepads, sound, widescreen and 60 fps, from the player's own disc. It is the same code as every other build (the decomp, `src/game`, `src/platform/common`) with a small platform layer, `src/platform/pc/`, on **Encore's platform interface** (`encore/platform.h`, API 2, and its SDL3 PC backend `targets/pc/encore/enc_pc.c`, vendored; D247: window, GL context, input, audio, time, threads, folders) and **OpenGL 3.3 core** (the picture). What the interface can't say yet goes through `gaps.c`, the only file left that calls SDL ([below](#on-encores-platform-interface-d245)).

## How it works

### 64-bit, with 4-byte pointers: the low image

Spyro was written for a 32-bit machine. Its structures hold 4-byte pointers, and the decomp keeps the PS1's layouts byte for byte, so the game's memory can't simply be rebuilt with 8-byte pointers. The PC build uses the low-image model that linux64 and the PS4 build use (`docs/ps4-plan.md`, D173, D174):

- every pointer field the game stores in its own memory is a 32-bit pointer (`P32F(T *)`: clang's `__ptr32 __uptr`, `src/compat/p32.h`; the generated `patches/spyro-1-ptr32`). Only clang has `__ptr32`, and only for x86 targets: there is no ARM64 build (Apple Silicon Macs, ARM Linux) with this model.
- everything the game can point at lies below 2 GB, so those 32 bits hold the whole address: the executable sits at 0x400000 (non-PIE on Linux, a fixed base on Windows), the 2 MB of PS1 RAM is a `.bss` array in it (`arena.c`), and the game runs on a thread whose stack is in the image too (the hand C takes addresses of locals). `main()` checks all three and refuses to run otherwise.
- **Linux:** nothing of the host's may be below 4 GB, so a host pointer cut to 32 bits by mistake crashes instead of quietly aliasing the game's memory. Encore's `lp64_low_image_dyn.c` (vendored in `third_party/encore`, D242) reserves every free page from 64 KiB to 4 GB (`PROT_NONE`) from `.preinit_array`, which the dynamic loader runs before any shared library starts. glibc's malloc, SDL, the X11/Wayland and GL drivers and every thread stack then land above 4 GB. (The headless linux64 build is static and needs its own malloc; this one doesn't.) `LP64_LOW_TRAP=0` turns the reservation off.
- **Windows:** the executable has a fixed base of 0x400000 (no ASLR) and is not large-address-aware, so Windows keeps the whole process below 2 GB: every address fits 32 bits and nothing can be cut (`low.c`, Encore's `lp64_low_image_win.c` probes where memory lands). The game thread is an ordinary thread there (its stack is below 2 GB too; `main.c` checks). Wine doesn't apply that 2 GB limit to 64-bit programs; the game's own memory is low there anyway (the image and its `.bss`).

### Two threads

- The **main thread** owns the platform (Encore's PC backend, SDL underneath): the window, its GL context (Encore's GPU mode), the keyboard and gamepads. At every display refresh it shows the newest frame that is due.
- The **game thread** runs the game, the renderer (the GPU renderer on a GL context of its own, or the software rasterizer, below) and the SPU, paced by a 60 Hz clock of real time (`vblank.c`), the same catch-up model as the Vita and the PS4 (`common/clock.c`): if a tick runs late, the vblanks it missed run at once, so the game keeps its speed and the audio its continuity.

Frames go from one to the other with the vblank they are due at (`frames.c`), as the Vita's render thread shows them (stage 7c, D112): the presenter's in-between frame (60 fps) and its widescreen frame are filed as they are drawn, a vblank ahead; at a vblank with none (boot logos, loading screens) the PS1 display is filed as it is. The game thread never waits for the display, and the display can run at any rate (60, 75, 144 Hz, variable refresh): each refresh shows the frame due by then.

### Picture, input, sound

- **Picture** (`video.c`): one texture, one quad: 4:3, or 16:9 for the presenter's widescreen frames, as large as the window allows, black bars around. `filter=1` scales linearly, `filter=0` by nearest neighbour. F11 or Alt+Enter toggles (borderless) fullscreen; the window resizes freely. With `gpu=1` the texture is the GPU renderer's picture, already at the window's size; with `gpu=0` it is the software rasterizer's frame at the PS1's (512×240, the 224 lines the game draws shown), scaled up.
- **Input** (`input.c`, `pad.c`): Encore's pads (`enc_pad_read`: SDL3's gamepad API underneath, XInput, DualShock 4, DualSense, Switch Pro, …, up to 4) as an analog DualShock: face buttons by position, triggers as L2/R2 from half way, the right stick the free camera (the Vita's defaults). Keyboard: arrows or WASD the d-pad, K/Space Cross, L Circle, J/Left Shift Square, I Triangle, Q/E L1/R1, 1/3 L2/R2, Enter START, Backspace/Tab SELECT.
- **The options menu** (`menu.c`, `menu_rows.c`, D275): hold SELECT and press START (Back + Start on a gamepad; Backspace or Tab, then Enter on the keyboard), or press Esc, F1 or a gamepad's guide button. The game pauses (the menu runs inside its VSync wait) and the panel the Vita and PS4 show is drawn over the last picture. Up/Down (arrows or W/S) choose a row, Left/Right (or Cross: K, Space) change it, Circle (L), START (Enter), Esc or F1 close it. The game never sees the menu's buttons. Rows: PICTURE (FULLSCREEN, WIDESCREEN, 60 FPS, VSYNC, FILTER, RENDERER, PERSPECTIVE, SUB-PIXEL, RESOLUTION, DRAW DISTANCE), CONTROLS (ANALOG STICK, FREE CAMERA, INVERT CAMERA X/Y, CAMERA SPEED, CAMERA RECENTRE) and GAME (SKIP INTRO LOGOS and the cheats, the PS4's `menu_game.c`). Changes apply at once, except RENDERER, 60 FPS's 60 HZ (60 Hz logic, D130) and PERSPECTIVE or SUB-PIXEL when the GPU renderer started with both off: those wait for the next start, and a NEXT START row says so. Closing the menu saves every row's key to `config.txt` in the user folder and keeps the file's other lines. The GPU rows show only with `gpu=1` running.
- **Sound** (`audio.c`): the SPU's 44.1 kHz stereo into Encore's audio (`enc_audio_push`; an SDL3 audio stream underneath), which converts it for the device. Over 250 ms queued, new audio is dropped until 80 ms is left.

## Files

| what | where |
|---|---|
| the disc: a `.bin` + `.cue` image, `tools/extract_disc.py`'s files, or the Store copy's `EBOOT.PBP` | `--disc DIR`, else a `disc` folder beside the executable, else the user folder |
| saves (`save/`), `spyro.log`, `config.txt`, `shots/` | the user folder: `~/.local/share/spyro-vita/Spyro/` on Linux, `%APPDATA%\spyro-vita\Spyro\` on Windows |

`config.txt` takes `key=value` lines, and the command line takes the same keys as `--key value` (it wins): `fullscreen`, `window=WxH`, `widescreen`, `interp` (0 off, 1 the in-between frame, 4 60 Hz logic), `vsync`, `filter`, `analog`, `stats`, `gpu`, `persp`, `subpixel`, `res` (the renderer, below), the options menu's other rows (`draw_distance` 100–400, `free_camera`, `cam_invert_x`, `cam_invert_y`, `cam_speed` 50–200, `cam_recentre`, `skip_logos`), and for tests `frames=N` (exit after N vblanks), `shot=V1,V2` (a PNG of the window at those vblanks), `menu_test=V` (the menu opened at vblank V and driven by `menu.c`'s script: WIDESCREEN off, a scroll to the last row, close; a PNG `shots/menu-V-N.png` after each step; `menu_keys=right,down,...` presses those buttons instead: `up`, `down`, `left`, `right`, `cross`, `circle`, `triangle`, `start`, `select`, `menu`, `none`) and `virtual=1` (below). The menu writes the file back when it closes.

## The GPU renderer (`gpu=1`, D243)

The game is drawn by the GPU at the window's resolution: `src/platform/pc/gpu_gl.c`, a trimmed copy of the Vita's renderer (`src/platform/vita/gpu_gl.c`, D35 and on), with the same model and the same shaders.

- **The model:** PS1 VRAM stays a CPU array, and it is also the texture every textured primitive samples: the shaders decode 4-bit, 8-bit and 15-bit texels and the CLUTs themselves, texel 0 transparent, STP for semi-transparency. Each draw area the game draws into (Spyro's two 512×224 buffers) is a *target*, an FBO covering that VRAM rect at the window's scale. When something reads VRAM back (the pause and inventory backgrounds are StoreImage copies of the last frame), the target's pixels come back by replaying a log of what was drawn into it through the software rasterizer: the PS1's exact pixels, with no GPU read-back.
- **The shaders** are the Vita's GLSL ES 1.00 text. `glsl330()` adds `#version 330 core` and swaps the old words as whole words (`attribute`/`varying` to `in`/`out`, `texture2D` to `texture`, `gl_FragColor` to an `out` variable). Vertices go through VBOs and VAOs (the core profile has no client-side arrays).
- **Threads:** the game thread draws, on a second GL context that `video.c` makes beside the main thread's and shares with it (textures, sync objects). The presenter's 16:9 and 60 fps frames reach it through `PresentCapture`, as on the Vita. `capture()` draws the presentation pass's VRAM rect from the targets into a *picture*, one texture per slot of `frames.c`'s ring, and `commit()` files the slot with its due vblank. At a vblank with no presenter frame, the PS1 display is drawn into a picture the same way (VRAM under the targets). Each picture is fenced. The main thread waits on the fence, draws the picture into the window and fences its own draw, so the game thread doesn't draw that slot again before the main thread is done with it. A 24-bit display (the boot logos) is still converted on the CPU.
- **Options:** `persp=1` and `subpixel=1` (both on by default, the Vita's defaults, D39 and #177/#178) use the vertex cache's exact projections: perspective-correct textures and sub-pixel vertices. `res=N` draws the targets at N times the PS1's pixels instead of the window's size (`res=1` is the PS1's own resolution). Resizing the window or going fullscreen makes the targets again at the new size (at most every quarter second while a window is dragged).
- **Left out of the Vita's file:** the decoded-texture atlas (`tex_cache`, `tex_filter`), the 28-byte vertex, the fused and direct paths, draw joining, `gpu_opaque` and `gpu_uber`, dynamic resolution, the GL state shadow, the render thread and the Vita's meters. These exist for the Vita's GPU and CPU budget; a desktop GPU doesn't need them. `tex_filter` (filtered textures) is the one with a visible effect, and needs the atlas.
- **gpu=0** is the reference: linux32's software rasterizer as before, compiled with the Vita's `gpu_soft_names.h` (its entry points renamed `soft_*`); `gpu.c` dispatches between the two, as the Vita's `gpu.c` does. If the GPU renderer can't start (no shared context, a shader that won't compile), the log says so and the software rasterizer draws.
- **`virtual=1`** (tests): the clock counts the game's own vblanks, one for each vblank it waits for, so a run is never late. It shows the same frames at the same vblanks however fast the machine draws. A real-time run that falls behind (llvmpipe drawing `gpu=1` at 1280×720) reaches a pad script's presses at other moments, so two renderers can't be compared without it.

## Building

```bash
docker build --platform linux/amd64 -t spyro-pc -f tools/docker/pc.Dockerfile tools/docker
tools/pc/tc cmake -S . -B build/pc-linux -G Ninja -DSPYRO_HOST=pc -DCMAKE_TOOLCHAIN_FILE=cmake/pc-linux.toolchain.cmake
tools/pc/tc cmake --build build/pc-linux        # -> build/pc-linux/spyro
```

The image is amd64 (Debian 12, clang 19, SDL 3.4.18 built static). On an Apple Silicon Mac, point `DOCKER_HOST` at a Docker VM with Rosetta (`colima start --vm-type vz --vz-rosetta`): the build and the game then run at near-native speed. `PC_X=1 tools/pc/tc build/pc-linux/spyro --disc disc/image --frames 3000 --shot 600,2400` runs it under a virtual X display (Xvfb, Mesa's llvmpipe), which is how CI and a Mac check it.

### Windows

```bash
tools/pc/tc cmake -S . -B build/pc-win -G Ninja -DSPYRO_HOST=pc -DCMAKE_TOOLCHAIN_FILE=cmake/pc-windows.toolchain.cmake
tools/pc/tc cmake --build build/pc-win          # -> build/pc-win/spyro.exe
```

Cross-compiled in the same image with llvm-mingw (clang, the UCRT): one static `spyro.exe` that needs only Windows' own DLLs (Windows 10 and later have the UCRT). What Windows needed beyond Linux (D241):

- **`__ptr32` survives mingw's headers:** `_mingw.h` defines `__ptr32` and `__ptr64` as empty macros (MSVC's keywords, for other compilers), which silently made every `P32` field 8 bytes once any system header was in (the hand C's layout asserts caught it). `src/compat/p32.h` takes `_mingw.h` first and drops the two macros; clang's keywords remain.
- **The GNU bitfield layout** (`-mno-ms-bitfields`), so the PS1's structs keep their offsets; the BSD type names (`u_char`, `u_int`, …) from `src/compat/pc_win.h`; `long *` for `int *` a warning (Windows' `long` is 32 bits, as PsyQ's).
- **COFF assembly:** `tools/data2s.py --coff` and `pres_snap_syms.py --coff` (no `.type`/`.size`, COFF section flags, no GNU-stack note).
- **Relocatable links with GNU ld:** lld's MinGW driver can't do `-r`, which the presenter and the 60 Hz update are linked into; `binutils-mingw-w64`'s `ld` and `objcopy` do those, lld the executable.
- **Every file binary** (`_set_fmode(_O_BINARY)`): the common code opens the disc with `open`/`fopen` as on POSIX. `pread` is a positional `ReadFile` that puts the descriptor's offset back (Windows moves it even for a positional read; D242). Both come from Encore's `targets/pc/runtime/winposix`, vendored. `mkdir` has no mode; `etext`/`_end` (a save state's layout stamp) are marks in the image.

To try it on the Mac: `docker build` an image `FROM spyro-pc` with Debian's `wine` and run `wine build/pc-win/spyro.exe --disc 'Z:\work\disc\image'` under `tools/pc/xrun.sh` (Wine's GL is Mesa's llvmpipe there).

### macOS (Apple silicon; D250 O4, D265)

```bash
tools/pc/sdl3_macos.sh                      # once: SDL 3.4.18, static, in build/sdl3-macos
cmake -S . -B build/pc-macos -G Ninja -DSPYRO_HOST=pc -DCMAKE_TOOLCHAIN_FILE=cmake/pc-macos.toolchain.cmake \
    -DPython3_EXECUTABLE=/usr/bin/python3   # a Python with PyYAML (the system's has it)
cmake --build build/pc-macos                # -> build/pc-macos/Spyro.app
build/pc-macos/Spyro.app/Contents/MacOS/Spyro --disc disc/image
```

Native, on the Mac itself: Apple's clang (the Command Line Tools), macOS 13 or later, arm64 only. ARM64 has no `__ptr32`, so this build uses the offset model (`src/compat/p32.h`, D250): every P32 slot is a 32-bit offset from the executable's Mach-O header (`__mh_execute_header`), wherever the kernel slides it. `__PAGEZERO` covers the low 4 GB, so a pointer cut to 32 bits instead of encoded crashes, as the reservation does on Linux. `src/platform/common/p32_image.c` checks the image and stores the data image's offsets before the game starts. The window is SDL3's, with Encore's GL request (3.3 core, forward-compatible), which macOS answers with its 4.1 core context; the `#version 330` shaders run as they are. `Spyro.app` is signed ad hoc: Gatekeeper asks before the first start of a copy from elsewhere (right-click, Open).

What Mach-O needed beyond Linux (D265): the data image's and the presenter's assembly in Mach-O's dialect (`tools/data2s.py --macho`, `pres_snap_syms.py --macho`: underscored names, `segment,section` names); `objcopy`'s symbol edits on the presenter's and the 60 Hz rules' objects done by `tools/macho_syms.py` (private extern, then `ld -r`, which makes them local); ld64's `-u` for `--undefined`; `__asm__` labels through `SPYRO_ASM_LIT` (Mach-O's underscore); `etext`/`_end` marks in the image; the game thread's stack aligned to 16 KB pages.

## Encore

The game-free pieces of this build live in Encore (`ShayVD/encore`, PR #64: `targets/pc/`, `lp64/runtime/low_image`). The low image's two desktop variants and the Windows shims are vendored back here in `third_party/encore` (`tools/vendor.sh --dir third_party/encore`; `VENDORED` names the commit): never edit them here, change Encore and re-vendor. Since D247 `core/` and `targets/pc/encore/` are vendored too, and the platform layer runs on the interface (below); its picture path (frames filed by due vblank, the presenter's 16:9 and 60 fps frames) stays the game's.

### On Encore's platform interface (D247)

`src/platform/pc/` calls `enc_` for everything the interface has: `enc_platform_init` and the user folder's `save/` (`enc_path`), the window and the GL 3.3 core context (`enc_video_open` with `ENC_GFX_GL`, `enc_video_gpu`, `_begin`, `_present`; the GL loader is `EncGpu.get_proc`), events and pads (`enc_input_poll`, `enc_event_poll`, `enc_pad_read`), sound (`enc_audio_*`), time (`enc_time_us`, `enc_sleep_us`), mutexes, and the game thread on Windows (`enc_thread_start`). Encore's library (`encore_pc` in `src/platform/pc/CMakeLists.txt`) is built with plain flags, as Encore's own tests build it. The rest goes through `gaps.c` and `gaps.h`, each a gap in the interface with the smallest addition that would retire it:

| | What the PC layer needs | The shim in `gaps.c` (or elsewhere) | The smallest addition to `platform.h` / `enc_pc.h` |
|---|---|---|---|
| G1 | the user folder players already have (`spyro-vita/Spyro`, D240), not the backend's `encore/spyro` | `SDL_GetPrefPath`, handed to the backend as `ENC_PC_ROOT` (so `enc_path(ENC_DIR_SAVE)` is the old `save/`) | `EncPlatformConfig.org` (the vendor folder; NULL = `encore`) |
| G2 | the executable's folder (`disc/` beside it) | `SDL_GetBasePath` | an `ENC_DIR_APP` (the install folder, read only) |
| G3 | the first window size (`window=WxH`) | `ENC_PC_WINDOW`, set before init | `EncVideoConfig.window_w`, `window_h` (0 = the backend's) |
| G4 | the title, a 320×240 minimum, fullscreen at start, the mouse pointer hidden when idle or in fullscreen | `SDL_SetWindowTitle`, `SDL_SetWindowMinimumSize`, `SDL_SetWindowFullscreen`, `SDL_Show/HideCursor`, `SDL_GetMouseState` | the backend uses `EncPlatformConfig.title` (a fix, no API change); `EncVideoConfig.fullscreen`; the idle pointer as the PC backend's own behaviour |
| G5 | the video driver's name in the log (CI looks for `video: x11, llvmpipe`) | `SDL_GetCurrentVideoDriver` | `EncPlatformInfo.driver` (or `enc_pc_video_driver()` in `enc_pc.h`) |
| G6 | a second GL context, shared with the window's, current on the game thread (the GPU renderer, D243) | `SDL_GL_CreateContext` with `SDL_GL_SHARE_WITH_CURRENT_CONTEXT`, `SDL_GL_MakeCurrent` | `EncGpu.gl_create_shared()` and `gl_make_current(ctx)` (GL only) |
| G7 | spyro's keyboard map (WASD, K L J I, Space …), not the backend's fixed one on pad 0 (Z X A S, Q W …), which clashes (A, S, W, Q) | `SDL_GetKeyboardState`, and the backend's map's bits taken back out of pad 0 | `enc_pc_set_keymap(table, n)` in `enc_pc.h` (NULL: no keyboard on pad 0) |
| G8 | a message box (no disc, no display) | `SDL_ShowSimpleMessageBox` | `enc_message(title, text)` (blocking; `ENC_ERR_UNSUPPORTED` headless) |
| G9 | dropping the oldest queued sound after a stall | none: the newest is dropped instead until the queue is down to 80 ms (`audio.c`) | `enc_audio_drop(int keep_frames)` |
| G10 | GL's types and prototypes | `<SDL3/SDL_opengl.h>` (a header: no SDL call) in `video.c`, `pc_gl.h` | an `encore/gl.h` with the core profile's declarations |
| G11 | a swap at every display refresh, never paced to the source's 60 Hz (`frames.c` picks the frame due) | `EncVideoConfig.refresh_hz` set to the display's (`enc_platform_info()->refresh_hz`), so the backend never paces | `refresh_hz = 0`: the display's rate, never paced |
| G12 | whether the swap really waits for the display (the log's `vsync on`; no wait means pace yourself) | `vsync` off on a headless driver; with a real one, `SDL_GL_GetSwapInterval` after the open, and if the driver refused the interval (Mesa under Xvfb) the video is opened again without vsync, as before D247 (otherwise the backend paces every present to its own 60 Hz clock and a slow picture falls behind the frames due) | `EncGpu.vsync` (1 when the display paces the swap) |
| G13 | the game thread on a stack in the image (Linux, the low image) | `pthread_attr_setstack` (POSIX, not SDL; Windows uses `enc_thread_start`) | `EncThreadConfig.stack_mem` |
| G14 | one log: the backend writes its own `log/spyro.log` and stderr beside the game's `spyro.log` | none | `enc_log_set_sink(fn)` |
| G15 | the options menu's keys (Esc, F1) and setting fullscreen from the menu (D275) | `SDL_GetKeyboardState` for Esc and F1; `SDL_SetWindowFullscreen` and `SDL_GetWindowFlags` (the backend's F11 state isn't told, so F11 may take two presses after a menu change, as after `--fullscreen`) | `ENC_BTN_MENU` for F1 too; `enc_video_set_fullscreen` / `EncPlatformInfo.fullscreen` |
| G16 | VSYNC changed in the menu (D275) | `SDL_GL_SetSwapInterval` on the main context | `enc_video_set_vsync` |

Other differences from the SDL layer before D247: the backend lets gamepads work while the window is in the background and installs no SDL signal handlers (Ctrl+C ends the process at once); it opens up to 4 gamepads (8 before); the triggers count as L2/R2 from 125 of 255 (16,000 of 32,767 before: the same to within one step); it makes a hidden window at start to see that GL 3.3 works; the window's GL surface has the driver's default depth buffer (the picture draws without one).

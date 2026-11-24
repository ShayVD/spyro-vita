# PC build (Linux, Windows; D240, D241, D242)

**Status (2026-11-23):** Linux and Windows build and play (the US disc from boot to Artisans; Windows checked under Wine). CI builds both and starts each without a disc (Windows on a Windows runner). Next: the GPU renderer at the window's resolution, and a person with a real GPU and gamepad.

A native desktop build of the game: a window, keyboard and gamepads, sound, widescreen and 60 fps, from the player's own disc. It is the same code as every other build (the decomp, `src/game`, `src/platform/common`) with a small platform layer, `src/platform/pc/`, on **SDL3** (window, input, audio) and **OpenGL 3.3 core** (the picture).

## How it works

### 64-bit, with 4-byte pointers: the low image

Spyro was written for a 32-bit machine. Its structures hold 4-byte pointers, and the decomp keeps the PS1's layouts byte for byte, so the game's memory can't simply be rebuilt with 8-byte pointers. The PC build uses the low-image model that linux64 and the PS4 build use (`docs/ps4-plan.md`, D173, D174):

- every pointer field the game stores in its own memory is a 32-bit pointer (`P32`: clang's `__ptr32 __uptr`, `src/compat/p32.h`; the generated `patches/spyro-1-ptr32`). Only clang has `__ptr32`, and only for x86 targets: there is no ARM64 build (Apple Silicon Macs, ARM Linux) with this model.
- everything the game can point at lies below 2 GB, so those 32 bits hold the whole address: the executable sits at 0x400000 (non-PIE on Linux, a fixed base on Windows), the 2 MB of PS1 RAM is a `.bss` array in it (`arena.c`), and the game runs on a thread whose stack is in the image too (the hand C takes addresses of locals). `main()` checks all three and refuses to run otherwise.
- **Linux:** nothing of the host's may be below 4 GB, so a host pointer cut to 32 bits by mistake crashes instead of quietly aliasing the game's memory. Encore's `lp64_low_image_dyn.c` (vendored in `third_party/encore`, D242) reserves every free page from 64 KiB to 4 GB (`PROT_NONE`) from `.preinit_array`, which the dynamic loader runs before any shared library starts. glibc's malloc, SDL, the X11/Wayland and GL drivers and every thread stack then land above 4 GB. (The headless linux64 build is static and needs its own malloc; this one doesn't.) `LP64_LOW_TRAP=0` turns the reservation off.
- **Windows:** the executable has a fixed base of 0x400000 (no ASLR) and is not large-address-aware, so Windows keeps the whole process below 2 GB: every address fits 32 bits and nothing can be cut (`low.c`, Encore's `lp64_low_image_win.c` probes where memory lands). The game thread is an ordinary thread there (its stack is below 2 GB too; `main.c` checks). Wine doesn't apply that 2 GB limit to 64-bit programs; the game's own memory is low there anyway (the image and its `.bss`).

### Two threads

- The **main thread** owns SDL: the window, its GL context, the keyboard and gamepads. At every display refresh it shows the newest frame that is due.
- The **game thread** runs the game, the software rasterizer (linux32's `gpu_soft.c`, the PS1's exact pixels) and the SPU, paced by a 60 Hz clock of real time (`vblank.c`), the same catch-up model as the Vita and the PS4 (`common/clock.c`): if a tick runs late, the vblanks it missed run at once, so the game keeps its speed and the audio its continuity.

Frames go from one to the other with the vblank they are due at (`frames.c`), as the Vita's render thread shows them (stage 7c, D112): the presenter's in-between frame (60 fps) and its widescreen frame are filed as they are drawn, a vblank ahead; at a vblank with none (boot logos, loading screens) the PS1 display is filed as it is. The game thread never waits for the display, and the display can run at any rate (60, 75, 144 Hz, variable refresh): each refresh shows the frame due by then.

### Picture, input, sound

- **Picture** (`video.c`): one texture, one quad: 4:3, or 16:9 for the presenter's widescreen frames, as large as the window allows, black bars around. `filter=1` scales linearly, `filter=0` by nearest neighbour. F11 or Alt+Enter toggles (borderless) fullscreen; the window resizes freely. The frames are the PS1's resolution (512×240, the 224 lines the game draws shown): the GPU renderer at the window's resolution is the next step.
- **Input** (`input.c`, `pad.c`): SDL3's gamepad API (XInput, DualShock 4, DualSense, Switch Pro, …) as an analog DualShock: face buttons by position, triggers as L2/R2, the right stick the free camera (the Vita's defaults). Keyboard: arrows or WASD the d-pad, K/Space Cross, L Circle, J/Left Shift Square, I Triangle, Q/E L1/R1, 1/3 L2/R2, Enter START, Backspace/Tab SELECT.
- **Sound** (`audio.c`): the SPU's 44.1 kHz stereo into an SDL3 audio stream, which converts it for the device.

## Files

| what | where |
|---|---|
| the disc: a `.bin` + `.cue` image, `tools/extract_disc.py`'s files, or the Store copy's `EBOOT.PBP` | `--disc DIR`, else a `disc` folder beside the executable, else the user folder |
| saves (`save/`), `spyro.log`, `config.txt`, `shots/` | the user folder: `~/.local/share/spyro-vita/Spyro/` on Linux, `%APPDATA%\spyro-vita\Spyro\` on Windows |

`config.txt` takes `key=value` lines, and the command line takes the same keys as `--key value` (it wins): `fullscreen`, `window=WxH`, `widescreen`, `interp`, `vsync`, `filter`, `analog`, `stats`, and for tests `frames=N` (exit after N vblanks) and `shot=V1,V2` (a PNG of the window at those vblanks).

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

### macOS (not built)

No Mac build yet. An Apple Silicon (ARM64) build can't use the model: clang's `__ptr32` exists only for x86 targets (on AArch64 it is accepted and ignored: a "P32" pointer is 8 bytes). An x86-64 Mac build, run under Rosetta 2 on Apple Silicon, could: a non-PIE image at 0x400000 with a small `__PAGEZERO` (`-pagezero_size`, `-no_pie`; both deprecated by Apple's linker), the reservation below 4 GB as on Linux, and GL 4.1 core (which takes the `#version 330` shaders as they are). It needs the macOS SDK and a Mac to sign and test on; Rosetta's future (Apple has said it is for the transition) makes it a poor bet for a store release. The other way is the offset model of `lp64/docs/low-image.md` (32-bit offsets from a base, every target), a much larger change.

## Encore

The game-free pieces of this build live in Encore (`ShayVD/encore`, PR #64: `targets/pc/`, `lp64/runtime/low_image`). The low image's two desktop variants and the Windows shims are vendored back here in `third_party/encore` (`tools/vendor.sh --dir third_party/encore`; `VENDORED` names the commit): never edit them here, change Encore and re-vendor. Encore's PC backend of its platform interface (`targets/pc/encore/enc_pc.c`) is the same SDL3 and GL code in the interface's shape; this game keeps its own platform layer, whose picture path (frames filed by due vblank, the presenter's 16:9 and 60 fps frames) is the game's.

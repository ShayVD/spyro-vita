# PS4 build (plan P2 on; D180)

The PS4 fpkg is planned in `docs/ps4-plan.md`. This builds **the game** (P3–P4, D193, D245: `src/platform/ps4/`, a test fpkg with title ID `SPYT00002`), **the release fpkg** (P5, D246: title ID `SPYP00001`, below) and the **P2 spike** (D180), a test fpkg that reports where the PS4 puts what the low-image model (D173, D174) needs.

## The toolchain

[OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) v0.5.4 (2026-01-04, `toolchain-llvm-18.tar.gz`, SHA-256 `3c7cd5bb…4526`) and clang/lld 18. OpenOrbis ships no compiler.

| piece | version | licence | what for |
|---|---|---|---|
| OpenOrbis PS4 Toolchain | v0.5.4 | GPL-3.0 | headers, library stubs, `crt1.o`, `crtlib.o`, `link.x`, `sce_module/` PRXs |
| musl (OpenOrbis fork, in the tarball) | | MIT | libc |
| libc++ (OpenOrbis's llvm-project fork, in the tarball) | | Apache-2.0 with LLVM exception | C++ (the spike doesn't use it) |
| create-fself | v1.3 (`f2da622`) | GPL-3.0 | ELF → `eboot.bin` (fake SELF) |
| create-gp4 | v1.0 (`3f55f6a`) | GPL-3.0 | package project file |
| LibOrbisPkg PkgTool.Core | 0.2.231 | LGPL-3.0 | `param.sfo`, the fake PKG |
| clang, ld.lld | 18.1.8 (Mac), 18.1.3 (Docker, Ubuntu 24.04) | Apache-2.0 with LLVM exception | compile, link |

The owner approved OpenOrbis and both set-ups on 2026-11-20. The tools only build; what ends up *in* an fpkg from them is `crt1.o`/`crtlib.o`, musl and the stubs (and the PRXs and `right.sprx` in the package). Whether a released game fpkg linking OpenOrbis's GPL-3 CRT must itself be GPL-3 is for P5 (D180).

Two equivalent set-ups. Both build the spike the same way (`tools/ps4/spike/build.sh`).

### Native on the Mac

```bash
tools/ps4/sdk.sh
```

It downloads and checks (SHA-256) the OpenOrbis tarball and LLVM 18.1.8's official arm64 macOS release into `~/.local/share/spyro-ps4` (`SPYRO_PS4_SDK` to change), outside the repo, and prints the environment (`eval "$(tools/ps4/sdk.sh --env)"`). OpenOrbis's macOS tools are x86-64 and run under Rosetta 2. Homebrew's `llvm@18` has no bottle for macOS 14 and builds from source for hours; Apple's clang can't target the PS4.

### Docker (native arm64)

```bash
docker build --platform linux/arm64 -t spyro-ps4 -f tools/docker/ps4.Dockerfile tools/docker
tools/ps4/tc <command>
```

Ubuntu 24.04 arm64 with clang-18/lld-18, the same OpenOrbis tarball, `create-fself` and `create-gp4` built for arm64 from the pinned commits, and PkgTool.Core's framework-dependent build on Ubuntu's arm64 .NET 8 runtime. `--platform` matters: a cached amd64 `ubuntu:24.04` otherwise gets used under emulation. The image also carries `cmake`, `ninja` and `patch` for the game build (`cmake/prepare.cmake` applies `patches/` with `patch`). After the Dockerfile changes, rebuild the local image with the same command and tag (#264: an image from before the cmake and patch lines fails with `cmake: command not found`, or `patch failed`).

## The game (P3–P4, D193, D245)

```bash
eval "$(tools/ps4/sdk.sh --env)"
cmake -S . -B build/ps4 -G Ninja -DSPYRO_HOST=ps4 -DCMAKE_TOOLCHAIN_FILE=cmake/ps4.toolchain.cmake \
    -DPython3_EXECUTABLE=/usr/bin/python3        # a Python with PyYAML
cmake --build build/ps4    # → build/ps4/pkg/IV0000-SPYT00002_00-SPYROPS4GAME0000.pkg
```

On the Mac, or in the Docker image (CI's `ps4` job builds it there, D246). CMake builds the static libraries (the game, `src/platform/common`, `src/platform/ps4` with linux32's software rasterizer, PNG writer, log and save-state glue). `src/platform/ps4/CMakeLists.txt` then does three things:
- links `spyro.elf` with ld.lld and OpenOrbis's `link.x` (a PIE, as the spike);
- runs `oelf_check.py`;
- packs the fpkg with `tools/ps4/pkg.sh` (the spike's steps: the Piglet sample's PAID and auth info, the grey placeholder icon, no LiveArea or Vita art).

What the PS4 build does differently (all of it PS4 only; linux32, linux64 and the Vita don't change):
- **`-fPIE`, and `data2s --pie`:** a PIE can't hold a relocated address in 32 bits, so the data image's address words start as 0, and `main()` fills them from the `s1_pie_words` table.
- **`-march=btver2` and `tools/ps4/cc_unaligned.py`** (the C compiler's launcher): clang takes global arrays of 16 bytes or more as 16-byte aligned, and the decomp's sit at their PS1 addresses. Every C unit goes through assembly, with the aligned SSE moves rewritten to unaligned ones. Without this, starting a game crashed on hardware; qemu doesn't check it.
- **`-fno-vectorize`:** a fourth clang 18 `__ptr32` bug, a vector `addrspacecast` in `P32_INIT`'s loops under AVX.
- **The presenter's object:** `tools/ps4/keep_commons.py` keeps its common symbols global (llvm-objcopy would localize them into absolute symbols).
- **`main()` loads `libSceUserService`, `libSceVideoOut`, `libScePad`, `libSceAudioOut` and `libSceSystemService`** with `sceSysmoduleLoadModuleInternal`. The PS4 doesn't load them for the eboot.
- **`param.sfo` has CATEGORY `gde` and FORMAT `obs`**, as OpenOrbis's Piglet sample: with `gd`, Piglet's `eglGetDisplay` returned `EGL_NO_DISPLAY` with no error (GPU runs 1–2, D245).

On the PS4 (the owner's yes first, every time): the disc image (`.bin` + `.cue`), the files `tools/extract_disc.py` writes, or the Store copy's `EBOOT.PBP` go in `/data/spyro/`. Saves go to `/data/spyro/save/`, and the log to `/data/spyro/spyro.log`.

**Options** go in `/data/spyro/config.txt`, one `key=value` a line. The options menu writes its rows' keys there and keeps the other lines:
- the menu's (the Vita's keys and defaults): `widescreen`, `interp` (0, 1, 4), `wobble` (PSX WOBBLE, 0 the default: the fix on), `res` (0 native 1080p, 1 the PS1's pixels), `draw_distance` (percent: 100, the menu's NORMAL; 200, MAX (ADAPTIVE); 150 still read), `analog`, `free_camera`, `cam_invert_x`, `cam_invert_y`, `cam_speed`, `cam_recentre`, `touch_menu`, `skip_logos`, `state_slot`;
- developers': `frames=N`, `stats=0|1|2`, `realtime=0|1`, `shot=V`, `shot_every=N`, `interp_budget=0|1`, `gpu=0` (the software renderer), `gpu_vsync=1` (swap interval 1: blocked Piglet's first swap, D245), `shader_compile=1` (the player's Piglet pair compiles the shaders), `shader_dump=0`, `menu_test=V1,...`.

**The options menu:** hold the touch pad's click (the PS1's SELECT) and press OPTIONS, or tap the touch pad (`touch_menu=1`, the default). PICTURE: WIDESCREEN, 60 FPS, PSX WOBBLE, RESOLUTION, DRAW DISTANCE (NORMAL, MAX (ADAPTIVE); #260. The PS4's MAX is adaptive through `lod.c`'s limiter only: the buffer's limits and, with 60 Hz logic, the fixed 48 KB work budget. The Vita's budget timed from the frame, D212, isn't built for the PS4); CONTROLS; GAME (logos, cheats); SAVE (save states in `/data/spyro/save/`).

```bash
curl -T build/ps4/pkg/IV0000-SPYT00002_00-SPYROPS4GAME0000.pkg ftp://$PS4_IP:2121/data/pkg/
curl -o logs/…/spyro.log ftp://$PS4_IP:2121/data/spyro/spyro.log
```

Install the pkg with GoldHEN's Package Installer and run "Spyro PS4 (test)". To catch a crash, record GoldHEN's klog first (`nc $PS4_IP 3232 > logs/…/klog.txt`, then stop it by its PID). The crash report gives the faulting `rip` and backtrace at the eboot's base, 0x400000; `llvm-symbolizer --obj=build/ps4/spyro.elf ADDR-0x400000` (or `nm -n`) names the function. A watchdog thread logs where the game thread is if it stops for 3 s.

### The release fpkg (P5, D246)

```bash
eval "$(tools/ps4/sdk.sh --env)"
tools/ps4/release.sh                 # → build/release-ps4/<APP_VER>-<commit>/spyro-ps4-<APP_VER>.pkg
tools/ps4/release.sh --upload v1.3.0 # the same, then attached to that tag's DRAFT release
```

Built on the owner's Mac, never by CI: the shader binaries (`disc/ps4-shaders/`) are never committed (D245). CI's `ps4` job builds the test fpkg without them on every pull request, as a check, and keeps it only as a CI artifact. `release.sh`:
- refuses uncommitted changes, configures `build/ps4-release` with `-DSPYRO_PS4_RELEASE=ON` and builds it **twice from clean**: every SHA-256 (the ELF, `eboot.bin`, `param.sfo`, the pkg) must match. `SOURCE_DATE_EPOCH` is the commit's time, so `__DATE__`/`__TIME__` (`main.c`'s build stamp, the save states' header) don't differ: without it the two builds' ELFs did;
- with `SPYRO_PS4_RELEASE`, the build stops unless every shader has a binary for its current GLSL (`tools/ps4/shader_check.py` preprocesses `gpu_gles.c`'s shader block, hashes each source as `src_hash()` does and looks for `NAME-vs|fs-HASH.bin`), so a stale binary can't ship and leave players on the software renderer;
- `param.sfo`: TITLE `Spyro PS4`, TITLE_ID `SPYP00001`, CONTENT_ID `IV0000-SPYP00001_00-SPYROPS4RELEASE0`, APP_VER and VERSION from `cmake/version.cmake` (the Vita's too: tag vX.Y.Z is `0X.YZ`), CATEGORY `gde`, FORMAT `obs` (Piglet needs them, D245);
- `sce_sys/icon0.png` (512×512) from the Vita's LiveArea icon (the owner's decision, D246) by `tools/ps4/art.py`, plain Python, the same bytes every run; `disc/livearea/` (RGB) when present, else `assets/livearea/`. **No `pic1.png`:** release run 1 had the Vita's background as `pic1.png`, and it stayed on screen over the game's Piglet picture the whole run while the game ran (60 vblanks/s, 0 GL errors), so the owner had it removed;
- `licenses/` in the package: `LICENSE-MIT.txt`, `THIRD_PARTY_NOTICES.md`, `GPL-3.0.txt`, `CC0-1.0.txt`, `ART.md`. The fpkg is GPL-3.0 as a whole (OpenOrbis's `crt1.o`; D246);
- archives `build/release-ps4/<APP_VER>-<commit>/`: the pkg, its `.sha256`, `SHA256SUMS`, the unstripped `spyro.elf` and `spyro.map` (for crash reports, never attached) and `manifest.txt` (commit, versions, toolchain, the shaders by name and hash, `runtime_validation`). An existing folder is refused;
- `--upload vX.Y.Z`: HEAD must be that tag and its release a draft (CI's tag run makes it); it attaches the pkg and `.sha256` and never publishes.

The release and test fpkgs have different title IDs, so both can be installed; they share `/data/spyro/`. A release goes the Vita's way (encore-playbooks `vita-release`): a version-bump PR changing `cmake/version.cmake`, an annotated tag, CI's draft release with the VPKs, `release.sh --upload` for the fpkg, the owner installs that exact file and plays it, then publishes.

### The renderers (P4, D245)

**GLES2 on Piglet** (`gpu_gles.c`), the default: the PS1 picture drawn by the GPU at 1080p, 16:9, 60 fps (60 Hz logic in play, D150). The software rasterizer still draws every split into the CPU's VRAM (on 4 threads, `raster_mt.c`; `tests/ps4_raster` checks it against `gpu_soft.c`), so VRAM reads stay exact with no readback; the GPU redraws the same splits into 1080p targets and samples VRAM from a texture uploaded by rows. Presenter frames are kept on the GPU. Swap interval 0, as the Piglet sample (with 1 the first `eglSwapBuffers` never returned); the game's own 60 Hz clock paces it.

**The shaders (the owner's option C):** Piglet ships in every PS4's firmware, but its GLSL compiler (`libSceShaccVSH.sprx`) doesn't, and there's none we may use offline. So the game embeds **pre-compiled shader binaries**, made once on a PS4 that has the compiler, and loads them with `glShaderBinary` on the firmware's own Piglet: players need only the pkg and their disc image.
- **Capturing them:** a PS4 with the Piglet pair (`libScePigletv2VSH.sprx`, `libSceShaccVSH.sprx`) in `/data/self/system/common/lib/` ([sm64-ps4-osirizx](https://github.com/mindfreakdev/sm64-ps4-osirizx) says where they come from; this repo never fetches or ships them), `shader_compile=1` in `config.txt`: the game compiles its GLSL and saves each shader (`shader_dump=1`) to `/data/spyro/shaders/NAME-vs|fs-HASH.bin`, the hash being the source's FNV-1a. Copy them into `disc/ps4-shaders/` (gitignored; `-DSPYRO_PS4_SHADERS=` for another folder) and build: `tools/ps4/shaders_c.py` makes them a C table.
- **The binaries are not committed** (the owner's decision): they go only into the release fpkg. A build without them needs the Piglet pair on the PS4.
- **At start:** with binaries for every shader, the firmware's Piglet (`/<sandbox word>/common/lib/libScePigletv2VSH.sprx`); else, or with `shader_compile=1`, the player's pair. A shader whose source changed has no binary (its hash differs): the build logs it and, the binaries failing, writes `/data/spyro/shader_compile`, so the next start compiles with the pair.

**The software renderer** (`gpu=0`, or Piglet not starting; a notification says why): linux32's rasterizer scaled into a 1920×1080 `sceVideoOut` framebuffer on the CPU (`video.c`), 4:3 or 16:9.

Hardware runs (2026-11-21, `logs/p3/hw-run{1,2,3}/`):

| run | commit | result |
|---|---|---|
| 1 | 9f6312b | crash at the first call into `libSceUserService` (not loaded) |
| 2 | d58b0cb | title screen, controller works, ~7.5 pictures/s, no audio; starting a game crashed (`movaps` on an 8-aligned array) |
| 3 | 8c6673b | the game starts and runs: 60 vblanks/s, 0 late, audio; per VSync the game ~5 ms, scaling 4.3 ms |

GPU runs (P4, 2026-11-21/22, `logs/p4/gpu-run{1..11}/`, each with its `spyro.elf` and `commit.txt`):

| run | commit | result |
|---|---|---|
| 1 | a3ed0c4 | `eglGetDisplay` refused (software fallback); crash in the pause screen (P32 labels in the presenter's snapshot, fixed in ac7a6ee) |
| 2 | ac7a6ee | still refused; software path ~33 fps with 60 fps on |
| 3 | 148be51 | `param.sfo` `gde`/`obs`: Piglet up, shaders compiled and dumped; hung at the first picture |
| 4–5 | 9c4ac7b, bbf17ae | the watchdog: stuck in the first `eglSwapBuffers` (a second `sceVideoOut` handle wasn't the cause) |
| 6 | 5457cc4 | swap interval 0: **the GPU picture**, 60.0 vblanks/s, 0 late, no GL errors |
| 7 | 8f357a9 | the PICTURE rows (perspective, sub-pixel, resolution, draw distance) live; the new shader dumped |
| 8 | a457303 | **the firmware's Piglet with the embedded binaries**, no pair needed (up in 324 ms); closing the menu crashed (a drop list without NULL) |
| 9 | e99b16a | menu fixed, PSX WOBBLE row; the movies' 24-bit frames 0.8 ms (58 ms before); 3 late vblanks in the run |
| 10 | 65b6dc6 | after merging `main`: every polygon broken (main's #178 made `VCacheVert` floats; the PS4 still read `int16`) |
| 11 | 3358e30 | the float layout read (guarded by a `_Static_assert` since b00630e): the picture right, 11,400 vblanks, 15 late, no GL errors; menu saved; memory card written |

Release runs (P5, 2026-11-24, `logs/p5/hw-run{1,2}/`):

| run | commit | result |
|---|---|---|
| 1 | 91dcaf4 | `release.sh`'s archived pkg: installs, icon shown; the `pic1.png` background stayed over the game all run (the game ran behind it: Piglet with the embedded binaries, 59.9 vblanks/s, 0 GL errors) |
| 2 | 0c082af | no background: the game shows and plays; 16,200 vblanks, 14 late, 0 GL errors, no crash; draw distance 2× at x2.00 in play |

## The spike

```bash
tools/ps4/spike/build.sh                  # on the Mac → build/ps4-spike/
tools/ps4/tc tools/ps4/spike/build.sh     # in Docker → build/ps4-spike/
```

Output: `build/ps4-spike/IV0000-SPYT00001_00-SPYROP2SPIKE0000.pkg` (title ID `SPYT00001`, throwaway), and `SHA256SUMS` for `spike.elf`, `eboot.bin`, `param.sfo` and the pkg. It holds no game data and no art (`icon0.png` is a plain grey square from `icon.py`).

- Compiled for `x86_64-pc-freebsd12-elf` with `-fPIC -fms-extensions`, linked `-pie` with OpenOrbis's `link.x` as its samples are (the image is linked at 0; the PS4's loader chooses where it goes, which is what the spike reports). `__bss_start` and `_end` come from `--defsym` (lld doesn't define them under `link.x`, and weak references become imports that `create-fself` rejects).
- **Keep `.bss` alignment small.** `create-fself` sizes the eboot's read-write segment as the `.data*` sections plus `.bss`'s size, as if `.bss` began right after them. A 16 KB-aligned array opened a gap, the PS4 mapped `.bss` short, and its dynamic linker refused the eboot before `main` (CE-34878-0; D180). `build.sh` runs `tools/ps4/oelf_check.py` on the ELF after linking. The check does `create-fself`'s sum and fails the build if `.bss` would be cut short. The game's link will need it too.
- `create-fself` gets the **Piglet sample's** PAID (`0x3800000000000035`) and auth info, read from the SDK's `samples/piglet/Makefile`: the game will need Piglet, so the spike runs with the same rights.
- **Reproducible:** two builds on the Mac give the same SHA-256 for every file, and so do two in Docker. `create-fself` writes its `-in`/`--eboot` paths into the eboot, so `build.sh` passes relative ones; `create-gp4` stamps the build time into `pkg.gp4`, so `build.sh` puts the commit's time there. The Mac and Docker builds use different clang patch releases (18.1.8 and 18.1.3), and their code differs (not only `.comment`); each reproduces itself.

What it does on the PS4: system notifications on screen (start, the image/arena/stack addresses, a summary, the verdict), and the full report in `/data/spyro-spike/spike.log` (and stdout, GoldHEN's klog): `__text_start`, `.data`, `__bss_start`, `_end`, a 2 MB `.bss` arena, a thread on a 1 MB `.bss` stack through `scePthreadAttrSetstack` (and one through `pthread_attr_setstack`), a `__ptr32` field round trip, `malloc`/`mmap`, `sceKernelMapFlexibleMemory` and `sceKernelMapDirectMemory` at 0x80000000 (without and with `MAP_FIXED`, and flexible at 0x40000000), the module list and the whole `sceKernelVirtualQuery` map. The verdict is **LOW-IMAGE MODEL HOLDS** only if the image, the arena and the stack (and a local on it) all lie below 0x80000000. It then idles: close it with the PS button.

### On the PS4 (the owner's yes first, every time)

With GoldHEN's FTP server on:

```bash
PS4_IP=192.168.x.y tools/ps4/spike/ftp.sh upload     # → /data/pkg/
```

Install it with GoldHEN's Package Installer, run "Spyro PS4 spike", then:

```bash
PS4_IP=192.168.x.y tools/ps4/spike/ftp.sh log        # ← logs/p2/hw/spike.log
```

If it crashes (CE-34878-0), the crash reporter deletes the core dump. The reason is in the kernel log: with GoldHEN's klog server on, record it before the run with `nc $PS4_IP 3232 > logs/…/klog.txt` (stop it by its PID afterwards) and look for `rtld` and `# A user thread receives a fatal signal`.

### Results (2026-11-21, D180)

On the owner's PS4 Slim (second generation), firmware 13.04, run 3 (commit 5da3c11, `logs/p2/hw-run3/`):

| what | address |
|---|---|
| image (`__text_start` .. `_end`) | 0x400000 .. 0x819130 |
| 2 MB `.bss` arena | 0x4182c0 |
| `scePthreadAttrSetstack` `.bss` stack (1 MB) | 0x618300; its thread's local at 0x7182bc |
| `malloc`, `mmap` | 0x2_0000_0520, 0x2_0040_8000 |
| main thread's stack | 0x7_eedf_c000 .. 0x7_eeff_c000 |
| system libraries | 0x8_0000_0000 and up |
| `sceKernelMapFlexibleMemory(0x80000000)`, hint and `MAP_FIXED` | 0x80000000 (also 0x40000000) |
| `sceKernelMapDirectMemory(0x80000000)`, hint and `MAP_FIXED` | 0x80000000 |

Verdict: **the low-image model holds**. Runs 1 and 2 crashed on the `.bss` sizing above.

## `__ptr32` and LLVM 18

`tools/ps4/clang_bugs/` (D174) through clang 18.1.8 for `x86_64-pc-freebsd12-elf`, `-fms-extensions -O1`: all three bugs are there (logs in `logs/p2/clang_bugs/`, gitignored). A call through a `__ptr32` function pointer loads 8 bytes (`jmpq *g_fn(%rip)`) or crashes the backend; a `?:` mixing a `__ptr32` and a normal pointer crashes the backend (clang 19 hung); a `__ptr32` static initializer holding an address crashes the backend. P1's workarounds (`src/compat/p32.h`: the cast, `P32_INIT`, no `__ptr32` function pointers) compile correctly with it.

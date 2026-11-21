# PS4 build (plan P2 on; D180)

The PS4 fpkg is planned in `docs/ps4-plan.md`. This builds **the game** (P3, D193: `src/platform/ps4/`, a test fpkg with title ID `SPYT00002`) and the **P2 spike** (D180), a test fpkg that reports where the PS4 puts what the low-image model (D173, D174) needs.

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

Ubuntu 24.04 arm64 with clang-18/lld-18, the same OpenOrbis tarball, `create-fself` and `create-gp4` built for arm64 from the pinned commits, and PkgTool.Core's framework-dependent build on Ubuntu's arm64 .NET 8 runtime. `--platform` matters: a cached amd64 `ubuntu:24.04` otherwise gets used under emulation.

## The game (P3, D193)

```bash
eval "$(tools/ps4/sdk.sh --env)"
cmake -S . -B build/ps4 -G Ninja -DSPYRO_HOST=ps4 -DCMAKE_TOOLCHAIN_FILE=cmake/ps4.toolchain.cmake \
    -DPython3_EXECUTABLE=/usr/bin/python3        # a Python with PyYAML
cmake --build build/ps4    # → build/ps4/pkg/IV0000-SPYT00002_00-SPYROPS4GAME0000.pkg
```

Only on the Mac so far; the Docker image has the same tools. CMake builds the static libraries (the game, `src/platform/common`, `src/platform/ps4` with linux32's software rasterizer, PNG writer, log and save-state glue). `src/platform/ps4/CMakeLists.txt` then does three things:
- links `spyro.elf` with ld.lld and OpenOrbis's `link.x` (a PIE, as the spike);
- runs `oelf_check.py`;
- packs the fpkg with `tools/ps4/pkg.sh` (the spike's steps: the Piglet sample's PAID and auth info, the grey placeholder icon, no LiveArea or Vita art).

What the PS4 build does differently (all of it PS4 only; linux32, linux64 and the Vita don't change):
- **`-fPIE`, and `data2s --pie`:** a PIE can't hold a relocated address in 32 bits, so the data image's address words start as 0, and `main()` fills them from the `s1_pie_words` table.
- **`-march=btver2` and `tools/ps4/cc_unaligned.py`** (the C compiler's launcher): clang takes global arrays of 16 bytes or more as 16-byte aligned, and the decomp's sit at their PS1 addresses. Every C unit goes through assembly, with the aligned SSE moves rewritten to unaligned ones. Without this, starting a game crashed on hardware; qemu doesn't check it.
- **`-fno-vectorize`:** a fourth clang 18 `__ptr32` bug, a vector `addrspacecast` in `P32_INIT`'s loops under AVX.
- **The presenter's object:** `tools/ps4/keep_commons.py` keeps its common symbols global (llvm-objcopy would localize them into absolute symbols).
- **`main()` loads `libSceUserService`, `libSceVideoOut`, `libScePad` and `libSceAudioOut`** with `sceSysmoduleLoadModuleInternal`. The PS4 doesn't load them for the eboot.

On the PS4 (the owner's yes first, every time): the disc image (`.bin` + `.cue`), the files `tools/extract_disc.py` writes, or the Store copy's `EBOOT.PBP` go in `/data/spyro/`. Saves go to `/data/spyro/save/`, and the log to `/data/spyro/spyro.log`. Options go in `/data/spyro/config.txt`, one `key=value` a line: `frames=N`, `stats=0|1|2`, `realtime=0|1`, `analog=0|1`, `shot=V`.

```bash
curl -T build/ps4/pkg/IV0000-SPYT00002_00-SPYROPS4GAME0000.pkg ftp://$PS4_IP:2121/data/pkg/
curl -o logs/…/spyro.log ftp://$PS4_IP:2121/data/spyro/spyro.log
```

Install the pkg with GoldHEN's Package Installer and run "Spyro PS4 (test)". To catch a crash, record GoldHEN's klog first (`nc $PS4_IP 3232 > logs/…/klog.txt`, then stop it by its PID). The crash report gives the faulting `rip` and backtrace at the eboot's base, 0x400000; `llvm-symbolizer --obj=build/ps4/spyro.elf ADDR-0x400000` names the function.

Drawing is linux32's software rasterizer, scaled 4:3 into a 1920×1080 `sceVideoOut` framebuffer on the CPU (`video.c`). The GLES2 renderer on Piglet (P4) needs the player's own `libScePigletv2VSH.sprx` and `libSceShaccVSH.sprx` in `/data/self/system/common/lib/` (D193).

Hardware runs (2026-11-21, `logs/p3/hw-run{1,2,3}/`):

| run | commit | result |
|---|---|---|
| 1 | 9f6312b | crash at the first call into `libSceUserService` (not loaded) |
| 2 | d58b0cb | title screen, controller works, ~7.5 pictures/s, no audio; starting a game crashed (`movaps` on an 8-aligned array) |
| 3 | 8c6673b | the game starts and runs: 60 vblanks/s, 0 late, audio; per VSync the game ~5 ms, scaling 4.3 ms |

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

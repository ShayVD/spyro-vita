# PS4 (and PS5) fpkg: plan (D173)

Goal: the same port as a fake PKG for jailbroken PS4s, and through a PS4's backwards compatibility on jailbroken PS5s. It must play the whole game, as the Vita build does, at 1080p 16:9 and 60 fps (interpolated), from the player's own disc image or PlayStation Store copy. It comes after the Vita's M5. What we learn here goes back into `vita-port-kit`, which may become `playstation-port-kit` (the owner's idea, 2026-11-20).

## 1. The problem: the PS4 is 64-bit

The Vita and the host debug build are 32-bit, and the port relies on it:
- the disc's data (`WAD.WAD`) is loaded into the arena and its 32-bit pointer fields are fixed up in place (`PATCH_POINTER`, `loaders.c`), so the decomp's structs must keep their PS1 layout;
- the hand C reads fields at raw offsets and holds pointers in `u32` variables, and checks the layouts with `_Static_assert`s;
- `src/game/region_data.c:15` asserts `sizeof(void *) == 4`.

The native memory build (`SPYRO_NATIVE_MEM`, D100/D106, the Vita's default) already removed every demand on *where* the arena is (bit 31, 16 MB alignment). What's left is the *size* of a pointer.

## 2. Measured (2026-11-20, `tools/ps4/trial64.py`)

The linux32 native-mem build's compile commands replayed with clang for `x86_64-unknown-linux-gnu` (LP64, the PS4's model), 186 units (the main game, the hand C, `platform/common`, each overlay source once with every class; the overlays' `gen/` files are `gcc -m32 -E` output and can't be measured as they are). Logs: `logs/run-ps4trial/` (gitignored).

| | plain LP64 | pointer fields `__ptr32 __uptr` |
|---|---|---|
| units that fail | 22 | 17 (12 files) |
| layout `_Static_assert`s failed | 52 | 8 (all `hand_structs.h`'s `CameraView` mirror, `CreditsLine`, `region_data.c`) |
| other errors | 16 | ~30: `Moby **` locals walking `Moby *__ptr32[]` arrays (`draw.c`, `42CC4_hand.c`), 14 `int = void *` in `loaders.c`'s fix-ups |
| pointer→int casts (`-Wpointer-to-int-cast`) | 176 | 171 |
| int→pointer casts | 95 | 95 |

The `__ptr32` column needed no hand edits: a script marked all 231 pointer fields in the decomp's 15 headers (`trial64.py --ptr32`). The casts are the port's `(u32)(uintptr_t)p` and the decomp's `(int)p`: harmless when every address the game sees is below 4 GB, wrong otherwise.

One compiler bug showed up: Apple clang 16 crashes in its x86 backend (`Cannot select: i32 = bitcast`) on a `?:` that mixes a `__ptr32` and a normal `void *` (`src/game/present.c:625`). An explicit cast avoids it.

For comparison, the offset model below has to change every use of the 114 pointer field names: a crude grep finds ~14,500 `->m_X`/`.m_X` uses in the decomp's C and the hand C (it counts some same-named non-pointer fields too).

## 3. What others do (research, 2026-11-20; sources in D173)

- **PS4 homebrew SDK:** OpenOrbis PS4 Toolchain v0.5.4 (2026-01, LLVM 18, musl, libc++, Piglet GLES2 headers, an SDL2 port, `create-fself`, `create-gp4`, LibOrbisPkg's PkgTool). Recent ports (2Ship2Harkinian-PS4, ioQuake3-PS4, Recomp-to-PS4-FPKG, all 2026) build with clang for `x86_64-pc-freebsd12-elf`, then `create-fself` → `create-gp4` → PkgTool. `create-fself`'s defaults give a process that can't map system flexible memory; those ports set the auth-info fields.
- **Graphics:** Piglet (OpenGL ES 2 over EGL) in every working port. Retail firmware has no runtime shader compiler: ioQuake3-PS4 ships offline-compiled shader binaries (`glShaderBinary`); 2Ship needs the player to supply two Piglet modules (45 s boot). No open homebrew uses GNM directly.
- **Input and audio:** `scePad` and `sceAudioOut` directly (48 kHz s16 stereo) in the serious ports; SDL2 (OpenOrbis's SDL-PS4) also covers both.
- **Memory:** the executable loads at 0x400000 and its globals sit below 2 GB (2Ship's `TECHNICAL.md`); ordinary mappings land at 8 GB and above. Nobody has shown a fixed mapping below 4 GB on a retail PS4. (P2 measured it: the spike mapped flexible memory at 0x40000000 and 0x80000000 with `MAP_FIXED`, D180.)
- **`__ptr32` off Windows:** works with `-fms-extensions` on x86-64 ELF. Calls through a `__ptr32` *function* pointer crashed the backend until LLVM PR #209078 (2026-11-03), which OpenOrbis's LLVM 18 lacks; the decomp's structs have no function-pointer fields. AArch64 miscompiles narrow stores, so the checks run x86-64 code (under qemu), never arm64. The Tenkaichi3 decomp uses `__ptr32` with PS2 RAM mapped low on x86-64, and base+offset on arm64.
- **Offsets:** N64Recomp and psxrecomp keep guest pointers as 32-bit values and index one RAM block; 2Ship and Ship of Harkinian made their decomps 64-bit clean by hand, and 2Ship still hit pointers stored in `u32` on PS4.
- **No PS4 build of any Spyro decomp** (OpenPete is PC only).
- **PS5:** PS4 fpkgs install through etaHEN with kstuff; native PS5 GPU homebrew is immature. Firmware ranges vary by source.

## 4. Decision: the low-image model (D173)

**Everything the game can point at lives in the executable's image, below 4 GB, and the decomp's pointer fields are 32 bits wide.**

- **`P32`**, one macro: `__ptr32 __uptr` when building for x86-64 with clang (`-fms-extensions`), empty everywhere else. Every pointer field of the decomp's structs and of the port's mirrors (`hand_structs.h`, `CreditsLine`, ...) carries it. The decomp's headers get it through `patches/spyro-1-port`, generated by a script from the trial's rewrite, then reviewed. linux32 and the Vita compile the same text with `P32` empty, so their binaries don't change (checked by SHA-1).
- **The arena is a 2 MB `.bss` array** (`SPYRO_LOW_IMAGE`), not a heap block or a fixed mapping. No mapping at a chosen address is needed, which avoids the one thing nobody has shown on a PS4.
- **The game thread's stack is in `.bss` too** (`pthread_attr_setstack`): the hand C takes the addresses of locals and some reach `u32`s.
- **Host memory the game is handed** (CD buffers, the presenter's copies, any `malloc` in `src/game`) is audited: it either moves into the image or never reaches a `P32` field or a `u32`.
- **Link non-PIE at 0x400000**, the PS4's own base. (P2: OpenOrbis's PIE, linked at 0, is loaded there anyway, D180.) Check at startup that the image's end, the arena and the stack are below 4 GB, and refuse to run otherwise.
- **Any clang works**, not only OpenOrbis's LLVM 18: the toolchain's sysroot and libraries can be used with a newer clang (≥ the 2026-11-03 fix) if LLVM 18 shows `__ptr32` bugs we can't work around.

**Why not the offset model** (every pointer field a `u32` offset from one base, decoded at each use): it's what the recompilers do and runs on any LP64 host, but here it rewrites thousands of field uses in the decomp's C and the hand C, all as patches. That's a huge, risky diff on code that's verified byte for byte today. The low-image model is ~231 annotated fields plus ~40 local fixes, and it changes nothing on the 32-bit targets.

**The fallback:** if the PS4 doesn't load our image below 4 GB, or `__ptr32` miscompiles in ways we can't contain, we switch to offsets. P1's inventory of pointer fields is the same list the offset model needs, so it isn't lost.

**How a missed pointer is caught:** linux64 runs with glibc's `M_MMAP_THRESHOLD` 0 (every `malloc` an `mmap`, so above 4 GB) and the main stack high, so a host pointer that reaches a 32-bit field is truncated to an unmapped address and crashes, rather than aliasing low memory quietly.

## 5. Milestones

Each one is done only as its checks say. Every run on a PS4 or PS5 needs the owner's explicit yes first, as on the Vita.

- **P0 (this, done):** measure, research, decide (D173).
- **P1: linux64 host build (done, D174).** An x86-64 Linux host build with clang, run under `qemu-x86_64` in the host container, as linux32 runs under `qemu-i386` (new packages in `tools/docker/host.Dockerfile`: clang, the amd64 libc cross, `qemu-user`'s x86-64). `P32` everywhere, the arena and game stack in `.bss`, the trial's errors fixed, the region tables 64-bit clean. **Done when** `tools/host_checks.sh` on linux64 and linux32 (native-mem) gives byte-identical traces, shots, audio and memory cards over the whole-game tour and routes A and B, and linux32's and the Vita's binaries are unchanged.
- **P2: PS4 toolchain and a hardware spike (done, D180).** OpenOrbis on the Mac (or its Docker set-up on Linux), a test fpkg that prints where its image, `.bss`, a `.bss` array and a thread's `.bss` stack are, and tries `sceKernelMapFlexibleMemory` at 0x80000000 for the record. Needs the owner's PS4 (model, firmware, HEN) and a yes. **Done when** the addresses are logged from hardware. **Done (D180):** OpenOrbis v0.5.4 with clang 18 on the Mac and in Docker (`docs/ps4-build.md`). On the owner's PS4 Slim (firmware 13.04) the image loads at 0x400000..0x819130, the `.bss` arena at 0x4182c0 and a thread's `.bss` stack at 0x618300: the low-image model holds. `sceKernelMapFlexibleMemory` at 0x80000000 works too, even fixed. Gotcha for P3: keep `.bss` alignment small (`tools/ps4/oelf_check.py`).
- **P3: PS4 platform layer.** `src/platform/ps4/`: Piglet GLES2 for the renderer (`gpu_gl.c`'s vitaGL calls to plain GLES2 + EGL; shaders compiled offline if a compiler we may use exists, else the player's own Piglet modules, as 2Ship does), `scePad` and `sceAudioOut`, the disc image or Store copy from `/data/spyro/`, saves next to it. Boots to the title in an emulator first if one is good enough (shadPS4 runs homebrew fpkgs?), then on hardware. **Done (D193):** the game's fpkg (`SPYT00002`, `docs/ps4-build.md`) starts a game and runs on the owner's PS4 at 60 vblanks a second, with the controller and audio. It draws with linux32's software rasterizer, scaled into a `sceVideoOut` framebuffer. No shader compiler we may use offline exists, so the owner chose the player's own Piglet modules for the GLES2 renderer, with the software path as its fallback; the GLES2 port moves to P4. No emulator runs Piglet homebrew (shadPS4 needs macOS 26 here), so testing went straight to hardware. Gotchas: load the system modules by hand; the eboot is a PIE (`data2s --pie`); the decomp's arrays are misaligned for x86-64's rules (`tools/ps4/cc_unaligned.py`); a fourth `__ptr32` bug (`-fno-vectorize`).
- **P4: whole game on hardware**, at 60 fps interpolated, 1080p 16:9, the options menu working with the DualShock 4. The GLES2 renderer on Piglet (`gpu_gl.c`'s vitaGL calls to GLES2 + EGL), its GLSL compiled at boot by the player's own `libScePigletv2VSH.sprx` and `libSceShaccVSH.sprx` in `/data/self/system/common/lib/`, the software rasterizer when they're missing (D193). **So far (D245, `docs/ps4-build.md`):** the GLES2 renderer runs on the owner's PS4 at 1080p, 16:9, 60 fps (60 Hz logic), drawing what the software rasterizer keeps in VRAM. The owner chose pre-compiled shaders (option C): captured once with the Piglet pair, embedded in the build, loaded by the firmware's own Piglet, so players need only the pkg and their disc image; the binaries aren't committed and ship only in the release fpkg. The options menu has every Vita row that fits a DualShock 4 (PSX WOBBLE, resolution, draw distance among them), opened by a touch pad tap. Gotchas: `param.sfo` `gde`/`obs` for `eglGetDisplay`; swap interval 0; P32 for the presenter's snapshot labels. Saves work on hardware (GPU run 11). Still to do for P4: the whole game played to the end on hardware (both endings, the credits); the Store copy's `EBOOT.PBP` skipped for now.
- **P5: fpkg release (done, D246).** Title ID, `param.sfo`, icon and background (the LiveArea images' licence covers the Vita release only; ask), README with dump instructions, the release workflow builds it. A PS5 test through etaHEN. **Done (D246):** title ID `SPYP00001` ("Spyro PS4"), APP_VER from `cmake/version.cmake` (shared with the Vita); the owner chose the Vita's LiveArea images for the icon and background, a local `tools/ps4/release.sh` (two clean builds compared, shader binaries checked against their sources, an archive with a manifest, upload to a draft release only) with CI building the test fpkg without binaries as a check, and the fpkg offered under GPL-3.0 as a whole. README "On a PS4". [[hardware]] The PS5 test is left for later (the owner's word).
- **P6: back into the kit.** What P1–P5 teach goes into `vita-port-kit`, maybe renamed `playstation-port-kit`. The owner decides the rename.

## 6. Open questions for the owner

- The PS4 and PS5 to test on: model, firmware, HEN (GoldHEN version; etaHEN on the PS5). The PS4 is a second-generation Slim on firmware 13.04 (2026-11-21); GoldHEN version not given.
- ~~Whether a released fpkg that links OpenOrbis's GPL-3 CRT is offered under GPL-3 (P5, D180).~~ Yes, GPL-3.0 as a whole (D246).
- ~~A title ID for the fpkg, and which icon and background art we may use.~~ `SPYP00001`; the Vita's LiveArea images (D246).
- The PS5 for the etaHEN test: the owner has one; model, firmware and etaHEN version not given yet (P5 left it for later).
- ~~Whether P1's Docker packages (clang, the amd64 libc cross, `qemu-x86_64`) are fine to add.~~ Approved 2026-11-20 (D174).

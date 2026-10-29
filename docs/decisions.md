# Decisions

Record each decision with its date, what was chosen, and why.

## 2026-10-29 — M0 decisions

### D1. Translate the decomp's assembly *symbolically*, function by function, into C linked against the decomp's own C
- **Chosen:** our own MIPS→C translator in `tools/mips2c/` that reads the decomp's splat `.s` files (not the binary) and emits one C function per assembly function. Hand-written or hand-cleaned C replaces the generated C where it matters (see `docs/porting-plan.md` §7).
- **Why:** the `.s` files carry symbolic `%hi(g_Camera + 0xD0)` / `%lo(...)` relocations and named jump tables, so generated code can address the real host-linked globals. That means no emulated 2 MB RAM image, no BIOS and no recompiler runtime. psxrecomp's model (whole binary, emulated hardware, run-time overlay compile, interpreter fallback) needs JIT or an interpreter for overlays, and we want neither on the Vita.
- **Rejected:** porting psxrecomp. Its licence is PolyForm Noncommercial (`../psxrecomp-ref/LICENSE`), and its architecture doesn't fit a static Vita build. It stays reference only.

### D2. GTE: one register-level C model, used by both generated code and PsyCross's libgte
- **Chosen:** translated `mtc2/mfc2/ctc2/cfc2/lwc2/swc2` and GTE commands call a bit-exact C GTE model (`src/game/gte/`). The commands we need are RTPS, RTPT, NCLIP, DPCS, INTPL, MVMVA, SQR, OP, GPF, GPL and CC (inventory in `docs/porting-plan.md` §2.3).
- **Why:** the renderers, collision and maths code use the GTE as a fixed-point maths unit, not just for projection. Their results feed gameplay (collision.s, math.s), so they must be exact, or traces drift. A single projection choke point (RTPS/RTPT) is also where an optional widescreen field of view and sub-pixel precision go later.
- **Open:** whether PsyCross's `PsyX_GTE.cpp` is exact enough to *be* that model, or whether we write our own and point PsyCross at it. Decide in M1 with a GTE unit test against PCSX-Redux-captured vectors.

### D3. Overlays: link every overlay variant statically, rename per variant, dispatch through a table
- **Chosen:** the same pattern as `~/Projects/kh1-decomp/docs/native-overlays.md`: compile each overlay variant with a forced-include rename header (`ovl_<name>_` prefix), generate a module table (name, the WAD entry it corresponds to, entry points), and switch the "current overlay" when the game finishes its overlay read instead of jumping into loaded bytes.
- **Why:** no JIT and no code loading on the Vita, and the same addresses are different code in each overlay. kh1-decomp already proved the approach.

### D4. Build system: CMake + Ninja, `SPYRO_HOST=linux32|vita` switch, platform-free core
- **Chosen:** top-level `CMakeLists.txt` modelled on `~/Projects/kh-bbs-vita/CMakeLists.txt` (cmake 3.20, C11, a `HOST` cache variable, `add_subdirectory(src/platform/${SPYRO_HOST})`, generated C under `${CMAKE_BINARY_DIR}/gen`, never in git). The game core (decomp C + translated C + PsyCross) has no OS, window or GPU headers. Only `src/platform/<host>` touches SDL, vitaGL or VitaSDK.
- **Vita configure:** `cmake -S . -B build/vita -G Ninja -DSPYRO_HOST=vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake`.
- **Why:** this is the pattern the brief points to. Note that kh-bbs-vita only has the *plan* for its Vita CMake (`hosts/vita/` is empty, no `vglInit` anywhere in the three reference projects), so the Vita half is new work, not a copy.

### D5. Host debug build in Docker, kh-bbs-vita `tools/tc` style
- **Chosen:** `tools/tc` runs one command in a `--platform linux/amd64` Debian container (based on `kh-bbs-vita/tools/docker/toolchain.Dockerfile` plus `gcc-multilib` and `libsdl2-dev:i386`). Host flags come from kh1-decomp's `tools/host/census.py`: `-m32 -std=gnu99 -O1 -g -fno-strict-aliasing -fno-pic -fno-stack-protector -funsigned-char -ffp-contract=off -no-pie` (unsigned char: see D6), with optional `--asan`.
- **Why:** the Mac is arm64 and can't run 32-bit code natively: no `-m32` on arm64 macOS, and Apple CPUs have no AArch32. The 32-bit x86 Linux build is the quickest way to get gdb, ASan and 4-byte pointers.
- **Risk found in M0:** running a whole `linux/amd64` image under Docker Desktop's emulation crashed `python3` at random under `-j`. So the **matching build** now runs natively as `linux/arm64`, with only the i386 `cc1` emulated (`tools/build_matching.sh`). The host `-m32` build can't avoid amd64 emulation. If it's flaky too, turn on Docker Desktop's "Use Rosetta for x86/amd64 emulation" (an open question for the user), or use a Linux x86 machine or VM.
- **Note:** Docker Desktop does not share `/private/tmp`. Mount from inside the repo (`build/` is gitignored).

### D5b. Matching build: patched copy, never the submodule
- `patches/spyro-1/0001-moby_update-unprototyped-MobyRotateToAngle-call.patch` works around upstream `d2ac00b` not compiling with GCC 2.7.2 (a prototype added in `0d228ab` vs the original 4-argument call). It's byte-neutral, as the SHA-1 and LEVEL_33.ovl checks confirm. Drop it once upstream fixes this.

### D6. Compiler flags that guard behaviour (all targets)
- `-fsigned-char` is **wrong** for this game. The decomp compiles with `-funsigned-char` (`Makefile` `C_CLASSIC_FLAGS`), unlike kh1 (EE: signed). Use `-funsigned-char` on the host and the Vita too.
- `-ffp-contract=off`, no fast-math, FPSCR flush-to-zero off (kh-bbs-vita `docs/bringup.md`). Spyro is integer code, but PsyCross and our renderer aren't.
- `-fno-strict-aliasing` (the game code type-puns heavily, e.g. `((u_long *)p)[1] = colour` in `titlescreen.c`).

### D7. Vita packaging and runtime defaults (from the Duke and kh1 ports)
- Packaging: `vita-elf-create` → `vita-make-fself -s -c` → `vita-mksfoex -d ATTRIBUTE2=12` → `vita-pack-vpk`, as in `duke-nukem-advance-tc-vita/platform/PSVita/build.sh`, wrapped as CMake's `vita_create_self` / `vita_create_vpk`. Link with `-Wl,-q`, and don't strip.
- Link list: start from Duke's `Common.mak` L1028–1034 (vitaGL, vitashark, SceShaccCgExt, mathneon, plus the stubs) with `-lSDL2`.
- Clocks 444/222/222/166 at start-up. Heap via `_newlib_heap_size_user`. **Keep the main stack small** (kh1: a 52 MiB stack failed on hardware, C1-2681-7, while Vita3K accepted it).
- Logging: write through `sceIoWrite` with `sceIoRemove` first, and redirect printf to it (kh1 `host/vita/main.c`). newlib stdio logs stay empty in Vita3K. Run Vita3K at log level ERROR.
- LiveArea: 8-bit indexed PNGs (Duke `make_livearea.py`). **Title "Spyro Vita", title ID `SPYV00001`** (decided 2026-10-29; four letters plus five digits, and not in a Sony-assigned prefix).
- `libshacccg.suprx` is user-supplied and never committed, if vitaGL's run-time shader compiler is used.

### D8. Verification oracle runs out of process: PCSX-Redux
- **Chosen (2026-10-29):** PCSX-Redux (GPL) runs the original game as a separate process, driven by a Lua script inside the emulator (breakpoint callbacks, RAM, register and GTE reads, dumps to `logs/oracle/`), with its GDB server as a fallback. DuckStation (GPL-3.0) is kept as an accuracy cross-check. Neither is ever linked or vendored, the same as kh-bbs-vita's PPSSPP oracle (`tools/oracle/trace_diff.py`).
- **Why PCSX-Redux:** the user left the choice to "whichever is easier". In-emulator Lua means no separate GDB-protocol driver for the first differential tests.
- **BIOS:** `disc/bios/SCPH1001.BIN` (SHA-1 `10155d8d6e6e832d6ea66db9bc098321fb5e8ebf`, NTSC-U v2.2). It's gitignored through `disc/` and `SCPH*.BIN`, and moved there from `~/Downloads` at the user's request.
- **Why out of process:** licensing, and so the shipped binary contains only our code, CC0 decomp code and MIT PsyCross.

### D9. Data ownership
- The decomp's `asm/data/*.s` turn out to be almost free of raw pointers: of the ~3,250 `.word`s in the game's own (non-PsyQ) data files, the only pointers are 24 words in `math.data.s`, and splat already emitted those symbolically (`.word D_800752C8`), so they relocate at link time. The `0x80xxxxxx` values in `titlescreen_data.s` and `math.data.s` are packed sprite or colour words, not addresses. So we don't need kh1's 10k-row `data_relocs.tsv` machinery for the executable's data. A small `abscheck` still guards it. WAD-loaded data is a separate question (see `docs/porting-plan.md` §4).

### D10. Don't edit submodules; build oracles from copies
- The matching build runs on an rsync copy at `build/s1-matching/` (gitignored), so `third_party/spyro-1` stays clean.

## 2026-10-29: answers to the M0 open questions (from the user)

### D11. Scope and targets
- A decomp-based native port **counts for the bounty**. There are no extra rules.
- **Frame rate: aim high, 60 fps if possible.** Logic stays at the original 30 Hz (`g_DeltaTime` 2–4), and presentation interpolates to 60 (porting-plan §5). This raises the priority of readable C for pete, camera and the renderers in M2–M4.
- **Crash Bandicoot demo** (`S0/CRASH.EXE` via `LoadExec`): hide the title-menu entry. Don't emulate or chain-load it.

### D12. Tools and licensing
- **m2c (GPL-3.0) is allowed** as an offline drafting tool in `tools/`. It's never linked, and its output is reviewed and cleaned up by hand before it lands in `src/game/`.
- **Audio:** our own SDL2 software SPU and XA mixer replaces PsyCross's OpenAL `libspu`, and our own file-based `libcd` replaces PsyCross's. This deliberately diverges from "audio through PsyCross" (CLAUDE.md updated).
- **Arena:** a 16 MB-aligned allocation for the 2 MB PS1 RAM arena is accepted, despite the possible heap waste on the Vita.

### D13. Environment
- Docker Desktop already has **Rosetta for x86/amd64 emulation** turned on. The M0 Python segfaults happened under Rosetta, so amd64 emulation is still considered flaky. The matching build runs natively as arm64 (D5). For the `-m32` host build, retry or drop parallelism if steps crash, and fall back to an x86 Linux VM if it stays unreliable.
- **The upstream build break isn't reported upstream** (user's choice). It's recorded in `docs/upstream-notes.md`, with our patch in `patches/spyro-1/`.

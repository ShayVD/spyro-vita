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
- **Installed (2026-10-30, M2, with the user's approval for the official macOS build only):** PCSX-Redux dev channel build 413, version `a5d621af` (changeset `a5d621af59cd1453b31bc18d3812670b742eb712`), "MacOS Arm", from the project's official distribution (`https://distrib.app/pub/org/pcsx-redux/project/dev-macos-arm`, linked from the PCSX-Redux README; the `distrib.app.pcsx-redux.consoledev.net` host doesn't resolve). File `PCSX-Redux-a5d621af-Arm.dmg`, 50,155,556 bytes, SHA-256 `367e23ba9fa7e05513b6599bc181281be44ef0e4e148026ddb09ea0d17804efc`, SHA-1 `b442d0eb3bf049d471aea3d54680613e7ccc428a` (matches the publisher's manifest). The `.app` is copied out of the image into `tools/oracle/bin/`, which is gitignored. Driven by `tools/oracle/run.sh` (`-no-ui -testmode -interpreter -debugger`, portable settings in `tools/oracle/bin/home`).
- **Pacing:** `tools/oracle/run.sh` runs at real time (paced to audio output), about a minute per 3,600 vblanks: setting `settings.spu.UseNullSync` from Lua after start stalls it at the BIOS. M1's runner (D24) sets `UseNullSync` and `Speed 8` in `pcsx.json` before start and gets about 40 vblanks/s with breakpoints, 250/s without; adopting that in `run.sh` is a follow-up.
- **Pending loads at breakpoints (from D24):** Redux commits `mtc2`/`ctc2`/`lwc2` and loads one instruction late, so at an entry breakpoint a load in the caller's delay slot may still be pending. `capture.lua` doesn't replay those (M1's `oracle.lua` does); none of the 948 replayed samples failed from it.

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

## 2026-10-29: M1 decisions

### D14. Host build: native arm64 container, i686 cross compiler, game under qemu-i386
- **Chosen:** `tools/docker/host.Dockerfile` is a native arm64 Debian image with `gcc-i686-linux-gnu`, cmake, ninja, python3, `gdb-multiarch` and `qemu-user`. `tools/tc` runs commands in it. Only the i386 game binary is emulated (binfmt or `qemu-i386`, `QEMU_LD_PREFIX=/usr/i686-linux-gnu`); gdb attaches through `qemu-i386 -g` (`tools/gdb_run.sh`).
- **Why:** the `linux/amd64` image segfaulted python3 under Docker Desktop's emulation again (during `apt`'s post-install), as in M0 (D5, D13). This is the same split the matching build uses. Speed: 1800 vblanks (30 s of game) take 5 s.
- **Replaces** D5's "amd64 image" for the host build.

### D15. Decomp data: host assembly, not C arrays
- **Chosen:** `tools/data2s.py` turns `asm/data/*.s` into host GNU as (`.word`→`.long`, `.align N`→`.balign 2^N`) with every label kept. Only `D_80074B84` (PsyQ's sqrt table, read by `collision.s` and `42CC4.s`) is taken from the PsyQ data. Words in PS1 RAM range must be allowlisted (abscheck); words holding scratchpad addresses go to a table (`s1_scratch_relocs`) that start-up rebases.
- **Why:** this gives the same bytes, layout and symbols with no typing work. It was checked against `psx.map` (513 label spacings, 0 differences) and works with ARM gas too.

### D16. mips2c v0 design
- One C function per `.s` file (unit). Every glabel is an entry: `NAME(a0..a7)` for C callers (args 5–8 go to the emulated stack) and `NAME__m2c(a0..a3)` for translated callers. Local `j`/`jal`/branches are gotos, so code that falls through or jumps across glabels and `jr $t4` local subroutines keep working. `jr reg` goes through one switch over PS1 code addresses: labels, jump-table entries, local return points, and every instruction after an address-taken label.
- Registers are locals. `$sp` is the emulated stack (`g_mips_sp`), top at PS1 0x80020000 in the arena. `$v1` crosses calls in `g_mips_v1`. C callees get 8 arguments (the 4 registers plus 4 stack words).
- `%hi`/`%lo` use the host symbol's address. `lui 0x1F80` is the scratchpad: the first KB of the arena (PS1 0x80000000, 64 KB aligned). GTE ops call PsyCross's GTE (D2).
- **The KUSEG RAM mirror matters:** the game dereferences 0x000xxxxx addresses, through cleared bit-31 model tags, packed 21-bit pointers (also in compiled code) and speculative delay-slot loads through null. Every translated load and store maps 0–2 MB onto the arena (`MIPS_KUSEG`). Hand-written `sll 11; srl 10/11` pairs are also rebased on the arena.
- MIPS I load delays are honoured (none found in practice).
- **Later:** drop the per-access KUSEG check where analysis proves the base is a host pointer (speed on the Vita), and replace `0x80000000` tag ORs with `g_psx_arena_base` for a Vita arena that isn't at 0x80000000 (M3).

### D17. Deterministic virtual time on the host
- `VSync(0)` is exactly one vblank. `VSync(-1)` doesn't advance. Per vblank: pad script, `psx_cd_vblank`, `psx_spu_vblank`, the game's VSync callback, then screenshots.
- CD reads complete inside `CdRead` by default (`--cd-latency N` delays them), because `CDLoadSync` (`cd.c:105`) busy-waits on `m_IsReading` without VSync; on the PS1 the CD interrupt ends that loop.
- Runs are reproducible: two runs of the same build give byte-identical screenshots.

### D18. Headless host renderer behind PsyCross's GR_* layer
- The linux32 host doesn't use PsyCross's SDL/GL platform files (`SPYRO_PSYCROSS_PLATFORM=OFF`, patch 0002 `PSYX_NO_SDL`). It has its own: virtual vblanks, a pad script, PNG screenshots of the displayed VRAM rect, and a software rasterizer that implements `GR_*` into a CPU 1024×512 VRAM. PsyCross's OT walker and primitive parser (the code the Vita uses) are exercised unchanged apart from our patches.
- **Why:** there's no display in Docker on macOS, and GL under qemu would be slow and fragile. The Vita gets a vitaGL `GR_*` backend in M3.

### D19. Memory cards: a directory per card
- `src/platform/common/libmcrd.c` replaces PsyCross's `LIBMCRD.C`, which crashed on a missing file. `<save>/card0` is port A (always present, created on first use) and `card1` is port B (present if its directory exists). Each card file is a host file of blocks × 8 KB. Async calls complete at the next `MemCardSync`. Host: `--save DIR`. Vita: `ux0:data/spyro/save/` (M3).

### D20. Overlay Terminators are arena pointers; no rename header
- Supersedes D3's rename header: overlay symbols are already unique, as porting-plan §4.2 found. `Terminator_<ovl>` (end of overlay code, used as the copy buffer) becomes a `void *` set at start-up to its matching-build address in the arena (`gen/ovl_table.c`), and so does `g_OverlaySpacePointer` (`main_BSS_END`). The overlay bytes are still read into the overlay space as data.

### D21. Decomp compile mode for the port
- `MODERN_COMPILER` is defined. Its branches only replace type puns with field copies and fix an implicit return. The one behavioural change is `MobyRotateToAngle`'s 5th argument (`upstream-notes.md`), still to check with the oracle.
- `-fcommon`, because the decomp's uninitialised C globals are also defined in `asm/data` (GCC 2.7.2 merged them as common symbols).
- **D2 status:** M1 uses PsyCross's GTE (`MTC2`/`CTC2`/`GTE_operator`) for translated code too. Settled on 2026-10-30: `tests/gte` matches PCSX-Redux bit for bit on 9,314 vectors over the 11 ops the main executable uses, so we keep it and won't write our own (D24).

## 2026-10-29: M2 decisions

### D22. A human plays the host build in a browser (`--serve PORT`)
- **Chosen:** the linux32 binary itself runs a small HTTP server (`src/platform/linux32/serve.c`, plain sockets and pthreads). With `--serve PORT`, vblanks are paced to real time (60 Hz) and `http://localhost:PORT/` shows the picture on a canvas (raw RGB per frame via long-polled `/frame`), sends keyboard or Gamepad-API buttons to `/pad`, and plays the SPU mix through WebAudio from `/audio`. `TC_PORT=PORT tools/tc ...` publishes the port on the Mac's 127.0.0.1 only.
- **Why:** it needs nothing installed on the Mac or in the image, works with any browser and pad, and the game still runs headless under qemu-i386 (D14, D18). At 6× real time under qemu, there's headroom for pacing.
- **Rejected:** X11 via XQuartz (a new install on the Mac, and GL or SDL in the container under emulation), VNC (a server in the image, and macOS Screen Sharing insists on VNC passwords, so DES too), a native Mac viewer for a frame stream (a second program to maintain).
- **Evidence stays scripted:** milestone evidence is still pad scripts plus screenshots in `logs/runN-*`, which are deterministic; served runs are not.

### D23. Host build: the game's data at its PS1 addresses (PS1 layout)
- **Found:** near world geometry was missing everywhere (Artisans ground drawn as the clear colour, the title intro's canyon as "sky and sand"). `r_environment.s` keeps a pointer into `D_8006FCF4` (= `g_SonyImage`, a scratch buffer) and uses **its sign bit as a mode flag**: one path clears bit 31 with `sll 1; srl 1`, and the per-polygon loop tests `bgtz $s6` to choose between screen coordinates with clip flags packed in (`<< 5`) and raw ones. On the PS1 the buffer is at `0x8006FCF4` (negative); as a host global at `0x08xxxxxx` it was always positive, so flagged coordinates were read as raw.
- **Chosen:** `tools/data2s.py --map psx.map` makes every label from the decomp's `asm/data` an absolute symbol at its `psx.map` address, i.e. inside the RAM arena (mapped at `0x80000000` on linux32), and start-up copies the initial bytes there (`s1_psx_image`, `src/game/port.c`). CMake option `SPYRO_PSX_LAYOUT` (default ON). Pointers to game data now have the PS1's values: bit 31 set, and the cleared-bit-31 KUSEG alias maps back through `MIPS_KUSEG` exactly as on the hardware.
- **Why all of it, not one buffer:** any other sign or bit trick on a data pointer would fail the same way, silently. It also gives the oracle (§7.3) RAM that compares with the emulator's byte for byte for all of `asm/data`, and snapshots load with a copy.
- **Not covered:** globals defined with initialisers in the decomp's own C files (e.g. `CheatTable`) stay ordinary host globals. PsyQ's own data is PsyCross's.
- **Vita (M3):** absolute symbols need the arena at `0x80000000`, which a Vita executable can't ask for. Vita user memory is at `0x81000000+`, so pointers there are negative too, but the `sll 1; srl 1` alias (`0x01xxxxxx`) must then map back to the arena (extend `MIPS_KUSEG`), and data symbols must resolve to the arena (a generated `#define` header for C plus mips2c emitting arena addresses). Decide in M3.
- **Evidence:** `logs/run3-replay/exp2/shot-6569.png` (Artisans: waterfall, gem platform, textured ground) against `logs/run3-step1/glide.png` (before); `logs/run4-layout/contact.png` (title intro canyon at 1080–1200).


### D24. Behaviour oracle (M1 runner): PCSX-Redux a5d621af, native macOS, interpreter + debugger
- *Numbering:* written as D22 on the M1 branch, at the same time as M2's D22/D23; renumbered when M2 merged. Both runners use the same build: M1's `tools/oracle/redux` + `oracle.lua` (in `.tools/`, function snapshots, `tests/gte`, `tests/m2c_diff`) and M2's `tools/oracle/run.sh` + `capture.lua` (in `tools/oracle/bin/`, `spyro --replay`, demo traces; D8).
- **Build:** the pinned dev build `a5d621af` (2026-10-30) from distrib.app, the download location PCSX-Redux's README points to (the old `distrib.app.pcsx-redux.consoledev.net` no longer resolves). `tools/oracle/fetch_redux.sh` checks the published SHA-1 and unpacks it into the gitignored `.tools/`. It runs out of process only and is never linked (GPL).
- **Native macOS arm64, not Docker.** The Linux arm64 AppImage needs glibc 2.38 plus X11 libraries. Its dynarec crashed during the BIOS boot, and under the interpreter it seemed to hang in the game's CD retry. That hang turned out to be our own Lua bug (below), but the native build is faster and needs no image, so it stays the runner.
- **Flags:** `-no-ui -debugger -interpreter -softgpu`. Breakpoints only fire with the debugger on. The interpreter keeps them exact. The cost is about 40 vblanks/s with breakpoints and about 250/s without.
- **`pcsx.json` in the run directory:** `UseNullSync` (the SPU paces the CPU from the audio callback otherwise), `Mute`, `Speed 8`. The file also makes Redux portable, so settings and memory cards stay in the run directory. Emulated timing is cycle-counted and doesn't depend on host speed.
- **Lua gotchas:**
  - Event listeners and breakpoints held only by chunk locals are garbage-collected after the first large allocation. The vsync listener silently stopped, which looked like an emulator hang. Keep them in globals.
  - `PCSX.GPU.takeScreenShot()` throws under `-no-ui` on 24-bit frames. Dump VRAM with `getVRAM()` instead.
  - Redux commits `mtc2`/`ctc2`/`lwc2` and GPR loads after the next instruction. So at a breakpoint, the move or load in the slot just before it is still pending. `oracle.lua` replays those into its "before" snapshots.
- **Vblank numbering** counts from reset, so it includes the BIOS boot (~900 vblanks before the game's main loop). It is not aligned with the host's count yet.

## 2026-10-30: M3 decisions (first Vita build)

### D25. Vita: the arena is a 16 MB-aligned block from the heap; data labels through pointers; three aliases of PS1 RAM
- **Arena:** `memalign(16 MB, 2 MB)` from newlib's heap (`psx_arena_init`'s non-Linux branch, 128 MiB `_newlib_heap_size_user`). memalign hands the unused lead-in back to the heap, so nothing is wasted for good (D12 had accepted the waste anyway). A Vita executable can't ask for 0x80000000 itself; its user memory starts at 0x81000000, so the arena lands at some 0x8Y000000 with Y ≥ 1. 16 MB alignment keeps what the PS1 code relies on: the low 24 bits of an arena pointer are its PS1 offset, so OT links (PsyCross patch 0001: `base | link`) and 21-bit packed pointers (mips2c's `sll/srl` rebasing) still work unchanged.
- **Data labels (D23 on the Vita):** `tools/vita/arena_syms.py` reads `gen/s1_data.S` and writes a header, force-included into the game's C (decomp, overlays, mips2c output, `src/game`), with `#define NAME (*NAME__p)` for each of the 516 labels. `extern T NAME;` then declares `T *NAME__p`, and every use reads through a pointer that start-up (`src/platform/vita/main.c`) sets to `arena + (PS1 address - 0x80000000)`. It also writes `s1_data_vita.S`: the same data plus the pointers and a list of the 48 data words whose value is a label's PS1 address (24 in `math.data.s`, 24 in `s1_scratch_relocs`). Start-up moves those by `arena - 0x80000000` before `port.c` copies the images. An expression other than `LABEL ± constant` stops the generator, so nothing is missed silently.
- **Aliases** (`mips2c_rt.h`, `#ifdef __vita__` only; linux32 is unchanged): translated loads and stores map three PS1-RAM aliases onto the arena: `0x00000000 | offset` (KUSEG, as before), `0x80000000 | offset` (the game's tag ORs onto offsets; on linux32 this was the arena itself), and the arena's own address with bit 31 cleared (`sll 1; srl 1`, e.g. `0x01xxxxxx`). There's no Vita user memory at any of these. Cost: one more compare per translated access than linux32.
- **Why this and not alternatives:** absolute symbols need a link-time address; making the arena an ELF section gives a loader-chosen, not 16 MB-aligned, address; and rewriting every 0x80000000 constant in mips2c can't tell pointer tags from flag bits.
- **OT tags XORed with pointers:** `r_cyclorama.s`, `r_shadows.s` and `r_particles.s` build a packet's tag as `(len << 24 | 0x80000000) ^ next_packet`, cancelling bit 31 against the pointer's top byte 0x80. With the arena at 0x89000000 the length came out 9 too big (the first Vita3K run, `logs/run1-vita-title`: 352k PsyCross "bad GP0 command" errors from the title intro's sky on, with the picture still right). The mips2c hint `arena_tags` (per unit) emits those units' 0x80000000–0x8F000000 constants as `MIPS_ARENA_TAG(k)` = `k ^ 0x80000000 ^ (arena & 0xFF000000)`, which is `k` on linux32. GP0 commands like 0xE1000000 in the same units are left alone. A scan of the asm for `lui 0x8N00` / `%hi(0x8N000000)` found no other units.
- **Residual risk:** native C that dereferences a `0x80000000 | x` pointer, or compares one with a real pointer, would fail on the Vita only. A search of the decomp C found `| 0x80000000` only on flag words (`m_ControlFlags`, `unk_0xC0`, `m_ShadowDistance`), and `& 0x7FFFFFFF` pointers (`loaders.c` SimpleModel tags) would already crash natively on linux32, which runs. Range checks like `p < 0x80200000` would also differ; none found so far.

### D26. First Vita picture: the linux32 software rasterizer, shown through SDL2's GXM renderer
- **Chosen:** for the first hardware test, the Vita compiles linux32's `gpu_soft.c` (the `GR_*` rasterizer into a CPU 1024×512 VRAM) and `png.c` (the displayed rect, 15- or 24-bit), and shows the rect each vblank through an SDL2 streaming texture, scaled 4:3 into 960×544 by SDL2's `VITA gxm` renderer, with `PRESENTVSYNC` pacing the vblank.
- **Why:** vitaGL's fixed-function path compiles its shaders at run time (`ffp.o` calls `shark_compile_shader_extended`), so anything on vitaGL needs `ur0:data/libshacccg.suprx` on the device; SDL2's GXM renderer uses precompiled shaders and needs nothing. A PS1-accurate GPU backend (CLUT lookup in a shader, the semi-transparency modes, VRAM readback for `StoreImage`) is also a larger job than the first test should wait on. And the picture is the same one the linux32 host, and so the oracle comparisons, already show.
- **Next:** a vitaGL `GR_*` backend with batched draws (needs libshacccg, or shaders compiled once and cached), selectable at run time, with the software path kept as a fallback, as kh1's `gpu=` does. Move `gpu_soft.c` and the display reader to `src/platform/common` once M2 has landed (they're compiled from `src/platform/linux32` for now, so M2's fixes reach the Vita).
- **Host Python:** CMake's code generators need PyYAML; on macOS `/usr/bin/python3` has it (`-DPython3_EXECUTABLE=/usr/bin/python3`). Nothing was installed.

### D27. Vita L2/R2: the right stick (and, opt-in, the rear touch pad)
- L2/R2 turn Spyro's camera (`camera.c` `func_80035F58`: held R2/L2 set `D_80075924` to ∓1024) and appear in the cheat codes (`cheats.c`). A handheld Vita has neither button.
- **Chosen:** the right stick's X axis is L2 (left) / R2 (right), past a deadzone of 16000/32767. With `reartouch=1` in `config.txt`, a finger on the rear touch pad's left or right half is L2 or R2 too. It's off by default, because hands rest on the rear pad. A PS TV's DualShock L2/R2/L3/R3 work as themselves.
- **Why:** the right stick is where camera control sits in later games, needs no grip change, and leaves every face and shoulder button as on the PS1.

### D28. Vita pacing: the game's vblanks follow the display's
- **Found on hardware** (user's first test, build `a60dd0b`, 2026-10-30): 14–20 vblanks per second in 3D scenes, 19–24 on the title, 56 on the boot logos. VSync(0) ran one vblank and presented with a blocking vsync each time, so a late frame also lost a display frame per vblank. The game, its music and its timing all ran at a quarter to a third of speed, with audio gaps.
- **Chosen:** by default (`realtime=1`), VSync(0) waits for the next real vblank (`sceDisplayGetVcount`/`sceDisplayWaitVblankStart`) when the game is early. When it's late, it runs every vblank missed (pad, CD, SPU, the game's VSync callback; at most 8, the rest dropped after loads), as the PS1's interrupt would have. Then it presents once. The game's own delta time keeps movement at speed, and the SPU mix stays real-time (a 7200-vblank Vita3K run made exactly 120 s of audio, `logs/run12-vita-realtime`).
- `realtime=0` keeps the linux32 host's virtual time (D17) for scripted runs that are compared with the host byte for byte; `tools/vita/vita3k_run.sh` uses it unless `REALTIME=1`.
- **Not a speed fix by itself:** the game still draws only as many pictures as the CPU allows (D29). The log's stats line now says how many new pictures each 300 vblanks had and how many vblanks were late.

### D29. Vita speed: measured costs, cheap fixes, and the GPU renderer starting in M3
- **Measured:** the hardware log (D28) means about 120 ms of CPU per 30 Hz game frame against a 33 ms budget. Per-part timing (`prof.c`, "time" lines every 300 vblanks: game, ot, raster, spu, present, wait) in Vita3K puts the software rasterizer at about 60% of busy time, game logic about 25%, the OT walk about 6%, present about 5%, SPU about 3%. Vita3K's absolute times are about 11× faster than the Vita and noisy under load on the Mac, so only the shares are used. Hardware numbers for each part come with the next test build.
- **Cheap fixes, all checked byte-identical to the host** (shots, cards and WAV, `logs/run9`–`run11`):
  - PsyCross (its GTE and OT walk) was built with no `-O` on every host; the Vita now builds it at `-O2`. linux32 keeps `-O0`: its x87 maths could change with optimisation, and its pictures are the reference.
  - `mips2c_kuseg` is `always_inline` (GCC had stopped inlining it in the large units and was calling it per access) and reads the arena base through a const alias, so it isn't reloaded after every store.
  - `gpu_soft.c` divides gradients with 32-bit divisions where exact (`div16`, 52 M cases checked against the 64-bit form): the A9 has no hardware divide.
  - The present skips the VRAM conversion and upload when nothing was drawn and the display didn't change.
  - Clocks were already at the maximum (444/222/222/166).
- **Decision:** the CPU fixes can't close a 4× gap, so the GPU renderer starts in M3, not M4. The first attempt is PsyCross's own GL renderer (`src/render/PsyX_render.cpp`, VRAM as a texture with CLUT in a shader) on vitaGL, whose GLSL translator is in the installed library (`glsl_utils.o`). It needs `ur0:data/libshacccg.suprx` on the device. The software path stays as the fallback (`gpu=0`, and automatically when shaders can't be built). What was built instead of `PsyX_render.cpp` as it is, and why: D35.

## 2026-10-30: presentation (widescreen, 60 fps)

### D30. Widescreen and 60 fps as presentation passes over the unchanged game
- **Chosen:** both features run the game's own `GamestateDraw` again in a *presentation pass*:
  1. snapshot the 2 MB arena, scratchpad, GTE registers and PsyCross's envs;
  2. adjust the state (projection X row at 3/4; camera, Spyro and mobys halfway between ticks);
  3. render with VSync frozen and SPU writes dropped;
  4. capture the picture;
  5. restore.

  The original `GamestateDraw` still runs once per tick as the logic pass. Under widescreen its `DrawOTag` is skipped, so its side effects keep the 4:3 frustum. Off is the default, and then nothing runs: the demos match the oracle and the screenshots are byte-identical. Design, costs and logs: `docs/presentation.md`.
- **Widescreen goes in the camera's projection matrix, not RTPS/RTPT** (revises D2's plan). `CameraUpdateMatrices` already scales the Y row by 320/512 for the 4:3 TV. Scaling the X row by 3/4 there squeezes the picture *and* widens the frustum tests that `r_moby` does in that space. A GTE-level squeeze would leave those culls at 4:3, and it would reach gameplay: the camera logic projects Spyro through RTPS (`func_80017AA4`). HUD models (`func_80022A2C`) and C-built 2D primitives (the HUD OT) are narrowed separately.
- **Why a logic pass instead of one widescreen pass:** `r_moby` sets `m_WasDrawn` from its frustum test, and `func_80051FEC` uses that flag to pick which mobys update. With the wider frustum in the logic (`--widescreen-logic`), the Stone Hill attract demo desyncs: rand diverges at tick 123, Spyro's position at tick 541 (`logs/run5-wslogic`). The cost is the render path once more per tick, without rasterisation.
- **Snapshot (revised the same day):** the first version copied the whole 2 MB arena, which can't miss a writer. `--present-dirty` then measured 22–40 dirty 4 KB pages per pass, so the snapshot now covers only what the game reads again: the scratchpad, the game's data and bss, the moby array, the particle space and a 4-byte cache in each sector header. The primitive buffers, OTs and emulated stack are scratch. `--present-dirty` keeps a full copy and checks, byte for byte, that nothing else changed: 0 misses over the demos and two Artisans scripts. That's 4–6 MB per tick down to 170–260 KB.
- **On the Vita:**
  - **Capture:** with `gpu=1`, presentation frames are captured on the GPU, from the renderer's targets into a ring of textures. They're never read back: on the GPU renderer, `StoreImage` means a CPU replay of the frame (D35).
  - **Budget:** the in-between frame is skipped when the last tick's game CPU time plus its cost would pass `interp_budget` (default 26 ms). A late tick would raise `g_DeltaTime` and change how the game runs (D28); a skipped in-between frame only drops the picture to 30 fps for that tick.
  - **Defaults and menu (revised 2026-10-30):** both on by default on the Vita (the host keeps them off for its oracle checks). The port's own options menu (hold SELECT, press START) switches them at run time and saves them to `config.txt`. It's a platform overlay, not an entry in the game's own Options, so the decomp's menu code stays untouched and the game never sees the menu's buttons.
- **Revised after the first hardware test (2026-10-30, the user's choice):** widescreen draws once, in 16:9, in normal play; the logic sees the wider view, so mobys at the 16:9 edges count as on screen. The hidden 4:3 logic draw cost 4–8 ms per tick and pushed busy scenes to 20–26 fps on the Vita. The two-draw form stays for attract demos (they must replay) and as `widescreen_exact=1` / `--widescreen-exact`. Presentation passes no longer touch VRAM (PsyCross 0008, `g_psyxVramFrozen`): re-running the pause screen's texture save in a pass is what turned every texture red and green.
- **Second hardware test (main `d67e6fc`):** `r_environment` culls sectors with the camera's view matrix, not the projection matrix, so its frustum stayed 4:3 and the 16:9 strips showed textures and geometry loading in late. Its X row is narrowed too (mips2c `before` hint at `0x8002594C`). Another hidden 4:3 test of the same kind would show the same symptom. `r_moby` culls with the projection matrix (already widened); the sky (it gets both matrices) and the particles showed no gaps at the edges in the host shots, which isn't proof.

## 2026-10-30: whole game on the host (groundwork for M4)

*Numbering:* D25–D29 are M3's and D30 the presentation's (above), so this section is D31.

### D31. The whole-game check: a level tour by the game's own cheats, on both sides
- **Chosen:** the host's `--tour` (`src/platform/linux32/tour.c`) and the oracle's `ORACLE_TOUR` (`tools/oracle/capture.lua`) play the same script. After a new game, each entry is a level reached with the game's level-warp cheat from the inventory (`CheatProcessLevelWarp`, cheats.c; world 6 needs LEFT for the digit 6), or the credits, LEVELS OPEN or 99 LIVES cheat typed in the inventory, or an ending. `--pad-after` / `ORACLE_PAD_AFTER` then frees Nestor and saves at the fairy.
- **Endings by poke:** `end2` and `end3` write what the game's own moby code writes when Gnasty Gnorc is beaten (moby_update.c, Gnasty state 9: cutscene 2, then `InitCreditsSequence(0)`) and when Spyro enters Gnasty's Loot's vortex with 14000 gems (moby 217: cutscene 3, then the full credits). Beating Gnasty or collecting every gem with a pad script isn't practical; from the cutscene on, the code that runs is the game's own.
- **Flight levels:** the tour stays at most 60 vblanks in x5 levels. Left alone, Spyro flies into the water there and drowns about 240 vblanks after arriving, and the flight results keep interrupting the warp.
- **What compares, and what can't:** function samples replay exactly (as in M2). The per-vblank game-state traces can't be matched tick for tick outside the attract demos: the PS1 drops frames (`g_DeltaTime` 3 or 4), the host never does, so Spyro's glide into each level integrates differently (positions apart by up to ~60 units after 300 vblanks). PCSX-Redux `a5d621af` has no CPU overclock setting that would remove the drops. So each level is compared by function replay, by a screenshot from each side taken when the tour leaves it (`tools/oracle/tour_pairs.py`), and by the visit and sequence lengths in the traces (`tools/oracle/tour_report.py`).
- **Evidence:** `docs/whole-game-host.md`.

## 2026-10-30: Vita packaging

### D32. Vita link: 16 KiB of headroom after the text segment
- **Problem:** `vita-elf-create` appends its SCE data (module info and the import tables, about 2.8 KB here) at the end of the text segment. The linker starts the data segment at a 64 KiB boundary picked by `DATA_SEGMENT_ALIGN`, which can be right after the text. When the text ends less than ~2.8 KB below that boundary, packaging fails: "Cannot allocate 2776 bytes for SCE data at end of segment 0; segment 1 overlaps". Main had 43 KiB to spare by chance. The hand-written pete/camera branch shrank the text, left 704 bytes, and hit it. Reproduced on main by padding `.rodata` so that the text ends at `0x8132FD40`.
- **Decision:** link with `-Wl,--defsym=__sce_headroom=0x4000` (`src/platform/vita/CMakeLists.txt`). vitasdk's default linker script adds `__sce_headroom` bytes before aligning the data segment, so the data segment always starts at least 16 KiB after the text. Costs at most one 64 KiB step of address space, no memory. The padded build packages with it; main's layout is unchanged (data at `0x81340000` before and after).

## 2026-10-30: hand-written C for pete and camera

### D33. Hand-written C replaces mips2c units one function at a time, with the generated version a build switch away
- **Replace, don't fork:** a hand-written function in `src/game/*_hand.c` takes its unit out of the mips2c build (`M2C_HAND` in `cmake/mips2c.cmake`). The oracle replay finds it through `src/game/hand_index.c`.
- **A/B switch:** `-DSPYRO_M2C_FUNCS=name;name` (or `all`) builds those units from mips2c again and compiles their hand C out (`M2C_<name>=1`). It's for A/B tests and bisecting, on any target.
- **Partly hand-written units** (`M2C_PARTIAL`): the mips2c unit stays linked under `<name>_gen`, and the hand C calls it for the cases it doesn't cover yet. The big function can then be done and verified state by state instead of all at once. `func_80043FE4` (the 3,865-instruction pete physics) went this way; with its flying-ability phases done it is fully hand-written (`M2C_HAND`), and the list is empty.
- **Differential sweeps for paths no capture reaches** (`SPYRO_REPLAY_SWEEP`, `SPYRO_REPLAY_POKE` in `src/game/oracle_replay.c`): one RAM word set to each value of a list, the mips2c unit as the reference (a `-DSPYRO_M2C_REF=ON` build links every hand-written function's unit as `<name>_gen`), then the hand C from the same entry state. The reference is itself checked against the emulator on every captured sample, and it is a per-instruction translation, so agreement over the swept inputs is strong evidence. It does not replace oracle samples where a capture can reach the path.
- **Names for decomp fields stay in our headers:** `src/game/hand_structs.h` has layout-identical views (`SpyroFlameView`, `PadFrame`, `CameraView`), checked with `static_assert` against the decomp's `offsetof`. The decomp's own matched C uses the same `unk_` names, so a renaming patch would have to touch it all.

### D34. Hand C doesn't depend on return values the decomp's C doesn't return
- `func_80043FE4` reads v0 after calling `UpdateSpyroTurnMomentum` and `ApplySpyroSoftTurn`. The decomp declares both `void`, and they compile to functions whose last call (`func_80017928`, the absolute 12-bit angle difference) leaves its result in v0. The mips2c unit reads the host's return register after a `void` C call. That works on i686 at -O1 because the tail call's result is still in eax, but nothing guarantees it, on the Vita or with other flags.
- The hand C calls `func_80017928` again on the same fields (`TurnLeft` in `pete_hand.c`). It is pure, so the result is identical and doesn't depend on the ABI. Any other mips2c unit that reads v0 after a `void` decomp function has the same latent dependence. None is known to have failed.

## 2026-10-30: M3 GPU renderer

### D35. Vita GPU renderer: our own vitaGL `GR_*` backend, VRAM-rect render targets, readback by replay
- **Chosen:** `src/platform/vita/gpu_gl.c` implements PsyCross's `GR_*` interface on vitaGL. PsyCross's OT walker and splits are unchanged (no PsyCross patch was needed). `config.txt` `gpu=1` (the default) selects it at run time; `gpu=0` is the software rasterizer of D26. `gpu.c` owns the `GR_*` names and dispatches; `gpu_soft.c` is compiled with a forced-include header (`gpu_soft_names.h`) that gives its entry points a `soft_` prefix. `prof.c`'s `--wrap` entries wrap the dispatchers, so the "time" lines cover both modes.
- **Why not `PsyX_render.cpp` as it is (D29's plan):** it treats the window as the PS1 display. Draws go straight to the backbuffer, and VRAM only gets the picture back by reading the framebuffer on every `DrawSync`. Spyro breaks that model in several places. The boot logos (24-bit) and loading screens are `LoadImage`d into the display area and never drawn, so they would not show. The pause and inventory background `StoreImage`s the drawn frame (`gamestates/draw.c`), which needs the pixels on demand, not every frame. Its blend modes also blend texels without STP. And vitasdk's SDL2 has no GL context for it. What it does have, and what we reuse, is the `GrVertex` layout and attribute set-up, and the idea of keeping VRAM as a texture and doing the CLUT lookup in the shader.
- **How it draws:**
  - `s_vram` is the PS1 VRAM as the CPU sees it. It is uploaded as a 512×512 RGBA8 texture holding the raw 16-bit words (two per texel), and the fragment shaders decode 4-bit, 8-bit (CLUT) and 15-bit texels from it themselves: texel 0 transparent, colour × vertex / 128.
  - Each draw area (clip rect) the game draws into becomes a **target**: an FBO over that VRAM rect at the screen's resolution. Spyro's two 512×224 buffers become 725×508, so the 4:3 picture is 1:1 with the 960×544 screen's pixels; `gpu_res=N` gives N× native instead (now `res=`, D40). Inside its rect a target is newer than `s_vram`. `LoadImage` and `MoveImage` into a target are also drawn into it (from the uploaded VRAM), `ClearImage` clears both, and a draw area that overlaps targets merges them.
  - **Present:** the displayed VRAM rect is drawn from `s_vram`, then the targets over it at their places, 4:3 in the middle of 960×544. A 24-bit display (the boot logos) is converted on the CPU.
  - **Semi-transparency follows the PS1:** only STP texels blend. Average, add and add-a-quarter run in one premultiplied pass (`ONE, ONE_MINUS_SRC_ALPHA`); subtract runs in two passes (opaque texels, then `REVERSE_SUBTRACT`). Polygons with an edge of ≥1024×512 are dropped, as in `gpu_soft.c`.
  - Not modelled, as in `gpu_soft.c`: mask bit, texture window, dithering.
- **Readback by replay:** `s_vram` needs a target's pixels when something reads that VRAM: `StoreImage`, `MoveImage` from it, a textured prim whose page or CLUT lies in it, a screenshot, or a 24-bit display. Each target logs what was drawn into it since it was last cleared whole (Spyro: every frame, `isbg`): the splits with their vertices and environments, copies, and partial clears. A readback replays that log through the software rasterizer (its VRAM is free scratch with `gpu=1`). The result is the PS1's exact pixels: `gpu=1` screenshots are byte-identical to `gpu=0`'s and the host's. It needs no GPU sync and costs one software-rasterized frame, which only happens when the pause menu opens. If a log exceeds 8 MB, or with `gpu_readback=0`, the target is drawn down to 1× and read with `glReadPixels` instead.
  - **Why:** Vita3K (this macOS build, Vulkan and OpenGL backends alike, surface sync on or off) returns zeros for everything read back from GPU-rendered surfaces, so a `glReadPixels` readback can't be checked there. The replay is also exact, which `glReadPixels` of an upscaled target is not.
- **Fallbacks:** with no `ur0:data/libshacccg.suprx` (or `ur0:data/external/`), `gpu=0` and SDL2's GXM renderer are used, with a log line. If vitaGL starts but the game's shaders don't compile, the software rasterizer's picture is shown through vitaGL's blit shader, also with a log line. Once vitaGL owns GXM, SDL2's renderer can't take over. If not even the blit compiles, the app logs the failure and exits.
- **vitaGL findings** (the installed `libvitaGL.a`; its source isn't in vitasdk, so these come from disassembly and experiments):
  - `vglInitWithCustomSizes`/`vglInitExtended` return 1 when the resolution had to be clamped to the display's maximum and 0 otherwise. They initialise either way, so the value is not a success flag.
  - vitaGL's `init_gxm` takes a successful `sceAppMgrGetBudgetInfo` to mean "system app". A game gets an error from it on a Vita, but Vita3K's stub returns success, and then nothing vitaGL draws reaches the screen. `-Wl,--wrap=sceAppMgrGetBudgetInfo` returns an error on both.
  - `GL_COMPILE_STATUS` is true even when the shader compiler (shacccg through vitashark) failed; the program is then unusable (Vita3K: `sceGxmShaderPatcherCreateFragmentProgram` `INVALID_POINTER`). `shark_install_log_cb` catches the compiler's errors, and any error fails the compile.
  - The GLSL translator adds names of its own, so a shader variable called `y` clashed with one of them ("already declared"). Shader variables are q-prefixed.
  - In Vita3K, a `floor()` result divided by a constant vector, used as a texture coordinate, stopped every frame from showing, while multiplying by the reciprocal worked. The shaders have no divisions and no `mod()`: every divisor is a power of two, so multiplying by its reciprocal is exact.
  - Pointing the VRAM texture at our own block (`vglOverloadTexDataPointer`) was 10× cheaper than `glTexImage2D` in Vita3K, but only the first block was ever sampled there. The world's textures, loaded later, were missing. The upload is a full `glTexImage2D` when the GPU needs VRAM that changed (below).
- **Checked in Vita3K** (`logs/run44`–`run52`): title, 12000-vblank attract demo, M2's gems script and a pause → inventory script, all with `gpu=1`. The pictures are right in window captures: textured world, Spyro, Sparx, semi-transparent waterfalls, HUD, and the pause/inventory background. The replay shots are byte-identical to the host's: demo 100/100, gems 36/36 plus the memory card. There are no PsyCross errors. `gpu=0` with `realtime=0` is still byte-identical to the host: demo 100/100 plus the 35 MB WAV, gems 36/36 plus the card. Vita3K CPU time per vblank on the gems script, real time: busy 6.82 ms with `gpu=0` and 2.79 ms with `gpu=1` (raster 4.85 → 0.90 ms, present 0.55 → 0.05 ms; game logic about 1 ms either way).
- **First hardware run** (user, build `2661415`, 2026-10-30, about 3.5 minutes from boot through Artisans, pause menu included):
  - The shaders compiled on the device in about 8 s. There were no errors.
  - It ran at 60 vblanks/s throughout, and usually at 29–30 pictures/s (22–26 in the heaviest stretch), against 14–20 vblanks/s with the CPU renderer (D28).
  - Busy CPU was 60–93% of each vblank. Per vblank: game 2–5 ms, raster 5–6 ms, SPU/CD 0.8–2.7 ms, OT walk 1–1.5 ms, present 0.1 ms.
  - The VRAM upload took about 4.3 ms each and ran every frame (about 2.2 ms per vblank, inside "raster").
  - Opening the pause menu replayed one frame in about 50 ms.
- **Uploads only when the GPU needs them** (after that run): the only VRAM write in a normal frame is the game's automatic background clear (`isbg`) of the buffer it's about to draw, which lies inside a target. The target is cleared with `glClear` anyway, so a clear that falls inside a target now only marks it `tex_stale`, and doesn't make the GPU's VRAM texture out of date. A textured prim whose page or CLUT lies in such a target, a copy from it, or a target merge uploads first, as `cpu_stale` targets read back first.
  - In Vita3K, uploads fell from about 150 to 1–2 per 300 vblanks (screenshots' readbacks, level loads), and raster time halved (0.90 → 0.46 ms per vblank on the gems script).
  - Pictures were unchanged: demo 100/100 and gems 36/36 byte-identical to the host, and the pause and inventory screens right.
  - Expected on hardware: about 2 ms per vblank less. **Confirmed** (user, build `c40c9cf`): uploads 0 in nearly every 300-vblank window, raster 5–6 → 2.7–4 ms per vblank, busy CPU 60–93% → 43–78%, and 143–150 pictures per 300 vblanks in every gameplay window.
- **Not done here:** widescreen and 60 fps (M4); fewer draw calls; the SPU mix on another core (done in D37).

### D36. Portals, exits and the balloonist: short walks from poked positions, sampled per gamestate
- **Chosen:** two pad scripts (`tools/oracle/pads/route_portal_exit.txt`, `route_vortex_balloon.txt`) run as the pad-after script of a new game and the LEVELS OPEN cheat on both sides. The walks start from a `poke:` of Spyro's position next to the portal, the vortex ledge and the balloonist's dock. From there the game's own code runs: portal surface, vortex moby, balloonist moby, gamestates 1, 9, 10 and 12. The routes were steered on the host (`--step`, `tools/steer.py`) and made state-anchored by `tools/anchor_pad.py`. `capture.lua` samples per level and gamestate (`ORACLE_PER_STATE=1`), so the transition gamestates get their own samples.
- **Why the pokes:** pad lines are timed in vblanks. The PS1 runs homeworlds at a variable frame time (`g_DeltaTime` 2 or 3), the host at a fixed 2, so a walk of a minute ends somewhere else in the emulator. Route A from Artisans' start missed the portal there (`logs/run7-oracle-routeA-long`). A walk of a few seconds lands the same on both sides. Steering in the emulator itself (a Lua closed loop) would remove the pokes, but this needed only one portal, one vortex and one ride.
- **Also:** `$ORACLE_FUNCS` option `anysp`, because `func_8004F7E8` uses `$sp` as data (`docs/whole-game-host.md`).
- **Evidence:** `docs/whole-game-host.md` "Portals, exits and the balloonist".

### D37. Vita: the SPU mix and XA decode on their own core, replayed from per-vblank packets
- **Chosen:** `src/platform/vita/mixer.c` runs the software SPU mix and the XA decode on a thread pinned to user core 1 (`sceKernelCreateThread`, `SCE_KERNEL_CPU_MASK_USER_1`, priority 0x70, ahead of the game thread's 0xA0 if they ever share a core). The game thread doesn't mix any more. Once per vblank, where it called `psx_spu_vblank()`, it now:
  1. copies the 24 voices and the mix registers (master and CD volumes, `CdMix` matrix, mute) into a packet;
  2. runs `psx_spu_step()` on the live SPU state;
  3. posts the packet to a ring of 8, and waits only when all 8 are in flight.
- **What the mixer thread does:** it replays the packets in order. It keeps its own copies of the voices, with the ADPCM decoder state (history, decoded block, previous sample), and takes everything else from each packet: registers, envelope (key off), loop address and position. A voice whose `keyon_seq` changed since the last packet is taken whole, because key on resets the decoder. Then `psx_spu_mix` → `psx_audio_out` (the SDL2 queue, and `audio.wav` with `wav=1`).
- **Exactness:** the game reads back SPU state that the mix changes. `SpuGetKeyStatus` and `SpuGetAllKeysStatus` (`spu.c`) see a voice go to "envelope off" when its sample hits an end flag without repeat. Nothing in that depends on the samples. `psx_spu_step()` walks the same 4.12 counter steps and block flags as the mix, block by block instead of sample by sample, and leaves the decoder alone. So everything the game reads comes from the game thread, as the synchronous mix would have left it. The mixer checks that its own positions match the step's when a voice wasn't keyed on, and counts any mismatch in the log. The count was 0 in every run.
- **The XA music** moves with the mix. libcd's `psx_xa_sector` and `psx_xa_reset` are wrapped (`-Wl,--wrap`). The game thread still reads each sector and applies the stream's file/channel filter (`psx_xa_accept`, file I/O stays on one thread). It queues the raw 2336 bytes, and the resets, in order in the next packet. The mixer decodes them (`psx_xa_decode`) before that packet's mix, so the XA FIFO in `xa.c` is only touched on the mixer thread.
- **SPU RAM** is the only state the two threads share. `SpuWrite` and `SpuInit` are wrapped to wait until every posted packet has been mixed first ("drain"). The mixer reads RAM only. RAM is written at loads, so the waits cluster there: about 25 ms in total during boot in Vita3K, then none.
- **Shared code:** `src/platform/common` changes only by refactoring:
  - `libspu.c` splits `psx_spu_vblank()` into `psx_spu_mix_regs` + `psx_spu_mix` (over a voice array) + `psx_audio_out`, and adds `psx_spu_step()`.
  - `voice_next_block` is split into position and decoder halves.
  - `xa.c` splits `psx_xa_sector` into `psx_xa_accept` + `psx_xa_decode`.
  - The linux32 host still calls `psx_spu_vblank()`. Its 12000-vblank WAV from this tree is byte-identical to `run6-host-base`'s.
- **`config.txt mixer=`:** 1 (default) is the thread; 2 is the same thread with each vblank waiting for its mix (lockstep, for debugging); 0 mixes on the game thread as before. If the thread can't be created, the game thread mixes and the log says so.
- **Pitfall found on the way:** `--wrap=psx_xa_reset` also redirects the call inside `mixer.c`. The mixer's own reset was being re-queued from the mixer thread, so the XA FIFO was never reset. The WAV diverged at vblank 438, where the intro's XA starts, and the game later hung on a slot that thread had taken. The mixer calls `__real_psx_xa_reset`.
- **Checked in Vita3K** (`logs/run62`–`run74`, before merging main's PR #12):
  - With `realtime=0`, the 12000-vblank demo with `wav=1` is byte-identical to the host: 100/100 shots and the 35 MB WAV, with both `gpu=1` and `gpu=0`.
  - The gems script is also byte-identical: 36/36 shots and the memory card, in both modes.
  - With `REALTIME=1` on the gems script, `gpu=1`: ~60 vblanks/s, and `audio.wav` holds all 8768 vblanks.
  - Game-thread "spu" time fell from 0.47 to 0.09 ms per vblank, and busy time from 3.16 to 2.73 ms (`run74` is the branch before this change). The mixer thread took 0.4–0.6 ms per vblank (Vita3K's times).
  - What remains on the game thread is the CD model, the XA sector reads, the step and the 2.6 KB voice copy.
- **After merging main (PR #12, Vita widescreen and 60 fps)**, `logs/run75`–`run79`:
  - The demo is still byte-identical in both GPU modes (shots and WAV), and so are the gems runs (shots and card).
  - `REALTIME=1` with `widescreen=1 interp=1` on the gems script: 58.6 vblanks/s in Vita3K, the WAV holds all 8768 vblanks, game-thread `spu` is 0.15 ms per vblank and the mixer takes 0.5–0.7 ms. The audio queue ran dry 0–11 times per 300 vblanks.
- **Expected on hardware:** most of the ~2.5 ms "spu" per vblank (D35's `c40c9cf` run) moves to core 1, and the `mixer N:` line shows it there. Not yet run on hardware.

## 2026-10-30: hand-written C, round 2

### D38. Every pete and camera function hand-written, each checked on its own branch and again on the merged tree
- **Chosen:** the seven remaining functions were done in parallel, one branch per function group: camera, state change, transitions, and dispatch plus the flying-ability phases. Each branch replayed every capture, swept against the mips2c unit and checked both demos. The merged tree was then checked again as a whole, because a function's callees changed from mips2c to hand C between its branch and the merge.
- **Reference build switch:** `-DSPYRO_M2C_REF=ON` links every hand-written function's mips2c unit as `<name>_gen`, so differential sweeps work for fully hand-written functions too. It replaces the one-off `M2C_PARTIAL` link of `func_80043FE4`. Each reference unit is translated on its own. In one batch, mips2c calls the batch's own units through their register entries, which the rename leaves undefined. `SPYRO_REPLAY_SWEEP_REG` sweeps an entry register (`func_8003EA68`'s new state, a0).
- **Proof of identical behaviour beyond the samples:** the host is deterministic, so a build of main and the final build ran the same scripts: attract demos, the whole-game tour, routes A and B. Traces, screenshots and saves came out byte-identical. A difference anywhere in the game's logic would show there, not only at sampled calls.
- **On the Vita, no static initialisers from decomp data labels:** there, labels are pointers into the arena (D25), not link-time constants. Hand C builds tables of them at run time (`camera_hand.c` `Spring()`). The host build doesn't catch this, so the Vita build is part of the check.
- **Known, accepted differences:** inputs the game never sets up, where the asm reads low RAM and the hand C faults. They are state 26 or the follow-moby camera with no moby in use, the scripted camera states 0x80000007/0x8000000C with no moby, and a state change that plays a sound before any level has loaded. Each one is in its translation-log row.

## 2026-10-30: Vita picture options (texture filtering, perspective)

### D39. Texture filtering after the palette lookup, and perspective from a GTE side channel

- **Switches** (`config.txt`, `gpu=1` only, all off by default): `tex_filter=1` (3-point, N64 style) or `2` (bilinear); `persp=1` (perspective-correct textures); `subpixel=1` (vertices at their exact sub-pixel positions). With all three off, `gpu_gl.c` draws exactly as before. Every change is behind `s_xmode`, the shader sources are the old ones, and PsyCross's new hooks stay NULL. Any of them switches to an expanded vertex (`XVertex`: float x, y, w, and the filter's UV box) and its own vertex shader.
- **Filtering:** the fragment shader does the CLUT lookup per tap, then filters. Bilinear on palette indices would be meaningless.
  - Taps are clamped to the triangle's UV box, so neighbouring textures in the page never bleed in. Spyro gives a 32-texel texture as u0..u0+31 (`logs/run14-host-tridump`), so the box is the triangle's own lowest to highest u and v. A triangle has two opposite corners of its quad, so it spans the quad's box. The first version stopped one texel short, and every polygon edge showed a hard step (the blocky grass in `logs/run5-vita-all`).
  - **Wrap within a tile:** 88% of the world's textured triangles map one whole aligned 32×32 tile, and the same tile repeats from polygon to polygon. Subdivided near terrain maps its quarters: complete quartets with the same page and CLUT. A triangle whose box lies in one aligned 32×32 tile and covers at least 16×16 of it wraps within the tile instead of clamping, so the filter blends across polygon edges as the texture was drawn to. Clamped tiles left faint lines at every polygon edge (`logs/run17-vita-mall` against `logs/run28-vita-mall2`).
  - **What stays:** Spyro rotates and flips its grass tile from polygon to polygon, so the texture itself doesn't continue across those edges. The PS1's blocky texels hide that; filtered, a faint edge shows. Filter-only (no perspective) shows the same edges (`logs/run18-vita-mfilt2`), so they don't come from mixing perspective and affine polygons.
  - Transparent texels (0x0000) get no weight, and the colour is renormalised over the rest, so cut-out edges don't go dark or pink. A pixel is drawn where at least half the weight is opaque.
  - STP is the weighted majority, the same in both passes of the subtract mode.
  - Sprites and tiles (`SPRT`, `TILE`, HUD backgrounds) keep the nearest texel: they're drawn 1:1 and filtering only blurs them. Polygons are told apart by the hook below.
  - The game sets no texture window (no GP0 0xE2 anywhere in its code), so the UV box is the only bound needed.
  - The mask bit is not modelled by the GPU renderer (D35), so there is nothing for the filter to respect beyond STP.
- **Perspective and sub-pixel: a vertex cache beside the GTE** (`src/platform/common/vcache.c`, PsyCross patch 0007). The primitive layout stays PS1-native (`USE_PGXP=0`, `USE_EXTENDED_PRIM_POINTERS=0`, porting plan §4–5); the information travels next to it:
  1. `GTE_RotTransPers` calls `g_psyxRtpHook` after the integer projection. The hook reads the GTE registers only. It recomputes the transform without the truncations (the 44-bit sums, not MAC >> 12; the exact division, not UNR) and records the exact screen position and the view depth under the 16-bit SXY2 the game got. It writes nothing the game reads: `gteRegs` is untouched, and the table is ours.
  2. `MakeVertexTriangle`/`MakeVertexQuad` call `g_psyxPolyHook` with each polygon's own coordinates. It looks them up and leaves the answer in the `GrVertex`'s unused trailing bytes (`tcx`, `tcy`, `_p0`, `_p1`): polygon flag, entry, which depth, and "every vertex found". Sprites, tiles and lines never get one.
  3. `DrawOTag` calls `g_psyxDrawOTagDone` when it returns, drawn or skipped (the widescreen logic pass), and the table moves to the next frame. Spyro builds and draws its OT in the same frame (`gamestates/draw.c`), as the presentation passes do (D30).
  4. `gpu_gl.c` adds the sub-pixel offset to the integer position (`subpixel=1`) and uses the depth as w (`persp=1`) when the whole polygon was found. Otherwise w = 1, the PS1's affine mapping. 2D and HUD prims, and anything not found, fall back cleanly.
  - This works for Spyro because it projects every vertex it draws through RTPS. That includes the midpoints of subdivided near terrain, which `r_environment.s` averages in 3D and then projects. It stores SXY2 into its prims unchanged, so the prim's coordinates find the entry. The sub-pixel offset is taken from the pixel centre (exact − integer − 0.5), so found and not-found vertices stay unbiased against each other.
  - **Depth scales:** the world renderer zeroes the GTE translation and projects camera-relative vertices in units that differ between its passes by powers of two. The same screen point comes back with depths 4 or 16 times apart (both matrices have row norm 4095: `logs/run5-host-amb`). Its near precision path (all three coordinates within ±256) projects single vertices at 16 times their coordinates, so one polygon can mix scales. Only the ratios of a polygon's depths matter, so an entry keeps up to three depths a power of two apart, plus depth/16 when the vertex may be a precision one. Each polygon then picks the combination with the smallest spread: real depths within a polygon are much closer than ×4. Before this, 72% of polygons were found whole over the attract demos; after it, 87%.
  - **Ambiguity:** two different vertices at the same integer pixel in one frame (more than a pixel apart, or depths not a power of two apart) make the key unusable. So do vertices the GTE clamped (IR or SX/SY saturated, divide overflow).
- **Hit rates** (`vcache` lines in the Vita log, `--vcache` at exit on the host):
  - Attract demos (host): 87% of polygons found whole, 95% of vertices.
  - Title and intro (Vita3K): 89–95%.
  - Artisans on the gems script: 79–85%, at 4:3 or with widescreen and 60 fps.
  - The rest are drawn affine, as on the PS1. Most misses are ambiguous keys (~150 polygon vertices per frame) and vertices the GTE clamped; ~30 per frame have no entry.
  - The table is 8192 entries (256 KB, fits the A9's L2); the peak is ~4100 points per frame. The hit rates are the same as with 32768 entries.
- **The game logic is untouched.** On linux32, `--vcache` records into the same table:
  - Both attract demos match the oracle tick for tick with the cache on (`logs/run34-host-final-m3`: Stone Hill 866 ticks, Doctor Shemp 768).
  - The trace is byte-identical to a run without it, and so are the 100 shots.
  - With `--widescreen --interp --vcache` the logic trace is identical too (`logs/run35-host-ws-vcache`).
  - `ctest` 4/4 pass, including the GTE test against PCSX-Redux's 9,314 vectors.
  - In Vita3K, the replay shots (the PS1-exact pictures the game sees) are byte-identical with every option on and off (`logs/run15`–`run17`).
- **Off is unchanged** (Vita3K, `realtime=0`, gems script with the pause and inventory screens). Eight window captures with all three switches off are pixel-identical to the same build with main's `gpu_gl.c`: 0 of 2,196,480 pixels differ in each (`logs/run15-vita-mbase`, `logs/run16-vita-moff`).
- **Pictures** (window captures `win-V.png`, the same vblanks in every run): base `logs/run15-vita-mbase`; all three on `logs/run28-vita-mall2`; bilinear only `logs/run18-vita-mfilt2`; perspective and sub-pixel only `logs/run19-vita-mpersp`; all on with widescreen and 60 fps `logs/run20-vita-mws-all`. 7000 is Artisans with the waterfalls (transparent water, the sky, the near terrain), 8300 the gem pedestal and the hedge, 8700 the pause menu and 8900 the inventory (3D font and HUD models, which are untextured and unchanged).
- **CPU cost** (Vita3K, `REALTIME=1`, gems script, busy ms per vblank, `tools/vita/time_summary.py`; noisy, the Mac was loaded):
  - 4:3, 30 fps: 1.06 off, 1.68 with all three on (`logs/run32`, `run33`). Game +0.35 ms (the projection hook), OT walk +0.25 ms (the polygon lookups); the vertex expansion is ~0.17 ms per vblank inside "raster".
  - Widescreen and 60 fps, where the passes project everything again: 3.37 off, 3.54 all on (`logs/run29`, `run30`). The first pair before the trims below was 2.94 and 3.60 (`logs/run21`, `run22`).
  - `tex_filter` alone has no CPU cost beyond the expansion: it doesn't record projections (`logs/run31`).
  - Trims made after the first measurement: the table shrank from 900 KB to 256 KB, there is one division per vertex, and the lookups are skipped when only the filter is on.
  - **Not measured on hardware.** Vita3K runs ARM code several times faster than the Vita's A9 (D29 estimated ~11× for the whole game), so the hardware cost could be a few ms per vblank with `persp`/`subpixel`. The `time` lines' game and ot columns will show it.
- **GPU cost:** not measurable in Vita3K. The filter does 3 (3-point) or 4 (bilinear) palette lookups per pixel, each one or two dependent VRAM fetches, against one before. On hardware, a GPU-bound frame shows as more "wait" per vblank and fewer pictures per 300 vblanks. `tex_filter=1` is the cheaper choice if bilinear costs frames.
- **Why a side channel and not PGXP:** PsyCross's PGXP needs `USE_PGXP=1`. That changes `GrVertex` to floats and, with extended prim pointers, the primitive layout the game writes. This way, the bytes the game writes and reads are exactly the PS1's, and the extra information lives in a table the game never sees and in four bytes of PsyCross's own vertex.

## 2026-10-30: GPU render resolution

### D40. Render resolution: `res=native` (default), `res=ps1`, `res=N`; VRAM stays PS1-exact
- **Where it started:** D35's renderer already draws each target (the game's draw areas) at the screen's resolution, so the brief's "renders at PS1 resolution and scales up" was not the state of main. What was missing: a switch with clear names, half-pixel placement at higher resolutions, a present that doesn't resample the native picture, the game-over/credits freeze frame at the targets' resolution, and a GPU cost in the log.
- **Switch:** `config.txt` `res=native` (default) draws a 512×224 game buffer at 726×508, so the 4:3 picture is 1:1 with the 960×544 screen's pixels (16:9 under widescreen, D30: 960 wide). `res=ps1` (or `1`) draws at the PS1's own pixels and scales the picture up when shown, as `gpu_res=1` did. `res=2`…`4` draws at N× and scales down when shown (supersampled edges). `gpu_res=` stays as the old name.
- **Default `native`:** the hardware runs since D35 already measured it at full speed (59.5–60 vblanks/s, 143–150 pictures per 300 vblanks), and it is what the screen can show.
- **The 4:3 area is 726 pixels wide, not 725:** a 725-wide render target lost its last column (black in every Vita3K capture at native, with main's renderer too; 960- and 512-wide targets were full), and the game-over move then left a stray coloured column there. With 726 (0.1% wider than exact 4:3, still 1:1) the picture is lit to its last column, which matches the PS1's column 511. Whether hardware has the same quirk is unknown; an even width is safe either way.
- **Half a target pixel, not half a PS1 pixel:** the PS1 samples at a pixel's top-left corner, GL at its centre, so vertices were shifted by +0.5 PS1 pixel. At 1× that is the same; at native it moved every drawn polygon 0.7 px right and 1.1 px down against what `LoadImage` put in the same target and against the display mapping, and left the target's first column and row uncovered (fades and full-screen quads missed them). The shift is now half a target pixel: identical at `res=ps1`, and at higher resolutions target pixel j samples the PS1 position j/s, its own top-left corner.
- **Targets on whole screen pixels:** at `res=native` a 512×224 buffer was 725×508 target pixels (now 726, below), but Spyro's buffers start 8 lines into the 240-line display, 18.13 of the screen's 544 lines down, and the present stretched 508 rows over 507.73. With `filter=1` every row was blended with the next (soft HUD text), with `filter=0` a row was dropped. When the picture has the targets' own scale (the present at `res=native`, and the GPU-side presentation capture of D30), a target now starts on a whole pixel with one screen pixel per target pixel (`place()` in `gpu_gl.c`), and texture coordinates come from the target's real pixel size. `res=ps1` and `res=N` are scaled as before.
- **VRAM stays PS1-exact (unchanged from D35):** `s_vram` is the CPU's VRAM at 1×, and gets a target's pixels only when something reads them, by replaying the target's log through the software rasterizer. So everything the game reads back is exactly the PS1's, at any `res=`:
  - pause, inventory and flight-results background (`gamestates/draw.c:908-935`, `overlays/flight.c:248-258`): `StoreImage` of the frame, grayscale on the CPU (`func_80017E98`), `LoadImage` into a texture page. It is a 1× image by nature (the game converts the pixels), so it shows at PS1 resolution behind the menu, as on the PS1.
  - boot logos (24-bit): converted on the CPU from `s_vram`, as before. In Vita3K they show black in window captures with main's renderer and this one alike (the replay shots have them); hardware has to show whether that is Vita3K's.
  - textures loaded or changed at run time: `LoadImage` writes `s_vram`; a textured prim uploads it when it needs it.
  - render-to-VRAM-then-sample: a prim whose texture page or CLUT lies in a target reads that target back first (`sync_pages`). Spyro doesn't do this in normal play.
- **MoveImage between targets at the targets' resolution:** the game-over freeze (`draw.c:1671-1677`) and the credits (`func_credits_8007BFD0`) `MoveImage` the frame just drawn into the other buffer, then draw over it with `isbg=0`. Before, the destination target got the move from the 1× `s_vram` (a PS1-resolution frame for the whole game-over fade). Now, when the source rect lies inside one target and the destination in another, the destination is drawn from the source target's texture at full resolution. `s_vram` gets the same move from the replay, and the destination's log records it, so a later readback is still exact.
- **Pending copies flushed before a target is dropped:** `drop_target()` now sends out the queued VRAM→target copies first. Their `Target` pointers would otherwise outlive the target (or the slot the last target moves into), and the next flush would draw into a deleted FBO. The options menu's widescreen toggle (`vita_gpu_rescale()`, #14) drops every target mid-game, which made this reachable. It was first suspected for the red/green textures seen on hardware (main `3b4126a`), since drawing into a deleted FBO can write over GPU memory reused since, the VRAM texture included. It's kept as hardening. The confirmed cause is in D30 (PR #19): a widescreen presentation pass re-ran the pause screen's texture save, reproduced on the host (57,344 of 57,600 texture words wrong after pause and unpause, 0 with the fix). Toggles alone didn't reproduce red/green in Vita3K (three toggles in Artisans, `logs/run119`; with the fix `run120`, pictures unchanged).
- **Not done here:** sub-pixel vertex positions (D39's `subpixel=1` adds them from a GTE side channel; without it vertices are the PS1's integer screen coordinates, scaled); a high-resolution pause background.

## 2026-10-30: making the D39 options affordable

### D41. A decoded-texture atlas for the filter, vcache candidates per key, and bench=1
- **Hardware result that started this** (user, main `14380ab`, `tex_filter=1 persp=1 subpixel=1 res=native`, `logs/hw-14380ab-user/spyro.log`):
  - GPU-bound: 49–68 pictures per 300 vblanks in Artisans (10–12 fps).
  - The GPU-timed frame was 100–170 ms, with a "GPU tail" of 60–134 ms, while the CPU was 33–52% busy.
  - `3b4126a` (native resolution, none of the D39 options) did 100–150 pictures.
  - So the cost was the filter shader. Each of its 3–4 taps repeated the whole palette decode: two dependent VRAM reads plus ~25 ALU ops of nibble, byte and 15-bit unpacking.
  - The user also saw "graphical glitches".
- **Atlas** (`gpu_gl.c`, "decoded-texture atlas"):
  - **What's cached:** each block a filtered polygon uses (format, page, CLUT, and the 32×32 texel blocks its UV box covers) is decoded once by a GPU pass into a 1024×1024 RGBA8 atlas. RGB is the colour; alpha is the texel's kind (0 transparent, 1 opaque, 0.5 opaque with STP).
  - **The filter shader:** it now taps the atlas with plain fetches, with the same weights, box clamp, tile wrap, transparency renormalisation and STP majority as D39.
  - **When it's filled:** `expand()` does the lookups for a whole vertex buffer before any draw, and decodes what's new in one FBO pass.
  - **Invalidation:** any change to `s_vram` outside the targets starts over, in the other of two atlases, so draws already queued keep theirs. That covers `LoadImage`/`ClearImage`/`MoveImage` writes, readbacks, and a new target over a cached rect; CLUT rows count too. In gameplay `s_vram` doesn't change for minutes.
  - **Exclusions:** blocks over a target (a page or CLUT the game draws into) are never cached.
  - **Polygons the atlas doesn't hold, and sprites:** they are drawn with the persp/subpixel-only programs, which sample the nearest texel from VRAM. `draw()` splits a draw into runs of each kind, in order.
  - **No branch:** a single shader with both paths behind a dynamic branch drew no textured polygon at all in Vita3K (texture reads in a branch), so there is none.
  - **vitaGL findings on the way:** the decode pass's client arrays rebind array buffer 0, so the VBO is bound after `expand()` (glBufferData into buffer 0 crashed vitaGL's allocator). Interleaved client arrays (a stride) and a third texture unit were changed to packed arrays and unit 1 while debugging. `s_tex`'s unit is free during primitive draws.
- **vcache candidates** (`vcache.c`): ambiguous keys were the main reason vertices were missed.
  - Each key now keeps up to four candidates, each a position and a depth. Every distinct projection under the key counts: the same point at another scale (the ×4/×16 of D39), or a different point on the same pixel.
  - Each polygon picks the candidates with the smallest depth spread, as before.
  - Attract demos (host): polygons with every vertex found 87% → **96.9%**, vertices 95% → 98.7%, ambiguous keys (more than four candidates) 4 per frame. Artisans (Vita3K): 79–85% → **95%**. The remaining ~37 vertices per frame have no entry: coordinates the game didn't take from RTPS.
  - Both attract demos still match the oracle tick for tick (`logs/run40-host-vcache4`, `logs/run70-host-final2`), and so does the logic trace with `--widescreen --interp` (`logs/run46-host-ws-vcache4`). `ctest` 5/5.
- **Sub-pixel stays per key, not per polygon** (asked: "never mix precise and integer vertices in a polygon"):
  - A found key gives every polygon that uses it the same exact point, and a key not found leaves the vertex at its integer position in every polygon. So polygons sharing a vertex never crack.
  - All-or-nothing per polygon would put a shared vertex at its exact point in one polygon and its integer one in the neighbour, which is a crack of up to a pixel.
  - A polygon with one integer vertex has that vertex where the PS1 draws it; that's a wobble, not a crack.
  - So the position is always the key's first candidate's, for every polygon. A key whose candidates are more than 1/8 pixel apart stays integer in every polygon, whichever candidate's depth each polygon takes for perspective. With each polygon taking its own pick's position, the waterfall showed a dashed crack: neighbours had taken points up to a pixel apart for a shared vertex (`logs/run60-vita-all-atlas`, `run64`, `run65` win-7000, also without the filter; not without `subpixel`, `logs/run61`).
- **`tex_cache=1` (default): the atlas for the default picture too.** The parent's hardware baseline (main `d67e6fc`, no texture options, native 960×508, `logs/hw-d67e6fc-user/spyro.log`) was already GPU-bound in Artisans: 100–125 pictures per 300 vblanks, GPU-timed frame 45–60 ms, tail 30–40 ms. The base shader's palette decode per pixel (two dependent VRAM reads and ~25 ALU ops) is most of it.
  - With `tex_cache=1`, unfiltered textured prims (and, with `tex_filter`, the sprites) take the nearest texel from the atlas: one fetch, the same pixels. The atlas stores the decoded 15-bit colour exactly (k·8 in 8 bits), and the alpha says transparent, opaque or STP.
  - It needs the expanded vertex (atlas coordinates), which costs the CPU ~0.3–0.5 ms per vblank on hardware (D39's `expand` line). Blocks the atlas can't hold fall back to the VRAM decode, in runs, as with the filter.
  - `tex_cache=0` is the old path, unchanged.
- **`bench=1`** (`src/platform/vita/bench.c`):
  - **What it does:** leave the game alone. From vblank 600 it cycles 16 option sets, through the intro flyover, the title and both attract demos, so every set sees the same mix of scenes. Each set gets 60 vblanks per round for 12 rounds; the run ends at vblank 12120, about 3.5 minutes.
    - 4:3 native: all off with `tex_cache=0` (the old default) and `=1`; `tex_filter=1`; `=2`; `persp`; `subpixel`; all three with filter 1; with filter 2.
    - 4:3 ps1: off with cache 0 and 1; all three.
    - 16:9 native: off with cache 0 and 1; all three.
    - 16:9 ps1: off with cache 0 and 1.
  - **What's measured:** each set's pictures per second and GPU-timed frames (every second picture; frame and tail as in D40's "timed frame"). The first 12 vblanks after a switch don't count, since a resolution or widescreen change redraws the targets.
  - **What's logged:** `bench round N:` lines every 4 rounds, then `bench final:`, after which the configured options come back. Each `tex_cache=0`/`=1` pair is the before/after of the atlas on the default picture.
  - **Modes:** bench=1 runs at 30 fps (`interp=0`); bench=2 keeps `interp=`. The sets choose widescreen.
  - **Cost:** all option programs are compiled at start-up, so the shader compile takes longer.
- **Checked in Vita3K** (`realtime=0`, gems script plus the pause and inventory screens, window captures at the same 8 vblanks):
  - **Off unchanged:** with all options off, the captures are pixel-identical to main's `gpu_gl.c` and `vblank.c` (0 of 2,088,960 pixels differ in each; `logs/run42-vita-mainref`, `logs/run43-vita-off-t1`).
  - **All on** (`logs/run68-vita-all-t8`): textures filtered from the atlas, the terrain, Spyro, the waterfalls, the sky, HUD, 3D font and menus right, and no cracks.
  - **Game unchanged:** the replay shots (what the game reads back) are byte-identical to options off.
  - **Atlas:** in Artisans, every textured triangle (~1,400 a frame) is filtered from it; 41–464 blocks are decoded per 300 vblanks; at most 464 of 1,024 blocks are in use.
  - **`bench=1`** (`logs/run66-vita-bench`): runs the 10 sets through 20 rounds and logs per-set lines at rounds 5, 10, 15 and at the end, then restores the options. Vita3K's GPU timings say nothing about the Vita's GPU.
- **Not measured on hardware yet.** `bench=1` is for that. The target is everything on at native resolution holding 30 pictures per second in Artisans; if it doesn't, the defaults stay off and the bench lines give each option's cost.


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
- **Decision:** the CPU fixes can't close a 4× gap, so the GPU renderer starts in M3, not M4. The first attempt is PsyCross's own GL renderer (`src/render/PsyX_render.cpp`, VRAM as a texture with CLUT in a shader) on vitaGL, whose GLSL translator is in the installed library (`glsl_utils.o`). It needs `ur0:data/libshacccg.suprx` on the device. The software path stays as the fallback (`gpu=0`, and automatically when shaders can't be built).

## 2026-10-30: whole game on the host (groundwork for M4)

*Numbering:* D25–D29 are M3's (above), so this section starts at D30.

### D30. The whole-game check: a level tour by the game's own cheats, on both sides
- **Chosen:** the host's `--tour` (`src/platform/linux32/tour.c`) and the oracle's `ORACLE_TOUR` (`tools/oracle/capture.lua`) play the same script. After a new game, each entry is a level reached with the game's level-warp cheat from the inventory (`CheatProcessLevelWarp`, cheats.c; world 6 needs LEFT for the digit 6), or the credits, LEVELS OPEN or 99 LIVES cheat typed in the inventory, or an ending. `--pad-after` / `ORACLE_PAD_AFTER` then frees Nestor and saves at the fairy.
- **Endings by poke:** `end2` and `end3` write what the game's own moby code writes when Gnasty Gnorc is beaten (moby_update.c, Gnasty state 9: cutscene 2, then `InitCreditsSequence(0)`) and when Spyro enters Gnasty's Loot's vortex with 14000 gems (moby 217: cutscene 3, then the full credits). Beating Gnasty or collecting every gem with a pad script isn't practical; from the cutscene on, the code that runs is the game's own.
- **Flight levels:** the tour stays at most 60 vblanks in x5 levels. Left alone, Spyro flies into the water there and drowns about 240 vblanks after arriving, and the flight results keep interrupting the warp.
- **What compares, and what can't:** function samples replay exactly (as in M2). The per-vblank game-state traces can't be matched tick for tick outside the attract demos: the PS1 drops frames (`g_DeltaTime` 3 or 4), the host never does, so Spyro's glide into each level integrates differently (positions apart by up to ~60 units after 300 vblanks). PCSX-Redux `a5d621af` has no CPU overclock setting that would remove the drops. So each level is compared by function replay, by a screenshot from each side taken when the tour leaves it (`tools/oracle/tour_pairs.py`), and by the visit and sequence lengths in the traces (`tools/oracle/tour_report.py`).
- **Evidence:** `docs/whole-game-host.md`.

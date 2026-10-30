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

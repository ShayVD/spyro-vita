# How native the port is, and a staged plan to make it fully native (D76)

Written 2026-11-02 on main `2a0691c`, before PRs #48 (D75, dynamic resolution) and #49 (D74, joined draws) were merged; the measurements are from that code. The K runs are for main after both (`f534fb1`). This document changes no engine code. It adds three measuring tools and three hardware run configurations:
- `tools/native_audit.py`: the static tables;
- `tools/native_audit_map.sh`: a relink with a link map, which the static tables need;
- `tools/vita/native_share.py`: the run-time split from `spyro.log`;
- `tools/vita/fps-runs/k1`–`k3`: the hardware runs.

Numbers from tools and logs are measured. Numbers marked *estimate* are reasoning from them, and the reasoning is given.

## Summary

Brought up to date on 2026-11-04 (D101, main `82d05ce`) from the stage status lines in §5 and §3.5. The first version (D76) was written on main `2a0691c`. Stage 7's remaining steps were planned again on 2026-11-07 (D119, main `a47a30b`), and the fully-native list below is checked item by item there: what is done, what a step takes, and what stays by design.

- **The game itself is mostly native.** No instruction of the original game runs as mips2c any more (D96). Of the original executable's 91,415 MIPS instructions, **81.9% run as readable C** (matched decomp C plus hand C on the decomp's structs, the renderers' as their readable build, `fast_render=3`, the Vita's default since D124); counting all 37 overlays as they are on the disc, **97.1%** (`tools/native_audit.py`). Before D124 the renderers' hand C kept the PS1 machine model (the GTE register file, the scratchpad, `HPTR`): 63.5% and 94.1% (D96, §5 stage 3). D76 measured 50.6% and 91.7%.
- **Most of the time still goes to the emulation layers, but less of it.** On the K route with every layer on the game thread (run X4, D98, §3.5) a vblank takes **9.73 ms of CPU, and 64% of it is the measured emulation layers** (54–70% by part). Before: N1 (D80) 10.95 ms and 69%; stage 0 (K1, §3.4) 78–84% with its estimate of the software GTE. Since D80 the renderers' GTE is native maths, so 64% is close to the whole emulation share (*estimate*). The largest layer is the PS1 GPU → vitaGL translation (3.44 ms, 35%), then the renderers (2.86 ms: the game's own code, plus building the records) and the OT walk (2.09 ms).
- **60 fps** (optional and experimental in v1.1.0) **holds in play on hardware** (D81; X7 in Toasty: 0.1 late vblanks per 300). It misses in the Stone Hill attract demo's flight, where the render thread is the wall (15.4 ms a picture), and at loads. The in-between frame still re-runs the game's draw path on the game thread: 3.8–5.2 ms per tick in play (X7, §5 stage 7).
- **"Fully native"** means the game's own code (the decomp's logic, fixed-point maths and tick) is unchanged and runs on the Vita with nothing PS1-shaped in between:
  - no MIPS register model;
  - no GTE register file;
  - no PS1 primitives or ordering-table walk;
  - no 2 MB arena or 24-bit links;
  - no sector-addressed CD;
  - no software SPU register model;
  - no vblank emulation driving the logic.

  Faithfulness to the original's behaviour stays the hard constraint at every stage.
- **Done:**
  - stage 0, measuring (§3.4);
  - stage 0b, its two fixes: atlas decodes on the CPU (D79) and the XA read-ahead after a jump back (D78);
  - stage 1, native GTE maths (D80, D81; the last three renderers D98);
  - stage 3, readable C for all the mips2c code (D83, D87, D94, D96);
  - stage 5, native audio (D84, D88);
  - stage 6, native file I/O (D93);
  - stage 2: native draw records from every renderer (D82–D95), `native_draw=1` by default on the Vita; the direct vertices (D99) with the walk's box as SIMD (D105), a net gain on hardware with identical pictures, `nd_direct=1` by default on the Vita since D105; the clears' cheaper path behind `gpu_clear` (D111): on hardware the K route −30% CPU per vblank, and at 60 fps the render thread no longer the wall in the attract demo's flight, with identical pictures; `gpu_clear=2` by default on the Vita since D111;
  - stage 4, native memory: the Vita's moved arena on the host (D97); every decode site named in `psx_mem.h`, and a native build with no mips2c units that runs at any base, below 2 GB too, with the bit-31 tags in bit 0 (D100); the data labels as ordinary globals, the game's memory one heap block (overlay space), the scratchpad its own array, save states format 2, and native memory the Vita's default since a hardware pair (D106);
  - stage 7's steps 7a and 7b: the host audits of the in-between frame, and the port's own `VSync` over one clock module (D104);
  - stage 7's step 7c: frames carry their due vblank, and with `present_due=1` the Vita's render thread shows them at their display vblank and the game thread never waits for a present (D112). Its hardware pair (7C1–7C4) took the Stone Hill flight's late vblanks to 0, and `present_due=1` is the Vita's default since.
  - stage 7's step 7d: each renderer is compiled once more as a presenter build on its own state, by name (`NAME_hand_pres.c`, `render_ctx_names.h`), and `--render-ctx-check` runs it beside every renderer call with the game's memory read-only: 0 differences and 0 writes on every scenario (D123). The game's builds compile to the same code.
  - stage 7's step 7d2: every B2 renderer function has a readable build behind `fast_render=3` (`NAME_hand_rc.c`, D124): the control registers set as before, and no GTE data register or command, PS1 scratchpad, spill area or hand stack, which the compiler enforces. The presenter build takes the same code. On hardware (7D2a/7D2b) the pictures are identical and the renderers 5% lighter; `fast_render=3` is the Vita's default since 2026-11-09 (the owner's decision). Since D127 it is the only build of the renderers, and the host's default too: the register-file builds (`fast_render` 1 and 2, `ngte_fx`, the `_ngte`, `_ndr` and `_rc` files) left both builds, and the native build's spill-area stores and hand stack with them.
  - stage 7's step 7e: the presenter's draw is the decomp's own draw code compiled a second time (`draw.c` and the files its play branch calls, onto a snapshot: `src/game/pres/`, `cmake/presenter.cmake`), behind `interp_mode=2`. On the host its in-between frame in play is D30's pass's record for record on every scenario, and with it on every file is D30's (D125). D30's pass keeps the other gamestates.
  - stage 7's step 7f: the presenter on its own core (D126). Its output calls and its OT walk are its own (a second `PsxGpuWalk`); on the Vita it draws the in-between frame on a thread of its own (`pres_thread=1`, core 1) beside the logic draw, into a buffer the render thread runs where the game thread placed it in the ring, and the game thread joins it before the next update. The host runs it inline: D30's frames record for record, every file D30's. Vita3K `realtime=0`: D30's shots and captures. On hardware (7F1–7F5, the owner's yes) the game thread's busy % at 60 fps fell from 41.3% to 24.9% in Toasty (30 fps: 21.1%) and from 61.0% to 38.0% in Dark Hollow, the presenter never late; two windows after Toasty's game over were worse in 7F3 (a repeat would tell).
- stage 8, all of it (D113–D118; on hardware 8H1–8H4): libpad, libgpu's API, libgte with the GTE register file, and the OT walk with its splits, vertex buffer and GPU state are ours (`src/platform/common/libpad.c`, `libgpu.c`, `gpu_walk.c`; `src/game/libgte.c`, `gte_regs.c`). The Vita ELF links nothing of PsyCross since 8d, and since 8e the native and Vita builds compile no PsyCross code: only its `include/psx` headers (the owner's choice, A). PsyCross's GTE and walk are the reference the host's checks compare with (`SPYRO_PSYCROSS_REF`, `--walk-check`).
- **What I recommend next** (updated by D104, D105 and D106, 2026-11-05, D111, D112 and D113, 2026-11-06, D117, D118, D119 and D123, 2026-11-07, D125, 2026-11-08):
  1. **7c's pictures on hardware: done** (D119, 7P1/7P2, 2026-11-07): the K route at 60 fps with shots at V and V+1, main's VPK with `present_due` 0 against 1. No picture fault; the HD shots differ only by the vblank each mode presents. Judder is the owner's to judge on the screen.
  2. **7d2, 7e, 7f, 7g** (the owner's decision of 2026-11-07, D119, over D104's "wait"; 7d2 and 7e in parallel, the owner's decision of 2026-11-08, D124). D119 refreshed the plan for today's code ("The plan refreshed (D119)" in §5 stage 7):
     - 7d is done (D123): the re-entrant renderers are a second build of each renderer, with the presenter's context mapped by name;
     - 7d2 is done (D124) behind `fast_render=3`: the renderers' hand C as readable C by D87's rule (63.5% → 81.9% of the executable, measured). The hardware pair 7D2a/7D2b ran: identical pictures, the renderers 5% lighter; `fast_render=3` is the Vita's default (the owner, 2026-11-09), and since D127 the only build (the register-file builds removed from both builds, the host's default 3);
     - 7e is done (D125): the presenter is the decomp's own draw code compiled a second time against a snapshot, behind `interp_mode=2`;
     - 7f is done (D126): on the Vita the presenter draws on a thread of its own beside the logic draw; on hardware (7F1–7F5) the game thread's busy % at 60 fps fell 41.3% → 24.9% in Toasty and 61.0% → 38.0% in Dark Hollow, the presenter never late; the owner decides the default;
     - 7g, part 1 is done (D128): the presenter holds every gamestate's draw, the overlays' included, and draws the in-between frame in each and the attract demos' 16:9 frame, D30's pass's record for record on the host; part 2 retires D30's pass and `g_psx_vram_frozen` (the 2D narrowing is in the walk since D122).
  3. **Stage 8 is done** (D113–D118): the native and Vita builds compile no PsyCross code, only its headers (the owner's choice, A), and 8H1–8H4 matched main on hardware. What it left: the scripted `frames=` exit at 60 fps crashes the render thread mid-present (D118, main's too; a follow-up). Stage 2 is done: with the clears gone (D111) what is left of the PS1 GPU → vitaGL layer is about 1.0 ms per vblank, mostly `GR_DrawTriangles` (0.81 on the K route), and that is our code; stage 8 isn't a CPU stage.

---

## 1. Categories

What the code actually is, after reading `cmake/game.cmake`, `cmake/mips2c.cmake`, `cmake/psycross.cmake`, `src/game/`, `src/platform/` and the decomp's `asm/`.

| | Category | What's in it | Where the boundary is, and why |
|---|---|---|---|
| **A** | Decomp C, compiled natively | `third_party/spyro-1/src/*.c`, `src/gamestates/*.c`, `src/overlays/*.c` (×37 overlay builds), `src/compat` shims | Matched source, compiled by GCC for ARM like any C. It still *calls* PsyQ (category D) and builds PS1 primitives, and it assumes the PS1 memory layout (`docs/porting-plan.md` §4.4). That cost is counted where it lands (D, E), not here. |
| **B1** | Hand-written C, readable | `src/game/{camera,pete,pete_dispatch,pete_state,pete_transition}_hand.c`, `memory.c`, `order_table.c`, `rand_psyq.c`, `system_time.c` | Written from the asm with locals and the decomp's structs. No register model, no GTE register file, no KUSEG masking. 1 to 3 scratchpad references each, for state the asm leaves there. |
| **B2** | Hand-written C over the PS1 machine model | `src/game/{env,moby,pete_render,sky}_hand.c` (the six D72 renderers) | Readable control flow, but it keeps the machine's state as the asm left it: the global GTE register file (`MTC2/MFC2/CTC2`, 115 to 252 references per file), the scratchpad, `HPTR`/`MIPS_KUSEG` for pointers read from RAM, and the spill area. It's split from B1 because removing that state is a stage of its own. |
| **C** | Machine-translated C | `gen/mips2c/m2c_*.c` (`tools/mips2c`), and the runtime `src/game/mips2c_rt.{c,h}` | Every MIPS register is a `u32` local, `$sp` is an emulated stack in the arena, every load and store goes through `MIPS_KUSEG`, and computed jumps dispatch on PS1 code addresses. **Cf**: the six render units linked only as `fast_render=0`'s fallback (D72). |
| **D** | PsyQ emulated by PsyCross | `third_party/PsyCross/src/{psx/LIBGPU,LIBGTE,LIBETC,LIBPAD,LIBAPI,LIBSN,INLINE_C,ABS}.C`, `gte/PsyX_GTE.cpp`, `gpu/PsyX_GPU.cpp` (the OT walk and the primitive parser), plus our `patches/PsyCross` | A reimplementation of Sony's libraries at their API: PS1 primitives in, `GrVertex` triangles out. Our own libcd, libspu and libmcrd replace PsyCross's (E). |
| **E** | PS1 hardware and libraries emulated in our own code | `src/game/gte_inline.h`, `gte_ops.c` (the GTE); `src/platform/common/{libspu,xa,libcd,cdimage,libmcrd,arena,walk,vcache}.c`; `src/platform/vita/{gpu_gl,gpu,video,vblank,xa_ahead}.c`; `linux32/gpu_soft.c` (gpu=0) | Code whose job is to behave like a PS1 part: the GTE, the SPU, the CD drive, the memory card's API, PS1 RAM, the vblank clock, and the PS1 GPU's VRAM, texture pages, CLUTs and blend modes. `gpu_gl.c` is drawn here even though it calls vitaGL natively, because what it does is interpret the PS1 GPU's command stream. |
| **F** | Native Vita platform code | `src/platform/vita/{main,pad,audio,log,menu*,picture,present_vita,prof*,savestate_vita,bootscreen,bench,watchdog,gpu_thread,parse,mixer}.c`, `common/menu_*` | Threads, input, audio out, logs, the menu, save files, the watchdog. The thread wrappers (`gpu_thread.c`, `parse.c`, `mixer.c`) are F; the work they run is D or E. |
| **G** | Port features over the game | `src/game/{present,lod,cheats_port,savestate,where_port,port}.c` | Platform-neutral C written for the port: widescreen and 60 fps, draw distance, cheats, save states. |
| **X** | Third-party native libraries | SDL2 (857 KB of code), vitaGL (300 KB), newlib, libstdc++, vitashark, SCE stubs | Native, but not ours. They're shown so the ELF total adds up. |
| **T** | Host and test only | `src/game/{oracle_replay,trace,hand_index}.c`, `src/platform/linux32/` (except `gpu_soft.c` and `png.c`, which the Vita links) | Not in the Vita ELF. |

The original's *code* can only land in A–E: hardware (the GTE, GPU, SPU) has no instructions in `SCUS_942.28`. So table 2.1 shows what replaced the original's code. Tables 2.2 and 2.3 show what we ship, emulation layers included.

## 2. Static measures (`tools/native_audit.py`)

```bash
docker run --rm -v "$PWD:/work" -w /work vitasdk/vitasdk:latest tools/ci/build_vpk.sh build/vita-audit
docker run --rm -v "$PWD:/work" -w /work vitasdk/vitasdk:latest tools/native_audit_map.sh build/vita-audit
python3 tools/native_audit.py --build build/vita-audit            # --detail: every row; --json FILE
```

How it counts:
- **Instructions** are lines with splat's `/* off addr word */` prefix, not counting `.word`.
- **Per-object sizes** come from `tools/layout/psx.map`.
- **Nonmatching functions** are subtracted from their C object and counted by their own `.s` file.
- **`psyq.o` is split by library:** each instruction goes to the library of the named entry point before it. This is approximate where one library's internal functions follow another library's last entry point.
- **ELF bytes** come from the relinked map's `.text` input sections.
- **The six render units** hold both live code and `fast_render=0`'s fallback. mips2c emits one C function per unit, so symbol sizes can't separate the two; the unit's bytes are split by original instructions.
- **Source lines** are code lines, without comments or blank lines.

The run behind the tables below: `logs/native-audit/audit.txt` (gitignored), from the VPK built at `build/vita-audit` on this branch.

### 2.1 The original game's code, by what runs it on the Vita today

| Category | Executable (insns) | % | Executable + 37 overlays (insns) | % |
|---|---:|---:|---:|---:|
| A decomp C | 32,555 | 35.6% | 506,632 | 89.3% |
| B1 hand C, readable | 13,669 | 15.0% | 13,669 | 2.4% |
| B2 hand C over the machine model | 10,971 | 12.0% | 10,971 | 1.9% |
| C mips2c | 17,678 | 19.3% | 19,272 | 3.4% |
| D PsyQ → PsyCross | 8,884 | 9.7% | 8,884 | 1.6% |
| E PsyQ → our libcd, libspu, libmcrd | 7,658 | 8.4% | 7,658 | 1.4% |
| **total** | **91,415** | | **567,086** | |

- **Readable C (A + B1): 50.6% of the executable, 91.7% of the whole game.** The overlays are all matched C, compiled once per level (475k instructions across 37 copies), so the second column mostly measures them.
- **Functions:** 280 decomp C functions in the executable and 211 in the overlays (global symbols in `psx.map`; static ones aren't listed). Asm functions: 19 in B1, 6 in B2, 101 in C (named entry points, `D_` labels counted with their function).
- **What's left in C** (executable): collision 3,866, the remaining renderer code 6,335 (r_cyclorama's helpers 1,761, particles 1,512, r_environment's other functions 1,198, r_moby's culls 784, shadows 636, flame 444), gamestates 8 and 11 (3,133), the cyclorama set-up `func_80050BD0` (1,287), math 1,296, `42CC4.s` 1,050, and 711 in smaller ones. Plus 1,594 for the credits overlay's two functions.
- **The PsyQ split:**

  | Runs on | Library | Insns |
  |---|---|---:|
  | PsyCross | libgpu | 3,625 |
  | PsyCross | libpad | 2,784 |
  | PsyCross | libc | 1,169 |
  | PsyCross | libetc | 948 |
  | PsyCross | libgte | 228 |
  | PsyCross | crt | 96 |
  | PsyCross | libapi | 34 |
  | ours | libmcrd | 2,941 |
  | ours | libcd | 2,620 |
  | ours | libspu | 2,097 |

### 2.2 Source lines that build the Vita game

| Category | Code lines | % |
|---|---:|---:|
| A decomp C (+ `src/compat`) | 45,649 | 44.1% |
| B1 hand C, readable | 4,028 | 3.9% |
| B2 hand C over the machine model | 4,871 | 4.7% |
| C mips2c (generated, 20 units + runtime) | 32,563 | 31.5% |
| D PsyCross (compiled files) | 4,234 | 4.1% |
| E our emulation | 6,383 | 6.2% |
| F native Vita | 3,761 | 3.6% |
| G port features | 1,977 | 1.9% |
| **total** | **103,466** | |

There are also 2,378 lines of host and test code (T). The generated C is 31.5% of the source and covers 31.3% of the executable's instructions (C, plus the six renderers it still carries as a fallback): about 1.1 lines per MIPS instruction, against 0.29 for B1's hand C and 0.44 for B2's.

### 2.3 Vita ELF code (`.text`, Thumb-2)

| Category | Bytes | % of all | % without X |
|---|---:|---:|---:|
| A decomp C | 1,485,328 | 41.5% | 68.6% |
| B1 hand C, readable | 29,636 | 0.8% | 1.4% |
| B2 hand C over the machine model | 41,152 | 1.1% | 1.9% |
| C mips2c, live | 310,611 | 8.7% | 14.3% |
| Cf mips2c, fallback only | 142,970 | 4.0% | 6.6% |
| D PsyCross | 42,496 | 1.2% | 2.0% |
| E our emulation | 63,872 | 1.8% | 2.9% |
| F native Vita | 33,388 | 0.9% | 1.5% |
| G port features | 17,256 | 0.5% | 0.8% |
| X third-party libraries | 1,416,026 | 39.5% | |
| **total** | **3,582,735** | | **2,166,709** |

- The 37 overlay copies are 1.40 MB of A.
- mips2c's register-model code takes about 5 times the bytes of hand C per original instruction: 453 KB for ~30k instructions (C, plus the six renderers' fallback), against 71 KB for B1 and B2's ~25k.
- SDL2 (857 KB) is used only for the pad and the audio queue, plus the gpu=0 picture.

## 3. Run time

### 3.1 Hardware, from existing logs (`tools/vita/native_share.py`)

Each 300-vblank window's work on every thread, in ms per vblank:
- the `time`, `draw`, `render`, `parse`, `mixer`, `xa` and `atlas` lines (`src/platform/vita/prof.c`, `prof_draw.c`, `gpu_thread.c`, `parse.c`, `mixer.c`);
- waits left out (the game thread's join for the parse thread, the vblank wait).

**Main, Toasty, steady 30 fps** (`logs/hw-main-2a0691c`, windows 6900–10200; render thread, parse thread, `fast_render=1`):

| Layer | ms per vblank | % |
|---|---:|---:|
| game logic (`GamestateUpdate`: A + B1 + C) | 0.34 | 3% |
| renderers (B2 + inline GTE; mips2c culls, particles, shadows, flame) | 1.62 | 15% |
| other game-thread work (draw.c building primitives, HUD, PsyCross calls) | 0.57 | 5% |
| PsyCross OT walk (parse thread, + the copy) | 1.16 | 11% |
| render thread: PS1 GPU → vitaGL, atlas decode, vitaGL (includes its waits) | 5.46 | 51% |
| SPU model + mixer thread | 0.97 | 9% |
| XA read-ahead (CD reads) | 0.56 | 5% |
| **sum** | **10.68** | |

- **The render thread's busy time is an upper bound on its CPU.** It counts the commands it runs, `vglSwapBuffers` and `vglWaitVblankStart` among them (`gpu_gl.c:2160`, `2204`). At 60 fps it reads 16.0–16.3 ms in 10 of the 15 windows (`--fps 60 --all`): the thread is pinned on the vblank, not computing.
- **The logs from before the render thread give its CPU without the waits.** In `logs/hw-4f26bc7-user` and `logs/hw-26f3138-bench` (GL on the game thread), `raster` was 3.86 and 4.72 ms per vblank at 30 fps. That was before D70's leaner vertex and D72.
- **K1 measures that again on today's code.** Until it runs, read the 51% as "35–51%".
- **Before D72** (`logs/hw-4f26bc7-user` and `hw-26f3138-bench`, 30 fps, everything on the game thread, so CPU only):

  | Layer | % |
  |---|---:|
  | logic | 3% |
  | renderers (mips2c + PsyCross GTE) | 24–29% |
  | OT walk | 18–20% |
  | raster | 28–29% |
  | SPU and CD | 15–16% |

  Total 13.9–16.0 ms per vblank.

**The same windows by category:**
- **Game-own work** (the logic, the renderer bodies without their GTE, and draw.c's other work): *estimate* **~1.6 ms, 15–17%**. The renderers' GTE split is 60/40, from the host profile below.
- **Emulation layers:**
  - the GTE: ~1.0 ms *estimate*;
  - the OT walk: 1.16 ms;
  - the PS1 GPU translation and vitaGL: 5.46 ms, an upper bound;
  - SPU and XA: 0.97 ms;
  - CD: 0.56 ms.

  Together **7.5–9.1 ms, 83–85%**: the low end takes the render thread at the 3.86 ms its work cost on the game thread before D68, the high end at its busy time. That includes vitaGL's own driver work. A native renderer would still pay for that driver work, but for far fewer, larger draws.

**At 60 fps** (the same log, 15 windows): renderers 3.00, OT walk 1.94 and render thread 18.6 ms per vblank (saturated), so the game's own share is lower still.

**The GPU** (D74, `logs/hw-pr49-i1`, Toasty, 60 fps): the GPU's scene took 14.65–20.77 ms with PsyCross's split draws, against 6.10–7.30 ms joined (#49), and half the pixels saved 40–50% of the joined scene. So after #49, what's left of GPU time is mostly pixels: D75's lever, and resolution. The emulation-shaped part, many small draws, is what #49 removes, as far as the OT order allows.

### 3.2 Host profile (gprof under qemu-i386: relative only)

```bash
tools/tc cmake -S . -B build/linux32-prof -G Ninja -DSPYRO_HOST=linux32 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/linux32.toolchain.cmake -DCMAKE_C_FLAGS=-pg -DCMAKE_CXX_FLAGS=-pg -DCMAKE_EXE_LINKER_FLAGS=-pg
tools/tc cmake --build build/linux32-prof
tools/tc tools/native_audit_map.sh build/linux32-prof linux32
# in logs/X: spyro --disc /work/disc/files --frames 12000 --wav audio.wav --widescreen, then
tools/tc i686-linux-gnu-gprof -b -p build/linux32-prof/src/platform/linux32/spyro logs/X/gmon.out > logs/X/gprof-flat.txt
python3 tools/native_audit.py --prof logs/X/gprof-flat.txt build/linux32-prof
```

**The run:** the attract demos (12,000 vblanks, 16:9, with audio; `logs/native-audit/prof-demo`, gitignored), 48.3 s sampled.

**The host-only part:** 70% of it is the host's software rasterizer (`gpu_soft.c`), which the Vita doesn't run at `gpu=1`. Without it:

| Group | % |
|---|---:|
| PS1 primitives: PsyCross OT walk and its conversion to triangles | 43% |
| GTE in software (`m2c_gte_*`) | 19% |
| libc, PLT and start-up (unresolved) | 13% |
| hand renderers' own code (B2) | 11% |
| SPU and XA | 9% |
| mips2c | 2.6% |
| decomp C | 0.9% |
| port features | 0.6% |
| readable hand C | 0.6% |

- **Where the GTE time comes from:** of the 2.54 s in GTE commands, 2.45 s is called from the hand renderers (the call graph: env 0.84 + 0.24 + 0.15, sky 0.36, mobys 0.35 + 0.23 + 0.16, Spyro 0.11), against 1.63 s in the renderers' own code. **So the GTE is ~60% of the renderers' CPU.** That ratio is what section 3.1 uses for its estimate.
- **What qemu exaggerates:** the PsyCross OT walk's float work (`ApplyVertexPGXP`, 3.35 s, `float` vertex building). The hardware share (11%) is the one to plan with.

### 3.3 What the hardware runs settle (Stage 0)

Three config files in `tools/vita/fps-runs/`. Each one is a whole `ux0:data/spyro/config.txt`.

| Run | Switches | What it settles |
|---|---|---|
| `k1-native-cpu.txt` | 30 fps, `render_thread=0`, `parse_thread=0`, `fast_render=1`, `dynres=0` | Every layer on the game thread, CPU only. The `time` lines then give the OT walk (`ot`) and the PS1 GPU → vitaGL translation (`raster`) without the render thread's waits, on today's code. That's the real size of stage 2's target. |
| `k2-native-cpu-m2c.txt` | K1 with `fast_render=0` | The register-model renderers with PsyCross's GTE, against K1's hand C. The `draw` lines' renderer ms show, on hardware, what removing the machine model has bought so far, and they bound what stage 1 can buy. (G1 and G2 were planned for this and never run.) |
| `k3-native-threads.txt` | the shipped threads at 30 fps, `gpu_meter=1` (D75's meter, no `glFinish`), `dynres=0` | The same route with the threads on. Against K1 it shows how much of the render thread's busy time is waiting. Against main's log it shows what #49 changed, and the meter splits the GPU's time by what each scene draws into. |

**For the owner:**
1. Use a VPK built from main after #48 and #49 (`f534fb1` or later). `dynres` is off by default there since J6; the K files say so anyway.
2. Keep a copy of your own `ux0:data/spyro/config.txt`.
3. Copy one K file over it.
4. Start the game and leave it alone for 3 minutes: the title, the flythrough and both attract demos. Those windows have the same input in every run, so K1–K3 compare window by window.
5. Then start or load a game: 2 minutes in Artisans, then 2 minutes in Toasty.
6. Don't open the options menu, because it rewrites `config.txt`.
7. Quit, and send back `ux0:data/spyro/spyro.log` renamed `k1.log` (then `k2.log`, `k3.log`).
8. Put your own `config.txt` back.

To read them: `python3 tools/vita/native_share.py k1.log --fps 30 --all`.

**Checked in Vita3K** (the VPK from main `f534fb1`, SHA-1 `a97bb2ae…`; virtual vblanks, 5,400 of them): each K file's switches start as asked (`render: GL calls on the game thread`, `parse: OT walk on the game thread`, `fast_render=0: translated renderers`), each run reaches the Stone Hill attract demo, and the six shots of K1, K2 and K3 are identical (`logs/stage0/v3k-k1`–`k3`, gitignored). Vita3K's timings aren't the Vita's, so the numbers come from hardware only.

**One thing a config can't settle:** the render thread's own CPU with the threads on. The watchdog already samples every thread's `runClocks` each second (`watchdog.c`, D73), but logs them only in a hang line. A one-line periodic log of those samples would settle it. That's an engine change, so it isn't made here; it's the first item of stage 0's follow-up.

### 3.4 Stage 0 results: K1–K3 on hardware

**The runs:** the owner ran K1–K3 on the VPK from main `f534fb1` (`logs/hw-f534fb1-k1`–`k3`, gitignored). Each run went: the title, the Stone Hill attract demo, then play in Artisans and Stone Hill. K3 started from a Toasty save, and the owner left Toasty at once.

**How to compare them:**
- The title and attract-demo windows have the same input in every run, so they compare exactly.
- The play windows are the same places, but with different input.
- `tools/vita/native_share.py` and the `time`, `draw`, `render`, `parse`, `mixer`, `xa` and `gpu meter` lines give the figures below, as ms per vblank (means of the windows in brackets).

**K1, every layer on the game thread (CPU only, no waits):**

| Layer | Title (900–3300) | Attract demo (4500–5700) | Artisans play (8100–9000) | Stone Hill play |
|---|---:|---:|---:|---:|
| game logic | 0.01 | 0.56 | 0.44 | 0.44 |
| renderers (hand C + inline GTE) | 1.53 | 4.46 | 2.48 | 1.99 |
| other game-thread work | 0.26 | 0.67 | 0.31 | 0.31 |
| PsyCross OT walk | 1.99 | 2.63 | 2.51 | 2.24 |
| PS1 GPU → vitaGL (`raster`) | 2.86 | 3.85 | 3.81 | 3.60 |
| SPU model + XA reads on the game thread | 0.05 | 0.07 | 0.58 | 0.54 |
| mixer thread (core 1) | 0.82 | 0.99 | 1.03 | 0.99 |
| XA read-ahead (core 1) | 0.56 | 0.61 | 0.00 | 0.00 |
| present | 0.09 | 0.09 | 0.09 | 0.09 |
| **sum** | **8.17** | **13.93** | **11.25** | **10.20** |
| emulation, measured (OT + raster + SPU/XA + mixer + read-ahead) | 77% | 58% | 70% | 72% |
| plus the GTE at 60% of the renderers (*estimate*, §3.2) | 88% | 78% | 84% | 84% |

- **The game thread is 55–74% busy in play** at 30 fps: 9.2–12.3 ms of a 16.7 ms vblank. Of that, the OT walk and `raster` alone are 5.8–6.5 ms.
- **`raster` is nearly flat:** 2.9 ms per vblank on the title screen, 3.6–3.9 in play. Most of the PS1 GPU → vitaGL cost is per draw and per frame, not per triangle. That's stage 2's target.

**K3 against K1: how much of the render thread is waiting.** The render thread's busy time against K1's `raster`, the same work on the game thread:

| | Title | Demo | Artisans | Stone Hill |
|---|---:|---:|---:|---:|
| render thread busy (K3) | 4.33 | 5.84 | 5.48 | 5.31 |
| `raster` (K1) | 2.86 | 3.85 | 3.81 | 3.60 |
| CPU share of busy | 66% | 66% | 70% | 68% |

So about a third of the render thread's busy time is `vglSwapBuffers` and `vglWaitVblankStart`. Section 3.1's 35–51% for main's Toasty log is about 35%.

**The OT walk:** 1.0–1.5 ms per vblank on the parse thread (K3), against 2.0–2.6 ms on the game thread (K1) for the same frames. Walking the copy is cheaper than the in-place walk; why isn't measured.

**K2 against K1: what D72's hand C bought, on hardware.** This is the first hardware measurement of D72: G1–G3 never ran. The renderers' ms per vblank, `fast_render=0` against `1`, on identical input:

| | Title | Attract demo |
|---|---:|---:|
| all renderers | 2.40 → 1.53 (−36%) | 7.00 → 4.46 (−36%) |
| env | 1.64 → 1.05 | 3.24 → 2.09 (−35%) |
| mobys | 0.34 → 0.20 | 2.20 → 1.31 (−40%) |
| sky | 0.42 → 0.28 | 0.76 → 0.52 (−32%) |
| Spyro | — | 0.58 → 0.35 (−40%) |

- D72 estimated −30 to −45%; it's −36%.
- **What's left for stage 1:** 1.5–4.5 ms per vblank of renderer CPU. The GTE's part of it isn't separable on hardware: K2 swaps the register-model code and the GTE together. Stage 1's estimate stays −0.5 to −1.3 ms per vblank at 30 fps, twice that at 60.

**The GPU (K3, D75's meter, ms of GPU per picture):**

| | Title (2100–3300) | Attract demo | Artisans play (8700–9300) | Stone Hill play (10200–10500) |
|---|---:|---:|---:|---:|
| the game's scene ("game's frames" + "other") | 5.4 | 7.2–10.3 | 5.1–7.2 | 5.8–6.3 |
| texture decodes (atlas passes) | 0.0 | 0.3–4.1 | 1.4–9.1 | 2.1–6.6 |
| the present | 0.85 | 0.84 | 0.85 | 0.85 |
| longest gap between two pictures | 5.7 | 17–29 | 18–29 | 27–28 |

- **The meter's split between "game's frames" and "other" looks off:** it counts 0.2 game-frame scenes a picture where there's one. So the two are added up here.
- **Texture decodes are tagged by their surface, the atlas, so that row is solid:**
  - In play, 20–129 decode passes per 300 vblanks (`gpu N: ... atlas decode passes`; 104 and 129 in Artisans), with **no VRAM upload at all.** The atlas is filling in as new texture/CLUT pairs come into view (489–561 blocks a window in Artisans).
  - **Each pass costs about 10.5 ms of GPU** (the decode row × pictures ÷ passes: 10.4–10.6 in all four play windows): `atlas_decode()` (`gpu_gl.c:1220`) binds the FBO of the 2048 × 2048 RGBA8 atlas and sets a full viewport. On the SGX's tiles that is a scene over the whole 16 MB surface, loaded and stored, to write a few 32 × 32 blocks.
  - The windows with decodes are the ones with 17–29 ms gaps between pictures; the title's, with none, stay at 5.7 ms.

  This is the biggest single GPU cost in play, and it isn't the game's own work.
- **The fix is the native way:**
  - Decode the blocks on the CPU into the atlas texture's memory (CLUT lookup, STP alpha, the same rules as the decode shader). That's about 100 blocks a second, a few hundred microseconds on core 0 or 1, *estimate*.
  - The draws already queued don't read the new blocks, so there's no hazard.
  - Check it with Vita3K window captures, pixel-identical.

**The XA read-ahead stops after the first level load.**
- **What the logs show:** in all three runs, the read-ahead (D64) serves every music sector until the first level load: Artisans after the intro in K1 and K2, Toasty → Artisans in K3. From then on it serves none (`xa N: 0 music sectors from the read-ahead, 605–750 read on the game thread; read-ahead 0 windows`). That costs the game thread 0.5–0.6 ms per vblank (the `spu` part).
- **Likely cause, from reading `xa_ahead.c`'s `want()`:** a window is reused only when it is free, or ready and wholly *behind* the stream's head. When the music jumps back to a lower LBA (a new track earlier on the disc), all four ready windows are ahead of the head, so `want()` never finds a slot, and no window is asked for again.
- main's Toasty log (`hw-main-2a0691c`) never changed level, so it never showed this.
- **The fix:** also reuse a ready window that is ahead of the stream by more than the read-ahead's reach. Check it with the host-identical WAV and the `xa` lines.

### 3.5 After stages 1, 2, 5 and 6 (D98)

**Already measured, from existing logs.** Stage 0's K1–K3 were the owner's own route; since then the K route (`tools/vita/hw-routes/k-route.pad`) has run unattended with K1's switches (every layer on the game thread, 30 fps, virtual vblanks) four times: N1 and N2 (D80, main after D79, `fast_render` 1 and 2, before records and the native mix) and W1 and W2 (D94: main `3bfe4f7` and its stage 3 branch, `fast_render=2`, `native_draw=1`, `mixer=3`, before D92, D93's `cd_native` and D95). `tools/vita/native_share.py`, means of the windows in each part (`logs/d98/hw-existing.txt`, from `logs/hw-ngte-n1`, `-n2` of the native-gte worktree and `logs/hw-w1`, `-w2` of stage3-rest's; gitignored), ms per vblank:

| Part (windows) | Run | logic | renderers | draw/other | OT walk | PS1 GPU → vitaGL | SPU + mixer | XA reads | present | sum | emulation, measured |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| title (9) | N1 | 0.01 | 1.52 | 0.31 | 2.00 | 2.89 | 0.87 | 0.59 | 0.21 | 8.40 | 76% |
| | W1 | 0.01 | 1.71 | 0.33 | 1.64 | 2.89 | 0.24 | 0.56 | 0.22 | 7.60 | 70% |
| attract demo (5) | N1 | 0.54 | 4.44 | 0.62 | 2.59 | 3.91 | 1.05 | 0.62 | 0.09 | 13.86 | 59% |
| | W1 | 0.34 | 4.95 | 0.67 | 2.29 | 3.90 | 0.33 | 0.58 | 0.10 | 13.17 | 54% |
| Stone Hill (9) | N1 | 0.32 | 2.91 | 0.41 | 2.72 | 3.51 | 0.84 | 0.58 | 0.38 | 11.66 | 66% |
| | W1 | 0.20 | 3.19 | 0.42 | 2.36 | 3.51 | 0.23 | 0.57 | 0.39 | 10.86 | 61% |
| Dark Hollow (11) | N1 | 0.38 | 2.98 | 0.47 | 3.51 | 4.35 | 0.99 | 0.59 | 0.41 | 13.67 | 69% |
| | W1 | 0.22 | 3.31 | 0.46 | 3.12 | 4.34 | 0.31 | 0.58 | 0.46 | 12.82 | 65% |
| the route (51) | N1 | 0.26 | 2.46 | 0.40 | 2.64 | 3.50 | 0.90 | 0.49 | 0.31 | 10.95 | 69% |
| | N2 | 0.26 | 2.09 | 0.39 | 2.64 | 3.50 | 0.90 | 0.49 | 0.31 | 10.57 | 71% |
| | W1 | 0.18 | 2.72 | 0.42 | 2.28 | 3.50 | 0.26 | 0.47 | 0.32 | 10.16 | 64% |
| | W2 | 0.16 | 2.93 | 0.21 | 2.29 | 3.49 | 0.26 | 0.46 | 0.30 | 10.11 | 64% |

"Emulation, measured" is stage 0's measured part (§3.4): the OT walk, the PS1 GPU → vitaGL translation, the SPU and mixer, and the XA reads, of the sum.
- **Between N1 and W1** the route's sum fell by 0.8 ms per vblank (−7%), and the measured emulation share from 69% to 64%:
  - the native mix (stage 5, D84, `mixer=3` by default since D88): SPU + mixer 0.90 → 0.26;
  - the records (stage 2, `native_draw=1` by default since D91): the OT walk 2.64 → 2.28, and the renderers, which now build the records, 2.46 → 2.72 (N2's native GTE maths, D80, had taken them to 2.09);
  - the PS1 GPU → vitaGL translation is unchanged at 3.50 (CPU only in these runs, `render_thread=0`). It is now the largest layer, a third of the sum: stage 2's next target.
- **The GTE:** stage 0 added an *estimate* of the software GTE at 60% of the renderers' time (§3.2), which gave its 78–84%. Since D80 the six main renderers' GTE commands are native maths on C values, part of the game's own renderer code. What stayed on the GTE register file is D95's shadows, flame and particles, the renderers' cold paths and the mips2c helpers. On the K route the shadows and flame (`fx`) take 0.03 ms per vblank and the particles 0.016 (W1's `draw` lines), 1.6% of the renderers. So the register-file GTE is now a few percent of the renderers at most (*estimate*), and the measured 64% is close to the whole emulation share on today's code, against stage 0's 78–84%.
- XA reads stay at 0.5–0.6 ms: these runs predate D93's native disc reads (`cd_native=1` by default since D93), which move them to a worker thread's single reads.

**Runs X1–X7 on this branch** (2026-11-04, D98; `tools/vita/fps-runs/README.md`, "Runs X1–X7"; VPK D98 merged with main `e3fbfd3`, D96 and D97). The K route with K1's switches, the same parts:

| Part (windows) | Run | logic | renderers | draw/other | OT walk | PS1 GPU → vitaGL | SPU + mixer | XA reads | present | sum | emulation, measured |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| title (9) | X4 | 0.01 | 1.69 | 0.32 | 1.52 | 2.89 | 0.22 | 0.51 | 0.22 | 7.37 | 70% |
| attract demo (5) | X4 | 0.30 | 5.39 | 0.05 | 2.07 | 3.82 | 0.30 | 0.55 | 0.10 | 12.57 | 54% |
| Stone Hill (9) | X4 | 0.16 | 3.37 | 0.12 | 2.15 | 3.43 | 0.21 | 0.53 | 0.35 | 10.32 | 61% |
| Dark Hollow (11) | X4 | 0.19 | 3.61 | 0.12 | 2.84 | 4.22 | 0.29 | 0.54 | 0.42 | 12.22 | 64% |
| the route (51) | X4 (`fast_render=2`, the default) | 0.15 | 2.86 | 0.20 | 2.09 | 3.44 | 0.24 | 0.44 | 0.31 | 9.73 | 64% |
| | X5 (`ngte_fx=1`) | 0.15 | 2.87 | 0.20 | 2.09 | 3.44 | 0.24 | 0.44 | 0.30 | 9.74 | 64% |
| | X1 (`fast_render=1`) | 0.15 | 2.59 | 0.21 | 2.31 | 3.44 | 0.24 | 0.43 | 0.30 | 9.67 | 66% |
| | X2 (`fast_render=0`) | 0.16 | 4.17 | 0.18 | 2.31 | 3.43 | 0.23 | 0.43 | 0.31 | 11.22 | 57% |

- **Today's split** (X4): the measured emulation layers are **64%** of the CPU per vblank over the route, 54–70% by part, and the sum 9.73 ms (W1: 10.16; N1: 10.95). With the GTE now mostly native maths, that's close to the whole emulation share (*estimate*; stage 0: 78–84% with its GTE estimate, 58–77% measured on its own route).
- **The largest layer is the PS1 GPU → vitaGL translation,** 3.44 ms per vblank, 35% of the sum, then the renderers (2.86, the game's own code and the records) and the OT walk (2.09). On the render thread (X3, threads on) its busy time is 4.13–6.51 ms per vblank, of which 65–70% is CPU (X1's game-thread figures for the same frames), as in stage 0.
- **D95 and D98 on hardware** (D98): the shadows and flame (`fx`) and the particles take 0.032 and 0.017 ms per vblank as mips2c units (X2), 0.021 and 0.010 as D95's hand C with records (X4), 0.027 and 0.012 with D98's native maths (X5): D98 is slower for these short loops, so `ngte_fx` stays 0. All four runs give the same 18 pictures and memory card.
- **Toasty** (X6, X7): at 30 fps no late vblanks in play; at 60 fps 0.1 late per 300 (worst 1), the GPU at 6.3 ms a picture (max 7.7), the render thread 10.6 ms a picture, as D81's O2.

**Runs Y1 and Y2** (2026-11-05, D99; VPK D99 at `937d6e6`): X4's switches with `nd_direct` 0 and 1, the stage 2 direct vertices. Both runs had D99's `gpu parts` timers on, which cost the GL layer 0.17 ms per vblank against X4 (now behind `gpu_parts=1`). The route (51 windows):

| Part (windows) | Run | logic | renderers | draw/other | OT walk | PS1 GPU → vitaGL | SPU + mixer | XA reads | present | sum | emulation, measured |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| the route (51) | Y1 (`nd_direct=0`) | 0.15 | 2.86 | 0.20 | 2.10 | 3.61 | 0.24 | 0.44 | 0.31 | 9.92 | 64% |
| | Y2 (`nd_direct=1`) | 0.15 | 2.85 | 0.20 | 2.08 | 3.58 | 0.23 | 0.44 | 0.30 | 9.82 | 64% |

- **Before and after: 9.92 → 9.82 ms per vblank** with every layer on the game thread.
  - The GL side's vertex buffers (inside the OT walk's layer here) fell 1.11 → 0.65 ms; the walk grew by as much (0.91 → 1.35).
  - What stays in the PS1 GPU → vitaGL layer is `GR_DrawTriangles`: per vblank, the draws' work before their batch 0.15 ms, the target logs' copies 0.17, the batches' GL calls 0.49, and inside `sceGxmBeginScene`/`EndScene` 0.13 (Y2's `gpu parts`). That is 0.9 of its 3.6 ms; the rest of `GR_DrawTriangles`' time is in none of these parts, and the next thing to measure.
- **At 60 fps** (Y3, Y4, the shipped threads): the render thread 0.75 ms per vblank lighter, the parse thread 0.72 heavier, late vblanks fewer (D99).

**Runs Y5–Y8** (2026-11-05, D105; this PR's VPK): Y1–Y4 again with the walk's box as ARMv6 SIMD, and a second `gpu parts` line with each entry point's whole time. Means of the route's 50 windows without the last, where the app quits (D105 says why):

| Part | Run | OT walk | PS1 GPU → vitaGL | sum | the walk itself |
|---|---|---:|---:|---:|---:|
| the route (50) | Y1 (`nd_direct=0`, D99) | 2.08 | 3.60 | 9.84 | 0.91 |
| | Y2 (`=1`, D99's box) | 2.06 | 3.56 | 9.74 | 1.35 |
| | Y5 (`=0`) | 2.11 | 3.54 | 9.81 | 0.93 |
| | Y6 (`=1`, D105's box) | 1.77 | 3.50 | 9.39 | 1.07 |

- **Before and after: 9.81 → 9.39 ms per vblank** with every layer on the game thread. The walk itself (the OT walk less the GL side's vertex buffers) grows 0.14 ms where D99's box grew it 0.44.
- **What the PS1 GPU → vitaGL layer is** (Y5, prof.c's `raster` 3.66 ms per vblank from vblank 900): `GR_ClearVRAM` **2.70**, `GR_DrawTriangles` 0.85 (the batches' GL calls in it 0.36, the work before them 0.15, the logs 0.19), the VRAM reads and copies 0.01. The clears are the walk's fill packets turned into `ClearImage`: a per-pixel wrapped loop over the software VRAM and a `glClear` per target. On the render thread at 60 fps (Y7, Y8) they are 5.7 ms per vblank against the draws' 1.5. That is the layer's next target.
- **At 60 fps** (Y7, Y8): the render thread 0.83 ms per vblank lighter, the parse thread 0.22 heavier, the game thread's wait for the walk 0.10 longer with its busy share unchanged, late vblanks fewer (D105).

**Runs Y9–Y12** (2026-11-06, D111; VPK `logs/d111-vpk-2.vpk`, the tree of main `c088cc0`): Y6 and Y8 again with `gpu_clear` 0 and 2, the clears. All 18 pictures and the card identical (also against Y6). The K route with every layer on the game thread (50 windows):

| | `gpu_clear` | renderers | OT walk | PS1 GPU → vitaGL | sum | of the layer: `GR_ClearVRAM` (`s_vram`, log base, `glClear`) |
|---|---|---:|---:|---:|---:|---:|
| Y9 | 0 | 2.84 | 1.82 | 3.59 | 9.54 | 2.77 (1.07, 1.65, 0.03) |
| Y10 | 2 | 2.73 | 1.75 | **1.01** | **6.70** | 0.03 (0, 0, 0.02) |

- **The clears were two CPU loops** (99% on the Vita): the software VRAM, and the target log's base. With `gpu_clear=2` neither runs, and the sum falls 30%. What is left of the layer is `GR_DrawTriangles` (0.81).
- **At 60 fps** (Y11, Y12): the render thread's clears 5.46 → 0.15 ms per vblank, its busy time 10.49 → 8.12; the Stone Hill demo's flight 12.9 → 7.2 ms a picture and 3.0 → 0.7 late vblanks per 300; the loads 102 → 68 late vblanks.

**Runs 8H1–8H4** (2026-11-07, D118, stage 8f): main's VPK (`e640170`) against stage 8's, whose ELF code is byte-identical to main's. On the K route with Y10's settings, all 18 pictures and the card are identical, and every layer is the same within 0.01 ms (51 windows):

| Run | logic | renderers | draw/other | OT walk | render thread | SPU + mixer | XA/CD | present | sum |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8H1 (main) | 0.13 | 2.72 | 0.20 | 1.76 | 1.03 | 0.23 | 0.43 | 0.29 | 6.79 |
| 8H2 (stage 8) | 0.13 | 2.72 | 0.19 | 1.76 | 1.03 | 0.23 | 0.43 | 0.28 | 6.77 |

- **Against Y10's 6.70** (the same settings on main `c088cc0`) the sum is about the same.
- **Toasty at 60 fps** (Y12's settings; 8H3 main, 8H4 stage 8): 0 late vblanks per 300 in Stone Hill and 0.4 in Toasty in both. In Artisans, around the real-time route's game over, 3.5 against 4.3. The loads 132 against 120. In all, about 203 against 205: the runs' own spread.
- **Both runs left a crash dump at exit:** the render thread presents through 7c's `rt_due` while `frames=` exits the process (D118). It is main's too, and only the scripted exit hits it.

**Runs 7P1 and 7P2** (2026-11-07, D119; main's CI VPK `a47a30b`; `tools/vita/fps-runs/README.md`, "Runs 7P1 and 7P2"). The K route at 60 fps with X7's settings and the shipped `gpu_clear=2`, shots at nine places at V and V+1 with `shot_hd=1`, `present_due` 0 (7P1) against 1 (7P2):
- **The pictures:** the PS1 display identical in 16 of 18 shots (6000/6001 are the attract demo at another point of its path: real time). In the HD shots, 8 of 18 differ with the same PS1 display: one vblank apart, because `present_due=0` shoots the next frame presented and `=1` the frame due at V (7P1's 9001 and 16501 are byte-identical to 7P2's 9000 and 16500), or within the box around Spyro and Sparx. No picture fault in any pair, and the card is identical.
- **The timing:** each pair of shots costs 58–64 late vblanks in the window after it. Without those windows, 1.56 against 1.41 late vblanks per 300 from vblank 900, and 7P2's `7c:` part 5 late frames in 14 vblanks (7C2: 8 in 17). The wait for the previous present in Stone Hill: 6.96 ms against 0.
- **No crash dump at exit** in either run, as in 7C2 and 7C4; 8H3 and 8H4 (Toasty, 21,000 vblanks) left one each. D118's exit dump is intermittent, and the session that stops the render thread first (PR #103) is its fix.


---

## 4. What "fully native" means for Spyro

**Kept, because it's the game:**
- the decomp's game logic and data structures;
- its fixed-point maths, bit for bit;
- the 30 Hz tick with `g_DeltaTime` clamped to 2–4 (`main.c`);
- the moby and particle classes per level;
- the WAD data formats, the memory card format, and the order in which things are drawn;
- the level-data budget and its contiguity checks (`loaders.c`).

The original's results are the specification. The oracle (PCSX-Redux), the host's byte-identical checks and Vita3K's identical captures stay the gate for every stage.

**Removed, because it's emulation:**

| Today | Fully native |
|---|---|
| mips2c C: MIPS registers, an emulated `$sp`, KUSEG masking, dispatch on PS1 code addresses | Readable C on the decomp's structs, like B1 |
| A GTE register file shared by every renderer (`gteRegs`, `MTC2/MFC2/CTC2`) | Fixed-point functions on C values (`rtps(&m, &v) → {sxy, sz}`), with the state that really lives across calls (the camera's rotation and translation, the screen offset and H, the far colour) as named globals; NEON where loops pay |
| Renderers write PS1 primitives with 24-bit OT tags; PsyCross walks the OT, parses each primitive and splits draws; `gpu_gl.c` interprets PS1 GPU state | The renderers append native vertices (the 28-byte vertex of D70) to per-bucket batches, in the same order. One pass turns the buckets into a few large draws. The PS1 primitive path remains only for the rare framebuffer tricks (StoreImage/MoveImage), if those stay as they are. |
| 2 MB "PS1 RAM" at a 16 MB-aligned address, data at PS1 addresses (D23, D25), bit-31 model tags, 21-bit animation pointers, 24-bit OT links | Ordinary heap memory with native pointers. The level-data budget is kept as a number, not as an address range. |
| An SPU register model mixing ADPCM per sample per voice, once per vblank | Samples decoded once, when they're written to sound RAM. Voices mixed with NEON, with the same ADSR and pitch steps. XA decoded on the read-ahead thread straight to PCM. SceAudio output. |
| libcd over LBAs: sector reads, seeks, a CD timing model | Reads by file and offset (`sceIo`, async), from the disc image or the extracted files. The completion order and frame latency the game sees are kept. |
| The game's vblank count driven by `sceDisplay` with a catch-up loop (`vblank.c`, MAX_CATCH_UP 8) | A 59.94 Hz clock driving the 30 Hz logic. Presentation on its own thread at the display's rate, interpolating snapshots (present.c's idea, without re-running the game's draw path on the game thread). |
| PsyCross (libgpu, libgte, libetc, libpad) | Thin native functions for the 76 PsyQ entry points the game calls, or none where a stage removed the call. PsyCross is no longer linked. |

**Optional extras on top,** off by default (`CLAUDE.md`): a depth buffer, perspective-correct textures, sub-pixel vertices. Native vertices carry the GTE's own depth (`SZ`) and the true `1/z`, so these become per-vertex attributes instead of today's vertex-cache reconstruction (`vcache.c`, D39).

---

## 5. The staged plan

Ordered by payoff over risk. Every stage keeps the old path one switch away until hardware has run it, as D72's `fast_render` did. Sizes are rough, from this audit's line and instruction counts:
- the hand C so far took about 0.29 code lines per original instruction (B1: 13,669 → 4,028);
- the renderers took about 0.44 (B2: 10,971 → 4,871).

### Stage 0: measure (small, config only, now)

- **Replaces:** nothing.
- **Gains:** K1–K3 turn the render thread's upper bound into a measured CPU figure. They give the hardware cost of the register model and software GTE in the renderers (K2 against K1), and the threads' waits (K3).
- **Follow-up (small engine change, its own PR):** a periodic per-thread CPU line from the watchdog's `runClocks` samples, every 300 vblanks. A sampling profiler is out: the Vita has none we can use without a kernel plugin.
- **Verify:** none needed. The runs only read.
- **Optional:** not applicable.

### Stage 0b: the two fixes stage 0 found (small, low risk, next)

**Atlas decodes on the CPU** (category E → native):
- **Replaces:** `atlas_decode()`'s render pass over the whole 2048 × 2048 atlas (`gpu_gl.c`).
- **Method:** decode each new 32 × 32 block on the CPU (CLUT lookup, the STP and transparency rules of the decode shader) and write it into the atlas texture's memory. vitaGL keeps the texture in memory the CPU can write. NEON for the 4- and 8-bit lookups.
- **Gains:** 1.4–9.1 ms of GPU per picture in play (§3.4), and the 17–29 ms gaps between pictures in new areas. At 60 fps that is the difference between holding and falling to 30 when exploring.
- **Verify:** Vita3K window captures pixel-identical on D74's routes, and the `atlas` lines' block counts unchanged. On hardware, K3 again: the meter's "texture decodes" row should read 0.
- **Size:** ~200–400 lines in `gpu_gl.c`.
- **Optional:** yes, a switch, until hardware has run it.
- **Done (D79):** `atlas_cpu=1`, the default. In Vita3K the window captures of the gems script, the pause route and the attract demo show 0 differing pixels inside the picture against the pass (`atlas_cpu=0`), and the `atlas` lines' counts are identical. On hardware (D79): `atlas_cpu=2` found 0 differing texels in 1,754 entries; the meter's "texture decodes" is 0; the CPU takes 27–49 µs per block (at most 0.07 ms per vblank). No NEON: not worth it at that size.
- **Hardware runs L1–L3** (`tools/vita/fps-runs/`, each a whole `config.txt`): K3's switches and route with the pass (`l1-atlas-gpu.txt`), with the CPU decode (`l2-atlas-cpu.txt`), and with the pass checked texel by texel against the CPU (`l3-atlas-check.txt`, optional; its timings mean nothing). For the owner:
  1. Use a VPK built from this PR's branch, or from main once it's merged.
  2. Keep a copy of your own `ux0:data/spyro/config.txt`, and copy one L file over it.
  3. Start the game and leave it alone for 3 minutes: the title, the flythrough and the Stone Hill attract demo.
  4. Then load a game: 2 minutes in Artisans, then 2 minutes in Stone Hill. Don't open the options menu (it rewrites `config.txt`).
  5. Quit, and send back `ux0:data/spyro/spyro.log` renamed `l1.log` (then `l2.log`, `l3.log`). Put your own `config.txt` back.
  - **What they settle:** L2's `gpu meter` lines should have no "texture decodes" time, and its `atlas N: atlas_cpu=1:` lines give the CPU's time per block. Against L1, the play windows' GPU time per picture and the gaps between pictures. L3's `atlas N: atlas_cpu=2:` lines should all say 0 differ.

**The XA read-ahead after a jump back:**
- **Replaces:** `want()`'s slot choice in `xa_ahead.c`.
- **Gains:** 0.5–0.6 ms per vblank back off the game thread, in every level after the first.
- **Verify:** host WAV byte-identical; on hardware, the `xa` lines serve from the read-ahead after level changes.
- **Size:** ~10 lines.
- **Done in D78.** In Vita3K and on hardware the WAVs are byte-identical, and the read-ahead serves the music after every level change. On hardware the `spu` part in play went from 0.58–0.66 ms per vblank back to 0.07 (run M1, unattended).

### Stage 1: GTE calls into native maths (medium, low risk)

**Replaces** (category E): `gte_inline.h`'s register-file commands in the four B2 files (`env_hand.c`, `moby_hand.c`, `pete_render_hand.c`, `sky_hand.c`):
- 245 `MTC2`, 200 `MFC2`, 152 `CTC2`, 32 `CFC2`;
- the RTPS, NCLIP, DPCS, INTPL, MVMVA and GPF calls between them.

**Method:**
1. **Audit which GTE registers are live across function boundaries.** D72 found that the renderers store data in control registers (the camera position in RBK/GBK/BBK) and that mips2c code and PsyCross's libgte (12 calls in the decomp C: `MulMatrix`, `SetTransMatrix`, `SetGeomOffset`, …) set registers the renderers read. Those become named globals with accessors that keep the register file in sync while any mips2c caller remains.
2. **Inside a renderer, rewrite each command as a function of C values.** Compute only what the code reads afterwards. Keep the exact integer formulas, including the saturation and the `FLAG` bits that are read; flags that nothing reads go.
3. **Use NEON where a loop projects many vertices.** The environment's vertex loop issues 7–15 million RTPS per host run (D72). That's 4 × s32 lanes with `vmull`/`vqshrn` for the 16 × 16 → 32 products and saturating narrows; the division (`H/SZ`) stays scalar, with the GTE's own reciprocal (its UNR table), so the results don't change.

**Gains:**
- **CPU:** *estimate* −0.5 to −1.3 ms per vblank on the game thread at 30 fps, twice that at 60: 60% of the renderers' 1.5–4.5 ms (K1, §3.4), minus what the native maths still costs.
- **Maintainability:** it removes the hidden register-file coupling between renderers. Stage 2 needs that: its vertices must be C values.

**Verify:**
1. `tests/gte` per new function: the 9,314 PCSX-Redux vectors, plus millions of random states against `gte_inline.h`, comparing every output the caller reads.
2. A mutation must fail the tests.
3. The oracle replay of the six renderers (D72's 661 samples).
4. `tools/fast_render_ab.sh` extended to the new level: the 198 host files byte-identical.
5. Vita3K shots and window captures identical.
6. Hardware K1 against the new build.

**Size:** ~1.5–2.5k lines changed in the four files, plus tests.

**Optional:** yes, as `fast_render=2`. `fast_render=1` (today's register-file C) and `0` (mips2c) stay linked.

**Done (D80 and D81, 2026-11-02).** `fast_render=2` was the default from D81 (the host's still; the Vita's is 3 since D124). NEON one vertex at a time was tried and is 5% slower on hardware (D81). It would pay only with four vertices per batch, in stage 2's vertex loops. N1 and N2 ran on hardware: identical pictures and memory card, and the renderers −15% (2.28 → 1.92 ms per vblank). What it is and how it was checked: `docs/decisions.md` D80 and `docs/translation-log.md`. In short: every call checked against `fast_render=1` over every host script (0 differences), 198/198 host files and 661/661 oracle samples identical, Vita3K shots and window captures identical. The renderers are −15 to −17% on the host (qemu), −16 to −35% in Vita3K and −15% on hardware.

**The last three (D98, 2026-11-04).** D95's shadows, flame and particles, hand C on the GTE register file, now have native GTE maths too, behind `ngte_fx=1` with `fast_render=2` (RTPT, GPF with sf 1 and GPL are new in `gte_native.h`). The register file is left exact by replaying, at the end, only the commands whose results can still show (`docs/decisions.md` D98). Checked call by call against both the mips2c unit and `fast_render=1`, and on hardware (X4, X5: the same pictures and card). It is a few microseconds per vblank slower than D95's register-file code for these short loops, so the default stays `ngte_fx=0`.

#### The GTE audit

**Measured, on the host** (`--fast-render-check --gte-audit N`, `src/game/hand_check.c`). Before every Nth call of each renderer, each of the 64 GTE registers in turn gets its raw storage changed, with two patterns. The register-file hand C then runs again from the same state, and the result is sorted:

| Class | Meaning |
|---|---|
| in | the change shows in RAM |
| gte | the change shows only in other registers the call leaves |
| kept | the register is left exactly as changed |
| dead | the register is written before it's read |

Classes are unions over the sampled calls. The attract demos (every 100th call, `logs/ngte9-audit-demo`) and routes A and B (every 25th, `logs/ngte13-audit-route*`) give:

| Renderer | in | gte |
|---|---|---|
| env `func_800258F0` | OFX OFY H | RGBC IR0 SZ0-2 RGB2 DQA DQB |
| sky `func_8004EBA8` | OFX OFY H | IR0-3 SZ0-2 DQA DQB |
| mobys `func_8001F798` | OFX OFY H | RGBC IR0-3 SZ0-2 DQA DQB |
| lit mobys `func_80022A2C` | OFX OFY H | RGBC IR0 SZ0-2 DQB |
| Spyro `func_80023AC4` | OFX OFY H | RGBC IR0 SZ0-2 DQA DQB |
| specular `func_80020F34` | OFX OFY H | IR0-3 SZ0-2 DQA DQB |

Audited calls: 238 in the demos, 1,036 on route A and 1,043 on route B (2,317 in all, 128 runs each); every audited call also passed the 1-against-2 check.

**What reaches the renderers from outside:** only the screen offset and H. They come from `SetGeomOffset(256, 120)` and `SetGeomScreen(341)` (`third_party/spyro-1/src/initialization.c:260-261`).

The other libgte calls in the decomp C only set the rotation and translation (and leave MAC/IR): `camera.c:37` `SetTransMatrix`, `camera.c:61`, `:76` `MulMatrix`, `pete.c:1938`, `:2135`, `:2234` `MulMatrix0`, `gamestates/draw.c:821` `SetTransMatrix`, `:833`, `:840` `MulMatrix`. Every renderer moves its own matrix in before it uses one, so these are "dead" for all six.

**The "gte" class:**
- DQA/DQB change only RTPS's MAC0/IR0, which no renderer reads.
- RGBC's code byte is INTPL's colour push (env's animation blends `env_hand.c:182`, mobys' frame blends `moby_hand.c:261`, Spyro's `pete_render_hand.c:374`).
- SZ0-2 (and RGB2, IR0-3) survive into the FIFO's other slots or the call's last results when a call projects fewer than four vertices or runs fewer than three colour commands; IR0's high half survives RTPS, which writes only the low half.

**State a renderer keeps in the GTE within a call:**
- **env:** the camera position in RBK/GBK/BBK (`env_hand.c:922`), read back per sector (`:943-944`, `:1444-1445`).
- **mobys:** ZSF3 = `g_SkipLowPolyWorld` (`moby_hand.c:997`, read at `:870`; the specular entry `:2381`), and the detail in ZSF4 (`:787`).
- **lit mobys:** the zone in DQB and the HUD flag in DQA (`:1574`, `:1674`, read at `:1756-1757`).
- **Spyro:** the depth parameters in RBK/GBK/BBK (`pete_render_hand.c:1144`, read at `:970`, `:978`, `:1239`), and V0 from the joints' RTPS (`:1177-1183`), which the first vertex step transforms (`PartVertices`, `:1225`).

**State that outlives a call:**
- **OFX/OFY:** rewritten for each HUD model (`moby_hand.c:1555`) and put back to 256/120 at its end (`:1735`).
- **DQA/DQB:** left as moby sprites (`:610-611`) and lit mobys (`:1574`, `:1674`) set them; they reach later RTPS's MAC0/IR0 only.
- **The whole register file:** the oracle replay compares it, save states store it (D51), and the in-between frames snapshot and restore it (D30). So D80 keeps it exact rather than keeping only what's read.
- **Reads with side effects:** MFC2 of SZ3, IR1-3 and VZ0 rewrite the register's high half, so the native code replays them too.

**For stage 2:** a renderer's projected vertices depend on its own inputs plus OFX/OFY/H, three values per call. Native draw lists can take them as C values from `ngte_proj_get()`.

#### Hardware runs N1 and N2

Two config files in `tools/vita/fps-runs/`, K1's switches with `fast_render` 1 and 2:

| Run | Switches | What it settles |
|---|---|---|
| `n1-gte-1.txt` | 30 fps, `render_thread=0`, `parse_thread=0`, `dynres=0`, `fast_render=1` | K1 on the stage 1 VPK: the register-file renderers |
| `n2-gte-2.txt` | N1 with `fast_render=2` | the native GTE maths; its `draw` lines against N1's are stage 1's gain on hardware |

**For the owner:**
1. Use the VPK from this PR's branch (`claude/native-gte-9218f1`), or main once it's merged.
2. Keep a copy of your own `ux0:data/spyro/config.txt`.
3. Copy `n1-gte-1.txt` over it.
4. Start the game and leave it alone for 3 minutes: the title, the flythrough and both attract demos.
5. Then start or load a game: 2 minutes in Artisans, then 2 minutes in Toasty.
6. Don't open the options menu, because it rewrites `config.txt`.
7. Quit, and send back `ux0:data/spyro/spyro.log` renamed `n1.log`.
8. Do the same with `n2-gte-2.txt`, and send `n2.log`.
9. Put your own `config.txt` back.

The log's start says which build ran: `fast_render=2: hand-written renderers, native GTE maths (D80)`.

To read them: `python3 tools/vita/native_share.py n1.log --fps 30 --all`, then the same for `n2.log`. Compare the `draw` lines' env, mobys, spyro and sky ms window by window: the attract windows have the same input in both runs.

#### 60 fps with everything merged (D81)

Runs O1 and O2 (`tools/vita/fps-runs/o1-60-fr1.txt`, `o2-60-fr2.txt`; main `6128154`, the menu's 60 FPS, real vblanks) on the K route and a new Toasty route (`tools/vita/hw-routes/o-toasty.*`). Read them with `tools/vita/fps60_summary.py`.
- **60 fps holds in play** in Artisans, Stone Hill, Dark Hollow and Toasty: 300 pictures and 0–1 late vblanks in every steady window, and no fall to 30 outside loads.
- **The exception is the Stone Hill attract demo's flight:** 36–44 late per 300 in three of its seven windows, bound by the render thread (81–89% busy, 15–16 ms a picture, 150,000–194,000 PS1 GPU commands a window). The GPU there takes 7–10 ms, and the game thread is 48–65% busy. That's stage 2's target.
- **At 60 fps `fast_render=2` takes 15–20% off the renderers,** 1.0–1.9 ms per tick of the game thread.

### Stage 2: native draw lists instead of PS1 primitives and the OT walk (large, medium–high risk, biggest payoff)

**Replaces** (D and E):
- the PS1 primitives the six renderers write into the 2 × 0x1C000-byte primitive buffers, with their 24-bit OT tags;
- PsyCross's walk and parse of them (`PsyX_GPU.cpp`, on the parse thread since D71);
- `gpu_gl.c`'s per-primitive interpretation of PS1 GPU state for those primitives (texture page, CLUT, blend mode, the atlas lookup per triangle).

The decomp C's own primitives stay PS1 primitives in the same OT: `draw.c` (52 lines name a primitive type), `particle_spawn.c` (42), `flight.c`, `particle_update.c`. They're few per frame.

**Method (incremental, exact order):**
1. **A port-only marker packet** that a renderer links into an OT bucket where it would have linked its primitives. The marker names a segment of a native vertex batch. The OT walk (`patches/PsyCross/0009-ot-walk-hooks.patch` already hooks it) emits the segment when it reaches the marker. So the draw order stays exactly the OT's: buckets, FIFO within a bucket, the HUD chain last.
2. **Environment first** (45–47% of the renderers' cost, D72). For each primitive the asm would have written, it appends the already-resolved vertex: screen x/y from the GTE, colour after the depth-cue fade, and atlas UV and texture state resolved once per texture page and CLUT instead of per triangle in `expand()`. The blend mode and the textured or untextured program go in per segment, so #49's joins happen at build time.
3. **Then mobys, Spyro and the sky; then particles, shadows and the flame.**

   Shadows use subtractive blending. #49 never joins subtract draws, because its two passes would interleave differently, so shadows need their own segment rule.

**Gains:**
- **CPU:** *estimate* the OT walk's 1.1–2.2 ms per vblank goes for this geometry, and the render thread's per-triangle work falls by most of its CPU share. K1 measures that share: before D68 it was 3.9–4.7 ms per vblank.
- **Memory:** about 230 KB of primitive buffers. That matters for stage 4.
- **GPU:** whatever #49's joins still can't merge.
- **Battery:** the fewer cores busy, the less it uses.
- **Optional extras:** they become cheap. Depth and `1/w` ride in the vertex.

**Verify:**
- **Triangle-stream equivalence on the host.** A host build emits both the PS1 primitives and the native batch for every frame. It compares PsyCross's `GrVertex` triangles with the batch's, in order, byte for byte (the host already has `--tri-dump`). Run it over the demos, routes A and B, and the whole-game tour.
- **Then** `tools/host_checks.sh` byte-identical, Vita3K window captures identical (D74's routes), and hardware K1 and K3.

**Size:** ~3–5k lines across the renderers, a new `src/platform/vita` batch path, and a small PsyCross patch.

**Optional:** yes, `native_draw=0/1`, then per renderer. It touches `gpu_gl.c`, so it starts only after #48 and #49 are merged.

**Done in part (D82, 2026-11-02), behind `native_draw=1` (default 0).** On hardware (P5/P6) the walk's thread gets 0.46 ms per vblank faster and the renderers 0.52 ms slower, so the default stays 0 until the renderers stop building the PS1 primitives too. What it is and how it was checked: `docs/decisions.md` D82 and `docs/translation-log.md`.
- **Converted:** all six hand renderers' primitives: env, the sky, the regular, lit and specular mobys, Spyro. Each primitive is turned into the vertices PsyCross's parse would make of it where the code links it, and a marker goes into the bucket in its place, or the primitive joins the marker already at the bucket's tail. The walk copies the records (PsyCross patch 0010's packet hook).
  - The primitives are still built where they were, so the buffer's pointer and its bounds don't change.
  - The order is the OT's.
  - In the demos 7.4 M primitives become 2.0 M markers.
- **Checked:**
  - **Every call** of every converted renderer, both ways, through PsyCross's own walk (host `--native-draw-check`), on the demos, gems, wsi, lod, the vertex cache, routes A and B and the whole-game tour: 184 M triangles, 0 differences.
  - **Every file** of those runs identical with `native_draw` off and with main's runs.
  - **Vita3K:** the gems script's and the demos' shots and window captures, and the pause route's shots, identical.
- **Hardware (P5, P6):** the walk's thread 0.46 ms per vblank faster, the renderers 0.52 ms slower (they built every primitive and then its record), so the game thread was no faster.

**Then (D86, 2026-11-03): the records without the PS1 stores, still behind `native_draw=1` (default 0); on hardware no cheaper.**
- **The hot sites build their records from the values they would store** and skip the primitive's stores: env's high- and low-poly passes, its 2x2 pieces and edge triangles, the moby and specular faces, the lit Gouraud faces, Spyro's faces, the sky. The space is still taken, so the buffer's pointer and every bound are the same. The stores made before a face's last check, and those a G4/G3 leaves past its end, stay: the check showed the game reads them.
- **A smaller record:** 3 words a textured vertex and 2 an untextured one (5 before), the page/CLUT once per record. The records of the gems script fall from 429 to 266 MB, the largest list from 130 to 75 KB.
- **Checked:** the per-call check both ways on the demos, gems, wsi, lod, the vertex cache, routes A and B and the whole tour (0 calls differ, every file identical); `tools/host_checks.sh` byte-identical with main; Vita3K's gems and demo shots and captures and the pause route identical.
- **Hardware (P7, P8): no cheaper.** The renderers still cost +0.53 ms per vblank with records (D82: +0.51), though Vita3K showed that halved. The game thread was busy 4.39 → 4.97 ms per vblank, and late vblanks rose 2.6 → 3.4 per 300. So the PS1 stores weren't the cost: what's left is the record path itself (a second stream of writes per primitive, the markers and joins). `native_draw` stays 0.
- **Not yet:**
  - **Measuring the record path on hardware** (the CPU's counters, or timing env's sites one by one) before any more work goes into it.
  - **The render thread's side:** the atlas resolved once per page and CLUT, the program and blend per segment. On P6 `expand` took 0.78 ms per vblank of the render thread, which isn't shown to be the limit at 60 fps.
  - **The decomp C's own primitives:** particles, shadows (subtractive), the flame, the HUD. They're 0.5–2.6% of the packets the walk sees (D86). Particles, shadows and the flame are mips2c units and need hand C first; the HUD's are the ones widescreen squeezes in the OT.

**Then (D91, 2026-11-03): measured on hardware, a leaner record, and `native_draw=1` by default on the Vita.**
- **Measured:** `nd_ab=1` times variants of the record path in turn, 8 lists each, in the same scenes (runs T1 and T2). About 60% of D86's extra renderer cost was the second write stream itself (a probe that writes 12 words a primitive and builds no record costs +0.18–0.19 ms per list); the rest was the record path's instructions. Uncached pools and prefetching didn't help the records.
- **The lean record:** the primitive's own words in the pool, plus a link and the kind. The parse thread turns them into vertices. Joins come from a side table, so the game thread never reads a pool. The hot state is one struct, and there are no per-primitive counters.
- **Checked:** the per-call check both ways and the file A/B on gems, the demos, wsi, the vertex cache, lod, routes A and B and the whole tour (0 calls differ, every file identical); Vita3K's gems shots and captures identical.
- **Hardware:** T3 put the renderers at +0.16 ms per list with records (D86: +0.28–0.33). P9/P10: the game thread 5.79 → 5.73 ms per vblank, the parse thread 2.48 → 1.76; late vblanks 2.53 → 2.78 per 300, the rise all in the attract demos (Toasty play 24 → 23).
- **Default `native_draw=1` on the Vita** (the owner's decision); the host keeps 0.
- **Not yet:** the renderers' last 0.16 ms (mostly the second stream), the render thread's side, the decomp C's own primitives (as above).

**Then (D92, 2026-11-03): the rest of stage 2 that doesn't need hand C.**
- **The walk parses every polygon packet with the records' converter** (`nd_fast`, on with `native_draw` 1 and 2): so with `native_draw=1` the particles', shadows', flame's, HUD's and `flight.c`'s polygons reach the GPU through the same code as the records. Their lines, `DR_MODE` packets and fills stay with PsyCross.
- **`native_draw=2`:** the renderers on their PS1 path, no records and so no second stream of writes; the walk parses their packets with the same converter, without joins. `nd_ab=1` times 0, 1 and 2 in turn, to settle on hardware whether the records' +0.16 ms per list buys more on the walk than it costs.
- **The render thread** (`gpu_fused=1`): the slab's vertices go to the lean vertex in one pass with one copy each; `atlas_find`'s answers are kept by page, CLUT and blocks; a draw's program is chosen per run of like triangles; untextured subtract draws join (the shadows' rule; it seldom fires: Spyro's shadow is already one split, the mobys' are textured). `gpu_fused=2` times the two ways in turn.
- **Checked:** see D92 (host per-call check with a third walk for `native_draw=2`, a per-packet parse check, file A/B; Vita3K shots and captures with `native_draw` 0, 1, 2 and `gpu_fused` 0, 1, 2).
- **Hardware (U1):**
  - Per list, against no records: records −0.19 ms of game thread and −0.95 ms of the parse thread; the in-place parse (`native_draw=2`) −0.18 and −0.51.
  - So the two ways are equal for the game thread, and records stay the default for the parse thread's 0.44 ms.
  - `gpu_fused=1`: −0.275 ms of render thread per vertex buffer (9%).
- **Still not done, and why:**
  - **Records built by the particles, shadows and flame themselves:** they are mips2c units (`r_particles.s`, `r_shadows.s`, `r_flame.s`, 2,642 instructions); records need hand C, which is the next PR (D95).
  - **The HUD's and `flight.c`'s sites as records:** they link through `func_800168DC`, but widescreen narrows these primitives in place at `DrawOTag`, so a record taken at the link would be wrong there; and the walk already parses them natively (`nd_fast`).
  - **The renderers' last 0.16 ms:** with records it is the second stream (D91; U1 +0.19). `native_draw=2` removes it by having no records, but then the walk loses the joins. For the game thread it is a wash (U1), so the records' parse-thread saving decides.

**Then (D95, 2026-11-04): the shadows, the flame and the particles as hand C with records.**
- `r_shadows.s`, `r_flame.s` and `r_particles.s`'s seven drawing entries (2,642 instructions) are hand C on the GTE register file (`src/game/shadows_hand.c`, `flame_hand.c`, `particles_hand.c`; `HAND_RENDERER_UNIT`), checked call by call against their mips2c units and by the oracle replay. Their polygons are native records with `native_draw=1`; their lines stay PS1 primitives.
- So every renderer's polygons now reach the walk as records. What stays PS1 primitives: lines, the HUD's and `flight.c`'s 2D primitives (parsed natively by `nd_fast`), and the draw environment packets.
- Left as mips2c in the renderers: `r_cyclorama.s`'s helpers, `r_environment.s`'s other functions and `r_moby.s`'s culls (stage 3), and `r_particles.s`'s slot allocators. D96 (stage 3) made them hand C too.

**Then (D99, 2026-11-04): direct vertices, the last piece, behind `nd_direct=1` (default 0).**
- **What was left:** each triangle was converted twice, by the walk (`em_prim`, into PsyCross's `GrVertex`) and again by the render thread (`expand_lean`: every vertex copied into the lean vertex with its UV box, atlas offset and flags). U1 timed that second pass at 1.9 ms of the render thread's 2.8 ms per vertex buffer (D92); X4 at 0.79–0.92 ms per vblank on the game thread.
- **Now:** the walk writes each triangle's UV box into the vertex's spare bytes and lists the triangle. The render thread draws the slab's vertices as they came and adds only what depends on the atlas and the options, the atlas offset and two flags, in a side stream of 8 bytes a vertex. One rule for the box on both sides (`src/platform/common/psx_lean.h`). Everything `em_prim` makes is covered: the records and every polygon packet `nd_fast` parses (the HUD's polygons, particles, shadows, flame). PsyCross's own sprites, lines and rectangles are converted on the render thread by the same rule.
- **The OT walk stays.** It orders the records with what is still a PS1 packet, and with the markers it visits a few hundred packets a list. Its cost was the conversions.
- **Checked:**
  - **host:** every packet both ways, 0 differ, every file identical on the demos, gems, 16:9 with 60 fps in-between frames, lod, routes A and B and the whole tour; `host_checks.sh` byte-identical with main; the oracle replay 0 FAIL.
  - **Vita3K against main's VPK:** shots, window captures and card identical on gems, gems with every layer on the game thread, gems with `tex_filter=1`, the demos, 16:9 at 60 fps, and the pause route with a save state and load. `nd_direct=2` compares the render thread's own conversion with `expand_lean`'s, attribute by attribute: 0 of 21.9 M vertices differ.
- **Hardware (Y1–Y4, 2026-11-05, D99):**
  - **Pictures:** the same 18 pictures and card with the switch off and on.
  - **K route, every layer on the game thread** (§3.5): the GL side's vertex buffers drop 1.11 → 0.65 ms per vblank, but the walk grows by as much. The box costs the same wherever it is computed, so the sum moves 9.92 → 9.82 ms.
  - **Toasty at 60 fps, the shipped threads:**
    - the render thread, the wall at 60 fps, does 0.75 ms per vblank less, 19% of its work;
    - the parse thread does 0.72 more, and the game thread waits 0.33 more for it;
    - late vblanks fell (the Stone Hill demo 9.9 → 2.9 per 300, Toasty 0.1 → 0.0, loads 169 → 97).
  - **The default stays 0 (the owner's decision).** A cheaper box in the walk (ARMv6 SIMD minima and maxima, the skip test from the spans) would make it a gain on every thread.

**Then (D105, 2026-11-05): the walk's box as ARMv6 SIMD; `nd_direct=1` the Vita's default (the owner's decision).**
- **Now:** `em_direct` makes the skip test (`qsub16`, `uadd16`, `uqsub16` per edge) and the box (byte minima of each vertex's `u, v, 255 − u, 255 − v` word, `usub8` + `sel`) on whole words, a quad's shared edge and vertices once. About 100 instructions for a quad's two triangles, against about 260 for D99's rule. Off ARM the same lanes are C, so the host's per-packet check runs this form against D99's rule.
- **Checked:** `tests/lean`, 8.4 M triangles on the host and 33.6 M with the Vita's compiler under qemu-arm, 0 differ, and mutants caught. Every packet both ways on six host scenarios, 0 differ, every file identical. `host_checks.sh` 279/279 identical with main. Vita3K `nd_direct` 0 against 1: shots, captures and card identical on the same six runs as D99's; `nd_direct=2` with the lane form, 0 of 21.9 M vertices differ.
- **Hardware (Y5–Y8, 2026-11-05, D105):** the same 18 pictures and card. The K route on one thread 9.81 → 9.39 ms per vblank (−4%; D99 −1%): the walk now grows 0.14 ms where it grew 0.44. Toasty at 60 fps: the render thread −0.83 ms per vblank, the parse thread +0.22 (D99 +0.72), the game thread's wait +0.10 (D99 +0.33) with its busy share unchanged, late vblanks fewer (the Stone Hill demo 9.7 → 3.0 per 300, loads 195 → 108).
- **Next inside the layer: the clears.** `GR_ClearVRAM` is 2.70 of the "PS1 GPU → vitaGL" layer's 3.66 ms per vblank on the K route, and 5.7 ms per vblank on the render thread at 60 fps (§3.5).

**Then (D111, 2026-11-06): the clears, behind `gpu_clear` (default 0 until hardware).**
- **What a clear is:** Spyro clears once a frame, the draw buffer it is about to draw (512×224, whole, unwrapped, the level's sky colour; `logs/clr-count`). `gpu_gl.c` then did five things: sent the held batch, set the 114,688 pixels of `s_vram` one at a time through the wrap, sent pending uploads, started the target's log again by writing its base (another 114,688 pixels, the colour), and cleared the target with a scissored `glClear`.
- **The split** (a third `gpu parts` line with `gpu_parts=1`; Vita3K, gems with every layer on the game thread, means from vblank 900, `logs/d111-v3k-t*`): `GR_ClearVRAM` 0.33 ms per vblank, of which the `s_vram` loop 0.12, the log's base 0.16 and the `glClear` 0.03; the batch, the uploads, the presentation slots and the bind 0.00. **The two CPU loops are 85% of it.** Vita3K's GPU calls are not the Vita's, so the hardware split is Y9's to give (proposed, `tools/vita/fps-runs/README.md`).
- **Now:**
  - `gpu_clear=1`: the VRAM rect row by row, each unwrapped piece of a row 16 pixels a step with NEON (`src/platform/common/psx_fill.h`), and a whole clear's log base kept as its colour, written only if a replay needs it. Vita3K: the clear 0.33 → 0.09.
  - `gpu_clear=2`: also, a clear of a target's whole rect doesn't write `s_vram` at all. The target is marked as drawn, so whatever reads that VRAM first gets the target's pixels back, and the read-back rewrites the whole rect from the log (base: the colour). Only with `gpu_readback=1` (the default) and at full scale. Vita3K: 0.33 → 0.06, the `glClear` alone; on the render thread at 60 fps 0.98 → 0.05.
  - `gpu_clear=3`: 2's audit, poison in the pixels 2 leaves unwritten.
  - The host's `--gpu-clear` is 1's fill in the software rasterizer (`gpu_soft.c`).
- **Checked:** `tests/fill` (the fill against the plain loop: the host's C path, 0.3 G pixels, and the Vita's NEON under qemu-arm, 9.4 G pixels, 0 differ; the mutant caught); `host_checks.sh` 287/287 identical for main, off and on; the host's VRAM dumps identical with and without `--gpu-clear` (239 dumps over the demos, gems with the pause, routes A and B); Vita3K against main's VPK: every shot and card identical with 1, 2 and the audit, the pause screens' read-back included; the captures that differ are Vita3K's (main's own differ between runs at 60 fps; D111 has the table).
- **Hardware (Y9–Y12, 2026-11-06, D111):** the same 18 pictures and card as Y6, with `gpu_clear` 0 and 2.
  - **On the Vita the loops are 99% of a clear:** `s_vram` 1.07 and the log base 1.65 of 2.77 ms per vblank. The `glClear` is 0.03.
  - **The K route with every layer on the game thread:** 9.54 → **6.70 ms per vblank (−30%)**, the PS1 GPU → vitaGL layer 3.59 → 1.01.
  - **Toasty at 60 fps:** the render thread clears for 0.15 ms per vblank instead of 5.46, and is busy 8.12 ms instead of 10.49; the rest of the freed time it waits for the display. The Stone Hill demo's flight, where it was the wall, now takes 7.2 ms a picture (12.9 in Y11), with 0.7 late vblanks per 300 against 3.0. The loads have 68 late vblanks against 102.
- **Default: `gpu_clear=2` on the Vita** since D111's hardware runs (the owner's decision); the host stays 0.

### Stage 3: readable C for the rest of the mips2c code (large, low–medium risk, in parallel with stage 2)

**Replaces** (C): 19,272 original instructions, 32,563 generated lines and 453 KB of ARM code:

| Code | Insns |
|---|---:|
| `collision.s` | 3,866 |
| `math.s` | 1,296 |
| `42CC4.s` | 1,050 |
| the gamestates 8 and 11 update | 3,133 |
| the cyclorama set-up | 1,287 |
| the renderers' remaining functions (r_cyclorama's helpers, particles, r_environment's other five, r_moby's culls, shadows, flame) | 6,335 |
| moby lists, `moby_helpers`' one, images, particle allocation | 711 |
| the credits overlay's two functions | 1,594 |

Also the B2 renderers' remaining machine state: the scratchpad and spill area as locals, and `HPTR` replaced by native pointers, once stages 1 and 2 have taken their GTE and primitives. That turns B2 into B1.

**Method:** D33's, as for pete and camera. One function at a time, written from the asm. Each function is checked against the oracle with targeted captures, and against its own mips2c unit with differential sweeps (`SPYRO_M2C_REF=ON`). A/B builds come from `SPYRO_M2C_FUNCS`. Collision first: its results feed gameplay, it's the largest, and `docs/porting-plan.md` §2.4 ranks it the hardest.

**Gains:**
- **CPU:** small. Logic is 3% of the CPU, and mips2c 2.6% of the host profile. The culls, particles and shadows are a slice of the renderers.
- **Maintainability:** large. ELF: −450 KB.
- **The real reason:** it's the prerequisite for stage 4. mips2c code addresses memory through `MIPS_KUSEG`, keeps its stack in the arena and dispatches on PS1 code addresses.

**Verify:**
1. Oracle replay of every capture set (7,155 samples today).
2. Differential sweeps for the paths no capture reaches.
3. `tools/host_checks.sh` byte-identical: the demos, the whole-game tour, routes A and B.
4. Both attract demos tick for tick against the emulator (`trace_diff.py`).
5. Vita3K, then hardware.

**Size:** *estimate* ~6–8k lines of hand C at B1's rate (more for collision, with its computed jumps).

**Optional:** a build option per function (`SPYRO_M2C_FUNCS`), as now. No run-time switch is needed: equivalence is proven on the host.

**Status (D83, 2026-11-02): collision done.** `asm/collision.s` (3,866 instructions, six entry points) is readable C in `src/game/collision_hand.c`. It keeps nothing in the scratchpad or the GTE's registers: what the asm leaves there nothing reads (D83 says how that was shown), and a shadow in the host's oracle replay runs the mips2c unit beside every call so the replay stays exact. `tools/native_audit.py` on this branch's VPK (`logs/native-audit.txt`): readable C (A + B1) is **54.8% of the executable** (50.6% before), **92.4% of the whole game** with the overlays (91.7%); the original's instructions that run as mips2c fall from 19,272 to 15,406 (executable 17,678 → 13,812), and the ELF's live mips2c code from 310,611 to 262,127 bytes (B1 29,636 → 38,820). Next: `math.s` (its rotation matrix is state other calls read, so it stays in the GTE), `42CC4.s`, the gamestates 8 and 11 update. The renderers' remaining helpers wait for stage 2.

**Status (D87, 2026-11-03): `math.s`, `42CC4.s` and the gamestates 8 and 11 update done.** All 45 entries of `asm/math.s` in `src/game/math_hand.c`, all nine of `asm/42CC4.s` in `src/game/42CC4_hand.c`, and the dragon rescue (`func_8002F3E4`) and the fairy (`func_800314B4`) in `src/game/gamestates_hand.c`. The GTE's control registers the asm sets (the rotation and translation that `VecRotateByLastMatrix` and the projections read, and RBK) are set the same; the working registers it leaves (V0, IR, MAC, the FIFOs, LZCR, FLAG) nothing reads, and go (D87 says how that was shown). Shadows in the host build with the units linked compare each call with its mips2c unit (math: result, output, rotation and translation; 42CC4: RAM and the GTE's control registers; the gamestates: RAM, with the unit in a forked child), in the oracle replay and, with `SPYRO_SHADOW`, over whole host runs. `tools/native_audit.py` on this branch's VPK (`logs/native-audit.txt`): readable C (A + B1) is **60.8% of the executable** (54.8% before), **93.4% of the whole game** (92.4%); the original's instructions that run as mips2c fall from 15,406 to 9,927 (executable 13,812 → 8,333), and the ELF's live mips2c code from 262,127 to 164,795 bytes (B1 38,820 → 52,676). Left as mips2c: the renderers' helpers (6,335, after stage 2), the cyclorama set-up `func_80050BD0` (1,287), moby lists, `moby_helpers`' one, images and particle allocation (711), and the credits overlay's two functions (1,594).

**Status (D94, 2026-11-04): the cyclorama set-up, the moby lists, the particle slots, the images, `func_8003BFC0` and the credits overlay done.** `func_80050BD0` in `src/game/cyclorama_hand.c`; all three entries of `asm/moby_lists.s` in `moby_lists_hand.c`, both of `asm/particle_alloc.s` in `particle_alloc_hand.c`, all three of `asm/images.s` in `images_hand.c`; `func_8003BFC0` in `moby_helpers_hand.c`; the credits overlay's update and draw in `credits_hand.c`. One shared shadow (`hand_shadow.{h,c}`) compares each call with its mips2c unit, in this process or, for the functions that act outside RAM (sounds, the CD, the GPU), in a forked child; it found a real bug in the first draft of the cyclorama set-up that the emulator's samples hadn't (D94). `tools/native_audit.py` on this branch's VPK (`logs/native-audit.txt`): readable C (A + B1) is **63.0% of the executable** (60.8% before), **94.0% of the whole game** (93.4%); the original's instructions that run as mips2c fall from 9,927 to 6,335 (the executable's from 8,333 to 6,335: the overlays have none left), and the ELF's live mips2c code from 164,795 to 94,951 bytes (B1 52,676 → 62,148). Left as mips2c: only the renderers' helpers (6,335: r_cyclorama's 1,761, particles 1,512, r_environment's other functions 1,198, r_moby's culls 784, shadows 636, flame 444), which wait for stage 2.

**Status (D96, 2026-11-04): stage 3 done.** The renderer units' last mips2c functions are hand C: `r_moby.s`'s culls (`src/game/moby_cull_hand.c`), `r_environment.s`'s animation update (`env_update_hand.c`) and set-up and state calls (`env_anim_hand.c`), `r_cyclorama.s`'s depth-cued sky, portal skies and portal face with their clippers (`sky_clip_hand.c`) and `r_particles.s`'s glow and sparkle slots (`particle_slots_hand.c`), behind `render_hand.h`'s `HAND_UNIT_FN`: fast_render 1 and 2 run the hand C, 0 the unit, kept linked as the fallback; `--fast-render-check` compares every call with the unit. Main's audit overstated what was left: of the 6,335 instructions it showed as mips2c, 2,642 were D95's hand C, which `tools/native_audit.py` didn't see (a `)` in a cmake comment ended its list; D94 noted the audit didn't yet count D95's units); the live game ran 3,805 as mips2c, all converted here. `tools/native_audit.py` on this branch's VPK (`logs/native-audit/d96.txt`; main `e87aabc`'s, `logs/native-audit/base-main-script.txt`): **no instruction of the original game runs as mips2c any more** (6,335 before as counted, 3,805 as run); readable C (A + B1) is **63.5% of the executable** (63.0% before), **94.1% of the whole game** (94.0%); with the hand C over the machine model (B2), 81.9% and 97.1%. The ELF's live mips2c code is 348 bytes, the runtime the fallback units need (94,951 before); the units, all fallback now, are 237,576 bytes (142,970); B1 65,948 (62,148), B2 163,704 (42,696; it now counts D95's files, which the old audit put under G). What's left of stage 3's aim is the B2 code's machine model (the GTE register file, the scratchpad, `HPTR`), which stages 1, 2 and 4 take away.

### Stage 4: native memory (large, high risk, after stages 2 and 3)

**Replaces** (E):
- the 2 MB arena at a 16 MB-aligned base (`arena.c`, `arena_moved.cmake`, formerly `vita_arena.cmake`);
- the game's data at PS1 addresses (`SPYRO_PSX_LAYOUT`, D23);
- 24-bit OT links (`patches/PsyCross/0001-arena-ot.patch`);
- bit-31 model tags and 21-bit packed animation pointers (`docs/porting-plan.md` §4.4);
- the `MIPS_ARENA_TAG` constants (M3).

**Method:**
- **The remaining PS1 primitives** use PsyCross's extended 32-bit prim pointers (`USE_EXTENDED_PRIM_POINTERS=1`), or go through stage 2's batch.
- **Level data** comes from one heap block of the same size, so the loader's budget and contiguity checks keep their meaning. `PATCH_POINTER` fixups already add a base.
- **The tags move to a side bit or a struct field,** at the ~6 C sites `docs/porting-plan.md` lists plus the hand C.
- **Save states get a new version:** they store the arena by address today (D51).

**Gains:**
- **Maintainability:** no address arithmetic tied to the PS1, and no alignment demands on the Vita's allocator.
- **Easier threading:** stage 7 needs snapshots that aren't one shared 2 MB block.
- **Memory:** a little.

**Verify:**
- Host byte-identical traces, shots and memory cards, plus save-state round trips.
- **A new cost:** the oracle replay compares RAM at PS1 addresses (`oracle_replay.c`), so it needs an address map. Alternatively, keep a host build with the arena as the oracle configuration: that's the cheaper choice, and it keeps the replay meaningful.

**Size:** ~1–2k lines, spread thin over many files, and decomp patches (`patches/spyro-1-port`).

**Optional:** as a build option (an arena build for the oracle, a native build for the Vita), not at run time.

#### The stage 4 plan (D97, 2026-11-04)

Written on main `e87aabc`, with stage 3's last round (D96, the renderers' mips2c helpers) and D98 (native GTE maths in the shadows, flame and particles) still in flight. What follows is every place the port leans on PS1 addresses or on the arena, the order to take them in, and how each step is checked.

##### What depends on PS1 addresses or on the arena

**1. Where memory is.**

| Site | What it assumes |
|---|---|
| `src/platform/common/arena.c` | linux32: `mmap(0x80000000, 2 MB, MAP_FIXED_NOREPLACE)`. Vita: `memalign(16 MB, 2 MB)`. Both check 16 MB alignment and bit 31. |
| `src/platform/common/psx_arena.h`, `src/compat/port_prefix.h` | `PSX_ARENA(a)` / `PSX_ADDR(a)` = `base + (a - 0x80000000)`; `PSX_KUSEG(p)` maps the game's stores through small integers onto the arena, below 0x10000 onto `g_psx_kernel_ram` (D42; `patches/spyro-1-port/0006`, `0007`). |
| `patches/spyro-1-port/0002`, `0003` | `Terminator_<ovl>` = the overlay's end at a fixed arena address (D20); `PSX_ADDR(0x80200000)` for the top of RAM (`initialization.c`, `4BEF8.c`, `update.c`, `balloonist.c`); `g_OverlaySpacePointer` = `PSX_ADDR(main_BSS_END)`. |
| `src/game/port.c` | the data labels' image copied to their PS1 addresses (`psx_layout`), the scratchpad's data words moved (`s1_scratch_relocs`), the mips2c stack at `PSX_ARENA(0x80020000)`. |
| `src/game/order_table.c`, `src/game/rand_psyq.c` | `PSX_ARENA(0x8006BBE0)` (the OT terminator) and `PSX_ARENA(0x80075AC0)` (PsyQ's `rand` seed): labels named by address. |
| `src/platform/linux32/{pad,vblank,step,tour,main}.c`, `src/platform/vita/vblank.c` | pad buffers, `--poke`, `--peek`, the tour's cheats: PS1 addresses through `PSX_ARENA`, range-checked against 0x80000000–0x80200000. |
| `src/platform/common/walk.c` | the lazy walk's copy of the arena (D71) is a second 16 MB-aligned 2 MB block, because OT links rebuild as `copy | link`. |

**2. The game's data at PS1 addresses (D23, D25).** `tools/data2s.py --map` makes each of the 516 `asm/data` labels an absolute symbol at its `psx.map` address. On linux32 that's inside the arena at 0x80000000. On the Vita, `tools/vita/arena_syms.py` turns every label into `(*NAME__p)`, force-included into all the game's C, and `arena_symbols()` sets the 516 pointers and moves 48 data words at start-up. Every global access in the decomp C on the Vita is therefore a load through a pointer.

**3. Encodings in address bits.**

| Encoding | Writers | Readers |
|---|---|---|
| **24-bit OT links**, `0x04FFFFFF` terminator | `order_table.c`; PsyCross's `setaddr`/`addPrim`; the decomp C's primitives (`draw.c`, the HUD, `flight.c`, `DR_*` packets); every renderer (mips2c and hand C); native draw markers (D82) | PsyCross `nextPrim` = `base \| addr` (`patches/PsyCross/0001`); `walk.c`; `native_draw_check.c:90` |
| **Tags XORed with links**: `(len << 24 \| 0x80000000) ^ next` | mips2c `r_cyclorama.s`, `r_shadows.s`, `r_particles.s` (hint `arena_tags`); `sky_hand.c` (4), `particles_hand.c` (1), `shadows_hand.c` (1) | (the tag word itself) |
| **21-bit packed animation pointers**, and the halved form (Spyro's frames: `(p >> 1) & 0x1FFFFF`) | `loaders.c:291–294, 349–363` | mips2c's rebase after `sll 11; srl 10/11` (`tools/mips2c/mips2c.py`: `r += g_psx_arena_base`); `moby_hand.c:786, 887`; `pete_render_hand.c:210`; `pete_hand.c:150`; `collision_hand.c:302`; `42CC4_hand.c:448, 536`; `native_draw.h:146` (`ND_SIDE_KEY`); `balloonist.c:597–605` (moves a model by a pointer difference) |
| **Bit 31 cleared = SimpleModel** in `g_Models[]` | `loaders.c:315` | `loaders.c:701`; `moby_hand.c:1761`; the mips2c culls `func_8001F158`, `func_800208FC` (`r_moby.s`, D96's) |
| **Bit 31 of a collision pointer** (`m_CollisionModels[i]`, `m_CollisionGroup`) | `loaders.c:324` (the tag comes from the WAD's offset) | `moby_update.c:5054, 17096` (sign tests); `collision_hand.c:260` (`\| 0x80000000` rebuilds the pointer) |
| **Bit 31 as a mode flag on a data pointer** (`D_8006FCF4`, D23) | `r_environment.s` | the hand renderer (`env_hand.c`) no longer does this; whether r_environment's other functions (D96's) do is checked in step 4c |
| **24-bit compares** | — | `loaders.c:1257, 1553` (`& 0xffffff` on `g_Models[i]` and `m_SharedAnimations`): right only while the arena doesn't cross a 16 MB boundary |

**4. Aliases of PS1 RAM in translated code.** `MIPS_KUSEG(a)` (`src/game/mips2c_rt.h`) maps every translated load and store: addresses below 2 MB onto the arena (KUSEG, and the asm's speculative loads through null). On the Vita it also maps `0x80000000 | offset` and the arena's own address with bit 31 cleared (`sll 1; srl 1`), which is why the Vita's arena must sit where no user memory aliases it. The B2 hand renderers inherit it: `HPTR(type, a)` = `MIPS_KUSEG(a)` (`render_hand.h`; `moby_hand.c` 20 uses, `pete_render_hand.c` 12, `sky_hand.c` 6, `env_hand.c` 26 `MIPS_KUSEG`s, the particles, shadows and flame a few each). `MIPS_ARENA_TAG(k)` cancels the arena's top byte in the XORed tags. The scratchpad is the arena's first KB (`MIPS_SCRATCH_HI`, `SCRATCH_BASE`), which needs a 64 KB-aligned base (`lui` + `ori`).

**5. The mips2c machine.** `g_mips_sp` runs an emulated stack inside the arena (`EMU_STACK_TOP` 0x80020000, in the PS1 executable's image, unused by the port), and computed jumps dispatch on PS1 code addresses. Both go with the last mips2c unit (D96).

**6. Whole-arena consumers.**
- **Save states** (`savestate.c`, D51): chunk `ARNA` is the 2 MB block as is; the header records the arena's base, and a state from another base is refused.
- **The presenter's in-between frame** (`present.c`, D30/D70): it snapshots the arena (`s_arena_full`, dirty pages), re-runs the draw path and puts the arena back; `SECTOR_FLAGS` by address. Stage 7 removes the re-run.
- **The oracle replay** (`oracle_replay.c`): emulator RAM images loaded at 0x80000000; RAM compared word by word from 0x80010000; scratchpad words rebased to the arena's first KB; KUSEG pointers counted as "rebased".
- **The shadows and checks:** `hand_shadow.c`, `hand_check.c`, `native_draw_check.c`, and the collision, 42CC4 and gamestates shadows copy and compare the whole arena. These are debug-only, and stay arena-only.

##### The build-time choice: confirmed, with a third configuration

The original plan proposed an arena build for the oracle and a native build for the Vita, chosen at build time. That's right, and the reason is concrete: the replay loads the emulator's RAM, whose pointer words are PS1 addresses. An address map would have to tell which words are pointers, the same thing D51 found impossible for rebasing states.

What the plan lacked is a way to check the native layout as thoroughly as the host checks the arena build. Vita3K captures cover a route of 17 pictures; `tools/host_checks.sh` covers 279 files over 138,000 vblanks. So there are three configurations, all chosen at build time:

| Config | Where | What it is for |
|---|---|---|
| **arena** (linux32 default) | arena at 0x80000000, labels at their PS1 addresses | the oracle replay, mips2c A/B builds (`SPYRO_M2C_FUNCS`, `SPYRO_M2C_REF`), RAM-level comparisons, `--poke`/`--peek` by `psx.map` address |
| **moved** (the Vita today; linux32 with `SPYRO_ARENA_AT`, step 4a) | arena at another 16 MB-aligned address above 2 GB, labels through pointers (D25) | runs the Vita's memory model through the host's checks |
| **native** (`SPYRO_NATIVE_MEM=ON`, from step 4c) | labels as ordinary globals, the game's memory from the heap at any address, no encodings that need a particular base | the Vita's default once proven. **Built for linux32 too**, where pointers are below 2 GB and unaligned, which is a stricter test than the Vita: bit 31 can't hide a tag that was missed. |

Each native step is checked by running `host_checks.sh` on the arena build and on the native build, and the traces, shots, audio and memory cards must be byte-identical. RAM itself isn't compared, because its layout differs. The arena build's own binary must not change in any step that doesn't mean to change it; the `spyro` binary's SHA-1 shows that.

##### Two things the original plan said that this one changes

- **Keep the 24-bit OT links, as offsets into the game's memory.** Don't switch PsyCross to `USE_EXTENDED_PRIM_POINTERS=1`. That adds a word to every primitive, so every writer would need changing: the decomp C through `libgpu.h`, the hand renderers at raw offsets, the records' converter and the presenter's widescreen squeeze. It would also change how much of the primitive buffer each frame fills, which the LOD budget and the hard bounds measure (D49, D82). That's a behaviour change the plan's §4 forbids. A link only has to name a primitive inside one ≤16 MB block, so the native build decodes it relative to the block (`base + ((link - base) & 0xFFFFFF)`) and nothing needs the block aligned. The OT itself goes when stages 2 and 8 replace the walk.
- **Keep the 21-bit animation pointers too, as offsets.** They are the WAD's frame format: the upper 11 bits carry other fields. The native build decodes them relative to the block as well. Only the bit-31 tags really need a new home, because their bit belongs to the pointer.

##### The steps

| Step | What | Waits for | Check | Size |
|---|---|---|---|---|
| **4a** | **The moved arena on the host** (this PR). `SPYRO_ARENA_AT` builds linux32 with the arena at another 16 MB-aligned address, with the Vita's `arena_syms` labels, aliases and `MIPS_ARENA_TAG`. The aliases' regions are reserved, so nothing else can land there (the Vita's guarantee, made explicit). The label set-up moves from the Vita's `main.c` to `port.c`, shared. | — | the arena build's binary unchanged; `host_checks.sh` on the moved build identical to the arena build's; save-state round trips on the moved build; the oracle replay on the arena build; Vita3K shots and the window capture against main's VPK | ~150 lines |
| **4b** | **Name every decode site.** One header (`src/game/psx_mem.h`) with `MEM_OT_NEXT`, `MEM_ANIM21`, `MEM_ANIM_HALF`, `MEM_MODEL_SIMPLE`, `MEM_MODEL_PTR`, `MEM_COLL_PTR`, used at every reader in §3. In the arena and moved builds they expand to today's expressions. | D96 and D98 merged (their files hold sites) | arena build binary unchanged; moved build as 4a | ~200 lines, about 40 sites |
| **4c** | **The native build starts:** `SPYRO_NATIVE_MEM=ON` refuses mips2c units (`SPYRO_M2C_FUNCS`, `SPYRO_M2C_REF`) and links no `mips2c_rt` stack; `HPTR`/`MIPS_KUSEG` in the hand C become plain casts. An audit confirms no hand C relies on a KUSEG alias. | D96 (no mips2c left in the default build) | linux32 native with the arena still at 0x80000000: identical to the arena build | small |
| **4d** | **Any base.** In native, the macros decode relative to the base. `arena.c` drops the 16 MB alignment (Vita: `malloc`), and so does `walk.c`'s copy. PsyCross's `nextPrim` goes through a hook (a new `patches/PsyCross` patch, arena expression kept). `loaders.c`'s `& 0xffffff` compares become pointer compares, in the native build only (a `patches/spyro-1-port` patch). | 4b, 4c | linux32 native with the arena at an unaligned address above 2 GB: identical | ~100 lines |
| **4e** | **The bit-31 tags move to side fields.** SimpleModel: a bit array beside `g_Models` (set where `PatchMobyModelPointers` returns, read at `loaders.c:701`, `moby_hand.c`). The collision tag: a side bit kept with the moby and the model. `collision_hand.c` and `moby_hand.c` stop OR-ing 0x80000000. This is a decomp patch, native build only. New state means a new save-state chunk. | 4d | linux32 native with the arena **below 2 GB**: identical. That is the test that nothing still needs bit 31. | ~150 lines and a patch |
| **4f** | **Data labels as ordinary globals** (native: no `--map`, no `arena_syms`). Primitives that live in labels and are linked into the OT get looked at (the OT terminator is one) and moved into the block. `order_table.c` and `rand_psyq.c` refer to their labels by name. The host's `--poke`, `--peek` and pad-script `poke:` take PS1 addresses through a generated table (PS1 address → label + offset). `tools/savestate_globals.py` checks the labels' section instead of the arena. The presenter snapshots the labels' section beside the block. | 4e | linux32 native: identical; the D51 `savestate_globals` ctest | ~400 lines, mostly tools |
| **4g** | **The game's memory from the heap.** The block becomes what the PS1 has above the executable: overlay space to the top of RAM (`main_BSS_END` 0x8007AA38 to 0x80200000, 1,594,824 bytes), same size, so the loader's budget and contiguity checks keep their meaning. The scratchpad becomes its own static array. `PSX_ADDR(0x80200000)` becomes the block's end, and `Terminator_*` and `g_OverlaySpacePointer` come from the block's base. | 4f | linux32 native: identical | ~150 lines |
| **4h** | **Save states v2** (native): chunks for the labels' section and the block, both bases recorded, the same "only this layout" rule as D51. Version-1 states are refused as "OTHER BUILD". | 4g | round trips on linux32 native (`--state`, `tools/savestate_compare.py`), in Vita3K, a fresh launch loading a state | ~100 lines |
| **4i** | **The Vita on native**, default after Vita3K and a hardware run; the moved configuration stays one switch away until hardware has run native. The arena configuration stays the host's default for the oracle. | 4h | Vita3K shots, window captures and the card identical to the moved VPK; a hardware pair (the owner's yes first) | config |

**Not in stage 4:** the presenter's arena snapshot becomes two regions in 4f and goes with stage 7. The debug shadows stay arena-only.

**Order:** 4a now. 4b–4e follow D96 and D98, and are where the risk is, because they touch every renderer's reads. 4f and 4g are mostly tooling. 4h and 4i close it.

**Status (D97, 2026-11-04): step 4a done.** `-DSPYRO_ARENA_AT=0x8F000000` runs the Vita's moved arena through the host's checks. The moved build gave main's 278 host-check files and memory cards byte for byte. It caught one host tool (`tour.c`) storing through a game pointer as if it were a PS1 address. The arena build is unchanged but for that fix, and Vita3K's 17 shots, window captures and card are identical. Details and logs: D97.

**Status (D100, 2026-11-04): steps 4b and 4c done.** `src/game/psx_mem.h` names every decode site in §3 (and one the table missed, `moby_hand.c`'s face-save list). The arena build is unchanged but for two instruction pairs that compute the same value. `-DSPYRO_NATIVE_MEM=ON` builds linux32 with no mips2c unit, with `HPTR`/`MIPS_KUSEG` as plain casts and no mips2c stack in the arena. A KUSEG audit (`-DSPYRO_KUSEG_AUDIT=ON`) over the host checks found only the three sites 4b names relying on an alias. With the arena at 0x80000000 the native build gave main's 279 host-check files byte for byte, and so did the moved build. Details and logs: D100. 4d and 4e follow in a second PR.

**Status (D100, 2026-11-04): steps 4d and 4e done.** In the native build every packed address decodes relative to the arena's base, and `SPYRO_ARENA_AT` may be any page. PsyCross's `nextPrim` goes through `SPYRO_OT_NEXT` (`patches/PsyCross/0013`). The decomp's 24-bit compares and its 21-bit frame-pointer writers are fixed for any base (`patches/spyro-1-port/0011`). The two bit-31 tags moved to **bit 0** of the same pointer, not to the side fields planned above: the collision tag is copied onto mobys outside `g_LevelMobys`, and bit 0 travels with the pointer, so no new save-state chunk is needed. The check below 2 GB found a third tag the inventory missed, the environment renderer's subdivision lists (header against polygon by the sign), now `MEM_SUBDIV_*`. Native at 0x92F81000 (unaligned, crossing a 16 MB line) and at 0x52F81000 (below 2 GB): main's 279 host-check files byte for byte each; save states round-trip below 2 GB. Details and logs: D100.

**Status (D106, 2026-11-05): steps 4f–4h done on the host.** In the native build the 516 data labels are ordinary globals in one section, `spyro_labels`, laid out as PS1 RAM from 0x8006BBE0 to main_BSS_END, so a PS1 address there is one offset (not the per-label table planned above); PsyQ's `rand` seed is named in its hole. A new audit (`-DSPYRO_MEM_AUDIT=ON`) showed that the OT terminator is the only label anything links to or decodes into. The game's memory is one heap block: overlay space at its PS1 size, behind the rest of main_BSS_END's page, which holds the terminator (0x8007A000–0x80200000, 1,597,440 bytes). The scratchpad is `g_psx_scratch`. Native save states are format 2 (`BLCK`, `LABL`, both addresses in the header), and format 1 is refused as another build. The native build with the labels as globals (block below 2 GB at 0x52F81000), and then with the block from the heap (0x3FBED000), each gave main's 279 host-check files byte for byte; save states round-trip, a fresh launch included. Details and logs: D106.

**Status (D106, 2026-11-05): step 4i done; stage 4 done.** The native VPK gave main's 17 shots, 17 HD shots and memory card on the owner's Vita, and native memory is the Vita's default; `-DSPYRO_NATIVE_MEM=OFF` builds the moved arena. linux32's default stays the arena build, for the oracle.

### Stage 5: native audio (medium, low–medium risk)

**Replaces** (E):
- `libspu.c`'s per-vblank voice model: ADPCM decoded per voice every sample, linear interpolation;
- `xa.c`'s decode on the mixer thread;
- SDL2's audio queue (`vita/audio.c`).

**Method:**
1. Decode each sample to PCM once, when the game writes it to sound RAM (`SpuWrite`, already wrapped for the mixer, D37). ADPCM decode is deterministic, and the loop points and flags are kept.
2. Mix the voices with NEON, at the same pitch steps and ADSR as `libspu.c`.
3. Decode XA to PCM on the read-ahead thread (`xa_ahead.c`).
4. Output through `sceAudioOut`.
5. The PS1's 4-tap Gaussian interpolation (the translation log notes we use linear) becomes possible as an optional *more faithful* mode.

**Gains:**
- **CPU:** *estimate* the mixer's 0.8–1.0 ms per vblank on core 1 roughly halves. It doesn't touch the game thread (0.06 ms).
- **Battery:** a little.
- **Maintainability:** SDL2 can go if the pad goes native too (`sceCtrl`, `sceTouch`). That's 857 KB of code, but a new decision: SDL2 is in the link list from the EDuke32 port's pattern (D7).

**Verify:**
- The host `--wav` byte-identical against today's mixer over the demos and routes. The mix is deterministic in virtual time.
- Vita3K WAV, then listening on hardware.
- The mixer's `position mismatches` stay 0.

**Size:** ~1–1.5k lines.

**Optional:** yes (a `mixer=` value).

**Status (D84, 2026-11-02; D88, 2026-11-03): done, run on hardware; `mixer=3` is the default since D88.** `mixer=3` (4: lockstep) is the native mix:
- SPU RAM decoded at `SpuWrite`, each block keyed by the history it was decoded from;
- the mix voice by voice, NEON on the Vita;
- the music decoded on the read-ahead thread;
- `sceAudioOut` out.

On hardware (R1/R2, D84): the WAVs are byte-identical, and the mixer's CPU on core 1 fell 0.888 → 0.242 ms per vblank (−73%; with the read-ahead and output threads, 1.050 ms plus SDL2's thread → 0.674 ms). But `mixer=3` ran dry more often in play at the route's end (34 against 14 in the last three windows), so the default stays `mixer=1` until that is fixed and the time pair runs again. `SpuWrite`'s decode costs 56–57 ms per sound bank at a load on hardware.
- **The same samples:** byte-identical to `mixer=1` on the host over the demos, gems, routes A/B and a save-state run (`tools/mixer_ab.sh`), and to main's binary. In Vita3K the demo's WAV is the host's with both mixers. ctest `spu_native` checks 20,000 random vblanks, and the NEON path is checked on an arm64 Mac.
- **The time** (Vita3K only, so *estimate* for hardware): the mix's wall time per vblank fell 0.43 → 0.17 ms (−61%). The plan's estimate was "roughly halves" the 0.8–1.0 ms. The XA decode moved from the mixer to the read-ahead thread, which is on core 1 too, so it moves off the mixer's deadline but saves core 1 nothing. Decoding each sound bank once at `SpuWrite` costs the game thread 6–21 ms per load in Vita3K.
- **Not done:** the Gaussian interpolation. The oracle has no audio capture, so nothing could show it to be more faithful (D84).
- **The dry periods (D88, 2026-11-03): understood and fixed, run on hardware; `mixer=3` is the default.** On the device, `mixer=3` ran dry 11 times on the K route (every one where the game stopped: start-up and loads) against `mixer=1`'s 78, with `audio.wav` still D84's R1 (`a4b03472…`), and `SpuWrite` took 1.6–2.0 ms a sound bank instead of 56–57 ms. The game's vblanks follow the display (59.94 Hz), so the mixer makes ~44,056 frames a second against the port's 44,100: both mixers' queues run low by ~1 ms a second once a load's catch-up backlog is gone (R1 and R2 both went dry ~45 s after the route's last load). `mixer=3`'s output ring now holds its low point at 2304 frames by playing within 0.5% of rate 1 (a PI controller; `src/platform/common/audio_ring.c`), refills to 87 ms after running out and drops the excess, so the sound stays in step with the game. `audio.wav` is unchanged (all five `mixer_ab.sh` scenarios byte-identical). A model of the Vita's clocks (`tests/audio`) shows D84's ring padding 150 grains in 10 minutes and D88's none outside loads; Vita3K gives `mixer=3` 5 dry against `mixer=1`'s 5, all at loads.
- **`SpuWrite` no longer decodes (D88).** It only forgets the blocks it wrote; voices decode each block the first time they play it, on the mixer's core. In Vita3K a sound bank's `SpuWrite` fell from 6.4–21 ms to 0.2–0.7 ms on the game thread; the hardware runs below will say what is left of the 56–57 ms.
- **Hardware runs R1 and R2** (`tools/vita/fps-runs/r1-mixer-old.txt`, `r2-mixer-native.txt`; route `tools/vita/hw-routes/r-audio-k.cfg`), each only with the owner's approval (run once, 2026-11-02; results above and in D84). `VPK=` takes an absolute path:
  1. The samples, `realtime=0`: `audio.wav` must be byte-identical between the two.

     ```bash
     VPK=$PWD/build/vita/spyro-vita.vpk PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-r1-wav 1500 @tools/vita/fps-runs/r1-mixer-old.txt @tools/vita/hw-routes/r-audio-k.cfg
     ```

     ```bash
     VPK=$PWD/build/vita/spyro-vita.vpk PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-r2-wav 1500 @tools/vita/fps-runs/r2-mixer-native.txt @tools/vita/hw-routes/r-audio-k.cfg
     ```

  2. The time, in real time and without the WAV: the `mixer N: core 1 CPU per vblank` lines before and after, `position mismatches` 0, and the dry, cut and short-grain counts.

     ```bash
     VPK=$PWD/build/vita/spyro-vita.vpk PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 REALTIME=1 tools/vita/hw_run.sh logs/hw-r1-time 1500 @tools/vita/fps-runs/r1-mixer-old.txt @tools/vita/hw-routes/r-audio-k.cfg wav=0
     ```

     ```bash
     VPK=$PWD/build/vita/spyro-vita.vpk PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 REALTIME=1 tools/vita/hw_run.sh logs/hw-r2-time 1500 @tools/vita/fps-runs/r2-mixer-native.txt @tools/vita/hw-routes/r-audio-k.cfg wav=0
     ```

  3. Listening on the device with `mixer=3`, by hand, in a level with music and effects.
- **Hardware runs for D88** (the same route and configs as R1/R2, on the D88 VPK; each only with the owner's approval; run once, 2026-11-03, results in D88: they passed, so `mixer=3` became the default):
  1. R2's samples on the new VPK, `realtime=0`: `audio.wav` must be R1's from D84 (`a4b03472…`; `mixer=1`'s code is unchanged).

     ```bash
     VPK=$PWD/build/vita/spyro-vita.vpk PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-d88-r2-wav 1500 @tools/vita/fps-runs/r2-mixer-native.txt @tools/vita/hw-routes/r-audio-k.cfg
     ```

  2. The time pair, `REALTIME=1 … wav=0`: the dry and cut counts per window, the `audio N:` lines (the clocks should read about −1000 ppm), and the `SpuWrite` ms at the loads.

     ```bash
     VPK=$PWD/build/vita/spyro-vita.vpk PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 REALTIME=1 tools/vita/hw_run.sh logs/hw-d88-r1-time 1500 @tools/vita/fps-runs/r1-mixer-old.txt @tools/vita/hw-routes/r-audio-k.cfg wav=0
     ```

     ```bash
     VPK=$PWD/build/vita/spyro-vita.vpk PAD=$PWD/tools/vita/hw-routes/k-route.pad DEVICE_DEFAULTS=1 REALTIME=1 tools/vita/hw_run.sh logs/hw-d88-r2-time 1500 @tools/vita/fps-runs/r2-mixer-native.txt @tools/vita/hw-routes/r-audio-k.cfg wav=0
     ```

### Stage 6: native file I/O (small–medium, low–medium risk)

**Replaces** (E): `libcd.c` and `cdimage.c`'s sector-addressed model (LBAs, seeks, `CdControl`, 2352- and 2336-byte sectors), and the XA read-ahead's sector windows.

**Method:**
- The game's `CdRead` of an LBA range becomes a read of a file and offset. `manifest.json` (or the image's ISO directory) maps LBA to file.
- Reads go through `sceIo` async on a worker thread.
- The game still sees `CdReadCallback` and `CdReadSync` complete in the same order, and after at least the same number of ticks (`--cd-latency` keeps that testable).
- Whole-sector overruns stay (`cd.c` copies whole sectors; `docs/porting-plan.md` §4.4).
- XA music becomes a file stream of its interleaved channel.

**Gains:**
- **CPU:** small (the read-ahead's 0.56 ms per vblank is on core 1).
- **Load times:** shorter, *estimate*. The log's `cd:` line gives the file-system ms to compare.
- **Simpler code.**

**Verify:** host traces byte-identical with the same CD latency, Vita3K, then the load-time lines on hardware.

**Size:** ~0.5–1k lines.

**Optional:** yes.

**Status (D93, 2026-11-04): done, run on hardware; `cd_native=1` is the Vita's default (`--cd-native` on the host, off there).** `src/platform/common/cdnative.c` (500 lines) with `psx_cd_locate()` in `libcd.c`: a CdRead is one read of a file at an offset per file it touches, on a worker thread (core 1 on the Vita, where `xa_ahead`'s reader was), completing at the same tick; the XA stream is read ahead from where it starts in a ring of four 32-sector chunks that keep the subheaders and the sectors the filter plays, with the PCM decode for `mixer=3`, in place of `xa_ahead.c`'s windows. Both layouts. The same bytes: `test_cd` compares every sector of the disc and the stream's answers; host checks 287/287 files and the gems route 22/22 identical to main, from the image too; both attract demos tick for tick against the emulator; Vita3K 17/17 shots, the WAV and the card identical from both layouts. File-system calls in Vita3K's gems run: 3,081 → 27 (extracted files), 102 → 38 (image); the game thread's wait for its CdReads 56 → 19 ms (files), 7–11 → 13–36 ms (image, noisy). On hardware (runs V1/V2, the K route from the disc image): the same 18 pictures and memory card; the game thread's wait for the route's 55 CdReads 1,466 → 1,329 ms (−9%), file-system calls 232 → 98; the music's sectors on the game thread 37 → 3. Left: removing `xa_ahead.c` and the sector paths, once `cd_native=1` has been out for a while.

### Stage 7: logic decoupled from PS1 vblank timing (large, high risk, last)

**Replaces** (E):
- `vblank.c`'s model: the game's vblank count follows `sceDisplay`, and `VSync(0)` runs every missed vblank's callbacks (pad, CD, SPU) in a catch-up loop;
- the in-between frame re-running the game's draw path on the game thread (`present.c`, D30, D70).

**Method:**
- **The logic:** a 59.94 Hz clock from `sceKernelGetProcessTimeWide` counts the vblanks the logic sees: the `PadVSync` callback's cadence, `g_UnprocessedFrames`, `g_DeltaTime`'s clamp to 2–4, all unchanged.
- **The presentation:** the logic thread publishes a snapshot per tick: transforms, animation frames, particles, the camera.
- **The drawing:** a render thread builds stage 2's native batches from the last two snapshots at the display's rate.

That needs everything shared with the game thread to be gone or copied: the GTE state (stage 1), the primitive buffers (stage 2), the arena (stage 4).

**Gains:**
- a true 60 fps: the in-between frame no longer costs the game thread a second draw pass (C1: 3.3 + 2.1 + 2.6 + 0.5 ms, D70);
- steadier pacing;
- the catch-up loop goes.

**Verify:**
- `realtime=0` (virtual vblanks) stays exactly today's for scripted runs.
- Both attract demos tick for tick against the emulator.
- Host routes byte-identical.
- On hardware, the `present` and `vblank` lines' late counts.

**Size:** ~2–4k lines.

**Optional:** yes, the presentation modes (`interp=`), as today.

**Status (D127, 2026-11-09): 7a, 7b, 7c, 7d, 7d2 and 7e done; 7c's `present_due=1` the Vita's default since its hardware pair 7C1–7C4. 7d–7g go ahead (the owner's decision, D119), planned again for today's code with a new step 7d2 ("The plan refreshed (D119)", below); 7c's pictures checked on hardware (7P1/7P2, no fault). Two pieces of 7f and 7g that don't wait for 7d are done ahead (D122): the walk's state per instance, and the HUD's narrowing in the walk's conversion. 7d's presenter builds and `--render-ctx-check` are in (D123). 7d2 is done behind `fast_render=3` (D124): the readable renderers, the presenter on the same code; the default stays `fast_render=2`; its hardware pair 7D2a/7D2b ran (identical pictures, the renderers 5% lighter), and `fast_render=3` is the Vita's default since 2026-11-09 (the owner's decision); since D127 the register-file builds are gone from both builds (`fast_render` is 3, or 0 for the arena build's mips2c units) and 3 is the host's default too. 7e's presenter draw is in behind `interp_mode=2` (D125), run in parallel with 7d2 (the owner's decision, 2026-11-08); it draws at `fast_render` 2 and 3 alike. 7f is done (D126): on the Vita the presenter draws on a thread of its own beside the logic draw; on hardware (7F1–7F5) the game thread at 60 fps is near its 30 fps busy time in play, and `interp_mode=2` is the Vita's default since 2026-11-09 (the owner's decision). 7g's part 1 is done (D128): every gamestate's draw is in the presenter build, and the presenter draws every in-between frame and the attract demos' 16:9 frame; part 2 retires D30's pass (D129).**
- **7b:** `src/platform/common/libetc.c` is the port's `VSync`, `VSyncCallback` and `ResetCallback`, in place of PsyCross's `LIBETC.C`. `clock.c` holds the vblank count and the Vita's catch-up arithmetic for both platforms, and `tests/vcount` tests that code itself. Host checks 287/287, both demos tick for tick, Vita3K 17/17 shots, 17/17 captures and the card identical to main's VPK.
- **7a:** `--present-audit` and `--present-reads` on the host. The findings are below ("What 7a found (D104)").

#### The stage 7 plan (D101, 2026-11-04)

Written on main `82d05ce`, while stage 2's last piece (D99, the records straight to vitaGL) and stage 4's steps 4b–4e (D100) are in flight. It changes no engine code. It covers four things:
- everything that ties the game's logic and its pictures to the PS1's vblank today;
- what stage 7 keeps and what it moves;
- the steps;
- how faithfulness stays testable.

Measurements are cited; *estimates* are marked.

##### What ties the game to the vblank today

**1. The game's own clock** (the decomp; paths under `third_party/spyro-1/`).

| Site | What it does |
|---|---|
| `src/main.c` `main` | One tick: `GamestateUpdate` with `g_PadMutex = 0`; then `g_DeltaTime = g_UnprocessedFrames`, clamped to 2–4, and `g_UnprocessedFrames = 0`; then `GamestateDraw` (`PortPresentDraw` in the port, `patches/spyro-1-port/0005`). |
| `src/gamepad.c` `PadVSync`, installed by `VSyncCallback(PadVSync)` (`initialization.c:101`) | The vblank callback. It adds 1 to `g_LevelTicks`, and to `g_CDReadTime` while a load has a time limit. Then it reads the pad, buffers up to 4 vblanks of input (`m_BufferedInputs`) and adds 1 to `g_UnprocessedFrames`. In attract demos it returns before the pad, and `PadDemoUpdate` sets `g_UnprocessedFrames = 2` instead (`gamepad.c:214`: no lag frames in demo recordings). |
| `src/gamestates/draw.c` `GamestateDraw` (play), the other draws (`func_8001A050`, `func_8001A40C`, `func_8001C694`, …), and the title (`titlescreen.c:1440`) and flight (`flight.c:603`) overlays | The 30 Hz pacer. After `DrawSync(0)` it calls `VSync(0)` until `VSync(-1)` is 2 past the last frame's (`D_80075950.pre - .post < 2`), then `PutDispEnv`, `PutDrawEnv` and `DrawOTag`. A tick that took longer doesn't wait. |
| `initialization.c:268–377`, `flight.c:240–245` | Fixed waits in vblanks, such as the boot logos' 210. |
| `src/cd.c` `CDLoadTime` | A load with a limit (`g_CDMaxReadTime`) restarts its read when `g_CDReadTime` passes the limit. |
| `music.c:51`, `moby_update.c:26214`, `draw.c:275` | `g_LevelTicks` read: the music's 8-minute easter egg, a colour pulse, a rotation. |

So the logic's time is the number of `PadVSync` calls between two loop tops, clamped to 2–4. A late tick moves the game 3 or 4 vblanks, as on the PS1.

**2. The port's `VSync`.** PsyCross's `VSync` (`LIBETC.C`) is `PsyX_WaitForTimestep(1)` for `VSync(0)` and `PsyX_Sys_GetVBlankCount()` for `VSync(-1)`. `VSyncCallback` stores the callback.

| | Host (`src/platform/linux32/vblank.c`) | Vita (`src/platform/vita/vblank.c`) |
|---|---|---|
| `VSync(0)` (`PsyX_WaitForTimestep`) | `host_vblank()` once: virtual time, nothing on a timer (D17) | `realtime=1` (the default): waits for the display's next vblank (`sceDisplayWaitVblankStart`) if early; if late, runs every vblank it missed, at most `MAX_CATCH_UP` (8; the rest are dropped, after loads); then `present()` once (D28). `realtime=0`: one vblank and a present, as on the host, for scripted runs. |
| one vblank (`host_vblank`, `vblank`), in order | pokes, cheats, the pad script, `psx_cd_vblank`, `psx_spu_vblank`, the game's callback, the 960 presentation frame, shots and peeks, the trace | the watchdog, the pad, `psx_cd_vblank`, `vita_mixer_vblank`, the game's callback, profiling, pokes, shots, the stats lines |
| the clock | the number of calls | `sceDisplayGetVcount`, 16 bits on hardware, widened by `vcount.h` `vita_vcount_widen` (D90). Counted afresh after the options menu and after a save-state load (`s_vcount_synced`). |

So every callback runs on the game thread inside a `VSync(0)`, in a burst after a late tick, and never asynchronously. That keeps runs reproducible, and it makes the hand-off that `g_PadMutex` guards on the PS1 trivially safe.

**3. What else counts vblanks.**
- **The CD:**
  - `libcd.c` `psx_cd_vblank` ticks a pending `CdRead` (its latency) and moves a ReadS stream's head at 150 or 75 sectors a second (`VBLANK_HZ` 60), handing the sectors it passes to `xa.c`.
  - The latency is 0 on both builds (`vita/main.c:594`; the host's default), so reads complete inside `CdRead`. That's because `CDLoadSync` busy-waits without calling `VSync` (D17).
  - `--cd-latency N` above 0 stops the host at the first `CDLoadSync`, with or without `cd_native` (D93); `test_cd` covers latency instead.
  - `cd_native` (D93) changes only how the bytes arrive, at the same ticks.
- **The SPU:** 735 samples a vblank (`psx_spu.h` `PSX_SPU_VBLANK_HZ` 60). The mixer thread replays one packet per game vblank (`mixer.c` `vita_mixer_vblank`, D37, D84). The Vita's vblanks are the display's 59.94 Hz, so `audio_ring.c` plays within 0.5% of rate to hold its level (D88).
- **`g_LevelTicks` and `g_CDReadTime`** count only the vblanks the port runs: none during a host load, at most 8 after a Vita load. On the PS1 the interrupt counts every vblank of a load. Since loads complete inside `CdRead`, `CDLoadTime`'s limit never fires in the port. That is today's behaviour, and stage 7 doesn't change it.

**4. How 60 fps works today (D30, D44, D68, D70, D75).**
- **The last tick:** `PortPresentBeforeUpdate` (`present.c`, at the loop top) keeps the last tick's camera, Spyro, mobys (position, rotation, animation track) and particles (`s_prev`).
- **`PortPresentDraw`,** in place of `GamestateDraw`, runs four steps:
  1. `snap_take()` copies what the draw path writes that the game reads again: the scratchpad; the data and bss (0x8006BBE0–0x8007AA38); the moby array; the particle space; each sector's bytes 0x18–0x1B; the GTE registers; PsyCross's draw and display environments; and `g_mips_v1`.
  2. `blend_state()` moves the camera, Spyro, the mobys and the particles halfway back to the last tick, in place.
  3. `presentation_pass()` runs the whole `GamestateDraw` again with `VSync` frozen (`pass_vsync`), SPU writes dropped (`g_psx_spu_frozen`) and VRAM writes frozen (`g_psyxVramFrozen`; our libgpu's `g_psx_vram_frozen` since D115), and captures its picture.
  4. `snap_restore()`.

  Then the logic pass runs: the game's own draw.
- **Attract demos** (and `widescreen_exact`) get a second presentation pass, which draws the 16:9 picture while the logic draws a hidden 4:3 frame, so the recorded input replays (D30).
- **The queue:**
  - Each pass's picture goes into a ring at its `DrawOTag` (`push_capture`). `present()`, inside `VSync(0)`, shows one per call (`port_present_frame_slot`).
  - Real frames show on the same vblanks as at 30 fps (byte-identical by md5, `docs/presentation.md`). The in-between frame shows one vblank before its real frame.
- **The safety net** (`interp_budget`, D68, D75): if the game thread's CPU per tick, in-between frame included, would pass 31.3 ms, the tick shows its real frame on both vblanks. Overload on the GPU is dynres's to handle (D75).
- **What it costs on hardware** (X7's `present` lines, `logs/hw-x7` of the ngte-last-renderers worktree, gitignored; windows with 140 or more in-between frames, sorted by the last `where` line):

  | Part (windows) | Pass, ms per tick (max) | Snapshot and restore | Logic draw |
  |---|---:|---:|---:|
  | title (17) | 3.88 (12.97) | 0.44 | 3.43 |
  | Stone Hill attract demo (5), two passes | 11.61 (13.82) | 0.79 | 5.20 |
  | Artisans (19) | 3.41 (7.27) | 0.43 | 3.51 |
  | Toasty (25) | 4.69 (6.38) | 0.47 | 4.64 |

  In Toasty the game thread is 21.5% busy at 30 fps (X6) and 39.5% at 60 (X7).
- **Why the pass re-runs the renderers** (D70): nothing 3D reaches PsyCross.
  - The renderers hand on primitives already projected, culled at the screen's edges and by `NCLIP`, and depth-sorted into the OT.
  - Re-using one tick's geometry for another leaves gaps where that tick culled: every edge when moving forward, the trailing edge when turning. It also leaves holes at silhouettes.

**5. The threads around it.**
- **The render thread** (`render_thread=1`, `gpu_thread.c`, D63) runs every GL call on core 2, fed by commands copied out of the game.
  - Whether a vblank shows a new picture is decided on the game thread. It posts a present only once the previous one has run, so a slow render thread makes the game wait in `VSync`.
  - In the Stone Hill attract demo's flight at 60 fps, the game waited "previous frame not shown yet" 81–121 times a window (D81). There the render thread is the wall: 15.38 ms a picture (X7).
- **The parse thread** (`parse.c`, D71) walks a copy of the OT on core 1, or on core 3 when its start-up check passes.
- **The mixer and the CD worker** run on core 1.

##### What stage 7 keeps and what it moves

**Kept, because it's the game's clock:**
- **The callbacks per vblank:** on the game thread, inside `VSync(0)`, in today's order, with the catch-up after a late tick. That is the PS1 interrupt's count, and `g_DeltaTime`'s 3 and 4 come from it.
- **Virtual time:** `realtime=0` and the host keep one virtual vblank per `VSync(0)` (D17).
- **The Vita's clock:** the display's widened vcount (D28, D90).
- **The CD and SPU models on game vblanks:** that keeps the WAV byte-identical, and every CD tick and the XA stream's position deterministic.
- **`MAX_CATCH_UP` and the loads,** unchanged (point 3 above says what they count differently from the PS1).

**Moved:**
- **Presentation leaves `VSync`.** The render thread shows pictures at display vblanks. Each frame is tagged with the vblank it's due at, the same vblanks as today. The game thread never waits for a present.
- **The in-between frame leaves the game thread.** A presenter draws it on another core from snapshots of ticks N−1 and N, with the game's own renderers made re-entrant, building stage 2's records. The game thread no longer snapshots, blends, re-runs `GamestateDraw` and restores.
- **The real frame stays the logic pass's own picture,** as today.

##### Three things the original plan said that this one changes

- **The catch-up loop stays.** The original plan said it would go. But it is the PS1 interrupt's model: a tick late by a vblank must see `g_UnprocessedFrames` at 3, `g_LevelTicks` one higher, and the CD and the SPU a vblank further on. What goes is presenting inside it.
- **The clock stays the display's vcount, not `sceKernelGetProcessTimeWide`.** The display flips on its own vblanks, and the audio ring already follows them (D88). A second clock would drift against the flips.
- **Neither interpolating snapshots into every picture at the display's rate, nor interpolating draw records.**
  - **The display's rate:** the Vita's display runs at 59.94 Hz, so 60 fps means one in-between frame a tick, as today.
  - **Draw records:** a record (D91: the primitive's own words, a link and a kind) is D70's screen-space output. Interpolating records between ticks means matching primitives that exist in only one tick (culled, clipped, back-facing, another LOD), in an OT order that changes. And screen positions don't interpolate the way the 3D points they came from do. The result would be a different picture from today's in-between frame, with no exact reference to check it against.
  - **Reprojection** would project tick N's vertices again and keep tick N's culls and order. It would cost less CPU (*estimate*: projection is only part of the renderers' cost), but it leaves D70's gaps at the trailing edge and at silhouettes.

  Neither is planned. If 7f's presenter doesn't fit on its core, reprojection is the fallback to try, as an option and with the owner's decision, because it changes the in-between picture.

##### What it needs from stages 1, 2 and 4

**From stage 2's direct path (D99, session `stage2-direct-gl`):**
- **A frame is a self-contained list:** its records, the leftover primitives (lines, fills, the HUD's 2D), its draw environment (clip, offset, background colour) and its texture uploads. It reaches the render thread as one unit, and drawing it reads no PsyCross state. Then two producers, the logic pass and the presenter, can submit frames independently, and the render thread can skip a frame's draws while still running its uploads (7c).
- **At least three pools,** so the presenter can write one frame while the render thread reads another.
- **Every primitive of a play frame reaches GL without PsyCross's walk.** Otherwise the presenter's frames would need PsyCross, which is one global state (its environments, its splits, `activeDrawEnv`) shared with the logic. The HUD is the case to settle (7a).
- **Presents decided per frame, not per `VSync`.** The capture ring (`g_present_capture`, gpu=1) can go once the in-between frame is drawn straight to the back buffer.

**From stage 4 (D100, session `stage4-decode-sites`, then 4f–4g):**
- **4c's native build,** where `HPTR` and `MIPS_KUSEG` are plain casts. A KUSEG alias maps through the one arena, so a renderer that reads through it can't be pointed at a snapshot.
- **4b's named decode sites,** so the re-entrant renderers decode the 21-bit animation pointers and the model tags in one place.
- **4g's scratchpad as its own array,** or 7d gives each context its own. The B2 renderers keep working state in the scratchpad and the spill area (`render_hand.h`): that's what is left of stage 3's aim.
- **4f's labels as ordinary globals.** The snapshot is then the named globals plus the moby array and the particle space, not arena regions. `present.c`'s region snapshot becomes two regions in 4f (D97) and goes, for play frames, in 7f.

**From stage 1:** the main renderers' GTE is native maths on C values (D80). The presenter's path must not replay into the global register file (`gteRegs`), which belongs to the logic. D95's shadows, flame and particles need D98's native variants there (`ngte_fx=1`'s code). Those are a few µs per vblank slower (D98), which doesn't matter off the game thread.

##### The steps

| Step | What | Waits for | Check | Size |
|---|---|---|---|---|
| **7a** | **Host audits, debug only.** (1) **The read set:** which game memory the in-between pass reads that the next update writes, beyond `s_prev`'s transforms. Method: a page-protect read trace of the pass on linux32, against the pages each update dirties (as `--present-dirty` does for writes). Known already: the environment animations rewrite texture V coordinates and sector bytes 0x18–0x1B (`env_update_hand.c`). (2) **The HUD:** is the in-between frame's HUD list byte-identical to the real frame's of the same tick? It draws tick N's HUD both times (*to check*). If so, the presenter can reuse it. (3) **Coverage:** in-between frames per gamestate on the demos, routes A and B and the tour, and what each one's pass costs. | — | none (it measures); logs in `logs/` | ~200 lines, debug only |
| **7b** | **The port owns `VSync`:** `VSync`, `VSyncCallback` and `ResetCallback` in our own C, over today's platform hooks, with today's semantics. One clock module, shared by linux32 and the Vita's `vblank.c`. `PsyX_WaitForTimestep` and `PsyX_Sys_GetVBlankCount` stop being called. | — | `host_checks.sh` byte-identical; Vita3K shots, captures and card identical; `tests/vcount` | ~150 lines |
| **7c** | **Presentation leaves `VSync`.** Frames carry their due vblank. The render thread shows the one due at each display vblank. The host and `realtime=0` show it inline at each virtual vblank, so runs stay deterministic. The game thread never waits for a present. When the render thread falls behind, it skips in-between frames' draws, then real frames' draws once the pools are full, always keeping their uploads; the `present` line counts the skips. Shots come from the frame due at their vblank. | D99 | host byte-identical (shots by vblank, with and without `--interp`); Vita3K `realtime=0` shots and captures identical to main's VPK; on hardware (the owner's yes first): the K and Toasty routes at 60 fps, late vblanks and the game thread's waits against X7 | ~300–500 lines |
| **7d** | **Re-entrant renderers.** The environment, the sky, the three moby renderers, Spyro, the shadows, the flame and the particles take a context. It holds the GTE state as C values (no register-file replay in a presenter's context), their scratchpad and spill area, their side-effect outputs (sector bytes 0x18–0x1B, the sector flags `D_800771C8`, `m_WasDrawn`, the animation queues) and their record pool. The logic's context is today's globals, so its calls compute the same. | 4b, 4c; 4g or a scratchpad per context | `--fast-render-check` and `--native-draw-check` with 0 differences on the demos, gems, wsi, lod, routes A and B and the tour; `host_checks.sh` byte-identical; Vita3K identical | *estimate* ~1.5–2.5k lines over 9 files |
| **7e** | **The presenter's draw for play and the attract demos,** on the host first, still on the game thread. Hand C for `GamestateDraw`'s play branch, working from a snapshot: both ticks' transforms, blended as `blend_state` blends them; the camera matrices as `CameraUpdateMatrices` makes them, 16:9 narrowing included; the draw distance as `lod.c` sees a pass. It calls 7d's renderers with a private context; the HUD follows 7a's answer. It covers the attract demos' 16:9 pass too. Behind a switch (`interp_mode=2`); D30's pass stays as the reference, and as the fallback for other gamestates. | 7a, 7d, D99 | every tick, the host compares the presenter's frame with D30's pass's, record for record (a check mode like `--native-draw-check`), on the demos, gems, wsi, routes A and B and the tour; with it on, traces, WAVs and cards identical to `--interp` off, and shots identical to D30's | *estimate* ~1–2k lines |
| **7f** | **The presenter on its own core.** The snapshot is published after each update. A worker draws the in-between frame on core 1 (or core 3, after D71's start-up check) while the game thread runs its logic pass. A frame that isn't ready by its due vblank isn't shown: the real frame stays up, as with a budget skip. In play the game thread no longer runs `snap_take`, `blend_state`, the pass or `snap_restore`, and `interp_budget` goes for play. | 7c, 7e | the host as for 7e (it runs the worker inline); Vita3K `realtime=0` shots and captures identical to D30's on the same VPK; on hardware (the owner's yes first): X6 and X7's Toasty route and the K route at 60 fps, for the game thread's busy %, late vblanks and the presenter's misses | ~300–500 lines |
| **7g** | **The other gamestates, then D30's pass retires.** In the order of 7a's counts: the pause and inventory screens (`func_8001A40C`), the transitions, the fairy, the cutscenes, the title. Each gets the presenter, or keeps D30's pass where that is cheap (the pause screens, D70). Once no gamestate needs it, the arena snapshot, `pass_vsync`, `g_psx_spu_frozen` and `g_psyxVramFrozen` go. | 7f | as 7e, per gamestate; the pause route's shots in Vita3K (D70) | ~0.5–1k lines |

All told *estimate* ~4–7k lines, more than the ~2–4k above: the re-entrant renderers and the presenter's draw weren't counted there.

**7d–7g as D101 wrote them are superseded** by "The plan refreshed (D119)" below, which keeps their aims and checks and changes how 7d and 7e are built. It also adds 7d2.

**Order:**
- **7a and 7b now:** both are small, and neither waits for anything.
- **7c after D99.**
- **7d after 4c,** in parallel with 4f–4i.
- **7e and 7f** are where the risk is, and the payoff.
- **7g** closes the stage.

##### What 7a found (D104, 2026-11-05)

From `host_checks.sh`'s four runs with `--interp --present-audit --present-reads` (`logs/s7a-*`, D104's table). The traces and cards are identical to the runs without the audits.
- **Coverage:** 69,468 in-between frames. By share: play 45.8%, credits 18.9%, title 14.1%, cutscenes 8.9%, pause 4.2%, inventory 4.1%, level transitions 1.5%, dragon 1.0%, balloonist 1.0%, fairy 0.3%, exits and entrances 0.1%.
  - The runs over-weight the title, the opening cutscene and the credits; a player's session is mostly play.
  - Every gamestate's pass costs the same order as play's (qemu: 4–27 ms against 15.6). The pause screens cost 61% of play's, so keeping D30's pass there (7g) saves code, not CPU.
- **The HUD:** byte-identical to the real frame's in every in-between frame except 22 of the dragon cutscene's 695.
  - Those are the dragon rescue's ring (`func_80058864`): 3D triangles projected through the camera into the HUD OT, so the blended camera moves them a pixel.
  - The presenter can take the HUD from the logic pass of the same tick. The dragon cutscene keeps D30's pass, or the presenter draws the ring itself.
- **The read set in play:**
  - 73 globals, about 380 words a tick (13 KB of symbols, against the 61 KB data region today's snapshot copies).
  - 16 of them, about 3.9 KB, change before the next draw: `g_Spyro`, `g_CurDB`, `D_80075950` and `D_80075858` every tick; `g_Camera`, `g_LevelTicks`, `g_Hud`, `g_Sparkles`, `g_Glows`, `g_SpyroFlame`, the sector flags `D_800771C8` and five more on some ticks.
  - Outside the globals: the moby array (about 160 bytes a tick change in the pages read), the particle space (about 760) and the animated level data (about 150 bytes in 3 pages: D101's texture V coordinates and sector bytes).
  - The title, the cutscenes and the credits read 34–40 globals.
  - Not seen by the trace: the scratchpad array, the GTE registers, PsyCross's state and C statics, which 7d's context holds.

**What it changes:**
- **7e's snapshot is a named list,** per gamestate from `tools/present_reads.py`, not a region copy.
- **7g's order** is the coverage order above.
- **Stage 8** needs less from stage 7. `g_psyxVSyncHook` is now our `libetc.c`'s, so only `g_psyxVramFrozen` and the `DrawOTag` hooks remain.

**The call on 7d–7g:** they wait.
- **For them:** the presenter needs much less than feared.
- **Against them:** 60 fps holds in play (D81), the attract demo's misses are the render thread's, and 7d–7f is *estimate* ~3–5k lines in the renderers for game-thread headroom.
- **What would start them:**
  - stage 8 wanting `g_psyxVramFrozen` gone;
  - a heavier mode running the game thread late at 60 fps;
  - 7c's hardware pair showing game-thread waits that 7c doesn't remove.
- **Then the first piece** is the snapshot from this list, checked against D30's pass, before the re-entrant renderers.

##### What 7c did (D112, 2026-11-06)

- **Frames carry their due vblank** (`present.c`): the queue is taken one frame per vblank, so a frame filed at count c behind k waiting ones is due at c + 1 + k. A full queue drops its oldest frame and moves the rest up a vblank, and the frame filed names the one dropped for it. The host takes the newest frame due at each vblank (`port_present_frame_due`), the same frame as before: host checks 279/279 plain and 279/279 with `--interp`, and the widescreen attract demos 50/50.
- **`present_due=1`** (the Vita, real time, render thread):
  - The game thread posts only the PS1 display's pictures and never waits for a present.
  - The render thread shows the newest frame due when it files a frame or runs out of work, and between draws only a frame a vblank late.
  - A frame whose draws start a vblank late has them skipped, keeping its uploads and clears: in-between frames always, real frames when a later one is filed or the game waits for the pools.
  - While the game waits for the ring or a slab, the render thread shows at most a picture every two vblanks.
  - The `present N:` line has a `7c:` part.
- **Off at first:** `realtime=0` and `present_due=0` are main's path. Vita3K `realtime=0` shots, captures and card are identical to main's VPK. Vita3K's real time can't judge the timing (a swap costs its render thread a whole vblank, and runs of one VPK differ 3×), so the hardware pair decided the default: `present_due=1` on the Vita since (the owner's decision after 7C1–7C4).
- **On hardware** (7C1–7C4, the owner's yes, 2026-11-06; X7's settings at 60 fps, `present_due` 0 against 1 on one VPK): all four to `frames=`, no crash.
  - The Stone Hill attract demo's flight: 3.2 and 3.8 late vblanks per 300 with `present_due=0` (worst 13 and 14), **0 with `=1`**. The game thread's wait for the previous present, 612–627 ms a window, is gone in every window.
  - In play (Artisans, Dark Hollow, Toasty) late vblanks stay where they were (0.0–0.6 per 300), and the loads the same (66–79 late vblanks in all).
  - These routes take no shots, so the pictures weren't compared on hardware; Vita3K's `realtime=0` shots and captures are main's.
  - The render thread showed 99.9% of its frames at their vblank; it skipped 4 and 11 frames' draws in the two runs (one of them a real frame's), and never a PS1 display picture. The frames never shown are the ones the full queue drops in the gamestates that tick every vblank, as on main.
- **Size:** ~670 lines with comments, about a third over the estimate. Most of it is the render thread's bookkeeping, plus the drop and hold rules Vita3K showed were needed.

##### The plan refreshed (D119, 2026-11-07)

The owner asked on 2026-11-07 for 7d–7g to go ahead ("lets kick off another session to finish off the remaining stage 7 and other pieces for native"). That overrides D104's and D112's "wait". This is D101's plan checked against main `a47a30b`, after stages 4 and 8. D119 has the reasons in full.

**What is different from D101's day:**
- **Every dependency is met:**
  - D99's direct vertices, and 7c's frames, which carry their due vblank;
  - stage 4 (`HPTR` a plain cast in the native build, the decode sites named, `g_psx_scratch` its own array, the labels ordinary globals);
  - stage 8 (the GPU state, the walk, libgpu, libgte and the register file are ours). PsyCross's global state no longer stands in the way.
- **The register file is still exact on the logic path:** the renderers' native maths leave `gteRegs` as the register-file code would (`NGTE_TAIL`, `NgteSync`, `render_hand.h`), and the effects stay on the register file at the Vita's `ngte_fx=0`.
- **The renderers' state, by name:**
  - `SCRATCH_BASE` (29 uses in eight files);
  - the spill area `D_80077DD8`;
  - the hand stack under `g_mips_sp` (`particles_hand.c`);
  - the sector flags `D_800771C8`;
  - the sector cache in bytes 0x18–0x1B of each sector header (`env_hand.c`, the only state reached through level data);
  - `m_WasDrawn` in the moby array;
  - `g_nd`'s record pool;
  - `g_CurDB`'s OT and primitive buffer.
- **The decomp's C** issues no GTE command and doesn't touch the scratchpad. It reaches the GTE only through libgte. So the scratchpad, the spill area and the data registers belong to the hand C alone.

**The steps** (in this order, each its own PR; the owner merges each before the next builds on it):

| Step | What | Check | Size |
|---|---|---|---|
| **7d** | **Done (D123):** the check found two more writes into shared data (the moby faces' replacements, the environment's animations into a sector's geometry) and five game globals the draw writes; the presenter build has its own of each. **Re-entrant renderers by a second build.** Each renderer file gains `NAME_hand_pres.c` (beside `_ngte` and `_ndr`): the native-maths code, effects included, with the state above mapped by name onto one presenter context (`render_ctx.h`). The context holds its own register file (set from the logic's control registers), scratchpad, spill area and hand stack, sector cache and flags (`SEC_CACHE()` in `env_hand.c`), and record pool, OT and primitive buffer. **The logic's build is unchanged byte for byte**; a context parameter would change the game thread's codegen. One presenter, so one static context. | new `--render-ctx-check`: at each renderer call of the logic draw, the presenter build runs from the same inputs; primitives, records, side outputs and the whole register file compared, the live state put back; 0 differences on the demos, gems, wsi, lod, routes A and B and the tour. `--fast-render-check`, `--native-draw-check`, `--walk-check` 0 differences; `host_checks.sh` byte-identical; Vita3K identical | *estimate* ~0.8–1.3k lines with the check |
| **7d2** | **Done (D124): `fast_render=3`, the Vita's default since 2026-11-09 (the owner's decision); on hardware (7D2a/7D2b) identical pictures and the renderers 5% lighter.** Each B2 file gains `NAME_hand_rc.c`; in it any GTE data register, FLAG or command, the scratchpad, the spill area or the hand stack fails to compile. The poison audit found nothing read across calls; the one data-register read is Spyro's colour filter's code byte (invisible, unreached). **The renderers' hand C to readable C (B2 → B1), by D87's rule** (collision D83, math and `42CC4.s` D87). Control registers a renderer sets stay set. The data registers it leaves go, with the `NGTE_TAIL` register-file steps and the `NgteSync` replays. Scratchpad state becomes locals or named fields where no call reads it after (a poison audit shows which bytes cross calls). The spill area and the hand stack go in the native build. Behind `fast_render=3`; the presenter build takes the same code, so its context has no register file. | the oracle replay on the arena build with D83/D87-style shadows (each call against the mips2c unit: RAM but the scratchpad, and the control registers); `host_checks.sh` byte-identical with `fast_render=3`; `--render-ctx-check`; Vita3K identical; on hardware (the owner's yes first) the K route with every layer on the game thread, for time and pictures. Then the owner decides the default, and the register-file builds can leave the native build. **Done:** the default (2026-11-09), and the register-file builds left both builds (D127, the owner's choice of 2026-11-09 over leaving the arena build's) | *estimate* ~1–2k lines changed, mostly deletions |
| **7e** | **Done (D125):** `draw.c`, `camera.c`, `environment.c` and the eight files of ours the play branch reaches, built again into one object whose only global is `GamestateDraw_pres`; a snapshot of 37 labels in a mirror of the data region, the mobys and particles copied and blended, six kinds of pointer rebased (the audit's); `--pres-check` 0 differences on every scenario, every file D30's with `interp_mode=2`; `pres_link`. **The presenter's draw: the decomp's own draw code, compiled a second time.** `src/gamestates/draw.c` and its callees are built again with `-include` of a names header. Each global they read maps onto the snapshot's copy, each renderer onto its 7d build, and D30's hooks onto stubs (`VSync` doesn't wait; `Spu*`, `LoadImage`, `StoreImage`, `MoveImage` dropped). No decomp file is edited. The frame is D30's pass's by construction, HUD included, so the HUD reuse and the dragon ring's exception go. The snapshot: 7a's read globals (`tools/present_reads.py`), plus the moby array and the particle space copied and blended in the copy, with the pointers into them rebased (an audit finds which globals hold such pointers). A link check keeps the presenter's objects off any writable global outside the snapshot, the context and the read-only level data. On the game thread, at D30's pass's point, behind `interp_mode=2`; D30's pass stays as the reference and fallback. The callees are inventoried from the link map: those touching no global are shared. | every tick on the host, the presenter's frame against D30's pass's, record for record, on the demos, gems, wsi, routes A and B and the tour; with it on, traces, WAVs and cards identical to `--interp` off, shots identical to D30's; the link check | *estimate* ~0.8–1.5k lines |
| **7f** | **Done (D126):** its output calls and walk its own (a second `PsxGpuWalk`, its own records' pool), on the Vita a thread of its own (`pres_thread=1`, core 1) recording into three buffers the render thread runs at the game thread's `C_BUF`, the join before the update; the logic draw's face replacements into a copy while it runs; the host inline, every check 7e's; Vita3K D30's; hardware 7F1–7F5: Toasty's game thread 41.3% → 24.9% busy at 60 fps, Dark Hollow 61.0% → 38.0%. **The presenter on its own core.** Its frame is recorded into its own command buffer and handed whole to the render thread (today the game thread is the command ring's only producer). The walk of its leftover primitives runs with its own walk state (**done ahead, D122:** `gpu_walk.c`'s state is a `PsxGpuWalk`, the game thread's one static instance; the second instance and its producer are still 7f's). It starts after update N, beside the logic draw N. If it is still running when update N+1 starts, the game thread waits: the level data's animated bytes change in the update, and the wait keeps the frame exact with no copy. A frame not ready by its due vblank isn't shown (7c's rules). In play `snap_take`, `blend_state`, the pass and `snap_restore` leave the game thread, and `interp_budget` goes for play. **First, merge main after the session that stops the render thread before the `frames=` exit** (D118's dump). | the host as for 7e (the worker inline); Vita3K `realtime=0` shots and captures identical to D30's on the same VPK; on hardware (the owner's yes first): X6 and X7's Toasty route and the K route at 60 fps, for the game thread's busy %, late vblanks, the presenter's misses and the join's wait | *estimate* ~0.6–1k lines |
| **7g** | **Part 1 done (D128):** the presenter build holds every gamestate's draw (the rest of `draw.c`, `credits_hand.c`, `spu.c`, `completion.c`, and the title's, flight levels' and balloon levels' overlay draws, those the level's pointers reach through `pres_ovl.c`); the snapshot is the whole data region; with `interp_mode=2` the presenter draws the in-between frame in every gamestate (on the Vita's thread in all but the credits' first stage) and the attract demos' 16:9 frame; `--pres-check` D30's pass's record for record in every gamestate the host's scenarios reach (`pause`, `die` and the tour among them), every file D30's, 0 bytes of the game's memory written (`--pres-write-check`). The demos' 16:9 frame stays on the game thread (its frame is filed after the logic draw; D128 has why). Part 2 (D129) retires D30's pass. **The other gamestates, then D30's pass retires.** In D104's coverage order (credits, title, cutscenes, pause and inventory, transitions, dragon, balloonist, fairy, exits): each is its draw function in the presenter build, the overlays included. Then the arena snapshot, `pass_vsync`, `g_psx_spu_frozen` and `g_psx_vram_frozen` (once `g_psyxVramFrozen`, D115) go. The presenter build drops those calls by name. The HUD's 16:9 narrowing moves from editing the OT's primitives in place and putting them back (`present.c` `narrow_x`) into the walk's conversion, so no pass edits the game's memory (**done ahead, D122:** the walk parses narrowed copies in their place, `PsxGpuSubst`). A gamestate left without the presenter could only show its real frame twice, which changes 60 fps there: a switch and the owner's decision; the plan is that every gamestate takes it. | as 7e, per gamestate; the pause route's shots in Vita3K (D70); `--widescreen` shots byte-identical after the narrowing moves | *estimate* ~0.8–1.5k lines |

All told *estimate* ~4–7k lines, the same as D101's total, distributed differently. The second build costs less than hand C for the draw path, while 7f's second producer and 7d2 are new.

**The other pieces:**
- **(a) The B2 hand C's machine model** (the register file, the scratchpad, the spill area, `HPTR`): all of it in 7d2. 7d moves none of it, so its check can compare the logic's code against itself.
  - `tools/native_audit.py` on main `a47a30b` (`logs/s7p/audit-before.txt`, gitignored): readable C (A + B1) **63.5%** of the executable and **94.1%** of the whole game; B2 16,845 instructions (18.4%) in 25 functions.
  - After 7d2 with `fast_render=3` the Vita's default (measured, D124): **81.9%** and **97.1%**, B2 at 0. What is left is E (16.7%, the PsyQ libraries the decomp calls, as our code) and X (1.4%, libc).
  - After D127 took the register-file builds out of the sources (measured, `logs/cleanup/audit-after.txt`): the instructions as above; B2 source lines 11,493 → 0 (B1 8,993 → 17,129), the Vita's code lines 85,885 → 82,327, the Vita ELF's `.text` 3,887,578 → 3,700,476 bytes.
  - In the native build `HPTR` is already a plain cast (D100); the arena build keeps it for the oracle.
- **(b) `g_psx_vram_frozen` and the in-place 2D narrowing:** 7g. The narrowing is done ahead of 7g (D122); `g_psx_vram_frozen` stays 7g's.
- **(c) The fully-native list** (the Summary):

  | Item | State |
  |---|---|
  | no MIPS register model | done (D127): the spill area and the hand stack under `g_mips_sp` left the native build with the register-file builds; `g_mips_sp` stays for the arena build's mips2c units and the oracle replay |
  | no GTE register file | the renderers' use of it: none (7d2, D124; the only build since D127). **By design:** the control registers libgte, the camera and the hand C set (the state §4 keeps), and `gteRegs` as the arena build's record for the oracle |
  | no PS1 primitives or OT walk | **by design** (D113): the decomp's C writes the primitives and links the OT; the walk is ours (D117) |
  | no 2 MB arena or 24-bit links | done (D106). **By design:** the heap is one block of the PS1's size with the decomp's packed formats, decoded relative to its base (`psx_mem.h`) |
  | no sector-addressed CD | done (D93); libcd's API stays as the decomp's calls, in our code |
  | no software SPU register model | done (D84, D88); libspu's API state stays as the decomp's calls |
  | no vblank emulation driving the logic | presentation left `VSync` (7c); the in-between frame leaves the game thread (7f); D30's pass and its hooks go (7g). **By design** (D101): the callbacks per vblank in `VSync(0)` with the catch-up, the PS1 interrupt's count |
  | PsyCross | its headers and the checks' reference stay (D118, the owner's choice A) |

  Outside the plan, and not blocking it: the watchdog's per-thread CPU line (stage 0's follow-up). The native build's `platform_linux32` ctest segfault (D118) is fixed: the test's own `psx_mem_host` was wrong (D121).

**7c's pictures on hardware (7P1/7P2; ran 2026-11-07, results in §3.5):**
- **The VPK:** main's CI VPK for both runs, with X7's settings at 60 fps (`REALTIME=1`, `DEVICE_DEFAULTS=1`, so `gpu_clear=2` as shipped). 7P1 sets `present_due=0` (`s7p1-k60-shots-due0.txt`); 7P2 keeps the shipped `=1` (`s7p2-k60-shots-due1.txt`).
- **The route:** the K route with `p-shots-k.cfg`: `n-gte-k.cfg`'s nine places, each shot at V and V+1, so a real frame and an in-between frame are both caught, with `shot_hd=1` (the GPU's frame due at that vblank).
- **The comparison:**
  - Real time drifts, so the pairs are compared, not required identical. Any pair that differs is looked at for a missing layer, a frame from the wrong tick, a HUD from another frame, or tearing.
  - The `7c:` counts and late vblanks against 7C1/7C2.
  - The owner watching is the check for judder.
- **Expected:** D118's dump at 7P2's exit, unless the parallel fix has merged.

##### How faithfulness stays testable

- **The logic never sees the presenter.** It reads nothing the presenter writes: the presenter's context is private and its snapshot is a copy. The callbacks keep their order and their bursts. So the checks stay the existing ones, run with the presenter on and off:
  - the oracle replay on the arena build (7,155 PASS, D97);
  - both attract demos tick for tick against the emulator (`trace_diff.py`: 866 and 768 ticks);
  - `tools/host_checks.sh` byte-identical: traces, shots, WAVs and memory cards (278–287 files);
  - Vita3K's shots, window captures and card;
  - save-state round trips.
- **Real frames** are the logic pass's own pictures, on the same vblanks. Shots stay byte-identical to 30 fps on those vblanks, as `docs/presentation.md` checks today.
- **In-between frames** must be byte-identical to D30's pass: record for record, every tick, on the host; picture for picture in Vita3K. D30's pass stays linked as the reference. That's a stronger check than D30 itself ever had (unchanged traces, and pictures looked at).
- **Timing:** only hardware can show what stage 7 buys, and only with the owner's yes. The host and `realtime=0` show pictures at virtual vblanks, so nothing there depends on the threads.

##### The payoff

From existing measurements. What stage 7 would change is an *estimate*.
- **The game thread at 60 fps:** the pass with its snapshot takes 3.8–5.2 ms per tick in play (X7: Artisans 3.84, Toasty 5.16) and 12.4 ms in the attract demo. 7f moves that off the game thread, so at 60 fps it would be about as busy as at 30 (Toasty: 39.5% → about 21.5%, *estimate*).
- **But 60 fps already holds in play** (D81; X7: 0–1 late vblanks per 300, no fall to 30 outside loads). So that time is headroom, not pictures.
  - What misses today is the Stone Hill attract demo's flight (X7 at 60 fps: 5.4 late vblanks per 300, worst 34) and the loads. In the flight the render thread is the wall (15.38 ms a picture). Stage 7 doesn't make the render thread faster; stage 2's direct path does.
  - What 7c changes there is who pays. Today the game thread waits for the render thread; after 7c, the render thread's overload costs a picture rather than a tick (*estimate*).
- **The GPU is unchanged:** it draws the same two pictures a tick (X7: 6.29 ms a picture in Toasty).
- **The cores:** the in-between renderers' 3.4–4.7 ms per tick (11.6 in the attract demo) move to core 1 or core 3. That should fit once D99 has removed the parse thread's walk (*estimate*); the mixer takes 0.24 ms per vblank there (X4).
- **The code:** the arena snapshot, three PsyCross hooks and the in-place blend go. Stages 4f and 8 need that.

So the CPU payoff is smaller than when D76 wrote stage 7: the measurements since then (D81, D98) show the game thread isn't the wall. That's why the cheap, independent steps (7a–7c) come first. Whether 7d–7g are worth their size can be decided on 7a's numbers and on what D99 leaves of the render thread.

##### What stage 8 needs from stage 7

- **libetc:** `VSync`, `VSyncCallback` and `ResetCallback` are ours (7b, D104: `src/platform/common/libetc.c`; `LIBETC.C` isn't built), and `PsyX_WaitForTimestep` and `PsyX_Sys_GetVBlankCount` are gone. Patches 0002, 0005 and 0009's hunks to `LIBETC.C` are dead and can go with stage 8.
- **The presentation hooks are gone:**
  - `g_psyxVramFrozen` (7g); `g_psyxVSyncHook` is our own `libetc.c`'s since 7b, so it no longer ties stage 8 to stage 7;
  - the `DrawOTag` pre and post hooks for captures (7c: frames go to the render thread whole).

  What's left of those hooks is the 2D narrowing for widescreen, which stage 2's direct path or stage 8 takes over.
- **No snapshot of PsyCross's `activeDrawEnv` or `activeDispEnv`:** a frame carries its own environment (D99, 7c).

That leaves stage 8 with libgpu's environment and VRAM calls, the walk for the leftover primitives, libgte's 12 calls and libpad.

D113 found that stage 8 needn't wait for 7g: `g_psyxVramFrozen` and the `DrawOTag` hooks are live on the default configuration (the attract demos' 16:9 pass, the 2D narrowing), and they move into our own libgpu with the calls they gate ("The stage 8 plan", below).

### Stage 8: retire PsyCross (medium, low–medium risk, after stages 1, 2, 4 and 7c; not after 7g): done (D113–D118)

**Status (D113, 2026-11-06): planned below. 8a done (D114). 8b done (D115): our `libgpu.c` in place of `LIBGPU.C`, the hooks direct calls. 8c done (D116). 8d done (D117, 2026-11-07): our walk (`gpu_walk.c`), PsyCross's only the reference; the Vita ELF links nothing of PsyCross. 8e done (D118, 2026-11-07): no `psycross` library in the native and Vita builds, PsyCross's headers kept (the owner's choice, A). 8f done (D118): 8H1–8H4 on hardware, the same pictures, card and CPU as main. Stage 8 is done.**

**Replaces** (D): what's left of PsyCross:
- libgpu's environment calls (`PutDrawEnv` 26, `PutDispEnv` 29, `DrawSync` 57, `LoadImage` 20, `StoreImage` 6, `MoveImage` 2 call sites in the decomp C; D113 recounts them);
- the OT walk for the leftover primitives;
- libgte's 12 calls;
- libpad (ours since 8a, D114; libetc since D104).

**Method:** thin native functions with the same contracts, or none where a stage removed the call. A patch to the decomp only where a call is removed outright.

**Gains:** one dependency fewer, and no `GrVertex` hop for the leftovers.

**Verify:** as stage 2.

**Size:** ~1k lines. D113 re-estimates it at ~2–2.5k (below).

**Optional:** no; it's the last step.

#### The stage 8 plan (D113, 2026-11-06)

Written on main `02f0b83` (D112). It changes no engine code. It covers:
- what the Vita and linux32 builds still take from PsyCross, and what of it the game reaches at run time;
- what of the GL layer is already ours;
- what the oracle, the checks and the headers need;
- the steps.

Measurements are cited with their logs; *estimates* are marked.

##### What the builds still link from PsyCross

**Method.** Both builds of main `02f0b83`, fresh (`build/s8-vita` with `-Wl,-Map`, `build/s8-linux32`, both gitignored). Every symbol `libpsycross.a` defines, joined with the undefined symbols of every other object in the build (`nm`), and the Vita ELF's link map for the members it pulls in. The two builds reference the same symbols, except that linux32 (the arena build) also has the mips2c renderers calling `GTE_operator` (the `fast_render=0` fallback) and `oracle_replay.c` calling `MFC2` and `CTC2`.

**Six of PsyCross's eleven compiled files are linked,** in both builds. Their code in the Vita ELF (`.text`, Thumb-2):

| File | `.text` (bytes) | What it is to the port |
|---|---:|---|
| `src/gte/PsyX_GTE.cpp` | 21,708 | the GTE register file `gteRegs` and `GTE_operator`; the hooks `g_psyxRtpHook`, `g_psyxRtpValueHook` (patches 0007, 0011) |
| `src/gpu/PsyX_GPU.cpp` | 8,980 | the OT walk, its packet parsers, the draw splits and vertex buffer, the GPU state (patches 0003, 0007, 0009, 0010, 0012, 0014) |
| `src/psx/LIBGTE.C` | 7,116 | libgte, over the register file |
| `src/psx/LIBGPU.C` | 3,868 | libgpu's API and its hooks (patches 0005, 0008, 0009) |
| `src/psx/INLINE_C.C` | 908 | `MTC2`, `CTC2`, `MFC2` and `doCOP2` (into `GTE_operator`) |
| `src/psx/LIBPAD.C` | 160 | libpad, over our `PsyX_Pad_*` |
| **total** | **42,740** | |

Not linked: `LIBAPI.C` (its only callers, `_96_init`, `_96_remove` and `LoadExec` in the decomp's `crash_demo.c`, aren't linked), `LIBSN.C`, `ABS.C` and `half_float.cpp`. Not compiled at all: `LIBCD.C`, `LIBSPU.C`, `LIBMCRD.C` and `LIBETC.C` (ours, E; `LIBETC.C` since D104), and PsyCross's platform layer (`PsyX_main.cpp`, `PsyX_pad.cpp`, `PsyX_render.cpp`, the OpenAL SPU: `SPYRO_PSYCROSS_PLATFORM=OFF`, `cmake/psycross.cmake`). Spyro has no libsnd.

**What our code takes from them, by library** (callers in the decomp under `third_party/spyro-1/src`, ours under `src/`):

| Library | Symbols | Callers |
|---|---|---|
| **libgpu, PsyQ API** (14) | `DrawSync`, `PutDrawEnv`, `PutDispEnv`, `DrawOTag`, `LoadImage`, `StoreImage`, `MoveImage`, `ClearImage`, `SetDrawMode` | `gamestates/draw.c`, `init.c`, `update.c`, `initialization.c`, `loaders.c`, `overlays/flight.c`, `overlays/titlescreen.c`; `credits_hand.c` (the credits overlay's asm, D96); `present.c` (`StoreImage` for the host's capture) |
| | `ResetGraph`, `SetGraphDebug`, `SetDefDrawEnv`, `SetDefDispEnv`, `SetDispMask` | `initialization.c` (start-up) |
| **libgpu, the port's hooks and state** (8) | `g_psyxGpuGate`, `g_psyxDrawOTagWalk`, `g_psyxVramFrozenGet` | `walk.c` (D71), `libetc.c` |
| | `g_psyxDrawOTagPre`, `g_psyxDrawOTagPost`, `g_psyxVramFrozen` | `present.c`: the 2D narrowing, the capture, the freeze during a presentation pass |
| | `g_psyxDrawOTagDone`, `drawsync_callback` | `vcache.c`, `savestate.c` |
| **The walk** (21) | `ParsePrimitivesLinkedList`, `ParsePrimitive`, `DrawAllSplits`, `ClearSplits`; `PsyX_AddSplit`, `PsyX_VertexBuffer`, `PsyX_SplitCount`, `PsyX_SplitGet`, `PsyX_SplitState*`; the hooks `g_psyxPacketHook`, `g_psyxPolyHook`, `g_psyxNextPacket`, `g_psyxClearSplitsHook` | `walk.c`, `parse.c`, both `vblank.c` (`PsyX_BeginScene`), `native_draw.c` (records, `nd_fast`, direct vertices), `vcache.c`; the host's checks (`native_draw_check.c`) |
| | the GPU state: `activeDrawEnv`, `activeDispEnv`, `currentDispEnv`, `g_GPUDisabledState`, `g_splitIndex` | `present.c`, `savestate.c` (the `GPU ` chunk), `native_draw.c`, `gpu_thread.c`, `video.c`, `png.c`, both `vblank.c` |
| **libgte, PsyQ API** (6 functions, the 12 calls) | `InitGeom`, `SetGeomOffset`, `SetGeomScreen` | `initialization.c` |
| | `SetTransMatrix` ×2, `MulMatrix` ×4, `MulMatrix0` ×3 | `camera.c`, `gamestates/draw.c`, `pete.c`; also `cyclorama_hand.c` (`MulMatrix`, `SetTransMatrix`) |
| **The GTE register file** | `gteRegs` (with `gtereg.h`'s `C2_*` names), `MTC2` | 29 objects of the Vita build, from 21 of our source files: the B2 hand C (`moby_hand.c`, `env_hand.c`, `sky_hand.c`, `pete_render_hand.c`, `shadows_hand.c`, `flame_hand.c`, `particles_hand.c` and their `_ngte`/`_ndr` builds, `math_hand.c`, `42CC4_hand.c`, `env_update_hand.c`, `sky_clip_hand.c`, `moby_cull_hand.c`, `images_hand.c`), `gte_ops.c`, `vcache.c`, `present.c`, `savestate.c`, the checks |
| | `GTE_operator` | through `doCOP2`, from `LIBGTE.C`'s `MulMatrix` and `MulMatrix0`; on linux32 also the mips2c renderers with `fast_render=0` |
| | `g_psyxRtpHook`, `g_psyxRtpValueHook` | `vcache.c`, `gte_ops.c`, the `_ngte` renderers |
| **libpad** (8) | `PadInitDirect`, `PadStartCom` | `initialization.c` |
| | `PadGetState`, `PadInfoMode`, `PadSetAct`, `PadSetActAlign`, `PadSetMainMode` | `gamepad.c` |
| | `PadStopCom` (`crash_demo.c`, not linked); `g_padCommEnable` | the Vita's `pad.c` |
| **libapi** | `_96_init`, `_96_remove`, `LoadExec` | `crash_demo.c` only: not linked |
| **libetc, libcd, libspu, libmcrd** | — | ours (D104; E) |

And the other way: PsyCross's linked code calls into ours for `GR_*` (16 entry points: `gpu.c`'s dispatch to `gpu_gl.c`, `gpu_thread.c` or `gpu_soft.c`), `PsyX_BeginScene`/`EndScene` (both `vblank.c`), `PsyX_Pad_*` (both `pad.c`), `GetVideoMode` (`libetc.c`), `PsyX_Log*`, `g_vramTexture`, `g_whiteTexture` and `g_psx_arena_base` (patch 0001's `nextPrim`).

**The call counts, recounted.** The decomp C (with its overlays) calls `PutDrawEnv` 22 times, `PutDispEnv` 25, `DrawSync` 48, `LoadImage` 16, `StoreImage` 4, `MoveImage` 2, `ClearImage` 12, `DrawOTag` 18, `SetDispMask` 7 and `SetDrawMode` 6; `credits_hand.c` adds 1, 1, 7, 3, 0, 2, 0, 1 of the first eight. The counts above this plan (26, 29, 57, 20, 6, 2) are D76's, and how it counted isn't recorded; nothing in the plan depends on the difference. So **the game calls 28 PsyQ entry points that PsyCross still provides** (14 libgpu, 6 libgte, 8 libpad), not §4's 76: libetc, libcd, libspu, libmcrd and libc are ours or the C library's. **What makes stage 8 more than thin functions is the 34 port-internal symbols around them**: the walk, the splits and the GPU state the records use, the register file the B2 hand C uses, and our patches' hooks.

##### What the game reaches at run time on the Vita's defaults

`native_draw=1`, `nd_direct=1`, `gpu_clear=2`, `present_due=1`, `mixer=3`, `cd_native=1`, native memory, 16:9 (`widescreen=1`), 30 fps. Host runs with the same switches (`--native-draw --nd-direct --gpu-clear --cd-native --mixer 3 --widescreen --walk-lazy`) give the counts below; the host's `gpu_soft.c` stands in for `gpu_gl.c`, whose calls are the same `GR_*` entry points.

**What reaches PsyCross's own parse** (`native_draw.c`'s `nd_report` line, which counts the packets the walk parsed itself by their first command; `logs/s8-demo`, `logs/s8-routeA`, `logs/s8-whole`, gitignored):

| Run | Lists | Records (primitives) | `nd_fast` packets (polygons) | Parsed by PsyCross: polygons, lines, rectangles, other |
|---|---:|---:|---:|---|
| both attract demos (12,000 vblanks) | 8,141 | 8,266,659 | 16,808 (4,803) | 0, **58,551**, 0, 0 |
| route A (13,000) | 6,669 | 8,746,163 | 254,483 (244,972) | 0, **77,274**, 0, 0 |
| the whole tour (100,000: every level, both endings, the credits, the cheats, which go through the inventory) | 50,169 | 78,878,781 | 436,245 (291,971) | 0, **883,246**, 0, 0 |

- **Only lines reach PsyCross's parse** on these switches: 7–18 line packets a list, from `gamestates/draw.c`, `flight.c`, the particle overlays and `particles_hand.c`. No polygon, rectangle (`TILE`/`SPRT`), fill or draw environment packet does. Every polygon is a record or an `nd_fast` packet (D92), and the frame's clear comes from `PutDrawEnv`'s `isbg` at the scene's start (`PsyX_BeginScene`, both `vblank.c`, ours), not from a fill packet.
- **So on the Vita's defaults PsyCross does, per list:** the walk's loop over a few hundred packets (the markers, D99), a few line packets, the splits and the vertex buffer the records fill, and `DrawAllSplits`. Per tick: libgpu's environment calls, `MulMatrix` and `MulMatrix0` through `GTE_operator`, libpad, and the register file the B2 hand C uses (with `ngte_fx=0`, the default, D95's shadows, flame and particles compute on it).
- **Dead on the defaults, live elsewhere:** the polygon parsers (`native_draw=0`, the host's default and the oracle's), the rectangle, fill, `DR_MOVE` and `DR_LOAD` parsers (no run reached them; `MoveImage` and `LoadImage` are called directly), `GTE_operator` for the mips2c renderers (`fast_render=0`, arena build only), the RTP hooks (the vertex cache, off by default). 8d keeps the rectangles and the rest anyway: they're small, and a packet no run reaches still has to draw as PsyCross would.

**Which functions run, and how often** (gprof call counts, a native-memory linux32 build with `-pg`, `build/s8-prof`, on the same switches; `logs/s8-prof-demo`, `logs/s8-prof-routeA`, `gprof-flat.txt` in each). Per run of 12,000 and 13,000 vblanks:

| Function | Demos | Route A | What calls it |
|---|---:|---:|---|
| `PsyX_AddSplit`, `PsyX_VertexBuffer` | 2.37 M, 2.12 M | 2.78 M, 2.58 M | the records (`native_draw.c`) |
| `MTC2`, `MFC2`, `CTC2` | 412 k, 235 k, 167 k | 188 k, 193 k, 135 k | `moby_hand.c`, and libgte's macros inside `MulMatrix` |
| `GTE_operator` (via `doCOP2`) | 78,432 | 64,266 | only `MulMatrix` (24,172 and 18,424 calls) and `MulMatrix0` (1,972 and 2,998), three `RTIR`s each: 3 × (24,172 + 1,972) = 78,432 |
| `DrawOTag`, `PutDrawEnv`, `PutDispEnv`, `DrawSync` | 8,141–8,181 | 6,669–6,753 | `GamestateDraw` and the other draws, once a list |
| `ParsePrimitivesLinkedList`, `DrawAllSplits`, `ClearSplits` | 6,169–6,170 | 6,669–6,670 | the walk, once a list (`walk.c`) |
| `SetTransMatrix` | 12,086 | 9,212 | `camera.c`, `draw.c` |
| `PadGetState`, `PadInfoMode`, `PadSetMainMode` | ~8,058 | ~13,000 | `gamepad.c`, about once a vblank (the attract demos skip the pad) |
| `StoreImage` | 6,169 | 6,679 | `present.c`'s capture on the host; the Vita captures in `gpu_gl.c` instead |
| `SetDrawMode`, `LoadImage`, `ClearImage` | 188, 42, 7 | 710, 84, 6 | `draw.c`; the loads; start-up and transitions |
| `InitGeom`, `SetGeom*`, `ResetGraph`, `SetGraphDebug`, `SetDef*Env`, `SetDispMask`, `PadInitDirect`, `PadStartCom`, `PadSetAct*` | 1–6 | 1–6 | start-up |

The register file's moves and `GTE_operator` are a few hundred calls a vblank; libgte's six functions, about two. None of it is a measurable share of a vblank (qemu's gprof times aren't the Vita's, §3.2).

##### What of the GL layer is ours already

- **All of the `GR_*` back end:** `gpu_gl.c` (vitaGL, 4,068 lines), `gpu_thread.c` (the render thread, D63, 7c), `gpu.c`'s dispatch, `gpu_soft.c` on the host. PsyCross's `PsyX_render.cpp` isn't compiled.
- **The VRAM model:** `gpu_gl.c`'s `s_vram`, the target logs, the read-back, the clears (D111).
- **The records and the direct vertices:** `native_draw.c` converts every polygon of a records list or an `nd_fast` packet (D82–D105), `psx_lean.h` is the box rule on both sides.
- **The presentation:** `present.c` and `gpu_thread.c` (7c: a frame goes to the render thread whole, with its environment).

**Still PsyCross's** (when D113 was written; since 8d, D117, all of it is ours: `gpu_walk.c` and `psx_gpu.h`, with polygons always through the records' converter and PsyCross's walk only the reference):
- the walk's loop and the GP0 split of a packet (patch 0003);
- the parsers for what the records' converter leaves: lines and polylines, `TILE`/`SPRT`, the draw environment commands (E1–E6 outside a polygon packet), the fill (GP0 02h → `ClearImage`), `DR_MOVE` (→ `MoveImage`) and `DR_LOAD`;
- the polygon parsers for `native_draw=0`, which is the host's default and the oracle's configuration;
- the draw splits and the vertex buffer (`g_splits`, `g_vertexBuffer`, `AddSplit`, `DrawSplit`, `DrawAllSplits`, `ClearSplits`): the records add to them too (`PsyX_AddSplit`, `PsyX_VertexBuffer`);
- the GPU state (`activeDrawEnv`, `activeDispEnv`, `currentDispEnv`, `g_GPUDisabledState`);
- the types and prototypes: `GrVertex`, `TextureID`, `BlendMode`, `TexFormat` and the `GR_*` declarations (`PsyX_render.h`), which `gpu_gl.c`, `gpu_thread.c` and `psx_lean.h` are written against.

##### How stage 8 goes ahead without 7g

- **`g_psyxVramFrozen` is live on the default configuration:** the attract demos at 16:9 run D30's second pass (`present.c`: `exact` is set for a demo unless `widescreen_logic`), and every 60 fps tick runs a pass. It only gates `LoadImage`, `StoreImage`, `MoveImage` and `ClearImage` (and the walk's copy, `walk.c`), which step 8b takes over. So **the flag becomes our libgpu's own,** set and cleared by `present.c` as today. When 7g comes, it removes the flag from our code. Neither the stage nor 7g has to be split.
- **The `DrawOTag` hooks are live too:** at 16:9 (the default) every `DrawOTag` narrows the HUD's and `flight.c`'s 2D primitives in place and puts them back (`present.c` `squeeze_2d`, `unsqueeze_2d`), and the capture begins and ends there. In 8b they become direct calls from our `DrawOTag` into `present.c`, the algorithm unchanged. Narrowing at the conversion instead of in place (no write and restore) becomes possible after 8d. It changes no picture and costs little today, so it isn't a step.

##### What the oracle, the checks and the headers need

- **The matching build** is untouched: it compiles the decomp with Sony's headers in its own Docker image, and `tools/build_matching.sh` stays green.
- **The arena build (linux32's default)** is the oracle's configuration (D97). Its mips2c renderers with `fast_render=0` call PsyCross's `GTE_operator`, and `oracle_replay.c` moves registers with `MTC2`, `MFC2` and `CTC2`.
- **The checks compare against PsyCross:** `tests/gte` against `GTE_operator` (and PCSX-Redux's vectors); `--native-draw-check`, `--native-draw-parse-check` and `--nd-direct-check` against PsyCross's own walk and parse (with `g_nd_fast_off`).
- **So PsyCross stays, as a reference.** A build switch, `SPYRO_PSYCROSS_REF` (on in the arena build and for `tests/gte`, never in the native or Vita build), compiles PsyCross's walk and GTE, renamed, beside ours, as `SPYRO_M2C_REF` keeps the mips2c units. The checks then compare our code with PsyCross's, call by call, which is what they do today with PsyCross as the code under test. Our patches stay for that library: the reference has to parse with the same hooks.
- **The headers are the open question, and it touches the licence.** The decomp and our code compile against PsyCross's `include/psx`: the decomp includes `libgte.h`, `libgpu.h`, `libetc.h`, `libpad.h`, `libcd.h`, `libspu.h`, `libmcrd.h` and `libapi.h` (its own `rand.h` and `strings.h`); 28 of our files include them, `gtereg.h` (9) and `PsyX_render.h` (10). Three ways:
  - **A. Keep the submodule's headers** (no PsyCross code compiled into the game). Our patches shrink to their header hunks (0001, 0004, 0006, 0013 and the hooks' declarations that move to our headers). Nothing about the licence changes. **Recommended.**
  - **B. Copy the headers we use into `src/`,** with PsyCross's MIT notice in each, and drop the submodule from the game's build (the reference build would still need it). MIT allows it with the notice kept.
  - **C. Write our own headers** from the hardware's documented layouts (psx-spx) and the decomp's use, never from Sony's. About 1.5k lines (*estimate*), checked by `tools/layoutcheck.sh` against the matching build's layouts. The most work, and a credit that would no longer be needed for the headers.

  **This is the owner's choice** (CLAUDE.md: ask before anything touching licensing). Steps 8a–8d don't depend on it; 8e waits for it. **The owner chose A** (2026-11-06, D118): PsyCross's headers stay as they are, behind our patches, and the licence stays MIT, credited in `THIRD_PARTY_NOTICES.md`.
- **The licence notice stays in any case.** `THIRD_PARTY_NOTICES.md` and the VPK's `licenses/` carry PsyCross's MIT text today. After stage 8 the port still has code derived from PsyCross: `gte_inline.h` and `gte_ops.c` are `PsyX_GTE.cpp`'s arithmetic "expression for expression" (D72), and 8c's and 8d's replacements keep PsyCross's semantics, so they will be ports of its code where they can't be anything else. Dropping the credit would need a clean rewrite of all of that, which isn't planned, and would be the owner's decision.

##### What can't change

- **Faithfulness:** `tools/host_checks.sh` byte-identical (traces, shots, WAVs, memory cards); Vita3K shots, window captures and card identical to main's VPK; the oracle replay on the arena build; both attract demos tick for tick against the emulator.
- **Save states:** the `GPU ` chunk (`DRAWENV`, two `DISPENV`, `g_GPUDisabledState`, `gteRegs`) keeps its bytes, so a format-2 state saved before stage 8 loads after it. Each step checks that. *Corrected by D115:* no build loads another build's state (D51 refuses it as "another build", rightly: a state holds code and data addresses, `vsync_callback` in this chunk among them). What each step can check is that the format is unchanged: the same chunks and lengths, and, between states saved at the same vblank by main and the step, every differing word an address that moved with the build (`tools/state_cmp.py`).
- **`tools/build_matching.sh` stays green.**
- **The decomp is patched only where a call is removed outright.** Stage 8 removes none: every one of the 28 entry points keeps its name and contract. So stage 8 adds no decomp patch.

##### The steps

| Step | What | Waits for | Check | Size |
|---|---|---|---|---|
| **8a** | **Done (D114).** **libpad.** `src/platform/common/libpad.c` replaces `LIBPAD.C` (left out in `cmake/psycross.cmake`, as `LIBETC.C` is): the eight entry points with PsyCross's results (`PadInfoMode` 7, `PadGetState` from the platform's status), over the platforms' pad functions (renamed from `PsyX_Pad_*`). | — | `host_checks.sh` byte-identical (pad scripts, analog runs); Vita3K gems: shots, captures, card identical to main's VPK | ~80 lines |
| **8b** | **libgpu's API.** `src/platform/common/libgpu.c` replaces `LIBGPU.C`: the 14 entry points with today's semantics, and the hooks as our own code. The gate is a direct call into `walk.c`; the VRAM freeze is our flag, set by `present.c` (above); `DrawOTag`'s pre and post call `present.c`, its done `vcache.c`. It still calls PsyCross's walk, splits and GPU state until 8d. `LIBGPU.C`'s hunks in patches 0005, 0008 and 0009 become dead. | — | `host_checks.sh` byte-identical; the host's VRAM dumps (`--vram-shot`, D111's 239) identical; Vita3K against main's VPK: gems, the demos (the 16:9 attract pass: the freeze), the pause route with a save state and a load: shots, captures, card identical; a state saved on main loads | ~300 lines |
| **8c** | **libgte and the register file.** The six functions in our C (`src/game/libgte.c`) on the fixed-point maths of `gte_int.h`, leaving the registers as PsyCross's do: `MulMatrix` and `MulMatrix0` leave the rotation as `m0` and `IR`, `MAC` and `FLAG` from their last `RTIR`; `InitGeom` sets `ZSF3`, `ZSF4`, `H`, `DQA`, `DQB`, `OFX`, `OFY`. `gteRegs`, `MTC2`, `CTC2` and `MFC2` move to our own file, the RTP hooks' definitions to `gte_ops.c`. Then `PsyX_GTE.cpp`, `LIBGTE.C` and `INLINE_C.C` leave the native and Vita builds; the arena build takes `GTE_operator` from the reference library. | — | `tests/gte`: each function against PsyCross's on random inputs and register states, the whole register file identical after each call, a mutant caught; `host_checks.sh` byte-identical; the oracle replay on the arena build (7,155 PASS, D97); Vita3K gems identical | ~250 lines, ~150 of test. **Done (D116):** `libgte.c`, `gte_regs.c`, the reference library `psycross_ref` (patch 0015); ~190 lines and ~240 of test |
| **8d** | **Done (D117).** **Our walk.** `src/platform/common/gpu_walk.c` replaces `PsyX_GPU.cpp`: the loop with the GP0 split, the records' packets and the `nd_fast` packets as direct calls into `native_draw.c`, the leftover parsers (lines, polylines, `TILE`/`SPRT`, the fill, `DR_MOVE`, `DR_LOAD`, E1–E6), the splits and the vertex buffer with today's `GrVertex`, and the GPU state. Polygons always go through the records' converter, with `native_draw=0` too: D92's per-packet check showed it parses as PsyCross does. `GrVertex` and the `GR_*` declarations move to our own header (`src/platform/common/psx_gpu.h`). | 8b | a new host check, `--walk-check` (reference build): every packet through both walks from the same state; splits, vertices and draw environment identical, 0 differ, on the demos, gems, wsi, lod, routes A and B and the tour, with `native_draw` 0, 1 and 2, `nd_direct` 0 and 1, and the vertex cache on; the existing checks still 0 differ; `host_checks.sh` byte-identical; Vita3K against main's VPK on D105's six runs | ~800 lines, ~250 of check |
| **8e** | **Done (D118).** **The build without PsyCross.** The headers as the owner decides (A, B or C above). `cmake/psycross.cmake` builds no library for the native or Vita build; `SPYRO_PSYCROSS_REF` builds the reference for the arena build and the tests. The dead patch hunks go (0002, 0005 and 0009's `LIBETC.C` ones with them); what stays patches the reference and, with A, the headers. `tools/native_audit.py` counts D as 0. `docs/porting-plan.md` and `CLAUDE.md`'s "Rendering" line follow. | 8a–8d, the owner's header decision | the Vita ELF's link map has no `libpsycross.a` member; `nm` of the native linux32 build has no PsyCross symbol; `host_checks.sh` byte-identical on the native and arena builds; ctest; the oracle replay; Vita3K against main's VPK; `tools/check_no_assets.sh`; `tools/build_matching.sh` | ~150 lines, mostly deletions; C adds ~1.5k |
| **8f** | **Done (D118, §3.5).** **Hardware** (proposed; the owner's yes first, for each run). Runs 8H1–8H4: main's VPK against stage 8's on the K route with shots and every layer on the game thread (Y9/Y10's settings: the 18 pictures and card, and the CPU split per vblank), and Toasty at 60 fps with the shipped threads (Y11/Y12's settings). | 8e | the same 18 pictures and card; no layer slower on the K route; late vblanks at 60 fps no worse | runs only |

**All told** *estimate* ~1.6k lines of our C and ~0.4k of checks, against the ~1k above (header choice C would add ~1.5k). The ~1k counted thin functions for the entry points. It missed the walk the records still go through (its splits, its vertex buffer, the leftover parsers), the register file the B2 hand C still reads and writes, and the reference build the checks need.

**Order and what can run at once:**
- **8a, 8b and 8c at once:** they touch different files (`libpad.c` and the two `pad.c`; `libgpu.c`, `present.c`, `walk.c`; the GTE files).
- **8d after 8b:** it moves the state 8b's functions use.
- **8e after the owner's header decision** (A, D118; done), then **8f** on hardware.

Each step changes no output, so each can be merged on the host's and Vita3K's checks alone; 8f is the one hardware gate.

##### What stays after stage 8

- **The PS1 primitives and the OT.** The decomp's own C writes them (`draw.c`, the HUD, `flight.c`, the environment packets), and the walk orders the records with them (D99). Stage 8 owns the walk; it doesn't remove it. Removing the OT would mean the decomp C writing records, a change to the decomp beyond removing a call.
- **The GTE register file** as the B2 hand C's machine model, now in our code. Taking the B2 hand C to readable C (B1) is what is left of stage 3's aim, not stage 8. D119 plans it as step 7d2, done as `fast_render=3`, the Vita's default (D124), and the only build since D127.
- **`g_psyxVramFrozen`'s role** (as our flag `g_psx_vram_frozen`) and the in-place 2D narrowing, until 7g (D119: the presenter build drops the VRAM calls by name, and the narrowing moves into the walk's conversion).
- **PsyCross as the reference** in the arena build and the tests, and its credit in `THIRD_PARTY_NOTICES.md`.
- **With header choice A,** the submodule, for its headers.

**What it buys** (*estimate*): one dependency fewer in the game, 43 KB less of the Vita ELF's code (half of it `GTE_operator`, which only `MulMatrix` still reaches), 14 patches reduced to the reference's, and the GPU state in our own code, where the render thread and 7d–7g can reach it. CPU per vblank: little. The leftover packets are a small share of the walk (above), and the walk's loop and splits stay the same work.

### Order and what can run at once

Status as of 2026-11-07 (D123; the table's layout is D101's). The payoff column gives the measured share each stage targets; ms are per vblank on the K route with every layer on the game thread (X4, §3.5) unless it says otherwise.

| Stage | Depends on | Payoff (measured share it targets) | Risk | Status |
|---|---|---|---|---|
| 0 measure | — | the render thread ~35% waiting; D72 bought −36% (§3.4) | none | done (§3.4) |
| 0b atlas on the CPU, XA read-ahead | — | 1.4–9.1 ms of GPU per picture in play; 0.5–0.6 ms of CPU per vblank | low | done (D78, D79) |
| 1 native GTE | — | the renderers −15 to −20% on hardware, 1.0–1.9 ms per tick at 60 fps | low | done (D80, D81, D98); `fast_render=2` was the default until D124's `fast_render=3` |
| 2 native draw lists | 1 | the OT walk (2.09 ms) and the PS1 GPU → vitaGL translation (3.44 ms, 35%); the render thread, the wall at 60 fps in the attract demo's flight | medium–high | records done (D82–D95), `native_draw=1` the Vita's default; direct vertices (D99, D105), `nd_direct=1` the Vita's default; the clears behind `gpu_clear` (D111): the translation 3.59 → 1.01 ms on hardware, `gpu_clear=2` the Vita's default |
| 3 readable C for mips2c | — (in parallel with 2: different files) | maintainability; prerequisite for 4 | low–medium | done (D83, D87, D94, D96) |
| 5 native audio | — | SPU + mixer 0.90 → 0.26 ms (core 1) | low–medium | done (D84, D88); `mixer=3` is the default |
| 6 native file I/O | — | the game thread's wait for its CdReads −9%, file-system calls 232 → 98 | low–medium | done (D93); `cd_native=1` is the Vita's default |
| 4 native memory | 2, 3 | maintainability; prerequisite for 7d and 8 | high | done (D97, D100, D106); native memory is the Vita's default |
| 7 decoupled timing | 1, 2 (D99), 4 (4b, 4c) | at 60 fps, the in-between frame off the game thread (3.8–5.2 ms per tick in play, X7); a late render thread costs pictures, not ticks | high | 7a and 7b done (D104); 7c done, `present_due=1` the Vita's default after its hardware pair (D112); 7d done (D123); 7d2 done (D124), `fast_render=3` the Vita's default after its hardware pair (identical pictures, renderers 5% lighter), the register-file builds removed (D127); 7e done, behind `interp_mode=2` (D125); 7f done (D126), on hardware the game thread at 60 fps near its 30 fps busy time in play (7F1–7F5), `interp_mode=2` the Vita's default; 7g part 1 done (D128: every gamestate on the presenter), part 2 (D30's pass retired) next; 7c's pictures checked on hardware (7P1/7P2) |
| 8 retire PsyCross | 1, 2, 4, 7c (not 7g, D113) | one dependency fewer; 43 KB of the Vita ELF's code; the GPU state in our code. Not CPU | low–medium | planned (D113); 8a done (D114), 8b done (D115), 8c done (D116: libgte and the register file are ours, PsyCross's GTE only the reference), 8d done (D117: our walk, the Vita ELF links nothing of PsyCross), 8e done (D118: no `psycross` library, PsyCross's headers kept, the owner's choice A), 8f done (D118: 8H1–8H4, the same as main on hardware). **Done** |

**What can run at once:**
- **Now:** 7g's part 2, D30's pass retired (D129), on part 1 (D128: every gamestate on the presenter), which builds on 7f (7d2 is done behind `fast_render=3`, D124, and since D127 the only build; 7e behind `interp_mode=2`, D125; 7f, D126, and `interp_mode=2` the Vita's default since 2026-11-09, the owner's decision after 7F1–7F5, without the repeat of 7F2/7F3 for the game-over windows).
- **Done:** 7a and 7b (D104); 7c, `present_due=1` the Vita's default after its hardware pair (D112); stage 2's direct vertices as the default (D105); stage 4, 4f–4i included (D106); stage 2's clears as the default (D111).
- **Stage 8 (D113): done** (D114–D118, 8H1–8H4 on hardware). None waited for 7d–7g.

## 6. Reproducing these numbers

| What | Command | Output |
|---|---|---|
| Static tables | `tools/native_audit.py --build build/vita-audit` (after the two Docker lines in §2) | §2's tables |
| Hardware split | `tools/vita/native_share.py LOG [--fps 30\|60] [--from V --to V] [--all]` | §3.1's tables |
| Host profile | §3.2's commands, then `tools/native_audit.py --prof FLAT BUILD [--detail]` | §3.2's table |

The logs used here are gitignored, in the main checkout's `logs/`: `hw-main-2a0691c`, `hw-pr49-i1`, `hw-pr40-c1`, `hw-pr40-d68`, `hw-4f26bc7-user`, `hw-26f3138-bench`, and `native-audit/` (this branch's runs).

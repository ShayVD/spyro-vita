# How native the port is, and a staged plan to make it fully native (D76)

Written 2026-11-02 on main `2a0691c`, before PRs #48 (D75, dynamic resolution) and #49 (D74, joined draws) were merged; the measurements are from that code. The K runs are for main after both (`f534fb1`). This document changes no engine code. It adds three measuring tools and three hardware run configurations:
- `tools/native_audit.py`: the static tables;
- `tools/native_audit_map.sh`: a relink with a link map, which the static tables need;
- `tools/vita/native_share.py`: the run-time split from `spyro.log`;
- `tools/vita/fps-runs/k1`–`k3`: the hardware runs.

Numbers from tools and logs are measured. Numbers marked *estimate* are reasoning from them, and the reasoning is given.

## Summary

Brought up to date on 2026-11-04 (D101, main `82d05ce`) from the stage status lines in §5 and §3.5. The first version (D76) was written on main `2a0691c`.

- **The game itself is mostly native.** No instruction of the original game runs as mips2c any more (D96). Of the original executable's 91,415 MIPS instructions, **63.5% run as readable C** (matched decomp C plus hand C on the decomp's structs); counting all 37 overlays as they are on the disc, **94.1%**. With the hand C that still keeps the PS1 machine model (the GTE register file, the scratchpad, `HPTR`), it's 81.9% and 97.1% (D96, §5 stage 3). D76 measured 50.6% and 91.7%.
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
  - stage 2 apart from its last piece: native draw records from every renderer (D82–D95), `native_draw=1` by default on the Vita;
  - stage 4's steps 4a–4c: the Vita's moved arena on the host (D97); every decode site named in `psx_mem.h`, and the native build with no mips2c units (D100).
- **In flight:**
  - **stage 2's last piece** (D99, session `stage2-direct-gl`): the records straight to vitaGL, without PsyCross's walk or `gpu_gl.c` interpreting PS1 GPU state. That's the 35% layer.
  - **stage 4's steps 4d–4e** (D100's second PR): any base address, and the bit-31 tags moved to bit 0 of the pointer.
- **What I recommend next, after those:**
  1. **Measure D99 on hardware** (the owner's yes first): the K route with K1's switches, and X7's Toasty route at 60 fps. They say what's left of the translation and of the render thread, which decides how much stage 7 is worth.
  2. **Stage 4's steps 4f–4i:** the data labels as ordinary globals, the game's memory from the heap, save states v2, the Vita on native memory.
  3. **Stage 7's steps 7a and 7b** (D101, §5): host audits and the port's own `VSync`. Both are small and wait for nothing. Then 7c after D99. Steps 7d–7g are decided on 7a's numbers and D99's measurement: since D81 and D98 the game thread isn't the wall at 60 fps, so stage 7's CPU gain is headroom rather than pictures.
  4. **Stage 8,** retiring PsyCross, last.

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

**Done (D80 and D81, 2026-11-02).** `fast_render=2` is the default since D81. NEON one vertex at a time was tried and is 5% slower on hardware (D81). It would pay only with four vertices per batch, in stage 2's vertex loops. N1 and N2 ran on hardware: identical pictures and memory card, and the renderers −15% (2.28 → 1.92 ms per vblank). What it is and how it was checked: `docs/decisions.md` D80 and `docs/translation-log.md`. In short: every call checked against `fast_render=1` over every host script (0 differences), 198/198 host files and 661/661 oracle samples identical, Vita3K shots and window captures identical. The renderers are −15 to −17% on the host (qemu), −16 to −35% in Vita3K and −15% on hardware.

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
  3. `presentation_pass()` runs the whole `GamestateDraw` again with `VSync` frozen (`pass_vsync`), SPU writes dropped (`g_psx_spu_frozen`) and VRAM writes frozen (`g_psyxVramFrozen`), and captures its picture.
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

**Order:**
- **7a and 7b now:** both are small, and neither waits for anything.
- **7c after D99.**
- **7d after 4c,** in parallel with 4f–4i.
- **7e and 7f** are where the risk is, and the payoff.
- **7g** closes the stage.

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

- **libetc:** `VSync`, `VSyncCallback` and `ResetCallback` are already ours (7b), and `PsyX_WaitForTimestep` and `PsyX_Sys_GetVBlankCount` are unused.
- **The presentation hooks are gone:**
  - `g_psyxVSyncHook` and `g_psyxVramFrozen` (7g);
  - the `DrawOTag` pre and post hooks for captures (7c: frames go to the render thread whole).

  What's left of those hooks is the 2D narrowing for widescreen, which stage 2's direct path or stage 8 takes over.
- **No snapshot of PsyCross's `activeDrawEnv` or `activeDispEnv`:** a frame carries its own environment (D99, 7c).

That leaves stage 8 with libgpu's environment and VRAM calls, the walk for the leftover primitives, libgte's 12 calls and libpad.

### Stage 8: retire PsyCross (small, after stages 1, 2 and 7)

**Replaces** (D): what's left of PsyCross:
- libgpu's environment calls (`PutDrawEnv` 26, `PutDispEnv` 29, `DrawSync` 57, `LoadImage` 20, `StoreImage` 6, `MoveImage` 2 call sites in the decomp C);
- the OT walk for the leftover primitives;
- libgte's 12 calls;
- libetc and libpad.

**Method:** thin native functions with the same contracts, or none where a stage removed the call. A patch to the decomp only where a call is removed outright.

**Gains:** one dependency fewer, and no `GrVertex` hop for the leftovers.

**Verify:** as stage 2.

**Size:** ~1k lines.

**Optional:** no; it's the last step.

### Order and what can run at once

Status as of 2026-11-04 (D101). The payoff column gives the measured share each stage targets; ms are per vblank on the K route with every layer on the game thread (X4, §3.5) unless it says otherwise.

| Stage | Depends on | Payoff (measured share it targets) | Risk | Status |
|---|---|---|---|---|
| 0 measure | — | the render thread ~35% waiting; D72 bought −36% (§3.4) | none | done (§3.4) |
| 0b atlas on the CPU, XA read-ahead | — | 1.4–9.1 ms of GPU per picture in play; 0.5–0.6 ms of CPU per vblank | low | done (D78, D79) |
| 1 native GTE | — | the renderers −15 to −20% on hardware, 1.0–1.9 ms per tick at 60 fps | low | done (D80, D81, D98); `fast_render=2` is the default |
| 2 native draw lists | 1 | the OT walk (2.09 ms) and the PS1 GPU → vitaGL translation (3.44 ms, 35%); the render thread, the wall at 60 fps in the attract demo's flight | medium–high | records done (D82–D95), `native_draw=1` the Vita's default; the direct path to vitaGL in flight (D99) |
| 3 readable C for mips2c | — (in parallel with 2: different files) | maintainability; prerequisite for 4 | low–medium | done (D83, D87, D94, D96) |
| 5 native audio | — | SPU + mixer 0.90 → 0.26 ms (core 1) | low–medium | done (D84, D88); `mixer=3` is the default |
| 6 native file I/O | — | the game thread's wait for its CdReads −9%, file-system calls 232 → 98 | low–medium | done (D93); `cd_native=1` is the Vita's default |
| 4 native memory | 2, 3 | maintainability; prerequisite for 7d and 8 | high | 4a–4c done (D97, D100); 4d–4e in review (D100); then 4f–4i |
| 7 decoupled timing | 1, 2 (D99), 4 (4b, 4c) | at 60 fps, the in-between frame off the game thread (3.8–5.2 ms per tick in play, X7); a late render thread costs pictures, not ticks | high | planned (D101); 7a and 7b next |
| 8 retire PsyCross | 1, 2, 7 | one dependency fewer | low | not planned in detail |

**What can run at once:**
- **Now:** D99 and D100 touch different files. Stage 7's 7a (debug tooling in `present.c` and the host) and 7b (the two `vblank.c` files and the port's own `VSync`) can run beside them; check D99's diff for the vblank files before starting 7b.
- **After D99:** its hardware measurement (the owner's yes first), then 7c.
- **After D100:** 4f–4i, and 7d beside them (7d changes the renderers' interfaces; 4f–4i are mostly the labels, the loader and tooling).
- **Then** 7e–7g, then stage 8.

## 6. Reproducing these numbers

| What | Command | Output |
|---|---|---|
| Static tables | `tools/native_audit.py --build build/vita-audit` (after the two Docker lines in §2) | §2's tables |
| Hardware split | `tools/vita/native_share.py LOG [--fps 30\|60] [--from V --to V] [--all]` | §3.1's tables |
| Host profile | §3.2's commands, then `tools/native_audit.py --prof FLAT BUILD [--detail]` | §3.2's table |

The logs used here are gitignored, in the main checkout's `logs/`: `hw-main-2a0691c`, `hw-pr49-i1`, `hw-pr40-c1`, `hw-pr40-d68`, `hw-4f26bc7-user`, `hw-26f3138-bench`, and `native-audit/` (this branch's runs).

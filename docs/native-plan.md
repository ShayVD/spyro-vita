# How native the port is, and a staged plan to make it fully native (D76)

Written 2026-11-02 on main `2a0691c`, before PRs #48 (D75, dynamic resolution) and #49 (D74, joined draws) were merged; the measurements are from that code. The K runs are for main after both (`f534fb1`). This document changes no engine code. It adds three measuring tools and three hardware run configurations:
- `tools/native_audit.py`: the static tables;
- `tools/native_audit_map.sh`: a relink with a link map, which the static tables need;
- `tools/vita/native_share.py`: the run-time split from `spyro.log`;
- `tools/vita/fps-runs/k1`–`k3`: the hardware runs.

Numbers from tools and logs are measured. Numbers marked *estimate* are reasoning from them, and the reasoning is given.

## Summary

- **The game itself is mostly native already.** Of the original executable's 91,415 MIPS instructions, **50.6% now run as readable C** (matched decomp C plus hand-written C). Counting all 37 overlays as they are on the disc, that's **91.7%**. Of the code bytes we ship that aren't third-party libraries, **70.0%** are readable C.
- **The time is spent in the emulation layers.** In main's heaviest logged level (Toasty, 30 fps, hardware), the game's own logic, renderer bodies and primitive building take **15–17%** of the CPU work per vblank, counted over all threads. The other **83–85%** goes to:
  - the software GTE;
  - PsyCross's OT walk;
  - turning PS1 primitives into vitaGL draws;
  - the SPU and XA models;
  - CD reads.

  On the GPU, D74's I1 run shows that PsyCross's way of splitting draws cost **half of Toasty's scene time** (15–21 ms down to 6–7 ms once the draws were joined).
- **Stage 0 confirmed it on hardware** (runs K1–K3 on main `f534fb1`, §3.4). With every layer on the game thread (K1), the emulation layers take **78–84%** of the CPU per vblank. That includes the GTE's share, which is still an estimate: without it, 58–72% is measured outright. The render thread's real CPU is **two-thirds** of its busy time, so the earlier 35–51% range resolves to its low end.
- **Stage 0 also found two things to fix before the stages:**
  - **On the GPU, texture decoding:** decoding PS1 textures into the atlas takes **7–9 ms of GPU time per picture** while new textures come into view in play. That's more than the game's own scene. Each decode pass is a render pass over the whole 2048×2048 atlas.
  - **The XA read-ahead (D64) stops working after the first level load,** which puts the music reads back on the game thread.
- **"Fully native"** means the game's own code (the decomp's logic, fixed-point maths and tick) is unchanged and runs on the Vita with nothing PS1-shaped in between:
  - no MIPS register model;
  - no GTE register file;
  - no PS1 primitives or ordering-table walk;
  - no 2 MB arena or 24-bit links;
  - no sector-addressed CD;
  - no software SPU register model;
  - no vblank emulation driving the logic.

  Faithfulness to the original's behaviour stays the hard constraint at every stage.
- **What I recommend next:**
  1. **Stage 0b, the two fixes stage 0 found** (§5): decode atlas blocks on the CPU straight into the texture's memory, with no render pass; and let the XA read-ahead recycle its windows after a jump back in the stream. Both are small, and both are checked pixel- and sample-identical.
  2. **Stage 1, native GTE maths** in the hand-written renderers. Low risk, testable exhaustively, optional per switch. It's also a prerequisite for stage 2: projected vertices have to be C values, not GTE registers.
  3. **Stage 2, native draw lists:** the renderers emit GPU vertices straight into per-bucket batches, in exact OT order, instead of PS1 primitives that PsyCross walks and re-parses. This is the biggest payoff (the OT walk, most of the render thread, the remaining draw splits). Environment first.

  **Stage 3, readable C for the remaining mips2c code,** can run in parallel with stage 2, because the files don't overlap. It's the prerequisite for dropping the arena (stage 4).

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
- **Done (D79):** `atlas_cpu=1`, the default. In Vita3K the window captures of the gems script, the pause route and the attract demo show 0 differing pixels inside the picture against the pass (`atlas_cpu=0`), and the `atlas` lines' counts are identical. No NEON yet: it waits for the hardware's per-block time.
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

### Stage 4: native memory (large, high risk, after stages 2 and 3)

**Replaces** (E):
- the 2 MB arena at a 16 MB-aligned base (`arena.c`, `vita_arena.cmake`);
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

| Stage | Depends on | Payoff (measured share it targets) | Risk |
|---|---|---|---|
| 0 measure | — | done (§3.4): the render thread is ~35%, D72 bought −36% | none |
| 0b atlas on the CPU, XA read-ahead | — | 1.4–9.1 ms GPU per picture in play; 0.5–0.6 ms CPU per vblank | low |
| 1 native GTE | — | ~9% of CPU per vblank (60% of the renderers' 15%) | low |
| 2 native draw lists | 1, #48 and #49 merged | the OT walk's 11% and most of the render thread's 35–51%; GPU draw count | medium–high |
| 3 readable C for mips2c | — (in parallel with 2: different files) | maintainability; prerequisite for 4 | low–medium |
| 5 native audio | — | 9% (core 1) | low–medium |
| 6 native file I/O | — | 5% (core 1), load times | low–medium |
| 4 native memory | 2, 3 | maintainability; prerequisite for 7 | high |
| 7 decoupled timing | 1, 2, 4 | true 60 fps without a second draw pass | high |
| 8 retire PsyCross | 1, 2, 7 | one dependency fewer | low |

Stages 5 and 6 are independent of the rest and small. They can fill gaps, but their payoff is on core 1, not on the game thread or the GPU.

## 6. Reproducing these numbers

| What | Command | Output |
|---|---|---|
| Static tables | `tools/native_audit.py --build build/vita-audit` (after the two Docker lines in §2) | §2's tables |
| Hardware split | `tools/vita/native_share.py LOG [--fps 30\|60] [--from V --to V] [--all]` | §3.1's tables |
| Host profile | §3.2's commands, then `tools/native_audit.py --prof FLAT BUILD [--detail]` | §3.2's table |

The logs used here are gitignored, in the main checkout's `logs/`: `hw-main-2a0691c`, `hw-pr49-i1`, `hw-pr40-c1`, `hw-pr40-d68`, `hw-4f26bc7-user`, `hw-26f3138-bench`, and `native-audit/` (this branch's runs).

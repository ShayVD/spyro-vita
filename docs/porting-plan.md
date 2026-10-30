# Porting plan: Spyro the Dragon (SCUS-94228) → PS Vita

Written 2026-10-29, at the end of M0. Paths without a prefix are relative to `third_party/spyro-1` (decomp commit `d2ac00b`). `PsyCross/...` means `third_party/PsyCross` (commit `e56e4cd`). Counts come from scripts run over the `.s` files (instruction = a line with the splat `/* off addr word */` prefix), not from file sizes.

---

## 0. Summary

- **The unmatched functions are smaller than they look.** The 227 KB `pete/func_80043FE4.s` is **3,865 instructions**. All 18 unmatched functions together come to **~20,000 instructions**. They are ordinary GCC 2.7.2 output: normal ABI, switch tables, **no GTE, no scratchpad**.
- **The bigger job is the hand-written assembly:** ~**24,500 instructions** in 16 files outside `asm/psyq*`. That includes the 7 renderers and `collision.s`, which use the GTE as a fixed-point maths unit, use all 1 KB of scratchpad, treat `$sp/$gp/$fp/$ra` as ordinary registers, and contain computed jumps.
- **PsyQ surface: 76 entry points, plus the CRT start-up.** PsyCross has 75 of them. `CdMix` is missing. libcd and libspu are too shallow for Spyro (no XA, no async read callbacks, no CD volume). GPU and GTE are well covered.
- **Three structural decisions follow from what the code assumes:**
  1. **Translation:** one symbolic MIPS→C translator in `tools/mips2c/` for *all* remaining asm, so that everything runs early. Then hand-written C replaces it where we need readability or speed (§7).
  2. **Memory:** the game's dynamic "PS1 RAM" is a **2 MB arena at a 16 MB-aligned host address with bit 31 set**. 24-bit OT links, 21-bit animation pointers and bit-31 model tags then keep working with tiny patches (§4).
  3. **Primitives:** keep the **PS1-native primitive layout**: PsyCross with `USE_EXTENDED_PRIM_POINTERS=0` and `USE_PGXP=0`, plus a one-line patch so `nextPrim` adds the arena base. Translated renderers then write exactly the bytes the PS1 GPU would see (§5).
- **Biggest unknowns:** whether Vita user memory can provide a 16 MB-aligned block with bit 31 set (expected yes, unverified); PsyCross renderer speed on the SGX through vitaGL; and the bounty's acceptance terms.

---

## 1. Environment checks (M0)

| Check | Result | Evidence |
|---|---|---|
| Submodules checked out | **PASS (after fix)** | `spyro-1` at `d2ac00b` and `PsyCross` at `e56e4cd` were present, but spyro-1's **nested** submodule `tools/maspsx` was not initialised. Fixed with `git submodule update --init --recursive`, which checked out maspsx `8b58a40`. No tracked file in either submodule changed. |
| Matching build → PSX.EXE SHA-1 `84E3728A…` | **PASS (after 3 fixes, below)** | `tools/build_matching.sh` → `PSX.EXE SHA-1 OK (84e3728a…e0c5)`, 38/38 `sha256sum -c` lines OK (PSX.EXE plus 37 overlays). Log: `build/build_matching-run1.log` |
| `disc/files/` extracted, `SCUS_942.28` SHA-1 | **PASS** | `shasum` gives `84e3728ab94720d0873e2514adf4aade4935e0c5`. All 16 files in `manifest.json` are present. |
| PsyQ headers in `psyq/include` | **PASS** | 51 entries (`libgpu.h`, `libgte.h`, `gtemac.h`, …) |
| VitaSDK, vitaGL, SDL2 | **PASS** | `/usr/local/vitasdk` (`$VITASDK` set). `libvitaGL.a`, `libvitashark.a`, `libmathneon.a`, `libSDL2.a` (+ mixer/image/net/ttf), `vitaGL.h`, `arm-vita-eabi-gcc`, `vita-mksfoex` and `vita-pack-vpk` are all present. |
| Extras | PASS | Docker 27.4 on an arm64 host (`tools/gcc2.7.2/cc1` is an i386 ELF; see note 3), cmake, ninja, python3, `/Applications/Vita3K.app` |

**Matching build notes (environment problems found and fixed):**
1. **The nested `maspsx` submodule was missing** (see the first row). The decomp's Makefile pipes `cpp | cc1 | maspsx | as` **without `pipefail`**, so every C file became an *empty 776-byte object*, and the build only failed at link time (`undefined reference to main`). `tools/build_matching.sh` now runs make with `SHELL='/bin/bash -o pipefail'`.
2. **Upstream HEAD `d2ac00b` doesn't compile with GCC 2.7.2.** Commit `0d228ab` added a full prototype for `MobyRotateToAngle` (5 parameters), but the original game calls it with 4 (`src/overlays/moby_update.c:22102,22108`, commented "One param too little"). That makes it a hard error in the `LEVEL_33` variant. Until `pipefail` exposed it, it looked like the empty-object problem. Fixed with `patches/spyro-1/0001-moby_update-unprototyped-MobyRotateToAngle-call.patch`, which calls it through a K&R cast: `((int (*)())MobyRotateToAngle)(…)`. The output is **byte-identical** (LEVEL_33.ovl OK). The patch is applied to the build copy only. **Worth reporting upstream.**
3. **Running the image as `linux/amd64` was flaky.** `python3` segfaulted at random under emulation with `-j`, leaving more empty objects (`Segmentation fault | python3 tools/remove_sections.py`). The script now builds the Dockerfile natively for arm64 (`s1_dev_env_arm64`), so only the i386 `cc1` runs through Docker Desktop's binfmt emulation. Clean, stable and faster.
4. Docker Desktop doesn't share `/private/tmp`. The build runs on an rsync copy at `build/s1-matching/` (gitignored), so `third_party/spyro-1` stays pristine; only the nested-submodule checkout changed its state.

---

## 2. Remaining non-C code

### 2.1 Unmatched functions (`asm/nonmatchings/`), ~20,000 instructions

Columns: instructions (insns), `jal` count, `jr` through a jump table, and the main callers (file:line). "What it does" comes from the decomp's own comments and the call sites.

| Function | Insns | jal | jtbl | What it does | Called from |
|---|---|---|---|---|---|
| `pete/func_80043FE4` | 3,865 | 195 | 2 | Spyro's physics state update for one delta substep (`pete.c:1371-1373`) | `UpdateSpyroPhysicsAndSurfaces` (`pete.c:1547`), plus 2091, 2190 |
| `pete/func_80041670` | 2,699 | 84 | 1 | State transitions and damage handling (calls `func_8003EA68` 36×, `HandleSpyroDamage` 26×) | `func_8004A200` (`pete.c:1920`) |
| `gamestates/update/func_8002F3E4` | 2,108 | 118 | 1 | Gamestate 8: dragon rescue cutscene update (`update.c:744-747`) | `GamestateUpdate` (`update.c:1438`) |
| `overlays/credits/func_credits_8007AA50` | 1,381 | 39 | 1 | Credits update: loads credit levels, SPU and VRAM | `update.c:1459` |
| `pete/func_8003EA68` | 1,324 | 67 | 2 | Sets Spyro's state (`func_8003FDC8` wraps it) | `pete.c:1074,1134,1136`, `loaders.c:467,1706,1769,1825`, `moby_update.c:26355`, asm |
| `camera/func_80035FB4` | 1,302 | 61 | 0 | Camera state update for the general states (`camera.c:265-270`) | `CameraUpdate` (`camera.c:564`) |
| `cyclorama/func_80050BD0` | 1,287 | 45 | 0 | Per-frame skybox setup: matrices, colour lerp, calls into r_cyclorama | `draw.c:912,1661,1812,1856,1899`, `flight.c:237`, `balloonist.c:932,944` |
| `pete/func_8003FE40` | 1,098 | 63 | 0 | Spyro moby and collision interaction (ColMoby, ColSphere) | `pete/func_80047B60` (16×) |
| `gamestates/update/func_800314B4` | 1,041 | 71 | 1 | Gamestate 11: fairy and save (memory card) | `update.c:1444` |
| `pete/func_80047B60` | 889 | 75 | 1 | Spyro's per-frame state dispatch (`pete.c:1376-1378`) | `pete.c:1551` |
| `camera/func_80034CE8` | 687 | 0 | 0 | Spherical-coordinate update, pure maths (`camera.c:241`) | `camera.c:509`, `func_80035FB4` ×2 |
| `pete/func_80048D10` | 562 | 31 | 0 | Spyro's head and flame update (ColTraceRay, rand) | `pete.c:1941`, `balloonist.c:190`, `func_800314B4` |
| `camera/func_80034480` | 513 | 38 | 0 | Camera collision; updates the spherical coordinates (`camera.c:226`) | `func_80035FB4` |
| `camera/func_800357A4` | 493 | 5 | 0 | Warps the camera when it's blocked (`camera.c:246`) | `CameraUpdate` (`camera.c:554`) |
| `moby_helpers/func_8003BFC0` | 230 | 13 | 0 | Moby movement or steering helper (Atan2Fast, `func_800529E4`) | `moby_update.c` (8 sites) |
| `overlays/credits/func_credits_8007BFD0` | 218 | 22 | 0 | Credits draw | `draw.c:2707` |
| `camera/func_80033F08` | 164 | 7 | 0 | Recomputes the camera's spherical coordinates from a position | `camera.c:217,236`, `loaders.c` ×3, `moby_update.c` ×2, `balloonist.c:728` |
| `pete/func_80049FAC` | 149 | 6 | 0 | Spyro floor probe | `pete.c:1940`, `balloonist.c` ×3, `func_8002F3E4` |

There are no `NON_MATCHING` C drafts in the tree. (We asked altro50 about decomp.me scratches: still open.)

### 2.2 Hand-written assembly (outside `asm/psyq*`), ~24,500 instructions

| File | Insns | Funcs | What's in it | Notable callers |
|---|---|---|---|---|
| `renderers/r_environment.s` | 6,199 | 6 | Level geometry: LOD, depth-cue fade, texture animation (`func_800258F0`, `func_8002A6FC`, `EnvAnim*`) | `draw.c`, `titlescreen.c`, `environment.c`, `moby_update.c` |
| `renderers/r_moby.s` | 4,699 | 8 | Moby (object) models (`func_8001F158/F798/208FC/20F34/22A2C`) | `draw.c`, `titlescreen.c`, `flight.c`, `balloonist.c` |
| `collision.s` | 3,932 | 23 | `ColTraceRay`, `ColSphere`, `ColFindFloor`, `ColMoby`, `ColSpyroInHitbox`, `func_8004DF24` | pete and camera code, `moby_update.c`, `balloonist.c` |
| `renderers/r_cyclorama.s` | 2,058 | 12 | Skybox geometry (`func_8004EBA8` and helpers) | `func_80050BD0`, `draw.c`, `titlescreen.c` |
| `renderers/r_pete.s` | 1,931 | 4 | Spyro's own model (`func_80023AC4`) | `draw.c` |
| `renderers/r_particles.s` | 1,540 | 9 | Particles (`func_800573C8`, …) | `draw.c`, `moby_update.c`, `loaders.c` |
| `math.s` | 1,322 | 45 | `Sin/Cos/Atan2/Vec*/RotVec8ToMatrix/OctDistance/ColTriUnpack`, … | everywhere |
| `42CC4.s` | 1,067 | 9 | `MobyAlloc/Free/UpdateCollision/SetShaded/GetAnimatedVertex/UpdateShadow` | moby code, loaders |
| `renderers/r_shadows.s` | 650 | 2 | Blob shadows (subtractive) | `draw.c` |
| `renderers/r_flame.s` | 452 | 1 | Flame breath | `draw.c` |
| `moby_lists.s` | 310 | 3 | Moby render queue (`func_800521C0`) | `draw.c`, `titlescreen.c`, … |
| `order_table.s` | 100 | 3 | Bucketed OT insert and link (`func_800168A0/8DC`, `func_80016784`) | every draw path |
| `images.s` | 96 | 3 | `ColorLerp` (GTE DPCS), framebuffer → 8 bpp (`func_80017E98`) | `draw.c`, `flight.c`, cyclorama |
| `particle_alloc.s` | 77 | 2 | Particle slot allocation | particle overlays |
| `memory.s` | 38 | 3 | `Memset`, `Memset16`, `Memcpy` | everywhere |
| `system_time.s` | 4 | 1 | Reads root counter 2 at `0x1F801120` | `titlescreen.c:666` |

Plus `asm/header.s` (the PS-X EXE header, which we drop) and `asm/data/*.s` (data, see §4.4). `asm/psyq.s` (16,576 insns, 76 named entry points plus internals), `psyq.libsn.s` and `asm/data/psyq.*` are **replaced by PsyCross** and our own shims, not translated.

### 2.3 GTE use (all of it is in assembly; the C code has none, and there is no inline asm anywhere in `src/`)

| Op | Where |
|---|---|
| RTPS / RTPT | every renderer, collision, math |
| NCLIP | renderers |
| DPCS / INTPL | depth-cue fade to the far colour (renderers, `images.s` ColorLerp) |
| MVMVA | matrix × vector (renderers, math, collision, 42CC4) |
| SQR / OP / GPF / GPL | the GTE used as a maths unit (collision 28 OP, 15 SQR; math) |
| CC | r_moby lighting |
| `lwc2/swc2` | math.s only (24/9) |

Renderers also **store data in GTE control registers** (e.g. the camera position in RBK/GBK/BBK in r_environment). An emulated register file must keep anything written to it, which PsyCross's does.

### 2.4 Difficulty ranking (hardest first)

| Rank | Code | Why |
|---|---|---|
| 1 | `collision.s` | Its results feed gameplay, so it must be bit-exact. GTE as a maths unit, 225 scratchpad references, 17 computed `jr` into code tables (`lui/addiu label; add idx; jr`, e.g. `collision.s:475-480`). |
| 2 | `r_environment.s`, `r_moby.s` | 11k insns. Non-standard ABI (saves all callee-saved registers to `D_80077DD8`, then uses `sp/gp/fp/ra` as ordinary registers; `$sp=0x1F800200` in `r_moby.s:598`). Scratchpad stack. Local subroutine calls through `jr $t4`. |
| 3 | `r_cyclorama.s`, `r_pete.s`, `r_particles.s`, `r_shadows.s`, `r_flame.s` | The same techniques, smaller. Cyclorama has jump tables of `j` instructions indexed by NCLIP sign bits. |
| 4 | `pete/func_80043FE4`, `func_80041670`, `update/func_8002F3E4` | Very large state machines, plain compiler output. The risk is volume and fidelity, not technique. |
| 5 | The other pete/camera/update functions, `func_80050BD0`, credits | Plain compiler output, 150–1,400 insns each. |
| 6 | `math.s`, `42CC4.s`, `moby_lists.s` | Small and numerous. Exactness matters, but each is easy to unit-test. |
| 7 | `order_table.s`, `memory.s`, `images.s`, `particle_alloc.s`, `system_time.s` | Trivial. Write them by hand. |

---

## 3. PsyQ surface vs PsyCross

The game calls exactly **76 PsyQ entry points** (every named `glabel` in `asm/psyq.s` that something outside psyq references), plus the CRT start-up `func_8005B8E0` (zeroes BSS, sets up the heap and `$sp` from `_ramsize`, calls `main`). The C has no GTE macros or inline asm. All GTE use is in the `.s` files (§2.3), which the translator maps to the GTE model.

"Covered" means PsyCross has a body that does what Spyro needs. "Partial" means a body exists but is stubbed or wrong for Spyro's usage. Evidence: `PsyCross/src/psx/*.C`.

| Library | Function(s) | Status | Notes |
|---|---|---|---|
| libgpu | ResetGraph, SetGraphDebug, SetDispMask, SetDefDrawEnv, SetDefDispEnv, PutDrawEnv, PutDispEnv, SetDrawMode, DrawSync, LoadImage, ClearImage, DrawOTag | **Covered** | `LIBGPU.C`. DrawOTag needs our arena patch (§5). PutDispEnv `isrgb24` is ignored, which affects the boot logos. |
| libgpu | StoreImage, MoveImage | **Partial** | They act on CPU VRAM only. The framebuffer isn't in CPU VRAM on ES2 (`USE_FRAMEBUFFER_BLIT=0`). Needed for the pause, inventory and flight-results grayscale freeze and the game-over freeze (`draw.c:908-933,1671-1677`). |
| libgte | InitGeom, SetGeomOffset, SetGeomScreen, SetTransMatrix, MulMatrix, MulMatrix0 | **Covered** | `LIBGTE.C`. |
| GTE ops | RTPS, RTPT, NCLIP, DPCS, INTPL, MVMVA, SQR, OP, GPF, GPL, CC + MTC2/MFC2/CTC2/CFC2 | **Covered** | `PsyCross/src/gte/PsyX_GTE.cpp:441-961`: integer-accurate, with UNR divide, FLAG and limiters. To be confirmed bit-exact against PCSX-Redux vectors in M1. |
| libetc | VSync, VSyncCallback, StopCallback | **Covered** (StopCallback is a stub, which is fine) | |
| libetc | PadInitDirect, PadStartCom, PadStopCom, PadGetState, PadInfoMode, PadSetActAlign, PadSetMainMode, PadSetAct | **Partial** | Bodies exist with TODOs. Fine for digital and analog input. Vibration (`PadSetAct`) maps to nothing. |
| libcd | CdInit, CdSync, CdControl, CdControlB, CdControlF, CdRead, CdReadCallback, CdLastCom, CdIntToPos, CdPosToInt | **Partial — replace** | Reads only a `IMAGE.CUE` bin. `CdReadCallback` is unimplemented, so Spyro's `m_IsReading` never clears and loads hang. `CdRead` copies only in `CdReadSync`, which Spyro never calls. `CdControl(B)` ignores Setmode, Setfilter and ReadS. `CdSync(1,NULL)` writes through NULL after GetlocP. `LIBCD.C:328-580,1022` |
| libcd | CdMix | **Missing** | Declared, not defined. Used at `initialization.c:128` and `update.c:338`. |
| CD-XA | (via CdlSetfilter + ReadS) | **Missing** | No XA decode anywhere in PsyCross (README TODO). |
| libspu | SpuInit, SpuSetVoiceAttr, SpuSetKey, SpuWrite, SpuSetTransferStartAddr, SpuIsTransferCompleted, SpuSet/GetVoiceVolume, SpuGetAllKeysStatus | **Partial** | OpenAL backend. Decodes the whole sample at key-on. No ADSR. |
| libspu | SpuGetKeyStatus | **Partial** | Never returns `SPU_RESET` (3), which `spu.c` checks for. |
| libspu | SpuSetCommonAttr, SpuSetTransferMode, SpuRead | **Partial** | `SpuSetCommonAttr` is a stub, so there is no master or CD volume and no music fade. |
| libmcrd | MemCardInit, Start, Stop, Exist, Accept, ReadFile, WriteFile, Sync, CreateFile, Format | **Partial** | Bodies exist (`LIBMCRD.C`). They need to point at `ux0:data/spyro/save/`. |
| libapi | LoadExec | stub | Only used by the Crash demo (`crash_demo.c:38`). Stub it, and **hide** that menu entry (user decision, 2026-10-29). |
| libapi | `_96_init`, `_96_remove` | stub | Harmless. |
| libc | memcpy, strlen, printf, sprintf, exit | host libc | |
| libc | **rand, srand** | **must be ours** | The PsyQ LCG (`next = next*1103515245+12345; return (next>>16)&0x7FFF`), not the host's. Used 566 times. Determinism and trace comparison depend on it. |
| crt0 | `func_8005B8E0`, `_ramsize`, `_stacksize` | replace | Our `main` sets up the arena (§4) and calls the decomp's `main`. |

**Licensing note:** `psyq/include` holds Sony's PsyQ headers. They are user-supplied, and the port must **not** depend on them. The host and Vita builds compile the decomp C against **PsyCross's `include/psx` headers (MIT)** plus a small compatibility header for any naming differences. Only the matching build uses the real PsyQ headers.

---

## 4. Overlays, memory map and pointer risks

### 4.1 How overlays work today
- There are **37 overlays**: 35 levels, plus `credits` and `titlescreen_code` (`overlays/overlays.yaml`). All load at **0x8007AA38** (`main_BSS_END`, from the `OVERLAY main_BSS_END : NOCROSSREFS` emitted by `tools/overlays/generate_overlay_ld.py:3`). Word 0 is the overlay index, followed by `.rodata/.text/.data/.bss`.
- Per-level variants come from compiling the same sources with `-DOVERLAY_NAME=<ovl>`. `NAME_OVERLAY_FUNCTION(f)` → `f_<OVL>` (`include/overlay_pointers.h:11`). `strip_overlay_function.py` removes the switch cases for moby and particle classes that aren't listed for that level (`overlays/mobys.yaml`, `particles.yaml`).
- **Every overlay symbol already has a unique name** (`SpawnMoby_LEVEL_10`, `Terminator_titlescreen_code`, …). The matching build links all 37 into one ELF, which proves there are no name clashes.
- Loading: `LoadLevel` case 2 calls `SetOverlayPointers()`, which stores link-time addresses into function pointers (`g_SpawnMoby`, `g_UpdateMoby`, `g_SpawnParticle`, `g_UpdateParticle`, `g_FlightResultsUpdate`, `D_8007567C`, `D_80075694`, `D_8007574C`, `D_800758D8`; `overlay_pointers.c:426-616`). Then `CDLoadAsync` reads the code bytes to 0x8007AA38 (`loaders.c:1390-1411`). Nothing relocates or jumps to an entry point. The titlescreen and credits code is called directly (`update.c:1449,1459`, `draw.c:2701,2707`).
- The overlay space doubles as a data buffer: the WAD header, level header and cutscene copy buffer (`initialization.c:137,296`, `update.c:1010,1187`). `Terminator_<ovl>` marks the end of the code and is used as a **data pointer**: `g_Buffers.m_CopyBuf = Terminator_LEVEL_xx` (`overlay_pointers.c:392`).

### 4.2 Static-link design for the Vita (no JIT)
1. **Compile all 37 overlay variants into the binary.** The names are already unique, so no rename header is needed (unlike kh1). CMake generates one compile per (overlay, source file) with the same `-DOVERLAY_NAME` and strip pipeline as the Makefile (reusing `tools/overlays/*.py`).
2. **`SetOverlayPointers()` works unchanged.** It already assigns host function addresses. That is the dispatch table.
3. **Overlay reads become data-only.** `CDLoadAsync` to `g_OverlaySpacePointer` still happens: it costs about 80 KB of read and keeps the timing and buffers identical. But the bytes are never executed. A port hook after the read checks the first word (the overlay index) against the level, for diagnostics.
4. **`Terminator_<ovl>` becomes a computed address,** not a symbol: `ARENA(0x8007AA38) + ovl_size[<ovl>]`, using the sizes from the matching build's `build/wad/*.ovl`, emitted into a generated `ovl_table.c`. Level data then lands exactly where it does on the PS1, inside the arena. That preserves the ~1 MB budget and the contiguity checks (`loaders.c:1013,1329-1370`).
5. Drop the unreleased levels 0–4, 7, 9 and 99 (`psx.ld:447-490`, `overlay_pointers.c:428-539,590-605`): make them assert.
6. `strip_overlay_function.py` is kept for fidelity. Using a single unstripped build of `moby_update` would only change behaviour if a class were spawned in a level that strips it. Whether that ever happens is unclear, so don't risk it.

### 4.3 The arena (the PS1 RAM model)
- **The arena is one 2 MB block for PS1 addresses `0x80000000–0x801FFFFF`.** It holds the overlay space, level data, `g_Buffers` (poly buffers 2×0x1C000, world OT 0x4000, particle space, shared animations) and the game "stack" top. `ARENA(a) = g_arena + (a - 0x80000000)`.
- **Requirements:**
  - base **aligned to 16 MB** (24-bit OT links and 21-bit packed pointers rebuild as `base | low_bits`);
  - base **≥ 0x80000000** (bit-31 model tags keep their PS1 meaning).
- **Host (32-bit Linux):** `mmap(0x80000000, 2 MB, MAP_FIXED)`. The arena then sits *at the PS1 addresses*, so emulator RAM dumps compare byte for byte (§7.3).
- **Vita:** `memalign(16 MB, 2 MB)` from the newlib heap, or `sceKernelAllocMemBlock` plus alignment. Vita user memory is normally mapped at `0x81000000+`, so bit 31 is set. **Verify on hardware early in M3.** If it fails, fall back to the "rewrite the tags" option below.
- Game globals stay ordinary C globals; host-linked, not in the arena.

### 4.4 Pointer and data risks, one by one

| Risk | Where | Host and Vita handling |
|---|---|---|
| Offset→pointer fixups on loaded WAD data (`PATCH_POINTER`) | `common.h:34-37`; `loaders.c:98-218,567-672,958-963,1342,1363`; `4BEF8.c:72-74` | Works as-is: 32-bit fields, base + offset, and the bases are arena addresses. |
| SPU addresses in sound tables (+0x1010) | `loaders.c:53` | Not CPU pointers. Works as-is. |
| Bit-31 tag on model pointers (`& 0x7FFFFFFF` = SimpleModel; `(int)p < 0` test) | `loaders.c:315,324,701`; renderers rebuild with `or 0x80000000` | Works when the arena is ≥ 0x80000000. Otherwise use bit 23 as the tag, which is always zero in a 2 MB arena at a 16 MB-aligned base. That's ~6 C sites plus translator hints. |
| 21-bit packed animation-frame pointers | `loaders.c:291-294,349-363`; `r_moby.s:1614-1616` (`srl 8; or 0x80000000`); `balloonist.c:597-605` | The masks are unchanged. Replace the constant `0x80000000` with `g_arena_base` (a translator hint plus C edits). Correct because of the 16 MB alignment. |
| 24-bit OT links (`sh`+`sb`) and the `0x04FFFFFF` terminator | `order_table.s:12-147`; renderers; `graphics.h:96` `PRIM_TAG` | Keep the PS1 layout. PsyCross is built non-extended, and its `nextPrim` does `base | addr24` (patch). **Every primitive linked into the OT must live in the arena.** M1 audits primitives in static globals (`DB`'s DR_* and DrawEnv). If any exist, they get moved into a reserved arena page. |
| 24-bit pointer compares | `loaders.c:1257,1553` (`& 0xffffff`) | Fine within a single arena, because the low 24 bits are unique. |
| Unsigned address ordering (shared animations above level data) | `loaders.c:253` | Preserved by the arena layout. |
| Hard-coded `0x80200000` (top of RAM) | `initialization.c:303,400`; `4BEF8.c:53-63`; `update.c:1137,1191` | → `ARENA(0x80200000)`. A patch or a `PSX_RAM_TOP` macro, in `patches/`. |
| 8 MB devkit branches (`< 0x80200000` else `0x801FE000`; demo record at `0x80600000`) | `balloonist.c:591-594`; `loaders.c:689-690` | Patch them to the retail branch. On the host the comparison would otherwise be meaningless. |
| `_ramsize`/`_stacksize`, crt0 | `asm/data/initialization.sdata.s:7-13`; `psyq.s:22-38` | Replaced by our start-up code. |
| Scratchpad `0x1F800000` (all 1 KB; `$sp=0x1F800200`, `$gp=0x1F8003FC`) | asm only: collision 225, r_environment 373, r_shadows 88, … refs; tables `D_8006CF48/58` (`math.data.s:392-416`) | A static 1 KB array, `g_scratch`. The translator maps `%hi/%lo(0x1F80xxxx)` and the constant tables to `g_scratch+off`. The tables are rebuilt at start-up. |
| Root counter `0x1F801120` | `system_time.s:11-12` (only for the title RNG seed) | A hand-written C function returning a host tick count. |
| CD reads write whole sectors (up to 2047 bytes past `size`, offset truncated to 2048) | `cd.c:93,97,118,122` | Our libcd copies whole sectors too, which keeps the overrun behaviour identical. |
| `LoadExec` (Crash demo) | `crash_demo.c:38` | Stub it, and hide the Crash demo menu entry (patch in `titlescreen.c`). |
| ARM `char` signedness | — | The decomp builds with `-funsigned-char`, so keep that on all targets. |
| Struct layout | all `include/*.h` | No 64-bit types, so 32-bit ARM EABI and i386 match MIPS o32 for these structs. Add `_Static_assert(sizeof/offsetof)` against the matching build's DWARF (generated by a `layoutcheck` tool, as in kh1). The translated code uses raw offsets, so this is load-bearing. |
| Data in `asm/data/*.s` | game data files: ~3,250 `.word`s | Only 24 pointers (`math.data.s`), and those are already symbolic. The `0x80xxxxxx` words in `titlescreen_data.s` are packed sprite fields, not addresses. Convert to C arrays with a script, plus an `abscheck` that stays at 0. |

---

## 5. Rendering path

**What Spyro does** (`src/4BEF8.c:13-41`, `draw.c:2648-2668,850-867`, `asm/order_table.s`):
- Two primitive buffers of 0x1C000 bytes (0x13000 in reduced mode), carved down from the top of RAM. One shared world OT (0x4000 bytes = 2048 buckets of `{tail, head}`, **not** `OT_TAG`s) and a one-entry HUD OT.
- Primitives are appended FIFO per bucket. `func_80016784` chains the buckets from highest to lowest, clears them as it goes, appends the HUD chain, and returns the head for `DrawOTag`.
- 30 Hz: it waits until at least 2 vblanks have passed.
- Draw area 512×224, display 512×240, `isbg=1` (clear to `g_Cyclorama.m_BackgroundColor`), dithering on (`initialization.c:60-70`).
- **Primitive types:** GT3, GT4, G3, G4, FT3, FT4, F3, F4, LINE_F2, DR_TPAGE. Semi-transparency: average (G3), additive (particles, `E1000620`), **subtractive** (shadows, `E1000640`) and the flame GT4 (0x3E).
- **Skybox (cyclorama):** GTE-projected G3 and F3 geometry, vertex colours depth-cued towards a lerped far colour (`r_cyclorama.s:424-440`). `func_80050BD0` (still unmatched) sets up the matrices.
- **Distance fade and LOD:** there's no fog pass. The renderers load the far colour (RFC/GFC/BFC = the per-sector or background colour), then run DPCS or INTPL with IR0, so geometry fades into the clear colour. `m_LodDistance` and `m_CullingDistance` (0x28000, 0x1C000 or 0x14000; `environment.c:17-29`) pick low-poly G3/G4 far geometry or textured GT3/GT4 near geometry.
- Texture animation rewrites UVs and doesn't touch VRAM. VRAM is loaded with `LoadImage` in 512-wide blocks per level.
- **Framebuffer tricks:**
  - Pause, inventory and flight results: StoreImage → CPU 8 bpp grayscale → LoadImage → redraw as FT4 (`draw.c:908-933`, `flight.c:248-258`).
  - Game over: MoveImage freeze, then `isbg=0` (`draw.c:1671-1677`).
  - Boot logos: 24-bit display (`initialization.c:273-387`).

**PsyCross plus vitaGL:**

| Need | Status | Work |
|---|---|---|
| Parse PS1 primitives from an OT | Covered (`PsyX_GPU.cpp:799-850,1432-1619`) | Build with `USE_EXTENDED_PRIM_POINTERS=0`, `USE_PGXP=0`. Patch `nextPrim` to `g_arena_base | addr24` (`libgpu.h:194`). The end marker `0xffffff` already matches Spyro's terminator. |
| VRAM, 4/8/16 bpp textures and CLUTs | Covered (CPU `vram[]`, palette decode in the shader) | The whole 1 MB VRAM texture is re-uploaded whenever anything is dirty (`PsyX_render.cpp:1686-1706`). Fine for per-level loads, but watch the pause-screen path. Dirty rectangles later if needed. |
| Blend modes 0–2 | Covered | |
| Blend mode 3 (B + F/4) | Partial | Uses `glBlendColor`/`GL_CONSTANT_ALPHA`. SceGxm likely has no constant blend colour, so do ×0.25 in the shader on the Vita. Does Spyro even use mode 3? Check in M1. |
| Dithering | Partial (desktop and ES3 only) | It's a shader matrix. Make it optional, default off on the Vita. |
| Mask bit | Partial (ignored) | Spyro doesn't appear to use it. Confirm in M1. |
| Framebuffer readback (StoreImage/MoveImage) | Missing on ES2 | Render into an FBO at 512×240, or reimplement the three effects at a higher level (grab the FBO and grayscale it in a shader). The higher-level route is cheaper on the SGX. Needed in M2 (pause menu), not M1. |
| `isrgb24` boot images | Missing | Small: decode the 24-bit region to a texture. |
| vitaGL backend | Missing | Add a `__vita__` branch to `PsyX_render.h/.cpp`: vitaGL context instead of SDL_GL, GLSL ES 1.00 shaders (already present), no VAO (or check vitaGL's VAO support), no PBO or blit. `GL_LUMINANCE_ALPHA` VRAM texture. Keep it in `patches/PsyCross/`. |
| Draw-call batching | Partial | PsyCross starts a new split on every blend, texture-mode or texture-page change (`PsyX_GPU.cpp:673-721`). Spyro interleaves G and GT primitives in depth order, so expect hundreds of draw calls a frame. Measure on hardware in M3. The fallback is our own GP0→vertex-batch renderer with a texture-atlas cache. |
| GTE projection | Covered | Widescreen field of view later (M4): scale X in RTPS/RTPT at the single choke point in our GTE model, plus widen the culling the game does in `r_environment` (cull sites found by the translator's annotations). |
| 60 fps | — | **Goal: as high as possible, 60 fps where the hardware allows** (user, 2026-10-29). Logic stays at the original 30 Hz, because `g_DeltaTime` is clamped to 2–4 (`main.c:24-31`), so logic below 2 vblanks isn't supported. Presentation interpolates camera, moby, Spyro and particle transforms between the last two ticks, then renders at 60. That needs a "render twice per tick" path: snapshot the transforms, re-run the draw functions with interpolated matrices. That's where readable C in pete, camera and the renderers pays off. Plan the renderer translation with this in mind from M2, but deliver it in M4. |

**Target resolution:** render PS1 coordinates (512×224 or 512×240) into a 960×544 framebuffer, scaled by PsyCross's projection (it already renders at window resolution). Widescreen is M4.

---

## 6. Audio and CD

**What Spyro does:**
- **WAD reads** (`src/cd.c:78-128`): `CdControl(Setmode, Speed)` → `CdIntToPos(37 + offset/2048)` → Setloc → `CdRead(n, buf, Speed)`. Completion comes through `CdReadCallback(CDReadDone)` clearing `m_IsReading`, plus `CdSync(1,0)` polling, with a timeout and a retry through `CdInit` (`cd.c:7-36`). The loaders are async state machines (`loaders.c:815-944`). The WAD sector (37) is hard-coded (`initialization.c:134`).
- **XA music:** 6 `PETEXA*.STR` files × 8 channels. Start LBAs are hard-coded in `D_8006F200` (0xEA60 = 60000 … 0x37450 = 226384, which match `manifest.json`), lengths in `D_8006EEDC`. Play is Setmode (SF|RT|Speed), then `Setfilter{file 1, chan song%8}`, then `CdControlB(ReadS)`. Looping is done by polling `CdControlF(GetlocP)` + `CdSync(1, buf)` and reading `buf[5..7]` (`music.c:12-89`). Fades use `SpuSetCommonAttr` CD volume. `CdMix` routes left-only.
- **SPU:** 24 voices keyed once per frame (`spu.c:766-779`). ADSR is fixed (ar/dr/sr/rr 0, sl 15; `spu.c:39-56`). There's no reverb. Sound banks go in with `SpuWrite` at 0x1010. The dragon cutscenes swap 32 KB SPU blocks in and out (SpuRead/SpuWrite). `SpuGetKeyStatus` is expected to return `SPU_RESET`.

**Plan:** replace PsyCross's libcd and libspu with our own backend in `src/platform/common/` (a patch drops PsyCross's versions from its CMake):
1. **`libcd` over files.** A virtual disc built from `ux0:data/spyro/manifest.json` (written by `tools/extract_disc.py`): LBA → (file, offset, form). Form 1 files read 2048 bytes per sector. XA files read 2336-byte sectors (subheader + data).
   - `CdRead` queues a job.
   - A worker thread (SDL thread, or `sceKernelCreateThread`) reads, then completes on the game thread at the next VSync tick. It fires `CdReadCallback` and sets the `CdSync` status, preserving async behaviour with deterministic completion timing (a configurable "sectors per frame" so loads are as fast as the hardware allows, but always complete at a frame boundary).
   - Implements Setloc, Setmode, Setfilter, ReadS, Pause, GetlocP (reporting the current XA sector), `CdMix` and `CdLastCom`.
2. **XA ADPCM decoder** (our own, from the public format spec): streams the selected file and channel (filtered by subheader file and channel), 37.8 kHz stereo or mono, resampled into the mixer.
3. **Software SPU mixer** (our own): 24 voices, ADPCM decode from a 512 KB SPU RAM image, pitch, ADSR, key status (incl. `SPU_RESET`), master and CD volume, plus the XA input. It's driven by an **SDL2 audio callback** on both host and Vita, as CLAUDE.md asks. PsyCross's OpenAL path is dropped: it lacks ADSR, true key status and CD volume, and it adds a dependency.
4. **Memory card:** PsyCross's `LIBMCRD.C`, with its path pointed at `ux0:data/spyro/save/` (host: `./save/`).

---

## 7. How to translate the unmatched functions and the hand-written asm (recommendation)

### 7.1 The options weighed
- **(a) Hand-decompile everything to non-matching C.** That's ~44,500 instructions of MIPS. It's realistic for the 20k of plain compiler output (m2c drafts, cleaned up), but slow and error-prone for the 24.5k of hand-tuned GTE code, where readable C gains little. M1 and M2 would wait weeks on it.
- **(b) Our own MIPS→C translator.** MIPS I is small (≈45 opcodes), function boundaries are known, and the splat `.s` files hand us **symbolic relocations and named jump tables**. So generated code can do `*(s32*)((u8*)&g_Camera + 0xD0)` against real host globals: no RAM image, no BIOS, no interpreter. The odd parts (computed `jr` into code tables, `jr $t4` local subroutines, `sp/gp/fp` as data registers, scratchpad, GTE) are all statically visible and handled with a per-site hints file. Output is ugly but correct, and it's *testable* against the original.
- **(c) psxrecomp techniques.** Its architecture (whole binary, emulated hardware, run-time overlay compile, interpreter fallback) doesn't fit a no-JIT, statically linked Vita port. Its licence is **PolyForm Noncommercial** (`../psxrecomp-ref/LICENSE`, not "unclear" as `research.md` says), so we only take ideas from its docs: delay-slot handling, GTE as function calls, running the oracle out of process.

### 7.2 Recommendation: (d), translator first, hand-written C where it pays off
1. **Write `tools/mips2c/` (Python) in M1.**
   - Input: a `.s` file. Output: one C function per `glabel`, with MIPS registers as locals (`u32 v0, a0, …`) and an explicit emulated stack (`g_mips_stack`, 64 KB in the arena, so `$sp`-relative code and pointers to locals keep working).
   - Calls: `jal X` turns into a call through a generated thunk. C functions get typed arguments from the headers. Translated functions use a uniform `(a0..a3, stack)` signature plus a typed wrapper, so C code can call them.
   - `%hi/%lo(sym+off)` turns into the host address of `sym`, plus the offset. `0x1F80xxxx` turns into `g_scratch`. GTE instructions turn into calls on the GTE model, which is register-exact.
   - A `hints.yaml` lists computed-jump tables (label, stride, count), local link registers, the `or 0x80000000` rebuild sites, and cull sites for widescreen.
   - Self-check: every instruction in the input is either translated or listed in hints. Unknown patterns are a build error, not a silent guess.
2. **Translate all ~44.5k instructions mechanically in M1/M2.** That's 18 unmatched functions plus the 16 hand-written files. It gives a complete, behaviour-equivalent game early.
3. **Then replace, in priority order, with hand-written C.** m2c (GPL-3.0) is approved as an offline drafting tool (user, 2026-10-29). It runs from `tools/`, is never linked, and its output is cleaned up by hand:
   - the trivial files (`memory`, `order_table`, `system_time`, `particle_alloc`, `images`), at once, in M1;
   - **pete and camera**, in M2–M4, because 60 fps interpolation and widescreen need to understand them;
   - renderers only where profiling on the Vita says so (M3+).
   Each replacement must pass the same differential tests as the code it replaces.
4. **Why not hand-decompile first?** The 3,865-instruction `func_80043FE4` would take one person days to decompile by hand, and the result still has to be proven equivalent. With the translator it is generated in seconds, and the same tests prove it. The hand-written rewrite can then proceed against a working baseline.

### 7.3 Verification: proving each function behaves like the original
Three layers, from cheapest to most complete:
1. **Function-level differential tests (main tool).**
   - Run the original in **PCSX-Redux** (chosen 2026-10-29: its built-in Lua can set breakpoints with callbacks and read RAM, registers and GTE in the emulator itself, so the oracle needs no external GDB driver; it also has a GDB server as a fallback). It runs out of process only and is never linked (GPL). BIOS: `disc/bios/SCPH1001.BIN` (SHA-1 `10155d8d…8ebf`, gitignored). DuckStation stays a cross-check for accuracy questions.
   - A Lua script loaded into PCSX-Redux plus a Python side (`tools/oracle/`, modelled on kh-bbs-vita's `tools/oracle/trace_diff.py`) breaks at a function's entry. It records the argument registers, all 2 MB of RAM and the scratchpad, plus the GTE register file. At the return it records `v0/v1`, RAM, scratchpad and GTE again.
   - On the host build the arena *is* `0x80000000–0x801FFFFF`. Game globals get loaded from the snapshot by symbol: names, addresses and sizes come from the matching build's `build/psx.map`, and pointers into the global area are remapped via that table. Then the translated or hand-written function runs with the same inputs, and the outputs are diffed.
   - We collect ~20–100 snapshots per function from play: title, Artisans, flight, cutscene and credits. Each one becomes a regression test. GTE ops get the same treatment, as a unit test of the GTE model.
2. **Frame traces.** The host build logs a per-frame record: `g_Gamestate`, `g_DeltaTime`, Spyro's position, state and velocity, the camera, moby count and a hash of the RNG. The oracle Lua script logs the same from PCSX-Redux memory each vblank. `trace_diff.py` reports the first divergent frame and field. **Determinism hooks:** replay the recorded `g_UnprocessedFrames` (lag frames) and the pad input per frame; seed the root-counter read the same way.
3. **The game's own attract demos.** Title-screen demo mode replays recorded input (`TSM_Demo`), which gives free, deterministic end-to-end tests that cover Spyro's controller, the camera and collision. A demo that ends with Spyro where the emulator's does is a strong signal.

Each translated or replaced function gets a `docs/translation-log.md` row: method (mips2c vX, or hand), tests (snapshot count, demo or trace coverage) and known differences.

**As built (M2, 2026-10-30):** layers 1–3 run (`tools/oracle/`, `docs/host-build.md` "Oracle"). Layer 1 needed no symbol-by-symbol loading: with the PS1 layout (D23) the emulator's RAM loads into the arena with one `memcpy`. What differs by construction is handled explicitly in `spyro --replay`: scratchpad pointers (0x1F80xxxx) compare in the port's form, packed pointers that mips2c rebases on the arena count as equal, v0/v1 count only when the function set them, saved caller registers are told apart, and interrupt-written RAM is listed with its writer in `tools/oracle/ignore_ranges.txt`. Results: `docs/translation-log.md`.

Optional later: a "PS1-exact layout" host build (each global in its own section via `-fdata-sections`, placed at its `psx.map` address by a generated linker script). Snapshots would then load with a single `memcpy`. Only worth it if pointer remapping in layer 1 turns out to be painful.

---

## 8. Reused build patterns
See `docs/decisions.md` D4–D8. Briefly:
- CMake shape and `tools/tc` Docker wrapper from kh-bbs-vita.
- Host flags, `layoutcheck`/`abscheck`-style static checkers and pad-script replay from kh1-decomp.
- Vita packaging, link list, LiveArea, clocks, logging and README layout from the Duke and kh1 Vita builds.
- Overlay handling from kh1-decomp's `native-overlays.md`. That's simpler here, because the decomp already gives every overlay symbol a unique name and dispatches through `SetOverlayPointers`.

**Correction to CLAUDE.md:** kh-bbs-vita has **no Vita CMake or vitaGL code yet** (`hosts/vita/` is empty, and none of the three projects calls `vglInit`). The vitaGL init, CMake Vita target and PsyCross-on-vitaGL work is new.

---

## 9. M1 task breakdown: host 32-bit build boots to the title screen

**Definition of done:** `build/linux32/spyro` launched with `--disc disc/files` shows the boot logos, the title intro (Spyro landing, logo), and the "Press Start" / New Game / Load menu, and responds to pad input. A screenshot and log are saved in `logs/run1-*`. Pete, camera and level gameplay may still be stubs. Translated code is allowed.

Order matters: each step runs before the next starts.

1. **Scaffolding**
   - [x] `CMakeLists.txt` (cmake 3.20, C11, `SPYRO_HOST=linux32|vita`), `src/platform/linux32/CMakeLists.txt`, `cmake/` helpers (`prepare.cmake` copies and patches the submodules into the build tree).
   - [x] `tools/docker/host.Dockerfile` and the `tools/tc` wrapper. *Changed:* native arm64 with `gcc-i686-linux-gnu` and `qemu-user`, no SDL/GL (decisions D14, D18).
   - [x] `tools/build_matching.sh`: rsync to `build/s1-matching`, apply `patches/spyro-1/`, `make all` under `pipefail`, SHA-1 check. Leaves `build/s1-matching/build/psx.map`, `psx.elf` (DWARF) and `wad/*.ovl` for the generators. *(Done in M0 as an environment fix.)*
   - [x] `tools/check_no_assets.sh`. The pre-commit hook is a wrapper script in `$(git rev-parse --git-common-dir)/hooks/pre-commit` that runs `$(git rev-parse --show-toplevel)/tools/check_no_assets.sh` from whichever checkout or worktree is committing, and exits 0 when that file is missing (branches that predate it). A symlink doesn't work: it resolves against the main checkout. The wrapper's text is in the script's header comment.
2. **PsyCross on the host, as a library**
   - [x] PsyCross as a static library (`cmake/psycross.cmake`) with `USE_EXTENDED_PRIM_POINTERS=0` and `USE_PGXP=0`.
   - [x] Patch 1 (`patches/PsyCross/0001-arena-ot.patch`): `nextPrim` ORs in `g_psx_arena_base`.
   - [x] Excluded by CMake: `LIBCD.C`, `LIBSPU.C`, `LIBMCRD.C`, `PsyX_SPUAL.cpp` and the SDL/GL platform files. Patch 2 makes SDL optional, 3 splits OT packets into GP0 commands, 4 gives `DRAWENV` PsyQ's layout (see `upstream-notes.md`).
   - [x] Builds for i686.
3. **Decomp C on the host**
   - [x] `src/compat/` (`libgpu.h` adds `RECT`; `port_prefix.h` empties `INCLUDE_ASM`, adds `PSX_ADDR` and `PortFatal`). No Sony headers.
   - [x] All of `src/*.c` and `src/gamestates/*.c` compile with `-m32 -funsigned-char -fno-strict-aliasing -ffp-contract=off` (plus `-fcommon`, `MODERN_COMPILER`; D21).
   - [x] Patches for the §4.4 constants (`patches/spyro-1-port/0003`: `0x80200000` via `PSX_ADDR`, the devkit branches). `LoadExec` and hiding the Crash demo entry: not done yet (the entry isn't reached in M1).
   - [x] Our `rand/srand` (PsyQ LCG, seed 0 at boot): `src/game/rand_psyq.c`.
4. **Data**
   - [x] `tools/data2s.py` (host assembly instead of C, D15) plus `abscheck` and the scratchpad relocation table.
   - [x] `tools/layoutcheck.sh`/`.py`: compares DWARF layouts of the decomp compiled for mipsel against PsyQ's headers with the port's objects. 94 types, 0 mismatches after PsyCross patch 0004.
5. **Arena and start-up**
   - [x] `src/platform/common/arena.c` (mmap at 0x80000000) and `src/game/port.c`: scratchpad = first KB of the arena, scratchpad tables rebased (`D_8006CF48`, `D_8006CF58`, `D_8006D0D8`), emulated stack top at 0x80020000.
   - [x] `main()` → `spyro_main()` (port set-up) → decomp `main()`.
   - [x] Audit: the only static primitive linked into the OT is `D_8006BBE0` (terminator, `order_table.s`); C code only links poly-buffer primitives. Its copy lives in the arena at its PS1 address.
6. **Overlays**
   - [x] `cmake/overlays.cmake` + `tools/overlays/port_overlays.py`.
   - [x] Generated `ovl_table.c`: `Terminator_<ovl>` = its matching-build address in the arena (D20).
   - [x] All 37 build. `titlescreen_code` and the two attract-demo levels run.
7. **`tools/mips2c` v0**
   - [x] Parser for splat `.s` (glabel/jlabel/dlabel, `.rodata.jtbl` and rodata strings, `%hi/%lo/%gp_rel`).
   - [x] Emitter: delay slots, load delays, all MIPS I ALU/branch/load/store, `mult/div`, calls, `jr`/`jalr` dispatch, jump tables (D16).
   - [x] Self-check: unknown instructions or operands stop the build.
   - [x] First targets: `math.s`, `42CC4.s`, `moby_lists.s`, `particle_alloc.s`, plus everything else (below).
8. **GTE model**
   - [ ] Decide between wrapping PsyCross's `PsyX_GTE.cpp` and writing our own (decision D2). **Interim:** PsyCross's GTE is used (D21); decide after the unit test.
   - [ ] A unit test with ~1,000 vectors per op captured from PCSX-Redux. **Blocked:** needs PCSX-Redux (download not yet approved).
9. **mips2c v1: the renderers for the title screen**
   - [x] No hints needed: unit-wide gotos and address dispatch cover `jr $t4`, `$sp` as data and `j`-tables. `order_table` is hand-written.
   - [x] Translated and linked.
   - [x] All seven renderers and `collision.s` are translated; no stubs.
10. **libcd over files plus a minimal SPU**
    - [x] `src/platform/common/libcd.c` (tests/cd).
    - [x] `src/platform/common/libspu.c`: SPU RAM image, voice lifetime from ADPCM end flags, `SPU_RESET`. Silent.
11. **Unmatched functions for M1**
    - [x] All 18 translate; no stubs. The attract demos run pete and camera code.
12. **Boot to the title**
    - [x] Fixed: CD busy-wait (D17), KUSEG pointers (D16), scratchpad tables, memory cards (D19), OT packets (PsyCross 0003), `DRAWENV` layout (0004). The `isrgb24` logos work in the soft renderer.
    - [x] `--frames N --shot/--shot-every --pad --save`: `logs/run1-final3` (title and menu), `logs/run1-demo4` (attract demos).
13. **Oracle bring-up (can run in parallel from step 7)**
    - [ ] Install PCSX-Redux (ask before downloading). **Not started:** waiting for approval to download. Then write `tools/oracle/oracle.lua`.
    - [ ] The first differential test: `math.s` translated vs the original. Blocked on the oracle.

**M1 is not:** playable levels, audio output, the Vita, widescreen or 60 fps.

**M1 status (2026-10-29):** the definition of done is met on the host (`logs/run1-final3`). The boot logos, intro, Spyro logo with PRESS START, then Start → "Create save file?" → New Game / Load Game with three slots, and the cursor follows the pad script. The idle attract demos also play two levels and return to the title (`logs/run1-demo4`). Still open: step 8's GTE test and step 13's oracle (both need PCSX-Redux), and hiding the Crash demo entry.

---

## 9b. M2 status (2026-10-29): Artisans on the host

**Definition of done (CLAUDE.md):** all unmatched and asm-only functions in C (done in M1 by mips2c, D16), and Artisans fully playable on the host.

- [x] **Rendering gap found and fixed.** Near world geometry was missing (the title intro's "sky and sand", flat clear colour under Spyro). Cause: `r_environment.s` uses the sign bit of a pointer into `D_8006FCF4` as a mode flag. Fix: the game's data at its PS1 addresses (D23).
- [x] **Playable in Artisans (scripted evidence, `docs/host-build.md`):** walk, run, charge, flame (kills an enemy), jump and glide, free Nestor (dragon cutscene), save fairy (`func_800314B4`, "GAME SAVED", card file written), pause, options, inventory (StoreImage grayscale background), respawn and game over. Gem collection: in the attract demos (Stone Hill, Doctor Shemp).
- [x] **A human can play it:** `--serve PORT` in any browser, keyboard or gamepad, with sound (D22).
- [x] **Audio:** software SPU mixer and XA decoder (`libspu.c`, `xa.c`), WAV output; `CdMix` order from the hardware registers.
- [x] Crash demo combo disabled (D11, patch 0004). `MobyRotateToAngle`'s missing argument analysed (`upstream-notes.md`, deferred to the oracle in M4).
- [x] Tooling: `--trace`, `--step`/`tools/step.sh`, `--poke`, `--tri-dump`, raw VRAM dumps, `tools/oracle/trace_diff.py`.
- [ ] **Oracle (PCSX-Redux): not started, waiting for download approval.** So no GTE unit vectors, no function-level differential tests, no emulator frame traces. Nothing in `translation-log.md` is "verified against the emulator" yet.
- [x] Loading the save from the title (`logs/run7-load`: Artisans, 1 dragon).
- [x] Collecting gems in Artisans itself: four red gems on the stone platform by the waterfall pool (`logs/run9-gems`). Supercharge isn't in Artisans (no type-4 surface).
- [ ] Not shown by a run yet: a human playing through the whole homeworld.
- [ ] Readable hand-written C for pete and camera (for M4's interpolation): not started.

## 10. Open questions: answered 2026-10-29
1. **Bounty:** a decomp-based native port counts. There are no extra rules. Aim for high FPS: 60 if possible (§5).
2. **m2c:** allowed, as an offline tool (§7.2).
3. **Oracle emulator:** PCSX-Redux, with DuckStation as a cross-check (§7.3). BIOS moved to `disc/bios/SCPH1001.BIN`.
4. **Audio:** our own SDL2 software SPU and XA mixer replaces PsyCross's OpenAL libspu (§6).
5. **Arena alignment:** a 16 MB-aligned allocation is accepted (§4.3).
6. **VPK:** title "Spyro Vita", title ID `SPYV00001` (decisions D7).
7. **Crash Bandicoot demo:** hide the menu entry (§3, §4.4).
8. **Docker Rosetta:** already on (`UseVirtualizationFrameworkRosetta: true` in Docker Desktop's settings). Note: the Python crashes in M0 happened *with* Rosetta, so the `-m32` host build may be flaky too. If it is, use `-j1` for the affected steps, or a Linux x86 VM.
9. **Upstream bug:** not reported. Recorded in `docs/upstream-notes.md`.
10. **Doc fixes:** applied to CLAUDE.md and `docs/research.md`.

New questions will be raised as M1 runs into them.

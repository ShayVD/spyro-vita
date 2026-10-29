# Upstream notes

Problems found in the submodules that we work around locally instead of reporting. Each entry says what's wrong, where, and how we deal with it. Drop an entry, and its patch, once upstream fixes it.

## TheMobyCollective/spyro-1

### HEAD `d2ac00b` doesn't compile with GCC 2.7.2: `MobyRotateToAngle` call has too few arguments
- **Found:** 2026-10-29, during the M0 matching build.
- **Where:** `src/overlays/moby_update.c:22102` and `:22108` (the `#ifndef MODERN_COMPILER` branches). Only the `LEVEL_33` overlay variant compiles that code, after `tools/overlays/strip_overlay_function.py` strips the cases for other classes.
- **Why:** commit `0d228ab` (2026-10-03) added the full prototype `int MobyRotateToAngle(Moby *, int, int, int, int)` to `include/moby_helpers.h:165`. The original game calls it with 4 arguments (the source comment says "One param too little"), and GCC 2.7.2 rejects that call as `too few arguments to function`.
- **Why it went unnoticed:** the Makefile pipes `cpp | cc1 | maspsx | as` without `pipefail`, so a cc1 error leaves an empty object behind and the build only fails at link time, with unrelated undefined symbols.
- **Our fix:** `patches/spyro-1/0001-moby_update-unprototyped-MobyRotateToAngle-call.patch` calls it through an unprototyped (K&R) function-pointer cast, `((int (*)())MobyRotateToAngle)(moby, props->m_0x14, 4, 0x10)`. The build is byte-identical: `PSX.EXE` SHA-1 `84e3728a…e0c5` and `LEVEL_33.ovl` pass `sha256sum -c`. `tools/build_matching.sh` applies it to the build copy only.
- **For the port:** the host and Vita builds compile with a modern compiler, where the `MODERN_COMPILER` branch passes the 5th argument as `0`. The original passes only 4 arguments, so the callee reads its 5th (`continueRotation`) from whatever stale value sits in the caller's outgoing argument slot at `16($sp)`. That isn't necessarily `0`, so behaviour may differ. Check what value is really there with the oracle. Add a differential test for this call site in M2.
- **What the original reads (M2, from the matching build's `LEVEL_33/moby_update.o`):** both 4-argument calls (`0x8208`, `0x8220` in `UpdateMoby_LEVEL_33`) are `move a0,s3; lw a1,20(s1); li a2,4; jal; li a3,16`, with no store to `16(sp)`. `UpdateMoby_LEVEL_33` has a 560-byte frame and 53 stores to `16(sp)` (20 of them `sw zero`, the rest `s8`, `v0`, `t1`, `s3`, `s0`). So `continueRotation` is the 5th argument of the most recent 5-or-more-argument call made from that frame: earlier in the same moby loop, or in a previous frame's call (the frame sits at the same stack depth every tick). The Ice Wizard path itself makes no 5-argument call before it.
- **Effect:** `continueRotation` only matters once the wizard is within `0x10` of its target angle: nonzero turns it the last few units, `0` stops. It's Wizard Peak only (class 285), not Artisans.
- **Status:** the port passes `0`. To match exactly, the oracle should read `16(sp)` at `0x8208`/`0x8220` (overlay base + offset) during Wizard Peak; if it is ever nonzero, emulate the slot with a static that the 5-argument call sites in that function update. Deferred to M4 (whole-game run), as Artisans doesn't reach it.

### Makefile has no `pipefail`
- **Where:** `Makefile`, the `build/src/%.o` and `OVERLAY_SOURCE` recipes.
- **Effect:** any failure in a pipeline stage (cpp, cc1, maspsx, the Python filters) produces a 776-byte empty object instead of a build error.
- **Our fix:** `tools/build_matching.sh` runs `make SHELL='/bin/bash -o pipefail'`.

### Nested submodule `tools/maspsx`
- Not an upstream bug, but easy to miss: clone with `--recursive`, or run `git submodule update --init --recursive`. `tools/build_matching.sh` checks for it.

## OpenDriver2/PsyCross (`e56e4cd`)

Found while running Spyro on the linux32 host (M1). Each is worked around in `patches/PsyCross/` or by replacing a file.

- **Multi-command OT packets.** The non-extended OT walker expects a tag word before every command in a packet. `ProcessDrawEnv` treats an E1h–E6h command as consuming the packet's whole length. Spyro's renderers put several GP0 commands after one tag, as the GPU allows: 710 parse errors in 30 s. Patch 0003 splits packets by GP0 command size.
- **`DRAWENV` layout.** It has an extra `drt` byte under `#pragma pack(1)`, so it is 93 bytes against PsyQ's 92. That shifts every game struct that embeds it (Spyro's `DB`). Patch 0004 removes the (unused) byte. Found by `tools/layoutcheck.sh`.
- **GP0(01h)** is treated as `DR_MOVE`, so a cache-clear command would trigger a VRAM move. GP0(02h) fill rectangles aren't handled. Patch 0003 handles these.
- **`LIBMCRD.C`** calls `fopen` and uses the result without a NULL check (crash when the card file is missing). It has no file-level operations behind it. Replaced by `src/platform/common/libmcrd.c`.
- **`GR_ClearVRAM`** (GL renderer) packs the colour as `b<<11` with no 8-to-5-bit conversion. `DR_AREA` parsing makes the clip one pixel narrow. `isbg` clears only happen in `BeginScene`, behind a flag that only `EndScene` resets. The software renderer works around these; the vitaGL backend (M3) needs to as well.
- **`PutDispEnv`:** `activeDispEnv` lags one call behind with double buffering. Use `currentDispEnv` for what's on screen.

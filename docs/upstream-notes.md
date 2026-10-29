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

### Makefile has no `pipefail`
- **Where:** `Makefile`, the `build/src/%.o` and `OVERLAY_SOURCE` recipes.
- **Effect:** any failure in a pipeline stage (cpp, cc1, maspsx, the Python filters) produces a 776-byte empty object instead of a build error.
- **Our fix:** `tools/build_matching.sh` runs `make SHELL='/bin/bash -o pipefail'`.

### Nested submodule `tools/maspsx`
- Not an upstream bug, but easy to miss: clone with `--recursive`, or run `git submodule update --init --recursive`. `tools/build_matching.sh` checks for it.

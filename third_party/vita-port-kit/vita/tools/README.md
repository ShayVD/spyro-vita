# vita/tools

Host-side scripts. Each says where it came from in its header.

| Tool | What it does |
|---|---|
| `get_vitasdk.sh` | vitasdk into `$VITASDK` without docker or GitHub releases: the vitasdk/vitasdk image's toolchain layers from mirror.gcr.io, pinned by digest and sha256-checked (from kh1-decomp). On a desktop the usual vdpm install is simpler. |
| `hw_run.sh` | a run on a real Vita over vitacompanion: install or update the app, set the owner's files aside, write the run's `config.txt` (+ pad script, save folder), launch, wait (optionally quiet), stop at `frames=` / hang / crash dump / timeout, fetch the log and files, put the owner's files back however it ends. One run at a time on the machine, across games (a lock in `~/.cache/vita-port-kit`; a wait that times out writes `hw_run: NOT RUN: ...` to `hw-run.txt`). The eboot.bin goes up as `eboot.bin.hw-new`, size-checked, then renamed into place. `hw-run.txt` and `hw-run.env` name the eboot's and the VPK's SHA-1s and the VPK's path. `--put-eboot FILE` (a VPK or an eboot.bin) only installs it, under the lock, and reads it back (both SHA-1s, `READBACK IDENTICAL`); `HW_KEEP_APP=1` puts the Vita's eboot.bin from before the run back afterwards; `HW_ELF=` is copied next to each crash dump fetched. From spyro-vita (its D77), which kh1-decomp had a cut-down copy of. |
| `hw_check.sh` | read-only: can `hw_run.sh` reach the Vita, is the game installed, leftovers, crash dumps, plus the game's own checks. While another run holds the lock it says `in use by PID N (GAME): not probing` and exits 0 without touching the network (`HW_CHECK_FORCE=1` probes anyway). |
| `hw_ftp.py` | vitacompanion's FTP and command port; refuses to change anything outside the game's data and app folders. While `hw_run.sh`'s lock holds a live PID it refuses every operation, reads and commands too, with exit 3 (`the Vita is in use by PID N (GAME)`), except for the lock's holder (`HW_LOCK_HELD`, exported by `hw_run.sh`) and the processes it started; `HW_IGNORE_LOCK=1` overrides, and a lock left by a dead PID doesn't count. `hw_ftp.py lock` asks without touching the network. |
| `hw_mock.py` | a pretend Vita on localhost (FTP + command port + a fake game) for testing the above; like ftpvita it won't rename onto a name that is there. |
| `vpk_pack.py` | a linked ELF to a VPK: refuses an ELF that lost `sceUserMainThreadStackSize` / `_newlib_heap_size_user`, then vita-elf-create, make-fself (a safe app; `--unsafe` for an unsafe one), mksfoex (`ATTRIBUTE2=12`), pack-vpk, optionally with plain placeholder LiveArea art. `show` prints a VPK's `param.sfo` values, whether its `eboot.bin` is safe or unsafe, and the file list (diff two builds). Warns when the text segment leaves under 16 KiB before the data segment, and names the fix (`__sce_headroom`, gotchas 17) when vita-elf-create fails on it. The CMake twin is `../cmake/VpkApp.cmake` (`SCE_HEADROOM`, `UNSAFE`). From kh1-decomp `tools/vita/build.py`. |
| `psp2dmp.py` | a crash dump (`ux0:data/psp2core-*.psp2dmp`) read: modules and segments, threads with state, stop reason (`0x30004` data abort ..., with the faulting address) and registers; with `--elf` the PC and LR named from the unstripped ELF (load base subtracted; the ELF picked by size from a directory), `--stack THREAD` the return addresses on a stack, `--title` the title ids in it. Python 3, stdlib; addr2line if on `PATH`. Written for the kit from the format (facts from spyro-vita sessions' readers, vita-parse-core, psp2_core_parse; no code taken). Test: `test/psp2dmp_test.sh` (python3 only; a synthetic dump and ELF). |
| `livearea_png.py` | the Vita's rules for sce_sys art: `check` PNGs (8-bit indexed at 128×128 / 840×500 / 280×158), `from` converts a player's own RGB / RGBA art to fit, `vpk` checks a finished VPK's required files and art. Plain Python. From spyro-vita `tools/vita/make_livearea.py` (its PNG half) and `tools/ci/build_vpk.sh`. |
| `hw.game.example` | the settings and hooks a game keeps in its own `tools/vita/hw.game`. |

A game wires them up with two small wrappers that set `HW_GAME` (kh1-decomp and spyro-vita do this):

```bash
#!/usr/bin/env bash
# tools/vita/hw_run.sh: vita-port-kit's runner with this game's settings (tools/vita/hw.game)
root=$(cd "$(dirname "$0")/../.." && pwd)
HW_GAME=$root/tools/vita/hw.game exec "$root/third_party/vita-port-kit/vita/tools/hw_run.sh" "$@"
```

`hw_ftp.py` and `hw_mock.py` are Python; spyro-vita wraps them the same way (`tools/vita/hw_ftp.py`):

```python
#!/usr/bin/env python3
import os, runpy, sys
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("HW_GAME", os.path.join(root, "tools/vita/hw.game"))
kit = os.path.join(root, "third_party/vita-port-kit/vita/tools/hw_ftp.py")
sys.argv[0] = kit
runpy.run_path(kit, run_name="__main__")
```

**Every run on a real Vita needs the owner's explicit yes first** (`../docs/hardware-runs.md` §0); the tests below use
the mock only.

Tests: `test/livearea_png_test.sh` (python3 only). `test/vpk_pack_test.sh` (needs vitasdk, cmake, ninja; see `../cmake/README.md`). `test/hw_run_test.sh` runs `hw_run.sh` and `hw_check.sh` against `hw_mock.py` as vitacompanion 1.08 and 1.05: a
run to `frames=` with a pad script, the owner's files back untouched, the build's SHA-1s in `hw-run.txt` and
`hw-run.env`, a hang with a crash dump (`HW_ELF` copied next to it, `HW_KEEP_APP=1` putting the eboot.bin back),
`--restore` after a SIGKILL and after an eboot.bin upload cut off before its rename, `--put-eboot` with a VPK, a bare
eboot.bin and a VPK without one, `hw_ftp.py` refusing writes outside the game's folders; then the lock, held by a
`sleep` the test starts and kills by PID: `hw_ftp.py` exits 3 (reads and commands too) but lets `HW_IGNORE_LOCK=1`,
`HW_LOCK_HELD` and a script holding the lock itself through, `hw_check.sh` doesn't probe (`HW_CHECK_FORCE=1` does), a
run whose lock wait times out writes `NOT RUN`, a stale lock blocks nothing, `--put-eboot` waits for the lock; and a
game that is a git checkout (its `tools/vita/hw.env` read, `HW_VPK` found from its root, the commit named in the run's
account: fixed 2026-11-10, the game root came out as two lines in any git checkout before).

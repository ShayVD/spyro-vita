# vita/tools

Host-side scripts. Each says where it came from in its header.

| Tool | What it does |
|---|---|
| `get_vitasdk.sh` | vitasdk into `$VITASDK` without docker or GitHub releases: the vitasdk/vitasdk image's toolchain layers from mirror.gcr.io, pinned by digest and sha256-checked (from kh1-decomp). On a desktop the usual vdpm install is simpler. |
| `hw_run.sh` | a run on a real Vita over vitacompanion: install or update the app, set the owner's files aside, write the run's `config.txt` (+ pad script, save folder), launch, wait (optionally quiet), stop at `frames=` / hang / crash dump / timeout, fetch the log and files, put the owner's files back however it ends. One run at a time on the machine, across games (a lock in `~/.cache/vita-port-kit`; a wait that times out writes `hw_run: NOT RUN: ...` to `hw-run.txt`). The eboot.bin goes up as `eboot.bin.hw-new`, size-checked, then renamed into place. `hw-run.txt` and `hw-run.env` name the eboot's and the VPK's SHA-1s and the VPK's path. `--put-eboot FILE` (a VPK or an eboot.bin) only installs it, under the lock, and reads it back (both SHA-1s, `READBACK IDENTICAL`); `HW_KEEP_APP=1` puts the Vita's eboot.bin from before the run back afterwards; `HW_ELF=` is copied next to each crash dump fetched; `HW_NO_INSTALL=1` installs nothing and runs only when the installed eboot.bin is the VPK's (or `HW_EBOOT_SHA1`'s), for owners who install every build themselves. From spyro-vita (its D77), which kh1-decomp had a cut-down copy of. |
| `hw_check.sh` | read-only: can `hw_run.sh` reach the Vita, is the game installed, leftovers, crash dumps, plus the game's own checks. While another run holds the lock it says `in use by PID N (GAME): not probing` and exits 0 without touching the network (`HW_CHECK_FORCE=1` probes anyway). |
| `hw_ftp.py` | vitacompanion's FTP and command port; refuses to change anything outside the game's data and app folders. While `hw_run.sh`'s lock holds a live PID it refuses every operation, reads and commands too, with exit 3 (`the Vita is in use by PID N (GAME)`), except for the lock's holder (`HW_LOCK_HELD`, exported by `hw_run.sh`) and the processes it started; `HW_IGNORE_LOCK=1` overrides, and a lock left by a dead PID doesn't count. `hw_ftp.py lock` asks without touching the network. |
| `hw_mock.py` | a pretend Vita on localhost (FTP + command port + a fake game) for testing the above; like ftpvita it won't rename onto a name that is there. |
| `vpk_pack.py` | a linked ELF to a VPK: refuses an ELF that lost `sceUserMainThreadStackSize` / `_newlib_heap_size_user`, then vita-elf-create, make-fself (a safe app; `--unsafe` for an unsafe one), mksfoex (`ATTRIBUTE2=12`), pack-vpk, optionally with plain placeholder LiveArea art. `show` prints a VPK's `param.sfo` values, whether its `eboot.bin` is safe or unsafe, and the file list (diff two builds). Warns when the text segment leaves under 16 KiB before the data segment, and names the fix (`__sce_headroom`, gotchas 17) when vita-elf-create fails on it. The CMake twin is `../cmake/VpkApp.cmake` (`SCE_HEADROOM`, `UNSAFE`). From kh1-decomp `tools/vita/build.py`. |
| `psp2dmp.py` | a crash dump (`ux0:data/psp2core-*.psp2dmp`) read: modules and segments, threads with state, stop reason (`0x30004` data abort ..., with the faulting address) and registers; with `--elf` the PC and LR named from the unstripped ELF (load base subtracted; the ELF picked by size from a directory), `--stack THREAD` the return addresses on a stack, `--title` the title ids in it. Python 3, stdlib; addr2line if on `PATH`. Written for the kit from the format (facts from spyro-vita sessions' readers, vita-parse-core, psp2_core_parse; no code taken). Test: `test/psp2dmp_test.sh` (python3 only; a synthetic dump and ELF). |
| `livearea_png.py` | the Vita's rules for sce_sys art: `check` PNGs (8-bit indexed at 128×128 / 840×500 / 280×158), `from` converts a player's own RGB / RGBA art to fit, `vpk` checks a finished VPK's required files and art. Plain Python. From spyro-vita `tools/vita/make_livearea.py` (its PNG half) and `tools/ci/build_vpk.sh`. |
| `fnprof.py` | ranks a thread's functions from the log's `vpk-fnprof:` lines (`../runtime/fnprof`) against the unstripped ELF: the load slide from the init line's address, the callers each function was entered from, `--units` by source file (addr2line), `--alias PREFIX` to prefer a function's own name over an override table's alias. From kh1-vita `tools/vita/fnprof.py`. |
| `present_trace.py` | checks a present trace's CSV (`../runtime/presenttrace`) against its launch's log (footer, export line with the same pid / nonce / capture, timestamps that add up) and reports nearest-rank p50 / p95 / p99 / max of the picture interval and pictures per second over a game-frame range, as JSON. From kh1-vita `scripts/analyze_present_trace.py`. |
| `launch_token.py` | writes a launch-token request file (`../runtime/launchgate`) from the log's last `armed ... token PID NONCE` line, for the launch that log is from. From kh1-vita `scripts/present_trace_request.py`. |
| `embed_pad.py` | text files (pad scripts) as C strings in one generated header (`OUT.h NAME=SCRIPT.txt ...`; blank and `#` lines left out); `--check` exits 1 when the header is stale, for the game's tests. From kh1-vita `tools/vita/embed_pad.py`. |
| `v3k_run.sh` | runs a game's VPK in Vita3K with a config.txt from its arguments until the stop line, an exit or a timeout, under a per-game lock, and collects the log, shots, save folder and the game's `HW_COLLECT` files; exit 0 stop, 2 crash, 3 hang, 4 hang line, 1 setup error or a lock wait that timed out (`V3K_LOCK_WAIT`). The lock takes over a dead PID's or a PID-less lock over a minute old. Settings from the game's `hw.game`. From spyro-vita `tools/vita/vita3k_run.sh`. |
| `shots_cmp.py` | compares two runs' PNGs by name: identical bytes, or the fraction of pixels off by more than `--tol` inside `--edge` at most `--max-diff`. Exit 1 for a picture over the limit, a size change, a missing or undecodable picture, an edge that leaves no pixels, or nothing to compare. The autopilot's picture A/B. |
| `perf_report.py` | per-frame means of a log's `vpk-perfcount:` lines (`../runtime/perfcount`), or two runs compared (with both frame counts): `worse` past `--noise` (default 2%) exits 1; exit 2 when a log has no lines in range or cannot be read. |
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

Tests: `test/livearea_png_test.sh` (python3 only). `test/profiling_tools_test.py` (python3; a C compiler for one case) runs `fnprof.py` against a fake nm and a slid log, `present_trace.py` and `launch_token.py` on synthetic CSVs and logs (a truncated CSV, another launch's, timestamps that do not add up), and `embed_pad.py` (write, `--check`, a stale header, the header compiled and printed). `test/vpk_pack_test.sh` (needs vitasdk, cmake, ninja; see `../cmake/README.md`). `test/hw_run_test.sh` runs `hw_run.sh` and `hw_check.sh` against `hw_mock.py` as vitacompanion 1.08 and 1.05: a
run to `frames=` with a pad script, the owner's files back untouched, the build's SHA-1s in `hw-run.txt` and
`hw-run.env`, a hang with a crash dump (`HW_ELF` copied next to it, `HW_KEEP_APP=1` putting the eboot.bin back), `HW_NO_INSTALL=1` (a wrong build refused, the right one run, nothing installed),
`--restore` after a SIGKILL and after an eboot.bin upload cut off before its rename, `--put-eboot` with a VPK, a bare
eboot.bin and a VPK without one, `hw_ftp.py` refusing writes outside the game's folders; then the lock, held by a
`sleep` the test starts and kills by PID: `hw_ftp.py` exits 3 (reads and commands too) but lets `HW_IGNORE_LOCK=1`,
`HW_LOCK_HELD` and a script holding the lock itself through, `hw_check.sh` doesn't probe (`HW_CHECK_FORCE=1` does), a
run whose lock wait times out writes `NOT RUN`, a stale lock blocks nothing, `--put-eboot` waits for the lock; and a
game that is a git checkout (its `tools/vita/hw.env` read, `HW_VPK` found from its root, the commit named in the run's
account: fixed 2026-11-10, the game root came out as two lines in any git checkout before).
`test/v3k_run_test.sh` runs `v3k_run.sh` against a fake Vita3K (stop, crash, hang, hang line, the lock, the
collected files). `test/autopilot_tools_test.py` runs `perf_report.py` and `shots_cmp.py` on synthetic logs and PNGs.

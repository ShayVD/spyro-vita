# targets/vita/tools

Host-side scripts. Each says where it came from in its header.

| Tool | What it does |
|---|---|
| `get_vitasdk.sh` | vitasdk into `$VITASDK` without docker or GitHub releases: the vitasdk/vitasdk image's toolchain layers from mirror.gcr.io, pinned by digest and sha256-checked (from kh1-decomp). On a desktop the usual vdpm install is simpler. |
| `hw_run.sh` | a run on a real Vita over vitacompanion: install or update the app, set the owner's files aside, write the run's `config.txt` (+ pad script, save folder), launch, wait (optionally quiet), stop at `frames=` / hang / crash dump / timeout, fetch the log and files, put the owner's files back however it ends. One run at a time on the machine, across games (a lock in `~/.cache/vita-port-kit`; a wait that times out writes `hw_run: NOT RUN: ...` to `hw-run.txt`). The eboot.bin goes up as `eboot.bin.hw-new`, size-checked, then renamed into place. `hw-run.txt` and `hw-run.env` name the eboot's and the VPK's SHA-1s and the VPK's path. `--put-eboot FILE` (a VPK or an eboot.bin) only installs it, under the lock, and reads it back (both SHA-1s, `READBACK IDENTICAL`); `HW_KEEP_APP=1` puts the Vita's eboot.bin from before the run back afterwards; `HW_ELF=` is copied next to each crash dump fetched; `HW_NO_INSTALL=1` installs nothing and runs only when the installed eboot.bin is the VPK's (or `HW_EBOOT_SHA1`'s), for owners who install every build themselves. From spyro-vita (its D77), which kh1-decomp had a cut-down copy of. |
| `hw_check.sh` | read-only: can `hw_run.sh` reach the Vita, is the game installed, leftovers, crash dumps, plus the game's own checks. While another run holds the lock it says `in use by PID N (GAME): not probing` and exits 0 without touching the network (`HW_CHECK_FORCE=1` probes anyway). |
| `hw_ftp.py` | vitacompanion's FTP and command port; refuses to change anything outside the game's data and app folders. While `hw_run.sh`'s lock holds a live PID it refuses every operation, reads and commands too, with exit 3 (`the Vita is in use by PID N (GAME)`), except for the lock's holder (`HW_LOCK_HELD`, exported by `hw_run.sh`) and the processes it started; `HW_IGNORE_LOCK=1` overrides, and a lock left by a dead PID doesn't count. `hw_ftp.py lock` asks without touching the network. |
| `hw_mock.py` | a pretend Vita on localhost (FTP + command port + a fake game) for testing the above; like ftpvita it won't rename onto a name that is there. |
| `vpk_pack.py` | a linked ELF to a VPK: refuses an ELF that lost `sceUserMainThreadStackSize` / `_newlib_heap_size_user`, then vita-elf-create, make-fself (a safe app; `--unsafe` for an unsafe one), mksfoex (`ATTRIBUTE2=12`), pack-vpk, optionally with plain placeholder LiveArea art. `show` prints a VPK's `param.sfo` values, whether its `eboot.bin` is safe or unsafe, and the file list (diff two builds). `require` fails a VPK that lacks the files the build makes from the disc (gotchas 28). Warns when the text segment leaves under 16 KiB before the data segment, and names the fix (`__sce_headroom`, gotchas 17) when vita-elf-create fails on it. The CMake twin is `../cmake/VpkApp.cmake` (`SCE_HEADROOM`, `UNSAFE`). From kh1-decomp `tools/vita/build.py`. |
| `psp2dmp.py` | a crash dump (`ux0:data/psp2core-*.psp2dmp`) read: modules and segments, threads with state, stop reason (`0x30004` data abort ..., with the faulting address) and registers; with `--elf` the PC and LR named from the unstripped ELF (load base subtracted; the ELF picked by size from a directory), `--stack THREAD` the return addresses on a stack, `--title` the title ids in it. Python 3, stdlib; addr2line if on `PATH`. Written for the kit from the format (facts from spyro-vita sessions' readers, vita-parse-core, psp2_core_parse; no code taken). Test: `test/psp2dmp_test.sh` (python3 only; a synthetic dump and ELF). |
| `livearea_png.py` | the Vita's rules for sce_sys art: `check` PNGs (8-bit indexed at 128×128 / 840×500 / 280×158), `from` converts a player's own RGB / RGBA art to fit, `vpk` checks a finished VPK's required files and art. Plain Python. From spyro-vita `tools/vita/make_livearea.py` (its PNG half) and `tools/ci/build_vpk.sh`. |
| `fnprof.py` | ranks a thread's functions from the log's `vpk-fnprof:` lines (`../runtime/fnprof`) against the unstripped ELF: the load slide from the init line's address, the callers each function was entered from, `--units` by source file (addr2line), `--alias PREFIX` to prefer a function's own name over an override table's alias. From kh1-vita `tools/vita/fnprof.py`. |
| `present_trace.py` | checks a present trace's CSV (`../runtime/presenttrace`) against its launch's log (footer, export line with the same pid / nonce / capture, timestamps that add up) and reports nearest-rank p50 / p95 / p99 / max of the picture interval and pictures per second over a game-frame range, as JSON. From kh1-vita `scripts/analyze_present_trace.py`. |
| `launch_token.py` | writes a launch-token request file (`../runtime/launchgate`) from the log's last `armed ... token PID NONCE` line, for the launch that log is from. From kh1-vita `scripts/present_trace_request.py`. |
| `embed_pad.py` | text files (pad scripts) as C strings in one generated header (`OUT.h NAME=SCRIPT.txt ...`; blank and `#` lines left out); `--check` exits 1 when the header is stale, for the game's tests. From kh1-vita `tools/vita/embed_pad.py`. |
| `v3k_run.sh` | runs a game's VPK in Vita3K with a config.txt from its arguments until the stop line, an exit or a timeout, under a per-game lock, and collects the log, shots, save folder and the game's `HW_COLLECT` files; exit 0 stop, 2 crash, 3 hang, 4 hang line, 7 the fail line (the game file's optional `HW_FAIL_RE`: the route's goal was missed; the run stops there), 1 setup error or a lock wait that timed out (`V3K_LOCK_WAIT`). The lock takes over a dead PID's or a PID-less lock over a minute old. Settings from the game's `hw.game`. `PAD=` plays a pad script; `KEYS=` (macOS) plays a key script through the controller (below). `CAPTURE=1` (macOS) grabs the window at each of the game's holds (`win-V.png`); `capture.log` names the grabs that failed (`no capture (R)`), and calls a one-colour grab whose colour is the game's own VRAM shot of that vblank a `single-colour capture` (a real black fade), not a failure. From spyro-vita `tools/vita/vita3k_run.sh`. |
| `v3k_keys.swift` | `v3k_run.sh KEYS=`'s injector: a key script's steps (`wait`, `log RE`, `press cross`, `down`/`up`) posted as macOS keyboard events (`CGEvent.postToPid`) to one Vita3K PID, Vita inputs mapped to keys by the run's Vita3K `config.yml`. `--check` parses a script and checks that events may be posted. Built to `$TMPDIR/v3k_keys` on first use. From crash-recomp's issue #62 injector (its `docs/host-runs.md`). |
| `keys.example` | an example key script. |
| `ap_gate` | the port autopilot's gate runner for the Vita (`playbooks/port-autopilot/targets.md`): `v3k_run.sh` with the game's `hw.game`, `ARTIFACT=` for another VPK, `run.log` and `run.txt` in OUT. It counts Vita3K's invalid memory accesses (`Invalid read/write of uintN_t at addr...`: unmapped memory a Vita would fault on, which Vita3K reads as zeros) in `vita3k.txt`, prints the count and the most frequent addresses, writes `invalid_accesses=N` to `run.txt`, names where each happened (`v3k_where.py`, with the game file's `HW_ELF`; else the raw `PC:` dumps), and exits 6 when a run reached its stop line with any. `AP_GATE_INVALID_FAIL=0` only reports them; `AP_GATE_INVALID_RE` changes the pattern. `HW_FAIL_RE` in the game file stops a run at the game's miss line with exit 7, so a missed goal is not read as a crash. Exit 0 stop, 1 setup, 2 crash, 3 hang, 4 hang line, 5 lock busy, 6 invalid accesses, 7 fail line. |
| `v3k_where.py` | where a Vita3K run went wrong: each invalid memory access or guest exception in Vita3K's output (`vita3k.txt`), with the PC and LR of the CPU dump Vita3K logs after it (at its default ERROR level) named by the nearest function in the build's unstripped ELF (`nm`, as `fnprof.py` reads it), and the instruction Vita3K disassembled; one line per site, most frequent first. The load slide comes from Vita3K's `Loaded module segment 0 @ [A - B / L] ... app0:eboot.bin` line (INFO level, `VITA3K_LOG=2`), else 0: Vita3K loaded a test eboot at its link address. A PC may be the start of Vita3K's translated block, so trust the function more than the offset. |
| `shots_cmp.py` | compares two runs' PNGs by name: identical bytes, or the fraction of pixels off by more than `--tol` inside `--edge` at most `--max-diff`. Exit 1 for a picture over the limit, a size change, a missing or undecodable picture, an edge that leaves no pixels, or nothing to compare. The autopilot's picture A/B. `--one-colour R,G,B PNG [--tol N]`: exit 0 when every pixel is that colour (`v3k_run.sh CAPTURE=1` uses it). |
| `perf_report.py` | per-frame means of a log's `vpk-perfcount:` lines (`../runtime/perfcount`), or two runs compared (with both frame counts): `worse` past `--noise` (default 2%) exits 1; `--counters a,b` judges only those (`--ignore` the inverse; both take globs such as `vc.rec.*`), the rest show as `info`; `--rerun BASE2.log` adds runs of the base VPK, and a counter is then `worse` only when worse than every one (same-VPK noise); `--spread RUN1 RUN2 [...]` takes two or more run folders (their `run.log`) or logs of one build and prints each counter's run-to-run spread over the range and per window, exit 1 when one moves past `--noise` (`--ignore-list`: those counters as an `"ignore": [...]` line for `config.perf_gate` or a benchmark scene); exit 2 when a log has no lines in range or cannot be read. |
| `hw_log.py` | tables from a hardware log's per-window lines (watchdog threads lines, bound lines, a pictures line): `summary --frames A-B` gives one console A/B row (pictures/s, worst window, each thread's mean ms a window), `pics` pictures/s per room or scene, `windows` each thread's and core's load per window (`--frames`, or `--start-re`/`--end-re` for a fight window), and `sym` a logged address to function and line (address - image base + the ELF's base, through `arm-vita-eabi-addr2line`). Reads a running `K shown` watchdog header (kh1-vita's) by default; a game's own line shapes go in a `--format` JSON file (`tools/vita/hw_log.json`): `window_re` for a line that closes each window with its frame, clock and pictures (spyro-vita's `vita vblank N:` line), `metrics` for per-window numbers keyed by the window's frame (late vblanks, busy %, a walk's ms), and a `scene_re` with an `at` group that places each scene change at its frame. `summary` takes several logs (`--vs` for the B side), prints each run's timing (the frame of each scene change), groups the runs by it and warns when they or an A/B span timings (kb `realtime-pad-route-splits-into-timings`); `--drop-loads` leaves out windows with a scene line inside. Counters stay `perf_report.py`'s. |
| `relink.py` | a worker's change on the Vita without a full build: `run --from BATCH_BUILD` clones the last finished build (copy-on-write) into the checkout, compiles only what the branch touched (a changed header: every source that includes it), reruns the build's map, link and package steps through the game's hook (`tools/vita/relink.game`), and says which symbols and VPK entries differ from the batch's. Refuses an unfinished build, and with exit 3 a change only the full build covers (build scripts, tables, forced headers). `diff OLD.elf NEW.elf --expect NAME` and `vpk OLD NEW` on their own. `../docs/relink.md`. From kh1-vita worker #342's relink against batch 73. |
| `relink.game.example` | the settings and hooks a game keeps in its own `tools/vita/relink.game` for `relink.py`. |
| `hw.game.example` | the settings and hooks a game keeps in its own `tools/vita/hw.game`. |

**Key scripts (`KEYS=`).** A pad script (`PAD=`, a `pad=` line in `config.txt`) feeds the game's pad reads
directly and bypasses the controller. To test the real input path (`sceCtrl` through the port's controller code) in
Vita3K, a run takes `KEYS=FILE` instead: `v3k_keys.swift` posts macOS keyboard events to the run's own Vita3K (the
PID `v3k_run.sh` started, once its arguments show `-w <VPK>`, so another session's Vita3K is never touched), and
Vita3K's keyboard map (its `config.yml`, `keyboard-button-*`, `keyboard-leftstick-*`) turns them into buttons and the
left stick. The script is checked before the lock; the injector starts once Vita3K has, so a run that waits up to
`V3K_LOCK_WAIT` for the lock sends nothing until it holds it. Steps (`keys.example`):

```
log crt0 120         # wait (up to 120 s) for the game's log to match a regex
wait 2               # seconds
press start 0.8      # down, held 0.8 s (default 0.2), up
press select+l1      # names joined with +: down in order, up in reverse
down lstick-up       # down / up on their own
up lstick-up
press key:KeyX       # a key by Vita3K's name
```

Names: `cross circle square triangle start select up down left right l1 r1 l2 r2 l3 r3 psbutton`, `lstick-*`,
`rstick-*` (`up down left right`). The steps go to `OUT/keys.log` with timestamps and the outcome to `v3k-run.txt` as
`keys=` (0 played, 1 a bad script or no permission, 2 Vita3K ended first, 3 a `log` step timed out, `stopped` the run
ended first: the run's own result stands either way). macOS only (elsewhere `KEYS=` exits 1); the app the run is
started from needs Accessibility permission (System Settings, Privacy & Security) to post events, and Vita3K's window
comes to the front once, before the first key. Vita3K's keyboard stick jumps between 0 and 255, never a value
between (a dead zone is not tested this way); Vita3K's L2/R2 keys reach no `sceCtrlPeekBufferPositive` reader; a
real Vita's `SceCtrl` still needs a hardware run. `ap_gate` passes `KEYS` through, as it does `PAD`.

**Exact gates.** Once two Vita3K runs of one VPK repeat byte for byte (`../../packs/ps2/docs/ps2-ports.md` §1),
compare with no tolerance: `shots_cmp.py A B --max-diff 0 --tol 0 --edge 0` and `perf_report.py A.log B.log
--noise 0 --counters ...`. Run a change meant to move the baseline twice and require the pair identical before it
becomes the new baseline (`../../playbooks/port-autopilot/SKILL.md`, Batched builds). Host-timed counters
(presents, wait times) never repeat under Vita3K: keep them out of `--counters`. For `v3k_run.sh`, a Metal
`DeviceLostError` or an `EXC_BAD_ACCESS` in Vita3K is Vita3K's: rerun on the change's VPK and on the base VPK
before blaming either (`../docs/gotchas.md` 24).

A game wires them up with two small wrappers that set `HW_GAME` (kh1-decomp and spyro-vita do this):

```bash
#!/usr/bin/env bash
# tools/vita/hw_run.sh: Encore's runner with this game's settings (tools/vita/hw.game)
root=$(cd "$(dirname "$0")/../.." && pwd)
HW_GAME=$root/tools/vita/hw.game exec "$root/third_party/encore/targets/vita/tools/hw_run.sh" "$@"
```

`hw_ftp.py` and `hw_mock.py` are Python; spyro-vita wraps them the same way (`tools/vita/hw_ftp.py`):

```python
#!/usr/bin/env python3
import os, runpy, sys
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("HW_GAME", os.path.join(root, "tools/vita/hw.game"))
kit = os.path.join(root, "third_party/encore/targets/vita/tools/hw_ftp.py")
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
`test/relink_test.py` (python3, git, bash) runs `relink.py` on a fake game (a hashing "compiler", a synthetic ARM ELF and VPK), with and without a compile_commands.json: no change relinks byte for byte, one file changes one symbol, a header recompiles its includers through another header, a resized function, a submodule's file, uncommitted and untracked files, the refusals (a build script, an unknown file, a build newer than its VPK, an existing output), clone, copy and hard-link modes, and the batch's files untouched after each.
`test/v3k_keys_test.sh` (macOS, swiftc; skipped elsewhere) runs `v3k_run.sh KEYS=` against a fake Vita3K window (`test/v3k_keys_fake.swift`) that logs the key events it receives: presses arrive in order at the run's own process, mapped by the run's `config.yml`; a bad script is refused before the lock; a `log` step that times out is `keys=3`. Its window comes to the front for a moment. `test/v3k_run_test.sh` runs `v3k_run.sh` against a fake Vita3K (stop, crash, hang, hang line, the fail line with and without `HW_FAIL_RE`, `CAPTURE=1`'s single-colour captures against a fake grabber, the lock, the
collected files), and `ap_gate` over it: the invalid-access count and exit 6, and `v3k_where.py`'s names through a
fake `nm` (the slide assumed 0, the slide from a segment line, no `HW_ELF`, `ARTIFACT=`, a missing ELF). `test/autopilot_tools_test.py` runs `perf_report.py` (with `--rerun` and `--spread`), `hw_log.py` (a fake addr2line for `sym`; kh1-vita's shown-count watchdog lines, `summary`, `--frames`, `--format`), `vpk_pack.py require` and `shots_cmp.py` on synthetic logs and PNGs.

# vita/tools

Host-side scripts. Each says where it came from in its header.

| Tool | What it does |
|---|---|
| `get_vitasdk.sh` | vitasdk into `$VITASDK` without docker or GitHub releases: the vitasdk/vitasdk image's toolchain layers from mirror.gcr.io, pinned by digest and sha256-checked (from kh1-decomp). On a desktop the usual vdpm install is simpler. |
| `hw_run.sh` | a run on a real Vita over vitacompanion: install or update the app, set the owner's files aside, write the run's `config.txt` (+ pad script, save folder), launch, wait (optionally quiet), stop at `frames=` / hang / crash dump / timeout, fetch the log and files, put the owner's files back however it ends. One run at a time on the machine, across games (a lock in `~/.cache/vita-port-kit`). From spyro-vita (its D77), which kh1-decomp had a cut-down copy of. |
| `hw_check.sh` | read-only: can `hw_run.sh` reach the Vita, is the game installed, leftovers, crash dumps, plus the game's own checks. |
| `hw_ftp.py` | vitacompanion's FTP and command port; refuses to change anything outside the game's data and app folders. |
| `hw_mock.py` | a pretend Vita on localhost (FTP + command port + a fake game) for testing the above. |
| `vpk_pack.py` | a linked ELF to a VPK: refuses an ELF that lost `sceUserMainThreadStackSize` / `_newlib_heap_size_user`, then vita-elf-create, make-fself, mksfoex (`ATTRIBUTE2=12`), pack-vpk, optionally with plain placeholder LiveArea art. `show` prints a VPK's `param.sfo` values and file list (diff two builds). The CMake twin is `../cmake/VpkApp.cmake`. From kh1-decomp `tools/vita/build.py`. |
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
run to `frames=` with a pad script, the owner's files back untouched, a hang with a crash dump, `--restore` after a
SIGKILL, `hw_ftp.py` refusing writes outside the game's folders, and a game that is a git checkout (its
`tools/vita/hw.env` read, `HW_VPK` found from its root, the commit named in the run's account: fixed 2026-11-10, the
game root came out as two lines in any git checkout before).

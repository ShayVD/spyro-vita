# Decisions, part 3

Decisions from D162 on; D1–D102 are in [decisions.md](decisions.md) and D103–D161 in [decisions-2.md](decisions-2.md). Record each decision with its date, what was chosen, and why.

### D162. The shader compiler is found by loading it, from the data folder too, and a screen says when there is none (2026-11-16)

- **Found** (issue #152, mistial-dev, on a PS TV: VTE-1001, 3.65 Ensō, YAMT's mounts, ioPlus): v1.2.1's `spyro-vita.vpk`, the safe app, ran on the software rasterizer with `ur0:data/libshacccg.suprx` installed: 13 ms of raster a vblank, 99% busy, 196 of 300 vblanks late in Beast Makers. The port decided by a stat of the two `ur0:data` places (D35), and on that set-up a safe app gets `0x80010002`, "no such file", for `ur0:data`, from `sceIoGetstat` and from a module load alike (their closed PR #153, whose diagnostic build logged both). Why that device hides `ur0:data` from a safe app isn't known. It isn't every safe app's lot: the owner's Vita has run the safe VPK with `gpu=1` since D35 (`logs/hw-60hz-analog-20261111/spyro.log` in the main checkout, among many).
- **Decision:** `shader_compiler()` in `src/platform/vita/main.c`:
  - **The stat first, as before.** Where it succeeds nothing changes: vitaGL loads the module at its own start.
  - **Else the module is loaded here**, with the libshark vitaGL links (`shark_init(path)`), from the two `ur0:data` places and then from `ux0:data/spyro/libshacccg.suprx`, a copy the player makes where a safe app may read. vitaShaRK's `shark_init` returns at once when the module is loaded, so vitaGL's `start_shader_compiler` (`shark_init(NULL)`, then the `external` path; read in the installed `libvitaGL.a` and `libvitashark.a`) finds the compiler there and loads nothing again.
  - **The allocators are vitaGL's.** vitaGL gives the compiler `vglMalloc` and `vglFree` (`shark_set_allocators`) before its own load, and libshark hands them to the module at the load. A load here without them would leave the compiler on newlib's heap, as PR #153's did. So they are set here first, through two wrappers; the compiler calls them from the first compile on, after `vglInit`.
  - **Not `app0:`**, which PR #153 also tried: only a VPK repacked with Sony's module inside would have it there, and we never ship that.
  - **A screen instead of a silent crawl** (the report's other ask): when `gpu=1` was wanted and nothing loaded, **NO SHADER COMPILER** (`vita_boot_panel`, the disc screens' panel, D60) gives the first place's code, says to install the module and, if it is installed, to copy it to the data folder; X goes on with `gpu=0`, as the README promises a game without the module. `gpu=0` in `config.txt` never shows it. With `frames=` set it leaves by itself after 300 vblanks.
  - **The log** says each place's stat and load code, then `gpu: no shader compiler (libshacccg.suprx) could be loaded: using the software renderer (gpu=0)`, in place of `gpu: no ur0:data/libshacccg.suprx ...`, which was wrong on the PS TV. A load from here logs `gpu: shader compiler loaded from PATH (D162)`.
  - **`shacc_ur0=0`** (`config.txt`, tests): `ur0:data` counts as unreadable, so the other place and the screen can be run on a device that reads `ur0:data`.
  - `tools/vita/hw.game`'s check accepts the data folder's copy too.
- **This file:** `decisions-2.md` was 1.5 KB under the 512 KiB limit of `tools/check_no_assets.sh`, so D162 starts part 3.
- **Checked** (2026-11-16, Vita3K, the safe VPK of this branch built with the local VitaSDK, `realtime=0`; logs in the worktree's `logs/d162/`, gitignored; Vita3K is not hardware and has no safe app's sandbox):
  - **A, nothing forced** (`a-ur0`, 1,800 vblanks): `renderer: GPU (vitaGL, gpu=1)`, shaders compiled, no new log line.
  - **B, `shacc_ur0=0` with a copy in the data folder** (`b-data`): `gpu: shader compiler loaded from ux0:data/spyro/libshacccg.suprx (D162)`, `renderer: GPU`, `shaders compiled in 56106 ms`, `GPU renderer ready`; its 6 shots are A's byte for byte (the readback replay).
  - **C, `shacc_ur0=0` with no copy:** the screen is up and stays without a press (`c3-press/win-grab.png`, 12 of the panel's 13 lines; `c1-panel`, 30 s); X (Vita3K's key, sent to its window) goes on into the logos on the software renderer (`c3-press/c3-after.png`); with `frames=1800` it leaves after 300 vblanks and the run is `gpu=0`, its 6 shots A's byte for byte (`c2-soft`). The load of a file that isn't there is `80010002` in Vita3K too.
  - **The unsafe VPK:** built by CI only.
- **Not checked:**
  - **Hardware.** Not the owner's Vita (A, and B and C by `shacc_ur0=0`), and not the PS TV of the report, the only device known to show the fault: its owner is asked to run the release that carries this.
  - **A device with no module at all** (the `ur0:data` lines of the log and the screen's code from a stat): Vita3K's `ur0:data` is shared with other runs, so the module wasn't moved away; C runs the same code from the third place on.

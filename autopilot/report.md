# Autopilot report: spyro-vita, 2026-11-19 to 2026-11-20

Written by the port-autopilot coordinator (vita-port-kit `playbooks/port-autopilot`) when `autopilot.py stop-check`
printed `stop`: phase 9, no workers, three rounds in a row without a counter gain, and the only open issue waits for
the owner. Everything below is on the local `autopilot` branch. Nothing is pushed and `main` is untouched: the owner
merges `autopilot` into `main`.

## The brief

"This port is basically done, so we're looking for optimisations mostly. It was mentioned that there may be fixes
out there to remove the PS1 wobble, so maybe we could do that and have it as an option in the mod menu."

The wobble fix was already there: the options menu's PICTURE section has PERSPECTIVE TEXTURES (`persp=1`, affine
texture warping) and SUB-PIXEL VERTICES (`subpixel=1`, whole-pixel vertex snapping), from D39, both off by default.
At 60 fps it was mostly broken, and it was too costly for the heavy levels (D89). This session fixed the first and
cut the second; its real cost on a Vita is not measured yet.

## Phases

- **Phase 8 (performance)**: started on the owner's word, 2026-11-19 11:4x UTC. Rounds 1 and 2 each ended with a
  counter gain; rounds 3, 4 and 5 found nothing safe left to cut (vertex uploads, program switches and OT packets
  are the cost of the picture itself or would need draws reordered), which is phase 8's exit.
- **Phase 9 (polish)**: from 2026-11-20 ~02:20 UTC. Its backlog (#170-#174) merged; three rounds without a gain
  followed, which is the stop.

## What merged (16)

| Phase | Issue | Merge | What |
|---|---|---|---|
| 8 | #158 | `0525e1f` | Wobble fix on every frame: the presenter's in-between frames record and look up their own vertex-cache entries; on the Vita at 60 fps both kinds of frame had lost every answer since D125 (D130) |
| 8 | #160 | `cfe0d45` | Work counters: `vpk-perfcount` lines (22 counters) and benchmark scenes; vendored kit 4c46886b -> 31295be7 (D131) |
| 8 | #163 | `ad697c3` | Host ctest: `cmake/link_exec.sh` keeps executables' execute bit on colima's sshfs mount; mutant tests must show their own error (D136) |
| 8 | #161 | `5eb8423` | Vita3K's crash found: the game wraps vitaGL's 128 KB VDM ring over commands Vita3K's render thread hasn't run; `vdm_ring=KB`, off by default; every route passes `vdm_ring=1024` (D132) |
| 8 | #166 | `f0cf4e6` | `main.c` accepts the `vdm_ring` key |
| 8 | #162 | `a86cab5` | With the cache on, the presenter runs on core 1 again: game thread's cache work at 60 fps halved (D133) |
| 8 | #167 | `6a92241` | Host pad scripts tracked under `tools/oracle/pads/` (with #164's work) |
| 8 | #159 | `bbc36b1` | Vertex cache: identical answers with ~60% fewer divisions per vertex and ~15% fewer table lines (D134) |
| 8 | #168 | `bec34cc` | `fast_render_ab.sh` gives every run its own memory card; gems runs must reach the gems (D135) |
| 8 | #165 | `c713ab2` | Docs for #163 (D136) |
| 8 | #169 | `9d42dac` | `gpu_gl.c` skips GL calls that set what is already set (D137) |
| 9 | #170 | `c69544b` | Hardware session prep: run configs AP1-AP6b in `tools/vita/fps-runs/` |
| 9 | #171 | `6939e26` | Two stale comments (objects byte-identical) |
| 9 | #173 | `7605bb1` | Host warns when a run without `--save` finds a card in `./save` |
| 9 | #172 | `ddbb408` | The vertex cache's cost counts in the `vpk-perfcount` lines |
| 9 | #174 | `b478a96` | `host_checks.sh`'s demo run gets its own `--save` |

Closed without a merge: #164 (scope too narrow; its branch continued as #167).

Every merge was gated: the branch merged with `autopilot`, built with the native VitaSDK, and every Vita3K route
(`title`, `pause-ws`, `pause-ws-wobble`, `wobble60`) compared picture for picture with the baseline at zero tolerance,
or, for changes outside the Vita build (host tools, docs, comments), scope, merge and `noassets`.

## Counters now against the first baselines

`perf_report.py`, vblanks 6,600-9,000, per frame. The counters arrived with #160, so the first baselines are
`base-cfe0d45` (`pause-ws`) and `base-5eb8423` (`wobble60`, the first with counters), against `base-ddbb408`.

| Counter | pause-ws (30 fps, wobble fix off) | wobble60 (60 fps, wobble fix on) |
|---|---|---|
| `gl.attribs` | 226.0 -> 12.0 (-94.7%) | 421.3 -> 14.5 (-96.6%) |
| `gl.uniforms` | 93.4 -> 10.2 (-89.1%) | 186.7 -> 25.9 (-86.1%) |
| `gl.tex_binds` | 21.8 -> 1.5 (-93.1%) | 43.6 -> 2.0 (-95.4%) |
| `gl.programs` | 52.6 -> 47.5 (-9.6%) | 105.0 -> 95.0 (-9.6%) |
| `gl.state` | 158.5 -> 122.3 (-22.8%) | 316.3 -> 242.8 (-23.2%) |
| `vc.proj`, `vc.polys`, `vc.verts`, `vc.misses` (game thread) | 0 | -49.9% to -50.1% |

Draws, triangles, uploads and every picture are unchanged. These are counts of work, not time.

## Hardware runs

None. Every hardware run needs the owner's yes, and none was asked for this session. The configs for the pending
measurements are ready (#170, `tools/vita/fps-runs/README.md`, "Runs AP1-AP6", about 50 minutes):

- AP1 against AP2: the wobble fix's cost on Toasty at 60 fps.
- AP3 against AP2: #162, the presenter on core 1.
- AP4a against AP4b: #159, the cheaper cache.
- AP5a against AP5b: #169, the GL-state cache.
- AP6a against AP6b: whether `vdm_ring=1024` is harmless enough to be the default.

## Vita3K's own failures

41 Vita3K crashes over the session (the Notes in `autopilot/state.md`), 21 of them in #161's experiment. Before
D132, routes with the wobble options crashed most runs; the cause was Vita3K running half-overwritten commands from
an overrun VDM ring (#161). Since every route passes `vdm_ring=1024`, one crash: a start-up GPU failure on a loaded
Mac (`vita/docs/gotchas.md` 24), which passed on its rerun. Another project's autopilot (atv2-vita) ran its own
Vita3K beside ours much of the time; #161 showed it was not the cause.

## Open for the owner

- **#175 (needs-owner)**: colima's sshfs mount still loses an execute bit now and then, after `link_exec.sh`'s check.
  Options: keep the launcher only; re-check in the tools that run binaries (the autopilot's pick); `build/` on a
  Docker volume; colima with `mountType: virtiofs` (rebuilds the VM other projects share). Nothing started.
- **The hardware session** above.
- **Not blocked, no issue open**: the README's known issues should state the wobble options' cost at 60 fps once
  AP1-AP3 have run; `vdm_ring`'s default after AP6.

No issue is labelled `blocked`.

## Before merging `autopilot` into `main`

- `autopilot` holds `autopilot/` (this report, `state.md`, `config.json`) and `.github/ISSUE_TEMPLATE/autopilot.md`,
  and anchors `/runtime/` in `.gitignore`. Keep or drop them as you prefer; the game doesn't use them.
- The vendored vita-port-kit moved from 4c46886b to 31295be7 (#160): besides `vita/runtime/perfcount` it brought
  newer kit tools and a `vpk_disc_open` that closes an already-open disc first. Every route passed with it.
- Decisions D130-D137 are new in `docs/decisions-2.md`. #165's worker noticed `docs/decisions-3.md` with a "D163"
  on the unmerged branches `claude/version-1.2.2` and `claude/shader-probe-152`: that numbering would collide if
  those branches merge.
- The main checkout is on `autopilot`. Switch back with `git switch main` when you're done reviewing.

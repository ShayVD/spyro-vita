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
cut the second; on a Vita it costs 2.6 ms a vblank of the game thread at 60 fps (D172, "Hardware runs" below).

## Phases

- **Phase 8 (performance)**: started on the owner's word, 2026-11-19 11:4x UTC. Rounds 1 and 2 each ended with a
  counter gain; rounds 3, 4 and 5 found nothing safe left to cut (vertex uploads, program switches and OT packets
  are the cost of the picture itself or would need draws reordered), which is phase 8's exit.
- **Phase 9 (polish)**: from 2026-11-20 ~02:20 UTC. Its backlog (#170-#174) merged; three rounds without a gain
  followed, which is the stop.

## What merged (16)

| Phase | Issue | Merge | What |
|---|---|---|---|
| 8 | #158 | `0525e1f` | Wobble fix on every frame: the presenter's in-between frames record and look up their own vertex-cache entries; on the Vita at 60 fps both kinds of frame had lost every answer since D125 (D164) |
| 8 | #160 | `cfe0d45` | Work counters: `vpk-perfcount` lines (22 counters) and benchmark scenes; vendored kit 4c46886b -> 31295be7 (D165) |
| 8 | #163 | `ad697c3` | Host ctest: `cmake/link_exec.sh` keeps executables' execute bit on colima's sshfs mount; mutant tests must show their own error (D170) |
| 8 | #161 | `5eb8423` | Vita3K's crash found: the game wraps vitaGL's 128 KB VDM ring over commands Vita3K's render thread hasn't run; `vdm_ring=KB`, off by default; every route passes `vdm_ring=1024` (D166) |
| 8 | #166 | `f0cf4e6` | `main.c` accepts the `vdm_ring` key |
| 8 | #162 | `a86cab5` | With the cache on, the presenter runs on core 1 again: game thread's cache work at 60 fps halved (D167) |
| 8 | #167 | `6a92241` | Host pad scripts tracked under `tools/oracle/pads/` (with #164's work) |
| 8 | #159 | `bbc36b1` | Vertex cache: identical answers with ~60% fewer divisions per vertex and ~15% fewer table lines (D168) |
| 8 | #168 | `bec34cc` | `fast_render_ab.sh` gives every run its own memory card; gems runs must reach the gems (D169) |
| 8 | #165 | `c713ab2` | Docs for #163 (D170) |
| 8 | #169 | `9d42dac` | `gpu_gl.c` skips GL calls that set what is already set (D171) |
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

Run on 2026-11-20 after the merge with main (D172 has every number), on the owner's yes, AP1-AP7 of
`tools/vita/fps-runs/README.md`, every run to its end with no crash:

- **PAL (AP7):** the owner's Europe disc and settings on the merged build: verified, its pictures identical to
  Vita3K's, the GPU's in-between frame clean (no seams), the presenter on core 1, 98% of vertices found.
- **The wobble fix at 60 fps (AP1 -> AP2):** the game thread 3.4 -> 6.0 ms a vblank; quiet levels hold 60 fps,
  busy ones don't (Stone Hill ~50 late vblanks in 300). Now in the README's known issues.
- **The presenter on core 1 (AP3 -> AP2, #162):** the game thread 9.2 -> 6.0 ms a vblank (-35%); the title's and
  Toasty's late vblanks 12 -> 0.3 and 21 -> 1.2 per 300.
- **The cheaper cache (AP4, #159):** the cache's own time -8%; no frame-rate gain measurable above run-to-run noise.
- **The GL state shadow (AP5, #169):** the calls cut as in Vita3K (attribs 421 -> 15 a frame), the render thread's
  time the same: fewer calls, no time.
- **`vdm_ring=1024` (AP6):** harmless (same pictures, card, counters, time) but 896 KB less CDRAM; only Vita3K
  needs it, so the default stays off.

The merged build is left installed on the Vita (eboot read back identical).

## Vita3K's own failures

41 Vita3K crashes over the session (the Notes in `autopilot/state.md`), 21 of them in #161's experiment. Before
D166, routes with the wobble options crashed most runs; the cause was Vita3K running half-overwritten commands from
an overrun VDM ring (#161). Since every route passes `vdm_ring=1024`, one crash: a start-up GPU failure on a loaded
Mac (`vita/docs/gotchas.md` 24), which passed on its rerun. Another project's autopilot (atv2-vita) ran its own
Vita3K beside ours much of the time; #161 showed it was not the cause.

## Open for the owner

- **#175 (needs-owner)**: colima's sshfs mount still loses an execute bit now and then, after `link_exec.sh`'s check.
  Options: keep the launcher only; re-check in the tools that run binaries (the autopilot's pick); `build/` on a
  Docker volume; colima with `mountType: virtiofs` (rebuilds the VM other projects share). Nothing started.
- Done since the stop: the hardware session (above), the README's line on the wobble options' cost at 60 fps, and
  `vdm_ring`'s default (stays off).

No issue is labelled `blocked`.

## After the stop: main merged in (2026-11-20, D172)

The autopilot had branched from a local `main` 162 commits behind GitHub's (no PAL support, D154; no 1.2.x), so its
builds refused the owner's PAL disc on the Vita, and the hardware session stopped at its first run. On the owner's
word `main` (`a9db797`) is merged into `autopilot`:

- The autopilot's decisions are D164-D171 in `docs/decisions-3.md` (main has its own D130-D161); the merge is D172.
- The vertex cache keeps main's D152 (the walk says where each polygon's answers go) and D164's own table for the
  in-between frame; D152's `pres_draw_vcache` is gone (D172 says why). D167's presenter on core 1 works on top.
- The kit stays at 31295be7 (it holds main's 78915280), vendoring both `perfcount` and `vcount`.
- Vita3K, main against the merge, every picture byte-identical: 303 of 303 over the four routes and two new PAL routes
  (the owner's 60 HZ, 16:9, wobble-fix settings on the Europe image). On `wobble60` the presenter runs on core 1 (main
  keeps it inline), 97% of its vertices found, 0% lost, and the `gl.*`/`vc.*` counters equal the pre-merge ones.

## Phase 8 again: the owner's 2026-11-20 issues (round 9, D175-D197 (with D194, D195, D196))

The owner asked for one PSX WOBBLE row with no wobble by default, no wobble at all (textures stretching and changing at
the 16:9 edges), the wobble fix's cost at 60 fps, and 2x draw distance's frame drops. Merged into `autopilot`, each
through the gate (from #194 on with the host build and ctest):

| Issue | Decision | What |
|---|---|---|
| #177 | D194 | one PSX WOBBLE row, off (no wobble) by default, `config_version=6` |
| #178 | D195 | clamped edge vertices drawn at their exact place |
| #180 | D175 | 2x draw distance: low-detail far mobys, a 48 KB budget at 60 Hz logic |
| #183 | D176 | world grids' moved points found again from their quad |
| #186 | D177 | the sky records only its drawn triangles' vertices |
| #181, #184 | D178 | ambiguous keys from the frame's order; grid x16 depths; race-free stats (the "44% scene") |
| #187 | D196 | the OT walk overlaps the game thread again (a save-state join blocked it every tick since D71) |
| #190 | D181 | `prof_draw`'s sky counted once |
| #189 | D182 | renderers hand over their screen-space vertices (shadows, glows, sky cuts, splits) |
| #185 | D183 | `dd_budget=0` / `--lod-budget 0` off switch |
| #194 | D184 | `render_ctx_link` fixed (broken by #180); the gate now runs the host build and ctest |
| #182 | D185 | `vita-testing.md` and the bench for the default |
| #193 | D186 | the presenter's vertex cache worked out on first read |
| #195 | D187 | `parse_thread=2`: at 30 Hz the walk runs on the game thread beside a pending presenter job |
| #179 | D188 | the logic draw's projections worked out by the walk while it's idle (60 Hz) |
| #196 | D189 | `vc_lazy_logic=` key, `vpk-perfcount-vc` docs |
| #192 | D190 | ambiguous keys past 32 projections and with clamped points |
| #197 | D191 | a quad's ambiguous and no-entry vertices resolve together |

Hardware session 3 (D179, owner's yes) measured #180/#183/#186 on the Vita: sky -33%, polygons found whole 96.4 ->
98.2%, and found the 30 Hz presenter saturating in Toasty (#193, #195).

**Round end (Vita3K, `owner60` 6,600-9,000, `base-3ba4fd2` -> `base-63d80f4`):** `vc.misses` -97.8% (polygons drawn
with the PS1's wobble: from ~6% to ~0.4%, mostly 2D by design), `gl.draws` -6.0%, `vc.proj` -7.2%; `vc.lines` +13.5%
(the lookups that now find those vertices). Every replay shot of the five routes is unchanged. By the phase's rule (a
counter worse) this round counts as no gain: `rounds_without_gain` 1 of 3. FPS isn't judged in Vita3K; the
game-thread and presenter changes (D196, D186-D188) need a hardware run, which waits for the owner's yes.

**Open for the owner:** the hardware run (the new build isn't on the Vita: it has `ca4450e`); raising the vendored
kit's `VPK_PC_MAX` (a `../vita-port-kit` change); whether ambiguous portal outline keys should be cut at their pixel
centre (354 more exact sky-clip vertices, 281 affine polygons move; D191).

## Before merging `autopilot` into `main`

- `autopilot` holds `autopilot/` (this report, `state.md`, `config.json`) and `.github/ISSUE_TEMPLATE/autopilot.md`,
  and anchors `/runtime/` in `.gitignore`. Keep or drop them as you prefer; the game doesn't use them.
- The vendored vita-port-kit moved from 78915280 (main's) to 31295be7: besides `vita/runtime/perfcount` it brought
  newer kit tools and a `vpk_disc_open` that closes an already-open disc first. Every route passed with it.
- The main checkout is on `autopilot`. Switch back with `git switch main` when you're done reviewing.

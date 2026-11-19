# Autopilot state: Spyro the Dragon (spyro-vita)

Written by the port-autopilot coordinator (vita-port-kit `playbooks/port-autopilot`) at the end of every tick and
committed on the `autopilot` branch. The JSON block is what the tools read
(`autopilot.py check autopilot/state.md`); the notes under it are for people.

```json
{
  "version": 1,
  "game": "spyro",
  "phase": 8,
  "workers": [
    {
      "issue": 159,
      "branch": "auto/159-1",
      "worktree": "/Users/shayvandam/Projects/spyro-vita-auto-159",
      "pids": [],
      "started_at": "2026-11-19T21:21:51Z",
      "files": [
        "src/platform/common/vcache.c",
        "src/platform/common/psx_vcache.h",
        "src/game/gte_ops.c",
        "src/game/gte_native.h",
        "docs/decisions-2.md"
      ],
      "agent": "a8691855df65aa8f1"
    }
  ],
  "last_good": "a86cab56aaea9741264a16e1502d826ee5313bbc",
  "baselines": {
    "run_dir": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/runs/base-a86cab5",
    "commit": "a86cab56aaea9741264a16e1502d826ee5313bbc"
  },
  "hardware_runs": [],
  "perf_candidates": [
    {
      "title": "Presenter thread with the vertex cache on",
      "expected": "in-between frame's cache work off the game thread at 60 fps",
      "issue": 162
    },
    {
      "title": "Hardware cost of the wobble fix at 60 fps after #158",
      "expected": "owner-approved run: fps-runs t1-subpixel-60 style, Toasty, persp=1 subpixel=1 interp on",
      "issue": 0
    },
    {
      "title": "Hardware check of vdm_ring=1024 (D132) before making it the default",
      "expected": "no fps or picture change, +896 KB RAM; owner-approved run only",
      "issue": 0
    },
    {
      "title": "Hardware A/B for #162 (presenter on core 1 with the cache on)",
      "expected": "about half of D89's ~4.5 ms/vblank cache work leaves the game thread; s7f3-toasty60-pres + o-toasty + persp=1 subpixel=1, A this path vs B pres_thread=0",
      "issue": 162
    }
  ],
  "owner_decisions": [
    {
      "issue": 0,
      "question": "Where does the autopilot start on spyro-vita?",
      "default_taken": "phase 8 (owner, 2026-11-19: the port is basically done, look for optimisations; wobble removal as a menu option)",
      "switch": "state.phase"
    }
  ],
  "attempts": {
    "161": 1
  },
  "blocked": [],
  "issues_filed": {
    "date": "2026-11-19",
    "phase": 8,
    "count": 11
  },
  "round": {
    "n": 1,
    "base_run_dir": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/runs/base-f7aeb43",
    "base_commit": "f7aeb4343248602a2c3797f000b60a436bb62b6f",
    "started_at": "2026-11-19T13:05:58Z",
    "filed": 11,
    "investigated": false
  },
  "rounds_without_gain": 0,
  "hardware_pending": null,
  "job": null,
  "batch": null
}
```

Fields: `workers[]` = {issue, branch, worktree, pids, started_at, last_commit_at, files, agent, reported} (`agent` is
the coordinator's Agent id for the worker; `reported` is true once it has reported and waits for its gate);
`baselines` = the Vita3K run folder (an absolute path) and commit the gate compares against; `hardware_runs[]` =
{date, commit, vpk, sha1, fps: {scene: n}}; `perf_candidates[]` = {title, expected, issue}; `owner_decisions[]` =
{issue, question, default_taken, switch}; `attempts` = {"issue number": tries}; `blocked` = the issue numbers the
coordinator labelled `blocked` (a number whose label is gone was unblocked by the owner); `round` = {n, base_run_dir,
base_commit, started_at, filed, investigated}: the current round in phases 8-9, measured against `base_run_dir`;
`hardware_pending` = {since, commit, step} while a hardware checkpoint pauses dispatch, else null; `job` = {kind,
issue, step, pid, out, started_at} while a coordinator job runs in the background, else null; `batch` = {n, branch,
base, issues, merges, step} while `build_mode` is `batched` and gated branches wait for one shared build, else null. Times (`started_at`,
`last_commit_at`) are ISO 8601; with no offset they are UTC.

`issues_filed` = {date, phase, count}: how many issues were filed on that **UTC** date (YYYY-MM-DD) in that
phase. The limit (`max_issues_per_day`, 20) applies per phase per day; a stale date or another phase leaves the
full limit.

Tool exit codes (`autopilot.py`): 0 ok, 1 check errors, 2 usage, 3 unreadable input; for `stop-check`, 10 =
continue.

## Notes

(The coordinator's short log: one line per tick that changed something.)
- 2026-11-19 phase 8: autopilot started at phase 8 on the owner's word (optimisations; PS1 wobble removal as a menu option). Disc: runtime/disc links to the gitignored disc/image (CLAUDE.md keeps it there).
- 2026-11-19 phase 8: filed #158 (wobble fix missing on the presenter's frames), #159 (vertex cache cost), #160 (perfcount counters); worker on #158; baseline build and routes running. Vita3K: none yet.
- 11:53Z phase 8: baseline build failed on setup (main checkout's build/vita has a non-Docker CMake cache); rerun into build/vita-ap. Vita3K: none yet.
- 12:36Z phase 8: the vitasdk/vitasdk image is amd64 (emulated on this arm64 Mac: 76/505 in 42 min); build command switched to the native VitaSDK (docs/vita-testing.md), baseline restarted into build/ap-native. The stopped Docker build's container (elegant_shaw) is left to finish: docker stop was refused by the permission check. Vita3K: none yet.
- 13:03Z phase 8: baseline f7aeb43 (native build): title pass, pause-ws pass, pause-ws-wobble crashed in Vita3K at vblank ~6300 (host EXC_BAD_ACCESS; game log clean, vcache 99% found); flakiness rerun into pause-ws-wobble-r2. Docker container elegant_shaw stopped on the owner's yes. Vita3K: 1 crash pending check.
- 13:05Z phase 8: pause-ws-wobble crashed again on the baseline VPK (r2, ~7800): filed #161; it is autopilot's, not a branch's. Baseline base-f7aeb43 set (title, pause-ws pass); last_good stays empty until the wobble route passes. Round 1 started. Vita3K: 2 crashes (wobble route, same VPK).
- 14:12Z phase 8: #158 reported done (report runtime/autopilot/reports/158-1.md): presenter frames 0% -> logic's share found; on Vita3K at 60 fps both kinds had been 100% side_lost since D125. Gate running (runs/gate-158-1). Vita3K (worker's): pause-ws 1 crash, pause-ws-wobble 2 crashes (#161).
- 14:22Z phase 8: merged #158 (0525e1f; gate all routes pass, shots identical). Added route wobble60 (60 fps with persp+subpixel); baseline base-0525e1f = the gate's runs + wobble60 (running). Filed #162 (presenter thread with the cache on), #163 (ctest exec bits). Vita3K: 2 crashes in the gate (pause-ws, pause-ws-wobble first tries; reruns passed).
- 14:25Z phase 8: baseline base-0525e1f set (gate-158-1's runs + wobble60, which crashed in Vita3K both tries: the 73-picture run kept as the baseline). last_good = 0525e1f: every route reached its stop line or crashed only with #161's Vita3K signature on the same VPK. Vita3K: 2 crashes (wobble60).
- 15:35Z phase 8: #160 worker flagged 'Interfere With Workloads' by the harness; checked its commands: no kills, no docker stops, Vita3K config untouched, only its own build/ copies and Vita3K runs. Found another project's autopilot (atv2-vita) running Vita3K concurrently (lock is per game): lead added to #161; worker told to stop measuring crash rates and commit. Vita3K: none of ours new.
- 15:49Z phase 8: #160 reported (reports/160-1.md); wobble60 was non-deterministic (interp_budget=1 skips by measured time): route now interp_budget=0, re-baselining it on 0525e1f's VPK, then gating #160 (one job). .gitignore runtime/ anchored to /runtime/ (it hid the kit's vita/runtime/). Filed #164 (gems pad untracked). Vita3K (worker's): 4 crashes, 1 overlapped atv2-vita's Vita3K.
- 15:58Z phase 8: wobble60 re-baselined with interp_budget=0 on 0525e1f: deterministic (runs b and d 91/91 same; base-0525e1f/wobble60 = run b). #160 gate: merge and build pass; route step rerun (coordinator script bug: run folder missing, every route exit 1 before Vita3K started; not the branch's). Vita3K: 2 crashes (wobble60 baseline tries a and c).
- 16:10Z phase 8: merged #160 (cfe0d45; gate: title/pause-ws/pause-ws-wobble pass, identical pictures; wobble60 crashed 3/3 with #161's signature at the baseline's own crash points, pictures identical up to them, another project's Vita3K overlapping: autopilot's, not the branch's). Baseline base-cfe0d45 with counters: gate runs + wobble60 (running). Vita3K: 3 crashes (wobble60).
- phase 8: workers on #161 (Vita3K crash, with the concurrent-Vita3K confound to separate) and #163 (ctest exec bits).
- 16:17Z phase 8: baseline base-cfe0d45: title/pause-ws/pause-ws-wobble from gate-160-1 (with counters); wobble60 crashed 4/4 on cfe0d45's VPK (0/7 with the gate's), against 2/4 on 0525e1f: suspected regression from #160, not proven (atv2-vita's Vita3K overlapped both sets). wobble60's picture baseline stays 0525e1f's complete run; no wobble60 counters baseline yet. last_good stays 0525e1f; the A/B handed to #161's worker before any bisect/revert. Vita3K: 4 crashes (wobble60).
- 18:01Z phase 8: #161 interim D132 (0ec1ba6): the crash is on Vita3K's render thread running corrupted renderer::Commands (host fault; Vita3K keeps them in the guest's VDM ring); every setting crashes; crashes also with no other Vita3K running; 0525e1f vs cfe0d45 3/9 vs 0/12 overall but 1/5 vs 0/5 interleaved and a clean cfe0d45-code build 1/5: #160 not shown to raise the rate. A diagnostic build passed 4/5 (layout or timing?). Suspected-regression note on #160 stands down for now.
- 18:04Z phase 8: #161 attempt 1 incomplete (reports/161-1.md): the crash is Vita3K's render thread running corrupted commands; no guest write found; every setting crashes, solo too; #160 cleared; a read-only diagnostic build passed 4/5 (unexplained). Attempt counted, branch auto/161-1 kept for attempt 2. Gate policy meanwhile: a route ending in exit 2 with this signature is Vita3K's; compare pictures up to the crash and run autopilot's VPK interleaved. Vita3K: 21 crashes in the worker's 26 runs.
- phase 8: #161 attempt 2 started (auto/161-2): diag1 vs clean interleaved, bisect the diag patch, vglSetVDMBufferSize probe; ~20 runs.
- 18:06Z phase 8: #163 reported done (reports/163-1.md: colima reverse-sshfs drops ld's chmod; link_exec.sh launcher + regex-checked mutants; ctest 29/29). Gate running: merge, VPK build, title and pause-ws only (the change is inside if(linux32), and the wobble routes crash ~80% in Vita3K while #161 needs the runner). Filed #165 (docs for it).
- 18:18Z phase 8: merged #163 (ad697c3; host-only by construction, title 10/10 same, pause-ws 3 crashes with #161's signature and identical pictures before them). last_good stays 0525e1f: no route can pass reliably in Vita3K until #161. Vita3K: 3 crashes (pause-ws, cache off).
- 19:06Z phase 8: #161 attempt 2 done (reports/161-2.md): Vita3K's crash is the game wrapping vitaGL's 128 KB VDM ring over commands Vita3K's render thread hasn't run; vdm_ring=1024 passed 5/5 vs 2/5. Every route now passes vdm_ring=1024. Gate running (gate.py, all four routes). Filed #166 (main.c unknown-key line).
- 19:14Z phase 8: merged #161 (5eb8423; every route exit 0 first try with vdm_ring=1024, pictures identical). Baseline base-5eb8423 = the gate's runs, the first with every route complete and counters on all of them; last_good = 5eb8423. Vita3K: 0 crashes.
- phase 8: workers on #162 (presenter thread with the cache on), #164 (gems pad), #166 (main.c vdm_ring key).
- 19:26Z phase 8: #166 reported done (reports/166-1.md); gate running (gate.py, four routes).
- 19:47Z phase 8: merged #166 (every route passes, pictures and counters unchanged). Vita3K: 1 start-up crash (title, MoltenVK device lost at the first acquire; gotchas 24), rerun passed.
- 19:49Z phase 8: #164 out of scope (three more scripts and a second pad use logs/pads/): closed without an attempt, branch auto/164-1 kept, continued in a new issue that starts by merging it.
- phase 8: worker on #167 (pad scripts, from auto/164-1).
- 21:12Z phase 8: #162 reported done (reports/162-1.md): presenter on core 1 with the cache on, pictures identical, game thread's vc.* -50% on wobble60. Gate running. #167's worker stopped on the API weekly limit (resets 21:00 Dublin); its commits are on auto/167-1, to resume by message.
- 21:21Z phase 8: merged #162 (every route first try, pictures identical, wobble60 vc.* -50% on the game thread). New baseline = the gate's runs; last_good moved. Vita3K: 0 crashes.
- phase 8: worker on #159 (cheaper vertex cache); #167's worker resumed after the API limit.
- 21:30Z phase 8: merged #167 (6a92241; tools/pads/docs only: scope, merge, noassets; worker's host A/B checks all identical). Filed #168 (fast_render_ab gems run loads a vblank late with --prof-draw).

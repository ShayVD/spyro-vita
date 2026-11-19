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
      "issue": 160,
      "branch": "auto/160-1",
      "worktree": "/Users/shayvandam/Projects/spyro-vita-auto-160",
      "pids": [],
      "started_at": "2026-11-19T14:22:56Z",
      "files": [
        "third_party/vita-port-kit/",
        "src/platform/vita/",
        "src/platform/common/gpu_walk.c",
        "src/platform/common/vcache.c",
        "CMakeLists.txt",
        "cmake/",
        "autopilot/config.json",
        "docs/decisions-2.md",
        "docs/vita-testing.md"
      ],
      "agent": "aec0c934d50cf302d",
      "last_commit_at": "2026-11-19T15:36:43+00:00",
      "reported": true
    }
  ],
  "last_good": "0525e1f4ce9bba5a8f14cd62c066af6bb42062b7",
  "baselines": {
    "run_dir": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/runs/base-0525e1f",
    "commit": "0525e1f4ce9bba5a8f14cd62c066af6bb42062b7"
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
  "attempts": {},
  "blocked": [],
  "issues_filed": {
    "date": "2026-11-19",
    "phase": 8,
    "count": 6
  },
  "round": {
    "n": 1,
    "base_run_dir": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/runs/base-f7aeb43",
    "base_commit": "f7aeb4343248602a2c3797f000b60a436bb62b6f",
    "started_at": "2026-11-19T13:05:58Z",
    "filed": 6,
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

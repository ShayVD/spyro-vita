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
      "issue": 158,
      "branch": "auto/158-1",
      "worktree": "/Users/shayvandam/Projects/spyro-vita-auto-158",
      "pids": [],
      "started_at": "2026-11-19T11:51:30Z",
      "files": [
        "src/game/pres/pres_draw.c",
        "src/game/render_ctx.c",
        "src/game/render_ctx.h",
        "src/platform/common/vcache.c",
        "src/platform/common/psx_vcache.h",
        "src/platform/vita/gpu_gl.c",
        "src/platform/vita/gpu_thread.c",
        "src/platform/linux32/main.c",
        "docs/decisions-2.md",
        "docs/presentation.md"
      ],
      "agent": "a73c23883e2369b4f"
    }
  ],
  "last_good": "",
  "baselines": {
    "run_dir": "",
    "commit": ""
  },
  "hardware_runs": [],
  "perf_candidates": [
    {
      "title": "Presenter thread with the vertex cache on (persp/subpixel force the presenter inline, gpu_thread.c:1301, D126)",
      "expected": "wobble fix at 60 fps gets the core-1 presenter back; file after #158 merges",
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
    "count": 3
  },
  "round": {
    "n": 0,
    "base_run_dir": "",
    "base_commit": "",
    "started_at": "",
    "filed": 0,
    "investigated": false
  },
  "rounds_without_gain": 0,
  "hardware_pending": null,
  "job": {
    "kind": "baseline",
    "issue": 0,
    "step": 1,
    "pid": 70358,
    "out": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/baseline.log",
    "started_at": "2026-11-19T11:52:05Z"
  },
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

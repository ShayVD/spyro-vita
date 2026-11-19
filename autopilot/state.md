# Autopilot state: Spyro the Dragon (spyro-vita)

Written by the port-autopilot coordinator (vita-port-kit `playbooks/port-autopilot`) at the end of every tick and
committed on the `autopilot` branch. The JSON block is what the tools read
(`autopilot.py check autopilot/state.md`); the notes under it are for people.

```json
{
  "version": 1,
  "game": "spyro",
  "phase": 8,
  "workers": [],
  "last_good": "",
  "baselines": {
    "run_dir": "",
    "commit": ""
  },
  "hardware_runs": [],
  "perf_candidates": [],
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
    "date": "",
    "phase": 8,
    "count": 0
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

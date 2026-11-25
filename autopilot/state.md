# Autopilot state: Spyro the Dragon (spyro-vita)

Written by the port-autopilot coordinator (vita-port-kit `playbooks/port-autopilot`) at the end of every tick and
committed on the `autopilot` branch. The JSON block is what the tools read
(`autopilot.py check autopilot/state.md`); the notes under it are for people.

```json
{
  "version": 1,
  "game": "spyro",
  "phase": 5,
  "workers": [
    {
      "issue": 231,
      "branch": "feat/231-wobble-fix-particlesdraw-s-missed",
      "worktree": "/Users/shayvandam/Projects/spyro-vita-issue-231",
      "pids": [],
      "started_at": "2026-11-25T05:15:21Z",
      "files": [
        "src/game/particles_hand.c",
        "src/game/particle_slots_hand.c",
        "src/platform/common/vcache.c",
        "src/platform/common/psx_vcache.h"
      ],
      "agent": "aa6c03cf1d320f4e7"
    }
  ],
  "last_good": "29d857aebaad17fe28639c43b54a89f2086b74e5",
  "hardware_runs": [
    {
      "date": "2026-11-21 12:00",
      "what": "owner OK: pulled spyro.log, spyro-prev.log, config.txt (read only); owner's play on b1a41ac, PAL, 60 HZ 2x: late/300 Artisans 19.6->1.4, Stone Hill 20.6->1.2 vs b604db4; 2x held at 1x by the 60 Hz budget (#200); shaders 10 s a boot (#201); wobble 99.8% whole in Stone Hill (#202)",
      "log": "logs/owner-20261121-1200/"
    },
    {
      "date": "2026-11-21 ~10:05",
      "what": "owner's yes ('Yes, install it'): b1a41ac (D197, vc_lazy=0 default) eboot installed, READBACK IDENTICAL (SHA-1 a9cb468c), left on the Vita",
      "log": "logs/hw-put-b1a41ac.out"
    },
    {
      "date": "2026-11-21 09:35-09:55",
      "what": "session 5 (owner's yes 'sure run them'): #198 bisect AP15a parse_thread=1 / AP15b vc_lazy=0 / AP15c vc_lazy_logic=0; only vc_lazy=0 clears the game-over lock (0 late/300), D197",
      "log": "logs/hw-ap15a..c"
    },
    {
      "date": "2026-11-21 09:03-09:30",
      "what": "session 4 (owner's yes 'vita is available for you, go'): AP11 owner60 on 63d80f4 (found whole 98.63->99.27%, busy 60->53%, 0 late), AP12 shot_hd (close walls/ground only), AP13 Toasty 30 Hz (Toasty late/300 30.7->3.5), AP14a/b owner60 on Toasty ca4450e vs 63d80f4 (opening 78->0 late/300); regression on the game-over screen at 30 Hz: 0 -> 60 late/300 (#198); 63d80f4 left on the Vita (READBACK IDENTICAL, SHA-1 f102a69c)",
      "log": "logs/hw-ap11..hw-ap14b, D192"
    },
    {
      "date": "2026-11-20 17:15-17:36",
      "what": "session 3 (owner's yes): AP8a/b owner60 b604db4 vs ca4450e (sky 1.54->1.03 ms, found whole 96.36->98.20%, 0 late both), AP9a/b shot_hd (changes only on close walls/ground), AP10 Toasty vs AP2 (presenter saturates in Toasty: late/300 1.2->30.7, #193); ca4450e left on the Vita (READBACK IDENTICAL)",
      "log": "logs/hw-ap8a..hw-ap10, D179"
    },
    {
      "date": "2026-11-20 13:49",
      "what": "owner OK: pulled spyro.log, spyro-prev.log, config.txt from the Vita (read only)",
      "log": "logs/owner-20261120-1349/"
    },
    {
      "date": "2026-11-20 13:45",
      "what": "owner asked for the current build to test by hand: autopilot b604db4 (#177 PSX WOBBLE row, #178 edge vertices) eboot installed, READBACK IDENTICAL (SHA-1 9eb4795b), left on the Vita",
      "log": "logs/hw-put-b604db4.out"
    },
    {
      "date": "2026-11-20",
      "commit": "37c3a95",
      "runs": "AP1-AP7 (tools/vita/fps-runs/README.md)",
      "result": "D172: PAL OK; wobble fix +2.6 ms game thread at 60 fps; presenter on core 1 -35%; GL shadow no time; cheaper cache below noise; vdm_ring=1024 harmless, default off"
    },
    {
      "date": "2026-11-22",
      "commit": "85a7b2c",
      "runs": "AP16a-d, AP16t (runtime/autopilot/hw-session6.sh)",
      "result": "D209: shader cache 10.3 s -> 0.14 s second boot; Stone Hill play busy 73.2 -> 71.5% but 48 KB budget holds 1x with 15-28% spare (#212); vc_lazy_logic=3 no gain; Artisans late 16.5 -> 0.5/300; attract demo ~177 late/300 inline presenter (#220)"
    },
    {
      "date": "2026-11-23",
      "commit": "687ca1d",
      "runs": "AP18a-c, AP18o, AP18t (runtime/autopilot/hw-session7b.sh)",
      "result": "D219: dd_budget=auto x1.03-1.41 vs 48 KB x1.00-1.31, held at 1x by measured time (15.2 ms of 14.5); D216-D218 cost nothing; attract demo 166 late/300, game waits 12 ms/vblank on presenter worker (#227); 60 Hz Artisans/Toasty over 14.5 ms at 1x, Artisans sky +0.5 ms, late 2.5/0.9 per 300 (#228)"
    }
  ],
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
      "title": "Hardware check of vdm_ring=1024 (D166) before making it the default",
      "expected": "no fps or picture change, +896 KB RAM; owner-approved run only",
      "issue": 0
    },
    {
      "title": "Hardware A/B for #162 (presenter on core 1 with the cache on)",
      "expected": "about half of D89's ~4.5 ms/vblank cache work leaves the game thread; s7f3-toasty60-pres + o-toasty + persp=1 subpixel=1, A this path vs B pres_thread=0",
      "issue": 162
    },
    {
      "title": "Hardware run for #159 (cheaper vertex cache)",
      "expected": "less game- and presenter-thread time with persp+subpixel at 60 fps; D167's form on the merged VPK vs 8dab625",
      "issue": 159
    },
    {
      "title": "Hardware run for #169 (redundant GL state skipped)",
      "expected": "less render-thread time per picture; pause-ws and wobble60, merged VPK vs autopilot-bbc36b1.vpk",
      "issue": 169
    },
    {
      "title": "Smaller expanded vertex when persp/subpixel are on without tex_filter (drop the UV box)",
      "expected": "wobble60 vbo.bytes 274 KB -> ~219 KB a frame (~16 -> ~13 MB/s at 60 fps): too small to matter on the Vita's bandwidth; not filed",
      "issue": 0
    },
    {
      "title": "Fewer program switches (wobble60 95/frame, all real after #169's dedup)",
      "expected": "needs draw reordering, which changes blending order: not byte-identical; not filed",
      "issue": 0
    },
    {
      "title": "README known issues: the wobble options' cost at 60 fps (after the hardware session)",
      "expected": "one line once AP1-AP3 are measured; the README already lists both options and their defaults",
      "issue": 0
    },
    {
      "title": "Hardware: start-up shader compile time and the 30/60 fps cost with PSX WOBBLE off (the fix) as the default",
      "expected": "owner-approved run after #177-#180 merge: AP1/AP2-style Toasty pair plus a cold start",
      "issue": 177
    },
    {
      "title": "Hardware: #181 owner60 at dd200 for the owner's 'changing' (ambiguous keys resolved, grid x16 depths)",
      "expected": "owner-approved: fewer perspective/affine flips; vcache clamps found whole ~98%",
      "issue": 181
    },
    {
      "title": "Hardware: #187 OT walk overlap (owner60/AP8 at 60 HZ, plus a heavy Artisans window at 1x)",
      "expected": "owner-approved: 'waited for the walk' ~0, game thread ~12 ms a tick in heavy windows, ~300 pictures/300",
      "issue": 187
    },
    {
      "title": "Hardware: #189/#181 pictures and cost (shot_hd at owner60 holds; vc.probe/lines +3-5%) on the owner's settings",
      "expected": "owner-approved: shadows/glows/portal edge drawn exactly; found whole ~99.6%",
      "issue": 189
    },
    {
      "title": "Hardware: AP10 rerun (Toasty 30 Hz + in-between) after #193 and #195",
      "expected": "owner-approved: pres busy down, passed-by ~0, Toasty late/300 back toward AP2's ~1",
      "issue": 193
    },
    {
      "title": "Hardware: #179 logic-frame deferral at 60 HZ (owner60-like heavy Artisans: game thread vs walk balance)",
      "expected": "owner-approved: logic draw ~2 ms a tick lower in heavy windows; walk takes it while idle",
      "issue": 179
    },
    {
      "issue": 203,
      "what": "hardware: 30 Hz still holds where 2x now draws more (title flyby, attract demos); if not, give 30 Hz a budget (D201)"
    },
    {
      "issue": 201,
      "what": "hardware: second boot reads 17 shader-cache hits; black-screen time (D200)"
    },
    {
      "issue": 204,
      "what": "hardware A/B: vc_lazy_logic=1 vs 3 in the owner 60 HZ Stone Hill and Artisans windows; if 3 wins make it the default (D206)"
    },
    {
      "title": "Hardware A/B for #212 (dd_budget=auto vs 48 KB) on owner60/stone60-style 60 Hz 2x",
      "expected": "2x holds at 60 Hz where the frame has room, no FPS loss; owner-approved run only",
      "issue": 212
    }
  ],
  "owner_decisions": [
    "2026-11-22: owner asked to fix the known wobble-fix and draw-distance issues and loop until fully fixed; filed #211-#219 (phase-8). Done = missed (non-exempt) wobble vertices at 0 on host routes S/P/Pf, 2x holding at 60 Hz on the owner's settings on hardware.",
    "2026-11-21 07:05: round 9 ended (all phase-8 issues merged, D175-D197 (with D194, D195, D196)). owner60 vs base-3ba4fd2: vc.misses -97.8%, gl.draws -6.0%, vc.proj -7.2%, vc.lines +13.5% -> no gain by rule (rounds_without_gain 1). Round 10 opened; its investigation needs hardware numbers (FPS only from hardware), so the loop pauses for the owner's yes on a hardware session.",
    "2026-11-21 02:45: coordinator: #196 rescoped to this repo (vc_lazy_logic= key, docs). Raising the vendored kit's VPK_PC_MAX would need a change in ../vita-port-kit and a re-vendor (VENDORED: change the kit, re-vendor); left for the owner.",
    "2026-11-20 16:55: owner said yes to the Vita runs, then 'actually ask me again when you have the builds ready'. Session 3 (runtime/autopilot/hw-session3.sh: AP8a/b owner60 on hardware b604db4 vs ca4450e, AP9a/b shot_hd pictures, AP10 Toasty vs hw-ap2, then put-eboot ca4450e) is prepared and NOT run; ask again before running.",
    "2026-11-20 15:40: coordinator: two workers (#181, #186; no shared files) now that other sessions' containers are down to one restarting salsaas container. Baseline 5cdfccb running.",
    "2026-11-20 14:20: coordinator: #183 dispatched before #181 (pick gave #181; both p0 and share vcache.c, #183's no-entry is the larger share of the owner's stretching). Baseline re-run at 474d375 (after #177, #178, #180) into runs/base-474d375.",
    "2026-11-20 13:55: owner (b604db4 on hardware, PAL 60 HZ 16:9 dd200, PSX WOBBLE OFF): still sees textures stretching and changing; eliminate it completely. Log logs/owner-20261120-1349: 90.6-96% of polygons found whole in play, no entry 12-38k and ambiguous up to 24k per 300 vblanks. #181 and #183 raised to p0 (next after #180, before #179), evidence and stronger Done-when on both issues.",
    "2026-11-20 13:50: owner: push everything as you go. Push autopilot (PR #176) after every merge and state tick; still never push main, workers still never push.",
    {
      "issue": 0,
      "question": "Where does the autopilot start on spyro-vita?",
      "default_taken": "phase 8 (owner, 2026-11-19: the port is basically done, look for optimisations; wobble removal as a menu option)",
      "switch": "state.phase"
    },
    {
      "issue": 175,
      "question": "colima's sshfs mount loses execute bits after the link: keep the launcher, re-check in the run tools, build/ on a Docker volume, or colima virtiofs?",
      "default_taken": "none started; pick is option 2 (re-check in the tools that run binaries), waiting for the owner",
      "switch": "none"
    },
    {
      "issue": 0,
      "question": "Hardware session for the six pending measurements (configs AP1-AP6b in tools/vita/fps-runs, #170)",
      "default_taken": "done 2026-11-20 on the owner's yes (D172); results in hardware_runs",
      "switch": "none"
    },
    {
      "issue": 0,
      "question": "Restart after D172 (2026-11-20)",
      "default_taken": "owner: one PSX WOBBLE row, no wobble by default (#177); no wobble at all and no FPS drop (#178, #179, #181); fix draw distance 2x (#180); start the loop again",
      "switch": "state.phase"
    },
    {
      "issue": 208,
      "question": "at 2x, pass-2 (far) mobys skip the vertex cache to save 2-3 ms?",
      "default": "keep the wobble fix on far mobys (no change)",
      "status": "open"
    }
  ],
  "attempts": {
    "161": 1
  },
  "blocked": [],
  "issues_filed": {
    "date": "2026-11-20",
    "phase": 9,
    "count": 6
  },
  "round": {
    "n": 12,
    "base_run_dir": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/runs/base-85a7b2c",
    "base_commit": "85a7b2c9c2beb85b8b7b4cad0243377d2bfc6c56",
    "started_at": "2026-11-22T06:56:29Z",
    "filed": 31,
    "investigated": true
  },
  "rounds_without_gain": 0,
  "hardware_pending": null,
  "job": null,
  "batch": null,
  "lanes": [
    5
  ],
  "targets": {
    "vita": {
      "phase": 8,
      "lanes": [
        8
      ],
      "baselines": {
        "run_dir": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/runs/vita/base-29d857a",
        "commit": "29d857aebaad17fe28639c43b54a89f2086b74e5"
      }
    }
  },
  "published": [
    {
      "branch": "release/encore-resume-d244",
      "commit": "7b6e66da036fe5639009c4a0b6f6b318387d9262",
      "pr": 248,
      "merged_at": null
    }
  ]
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
- 18:01Z phase 8: #161 interim D166 (0ec1ba6): the crash is on Vita3K's render thread running corrupted renderer::Commands (host fault; Vita3K keeps them in the guest's VDM ring); every setting crashes; crashes also with no other Vita3K running; 0525e1f vs cfe0d45 3/9 vs 0/12 overall but 1/5 vs 0/5 interleaved and a clean cfe0d45-code build 1/5: #160 not shown to raise the rate. A diagnostic build passed 4/5 (layout or timing?). Suspected-regression note on #160 stands down for now.
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
- 22:37Z phase 8: #159 reported done (reports/159-1.md): same answers (checksums identical over 91 M vertices), divisions/vertex ~1.0 -> ~0.4, table lines -15%. Gate running.
- 22:44Z phase 8: merged #159 (every route first try, pictures and counters unchanged; same answers with ~60% fewer divisions per vertex). New baseline from the gate; last_good moved. Vita3K: 0 crashes.
- phase 8: worker on #168 (host determinism with --prof-draw; the profiler is src/platform/linux32/prof_draw.c, not src/game/).
- 23:48Z phase 8: merged #168 (bec34cc; the gems runs shared ./save, so only the first reached the gems; each run now gets its own card and a gems check). Script/docs only.
- phase 8: worker on #165 (docs for #163), the last open issue.
- 23:55Z phase 8: merged #165 (docs). Round 1 ended with a gain: wobble60 vc.proj/polys/verts/misses -50% (base-5eb8423, the first wobble60 counters, vs base-bbc36b1), none worse; rounds_without_gain 0. last_good = head (no src/cmake/kit change since bbc36b1, whose gate is the regression run). Round 2 started; first candidate from the counters: redundant GL state calls (programs 105 vs draws 84 per frame).
- phase 8: removed a stale worker entry for #165 (left after its merge; it made pick skip #169). Worker on #169 (redundant GL state calls).
- 00:33Z phase 8: #169 reported done (reports/169-1.md): wobble60 attribs 421->15, uniforms 187->26, tex_binds 44->2, gl.state 316->243 per frame, draws/tris identical, pictures identical. Gate running.
- 00:44Z phase 8: merged #169 (9d42dac; every route first try, pictures identical, 5 counters better on both benchmarks). Round 2 ended with a gain (base-bbc36b1 vs base-9d42dac: gl.state/programs/tex_binds/uniforms/attribs better, none worse); rounds_without_gain 0. Round 3 started. Five hardware measurements now wait for the owner (perf_candidates).
- 01:15Z phase 8: round 3 investigated the benchmarks' upload counters (one VBO upload a frame: pause-ws 96 KB, wobble60 274 KB at ~40 B/vertex; textures 11 KB once): nothing worth filing. Round 3 ended with nothing merged and nothing filed: no gain, rounds_without_gain 1. Round 4 started. The remaining levers need hardware timing (6 candidates wait for the owner).
- 01:47Z phase 8: round 4 investigated gl.programs (95/frame on wobble60): every bind already goes through gs_use_program's dedup (#169), so the rest are real switches between joined draws' formats and kinds; cutting them needs reordering draws (blend order): not filed. Round 4 ended without a gain: rounds_without_gain 2 of 3. Round 5 started.
- 02:20Z phase 8 -> 9: round 5 investigated ot.packets (wobble60 463 = two pictures a tick x 232, the cost of 60 fps itself): nothing filed, third round without a gain: phase 8's exit. Phase 9 started (round 6): filed #170 (hardware run configs for the six pending measurements), #171 (stale comments), #172 (cache cost counters into vpk-perfcount), #173 (host ./save warning).
- phase 9: workers on #170, #171, #172, #173.
- 02:36Z phase 9: merged #170 (hardware run configs AP1-AP6b, parsed against hw_mock; no Vita touched). The hardware session is ready when the owner says yes.
- 02:39Z phase 9: #172 reported done (reports/172-1.md): six vcache cost counters in vpk-perfcount, pictures and older counters unchanged. Gate running.
- 02:44Z phase 9: merged #171 (comments only; objects byte-identical).
- 02:49Z phase 9: merged #173 (host ./save warning; host only). Filed #174 (host_checks.sh demo run without --save).
- 02:50Z phase 9: merged #172 (all routes first try, pictures identical, older counters same; new counters). New baseline from its gate; last_good moved. Worker on #174.
- 03:12Z phase 9: merged #174 (host_checks.sh demo gets its own --save). Filed #175 (needs-owner: colima's sshfs mount loses an execute bit after link_exec.sh's check). No workers running.
- 03:13Z phase 9: round 6 ended (polish merges #170-#174, no counter better: rounds_without_gain 1 of 3). README checked: it lists PERSPECTIVE TEXTURES and SUB-PIXEL VERTICES with defaults; the 60 fps cost line waits for the hardware session. Round 7 started. Waiting on the owner: hardware session, #175, merging autopilot into main.
- 04:13Z phase 9: round 7 investigated the dead interp_mode key in fps-runs: the s7f*/s7g* files record past hardware runs and keep the lines those runs used; nothing filed. No gain: rounds_without_gain 2 of 3. Round 8 started. #175 unanswered; no owner reply on the hardware session.
- 05:14Z phase 9: round 8 investigated whether autopilot's changes reach the CI VPK build (.github/workflows build.yml runs tools/ci/build_vpk.sh, unchanged; the new perfcount source goes through the same CMake the native gates built): nothing filed. No gain: rounds_without_gain 3 of 3.
- 05:15Z phase 9: stop-check printed stop (phase 9, no workers, 3 rounds without a gain, only #175 open and it needs the owner). Final report: autopilot/report.md. Loop ended.
- 2026-11-20T11:03Z phase 8 again (owner, 2026-11-20, after D172's merge and hardware session): filed #177-#181 (PSX WOBBLE row, clamped vertices, hook cost, draw distance 2x, remaining misses); route and benchmark owner60 added; Vita3K: 0
- 2026-11-20T11:05Z phase 8: round 9 started (owner's issues); baseline job base-3ba4fd2 running; worker #177 started (one worker: other sessions' containers running); Vita3K: 0
- 2026-11-20T11:27Z phase 8: baseline base-3ba4fd2 built (wobble60 needed a rerun: Vita3K crash); pause-ws-wobble's title shots differed from merge-m1 by interp_budget skips (Vita3K timing, 60 HZ default since D150): title, pause-ws, pause-ws-wobble now pin interp_budget=0, re-running; Vita3K: 1 crash
- 2026-11-20T11:32Z phase 8: tick - #177 working (no commit yet, running Vita3K routes), baseline re-run queued behind it on the Vita3K lock; one worker (other sessions' containers); Vita3K: 0
- 2026-11-20T11:50Z phase 8: #177 reported (code done; gate waits for the re-baseline job), worker #178 started, filed the docs/bench follow-up; Vita3K: 0
- 2026-11-20T12:00Z phase 8: merged #177 (D194; 5 routes the same against base-3ba4fd2); re-baseline done, pinned routes deterministic (91/91 repeat); worker #178 running; Vita3K: 0
- 2026-11-20T12:26Z phase 8: tick - #178 committed (clamped vertices drawn at their exact position), running its checks; one worker (other sessions' containers); Vita3K: 0
- 2026-11-20T12:47Z phase 8: #178 reported (clamped vertices exact; owner60 fallbacks 4,068 -> 621, found 93.30 -> 93.36%); gate job: GPU window captures (CAPTURE=1) base vs #178 on wobble60's lines; filed #183 (no-entry vertices, ~75% of misses) and #184 (stats race); Vita3K: 0
- 2026-11-20T12:48Z phase 8: worker #180 started (draw distance 2x); #178 gate waits for the GPU captures; Vita3K: 0
- 2026-11-20T12:52Z phase 8: tick - #178 captures: base done, #178's running; #180 working (no commit yet); Vita3K: 0
- 2026-11-20T12:54Z phase 8: merged #178 (D195; GPU window grabs identical to the baseline's); #180 running; Vita3K: 0
- 2026-11-20T13:10Z phase 8: #180's worker drew a security warning (interfere with workloads); its 32 commands checked: reads, two buildlock builds, Vita3K route runs under perl alarm waiting for the lock; no kill, no docker, no Vita; it goes on; Vita3K: 0
- 2026-11-20T13:18Z phase 8: tick - #180 running experiments (no commit yet); 6 containers of other sessions: one worker; queued #179, #182, #183, #181, #184; Vita3K: 0
- 12:33Z phase 8: round 11 started (owner 2026-11-21: flawless and fully optimized); #200-#202 rewritten as tasks; worker #200 started (others wait: other sessions' containers running)
- 12:59Z phase 8: worker #200 running (no commit yet); PR #176 merge checks green (vita, linux32 32/32, linux64, noassets; CI pass); pick empty (other sessions' containers)
- 13:30Z phase 8: #200 worker committed the lod-line fix (680ad01), profiling continues; pick empty
- 14:01Z phase 8: #200 worker still profiling (host runs live); pick empty
- 14:31Z phase 8: merged #200 (D199); filed #203-#207, #208 needs-owner
- 14:33Z phase 8: workers started for #201, #203, #205
- 15:04Z phase 8: #201 #203 #205 running, no commits yet; pick empty; #208 unanswered
- 15:35Z phase 8: #201 #203 #205 still running (long host/Vita3K runs), no commits yet; #203 ran up to 4 host runs at once (flagged, benign: own processes only)
- 16:06Z phase 8: #201 #203 #205 have uncommitted edits in scope; #205 Files path corrected (src/game/render_ctx_names.h)
- 16:37Z phase 8: stall check flagged #201 #203 #205 (no commit in 2 h) but all three are alive with in-scope edits; asked each for a WIP commit instead of stopping them
- 17:10Z phase 8: merged #201 (D200)
- 17:11Z phase 8: gate #203 running; worker #207 started
- 17:39Z phase 8: merged #203 (D201); owner60 30 Hz shots changed as expected (2x now grows there)
- 18:11Z phase 8: #205 gate running; #207 reported, gate queued
- 18:19Z phase 8: merged #205 (D202)
- 18:20Z phase 8: gate #207 running; worker #206 started (#202, #204 wait: docs/vita-testing.md with #207, vcache.c with #206)
- 19:05Z phase 8: merged #207 (D203)
- 19:37Z phase 8: #206 running (WIP commit 2e1cfb3); #202 #204 wait on vcache.c
- 20:08Z phase 8: #206 committed 69fafd6, running its checks; #202 #204 wait
- 20:39Z phase 8: #206 still running its checks (last commit 19:39Z); #202 #204 wait
- 21:10Z phase 8: #206 still running its checks; #202 #204 wait
- 21:41Z phase 8: stall check flagged #206 (last commit 19:39Z) but it is alive in its final checks (Vita build done, routes owner60/pause-ws running, ctest log present): not stopped
- 21:56Z phase 8: merged #206 (D204)
- 21:57Z phase 8: worker #202 started; #204 waits on vcache.c
- 22:29Z phase 8: #202 running (no commit yet, started 21:55Z); #204 waits
- 22:59Z phase 8: #202 WIP commit 3ad4e46 (sky clip cuts merged at one edge pixel; temporary diagnostics, uncommitted linux32 diag edits outside Files to be removed); #204 waits
- 23:31Z phase 8: #202 still investigating (WIP 3ad4e46); #204 waits
- 00:02Z phase 8: #202 second WIP 878f25c (merged cuts never move a point placed without them); #204 waits
- 00:33Z phase 8: #202 final commit 0137ab6 (last misses in a portal fly-in and Stone Hill), running its checks; #204 waits
- 01:04Z phase 8: #202 checks running (final 0137ab6); #204 waits
- 01:35Z phase 8: #202 still in final checks (0137ab6); #204 waits
- 02:01Z phase 8: merged #202 (D205); filed #209 (particles hand-over)
- 02:02Z phase 8: worker #204 started; #209 waits on vcache.c
- 02:34Z phase 8: #204 running (no commit yet, started ~02:04Z); #209 waits
- 03:05Z phase 8: #204 WIP ec05cb8 (vc_lazy_logic=3, a quota sized from the walk idle time); #209 waits
- 03:41Z phase 8: merged #204 (D206); coordinator raised main.c vc_lazy_logic clamp to 3
- 03:42Z phase 8: worker #209 started
- 04:13Z phase 8: #209 committed 5e3c295 (particles hand-over), checks running
- 04:44Z phase 8: #209 second commit 9eec614 (a particle corner gives way to other hand-overs), checks running
- 05:19Z phase 8: merged #209 (D207); vita-testing.md particles line updated
- 05:20Z phase 8: worker #210 started (stone60 Vita3K route)
- 05:51Z phase 8: #210 running (no commit yet)
- 06:22Z phase 8: #210 committed 628469d (stone60 route, host pads S/P/Pf) and 53c9fa0 (docs); finishing checks
- 06:40Z phase 8: merged #210 (D208): stone60 route, baseline base-bac5699/stone60
- 06:41Z phase 8: round 11 backlog done (D199-D208); round-end baseline running
- 06:56Z phase 8: round 11 ended (D199-D208, 10 merges). base-63d80f4 -> base-85a7b2c: vc.misses -15.5% (wobble60) / -21.6% (owner60) better; vcp.div +4.6..+17.7% worse (D197: presenter eager, deliberate); stone60 vc.invalid/clamped/div move as timing noise (D208). Shots same except owner60 30 Hz 2x (D201). Rule: no gain -> rounds_without_gain 2. The round s speed-ups (D202, D204, D206) cut cost per operation, invisible to Vita3K counters: round 12 waits for a hardware run (owner yes)
- 2026-11-22T08:30Z phase 8: filed #211-#219 (owner: wobble and draw distance until fully fixed), Vita3K: none
- 2026-11-22T17:24:53Z phase 8: started #211 (missed/exempt split); #212-#219 wait on its files; hardware session (hw-session5.sh) waits for the owner, Vita3K: none
- 2026-11-22T17:56:19Z phase 8: #211 committed 85dbac2, running its Vita3K routes; nothing else pickable (files overlap), Vita3K: none
- 2026-11-22T19:04:52Z phase 8: hardware session 6 recorded (D209), filed #220, rounds_without_gain 0, Vita3K: none
- 2026-11-22T19:11:37Z phase 8: #211 reported (incomplete: hook outside Files); coordinator widened Files, applied the hook (429ac24), gate running; filed #221, #222, Vita3K: none
- 2026-11-22T19:25:00Z phase 8: #211 scripted gate PASS; Vita build + routes with the hook running (check-211.sh), Vita3K: none yet
- 2026-11-22T19:40:55Z phase 8: merged #211 (D210), Vita3K: none
- 2026-11-22T19:42:42Z phase 8: started #212, #215, #220, Vita3K: none
- 2026-11-22T20:45:41Z phase 8: WIP commits on #212, #215, #220; all still checking, Vita3K: none
- 2026-11-22T22:12:21Z phase 8: #212 and #215 committed more; #220 still on its WIP; all checking, Vita3K: none
- 2026-11-22T22:35:26Z phase 8: #220 reported done; gate running, Vita3K: none
- 2026-11-22T23:02:47Z phase 8: #212 reported done (Vita3K stone60 x1.00 -> x1.82-2.00); gate waits for #220 gate, Vita3K: none
- 2026-11-22T23:11:40Z phase 8: merged #220 (D211), Vita3K: none
- 2026-11-22T23:20:09Z phase 8: #215 reported done (PortalSkyPlain S/P 1,230 -> 0); gate after #212, Vita3K: none
- 2026-11-22T23:24:37Z phase 8: merged #212 (D212; pictures change in 60 Hz windows: baseline rebuild after #215), gating #215, Vita3K: none
- 2026-11-22T23:25:53Z phase 8: filed #223, started #223; #215 gating, Vita3K: none
- 2026-11-22T23:55:19Z phase 8: merged #215 (D213), Vita3K: none
- 2026-11-22T23:57:48Z phase 8: started #213; baseline base-278ebf8 building; hw-session7.sh written (needs the owner yes), Vita3K: none
- 2026-11-23T00:24:06Z phase 8: baseline base-278ebf8 built (title, pause-ws, pause-ws-wobble, wobble60 = base-85a7b2c; demo60 new); owner60/stone60 pinned to dd_budget=1 (auto varies run to run in Vita3K) and rerunning, Vita3K: none
- 2026-11-23T00:34:50Z phase 8: #223 reported done; gate waits for the baseline rerun job, Vita3K: none
- 2026-11-23T00:54:10Z phase 8: base-278ebf8 complete (owner60/stone60 dd_budget=1 = base-85a7b2c); gating #223, Vita3K: none
- 2026-11-23T01:49:13Z phase 8: merged #223 (D214), filed #224, Vita3K: none
- 2026-11-23T01:50:06Z phase 8: started #224, Vita3K: none
- 2026-11-23T02:30:25Z phase 8: #213 committed (ae5cdfe, 53f4ddd) after the stall nudge; #224 checking, Vita3K: none
- 2026-11-23T03:10:52Z phase 8: #224 reported done; gating, Vita3K: none
- 2026-11-23T03:33:15Z phase 8: merged #224 (D215); presentation.md lod text updated, Vita3K: none
- 2026-11-23T03:34:11Z phase 8: hw-session7 VPK rebuilt as autopilot-53a6a5d (with D214, D215 lod text); waits for the owner yes, Vita3K: none
- 2026-11-23T03:38:46Z phase 8: #213 reported done (sky records by name; host gprof sky record -6..-22%); gating, Vita3K: none
- 2026-11-23T03:47:48Z phase 8: merged #213 (D216), Vita3K: none
- 2026-11-23T03:48:37Z phase 8: started #214, Vita3K: none
- 2026-11-23T05:03:11Z phase 8: #214 reported done (host lookup divisions -92..94%, vc.div -41..43%); gating, Vita3K: none
- 2026-11-23T05:13:01Z phase 8: merged #214 (D217), filed #225, Vita3K: none
- 2026-11-23T05:13:53Z phase 8: started #216, Vita3K: none
- 2026-11-23T06:16:43Z phase 8: #216 WIP c675447 (glows and moby shadows hand over their own points); checking, Vita3K: none
- 2026-11-23T07:49:39Z phase 8: #216 WIP a884349 (an unfound glow/shadow polygon's found vertices take their own point too); checking, Vita3K: none
- 2026-11-23T08:20:41Z phase 8: #216 reported done (GlowsAndSparkles 0 on all routes; MobyShadows 0 except S 4/5); gating, Vita3K: none
- 2026-11-23T08:26:57Z phase 8: merged #216 (D218), filed #226, Vita3K: none
- 2026-11-23T08:27:34Z phase 8: started #217, Vita3K: none
- 2026-11-24T10:34:33Z phase 8: merged #222 (D230), filed #244, #245; owner asked to stop the loop after this and open a PR. Vita3K: 9 runs (worker's), hardware: none
- 2026-11-24T10:49:10Z phase 8: owner decided #208 no and #235 no (closed); PR #246 merged into main; loop stays stopped. Vita3K: none, hardware: none
- 2026-11-24T11:17:45Z shared 5, vita 8: resumed under Encore (KIT=../encore); migrated autopilot -> develop, per-target state, source ps1; started #226 (vcache.c holds #219/#225/#230/#231/#237/#243; #244/#245 have no Files:), Vita3K: none, hardware: none
- 2026-11-24T11:49:23Z shared 5, vita 8: #226 WIP 2eacdce (docs), worker busy; pick empty (vcache.c held), Vita3K: none
- 2026-11-24T12:20:13Z shared 5, vita 8: #226 WIP f07763b (moby shadow centre own anchor), checking; Vita3K: none
- 2026-11-24T12:51:14Z shared 5, vita 8: #226 WIP 3069180 (own_nosrc split), checking; Vita3K: none
- 2026-11-24T13:22:13Z shared 5, vita 8: #226 in a long check (PID 45157, 54 min), docs edits; Vita3K: none
- 2026-11-24T13:53:14Z shared 5, vita 8: #226 alive (new vcache.c edits, fresh PID), last commit 12:27; Vita3K: none
- 2026-11-24T14:10:06Z shared 5, vita 8: merged #226 (D244), filed #247, commented #237, started #230; vcache.c still holds the queue (8 issues). Vita3K: 8 runs (worker's, 0 own failures), hardware: none
- 2026-11-24T14:14:43Z shared 5, vita 8: owner said stop; #230's attempt 1 stopped before any commit (worktree and branch removed, no attempt counted, its 4 host containers stopped); loop ended; Vita3K: none, hardware: none
- 2026-11-24T23:13:52Z shared 5, vita 8: resumed (owner: finish wobble and draw distance until no misses); develop fast-forwarded to origin fc5bf71 (#252 PC on Encore D247, #253 docker queue); #251 labelled phase-8 needs-owner (Vita on Encore, out of this loop's scope); started #230 attempt 1 (vcache.c holds the other 8), Vita3K: none, hardware: none
- 2026-11-24T23:49:28Z shared 5, vita 8: #230 alive (base host ctest in its background job; docker queue held by the arm64 session), no commit yet; Vita3K: none, hardware: none
- 2026-11-25T00:20:18Z shared 5, vita 8: #230 alive (tagging runs on head diag binaries, docker queue its own), no commit yet; Vita3K: none, hardware: none
- 2026-11-25T00:51:33Z shared 5, vita 8: #230 alive (tagging run 5 of its head-diag batch, dh-S60), no edits or commit yet at 96 min; Vita3K: none, hardware: none
- 2026-11-25T01:22:35Z shared 5, vita 8: #230 flagged by stall check (2 h 05, no commit) but alive (tagging batch on dh-Si, live PID 31969); nudged for a WIP commit, nothing stopped; Vita3K: none, hardware: none
- 2026-11-25T02:01:19Z shared 5, vita 8: #230 WIP fa63379, 33da221 (portal sky cut fallback place and depths), building fix2 for checks; Vita3K: none, hardware: none
- 2026-11-25T02:32:24Z shared 5, vita 8: #230 checking fix2 diag on host routes (d2-O60 newest), last commit 01:53; Vita3K: none, hardware: none
- 2026-11-25T03:03:24Z shared 5, vita 8: #230 building its FNV base binary (docker queue held by the arm64 session's PC build since 02:38), new vcache.c edits uncommitted, last commit 01:53; Vita3K: none, hardware: none
- 2026-11-25T03:34:23Z shared 5, vita 8: #230 running its per-polygon FNV compare (base vs fix2) on host routes, last commit 01:53; Vita3K: none, hardware: none
- 2026-11-25T04:05:25Z shared 5, vita 8: #230 flagged by stall check (last commit 01:53) but alive in its FNV compare (fnv-S60, clean tree); nudged, nothing stopped; Vita3K: none, hardware: none
- 2026-11-25T04:39:20Z shared 5, vita 8: #230 Vita build and Vita3K routes done on fix2 (run folder fix2, perf reports for owner60/stone60/toasty60), FNV compare finishing (S, P); last commit 01:53; Vita3K: worker's runs
- 2026-11-25T05:01:30Z shared 5, vita 8: #230 reported done (PortalSkyPlain/Fog 0 on host S/P/Pf/S60, stone60 window 6600 missed 38->0, FNV 0 changed, shots identical 8/8 re-checked by coordinator); scripted gate running; Vita3K: 8 runs (worker's, 0 own failures)
- 2026-11-25T05:14:51Z shared 5, vita 8: merged #230 (D248): PortalSkyPlain/Fog 0 on host S/P/Pf/S60, stone60 window 6600 missed 38->0; regression = the worker's 8-route run (code identical to head 29d857a): last_good and vita baseline -> base-29d857a; filed #257 (docs); Vita3K: 8 runs (worker's, 0 own failures), hardware: none
- 2026-11-25T05:15:56Z shared 5, vita 8: started #231 (ParticlesDraw, vcache.c) and #257 (docs); others held on vcache.c; Vita3K: none, hardware: none
- 2026-11-25T05:21:33Z shared 5, vita 8: merged #257 (docs, cut fraction line; host test step skipped: docs only); fa_seed note added to #247; #231 running; Vita3K: none, hardware: none
- 2026-11-25T05:53:11Z shared 5, vita 8: #231 tagging runs on head diag binaries (S, Si, S60, P in one container), no commit yet at ~40 min; Vita3K: none, hardware: none

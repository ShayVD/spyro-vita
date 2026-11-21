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
  "last_good": "63d80f4c28a1c048f5ae84c0fcd582a3149f0fcc",
  "baselines": {
    "run_dir": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/runs/base-63d80f4",
    "commit": "63d80f4c28a1c048f5ae84c0fcd582a3149f0fcc"
  },
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
    }
  ],
  "owner_decisions": [
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
    "n": 10,
    "base_run_dir": "/Users/shayvandam/Projects/spyro-vita/runtime/autopilot/runs/base-63d80f4",
    "base_commit": "63d80f4c28a1c048f5ae84c0fcd582a3149f0fcc",
    "started_at": "2026-11-21T07:05:00Z",
    "filed": 1,
    "investigated": true
  },
  "rounds_without_gain": 1,
  "hardware_pending": [],
  "job": null,
  "batch": null,
  "lanes": [
    8
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

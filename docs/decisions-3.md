# Decisions, part 3

Decisions from D162 on; D1–D102 are in [decisions.md](decisions.md), D103–D161 in [decisions-2.md](decisions-2.md). Record each decision with its date, what was chosen, and why.

### D162. The free camera's right stick holds the passive camera; an option turns the recentring off (issue #154, 2026-11-17)

- **Why:** issue #154: with the passive camera (the default, `D_80075914` 0x52) the free camera (D47/D53) "takes control over your right stick". `UpdateFollow` (in `func_80035FB4`, `src/game/camera_hand.c`) restarts the hold timer `D_80075894` (`RT(90,75)`) only while some input is held, and its test is `g_Pad.m_NoButtonsDown` (`gamepad.c`), which counts the buttons and the left stick, not the right one. So with the right stick alone held the timer ran out after 90 vblanks (2 a tick), `StopSphere()` and the 0x800 azimuth clamp started pulling the camera back behind Spyro against the stick: the turn dropped to about half its speed, and on release the camera swung back at once.
- **Chosen:**
  - **The right stick counts as input** for that timer when the free camera is on (`FreeCameraHeld()`: on, and either right axis off 0x7F). The camera now holds while the stick is held, and after release it holds 90 vblanks (about 1.5 s) before swinging back, as after L2/R2.
  - **`CAMERA RECENTRE` (CONTROLS, `cam_recentre`, default ON, the original)**: OFF makes the springs' azimuth error 0 (`RecentreOff()`, in `func_80034CE8`, after L2/R2's and the free camera's own override, which still win), so the camera keeps its heading while Spyro runs: no swing back behind him, in either camera mode. Only in the ground follow states (`CAM_FOLLOW`, `CAM_SPEED_CHECK`, the two recentre states, 1, 2, 4, and 5 without `m_flyingAbility`); flight levels, cutscenes, fly-ins and other scripted states keep the original's. Elevation and distance are untouched. Host: `--no-cam-recentre`; Vita: `g_port_controls.no_recentre` from `controls_apply()`, gated by `PortControlsLive()` like the other control options.
- **Checked** (the host, `logs/freecam154/`, pad script `pad.txt`: new game, the right stick held right from 7200 to 8100, then left stick up from 8700):
  - `before` (main's binary, `--analog --free-camera`): the timer 90 to 0 by 7320; the turn ~890-945 units per 60 vblanks, then ~460-500; at release it snaps back to 0x400 by 8280.
  - `after`: the timer stays 90 for the whole hold, the turn ~946-975 throughout; at release it holds 90 vblanks, then swings back to 0x400 by 8640.
  - `norecentre` (`--no-cam-recentre`): the heading stays 0xbf9 after release and while Spyro runs (8700-9600); `shot-9300` shows Spyro running away from the kept camera.
  - **Options off:** `tools/host_checks.sh` with main's binary and this one (`logs/fc154-base-*`, `logs/fc154-new-*`): the demos, the whole-game tour and routes A and B give byte-identical traces, shots, audio and saves; an `--analog` run without the free camera (`logs/freecam154/analog-only-*`) is byte-identical too.
  - The VPK: three local builds in the VitaSDK container (amd64, emulated) died of segfaults in gcc and Python on files this change doesn't touch (`logs/freecam154/vita-build*.log`); the PR's CI built both VPKs (run 44000057143, `a641485`).
- **Hardware** (the owner's Vita at 192.168.1.4, 2026-11-17, CI's `spyro-vita.vpk` of `a641485`, `hw_run.sh` with virtual vblanks, `logs/freecam154/hw/`): pad script `hw/pad.txt` (`k-route.pad`'s New Game, the right stick full right 11400-12300, the left stick up 12900-13200), `analog=1 free_camera=1`, no inversion, 23 shots; host references with the same script and flags (`h30`, `h30n`, `h60`, with `--peek`s of the timer, the turn and the camera's heading):
  - `v30` (30 Hz, recentre on): the camera turned to the host's heading at 12240, faces Spyro and holds at 12360, is partway back at 12540 and behind Spyro at 12840, each the host's view (`h30`: the timer 90 through the hold, 30 at 12360, the swing back from 12420).
  - `v30n` (`cam_recentre=0`): at 12840 the camera still faces Spyro (`h30n`: heading 0xbf9 from 12360 on); at 13200 and 13320 Spyro has run on with the camera's heading kept, the castle wall ahead, as on the host.
  - `v60` (`interp=4`, 60 Hz logic, the device's default; the 60 Hz rules build `camera_hand.c` again, D142): at 12240 and 12540 the host's `--tick60` views (`h60`).
  - The shots can't be compared byte for byte: the Vita's are the GPU's picture at 512×256, the host's the software rasterizer's at 512×240. By eye the camera matches in every pair looked at; Spyro's idle pose and Sparx differ a little at 13320 and 13500.
  - All three reached `frames=13800` (162, 156 and 246 s), and the owner's files in `ux0:data/spyro` were put back.
  - Not played by hand on the Vita.

### D163. The shader compiler is found by loading it, from the data folder too, and a screen says when there is none (2026-11-16)

- **Found** (issue #152, mistial-dev, on a PS TV: VTE-1001, 3.65 Ensō, YAMT's mounts, ioPlus): v1.2.1's `spyro-vita.vpk`, the safe app, ran on the software rasterizer with `ur0:data/libshacccg.suprx` installed: 13 ms of raster a vblank, 99% busy, 196 of 300 vblanks late in Beast Makers. The port decided by a stat of the two `ur0:data` places (D35), and on that set-up a safe app gets `0x80010002`, "no such file", for `ur0:data`, from `sceIoGetstat` and from a module load alike (their closed PR #153, whose diagnostic build logged both). Why that device hides `ur0:data` from a safe app isn't known. It isn't every safe app's lot: the owner's Vita has run the safe VPK with `gpu=1` since D35 (`logs/hw-60hz-analog-20261111/spyro.log` in the main checkout, among many).
- **Decision:** `shader_compiler()` in `src/platform/vita/main.c`:
  - **The stat first, as before.** Where it succeeds nothing changes: vitaGL loads the module at its own start.
  - **Else the module is loaded here**, with the libshark vitaGL links (`shark_init(path)`), from the two `ur0:data` places and then from `ux0:data/spyro/libshacccg.suprx`, a copy the player makes where a safe app may read. vitaShaRK's `shark_init` returns at once when the module is loaded, so vitaGL's `start_shader_compiler` (`shark_init(NULL)`, then the `external` path; read in the installed `libvitaGL.a` and `libvitashark.a`) finds the compiler there and loads nothing again.
  - **The allocators are vitaGL's.** vitaGL gives the compiler `vglMalloc` and `vglFree` (`shark_set_allocators`) before its own load, and libshark hands them to the module at the load. A load here without them would leave the compiler on newlib's heap, as PR #153's did. So they are set here first, through two wrappers; the compiler calls them from the first compile on, after `vglInit`.
  - **Not `app0:`**, which PR #153 also tried: only a VPK repacked with Sony's module inside would have it there, and we never ship that.
  - **A screen instead of a silent crawl** (the report's other ask): when `gpu=1` was wanted and nothing loaded, **NO SHADER COMPILER** (`vita_boot_panel`, the disc screens' panel, D60) gives the first place's code, says to install the module and, if it is installed, to copy it to the data folder; X goes on with `gpu=0`, as the README promises a game without the module. `gpu=0` in `config.txt` never shows it. With `frames=` set it leaves by itself after 300 vblanks.
  - **The log** says each place's stat and load code, then `gpu: no shader compiler (libshacccg.suprx) could be loaded: using the software renderer (gpu=0)`, in place of `gpu: no ur0:data/libshacccg.suprx ...`, which was wrong on the PS TV. A load from here logs `gpu: shader compiler loaded from PATH (D163)`.
  - **`shacc_ur0=0`** (`config.txt`, tests): `ur0:data` counts as unreadable, so the other place and the screen can be run on a device that reads `ur0:data`.
  - `tools/vita/hw.game`'s check accepts the data folder's copy too.
- **Numbered D162 when it was built and run:** the free camera's D162 (PR #156) merged first, so the logs below say `(D162)` where the code now says `(D163)`; the merge with it changed nothing else here.
- **Checked in Vita3K** (2026-11-16, the safe VPK of this branch built with the local VitaSDK, `realtime=0`; logs in the worktree's `logs/d162/`, gitignored; Vita3K is not hardware and has no safe app's sandbox):
  - **A, nothing forced** (`a-ur0`, 1,800 vblanks): `renderer: GPU (vitaGL, gpu=1)`, shaders compiled, no new log line.
  - **B, `shacc_ur0=0` with a copy in the data folder** (`b-data`): `gpu: shader compiler loaded from ux0:data/spyro/libshacccg.suprx (D162)`, `renderer: GPU`, `shaders compiled in 56106 ms`, `GPU renderer ready`; its 6 shots are A's byte for byte (the readback replay).
  - **C, `shacc_ur0=0` with no copy:** the screen is up and stays without a press (`c3-press/win-grab.png`, 12 of the panel's 13 lines; `c1-panel`, 30 s); X (Vita3K's key, sent to its window) goes on into the logos on the software renderer (`c3-press/c3-after.png`); with `frames=1800` it leaves after 300 vblanks and the run is `gpu=0`, its 6 shots A's byte for byte (`c2-soft`). The load of a file that isn't there is `80010002` in Vita3K too.
- **Checked on hardware** (2026-11-17, the owner's Vita, the safe VPK of `29bc908`, eboot SHA-1 `737e29b4…`, `tools/vita/hw_run.sh` with `realtime=0`; `logs/d162/hw-*`, the three runs and the copy in `hw-run-all.sh`). This Vita's safe app reads `ur0:data`, so B and C are by `shacc_ur0=0`:
  - **A, nothing forced** (`hw-a`, the title route, 2,700 vblanks): `renderer: GPU (vitaGL, gpu=1)`, `shaders compiled in 9940 ms`, no new log line.
  - **B, `shacc_ur0=0` with the Vita's own module copied to `ux0:data/spyro/`** (3,407,920 bytes, `hw-b`): `gpu: shader compiler loaded from ux0:data/spyro/libshacccg.suprx (D162)`, `renderer: GPU`, `shaders compiled in 9974 ms`, `GPU renderer ready`. A safe app does load a module from the data folder, and the compiler works on vitaGL's allocators set before the load. Its 5 shots and its 5 GPU pictures (`shot-V-hd.png`, 960x544) are A's byte for byte.
  - **C, `shacc_ur0=0` with no copy** (`hw-c`): `ux0:data/spyro/libshacccg.suprx: load 80010002`, the screen left after 300 vblanks (`frames=`), `renderer: software (gpu=0)`, 5 shots A's byte for byte, at 20.7 vblanks a second (`realtime=0`) where A's GPU run does 100 and more: what the screen warns of.
  - The copy was removed afterwards, and the owner's `config.txt`, `save/` and log were put back by each run.
- **The merged build** (2026-11-17, `1fb0ced`, with main's D162; Vita3K, `logs/d162/m-a`, `m-b`, `m-c`): A, B and C again, the log lines as above with `(D163)`, and all 18 shots A's of before the merge byte for byte. Not run on hardware again: the merge brought the free camera's changes and this decision's number, nothing in the compiler's path.
- **Not checked:**
  - **The PS TV of the report**, the only device known to show the fault: its owner is asked to run the release that carries this.
  - **X on the screen, on hardware:** the runs were scripted, so the screen left by `frames=`. The press was run in Vita3K (above); the wait is the disc screens' loop (D60), moved into `panel_wait`.
  - **A device with no module at all** (the `ur0:data` lines of the log and the screen's code from a stat): neither Vita3K's nor the Vita's module was moved away; C runs the same code from the third place on.
  - **The unsafe VPK:** built by CI only.

## 2026-11-19 — the presenter's frames in the vertex cache (#158)

### D164. With `persp`/`subpixel` on, the presenter's frames record their own projections into a table of their own and look up only those, and every polygon's answer goes to `g_vcache_side` at its index in its own walk's vertex buffer. Before, at 60 fps every in-between frame found none of its vertices, and on the Vita neither kind of frame got its answers at all
- **Why** (issue #158): since D129 the presenter draws every frame the logic draw doesn't, but `pres_draw.c`'s context set its projection hooks to NULL, so it recorded nothing, and its polygons were looked up (`psx_vcache_poly`) in the logic draw's table between two logic frames. D39's remedy for the wobble (D89) was then missing from every other presented frame at 60 fps.
- **Measured first** (commit `541d498`: counters only, no behaviour change). `VCacheStats` is now kept per kind of frame (`g_vcache_stats[VC_LOGIC]`, `[VC_PRES]`): the presenter's frame runs from `psx_vcache_pres_begin` (`pres_draw.c`) to its `DrawOTag`'s done hook. New counters: `other`, vertices found in an entry the other kind of frame's projections made (each entry keeps its maker), and `side_lost`, polygon vertices whose answer couldn't be written to `g_vcache_side` because they weren't in the vertex buffer the renderer had last. The host's `--vcache` report adds `vcache logic:` and `vcache presenter:` lines (the old `vcache:` line, which `tools/native_draw_ab.sh` reads, is both added); the Vita's log adds `vcache N logic:` and `vcache N pres:` lines.
- **Host, before** (`build/spyro-c1`, `--widescreen --interp --vcache`; `logs/m158-c1-*` in this worktree; per frame):

  | Run | Logic frames: polygons found whole / vertices found | Presenter frames: polygons / vertices | Presenter's vertices found in the logic draw's entries |
  |---|---|---|---|
  | gems pad, 8,768 vblanks | 97.2% / 98.9% (4,553 frames) | **0.0% / 0.0%** (4,543 frames; 4,384 vertices a frame with no entry) | 399 in the whole run |
  | intro cutscene (new game to vblank 10,300) | 93.4% / 97.7% (5,319) | **0.0% / 0.0%** (5,309; 5,017 no entry) | 1,337 |
  | attract demos, 12,000 vblanks | 97.9% / 99.3% (6,169) | **0.0% / 0.0%** (8,136, the demos' 16:9 frames among them; 4,865 no entry) | 913 |

  So the problem is real, as the issue's first case: the in-between frames are drawn integer and affine, the PS1's wobble on every other picture. The second case (a vertex picking up a logic entry's offset and depth) is rare on the host: the update projects almost nothing between the logic draw's done hook and the presenter (~0.1 vertex a frame, 0.00–0.01% of the presenter's vertices). The logic frames were barely touched (the same counters as at 30 fps, `logs/m158-c1-gems-30`, `-intro-30`, but for the peak's one entry the presenter's done hook took).
- **Vita3K, before** (VPK of `541d498`, SHA-1 `1b98823e…`, `realtime=0`, the pause route's pad with `config_version=3 widescreen=1 interp=1 persp=1 subpixel=1 frames=8000`; `$RUNS/auto-158-1-c1/wobble60`): the presenter's frames 0% found, as on the host, and **both kinds of frame 100% `lost`** from the first 60 fps window on (56% in the window where it starts). The cause: `g_vcache_side` was indexed from `s_base`, the buffer the renderer's `GR_UpdateVertexBuffer` had last (`psx_vcache_set_base`), and since D125 the presenter walks into a vertex buffer of its own (`present.c` `s_pw`). With the two walks taking turns, every lookup found the other walk's buffer there, wrote nothing, and the renderer read answers left from the last 30 fps frame: stale sub-pixel offsets on every vertex and stale depths for `persp`, in the logic frames too. The host can't show this: its renderer reads no answers and never sets a base.
- **The fix** (`c74914c`; `src/platform/common/vcache.c`, `psx_vcache.h`, `src/game/pres/pres_draw.c`):
  - Each kind of frame has its own table (`Rec`: table, generation, count). The logic draw's is as before (with D71's second table); the presenter's (360 KB) is allocated at its first frame with the cache recording.
  - `psx_vcache_pres_begin(walks)`, called by `pres_draw.c`'s context after `records_begin()` (the game's walk, which looks up the logic draw's table, is joined first), starts the presenter's frame (a new generation of its table) and returns the hook its renderers record through (`g_pres_rtp_value_hook`; `render_ctx.c`'s `pres_ngte_vcache` calls it from the presenter's native RTPS). Until its `DrawOTag`'s done hook, `psx_vcache_poly` looks up the presenter's table; the done hook then ends the presenter's frame and leaves the logic draw's table alone. With the GPU disabled (`walks` 0) its `DrawOTag` neither walks nor calls the done hook, so its frame isn't opened. `g_pres_rtp_hook` (the register-file hook) stays NULL: the readable renderers have no register file (D124).
  - The vertex buffers the renderer is given are remembered (up to four), and a polygon's answer is written at its index in whichever of them holds its vertices. A buffer seen the first time gets "not found" answers (integer position, w 1) for that one flush, since its polygons' answers couldn't be written.
  - With `tex_filter` only (polygons marked, nothing recorded) or the table's allocation failing, the presenter falls back to the old behaviour. The cache off (the default) runs none of this: `psx_vcache_pres_begin` returns NULL at once.
  - The host's `--vcache` refuses `--pres-check-thread` and `--pres-check-late`: the presenter must run inline for the cache, as it does on the Vita (D126; `gpu_thread.c` `pb_usable`).
- **Host, after** (`build/spyro-c2`, `logs/m158-c2-*`):

  | Run | Logic frames | Presenter frames | Found in the other's entries |
  |---|---|---|---|
  | gems | 97.2% / 98.9% | **97.2% / 98.9%** (2,973 projections a frame, 40 no entry) | 0 / 0 |
  | intro cutscene | 93.4% / 97.7% | **93.4% / 97.7%** (3,794, 61 no entry) | 0 / 0 |
  | attract demos | 97.9% / 99.3% | **96.1% / 98.4%** (3,668, 50 no entry) | 0 / 0 |

  The logic frames' counters are now exactly the 30 fps runs' (peaks included). The table's peak is 4,521 entries (the demos' 16:9 presenter frames), under its 6,144 limit.
- **Vita3K, after** (VPK of `c74914c`, SHA-1 `c2ea5179…`, the same route, `$RUNS/auto-158-1/wobble60-r2`): in Artisans (windows 7,200–7,800) the logic frames 94–95% of polygons and 97–98% of vertices found, **the presenter's 94–95% and 97–98%**, 150 presenter frames in each window, **0% lost** for both. In the title windows of the first try (`wobble60`, which Vita3K ended at vblank 3,000; below) both 96–97% and 98%. The `present` line's in-between frame cost on the game thread went from 2.4 to 6.3 ms a tick there (Vita3K timing, not evidence; D89 measured the cache's cost on hardware).
- **Logic untouched:** the attract demos' trace with `--widescreen --interp --vcache` is identical to the f7aeb43 binary's without the cache (`logs/m158-c2-demo-vi/trace.tsv` against `logs/m158-base-wsi/trace.tsv`, 12,000 vblanks), and so are the gems and intro traces and the gems run's memory card (`m158-c2-gems-i`, `-intro-i` against `m158-base-gems-off`, `-intro-off`). The instrumented build's demo trace is too (`m158-c1-demo-vi`).
- **The cache off is unchanged:**
  - Host, against a binary of f7aeb43 built from a clean copy (`build/base-src`): the attract demos at 16:9 and 60 fps (`--out960`) 25/25 files, gems at 16:9 and 60 fps 37/37, the intro to vblank 10,300 at 16:9 and 60 fps (a shot every 100 vblanks) 105/105, traces, shots and cards byte-identical (`logs/m158-base-*` against `logs/m158-c2-*`).
  - `--pres-check` with `--widescreen --interp`: gems 4,553 frames compared, 0 differ; the intro 5,319 compared, 0 differ (`logs/m158-c2-pchk-*`).
  - `tools/native_draw_ab.sh d158 demo gems vc` with the new binary (`logs/d158-ndab.txt`): demo 26/26, gems 19/19, vc 19/19 files identical, every native draw check 0 differ, `vc` finding 98.9% of vertices. The `vc` run first printed the script's "NO VERTICES FOUND": its grep for `vertices found 0.0%` matched the new presenter line of a run without `--interp` (no presenter frames). The host now prints a kind's lines only when it had frames, and the rerun (`logs/d158b-ndab.txt`, `vc` 19/19) has no such line.
  - Vita3K against the coordinator's f7aeb43 runs (`$RUNS/base-f7aeb43`), `shots_cmp.py --max-diff 0 --tol 0 --edge 0`: `title` 10/10 same; `pause-ws` 91/91 same (the rerun `pause-ws-r2`; Vita3K ended the first try after vblank 6,900, its 69 shots the same). `pause-ws-wobble` runs at 30 fps (the route doesn't turn on `interp`), so the presenter isn't in it: its shots are the same as the baseline's, 65/65 up to where Vita3K ended it (6,500 in the first try, 6,900 in the rerun; the baseline's two runs ended at 6,500 and 7,900).
- **Vita3K crashes:** `pause-ws-wobble` ends early in every run, the baseline's included, and `pause-ws` did once with the cache off, all with the same signature in `vita3k.txt` (`Unhandled EXC_BAD_ACCESS` at the same Vita3K addresses, "Unimplemented command opcode 104"), so they are Vita3K's, not this change's. A route that turns on `interp` with the cache would show this fix in pictures; the routes don't have one.
- **Not done here:** the presenter on its own thread with the cache on (D126 keeps it inline; the presenter's table and the side array would need to be its own thread's, a follow-up). Hardware: not run (it needs the owner's yes); D89's cost of the cache (~4.5 ms a vblank on the game thread at 60 fps) now applies to the in-between frames' projections too, which hardware has to measure.

- **Later (D172):** merged with main's D152, which also fixed the in-between frame's answers: the walk now says where each polygon's answers go (D152), and this table gives the in-between frame its projections.

## 2026-11-19 — work counters for phase 8 (#160)

### D165. The Vita logs a `vpk-perfcount:` line every 300 vblanks: vita-port-kit's `vita/runtime/perfcount` (vendored), 22 counters of draws, triangles, uploads, state changes, OT packets, the vertex cache's work and the presenter's frames. Every counter is a plain add where the work happens, pulled into the kit's counters once a vblank; no hot path has an atomic. Pictures unchanged
- **Why** (issue #160): phase 8 of the port autopilot measures work with counters, read by the kit's `vita/tools/perf_report.py`, since Vita3K's timing is not evidence. The log had `time` lines (busy ms) but no counter lines, so `autopilot/config.json` had no benchmark scenes and no merge could be shown to reduce work.
- **The kit:** `vita/runtime/perfcount` added to `third_party/vita-port-kit` by the kit's `tools/vendor.sh` (all seven paths re-vendored from kit `31295be7`; it was `4c46886b`). The other paths changed with it: `vita/tools` (new: `perf_report.py`, `shots_cmp.py`, `v3k_run.sh`, `embed_pad.py`, `fnprof.py`, `launch_token.py`, `livearea_png.py`, `present_trace.py`, `psp2dmp.py`, `v3k_window.swift` and their tests; changed: `hw_run.sh`, `hw_check.sh`, `hw_ftp.py`, `hw_mock.py`, `vpk_pack.py`, `hw.game.example`, README), `vita/cmake/VpkApp.cmake` (optional `SCE_HEADROOM` and `UNSAFE`, both off unless asked: this build passes neither), `vita/runtime/discimage` (`vpk_disc_open_source`, `vpk_disc_find_image`, `vpk_disc_boot_serial`, additions; `vpk_disc_open` closes an open disc first) and READMEs. `audioout`, `touch` and `modmenu`'s code are unchanged. `.gitignore`'s unanchored `runtime/` matches the new folder, so its files were added with `git add -f` (the other runtime folders are tracked the same way).
- **The counters** (`src/platform/vita/perfcount.c`, `vita_pc.h`; names stable, runs compare by name):
  - `gl.draws`, `gl.tris` (glDrawArrays and its triangles), `gr.draws`, `gr.tris` (the game's draws, `GR_DrawTriangles`, before joining), `gl.clears`, `tex.uploads`, `tex.bytes` (glTexImage2D with data), `vbo.uploads`, `vbo.bytes` (glBufferData), `gl.reads` (glReadPixels), `gl.state` (glUseProgram, glBindTexture, glBindFramebuffer, glEnable/glDisable, blend, scissor, viewport, texture parameters), with `gl.programs`, `gl.tex_binds`, `gl.fbo_binds` apart, `gl.uniforms`, `gl.attribs` (vertex attributes and buffer binds): `gpu_gl.c` includes `vita_pc.h` with `VITA_PC_GL`, which makes each of those GL calls a macro of the same call with a plain add before it into `g_vpc_gl[]`. Only the thread that holds the GL makes them (as `gpu_gl.c`'s own `s_st`).
  - `ot.packets`: packets walked by `psx_gpu_walk_in` (`gpu_walk.c`), counted in a local and added once a walk to `g_psx_walk_packets[own]` (the game's walk, the presenter's walk: one writer each).
  - `vc.proj`, `vc.polys`, `vc.verts`, `vc.misses` (`vcache.c`'s `g_vcache_work[]`, plain adds on the game thread, never reset: `g_vcache_stats` is reset by the render thread's report, so it can't be pulled exactly). All 0 with `persp`/`subpixel` off.
  - `pres.frames`: `g_present_stats.interp_frames`, the presenter's in-between frames.
  - `vita_pc_vblank` (`vblank.c`, every vblank on the game thread) pulls each running total's increase into the kit (`vpk_pc_add`, 22 atomic adds a vblank) and calls `vpk_pc_frame(vblank)`; `vita_exit` flushes the last part window after the render thread stops. The lines are always on (no option): the counting is an add per event.
- **A first version** counted each GL call with the kit's `vpk_pc_add` (a relaxed atomic add) on the render thread and the walk with an atomic add a walk. Its pause runs in Vita3K all ended early (3 of 3, Vita3K's `EXC_BAD_ACCESS`, issue #161). The final version's pause runs passed 2 of 3. Variants built to look for a cause (no GL counting, the old kit, the new kit alone) passed some runs and failed others, and the baseline's VPK passed 3 and failed 1 (`base-pause-ws-x4`, while another project's Vita3K was running on the Mac from 15:21). So the runs don't show a cause. The plain adds are cheaper anyway, and the crash is #161's to investigate.
- **Checked:**
  - VPK (native VitaSDK) builds; the kit's `perfcount_test` passes on the Mac.
  - Host (linux32, the default and `SPYRO_NATIVE_MEM` builds) against a clean copy of `d0ca29b` (`build/base-src`): `tools/native_draw_ab.sh` `demo` 26/26 and `wsi` 25/25 files identical (`N0_FROM=b160 MODE=plain`, `logs/n160-ndab.txt`); ctest 29/29 (`logs/n160-ctest.txt`). `SPYRO_NATIVE_MEM` builds, the attract demos for 12,000 vblanks at 16:9 and 60 fps (`--widescreen --interp --out960`), once with `--vcache` and once with `--pres-check`: 26/26 files each (trace, shots, WAV, cards) identical to the base binary's (`logs/b160-nat-*` against `logs/n160-nat-*`); `--pres-check` 6,169 frames compared, 0 differ. The gems pad (`logs/pads/gems.txt`) isn't in a fresh worktree, so `native_draw_ab.sh`'s `gems` and `vc` scenarios couldn't run.
  - Vita3K (`$RUNS/auto-160-1`): VPK SHA-1 `358815ec…`, against the coordinator's `base-0525e1f` with `shots_cmp.py --max-diff 0 --tol 0 --edge 0`: `title` 10/10 same (`title`); `pause-ws` 91/91 same, twice (`pause-ws-c1`, `pause-ws-c3`; `pause-ws-c2` ended at vblank 6,900 in Vita3K, its 67 shots the same); `wobble60` passed on its second try (`wobble60-r2`; the first ended at 8,100) with 71 shots the same as the baseline's, 2 over (3,300 and 6,600) and 8 with no baseline shot (the baseline's run ended at 7,300). Those differences aren't this change's: the route isn't deterministic in Vita3K. `interp_budget=1` (the default) skips in-between frames by measured time, so a run on a loaded Mac draws fewer (the first try 709 skipped, `wobble60-r2` 127, the baseline's two runs 161 and 94) and shows other pictures. The baseline's own two runs differ at 800 and 3,300.
  - Counters, `perf_report.py` over vblanks 6,600–9,000: `pause-ws` per frame: `gl.draws` 41.9, `gl.tris` 1,151, `gr.draws` 46.3, `gr.tris` 1,141, `gl.state` 158.5 (`gl.programs` 52.6, `gl.tex_binds` 21.8, `gl.fbo_binds` 1.0), `gl.uniforms` 93.4, `gl.attribs` 226.0, `gl.clears` 0.5, `tex.uploads` 0.011 (`tex.bytes` 11,360), `vbo.uploads` 1.0 (`vbo.bytes` 95,884), `ot.packets` 231.9, `vc.*` and `pres.frames` 0. Its two full runs compare with every counter the same, exactly (exit 0). `wobble60-r2`: `gl.draws` 83.7, `gl.tris` 2,299, `gr.draws` 92.5, `gl.state` 316.4, `gl.attribs` 421.5, `vbo.bytes` 273,643, `ot.packets` 463.3, `vc.proj` 3,566, `vc.polys` 1,535, `vc.verts` 5,293, `vc.misses` 105.0, `pres.frames` 0.498. Two `wobble60` runs don't compare (+6 to +50% over vblanks 600–8,100, `pres.frames` 0.444 against 0.538) for the budget's reason above: the scene needs `interp_budget=0` in its route to be a benchmark (left to the coordinator: this change edits only `config.json`'s `benchmark`).
- **Not done:** hardware (needs the owner's yes). The counters say nothing about time: phase 8's FPS comes from the owner's hardware runs.

### D166 (issue #161). The pause routes' Vita3K crash is Vita3K's render thread running `renderer::Command`s that were overwritten in the game's VDM ring: with a 1 MB ring (`config.txt` `vdm_ring=1024`, new, off by default) `wobble60` passed 5 of 5 against 2 of 5 for the same build without it, and a 64 KB ring crashed sooner. Vita3K routes pass `vdm_ring=1024`; the game and hardware are unchanged
- **Where it faults.** macOS writes a crash report for Vita3K (`~/Library/Logs/DiagnosticReports/Vita3K-*.ips`, at most 25 kept). Symbolised against Vita3K v0.2.1 4232-92bda548's own symbols (`nm` of the binary, offsets from the report), every report from a spyro run is on Vita3K's render thread inside `process_batch` (`renderer/src/batch.cpp`, the loop in `start_render_thread`): a command handler called with a corrupt command (`cmd_handle_create_context` = opcode 0, a zeroed command; `cmd_handle_set_screen_filter`, which the game never sends; pointer-authentication failures on loaded pointers), a jump through a garbage `free_func` (fault at 0x3c, 0x88), `free()` of a bad pointer (`free_tiny_botch`), and MoltenVK losing the device (`VK_ERROR_DEVICE_LOST`, an uncaught `vk::DeviceLostError`). `vita3k.txt` often has `Unimplemented command opcode N` with N 32, 48, 60, 64, 104, 216, 224 just before the end. The `Unhandled access to 0x0` (0x8, 0x10001, 0x3150000000000000 …) line is the host address. `Unhandled write protected region was valid. Address=0x606A5018` (and 0x62EE4000, 0x630E38D0, 0x643D1970) is in every run, passing or not: Vita3K's handler unprotecting a 16 KB host page that a guest write hit (`mem.cpp`, "HACK: keep going"). It isn't the crash.
- **Where Vita3K keeps those commands.** For an immediate context, Vita3K places its `renderer::Command`s (56 bytes, with host `next` and argument pointers) in the guest's VDM ring buffer, and its `SceGxmContext` C++ object in the guest's context host memory (`SceGxm.cpp`, `allocate_new_command`, `sceGxmCreateContext`). So a guest write there would corrupt Vita3K's own state. A diagnostic build (`$RUNS/auto-161-1/vpk/diag1.vpk`, not committed) logged vitaGL's addresses: the VDM ring is 0x60001000–0x60021000 at the start of vitaGL's RAM pool (block 0x60000000, 112 MB), followed by its vertex and fragment rings; the context host memory is a newlib `malloc` chunk at 0x883CB6B0 (2,048 bytes); the atlases are at 0x60EE59B0 and 0x61EE59C0 (16 MB each). Its checks, in 5 runs: the context chunk's header and the next chunk's never changed, and no atlas write (`ablock`, the only place the game writes into vitaGL's memory) went past an atlas's end. Nothing the game writes is next to the VDM ring.
- **Runs** (`$RUNS/auto-161-1/<label>`, the pause pad, `results.txt`; `$RUNS/auto-161-1-v3kprocs.txt` samples every Vita3K process every 5 s). Exit 0 passed, 2 Vita3K crashed; "last" is the last vblank logged. atv2-vita's Vita3K was running during all 26 runs (from 16:25 to 17:45 it never stopped).

  | Run | VPK, settings | Exit | Last | Fault (host address) |
  |---|---|---|---|---|
  | vpk-0525-1 | `autopilot-0525e1f`, wobble60 | 2 | 6900 | 0x10001 |
  | vpk-cfe-1 | `autopilot-cfe0d45`, wobble60 | 2 | 3300 | 0x0 |
  | vpk-0525-2 | 0525e1f, wobble60 | 2 | 8100 | 0x39B37 |
  | vpk-cfe-2 | cfe0d45, wobble60 | 2 | 3300 | 0x0 |
  | vpk-0525-3 | 0525e1f, wobble60 | 2 | 3300 | 0x3150000000000000 |
  | vpk-cfe-3 | cfe0d45, wobble60 | 2 | 3600 | 0x8 |
  | vpk-0525-4 | 0525e1f, wobble60 | 0 | 9100 | |
  | vpk-cfe-4 | cfe0d45, wobble60 | 2 | 3900 | 0x8 |
  | vpk-0525-5 | 0525e1f, wobble60 | 2 | 3000 | 0x8 |
  | vpk-cfe-5 | cfe0d45, wobble60 | 2 | 3000 | 0x8 |
  | diag-w60-1…5 | diag1 (this branch + the checks), wobble60 | 0,0,0,2,0 | 9100 ×4, 6300 | 0x0 |
  | clean-w60-1…5 | this branch (52fbc0b) built here, wobble60 | 0,2,2,2,2 | 9100, 4500, 6300, 7800, 3300 | 0x0, 0x0, 0x6F10000000000000, 0x0 |
  | set-persp-1, -2 | cfe0d45, pause-ws + `persp=1` | 2, 2 | 6900, 4201 | 0x0, 0x0 |
  | set-subpx-1, -2 | cfe0d45, pause-ws + `subpixel=1` | 2, 2 | 7200, 6300 | 0x8, 0x8 |
  | set-i60-1, -2 | cfe0d45, `config_version=3 widescreen=1 interp=1 interp_budget=0`, no cache | 2, 2 | 3300, 3900 | 0x8, 0x8 |
- **#160 and the crash rate.** 0525e1f against cfe0d45 on wobble60, interleaved: 1 of 5 against 0 of 5 passed; with the coordinator's earlier runs 3 of 9 against 0 of 12. A clean build of this branch (cfe0d45's code, built in this worktree) passed 1 of 5. That doesn't show #160 raised the rate: 1 in 5 or less is what every build did today, and #160's GL counting is plain adds before the same calls (no argument evaluated twice has a side effect).
- **The settings.** Every setting crashed today: `persp=1` alone 0/2, `subpixel=1` alone 0/2, 60 fps without the cache 0/2, wobble60 2/20 (0525e1f, cfe0d45 and the clean build). `tex_filter` is 0 in all the routes. On 2026-11-19 before 13:23, when no other Vita3K ran (atv2-vita's first run of the day started then), the routes crashed 6 of 12: `title` 0/2, `pause-ws` 1/3, `pause-ws-wobble` 4/4, `wobble60` 1/3 (`$RUNS/base-f7aeb43`, `auto-158-1*`). So it isn't caused by the other Vita3K. Two more solo runs were planned; atv2-vita's Vita3K didn't stop in 30 minutes of polling (17:30–18:00), so they weren't made.
- **Attempt 1 left open** (answered below): the diagnostic build passed 4 of 5, where the runs without its checks passed 2 of 21 in the same hour and a half. Its checks only read memory and log, so this suggests the crash depends on the guest's code or data layout, or timing, not settings; it is not explained yet.
- **Attempt 2: diag1's lead was noise.** diag1, a clean build of the same code (`clean52.vpk`, the same file attempt 1 ran) and the VDM probe were run in turn on `wobble60`, five rounds (`$RUNS/auto-161-2/<label>`, `results.txt`; each run's `ps-before.txt`/`ps-after.txt` list the other Vita3K processes). atv2-vita's Vita3K was running during every run but one (`vdm64-2`).

  | Build | Runs (exit; last vblank) | Passed |
  |---|---|---|
  | diag1 (`$RUNS/auto-161-1/vpk/diag1.vpk`) | diag-1…5: 2, 2, 0, 2, 2; 3900, 4501, 9100, 6900, 3000 | 1 of 5 |
  | clean52 (`$RUNS/auto-161-1/vpk/clean52.vpk`) | clean-1…5: 2, 2, 0, 2, 0; 8100, 3600, 9100, 3900, 9100 | 2 of 5 |
  | vdm.vpk with `vdm_ring=1024` (this branch, `fd5f624`; SHA-1 `3cd12b04…`) | vdm-1…5: 0 ×5; 9100 ×5 | **5 of 5** |
  | vdm.vpk with `vdm_ring=64` | vdm64-1, -2: 2, 2; 3000, **1200** | 0 of 2 |

  With attempt 1's runs, diag1 passed 5 of 10 and the builds without its checks 4 of 26 (`wobble60` and attempt 1's single-setting runs). That is no reason to look into its layout: it was luck, and the patch wasn't bisected. Every build without a bigger ring crashes about four runs in five; at a pass rate of 0.3 or less, five passes in a row by chance is under 1 in 400 (0.3^5).
- **The cause.** Vita3K keeps an immediate context's commands in the game's VDM ring (above). Its render thread runs them later, so a game that gets far enough ahead writes the next commands over ones still waiting, and the render thread then runs half-written commands: an opcode 0 command, `set_screen_filter`, junk `free_func`s, as in the crash reports. A ring 8 times bigger (1 MB) passed every run; half the size (64 KB) crashed both runs, one at vblank 1,200 (the earliest crash in this issue's runs). That fits all the earlier observations: `wobble60` draws twice as much as `pause-ws` (D165: `gl.draws` 83.7 against 41.9 a frame, about twice the commands), so it fills the ring twice as fast and crashed most; and the busier the Mac, the further Vita3K's render thread falls behind. Vita3K's source wasn't read (only its binary is here), so how it guards the ring is inferred from what happens, not from its code. A different ring size also moves everything after the ring in vitaGL's RAM pool (the vertex and fragment rings, the atlases); the bigger and the smaller ring both move it, the opposite way from each other, and only the bigger one passed, so the ring's size explains the results and the layout doesn't.
- **The change** (`src/platform/vita/gpu_gl.c`, `fd5f624`): `gl_gpu_init` reads a `vdm_ring=KB` line from `config.txt` itself (`vdm_ring_kb`) and calls `vglSetVDMBufferSize` before `vglInitExtended`. No line or 0 leaves vitaGL's default (GXM's 128 KB), so a build without the line is exactly as before; the value is capped at 16 MB and rounded down to 4 KB. It is read in `gpu_gl.c` because the ring has to be sized before `vglInit*` and `main.c`'s options are outside this change, so `main.c` logs `config: unknown key vdm_ring` (harmless). Off by default: on hardware a bigger ring only costs RAM (896 KB more of vitaGL's 200 MB pool at 1 MB) and changes no picture, but no hardware run has had it, and D89's hardware runs with `subpixel=1` didn't crash with the default ring.
- **Checked** (Vita3K, `vdm_ring=1024`, `$RUNS/auto-161-2`): `wobble60` 5 of 5 (`vdm-1`…`vdm-5`), each 91 of 91 shots the same as `base-0525e1f/wobble60` (`shots_cmp.py --max-diff 0 --tol 0 --edge 0`); `title` 10 of 10 the same as `base-cfe0d45/title` (`gate-title`); `pause-ws` 91 of 91 (`gate-pause-ws`); `pause-ws-wobble` 91 of 91 (`gate-pause-ws-wobble`), all exit 0. 20 Vita3K runs in all. Hardware: not run (needs the owner's yes).
- **What the gates should do.** Every Vita3K route gets `vdm_ring=1024` in its config lines (`autopilot/config.json`'s routes: the coordinator's file), so a Vita3K crash means something again. A crash with the line on is news: run the base VPK on the same route with the line before blaming a change, as before. Leave the default alone until a hardware run shows a bigger ring costs nothing there.

## 2026-11-19 — the presenter on its own core with the vertex cache on (#162)

### D167. With `persp`/`subpixel` on, the presenter's in-between frame runs on its own thread (`pres_thread=1`) like every other frame. It projects into its own table and writes its answers to a side array of its own, which its buffer carries to the render thread. The game thread's share of the vertex cache's work halves at 60 fps, and every picture is unchanged
- **Why** (issue #162): D126 kept the presenter inline whenever the vertex cache was on (`gpu_thread.c` `pb_usable`: `!gl_vcache_side_used() && !psx_vcache_polys`), because the cache's tables and `g_vcache_side` belonged to the game thread. Since D164 the presenter has its own table, but three things were still shared:
  - `s_pres`, the global flag that says "the presenter's frame is open", which chooses the table, the counters and the stats for every polygon;
  - `g_vcache_side`, which both walks write at their own indices;
  - `g_vcache_work`, the D165 counters.
  So since D129 the wobble fix put the whole in-between frame back on the game thread, on every frame at 60 fps.
- **The change** (`src/platform/common/vcache.c`, `psx_vcache.h`, `src/platform/vita/gpu_thread.c`, `gpu_gl.c`; comments in `src/game/pres/pres_draw.c`):
  - **Handing over:** `pres_draw.c`'s context still opens the presenter's frame on the game thread (`psx_vcache_pres_begin`). On the worker path, `pres_submit`, which places the job's `C_BUF` before the logic draw starts, then calls `psx_vcache_pres_detach`: `s_pres` goes back to 0, so the logic draw's lookups and its done hook behave as without a presenter, and `s_pres_async` is set. While `s_pres_async` is set, `psx_vcache_poly` treats a polygon whose vertices are outside the game's walk's buffer (`psx_gpu_game_walk()->vb`, the only buffer the logic draw walks into) as the presenter's. It looks the polygon up in the presenter's table and adds to the presenter's stats. The game-buffer test comes first, so the logic draw's walk never reads `s_pres_async`. The worker ends the frame in `pb_close` (`psx_vcache_pres_end`: frames, peak), because its `DrawOTag` doesn't call the done hook (`present.c`).
  - **Its answers:** they go to `s_pres_side` (a 512 KB malloc) at the vertex's index in the presenter's walk's buffer. The cache learns that buffer from `psx_vcache_set_base` during a presenter frame drawn inline, so the first presenter frame with the cache on still runs inline; `psx_vcache_pres_async_ok` gates the thread on it. `pb_verts` copies the answers into the job's buffer beside its vertices (`PBuf.side`, 3 × 512 KB, allocated the first time the cache asks for the worker). `C_PVERTS` carries a `side` flag, and the render thread passes that area to `gl_vertices` in place of `g_vcache_side`. The side area follows the same reuse rule as the vertices: job k's buffer is reused once job k-2 has finished.
  - **The counters:** `g_vcache_work` (D165's `vc.*`) is now the game thread's work (with its walk: the logic draw, and the presenter when inline). `g_vcache_pres_work` is the presenter's thread's. That keeps one writer per counter, with plain adds. The Vita log gains a `vcache N work:` line that gives both since the last report.
  - **What the presenter's thread touches:** its table (`s_rec[VC_PRES]`, renewed by `psx_vcache_pres_begin` on the game thread after the join), `s_pres_side`, `g_vcache_pres_work`, `g_vcache_stats[VC_PRES]` and its own buffer. None of these is written by the game thread or the parse thread while it runs. The render thread's report resets `g_vcache_stats` while either thread adds to it; that was already so for the logic draw's walk on the parse thread, and it is statistics only.
  - **Unchanged:** with `tex_filter` alone (polygons only marked, nothing recorded) the presenter stays inline. `pres_thread=0` keeps it on the game thread with the cache, exactly as before (checked below). The host runs the presenter inline, through the same `s_pres` path as before. With the cache off, `pb_usable` is D126's.
- **Vita3K** (`realtime=0`; VPK of `2fa01a0`, SHA-1 `fb0a0d7d…`, `$RUNS/auto-162-1/br1.vpk`; runs in `$RUNS/auto-162-1/<route>`, `results.txt`). Every route has `vdm_ring=1024` and every run exited 0 on its first try. Compared with `shots_cmp.py --max-diff 0 --tol 0 --edge 0` against `$RUNS/base-5eb8423`:
  - `title`: 10 of 10 the same;
  - `pause-ws`: 91 of 91;
  - `pause-ws-wobble`: 91 of 91;
  - `wobble60`: 91 of 91, and again 91 of 91 in a second run (`wobble60-again`);
  - `wobble60` with `pres_thread=0` (`wobble60-pt0`): 91 of 91, with every `vc.*` counter exactly the baseline's.
- **The presenter's frames on its thread** (`wobble60`, the `pres` lines): from the 600 window on, 81 frames on core 1 and 1 inline (the first, which teaches the cache its buffer). After that every frame ran on core 1 (150 a window): 0 inline, 0 buffers passed by, 0 too big, 0 waits for a free buffer, 0 display environments not the presenter's, 0 frames uncaptured.
- **Found share, unchanged** (`vcache N pres:` lines, the Artisans windows 6,600–9,000, baseline → this change):

  | Window | Polygons found whole | Vertices found | Lost |
  |---|---|---|---|
  | 6,600 | 92% → 92% | 97% → 97% | 0% → 0% |
  | 6,900 | 95% → 95% | 98% → 98% | 0% → 0% |
  | 7,200 | 94% → 94% | 97% → 97% | 0% → 0% |
  | 7,500 | 95% → 95% | 98% → 98% | 0% → 0% |
  | 7,800 | 95% → 95% | 98% → 98% | 0% → 0% |
  | 8,100 | 96% → 96% | 98% → 98% | 0% → 0% |
  | 8,400 | 94% → 94% | 97% → 97% | 0% → 0% |
  | 8,700 | 94% → 94% | 97% → 97% | 0% → 0% |
  | 9,000 | 97% → 97% | 98% → 98% | 0% → 0% |

  The projections per frame are the same: 4,300 → 4,300 at 6,600, 4,316 → 4,316 at 7,500. The logic draw's lines are unchanged too, including the occasional "198%"/"204%" of vertices found: the baseline has them as well. They come from the render thread's report resetting the stats while the parse thread adds to them.
- **Work counters** (`perf_report.py` against `base-5eb8423`, vblanks 6,600–9,000, noise 2%; `$RUNS/auto-162-1/perf_report.txt`):
  - **`pause-ws`:** every counter the same; `vc.*` and `pres.frames` are 0.
  - **`wobble60`:** every GL, GR, upload and `ot.packets` counter the same, within 0.1%. The differences are a few draws per 2,400 vblanks; the GL counters are pulled once a vblank from where the render thread has got to. `vc.proj` 3,565.7 → 1,785.1, `vc.polys` 1,534.7 → 768.2, `vc.verts` 5,293.2 → 2,649.8 and `vc.misses` 105.0 → 52.4 per frame, each −49.9 to −50.1%. That is the presenter's half moved to its thread: the `vcache N work:` lines give the two threads about equal shares (at 7,500: the game thread 647,545 projections, the presenter's thread 647,445). `pres.frames` 0.498 → 0.498.
- **Vita3K's time lines** (not evidence, D112): in the same windows the `present` line's "presenter on this thread" went from 5,499–12,604 µs a tick to 209–258 (the snapshot and context), and the game thread's busy share from 33–84% to 18–29%. The presenter's thread was busy 1.2–4.3 ms per vblank.
- **Host:** the host runs the presenter inline, so it checks that the inline path is unchanged. Both binaries were built here: the branch's (`build/linux32`) and one built from a clean copy of `1b248ca` (`build/base-src`, `build/base-linux32`). Logs are under `logs/` in the worktree (gitignored); the script is `logs/h162.sh`.
  - **The attract demos with the cache**, for 12,000 vblanks at 16:9 and 60 fps (`--widescreen --interp --out960 --vcache`): 26 of 26 files identical to the base binary's (trace, shots, WAV, card), and the logs are identical too (`logs/h162-base-vc`, `h162-new-vc`). The presenter's frames found 96.1% of polygons whole and 98.4% of vertices, and the logic draw's 97.9% and 99.3%, as in D164.
  - **`--pres-check` with the cache:** 6,169 frames compared, 6,169 identical, 0 differ. 26 of 26 files and the log are identical to the base binary's (`logs/h162-*-pchk`).
  - **`tools/native_draw_ab.sh n162 demo wsi`** (`logs/n162-ndab.txt`): demo 26 of 26 and wsi 25 of 25 files identical, every native draw check 0 differ. The gems pad isn't in a fresh worktree, so its `gems` and `vc` scenarios weren't run; the `--vcache` A/B above stands in for `vc`.
  - **ctest:** 29 of 29 passed, `gte` skipped as on main (`logs/n162-ctest.txt`).
  - The VPK builds with no warning in the changed files. `tools/build_matching.sh`: `PSX.EXE SHA-1 OK`.
- **Hardware: not run** (it needs the owner's yes). The run to do is 7F3, `tools/vita/fps-runs/s7f3-toasty60-pres.txt`, with `persp=1 subpixel=1` added on the command line, run twice on this VPK. Once as it is (the presenter on core 1) and once with `pres_thread=0` too, which is the code path before this change:
  - `REALTIME=1 VPK=$V PAD=$PWD/tools/vita/hw-routes/o-toasty.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-162a 1500 @tools/vita/fps-runs/s7f3-toasty60-pres.txt @tools/vita/hw-routes/o-toasty.cfg persp=1 subpixel=1`
  - the same into `logs/hw-162b`, with `pres_thread=0` added.
  Read the game thread's busy share and late vblanks with `fps60_summary.py` and `hw_cmp.py`, and the `pres`, `vcache N work:` and `vpk-perfcount:` lines. D89 measured the cache at about 4.5 ms a vblank on the game thread at 60 fps. Expected: about half of that leaves the game thread, and the presenter's thread gains it, along with its 512 KB copy of the answers a frame.
- **Not done:**
  - `present.c`'s comment at the presenter's `DrawOTag` ("the worker never has the vertex cache") is now out of date. `present.c` is outside this change's files.
  - `tools/vita/fps-runs` has no file of its own for the run above; the run uses 7F3's file with two extra lines.

- **Later (D172):** merged with main's D152, which also fixed the in-between frame's answers: the walk now says where each polygon's answers go (D152), and the presenter on its own thread writes to its own array at that index, the array made with its table.

## 2026-11-19 — a cheaper vertex cache (#159)

### D168. The vertex cache gives the same answers with less work: no division for a depth ratio unless the quotient's rounding could decide the comparison, no maths for a key already ambiguous, and 40-byte entries. Per looked-up vertex, divisions fall from 1.01 to 0.43 on Vita3K's `wobble60`, and per projection from 1.17 to 0.94. Every answer and picture is unchanged. Counters for the cost (divisions, probes, table lines) now come with each `vcache` report
- **Why** (issue #159): D89's hardware run T1 put the cache (`persp`/`subpixel`) at about 4.5 ms a vblank on the game thread at 60 fps, and 1–1.5 ms on the OT walk. Since D167 the presenter's half runs on its own core, but every projection is still recorded (`rtp_point`) and every polygon vertex looked up (`psx_vcache_poly`).
- **Counting the cost first** (`62fa5cb`, no change in behaviour): `g_vcache_cost` (the game thread) and `g_vcache_pres_cost` (the presenter's thread), beside D165/D167's `g_vcache_work`, one writer each, plain adds. They count, for records and lookups apart, float divisions, the table's slots probed, and the 32-byte lines of the table touched. The lines are a model: the lines the accesses fall in, from a line-aligned table. `psx_vcache_stats_sum`, which both reports call once a report, logs a `vcache cost` line per thread with what was added since the last one (host: once at exit; Vita: every 300 vblanks, with the `vcache N` lines). They aren't in the `vpk-perfcount` set, since that needs `src/platform/vita/perfcount.c`, outside this change's files.
- **What the counts showed** (host, the intro to vblank 7,600 with `--vcache`): 1.19 divisions a projection (`rtp_point`'s one, plus 0.19 from `add_cand`'s "same depth?" ratio), 0.90 a looked-up vertex (the polygon's depth choice divides every candidate pair), 1.19–1.21 probes, 1.76–1.78 table lines.
- **The change** (`src/platform/common/vcache.c` only):
  - **Ratios without dividing.** `add_cand`'s `ratio(w, c) <= 1.02f` divides only when the larger depth is within 1.0199–1.0201 times the smaller (`same_depth`). Outside that band, no rounding of the quotient (float, or the host's x87) can change the comparison. In the polygon's choice, a candidate whose ratio is 4 or more is passed over without dividing (`under4`: `a < 4b`, exact, since times 4 isn't rounded). Such a ratio can't be a polygon's: if it were the closest, the worst would be 4 or more, and the choice is only taken under 4 (`best` starts at 4). With one candidate for the first vertex, only "under 4" matters. A vertex left with one candidate is then its own pick, and it is under 4 without dividing when it's under 3.999 (`under3999`). Every other ratio is divided as before, in the same order, so the picks, their depths and the `VREF_PERSP` flags are the same.
  - **The entry before the maths.** A record now finds its entry first, as recording would have (made if new: the same entries and counts), and a key already ambiguous returns at once: it stays ambiguous whatever the projection is, and nothing else changed for it before.
  - **40-byte entries.** The frame number (24 bits) and the old 32-bit header (state, candidate count, the multi and source flags) share one tag word: key, tag, four candidates. A probe and the first candidate now share a 32-byte line more often. The table is 320 KB (was 360). The frame count comes round after 16.7 M frames (77 h at 60 a second). Each table is then cleared as it next starts recording (`stale`), not while the other frame's walk may still read it; the 32-bit count's clearing of both at once (after 2.2 years) could.
  - **The hash and probe are unchanged** (multiplicative hash, linear probe), so every entry's slot and every `VREF` slot number is the same, and so is "table full".
  - **The host's x87.** The linux32 build is i686 without SSE: an expression can stay at long double precision until it's stored. The answers used to reach `record()` as float arguments, so they were rounded there; inlined, an offset came out 1/256 pixel apart (intro, vertex 603,817). `flt()` rounds the offsets and depths to float again on i386 without SSE; on the Vita and elsewhere it's nothing.
  - **Not done**, because each changes answers: fixed point in `rtp_point`, a reciprocal shared by RTPT's three vertices, NEON (no IEEE division). The one division a projection left (`h / (z·SZ3)`) can't be removed without changing the offsets, and the issue says to keep such an approximation behind a switch; switches live in the platforms' `config.txt` code, outside this change's files. `ngte_vcache` (`gte_ops.c`) still builds the 15 values for each of RTPT's vertices; sharing them needs a new function the presenter's build would also have to rename (`render_ctx_names.h`).
- **Same answers** (host, `build/linux32`; a temporary FNV checksum, not committed, of every looked-up vertex's reference word and, for a found one, its candidate count, multi flag and every candidate's offsets and depth bits, with a checkpoint every million vertices; `logs/sum-{before,after}-*` in the worktree, gitignored). "Before" is `62fa5cb` (only the counters added), "after" this change:
  - the intro to 7,600 (`--pad tools/oracle/pads/newgame_host.txt`): 15,761,450 vertices, 16 checkpoints identical, `992e27c6` at the end;
  - the gems pad (`--widescreen`, 8,768 vblanks): 19,947,396 vertices, 20 checkpoints identical, `b8905b58`;
  - the attract demos at 16:9 and 60 fps (`--widescreen --interp --out960`, 12,000 vblanks, the presenter inline): 55,309,803 vertices, 56 checkpoints identical, `8c8f6698`.
  The `host vcache` lines (projections, entries, invalid, ambiguous, found) are identical in each pair.
- **Less work** (the counters, per projection and per looked-up vertex, before → after):

  | Run | divisions / projection | divisions / vertex | table lines / projection | table lines / vertex | probes |
  |---|---|---|---|---|---|
  | host intro to 7,600 | 1.19 → 0.94 | 0.90 → 0.30 | 1.78 → 1.50 | 1.76 → 1.49 | 1.19 / 1.21, same |
  | host gems | 1.20 → 0.95 | 0.91 → 0.29 | 1.76 → 1.50 | 1.77 → 1.50 | 1.18 / 1.21, same |
  | host demos 16:9 60 fps | 1.18 → 0.93 | 0.96 → 0.36 | 1.81 → 1.54 | 1.82 → 1.55 | 1.22 / 1.25, same |
  | Vita3K `wobble60`, 6,600–9,000, game thread | 1.172 → 0.939 | 1.010 → 0.426 | 1.802 → 1.530 | 1.833 → 1.551 | 1.216 / 1.258, same |
  | the same, presenter's thread | 1.173 → 0.939 | 1.011 → 0.427 | 1.803 → 1.531 | 1.833 → 1.552 | 1.216 / 1.258, same |

  In Vita3K's windows both threads recorded exactly the same 5,036,164 and 5,025,774 projections and looked up the same 7,402,631 and 7,392,939 vertices before and after. The "before" there is a VPK of `62fa5cb` (`$RUNS/auto-159-1/wobble60-before`, its pictures 91 of 91 the baseline's).
- **Vita3K** (`realtime=0`, every route `vdm_ring=1024`; VPK of `1aa9da4`, SHA-1 `4e10f1c3…`, `$RUNS/auto-159-1/br1.vpk`; runs in `$RUNS/auto-159-1/<route>`), each exit 0 on the first try. Against `$RUNS/base-a86cab5` with `shots_cmp.py --max-diff 0 --tol 0 --edge 0`: `title` 10 of 10 the same, `pause-ws` 91 of 91, `pause-ws-wobble` 91 of 91, `wobble60` 91 of 91. `perf_report.py` (vblanks 6,600–9,000, noise 2%): every counter the same on `pause-ws` and on `wobble60`, `vc.proj` 1,785.139, `vc.polys` 768.244, `vc.verts` 2,649.769, `vc.misses` 52.408 a frame on both sides.
- **Host checks** (logs under `logs/` in the worktree):
  - the attract demos at 16:9 and 60 fps with the cache, base `8dab625` against this change (`build/base-bin`, `build/new-bin`), trace, shots, WAV and card: 26 of 26 files identical, and the logs identical apart from the run's folder name and the new `vcache cost` lines (`logs/ab-{base,new}-vc`). `build/new-bin` was built from this change before two comment-only edits;
  - the same with `--pres-check`: 6,169 frames compared, 6,169 identical, 0 differ; 26 of 26 files identical, the logs as above (`logs/ab-{base,new}-pchk`);
  - `tools/native_draw_ab.sh n159 demo wsi` (`logs/n159-ndab.txt`): demo 26 of 26 and wsi 25 of 25 files identical, every native draw check 0 differ;
  - `ctest` (`logs/ctest.txt`): 28 of 29 passed, `gte` skipped as on main. `gte_native` first failed in 0.03 s: its binary had been left at mode 0644 by the link (`cmake/link_exec.sh`'s race on the Docker mount) and `logs/gte/gte.bin` wasn't in the fresh worktree. With the vectors copied in and the mode set, it passed (`logs/ctest-gte_native.txt`). Its sources aren't in this change.
- **Hardware: not run** (it needs the owner's yes). The issue names T1's file, `tools/vita/fps-runs/t1-subpixel-60.txt`, but it predates D91 and D127 (`fast_render=2`, `native_draw=0`), so the run to do is D167's on this VPK, the Toasty route at 60 fps with the cache on:
  - `REALTIME=1 VPK=$V PAD=$PWD/tools/vita/hw-routes/o-toasty.pad DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-159 1500 @tools/vita/fps-runs/s7f3-toasty60-pres.txt @tools/vita/hw-routes/o-toasty.cfg persp=1 subpixel=1`
  - and, to weigh it, the same on the `autopilot` VPK before it (`8dab625`, which is D167's run `logs/hw-162a` if that has been done).
  Read the game thread's time and busy share, the presenter's thread's, late vblanks per 300 (`fps60_summary.py`, `hw_cmp.py`), and the `vcache cost` lines, which should read as Vita3K's above. Expected: less than D89's 4.5 ms a vblank; how much less only the run can say, since the counts don't weigh a division against a cache line on the A9.
- **Not done:** the one division a projection, and the other ideas that change answers (above). The new counters in `vpk-perfcount` (`perfcount.c`). Probes per lookup (1.2–1.3) are near what a 3/4-full linear table gives; a larger table would cut them and cost L2.

## 2026-11-19 — fast_render_ab's gems runs and the shared memory card (#168)

### D169. The gems route's late loads in `tools/fast_render_ab.sh` were a memory card left in `./save` by an earlier run, not `--prof-draw` or any clock. Every run of the script now gets its own card folder, and gems and lod fail unless both runs end with the script's four gems
- **Why** (issue #168): #167's worker saw three of four `fast_render_ab.sh` gems runs miss the Artisans load at vblank 2108, collect no gems and end on the title screen (gamestate 13), while every run of the other scripts was on time. `fast_render_ab.sh` was the only one passing `--prof-draw`, whose wrappers read `CLOCK_THREAD_CPUTIME_ID`, so the profiler, or `main.c`'s `CLOCK_MONOTONIC` `now_us`, was the suspect. A pair could match with neither run reaching the gems, so gems and lod could pass without checking anything.
- **The cause:** the script gave `--save` only to the route and whole runs. Without it the memory cards are `./save/card0` (`libmcrd.c`'s default root), one folder for every run in the worktree. The gems script starts a new game, and the title screen's memory-card steps (`titlescreen.c`: `TS_SubState_LoadMemCards`, then with no save file `TS_SubState_CreateSaveConfirm` and `MemCardCreateFile`) write a save file there. The next run's title screen finds that file, so the menus the script's presses were recorded against (likely the create-a-save question) aren't the ones on screen, and the cross at vblank 2102 no longer starts Artisans. So only the first run in a fresh worktree (or after `./save` is removed) reaches the gems. That matches #167's runs: `i167fr-gems-f0`, its first, was on time; every later one was late. The other scripts (`native_draw_ab.sh` since D91, `pres_check_ab.sh`, `mixer_ab.sh`, `host_checks.sh`) already give each run its own `--save`, which is why none of their runs was late.
- **The clocks aren't involved:** `now_us` is only installed with `--cd-stats` (`psx_cd_set_clock`), which no script passes, and the profiler only adds time to totals logged at exit. Runs of the gems pad to vblank 2,400 with `--widescreen` on this branch's base build (`2f22813`; `logs/i168-*` in the worktree):
  - `--prof-draw`, no `--save`, four in a row in a fresh worktree: the first reached level 10 at vblank 2108, the next three stayed on level 0 (trace first differs at vblank 2108) — the card the first wrote at 22:53 was in `./save`.
  - no `--prof-draw`, no `--save`, four more with that card still there: all four identical, and all four stayed on level 0.
  - `--prof-draw` with a fresh `--save` per run: three with `--fast-render 3` and two with `0`, all five reached level 10 at 2108, and all five traces identical (and identical to the first run above).
- **Chosen:** `tools/fast_render_ab.sh` passes `--save DIR/save` to every run (as `native_draw_ab.sh` does), and keeps `--prof-draw`: it doesn't change a run. For gems and lod, a run whose trace's last row shows fewer than 4 gems prints `FAIL`, and the script exits 1. `docs/host-build.md` says so, and that runs sharing `./save` aren't comparable. No code changed: the host build and the Vita build are the same as before.
- **Checked** (this branch's build of `2f22813`; the stale `./save` card from the runs above was still in the worktree):
  - `tools/fast_render_ab.sh i168frN gems`, five times (`logs/i168-fr5x.txt`, `logs/i168frN-gems-f{0,3}`): each pair 19 of 19 files identical, exit 0. All ten traces are identical, Artisans loads at vblank 2108 in each, the last row shows 4 gems, and all ten cards are the same file.
  - The gems check: a trace ending with 0 gems (`logs/i168-prof-2`) and a missing one each print `FAIL`; a trace ending with 4 passes.
  - `tools/native_draw_ab.sh i168nd gems vc` (`logs/i168-ndab.txt`), exit 0: gems 19 of 19 and vc 19 of 19 files identical, every native draw check 0 differ; all four traces are the same as the five pairs' above.
  - `main.c` and the run loop are unchanged, so the attract demos weren't rerun. Nothing reaches the Vita: no VPK, no Vita3K.

## 2026-11-19 — the link launcher and the mutant tests (#163, #165)

### D170. Every linux32 link goes through `cmake/link_exec.sh`, which puts back an execute bit that ld's chmod lost on `tools/tc`'s bind mount (colima's reverse-sshfs), and every mutant test passes only when its output shows its own error (`spyro_mutant_test`, `PASS_REGULAR_EXPRESSION`), not on any failure (`WILL_FAIL`). The mount itself stays as it is
- **Why** (issue #163, made in 3f39b66; written up for #165): in a fresh worktree's host build, `test_gte_inline` and `test_gte_native_mutant3` had no execute bit (#158's worker: `gte_inline` failed until `chmod +x`, and its binary was byte-identical to the baseline's). qemu-i386 refuses such a file, so a normal test fails, and `gte_native_mutant3`, a `WILL_FAIL` test, "passed" without ever running: a check that couldn't fail.
- **The cause:** GNU ld makes its output executable only after writing and closing it: a stat, then a chmod to the mode plus 0111 less the umask, and no chmod if the stat fails (binutils `bfd/opncls.c`, `_maybe_make_executable`). `tools/tc` mounts the worktree at `/work` with `docker run -v`, and colima serves that mount over reverse-sshfs, where that chmod is now and then lost while other processes stat the same path (the build's parallel links, and lima's inotify agent watching the tree). #163's worker reproduced it (`logs/run163-race` in #163's worktree, which is gone; the numbers are in #163's closing comment and 3f39b66's message):
  - 1 of 60 parallel gcc links kept mode 0644;
  - 1 of 400 plain stat-then-chmod runs, with other processes stat'ing the file, kept mode 0644 (on the Mac too, says 3f39b66's message).
  So the bit is lost at ld's own chmod, not by a copy step or a post-build command.
- **Chosen:**
  - `cmake/link_exec.sh OUTPUT -- LINK-COMMAND...`, set as the global `RULE_LAUNCH_LINK` in `CMakeLists.txt` only when `SPYRO_HOST` is `linux32` (so the Vita build's ninja files never name it; #163's gate built the VPK, exit 0). It runs the link and returns its exit code if it fails. It skips `.a`, `.so` and `.so.*`. For anything else, while the output isn't executable it logs `lost its execute bit after the link (try N), setting it` and runs `chmod +x` (which applies the umask, as ld does), sleeping 1 s after a chmod that errors. After 5 tries it logs `still has no execute bit after the link` and fails the link (exit 1), so a build never ends with a binary that can't run. One launcher covers every link, rather than `chmod` steps added per target.
  - `spyro_mutant_test(name regex)` in `tests/CMakeLists.txt` sets `PASS_REGULAR_EXPRESSION` on all 11 mutants (`tests/fill`, `tests/gte`: `gte_native_mutant` to `gte_native_mutant3`, `libgte_mutant1` and `libgte_mutant2`, `tests/lean`, `tests/spu`, `tests/vcount`, `tests/walk`: `walk_synth_mutant1` and `walk_synth_mutant2`). Each regex is the mutant's own error (the mutated field's `want .. got ..`, the mutant's packets, its D tag) and the check's verdict line, so a binary that can't run, crashes, or fails for another reason fails the test. The exit code isn't looked at.
- **Checked** (#163's worker and gate, from #163's closing comment): a clean host build in a new worktree, 22 test binaries all rwx, ctest 29 of 29. `gte_native_mutant3`'s output shows `col MAC2: want .. got ..` and then `FAILED`; with its execute bit removed the test now fails. Gate (`runtime/autopilot/runs/gate-163-1`): native VPK build exit 0, title run exit 0, 10 of 10 the same, `noassets` 0. #165 changed only docs; nothing was rebuilt or rerun for it.
- **Found on the way:** a full host ctest takes about 29 minutes, the three `gte_native` mutants 200–300 s each (a million random register states per opcode under qemu-i386). And the mount can give ninja a stale view: "no work to do" right after an `rm` of an output. `docs/host-build.md` ("ctest, the mutant tests and the bind mount") says how to add a mutant, to run ctest in the background, and not to trust "no work to do" there.
- **Not taken** (both would fix the mount rather than work around it; left to the owner):
  - colima's virtiofs mount type: changing it rebuilds the colima VM, which other projects on this Mac share.
  - `build/` on a Docker volume instead of the bind mount: the build would no longer go through sshfs, but that changes `tools/tc` and where every worktree's build lives.

## 2026-11-20 — GL state shadow in gpu_gl.c (#169)

### D171. `gpu_gl.c` keeps a shadow of the GL state it last set and skips calls that would set the same thing again: the program in use, each program's `u_xf` and `u_semi`, the array buffer, the attribute arrays' enables and VBO pointers, the active texture unit, the texture bound to each unit, and the blend enable, equation and function. Per frame on `wobble60`, `gl.attribs` falls from 421.5 to 14.5, `gl.uniforms` from 186.7 to 25.9, `gl.tex_binds` from 43.7 to 2.0, `gl.programs` from 105.0 to 94.9 and `gl.state` from 316.4 to 242.7. Draws, triangles and every picture are unchanged
- **Why** (issue #169): the D165 counters showed more GL state calls than draws on every benchmark route. `batch_issue` sets the program, `u_xf`, the VBO, 3–7 attribute arrays and their pointers, the VRAM texture and the blend state for every joined draw. `draw` sets the program and both uniforms again for each run, though nearly all of them are the same from one batch to the next. vitaGL doesn't skip these. Any `glUniform*` or `glUseProgram` makes it copy the program's whole uniform buffer at the next draw (`vita-port-kit` `vita/docs/vitagl.md`, item 9). A changed blend factor re-patches the fragment program.
- **What** (`src/platform/vita/gpu_gl.c`, the `gs_*` functions after `Prog`): every program, uniform, buffer, attribute, texture-unit and blend call in the file goes through a `gs_*` function. That function makes the GL call only when the value differs from what was last set, so the call order and the state at each draw are the same as before. Nothing is reordered. A skipped call isn't made, so the `VITA_PC_GL` macros (`vita_pc.h`) never see it: the counters count calls made, and `vita_pc.h` is unchanged.
  - **Uniforms** are cached per GL program name, not per `Prog`. `s_prog` is a copy of one `s_vprog` row, so one program has two `Prog`s. Location -1 isn't called at all, since GL ignores it.
  - **Attribute pointers** are cached only for VBOs. vitaGL resolves a VBO pointer at the draw, so a `glBufferData` into the same buffer needs no new pointer. Pointers into client memory (the quads, the atlas decode) are always set, because their arrays are on the stack.
  - The array buffer is bound only when a pointer or `glBufferData` needs it. In `batch_issue`, the explicit bind of `s_vbo_side` became part of setting attributes 4 and 6.
  - **Not cached:** viewport, scissor and the scissor enable. vitaGL works out the viewport from the framebuffer bound when `glViewport` is called, and `glEnable(GL_SCISSOR_TEST)` re-marks the scissor for the next draw. Texture parameters were already cached for the atlas (`s_alin`). FBO binds were already cached (`bind_fbo`).
- **When it's forgotten:** every GL call in the port is in `gpu_gl.c`, so the only state the shadow can't see is vitaGL's own work and names that die.
  - `gs_forget` (bindings, enables, the active unit, blend) runs after every `vglSwapBuffers`: `swap()`, and the first swap in `gl_gpu_init`. The options panel's overlay is drawn right before the swap, through the same functions. It also runs in `gl_rescale` (widescreen, resolution and save states drop every target).
  - `gs_reset` also forgets the programs' uniform values, and runs in `gl_set_options` (options that compile programs).
  - `gs_tex_deleted` forgets a unit that held a texture just deleted (targets, spares, the read-back texture, presentation slots), since its name can come back from `glGenTextures`.
  - There is one vitaGL context. The render thread and the game thread never hold it at once (`vita_pc.h`, as `s_st`), so no context change happens.
- **Checked** (`runtime/autopilot/runs/auto-169-1`, VPK SHA-1 `32be7f26…`; all with `vdm_ring=1024`; each passed on its first try, exit 0):
  - **Pictures:** `shots_cmp.py --max-diff 0 --tol 0 --edge 0` against `base-bbc36b1` gives `title` 10/10 same, `pause-ws` 91/91, `pause-ws-wobble` 91/91 and `wobble60` 91/91. `pause-ws` with `tex_filter=1` and with `res=ps1` (they switch programs) gave 91/91 each against the same settings run on `autopilot-bbc36b1.vpk` (`base-pause-ws-texf`, `base-pause-ws-ps1`).
  - **Counters,** `perf_report.py --noise 0.02` over vblanks 6,600–9,000 (`perf-*.txt`), per frame, before → after:

    | route | gl.state | gl.programs | gl.tex_binds | gl.uniforms | gl.attribs |
    |---|---|---|---|---|---|
    | `wobble60` | 316.4 → 242.7 (−23%) | 105.0 → 94.9 (−10%) | 43.7 → 2.0 (−95%) | 186.7 → 25.9 (−86%) | 421.5 → 14.5 (−97%) |
    | `pause-ws` | 158.5 → 122.3 (−23%) | 52.6 → 47.5 (−10%) | 21.8 → 1.5 (−93%) | 93.4 → 10.2 (−89%) | 226.0 → 12.0 (−95%) |
    | `pause-ws-wobble` | 158.5 → 122.2 | 52.6 → 47.5 | 21.8 → 1.5 | 93.4 → 10.2 | 211.7 → 11.5 |
    | `pause-ws`, `tex_filter=1` | 170.2 → 127.3 | 48.2 → 36.5 (−24%) | 21.8 → 1.5 | 84.8 → 9.8 | 226.0 → 12.0 |
    | `pause-ws`, `res=ps1` | 158.5 → 122.3 | 52.6 → 47.5 | 21.8 → 1.5 | 93.5 → 10.2 | 226.1 → 12.0 |

    `title` over the whole run (its 2,700 vblanks end before 6,600): `gl.uniforms` 27.9 → 0.007 and `gl.attribs` 11.9 → 10.1.
    - `gl.draws`, `gl.tris` and every other counter are the same on `pause-ws` and `wobble60`, exactly.
    - On `pause-ws-wobble` and the two runs against the autopilot VPK, `gr.draws` (the game's draws, counted before any GL) is 0.1% apart in the window. Over the whole run, `gl.draws`, `gl.tris`, `gr.draws` and `vbo.uploads` are identical: a frame landed in another window in Vita3K.
  - **Build and assets:** the native VitaSDK VPK build exits 0 with no warning in `gpu_gl.c`, and `noassets` exits 0.
- **What's left:** `gl.programs` remains about one per draw run plus the present's quads. Those are real program changes: the joined draws alternate formats and kinds. The calls skipped here are CPU work on the render thread: vitaGL's per-call work, and uniform copies at the next draw. Only an owner-approved hardware run can show whether render-thread time or frame rate moves (the counters say nothing about time).
- **Later (issue #172): D168's cost counters in the `vpk-perfcount` lines.** `perfcount.c` pulls `g_vcache_cost` (game thread) and `g_vcache_pres_cost` (presenter's thread) once a vblank, as it does `g_vcache_work`, into six new counters: `vc.div`, `vc.probe`, `vc.lines` and `vcp.div`, `vcp.probe`, `vcp.lines`. Each is the record and lookup counts added, because splitting them would need 34 counters and the kit holds 32 (`VPK_PC_MAX`; a `_Static_assert` now guards it). The split per vertex stays in the `vcache cost` lines. Nothing new runs on a hot path, and each counter keeps its one writer (D167). The presenter's counters are read with a relaxed word load, like `gpu_gl.c`'s. Now 28 counters.
  - **Checked** (`runtime/autopilot/runs/auto-172-1`, all with `vdm_ring=1024`, each exit 0 on its first try). Pictures: `title` 10/10 same, `pause-ws` 91/91, `pause-ws-wobble` 91/91, `wobble60` 91/91 against `base-9d42dac`. Whole-run totals of every older counter are identical on all four routes. In the 6,600–9,000 window they're within 0.1%, a frame landing in another window (as above). The new counters are 0 on `title` and `pause-ws`. On `wobble60`, per frame: `vc.div` 2768.7, `vc.probe` 5442.9, `vc.lines` 6770.4, `vcp.div` 2763.0, `vcp.probe` 5432.3, `vcp.lines` 6758.8. Through vblank 9,000 their totals equal the `vcache cost` lines' sums exactly. `pause-ws-wobble` has the same game-thread values and `vcp.*` at 0 (no presenter frames).

## 2026-11-20 — main merged into the autopilot branch

### D172. The autopilot branch (D164–D171) merged with main at `a9db797`: main's D152 says where each polygon's answers go, D164's own table gives the in-between frame its projections, and D167's array is the presenter's on its own thread

- **Why:** the autopilot (`autopilot/report.md`) branched from a local `main` 162 commits behind GitHub's: no PAL support (D154), no 1.2.x. Its builds refused the owner's PAL disc on the Vita ("WRONG DISC: SCES-01438"). The owner: "pull main and merge it onto the autopilot branch, and then test on vita3k".
- **Decision numbers:** the autopilot's D130–D137 were main's numbers too (D130–D161 landed on main meanwhile), so they are D164–D171 here, in this file, with every reference the autopilot's commits made renumbered; main's own D130–D137 stay as they are.
- **The vertex cache, where D152 (main) and D164 (#158) fixed the same two faults:**
  - **Where the answers go:** D152's. The walk gives `psx_vcache_poly` the polygon's slot, `g_vcache_side` at its first vertex's index in the buffer it fills (`native_draw.c`). D164's list of known buffers (`psx_vcache_set_base`, `s_bases`) is gone with the call it hung on.
  - **The in-between frame's projections:** D164's. The presenter records into a table of its own (`psx_vcache_pres_begin`) and looks up there. D152's `pres_draw_vcache`, which pointed the presenter's hook at the game's (`rtp_value_hook`, the logic frame's table) for an inline draw, is removed: with D164's lookups in the presenter's table it would have recorded where nothing looks, and the in-between frame would have found nothing again (D152's cause 2).
  - **The presenter on its own thread (D167):** a polygon of its frame there gets the slot the walk gives, moved to the presenter's own array at the same index (`s_pres_side`). That array is made with the presenter's table at its first `psx_vcache_pres_begin` (inline), where D167 made it at the first buffer `psx_vcache_set_base` saw.
- **The vendored kit:** the autopilot's `31295be7` holds main's `78915280`; `VENDORED` lists both sides' runtime paths (`perfcount`, `vcount`), `tools/vendor.sh --check` clean.
- **Routes:** `wobble60` now says `config_version=5` (main's runners write 5; a 3 would be an older file's). On main 60 HZ is the default (D150), so `pause-ws` and `pause-ws-wobble`, which set no `interp`, now run 60 Hz logic in play, where they ran 30 fps before.

#### Checked
Runs in `runtime/autopilot/runs/merge-base` (main `a9db797` plus D166's two `vdm_ring` commits, so its wobble routes don't crash Vita3K) and `merge-m1` (this merge), VPKs in `runtime/autopilot/vpk/` (gitignored), native VitaSDK, `vdm_ring=1024` on every route.
- **Pictures, main against the merge** (`shots_cmp.py --max-diff 0 --tol 0 --edge 0`): `title` 10/10 same, `pause-ws` 91/91, `pause-ws-wobble` 91/91, `wobble60` 91/91, and two new PAL routes from the Europe image (`DISC=disc/image-pal`, `tools/vita/vita3k_run.sh`): `pal-title60` (the owner's D152 settings: 16:9, `interp=4`, `persp=1 subpixel=1 tex_filter=0`, shots at 600, 1200, 2100, 2101, 2700) 10/10, `pal-title` (defaults) 10/10. 303 of 303. Each run exit 0 on its first try. The PAL runs: "WAD.WAD matches a good dump".
- **The presenter with the cache** (`wobble60`, vblanks 7,200 and 8,400): main "0 frames on core 1, 150 inline"; the merge "150 frames on core 1, 0 inline", the cache's work split evenly between the game thread and the presenter's (637,444 and 637,128 projections to 7,200). The in-between frames find 97% of their vertices (94% of polygons whole), 0% lost; `pal-title60` 98% (96%), 0% lost.
- **Work counters against the autopilot's last baseline** (`base-ddbb408`, `perf_report.py`, `wobble60`, vblanks 6,600–9,000): every `gl.*` and `vc.*` counter the same within 0.1% (`gl.attribs` 14.5, `gl.uniforms` 25.9, `gl.tex_binds` 2.0 a frame): D168's and D171's gains hold on main. `pause-ws` doubles (`gl.draws` 41.9 → 90.8): the 60 Hz default above, the same pictures as main.
- **Host** (`tools/tc`, the linux32 build of main's tree and of the merge): `tools/host_checks.sh` 289 of 289 files identical (attract demos, the whole-game tour, routes A and B; traces, screenshots, saves, and the logs once the run's folder name is masked), `logs/hmb-*` against `logs/hm1-*`. ctest on the merge: 29 passed, 0 failed; `gte` skipped as on main, `pbp` and `pbp_made` skipped (no `EBOOT.PBP` made here) (`logs/hm1-ctest.txt`).
- **Hardware** (2026-11-20, the owner's yes in chat: "vita is ready for the hw run so do that now too"; the owner's Vita at 192.168.1.4, vitacompanion 1.05, `tools/vita/hw_run.sh` under the kit's lock; runs AP1–AP7 of `tools/vita/fps-runs/README.md`, logs `logs/hw-ap*`, gitignored). The merge's VPK `merged-final.vpk` (commit `37c3a95`, SHA-1 `1884188d…`, eboot `2619d317…`); AP4 and AP5a on the autopilot's older VPKs. Every run reached `frames=` with no crash dump, and the owner's files were put back each time. The Toasty runs are real time (`REALTIME=1`), so they drift: compare level means.
  - **AP7, the PAL disc** (the owner's settings, virtual vblanks): "SCES-01438, Spyro the Dragon (Europe)", "WAD.WAD matches a good dump"; the 5 shots byte-identical to Vita3K's `pal-title60` (Vita3K's `-hd` GPU pictures are black, so only hardware's say anything); the GPU's own picture of the in-between frame (`shot-2101-hd.png`) clean, no lines along polygon edges; the presenter 150 frames on core 1, 0 inline; 98% of vertices found (96% of polygons whole), 0% lost, in both kinds of frame. AP7-final, the same run at the session's end, 10 of 10 shots identical to AP7.
  - **The wobble fix's cost at 60 fps** (D164; AP1 `persp=0 subpixel=0` against AP2 both on, INTERPOLATED, Toasty route): the game thread 3.37 → 5.97 ms a vblank; late vblanks per 300 1.1 → 19.5; by level: the title 0.0 → 0.3, Toasty 0.0 → 1.2, Artisans 0.4 → 25.4, Stone Hill 0.1 → 49.9 (3 of 7 windows clean). Quiet levels hold 60 fps; busy ones don't. The README's known issues say so.
  - **The presenter on core 1 with the cache** (D167; AP3 `pres_thread=0` against AP2): the game thread 9.16 → 5.97 ms a vblank (−35%); late per 300 on the title 11.9 → 0.3 and in Toasty 20.9 → 1.2; falls to 30 fps 12 → 2. Stone Hill the same (49.1, 49.9): something else limits it there.
  - **The cheaper cache** (D168; AP4a, `a86cab5`, against AP4b, `bbc36b1`): the same work (`vc.*` within 1–2%, the drift), the cache's own time ("expand") 1.74 → 1.61 ms a vblank (−8%). The frame-level numbers moved the other way (the game thread 5.60 → 6.04 ms, Stone Hill late 24.9 → 47.9), but AP4a's Stone Hill is the outlier of the four runs with the cache on in Toasty (AP2, AP3 and AP4b 48–50): run-to-run noise larger than the saving. No frame-rate gain is measurable from it.
  - **The GL state shadow** (D171; AP5a, `bbc36b1`, against AP5b, the merge; the pause route, virtual vblanks, 60 fps, wobble on): per frame `gl.attribs` 421.3 → 14.5, `gl.uniforms` 186.7 → 25.9, `gl.tex_binds` 43.6 → 2.0, `gl.state` −23%, `gl.programs` −10%, draws the same, as in Vita3K. The render thread's time is the same (15.07, 15.08 ms a vblank) and so is the game thread's (5.59): vitaGL's redundant calls were cheap; the gain is calls, not time.
  - **`vdm_ring=1024`** (D166; AP6a default ring against AP6b): no crash, the 91 shots and the memory card byte-identical, every counter and time the same (the game thread 5.58, 5.65 ms; 0 late), and 896 KB less CDRAM free (15,024 → 14,128 KB; RAM the same, not RAM as AP6's plan expected). It only helps Vita3K, so the default stays off; the routes keep passing it.
  - **Left installed:** after AP7-final (which reinstalled the whole app: the older VPKs' `sce_sys/` differ), `hw_run.sh --put-eboot merged-final.vpk`: READBACK IDENTICAL, SHA-1 `2619d317…`. The owner's `config.txt` (the PAL disc) is untouched.

### D173. The PS4 fpkg: the low-image model, `__ptr32` pointer fields, measured first (2026-11-20)

- **Asked:** the owner, 2026-11-20: make the port a PS4 fpkg for jailbroken PS4s (and PS5s), choose the approach to 64-bit, measure it, and trial a host build. Plan: `docs/ps4-plan.md`. Numbered D173 because the autopilot branch holds D164–D172.
- **Measured** (`tools/ps4/trial64.py`: the linux32 native-mem build's compile commands replayed with clang for `x86_64-unknown-linux-gnu`, 186 units; `logs/run-ps4trial/`): plain LP64 fails 22 units, with 52 layout `_Static_assert`s and 176 pointer→int casts. With the decomp's 231 pointer fields marked `__ptr32 __uptr` by script (`--ptr32`), 17 units fail, 8 asserts (the port's own mirrors) and ~30 local errors, no hand edits. Apple clang 16 crashed on a `?:` mixing `__ptr32` and `void *` (`present.c:625`).
- **Research** (a subagent's web search, 2026-11-20): OpenOrbis v0.5.4 (LLVM 18) and LibOrbisPkg are what 2026's PS4 ports use (https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain, https://github.com/alechurri/2ship2harkinian-ps4, https://github.com/Mayo1970/ioQuake3-PS4, https://github.com/YoavAharonOfficial/Recomp-to-PS4-FPKG); Piglet GLES2 with no runtime shader compiler on retail firmware; the executable at 0x400000 with globals below 2 GB (2Ship's `docs/TECHNICAL.md`); no homebrew shown mapping a fixed address below 4 GB; `__ptr32` function-pointer calls fixed only in LLVM PR #209078 (2026-11-03), narrow stores miscompiled on AArch64 (llvm-project issue #228782); the Tenkaichi3 decomp uses `__ptr32` with low RAM on x86-64 (https://github.com/z3xox/Tenkaichi3Decomp); recompilers use base+offset. Not checked on hardware.
- **Chosen: the low-image model.** `P32` (`__ptr32 __uptr` for x86-64 clang, empty elsewhere) on every pointer field; the arena and the game thread's stack as `.bss` arrays in a non-PIE image at 0x400000, so everything the game can address is below 4 GB without a fixed mapping; a startup check refuses to run otherwise. linux32 and the Vita compile the same text with `P32` empty, unchanged.
- **Why not offsets everywhere:** ~231 annotated fields and ~40 local fixes against rewriting thousands of field uses (~14,500 by a crude grep) in the decomp's C and the hand C, all verified byte for byte today. The recompilers' model stays the fallback if the PS4 won't keep our image low or `__ptr32` miscompiles beyond workarounds; P1's field inventory serves it too.
- **Verification:** a linux64 host build (clang, x86-64 under `qemu-x86_64`, every `malloc` an `mmap` above 4 GB so a stray host pointer crashes instead of aliasing) must match linux32 byte for byte in `tools/host_checks.sh` (P1).

### D174. linux64: the low-image model works on x86-64, with three clang bugs walled off (PS4 plan P1, 2026-11-20)

- **Asked:** P1 of `docs/ps4-plan.md` (the session that chose D173): an x86-64 Linux host build in the low-image model, checked byte for byte against linux32. The owner approved clang, the amd64 cross libraries and `qemu-x86_64` in the host image (2026-11-20).
- **Built:** `-DSPYRO_HOST=linux64` (`cmake/linux64.toolchain.cmake`): `src/platform/linux32` built with clang 19 for `x86_64-linux-gnu`, `-fms-extensions`, static and non-PIE at 0x400000, native memory, run under `qemu-x86_64`. The image adds `clang-19`, `gcc`/`g++-x86-64-linux-gnu` and `libc6-dev-amd64-cross` (libstdc++ for the configure step's C++ check), and rewrites the cross `libm.a` linker script, which names native paths. `docs/host-build.md` has the commands.
- **`P32`** (`src/compat/p32.h`, forced into every C and C++ unit and into the overlays' preprocessing): `__ptr32 __uptr` for x86-64 clang, empty elsewhere. `patches/spyro-1-ptr32` (a new folder, applied after every other, so the region patches' context isn't disturbed): `0001` generated by `tools/ps4/p32_patch.py` from the fully patched tree (231 pointer fields, 223 pointer labels and initialised tables, 54 other `T **` such as `*(Vector3D **)&particle->m_Data.m_Raw[20]`, which compile silently and read 8 bytes, 5 PsyQ `long`/`u_long` data declarations and stores, 5 address-initialised tables), `0002` by hand (`PATCH_POINTER` casts to `__typeof__(dest)`: clang makes `int = void *` an error). The port's own: `CameraView`, `CreditsLine`, nine label declarations, the moby lists and collision grid (`Moby *P32 *`), `s1_scratch_relocs`. No line is added before a file's end, so the 32-bit builds' code doesn't move.
- **Found on the way, beyond the trial's list:** PsyQ's `u_long` is 4 bytes, LP64's 8: `SoundDefinition` and `g_TitlescreenSelColors` (the title screen's logo and PRESS START came out as one garbled triangle until fixed); `PortRegionData` called through an implicit `int` declaration (it truncates a pointer); explicit `(T **)` casts over the game's pointer arrays, which compile without a word.
- **Three clang 19 bugs** (reproductions in `tools/ps4/clang_bugs/`; not reported upstream yet): a call through a `__ptr32` function pointer loads 8 bytes or crashes the backend (LLVM 18/19 lack the 2026-11-03 fix); a `?:` mixing a `__ptr32` and a normal pointer hangs the compiler (Apple clang 16 crashed); a `__ptr32` static initializer holding an address crashes the backend. Walled off: the overlays' 13 function pointers are linux64's own 8-byte globals (`src/game/p32_fnptrs.c`; `data2s.py --host-label` renames their 4-byte PS1 slots, which nothing reads by address); one cast in `present.c`; `P32_INIT` tables filled by constructors; every compile under `timeout 300`.
- **The low image** (`SPYRO_LOW_IMAGE`): the arena is a `.bss` array (`arena.c`, with room for the presenter's area), the game runs on a thread whose 8 MB stack is a `.bss` array (`src/platform/linux32/low_image.c`), and `main` refuses to run unless the image, the arena and the stack lie below **2 GB**, not 4: the decomp's `(int)` casts sign-extend when they become pointers again. The PAL region data's block copies come from a `.bss` pool (`region_data.c`). Nothing of the host's lies below 4 GB: a constructor reserves every free page there, and `malloc` and its family are `low_image.c`'s, one `mmap` per block. `M_MMAP_THRESHOLD` 0 wasn't enough: static glibc starts its heap with `sbrk` right after the image before any constructor runs, and serves small blocks from it (measured: `malloc` at 0x2254230).
- **Checks** (tools in `tools/ps4/`): `p32_lint.py` (every pointer declared under a PS1 label's name has `P32`: 0 found); `layout64.py` (every struct in both builds' DWARF has the same size and offsets but 65 host types listed with reasons in `layout64_host_only.txt`); `cast_audit.py` (260 pointer/integer casts listed in `logs/p1/cast_audit.txt`: the hand C's u32 addresses, exact for the game's memory; a host pointer among them would crash, as above).
- **Verified** (logs gitignored, on the Mac):
  - `tools/host_checks.sh` on linux64 (`logs/p1-l64-{demo,whole,routeA,routeB}`) against linux32 native-mem from the same tree (`logs/p1-l32n-*`): every trace, shot, WAV and memory card byte-identical (26, 141, 56 and 56 files), and the logs identical once the folder name is set aside. The whole-game tour is 100,000 vblanks through every level, both endings and the credits.
  - PAL (`disc/files-pal`), which the host checks don't reach: `--region-check` gives the same counts on both (1427 pointers relocated into linux64's `.bss` pool), and a 3000-vblank run's trace, shots and audio are identical (`logs/p1-l64-pal`, `logs/p1-l32n-pal`).
  - linux32 unchanged: built from a85492b and from this tree with the same paths, build folder and `SOURCE_DATE_EPOCH` (`logs/p1/sha1-samepath.txt`), `.text`, `.data`, `.rodata`, `.bss` and the PS1 labels are byte-identical; the files' SHA-1s differ only by debug info (the `P32` tokens move some declarations' columns) and the build-ID note that hashes it. Native-mem linux32 the same way (`logs/p1/sha1-compare.txt`).
  - The Vita (VitaSDK on the Mac, a85492b against this tree, `logs/p1/vita*.log`): compiles and packs; `.text`, `.data` and `.bss` byte-identical, `.rodata` only by its two `__DATE__`/`__TIME__` stamps. Not run in Vita3K or on hardware (nothing it runs changed).
  - linux32's ctest, all 32 tests, on the arena build of this tree: passed (`logs/p1/ctest-l32.log`).
  - `layout64.py`: 447 types, 0 mismatched outside the 65 host types (`logs/p1/layout64.txt`); `p32_lint.py`: 0 (`logs/p1/p32_lint.txt`); `cast_audit.py`: 260 sites (`logs/p1/cast_audit.txt`).
- **Not verified:** a PS4 (P2 on); clang 18 (OpenOrbis's LLVM): only clang 19 here, with the same three bugs expected; linux64's save states (not in the host checks; the 13 function pointers aren't in a state, and `SetOverlayPointers` sets them at each level load); the options linux64 doesn't build (`--replay`, the PsyCross reference checks, ctest's units: linux32 only).

### D175. Draw distance 2x keeps to a 60 Hz work budget, and pass 2's mobys use the low-detail model (#180)

- **Why:** the owner plays at 2x with 60 Hz logic and loses frames. Their hardware log (`logs/owner-20261120-1056`) shows the logic draw tracking primitive bytes at about 0.2 ms a KB: every picture shown up to about 47 KB, pictures lost from about 52 KB. The drops fall where the limiter swings between x1.1 and x1.5 over scenes already heavy at 1x.
- **Measured** (Vita3K, `owner60`, vblanks 6,600–9,000, against `draw_distance=100`; `runtime/autopilot/runs/auto-180-1`):
  - 2x added gr.draws +8.5%, ot.packets +5%, vc.proj +2.9%, triangles +2.3%.
  - Almost all of it is `moby_cull_hand.c`'s pass 2: 1–8 extra mobys a draw, many draw calls each. Terrain's scaled LOD adds about 1.8 points of draw calls; the sky nothing.
  - Every pass-2 moby is at least 4 PS1 px in radius on screen, so a screen-size cull would remove visible objects.
- **Chosen:**
  1. Pass 2's mobys take the low-detail model: the record's detail is −1 where it would be positive, with no fade and no extra fog. That is the original's look past its detail distance, applied only beyond the original's draw distance.
  2. After a 60 Hz update in play (tick60's `wants_60`), the limiter also keeps the draw's primitives under a 48 KB budget (`g_lod.budget`, `LOD_BUDGET_60`): it halves the extra distance above it, drops it above 9/8 of it, and lets it grow again only under 7/8. Elsewhere (the title, attract demos, 30 Hz) only the buffer limits apply. A first try that applied the budget at the title too held the flyby at 1x and cost 5–14% more projections there.
  3. The lod log line reads "N back-offs (M for the budget)".
- **Checked** (native VitaSDK build exit 0; `noassets`; gate PASS):
  - `draw_distance=100` is unchanged: shots identical on title (10/10), pause-ws, pause-ws-wobble, wobble60 and `owner60` at 100 (91/91 each), counters identical.
  - `owner60` at 2x, vblanks 6,600–9,000: gl.draws −6.0%, gr.draws −5.5%, gl.programs −5.6%, gl.state −2.8%, vc.misses −11.1%, nothing worse.
  - `owner60`'s shots differ in 8 of 91, only in the far band: rows 8–60 at 7,700 (the far hedge and castle), and the distant terrain in the title flyby (600–1,200). The LOD boundary sits at a different distance; no near geometry changes.
- **Open:** not checked on hardware yet. The budget has no off switch outside the code yet (#185). Scenes already over about 52 KB at 1x still drop frames with 60 Hz logic (#187); the sky costs 2.5–3.3 ms a vblank (#186).

### D176. With the fix on, a quad whose one missing vertex is a world grid's moved inner point is drawn with that point's own projection: the frame's order of projections names it (#183)

- **Why:** with PSX WOBBLE OFF, quads with a vertex the cache had no entry for were drawn the PS1's way (affine, integer), and the owner saw textures stretch and change (`logs/owner-20261120-1349`). Tagging every no-entry vertex by renderer on the host showed the world renderer's were 99.5% `GridCorrect`'s inner points, which it moves on screen after projecting them.
- **What:** each table logs its frame's projections in order. For a quad with one no-entry vertex and three found, the missing corner is the projection at a + b − o in that order: o is the opposite corner, and a and b are its grid neighbours, one 1 step on and the other 3 or 5 on. It is used only when exactly one answer comes out and its entry is a valid single point. The vertex is then drawn there with that projection's depth (`VREF_CLAMP`), and the place is kept per key for the frame so quads that share it don't crack. Otherwise the vertex stays at its key, with a depth from the plane through the other three. The depth choice became `choose()`, with the same arithmetic. New counters `derived` and `der_snap`, with a `vcache derived:` log line.
- **Checked:**
  - **Same answers elsewhere:** every polygon without a no-entry vertex keeps the same answers (a host checksum of refs and side answers against 474d375, 6.0 M and 4.4 M polygons).
  - **Accuracy:** 99.1% of derived points land within 2 px of `GridCorrect`'s true point, and none land on a wrong one.
  - **Build and pictures:** native build, `noassets` and the gate pass. The five routes' replay shots are the same (the software picture).
  - **The GPU's own picture** (`CAPTURE=1`, `owner60`'s lines at 6,900, 7,200, 7,800 and 8,400; `runtime/autopilot/runs/auto-183-1/capture`) changes only on the close subdivided walls and ground, with no cracks.
  - **`owner60`, vblanks 6,900–9,000:** no-entry 201,814 → 129,236; polygons found whole 93.95 → 96.10%; `vc.misses` −26.3% (−25.5% on `wobble60`). `gl.draws` is the same; `vc.lines` +9–10% and `vc.div` +3–4%.
- **Left:** moby shadows' nudged corners (~33k on the host route), the sky's portal clipping (~28k), glow rims (~18k) and the world's split midpoints (~500) are each moved in screen space by their renderer, which has to hand over the derived point (#189). 2D polygons stay as they are. A by-construction path, where each renderer hands every vertex to its primitive with no lookup, would be about 15 files and 2–3 weeks; it waits for the owner.

### D177. The sky gives the vertex cache only the vertices its drawn triangles use (#186)

- **Why:** the owner's 60 Hz hardware log shows the sky at 2.5–3.3 ms a vblank. That figure counts `SkyDraw` twice: `prof_draw.c` charges `sky` for both `func_80050BD0` and `func_8004EBA8`, which the first calls. The cost comes from the wobble fix: the hardware runs ap1 and ap2 (D172) give 0.46 → 1.21 ms with it on.
- **Measured** (Vita3K, `owner60`, a temporary timing build):
  - Projecting with the vertex-cache hook is 87% of `SkyDraw`, and the hook's record is about 94% of that: 18–33 µs a call for the projection alone against 367–665 µs with the hook.
  - Primitive building is 3%. The GL side is one native chain of untextured F3/G3 records, so it stays as it is.
- **Chosen:**
  - Pass 2 projects without the hook.
  - After the drop test, it records only the vertices used by triangles not wholly off one side. They go in vertex order, through `g_psyxRtpValueHook`, with the register array built once a draw.
  - Records of dropped sectors and of vertices used only by culled triangles were never looked up.
  - Pass 1's sector-centre records stay. Dropping them changed 9 lookups on the host route, where world vertices matched a centre's key.
- **Checked:**
  - Native build, `noassets` and the gate pass.
  - The five routes' replay shots are the same as `base-5cdfccb`, and seven `CAPTURE=1` window grabs on `owner60` are byte-identical.
  - On the host route, every lookup count is the same, and `--fast-render-check` and `--native-draw-check` show 0 differ.
  - `owner60` (6,600–9,000): vc.proj −6.4%, vc.div −4.1%, vc.probe −3.9%, vc.lines −3.8%; nothing worse.
  - The sky's Vita3K time is about −35% (two pairs of timing runs; 1.59–1.68 → 1.05–1.09 ms, still double-counted).
- **Open:** a hardware run, and fixing the double count in `prof_draw.c`. The vertex cache's per-record cost is most of every renderer's cost with the fix on: env 0.81 → 2.07 ms on hardware (ap1/ap2).

### D178. With the fix on, an ambiguous key's vertex takes its depth from the projections under it near its polygon's others in the frame's order; the world grids' ×16 points get their true-scale depth; the Vita's vcache report no longer resets counters another thread adds to (#181)

- **Why:**
  - The owner saw textures "stretching and changing" (`logs/owner-20261120-1349`). Polygons switched between perspective and affine when a vertex's key held more than four points: far mobys and far ground at `draw_distance=200`.
  - Big close grid quads were drawn affine as "depths 4× apart".
  - The 44% scene in `logs/owner-20261120-1056` (vblank 3,600) was the report's `memset` reset racing the parse thread's walk. A lost reset carried 422,855 polygons into the next window.
- **What:**
  - Each table logs the answer of every projection under a key with different points (`lc`, `LOG_LC`). `resolve` takes the ambiguous vertex's projections within 256 log places of its polygon's found vertices, then the usual depth choice (`choose`). The position stays the key's integer one, so no cracks. Keys with a clamped or invalid projection, more than 32 projections, or a full log stay unresolved.
  - The ×16 precision candidate also accepts VY ≡ 15 (mod 16) when VX < 0. That's `GridVertex`'s borrow: x << 4 and y << 20 share one word.
  - `psx_vcache_stats_take` gives each report the counters since its last call, replacing the Vita's reset (also #184).
  - New counters `resolved` and `res_order`, and a `vcache resolved:` line.
- **Checked:**
  - **Host:** every polygon found before keeps its drawing, except 25 (route A) and 22 (route B) whose vertex or derived point gained its true-scale depth through the borrow rule; nothing found before became affine.
  - **Flips:** perspective/affine flips for polygons at the same coordinates in consecutive frames went 1,736 → 108 (A) and 512 → 10 (B). Frames with identical projections change no answer.
  - **Vita3K:** the five routes' replay shots are the same as `base-5cdfccb`; the gate passes.
  - **`owner60`, 6,900–9,000:** ambiguous vertices 77,583 → 851, polygons found whole 96.10 → 98.10%, depths 4× apart 8,561 → 827. `vc.misses` −39.1% (−20.5% on `wobble60`); `vc.div` and `vc.lines` +2–4%; GL counters the same.
  - **GPU picture:** `CAPTURE=1` grabs at 6,900, 7,200, 7,800 and 8,400 (`runtime/autopilot/runs/auto-181-1/capture`) change only on close ground and walls, with no cracks.
- **Left:**
  - 1,144 (route A) and 866 (route B) ambiguous vertices: clamped or invalid projections sharing the key, more than 32 projections under a key, or the polygon also has a no-entry vertex.
  - Among same-batch points on one pixel (a far moby), the depth choice decides and can't be proven right. Those polygons are mostly under 8 px.

### D179. Hardware session 3: #180, #183 and #186 on the Vita (2026-11-20)

- **Run:** with the owner's yes, AP8–AP10 (`tools/vita/fps-runs/ap8-owner60-hw.txt`, `ap9-owner60-hd.txt`, `ap2-toasty60-wobble.txt`; `runtime/autopilot/hw-session3.sh`, logs `logs/hw-ap8a`–`hw-ap10`). Builds: `autopilot-b604db4.vpk` (before) and `autopilot-ca4450e.vpk` (with #180, #183 and #186; not #181), US disc.
- **AP8 (owner60's lines; 16:9, 60 HZ, wobble fix on, `draw_distance=200`), b604db4 → ca4450e, vblanks 2,400–9,000:**
  - sky 1.54 → 1.03 ms a vblank (−33%, D177; both figures double-counted, #190); env, mobys and spyro the same;
  - polygons found whole 96.36 → 98.20%, no-entry vertices 318,585 → 162,858 (D176);
  - 0 late vblanks in both. This route stays in light Artisans scenes, so #180's budget backed off only twice.
- **AP9 (GPU pictures, `shot_hd=1`, at 6,900, 7,200, 7,800 and 8,400):**
  - the software shots are identical;
  - the HD pictures differ in 1.3–20.6% of pixels, only on the close subdivided walls and ground, as in Vita3K's `CAPTURE` grabs (D176), with no cracks.
- **AP10 (the Toasty route at 30 Hz with in-between frames, wobble fix on), against AP2 (D172, the same morning):**
  - the logic draw got cheaper (9.7 → 8.5 ms a tick in Toasty), and Artisans' late vblanks per 300 went 25.4 → 19.8;
  - but the presenter thread rose 8.63 → 9.11 ms a vblank and saturates in Toasty's heavy opening (96%). There it misses every in-between frame for about 25 s, so Toasty's late vblanks per 300 went 1.2 → 30.7 (#193). The owner's 60 HZ settings don't use the presenter in play.
- **Left on the Vita:** ca4450e's eboot (SHA-1 54c274d0, READBACK IDENTICAL).

### D180. The PS4 toolchain: OpenOrbis v0.5.4 with clang 18, natively on the Mac and in arm64 Docker; on a PS4 the low-image model holds (PS4 plan P2, 2026-11-20/21)

- **Asked:** P2 of `docs/ps4-plan.md` (the session that ran P1). Numbered D180, not D175: the autopilot branch's `decisions-3.md` already holds D173–D176 of its own (it collides with main's D173 and D174 too), so this leaves it room.
- **Approved** by the owner (2026-11-20/21): OpenOrbis as a new dependency, set up both natively on the Mac and in an arm64 Docker image of ours ("I'd like to play native on Mac too"); each hardware run (an explicit yes before every upload) over GoldHEN's FTP (192.168.1.26:2121; the owner installed and ran each build). The PS4: a second-generation Slim (not a Pro), firmware 13.04 (the spike's log); GoldHEN version not given.
- **Toolchain** (`docs/ps4-build.md`): OpenOrbis PS4 Toolchain v0.5.4 (`toolchain-llvm-18.tar.gz`, SHA-256 `3c7cd5bb…4526`; GPL-3.0, with musl MIT and libc++ Apache-2.0+LLVM exception inside), create-fself v1.3 and create-gp4 v1.0 (GPL-3.0), LibOrbisPkg's PkgTool.Core 0.2.231 (LGPL-3.0). OpenOrbis ships no compiler: LLVM 18.1.8's official arm64 macOS release on the Mac (`tools/ps4/sdk.sh`, SHA-256 checked, into `~/.local/share/spyro-ps4`; OpenOrbis's x86-64 macOS tools under Rosetta 2), Ubuntu 24.04's clang/lld 18.1.3 in `tools/docker/ps4.Dockerfile` (create-fself and create-gp4 built for arm64 from pinned commits, PkgTool.Core on .NET 8 arm64). Homebrew's `llvm@18` was tried first and dropped: no bottle for macOS 14, so it builds Python 3.15 and LLVM from source (stopped after 10 minutes).
- **Licence, for P5:** a game fpkg links OpenOrbis's `crt1.o`/`crtlib.o` (GPL-3.0, no linking exception), musl and the library stubs, and ships its `libc.prx`, `libSceFios2.prx` and `right.sprx`. Our sources (MIT, the decomp CC0, PsyCross MIT) are GPL-compatible, but whether the released fpkg must be offered under GPL-3 is the owner's call at P5. The spike isn't released.
- **`__ptr32` under LLVM 18** (`tools/ps4/clang_bugs/` for `x86_64-pc-freebsd12-elf`, `-fms-extensions -O1`; `logs/p2/clang_bugs/`): the three clang 19 bugs of D174 are all in 18.1.8. A call through a `__ptr32` function pointer reads 8 bytes from the 4-byte slot (`jmpq *g_fn(%rip)`) or crashes the backend; the `?:` mix crashes the backend ("Cannot select: i32 = bitcast", as Apple clang 16; clang 19 hung); the address-holding static initializer crashes it. P1's workarounds (the cast, `P32_INIT`, no `__ptr32` function pointers) compile correctly. So LLVM 18 is usable with P1's walls; no newer clang is needed yet.
- **The spike** (`tools/ps4/spike/`, title ID `SPYT00001`, `IV0000-SPYT00001_00-SPYROP2SPIKE0000.pkg`, no game data, a plain grey `icon0.png`): built like OpenOrbis's samples (PIE linked at 0 with its `link.x`), with the Piglet sample's PAID and auth info (the game will need Piglet). It logs to `/data/spyro-spike/spike.log` and as notifications: the image (`__text_start`, `.data`, `__bss_start`, `_end`), a 2 MB `.bss` arena, a thread on a 1 MB `.bss` stack (`scePthreadAttrSetstack`, and `pthread_attr_setstack`), a `__ptr32` round trip, `malloc`/`mmap`, `sceKernelMapFlexibleMemory`/`MapDirectMemory` at 0x80000000 (hint and `MAP_FIXED`), the modules and the whole virtual memory map, and a verdict.
- **Reproducible:** two builds on the Mac, and two in Docker, give byte-identical `spike.elf`, `eboot.bin`, `param.sfo` and `.pkg` (`logs/p2/sha/`), once `create-fself` gets relative paths (it writes them into the eboot) and `pkg.gp4`'s `volume_ts` is the commit's time (create-gp4 stamps the build time). The Mac's build and Docker's differ from each other: clang 18.1.8 against Ubuntu's 18.1.3 gives different code, not only a different `.comment` (same `.data` and `.bss` sizes). The official LLVM 18.1.8 aarch64-linux release in the image would make them match, at ~1 GB more image.
- **On hardware** (logs gitignored: `logs/p2/hw-run2/klog.txt`, `logs/p2/hw-run3/`):
  - Runs 1 and 2 crashed before `main` (CE-34878-0). GoldHEN's klog server (port 3232, recorded with `nc`) gave the reason: the eboot was loaded at 0x400000, then the dynamic linker refused it (`reloc_non_plt() failed ... rv=8`, `PRX_INVALID_IMAGE`). create-fself sizes the read-write `PT_LOAD` as the `.data*` sections plus `.bss`'s size, as if `.bss` began right after them; the spike's 16 KB-aligned stack arrays left a 0x3e20 gap, so the mapping ended at base+0x428000, short of `.bss`'s end (0x428a70), and a musl relocation pointed past it. Fixed by 64-byte alignment; `tools/ps4/oelf_check.py` now does create-fself's sum after every link and fails the build if `.bss` would be cut short. The crash reporter deletes the core dump; the klog is the way to see a crash.
  - **Run 3 (commit 5da3c11): the low-image model holds.** The image at 0x400000..0x819130 (text 0x400000, data and `.bss` 0x418000..0x81c000: the PIE linked at 0 goes to the PS4's own base, so no non-PIE link is needed); the 2 MB `.bss` arena at 0x4182c0; a thread created with `scePthreadAttrSetstack` on a 1 MB `.bss` stack at 0x618300 runs there (a local at 0x7182bc), and `pthread_attr_setstack` works too; the `__ptr32` field round trip is right. Everything the game addresses is below 2 GB.
  - The host's memory is far above 4 GB: `malloc` and `mmap` at 0x2_0000_0000 (8 GB), the main thread's stack at 0x7_eedf_c000..0x7_eeff_c000, the system libraries at 0x8_0000_0000. A host pointer truncated into a `P32` field points at nothing mapped, as on linux64.
  - Mapping low works, against the research's "nobody has shown it": `sceKernelMapFlexibleMemory` at 0x80000000 and 0x40000000 returns that address with and without `MAP_FIXED` (`MAP_FIXED|MAP_NO_OVERWRITE`, 0x10|0x80), and `sceKernelMapDirectMemory` at 0x80000000 too (under the Piglet auth info). The low-image model doesn't need it; it is a fallback if the image ever has to shrink.
  - The `sceKernelDebugOutText` mirror showed only the first line in the klog; the file log has everything.
- **Not verified:** a PS5; other firmware; the spike under the default auth info (it always ran with Piglet's).

### D181. `prof_draw.c`'s `sky` counts the sky once; the main draw's own time is "sky main" in the line's bracket (#190)

- **Why:** `func_80050BD0` (`cyclorama_hand.c`) calls `func_8004EBA8` (`sky_hand.c`), and the `--wrap` catches that call. Charging both wraps to `sky` counted the main draw twice (D177). So every earlier `sky` figure, hardware and Vita3K, is about 1.85× the real cost. That includes the owner's 2.5–3.3 ms, and D177's and D179's numbers.
- **What:**
  - Both wraps stay. The nested call isn't charged to `sky`.
  - `func_8004EBA8` still counts on its own when `draw.c`, the title or the credits call it directly.
  - The parts are unchanged and add up.
  - The main draw's own time (either caller) is `sky main` in the bracket, which `tools/vita`'s parsers cut off at `(`. That's why there's no seventh part.
  - Docs: `vita-testing.md`, `presentation.md`, `fps-runs/README.md`.
- **Checked** (Vita3K, `runtime/autopilot/runs/auto-190-1`; native build, `noassets`, gate PASS):
  - The five routes' shots and every work counter are the same as `base-cc65182`.
  - `owner60` (6,600–9,000) against 47e108a: sky 0.97 → 0.53 ms a vblank, the other parts 4.40 → 4.53, sky main 0.46. The main draw is about 87% of the sky.
  - To compare with older logs, old ≈ new `sky` + `sky main`.
- **Left:** the host's `src/platform/linux32/prof_draw.c` has the same double count.

### D182. With the fix on, the hand renderers hand the cache the vertices they make in screen space (#189)

- **Why:** after D176 the remaining no-entry vertices came from renderers that move or make a vertex after the projection. Tagged on host route A, these were:
  - moby shadows' nudged corners (33k);
  - the sky through portals and the portal face (28k);
  - glow rims (18k);
  - the world's split midpoints (0.7k).
- **What:**
  - **The hook:** `PSX_VC_DERIVE(how, sxy, a, b, p, q)` calls `psx_vc_derive`, which calls `g_psyxRtpDeriveHook`. The hook is NULL unless the cache records. The renderers call a direct function, because `tests/pres/link_check.py` bans indirect calls in presenter code. The presenter's copy is `g_pres_rtp_derive_hook` (`render_ctx_names.h`), set with its value hook by `psx_vcache_pres_begin`.
  - **Storage:** each table keeps up to 1,024 hand-overs a frame. They're used only for a vertex with no entry, and only after D176's derivation. A key placed differently by two hand-overs is dropped.
  - **Four forms:**
    - **alias:** a shadow's nudged corner takes its source's answer;
    - **offset:** a glow rim is the centre's exact place plus the screen offset, at the centre's depth;
    - **the 3D point at t:** `SplitMid` (t = ½) and `ClipEdge` give s = ((1−t)·sa·za + t·sb·zb)/((1−t)·za + t·zb), w = (1−t)·za + t·zb;
    - **a 2D cut:** the sky through portals and the portal face are cut where the exact side crosses the portal edge's line. Depth is 1/w along the side, or the place only (flat F3).
  - **Sources:** a source is a valid entry. A multi-point key gives its pixel centre and all its depths.
- **Checked:**
  - **Same drawing:** every polygon found before keeps its drawing. A host FNV comparison in one binary showed 0 changed and 0 now affine (route A: 6.09 M polygons, 28,484 newly found; route B: 8.96 M, 3,847).
  - **Host checks:** `--fast-render-check` and `--native-draw-check` give 0 differ on both routes, with `--vcache` so the hooks run.
  - **Builds and gate:** native build, `noassets`, gate PASS.
  - **Replay shots:** the same on all five routes against `base-cc65182`.
  - **`owner60`, 6,900–9,000:** no-entry 102,643 → 8,763; polygons found whole 98.10 → 99.62%; `vc.misses` −93.8% (−92.5% on `wobble60`); `gl.draws` the same; `vc.probe` and `vc.lines` +3–5%.
  - **GPU picture:** `CAPTURE=1` grabs at 6,900, 7,200, 7,800 and 8,400 change only on shadows, glows and the portal edge, 0.03–0.69% of pixels, with no cracks. At 7,200 the grey sliver and green speck at the portal's edge are gone.
- **Left:**
  - 2D polygons (HUD, title, fade) stay as they are, by design.
  - The portal face's 2D screen corners.
  - Hand-overs whose source is itself ambiguous or invalid (about 370 on route A).
  - Not run: the oracle replay (no PCSX-Redux here). `docs/translation-log.md` rows for the five renderers.
- **Found on the way:** ctest `render_ctx_link` has failed since #180 (D175): `lod_pres.c` names `g_tick60_mode` and `tick60_built`. The gate's `host_tests` command is still a placeholder, so the gate didn't run ctest (#194).

### D183. Draw distance's 60 Hz budget has an off switch: `dd_budget=0` on the Vita, `--lod-budget 0` on the host (#185)

- **Why:** #180 (D175) added a 60 Hz work budget to the draw-distance limiter. It was on by default, with no switch outside the code. Phase 8's rule is "on by default with an off switch".
- **What:**
  - **Vita:** `dd_budget=` in `config.txt`. A non-zero value (the default) sets `g_lod.budget = LOD_BUDGET_60` (48 KB). `0` sets 0, which leaves only the buffer's limits, as before #180.
  - **No menu row.** A start-up line from `main.c` says which applies.
  - **Host:** `--lod-budget KB` (default 48; 0 is off).
  - **Unchanged:** pass 2's low-detail mobys stay either way, and the budget's value and rules are as before.
  - **Docs:** `presentation.md`, `host-build.md`, `vita-testing.md` and the README describe the budget and the lod line's "N back-offs (M for the budget)".
- **Checked** (`runtime/autopilot/runs/auto-185-1`):
  - The native build, `noassets`, the host build and the gate all pass.
  - With the default, the five routes' shots and counters match `base-cc65182`.
  - `owner60` with `dd_budget=0` reads "(0 for the budget)" on all 29 lod lines.
  - Against pre-#180 (`base-3ba4fd2`), its shots differ only in the 7 title-flyby shots that pass 2's low-detail mobys changed. Shot 7,700, which the budget had changed, is back.
- **Left:** `picture.c`'s start-up line and the menu still say "experimental" for 2x. Whether 2x drops that label waits for a hardware run in open levels.

### D184. The presenter's copy of `lod.c` no longer reads tick60's state; the autopilot gate runs the host build and ctest (#194)

- **Why:**
  - Since #180 (D175), `logic60()` has read the game thread's `g_tick60_mode` and `tick60_built()`.
  - `lod_pres.c` compiled it into the presenter's object, which breaks D164/D167's split. Since then, ctest `render_ctx_link` has failed.
  - The gate missed this because `autopilot/config.json`'s `commands.host_tests` was still a placeholder.
- **What:**
  - The game's copy decides "60 Hz in play" as each shown draw ends, at the same point as before, and keeps it in `s_logic60`, which is part of `LOD_STATE`. The state is 4 bytes bigger, about 416 of `pres_draw.c`'s 512.
  - The presenter's build (`RENDER_PRES`) leaves out `logic60()` and `tick60.h`. It only carries the game's last decision, in the state it is handed, and it never runs the limiter.
  - `host_tests` is now the linux32 build plus ctest. It copies `build/s1-matching` into the worktree when needed and takes about 17 minutes on a quiet machine and 35–50 under load.
  - `buildlock` (`runtime/autopilot/env.sh`) now keeps its variables local.
- **Checked:**
  - ctest went from 28 passing with `render_ctx_link` failing to 29 passing and 3 skipped (`gte`, `pbp`, `pbp_made`). The gate's own run showed 100% of 32.
  - On Vita3K, all five routes give the same shots and counters as `base-b3b8cd1`, and `owner60`'s lod lines are identical, including the budget's back-offs.

### D185. `vita-testing.md` describes PSX WOBBLE, and the bench measures the default (#182)

- **Why:** #177 (D194) made PSX WOBBLE OFF (`persp=1 subpixel=1`) the default and moved the menu to `config_version=6`. Two things lagged behind:
  - `vita-testing.md` still described the two old rows and `config_version=5`.
  - The bench had no set for the default without a filter, so it couldn't measure what players get.
- **What changed in `vita-testing.md`:**
  - The `wobble=` row, with `persp=` and `subpixel=` kept as developers' keys.
  - The rule for old config files. With no `wobble=` line and a `config_version` below 6, an equal `persp`/`subpixel` pair reads as PSX WOBBLE OFF and is dropped at the next save. Any other mix is taken as written.
  - The test tools write `wobble=1`.
  - The vcache log lines as they stand after D195–D184: clamps, derived, resolved and hooked, plus D178's per-report counters.
- **What changed in `bench.c`:**
  - Two new sets, "4:3 native, persp subpixel" and "16:9 native, persp subpixel", each inside its widescreen group. The other sets are unchanged, so older logs still compare by name.
  - A run is now 20 × 60 × 12 = 14,400 vblanks from vblank 600, with the final lines at 15,000 (about 250 s; 13,560 before).
- **Checked:**
  - The native build, `noassets`, and the gate with the host build and ctest all pass.
  - Vita3K `title`: shots 10/10, the same as `base-b3b8cd1`.
  - The bench itself hasn't run; its first hardware run needs the owner's yes.

### D186. The presenter's thread gets a cheaper vertex cache: its projections are worked out only when an entry is first read, resolve stops scanning early, and each thread's counters have lines of their own (#193)

- **Why:**
  - At 30 Hz with in-between frames, the presenter's thread saturated in Toasty's heavy opening (D179 AP10: 96%, every in-between buffer passed by).
  - It shares core 1 with the parse thread, which has the higher priority, and with audio. Its 32 ms job was about 22 ms of its own work plus 9.6 ms of walk N's preemption.
  - `records_begin`'s join runs on the game thread and costs 0 in those windows.
- **What:**
  1. **Deferred projections (`T_PEND`).** The presenter's projections record their entry and log place at once, but keep their inputs on a per-entry list (16 bytes each, registers de-duplicated). The first lookup, hand-over source or derived point works them out in order. 25–35% are never needed. `psx_vcache_set_lazy(0)` turns this off.
  2. **Early stops in `resolve`.** It only tests whether a projection is near, with an early exit, and stops counting points at two.
  3. **Separate cache lines.** `g_vcache_ctr[2]` (the game's and the presenter's work and cost counters), `VCacheStats` and `Rec` are each 64-byte aligned. Before, the presenter's cost counters shared a line with the logic draw's on another core.
  4. The tag's frame number is now 23 bits.
- **Checked:**
  - **Same answers:** host per-polygon FNV is identical to b3b8cd1 on routes A (12.25 M polygons) and B (8.98 M), with the deferral on and off. The resolve, derive and hand-over counts are identical.
  - **Same pictures:** the five routes' shots match. The gate, including the host build and ctest, passes.
  - **Counters:** `wobble60` `vcp.div` −15%, `vcp.lines` −2.8%.
  - **Vita3K, Toasty's heavy opening:** the presenter's busy time per job, against the logic draw, fell about 28% (2.11–2.89 → 1.59–1.94); about 18.5 → 10.9 ms a job in one same-session A/B.
  - The presenter's own invalid, ambiguous and clamped stats now count only the keys that were worked out.
- **Open:**
  - the hardware rerun of AP10;
  - moving the presenter off the parse thread's core, and a `vc_lazy=` key (#195).

### D187. At 30 Hz with in-between frames, the logic walk runs on the game thread while a presenter job is pending (`parse_thread=2`, the default) (#195)

- **Why:**
  - AP10 (D179) at Toasty 12,000 showed the presenter's 32 ms job on core 1. About 9.6 ms of that was preemption by walk N: the parse thread runs at 0x98, ahead of the presenter at 0x9A. Meanwhile the game thread waited about 19 ms a tick.
  - At 30 Hz with the presenter, the game thread joins the presenter and then the walk right after the handover anyway. In AP10's Artisans window, `ot` was 3.71 against the walk's 3.52 ms busy a vblank. In Toasty, the walk was done while the game thread waited 9.45 ms for the presenter. So the walk's own thread gives no overlap there.
- **What:**
  - **`parse_thread=2`:** a list handed over while a presenter job is posted and not yet joined (`vita_pres_pending`) is walked on the game thread at once, from the same copy, and the join finds it done.
  - **Walker flag:** meanwhile the game thread counts as the walker (`vita_parse_on_walker`, by its stack below the handover), so `reserve` and `env_sync` don't join.
  - **Other lists** go to the thread, as with `parse_thread=1`, which is the old placement. In the owner's 60 HZ play there is no presenter job, so `2` behaves as `1`.
  - **Logging:** the `parse N:` line gives those walks separately.
  - **New keys:** `pres_core=2` (core 2), `pres_prio=` (0x40–0xBF) and `vc_lazy=` (D186's switch). All are logged on a `threads:` start-up line.
  - **Docs:** `vita-testing.md`'s key table and `vita.h`'s comment.
- **Compared** (Vita3K, in the same session against #193's VPK; presenter busy per logic draw):

  | Setting | Toasty 11,100–13,200 | `wobble60` |
  |---|---|---|
  | before | 1.87 | 1.60 |
  | `pres_core=2` | 1.73 | 1.64 |
  | `pres_prio=0x96` | 1.79 | 1.59 |
  | `parse_thread=2` | 1.49 (−20%) | 1.42 (−11%) |
  | `parse_thread=0` | 1.52 | 1.56 |

  - `pres_core=2` was not chosen: on hardware, the render thread's 4.74 ms a vblank runs in the same window as walk N's 4.82.
  - `pres_prio` only reorders a full core 1.
- **Checked:**
  - The five routes' shots are the same as `base-b3b8cd1`, and so are all six placements' `wobble60` shots.
  - The counters are the same apart from D186's `vcp.*`, and `vc_lazy=0` restores those.
  - Native build, gate with host build and ctest (32, 0 failed), and `noassets` all pass.
- **Expected on hardware:**
  - The presenter's job goes from about 32 to about 17 ms, and core 1 runs at about 55%.
  - Toasty's game thread takes about 24 ms a tick.
  - The in-between frames should be back on time. That's borderline-likely, not certain.
  - For an A/B: `parse_thread=1`, `vc_lazy=0`. Not run on hardware yet.

### D188. The logic draw's projections are worked out by its walk while the walk has time (#179)

- **Why:** with the wobble fix on, the vertex cache's per-projection record is most of the hand renderers' cost on the game thread. That is about 5.9 ms a tick in heavy 60 HZ Artisans on hardware (D196's split), and the game thread is the bottleneck at 60 HZ logic.
- **What:**
  - **Deferral at 60 Hz.** With 60 Hz logic the game thread finds the entry, logs its place and keeps the projection's inputs (D186's `T_PEND` store, now one per table) instead of doing the maths. The walk works them out at a polygon's first lookup, at a hand-over source, or at a derived point.
    - Each logic table has its own store and far count, so the walk can finish frame N while the game records N+1.
  - **Adaptive.** A projection is kept only while the previous frame's walk is idle. The walk sets a flag on its own cache line. Otherwise the game thread works it out itself, so the walk takes only what it has time for.
    - Keeping every projection overloaded the walk. On Vita3K `owner60` at 6,900, parse went 3.4 → 7.5 ms a vblank with a 5.7 ms join, and the tick got worse.
    - Which projections are kept never changes an answer.
  - **30 Hz with in-between frames.** The logic frames work out at once, as before, because the walk shares core 1 with the presenter (D186, D187).
  - **Switches.** `psx_vcache_set_lazy_logic` takes 0 (never), 1 (auto, the default) or 2 (always). `vc_lazy=0` turns off both deferrals.
  - **Per-thread counters.** The logic draw's record counts and the walk's deferred maths go to per-thread blocks, added to the shared counters once a frame with atomic adds. Records no longer write the walk's cache lines from the other core.
  - **New counters.** `g_vcache_rec`, the `vcache records:` line, and a `vpk-perfcount-vc` line holding `vc.rec.*`, `vc.walk.*`, `vc.resolved`, `vc.derived` and `vc.hooked` (the kit's 32 main counters are taken). Read it with `perf_report.py --prefix vpk-perfcount-vc`.
- **Checked:**
  - **Host answers:** per-polygon FNV is identical to 7df4b85 on routes A and B, with deferral off, auto, forced and `--walk-lazy`. `--fast-render-check` and `--native-draw-check` are unchanged.
  - **Pictures:** the five routes' shots are the same.
  - **Gate:** PASS, including the host build and ctest.
  - **Counters (`owner60`, 6,900–9,000):**
    - projections worked out on the game thread: 3,663 → about 2,350 a vblank (−36%);
    - divisions: 3,448 → 2,232;
    - the walk works out about 890 a vblank, and about 31% of the kept projections are never needed;
    - `vc.div` −5.3%; `vc.proj`, `vc.polys` and `vc.misses` are the same.
  - **Vita3K timing (same-session pairs):** the logic draw per tick is 15–20% lower, with the join at 0.03–0.2 ms.
- **Known:**
  - The invalid, ambiguous and clamped stats count only the projections that were worked out.
  - The far array's slots are taken in another order, so a frame with more than 1,024 far positions could choose differently.
  - Store lines are kept out of `vc.lines`.
- **Open:** a hardware run, and a `vc_lazy_logic=` key for its A/B (#196).

### D189. A `vc_lazy_logic=` key for D188's logic-frame deferral, and the `vpk-perfcount-vc` line documented (#196)

- **Why:** D188 had no config key for `psx_vcache_set_lazy_logic`, so its hardware A/B couldn't be run. Its separate counter line wasn't in the docs either.
- **What:**
  - `vc_lazy_logic=0|1|2` in `main.c`'s `read_config`: default 1, clamped. It is set next to `vc_lazy=` and logged on the `threads:` start-up line.
  - `vita-testing.md` now has the key, plus a note on `vpk-perfcount-vc`: its counters, and reading it with `perf_report.py --prefix vpk-perfcount-vc`.
- **Checked:**
  - Vita3K `owner60` with `vc_lazy_logic=0`: the `vcache records:` lines keep 0, and the 91 shots match the default run's.
  - The five routes' shots are the same as `base-c489090`.
  - The native build, the gate (host build and ctest) and `noassets` pass.
- **Counters** (`owner60`, 6,600–9,000, per vblank):
  - default: the game thread works out 1,894, keeps 1,320, and the walk works out 888;
  - `vc_lazy_logic=0`: the game thread works out 3,205;
  - `vc.div` is 6.6% lower with the default.
- **Not built:** a batch hook for RTPT's three vertices. RTPT is only used by particles, shadows and flames. The upper bound is about 0.38 ms a vblank and the realistic saving under 0.04 ms. Batching the env and moby `ngte_rtps_run` loops would be the place, at about 0.25–0.35 ms at best.
- **Left for the owner:** raising the vendored kit's `VPK_PC_MAX` (32 → 48), so the vc counters can join the main line. That's a change in `../vita-port-kit` plus a re-vendor.

### D190. With the fix on, an ambiguous key resolves however many projections it has, an invalid projection counts only when it is near the polygon's others, and a clamped point the frame's order names alone is drawn at its own place (#192)

- **Why:** after D178, about 1% of ambiguous vertices stayed unresolved. Re-measured on the host after D182, D186 and D188, that was 1,104 on route A and 490 on route B. Most were keys with more than 32 projections (593 on A) or keys made ambiguous by a clamped projection (336 on A, 355 on B).
- **What:**
  - `resolve` walks the whole chain under the key, keeping only the projections it takes and only to count points.
  - An invalid projection, including a first one, blocks the vertex only when it is among the near ones.
  - Each clamped projection's own exact place now goes into the log (`LOG_CL`). Places beyond int16 go to a new per-projection array, `lfar` (1,024 a frame per table). D195's per-key `far` is untouched, so D188's slot order and answers stay the same.
  - When the near projections are all one point (within 1/8 px), a clamped vertex is drawn at that point's oldest projection's place (`VREF_CLAMP`), as D195 does for found clamped keys.
  - A clamped point among several near ones stays ambiguous.
- **Checked:**
  - **Host FNV.** One binary with a run-time switch; its old path is byte-identical to a 35121cb base. Every polygon found before keeps its drawing: 6.13 M on A, 8.98 M on B, with 262 and 250 newly found. The dumps are identical with `vc_lazy=0`, with lazy logic forced, and with `--walk-lazy`.
  - **Ambiguous on the host:** 1,104 → 447 on A, 490 → 239 on B.
  - **Cracks:** of 113 frame-keys with a placed clamped point, none was left unplaced or placed elsewhere in the same frame.
  - **Vita3K:** the five routes' shots are the same, and the gate (host build and ctest) passes.
  - **Vita3K counters:**
    - `owner60` (6,900–9,000): ambiguous 785 → 190, `vc.misses` −7.6%, `vc.div` +0.3%.
    - `wobble60`: ambiguous 245 → 129, every counter the same.
  - **GPU picture (`CAPTURE=1`):** identical at 7,200, 7,800 and 8,400. At 6,900 only the texturing inside the wall and water polygons by the waterfall changes (1.29% of pixels), with no cracks.
- **Left:**
  - **A clamped point among several near ones:** 337 on A, 124 on B.
    - On A these are far points at 2x draw distance whose depth saturated SZ3, and 314 of the 337 polygons have no found vertex to order by.
    - On B they are edge-saturated keys with 2–3 clamped points in one batch, up to 13,858 px off the key. A wrong pick would draw a long spike and neighbours could crack, so these keep the PS1's drawing.
  - **Polygons with both an ambiguous and a no-entry vertex** (90 / 77): `resolve` only runs when every missing vertex is ambiguous (#197).

### D191. With the fix on, a quad's one vertex with no entry and its ambiguous ones are found together (#197)

- **Why:** after D190, `resolve` ran only when every missing vertex was ambiguous. Polygons that also had a vertex with no entry kept the PS1's drawing: 90 on host route A and 77 on route B.
- **What:**
  - **Which quads:** one vertex has no entry and no hand-over, and the other missing vertices are ambiguous.
  - **Place:** the quad first gets derive's place. The a + b − o search walks an ambiguous corner's projections too. The projection that place names counts as one of the polygon's places for `resolve`.
  - **Depth:** `resolve`'s depths are then the third corner of derive's plane.
  - **Keeping it:** the key's place for the frame is kept only when both steps succeed.
  - **Code:** `derive` is split into `derive_place` and `derive_depth`, with the same arithmetic.
  - **Log:** the `vcache resolved:` line gives the vertices left ambiguous, by reason (`res_der`, `res_why`).
- **Checked:**
  - **Host FNV** against c737af2, using one binary with a run-time switch whose old path is byte-identical.
    - Every polygon found before keeps its drawing, and no polygon still affine changes: 6.11 M polygons on A, 8.97 M on B.
    - Newly found: 27 on A, 61 on B.
    - The dumps are the same with `vc_lazy=0`, with lazy logic forced and with `--walk-lazy`.
  - **Cracks:** no new derived vertex is placed elsewhere in its frame. One is left at its key ahead of later polygons, which D176 allows.
  - **Pictures:** the five routes' shots are the same. The `CAPTURE=1` grabs at 6,900–8,400 are byte-identical; no fixed polygon falls on those frames.
  - **Builds:** the gate passes (host build and ctest), plus the native build and `noassets`.
  - **Host ambiguous left:** 447 → 420 on A, 239 → 178 on B.
- **Left:**
  - **Hand-overs dropped because their source is ambiguous:** 63 vertices on A, 16 on B.
    - Glow rims round an ambiguous centre would draw the same either way.
    - A world split or sky cut from an ambiguous end would be placed by each polygon's own depth for that end, so neighbours could crack.
  - **Ambiguous portal outline keys:** taking one at its pixel centre exactly places 354 more sky-clip vertices on A, but moves 281 polygons that stay affine. That picture change is the owner's call.

### D192. Hardware session 4: round 9's build (63d80f4) on the Vita (2026-11-21)

- **Run:** with the owner's yes ("vita is available for you, go"), AP11–AP14 (`runtime/autopilot/hw-session4.sh`, logs `logs/hw-ap11`–`hw-ap14b`). Builds: `autopilot-ca4450e.vpk` (D179's) and `autopilot-63d80f4.vpk` (D196–D191), US disc. New run file `tools/vita/fps-runs/ap14-owner60-toasty.txt`: the owner's settings (16:9, 60 HZ, the wobble fix on, `draw_distance=200`) on the Toasty route in real time.
- **AP14 (owner's settings, Toasty route), ca4450e → 63d80f4, play windows 11,700–18,000:**
  - late vblanks per 300 in Toasty's heavy opening (12,000–12,600): 78 → 0; over 13,500–15,300: 5–51 → 0;
  - game thread busy 99% → 63–95%. This is D196's walk overlap at 60 HZ, as expected there.
  - Artisans before the warp (11,100–11,400): late 106–109 → 2–3 per 300.
  - The run is in real time, so the two builds' play splits after 15,000 (63d80f4 reaches game over at 18,008; ca4450e doesn't).
- **AP11 (AP8's owner60 lines on the pause route) against AP8b:** polygons found whole 98.63 → 99.27%, the game thread's vertices not found 5,585 → 2,920; game thread busy 60.4 → 52.9% (worst 96 → 81); 0 late vblanks in both.
- **AP12 (`shot_hd=1`) against AP9b:** the software shots are identical. The GPU pictures differ in 0.9–4.0% of pixels, only on the close walls and the ground beside them (D190, D191); no cracks.
- **AP13 (Toasty at 30 Hz with in-between frames) against AP10 (D179):** Toasty's late vblanks per 300 30.7 → 3.5 (#193, #195). The presenter still runs at 95% in Toasty's opening (12,000–12,600) but no longer misses frames.
- **Regression (#198):** on the game-over screen in Artisans (gamestate 5, reached at ~19,350 by both AP10 and AP13), the new build settles from 20,100 into exactly 60 late vblanks per 300. The render thread takes 16.65 ms a picture, of which 11.5 ms is its present; it skips the draws of ~180 in-between frames per 300. The presenter is busy 91% (7.45 → 15.4 ms a vblank) for the same work counters, and drops ~200 game-thread frames from a full queue. ca4450e (AP10) holds 0 late on the same screen. AP14b shows the same lock after its own game over (19,500–21,000, 60 late per 300). For a short stretch in Toasty (13,200–15,000) AP14b's render thread also runs at 16 ms a picture, without late vblanks. The work counters don't change, so this looks like a pacing lock between the presenter, its buffers and the render thread's present rather than more work. Suspects: D187's `parse_thread=2`, D186's lazy presenter. Not bisected: each run needs the owner's yes.
- **Left on the Vita:** 63d80f4's eboot (SHA-1 f102a69c, READBACK IDENTICAL).

### D193. The PS4 platform layer: the game's fpkg runs on a PS4, software-rendered to sceVideoOut first; Piglet with the player's own modules next (PS4 plan P3, 2026-11-21)

- **Asked:** P3 of `docs/ps4-plan.md` (the session that ran P1 and P2). Numbered D193, not D181: the autopilot branch's `decisions-3.md` already reaches D192.
- **The owner's choices** (2026-11-21):
  - **Renderer:** "B + soft fallback". The GLES2 renderer on Piglet compiles our GLSL at boot with the player's own `libScePigletv2VSH.sprx` and `libSceShaccVSH.sprx` in `/data/self/system/common/lib/`, as 2Ship2Harkinian-PS4 and Ship of Harkinian do, and as our Vita build asks for `libshacccg.suprx` (D163). Without them the software rasterizer draws. The software path came first.
  - **Testing:** hardware only, no emulator.
  - **Research** (a subagent, 2026-11-21) found no shader compiler we may legally use offline:
    - ioQuake3-PS4 and PacBrew's SDL-PS4 ship blobs dumped on a console with `glPigletGetShaderBinarySCE`, compiled by Sony's compiler (option C, not taken).
    - `sleirsgoevy/piglet-shaders` is a 2022 Mesa proof of concept with no licence that compiles fragment shaders only.
    - OpenOrbis's Piglet sample uses the firmware's WebKit shaders (`libScePrecompiledShaders.sprx`), which can't draw PS1 primitives.
    - shadPS4 has no Piglet, and its macOS build needs macOS 26 (this Mac runs 14).
  - The two modules weren't on the owner's PS4 (an FTP listing, 2026-11-21): the owner will copy them.
- **Built** (`-DSPYRO_HOST=ps4`, `cmake/ps4.toolchain.cmake`, `docs/ps4-build.md`):
  - **Toolchain and memory:** OpenOrbis v0.5.4 and clang 18.1.8 on the Mac; `-fms-extensions`, `P32`, `SPYRO_NATIVE_MEM` and `SPYRO_LOW_IMAGE`; `-O2 -march=btver2 -fPIE`. The eboot is a PIE linked by ld.lld with OpenOrbis's `link.x`, which the PS4 loads at 0x400000 (D180). The arena and an 8 MB game stack are `.bss` arrays, the stack set with `scePthreadAttrSetstack`. `main()` refuses to run unless the image, the arena and the stack are below 2 GB. `oelf_check.py` runs after every link.
  - **The fpkg:** `tools/ps4/pkg.sh` is the spike's steps for the game: title ID `SPYT00002`, "Spyro PS4 (test)", the grey placeholder icon, the Piglet sample's PAID and auth info.
  - **`src/platform/ps4/` files:**
    - `main.c`: start-up, the module loads, `config.txt` (frames, stats, realtime, analog, shot).
    - `vblank.c`: the Vita's real-time clock over `sceVideoOutGetVblankStatus` and `sceVideoOutWaitVblank`.
    - `video.c`: linux32's software rasterizer's display scaled 4:3 into a 1920×1080 `sceVideoOut` framebuffer on the CPU, as OpenOrbis's graphics sample draws.
    - `pad.c`: `scePad`. The PS4's bits are the PS1's; OPTIONS is START, the touch pad's click is SELECT; analog with the free camera by default.
    - `audio.c`: the SPU's 44.1 kHz resampled to 48 kHz for `sceAudioOut` on a thread.
  - linux32's `gpu_soft.c`, `png.c`, `log.c` and `savestate_host.c` are reused unchanged. Disc data comes from `/data/spyro/` (image, extracted files or the Store copy's `EBOOT.PBP`, `cdimage.c` as on the Vita); saves go to `/data/spyro/save/`.
- **What a PIE and a real Jaguar needed, PS4 only:**
  - **Data words:** the data image's 32-bit address words (67) can't be relocated in a PIE (no `R_X86_64_32`). `tools/data2s.py --pie` writes them as 0 plus an `s1_pie_words` table of {slot, target} pairs that `main()` stores before anything reads them.
  - **The presenter's commons:** llvm-objcopy's `--keep-global-symbol` also localizes common symbols, and a local common is an absolute symbol at its alignment. The presenter's `pres_draw.o` would have read the decomp's `-fcommon` globals at address 4. `tools/ps4/keep_commons.py` keeps them global; GNU objcopy never localized them.
  - **tick60's link check:** `tick60_link.py` takes `.data.rel.ro` (a PIE's constant address tables) as read-only.
  - **System modules:** the system libraries beyond libkernel aren't loaded for the eboot. Hardware run 1 crashed calling into `libSceUserService` (`PRX_NOT_RESOLVED_FUNCTION`). `main()` now loads UserService, VideoOut, Pad and AudioOut with `sceSysmoduleLoadModuleInternal`. The P2 spike only used libkernel.
  - **Misaligned arrays:** clang takes every global array of 16 bytes or more as 16-byte aligned (x86-64's large-array rule, `extern` declarations and commons included), but the decomp's arrays sit at their PS1 addresses. Hardware run 2 crashed starting a game on `movaps` to `g_FlightCourseRecords`, which is 8-aligned (qemu-x86_64, linux64's checker, doesn't enforce SSE alignment). No clang or LLVM option, and no `#pragma clang attribute`, lowers it. `tools/ps4/cc_unaligned.py`, the compiler launcher, compiles each C unit with VEX SSE (`-march=btver2`), rewrites `vmovaps`/`vmovapd`/`vmovdqa` to their unaligned forms, and fails on a non-temporal or non-VEX aligned move. Only musl's own vararg saves keep aligned moves.
  - **A fourth `__ptr32` bug in clang 18:** with AVX the loop vectorizer turns `P32_INIT`'s copy loops into a vector `addrspacecast` the backend can't select ("Cannot select: v4i32 = addrspacecast"). The PS4 builds with `-fno-vectorize`.
  - **Audio's user:** `sceAudioOutOpen` refuses the game's user (0x809b0001) and opens as the system user (0xFF), as SDL-PS4 does.
- **On hardware** (the owner's PS4 Slim, firmware 13.04, GoldHEN; FTP 192.168.1.26:2121; the owner said yes to each upload and installed and ran each build; the owner's disc image uploaded to `/data/spyro/` with the owner's yes; logs gitignored in `logs/p3/hw-run{1,2,3}/`):
  - **Run 1 (9f6312b):** the module crash above.
  - **Run 2 (d58b0cb):** the title screen, the controller working ("it responded"), ~7.5 pictures a second (the framebuffer was read back: write-combined memory, now never read), no audio (0x809b0001). Starting a game crashed (the alignment above).
  - **Run 3 (8c6673b):** the game starts and runs ("the game starts and runs"). 60 vblanks a second, 0 late, a picture each vblank. Per VSync: the game ~5 ms, scaling the picture 4.3 ms, the rest waiting for the display. Audio plays (80 dry grains since the start, none dropped).
- **Unchanged elsewhere** (13c58f4 against 8c6673b, built from the same worktree path and build folders; `logs/p3/sha/result.txt`):
  - linux32 (SHA-1 `4fb114f5…`) and linux64 (`6fb54da5…`) are byte-identical.
  - The Vita's `.text`, `.data` and `.bss` are identical; its `.rodata` differs only by the version stamp (`v1.2.2-…-g<commit>`) and `__TIME__`.
  - `tools/host_checks.sh` on linux32 from both commits gives byte-identical traces, shots, audio and cards: demo 28, whole-game tour 143, routes A and B 58 files each, every run exit 0 (`logs/p3-l32-{base,head}-*`).
- **Not done or not verified:**
  - The GLES2 renderer on Piglet. It waits for the owner's modules and moves to P4 with 60 fps and widescreen, which need it.
  - Whole-game play, saves and the Store copy on the PS4 (P4).
  - The PS5.
  - The module load's need on other firmware.
  - Why `sceKernelDebugOutText` lines don't reach the klog. The file log has everything.

## 2026-11-20 — one PSX WOBBLE row, no wobble by default (#177)

### D194. The options menu's PICTURE section has one row, PSX WOBBLE (OFF / ON, `wobble=`), in place of PERSPECTIVE TEXTURES and SUB-PIXEL VERTICES; OFF, the default, is the fix (both on)

- **Why:** the owner (2026-11-20): one switch, and no wobble by default. Players didn't know the fix needs both rows on, and both were off by default.
- **What:** ON is the PS1's look (`persp=0 subpixel=0`), OFF has both on. The menu writes `config_version=6`. `persp=` and `subpixel=` stay as developer keys (as D153 kept `tex_filter=`/`dynres=`): a line wins over `wobble=` for its own half; when the halves agree the row shows it.
  - **Old menu files** (no `wobble=` line, every `config_version` line below 6): the pair `persp=1 subpixel=1`, or `persp=0 subpixel=0` (the old default), reads as wobble off, logged, and the pair is dropped at the next save; any other mix is taken as written. The highest `config_version` line decides here, because the test tools now write `config_version=6` and `wobble=1` first and a run file's own later `config_version=5` (AP1's `persp=0 subpixel=0`, say) must not read as an old player file; D68, D150 and D153 still use the last line.
  - **Test runs compare with the host as before:** `hw.game`'s `hw_config_base` writes `config_version=6` and, with the host lines, `wobble=1` (not with `DEVICE_DEFAULTS=1`, which now means the fix on); a new `v3k_config_base` hook writes `realtime=0` and `wobble=1` for the kit's `v3k_run.sh`; `tools/vita/vita3k_run.sh` writes both too. A route's own `persp=1 subpixel=1` still turns the fix on.
  - README: the one row, its default, the 60 fps cost under PSX WOBBLE OFF, and the programs compiling at start-up.
- **Cost:** the default now runs the vertex cache and its 40-byte vertex; with it the presenter's in-between frame runs on core 1 (D167). PSX WOBBLE ON gives that back. On hardware the fix cost the game thread 2.6 ms a vblank at 60 fps (D172); #179 and #178 work on that and on what it still misses.
- **Checked** (Vita3K, `runtime/autopilot/runs/auto-177-1`; native VitaSDK build exit 0; `noassets`): a `config.txt` with only `realtime=0` and `frames=` logs `persp=1 subpixel=1`; `wobble=1` logs `persp=0 subpixel=0`; `config_version=5 persp=0 subpixel=0` migrates (logged) to both on; `config_version=5 persp=1 subpixel=0` is kept. The five routes against `base-3ba4fd2`: `title` 10/10, `pause-ws` 91/91, `pause-ws-wobble` 91/91, `wobble60` 91/91, `owner60` 91/91 the same. Not on hardware: the start-up compile time with the fix on from the start (queued).
- **Routes** (with this merge): `title`, `pause-ws` and `pause-ws-wobble` set `interp_budget=0`. Since D150 made 60 HZ the default, the title's in-between frames were skipped or drawn by Vita3K's measured time, so those routes' title shots changed from run to run (8 of 91 between two runs of one VPK); pinned, two runs of one VPK give 91/91.

## 2026-11-20 — clamped vertices drawn at their exact position (#178)

### D195. With the fix on, a vertex whose projection the GTE clamped is recorded at its exact position and depth and drawn there, not marked invalid

- **Why:** the owner sees textures stretching at the 16:9 screen edges. A vertex whose projection the GTE clamped (IR1/IR2 saturated, the quotient at 0x1FFFF, SX/SY at 11 bits, SZ3) was recorded invalid, and every polygon using it fell back to the PS1's drawing. In 16:9 the sides show more than the 4:3 projection was made for, so big close polygons crossing the edge clamp.
- **What:** `rtp_point` rebuilds the GTE's own answer from the inputs with every clamp; when it is the game's key, the unclamped point is kept under it, flagged `T_CLAMP`. A position beyond a candidate's int16 range (±128 px) goes to a per-table float array (1,024 a frame), so an entry stays 40 bytes. A clamped point that shares a key with another point makes the key ambiguous, never used (saturated keys collide). The renderer draws a clamped vertex at its exact position even with `subpixel` off (`VREF_CLAMP`); positions stay per key, so polygons sharing a vertex don't crack. Culling still uses the integer coordinates.
- **Cost:** `VCacheVert` is three floats (dx, dy in pixels, w), 12 bytes a vertex instead of 8, in `g_vcache_side`, the presenter's array and the render thread's copies; `XVertex` stays 40 bytes (its UV box is bytes now) and the shader reads offset and w as one `vec3`. `vc.div` and `vc.lines` +0.4–1.3% (within noise).
- **Counters:** `vc.invalid` (projections recorded invalid), `vc.fallback` (polygons drawn affine because of one), `vc.clamped` (clamped projections recorded exactly); the `vcache N clamps` log line splits the misses by reason.
- **Checked** (Vita3K, `runtime/autopilot/runs/auto-178-1`; native VitaSDK builds exit 0; `noassets`): the five routes' replay shots the same as `base-3ba4fd2` (10/10, 91/91 ×4); the GPU's own picture (`CAPTURE=1` window grabs on `wobble60`'s lines at 3,000, 6,000, 7,200, 7,201, 8,400, `auto-178-1/capture`) byte-identical to the baseline VPK's, so the new vertex layout and shader draw what the old did. Vblanks 6,900–9,000: `owner60` invalid-key fallbacks 4,068 → 621, polygons with every vertex found 93.30 → 93.36%, `vc.misses` −1.9%; `wobble60` fallbacks 293 → 0. The invalid projections left are all behind the eye and unused.
- **What it showed:** clamps were a small part of the misses. On `owner60` 168,525 vertices had no cache entry at all and 101,465 were ambiguous, against 934 invalid (#183, #181); the edge stretching is more likely the no-entry vertices. Hardware pictures (`shot_hd=1`) are queued.

### D196. On the Vita the OT walk overlaps the game thread: `savestate_point` joins it only for a pending save or load (#187)

- **Why:**
  - Heavy scenes lose frames with 60 Hz logic even at 1x.
  - `savestate_point` (present.c's `PortPresentBeforeUpdate`, at every main loop top) joined the walk when `s_req_slot || g_savestate_tick_hook`. The Vita always sets the hook (`savestate_vita.c`, `vita_savestate_init`). So since D71, every tick joined about 50 µs after its `DrawOTag`'s handover, and the game thread waited for the whole walk.
  - A diagnostic build traced 300 of 300 waiting joins to `savestate_point`.
  - Hardware shows the wait equal to the walk's busy time: 5.71 against 5.62 ms a vblank in the owner's 60 HZ Artisans log (`logs/owner-20261120-1056`, 6,900), and 10.55 against 10.46 in AP8b (D179).
  - On the host the hook is only set with `--state`, so D71's `--walk-lazy` checks never saw it.
- **What:** the walk is joined there only for a pending save or load. The hook's own GPU calls join where they post (`vita_video_overlay` → `call()` → `env_sync` → `psx_walk_join`). A request it makes (`state_test`) is serviced the next tick, with the join. With 60 HZ logic the walk now runs beside the next tick and is joined at `DrawSync`, as D71 meant.
- **Checked** (Vita3K, `runtime/autopilot/runs/auto-187-1/final`; native build, `noassets`, gate PASS):
  - the five routes' shots are the same as `base-cc65182`, and every work counter is the same;
  - `owner60`'s wait for the walk went 6.1–7.6 → 0.00–0.06 ms a vblank (6,900–7,500), and `ot` 6.2–7.6 → 0.04–0.13. Vita3K's timing is only relative.
  - At 30 Hz with in-between frames the join moves to the presenter's `records_begin`, about 1 ms after the handover, so the gain there is small (#193).
- **Stage split on hardware** (Artisans, 60 HZ, wobble fix on), per KB of primitives:
  - hand renderers 0.15–0.18 ms;
  - walk 0.10–0.14 ms;
  - render thread 0.10–0.12 ms;
  - the vertex cache is about 0.19 ms across the three, the largest stage (#179, #193).
- **Expected on hardware:** the owner's heavy 60 HZ Artisans windows at 1x go from about 18 ms a tick on the game thread to about 12, so close to 300 pictures in 300 vblanks (212–266 in the owner's log). Not checked on hardware yet. Host `--walk-lazy` with `--state` not run.

### D197. The presenter's projections are worked out as it makes them again (`vc_lazy=0` by default); the logic draw's deferral no longer needs `vc_lazy` (#198)

- **Why:** D192's regression, bisected on hardware with the owner's yes ("sure run them"; `runtime/autopilot/hw-session5.sh`, `tools/vita/fps-runs/ap15-*.txt`, logs `logs/hw-ap15a`–`c`). These are AP13 (Toasty at 30 Hz with in-between frames, the wobble fix on) on `autopilot-63d80f4.vpk`, each with one switch changed:

  | Run | Game-over screen, late per 300 (20,100–21,000) | Toasty, late per 300 | Toasty opening, presenter busy |
  |---|---|---|---|
  | AP13 (defaults) | 53–61 | 3.5 | 95% |
  | AP15a `parse_thread=1` | 86 | 62.2 | – |
  | AP15b `vc_lazy=0` | **0** | **2.8** | 89% |
  | AP15c `vc_lazy_logic=0` | 60–62 | 7.3 | – |

  D186's lazy presenter is what locks the game-over screen: the same work counters, but the presenter takes twice as long a frame. On hardware it gains nothing in Toasty either: D186's gain was measured in Vita3K. D187's `parse_thread=2` is right; `parse_thread=1` is much worse.
- **What:**
  - The Vita's `vc_lazy` default is 0. `vc_lazy=1` keeps D186's form for A/B.
  - `vcache.c`'s logic-frame deferral (D188, `vc_lazy_logic`) no longer requires `vc_lazy`, so the owner's 60 HZ settings keep it (AP11's `vc.rec.kept`). The host never sets either switch, so its defaults are unchanged.
  - Docs: `vita-testing.md`'s two rows and its `vpk-perfcount-vc` note, `main.c`'s key list.
- **Checked:**
  - AP15b is this default on hardware, except that it has no logic deferral, which `vc_lazy=0` turned off until now.
  - Vita3K `wobble60` and `owner60`: 91 of 91 shots the same as `base-63d80f4` each (`runtime/autopilot/runs/auto-198`); `owner60` keeps the logic deferral (`vc.rec.kept` 39,795 against 40,542 at 8,701–9,000). Host build and ctest: 32 of 32 (`runtime/autopilot/host-198.log`).
  - Not rerun on hardware: the game-over screen with this exact build.

## 2026-11-21 — main merged into the autopilot branch for PR #176

### D198. The autopilot branch merged with main at `5c6021c` (the PS4 work, PRs #188, #191, #199); the branches' clashing decision numbers renumbered on the autopilot side

- **Why:** PR #176 couldn't merge. Main's PS4 decisions took D173, D174, D180 and D193, which the autopilot branch also used for its own (PSX WOBBLE's row, clamped vertices, the walk overlap, `vc_lazy=0`).
- **What:**
  - The autopilot branch's four became D194 (PSX WOBBLE's row), D195 (clamped vertices), D196 (the OT walk overlaps the game thread) and D197 (`vc_lazy=0` by default). Every reference in the repo was renumbered with them. GitHub issue and PR texts written before today keep the old numbers: read D173/D174/D180/D193 there as D194–D197 when the subject is the Vita.
  - This file keeps both branches' decisions, in number order, each with its date heading.
  - `CMakeLists.txt`: main's `SPYRO_HOST_DIR` (linux64 builds from `src/platform/linux32`), with the autopilot branch's link launcher (D170) still for linux32 only.
  - `tools/host_checks.sh`: main's `EMU` (qemu-x86_64 for a linux64 binary), with the autopilot branch's `--save` on the demo run.
- **Next free number:** D199.

## 2026-11-21 — the 60 Hz logic draw's profile (#200)

### D199. The lod line names the budget's back-offs at 1x, and the 60 Hz logic draw's cost is mostly the vertex cache (#200)

- **Why:**
  - In the owner's play log on b1a41ac (`logs/owner-20261121-1200`), 2x drew as 1x in Artisans and Stone Hill.
  - The line said `(0 for the budget)`, because `budget_cuts` counted only above 1x.
- **What:**
  - `lod.c` counts a budget back-off at any scale. The line reads `N back-offs (B for the budget, of them H held at 1x)`, on the Vita and the host.
  - The host's `prof_draw.c` charges the sky once (D181's fix) and gives each part's KB of primitives.
- **Found** (`runtime/autopilot/reports/200-1.md`, runs in `runtime/autopilot/runs/auto-200-1`):
  - **The budget isn't the only limit.** With the budget off (`--lod-budget 0` on the host in Stone Hill, `dd_budget=0` in Vita3K's `owner60` at 6,900–7,500), the scale stays at x1.00. The limiter measures the world against its bound (`ENV_MARGIN`, 44 KB, with 13–17 KB measured after the world), and that sits between 65% and 80%, so the scale never grows back (#203).
  - **Forced 2x:** the world's loops stop early 5 times a draw, and pass 2's 41 mobys a draw double the moby part.
  - **The vertex cache is most of the cost.**
    - On the host it is 85% of the logic draw's non-raster function time: `rtp_apply` 42%, `choose` 15%, `psx_vcache_poly` 13.5%.
    - In Vita3K `owner60` the logic draw is 7.04 ms a vblank, against 1.78 with `wobble=1`, and 9.18 with `vc_lazy_logic=0`.
    - Estimate for the owner's Stone Hill window: about 6.8 of the game thread's 14.84 ms (hardware ratios from D196 and AP1/AP2; not measured).
  - **`expand` is not on the game thread.** The `expand 772 ms` on the `vcache` line is the atlas's expand on the render thread.
- **Filed:**
  - #203: the world's bound;
  - #204: a quota of kept projections for the walk;
  - #205: a cheaper record;
  - #206: the walk's lookups;
  - #207: docs, and KB per part on the Vita;
  - #208: the owner's call on far mobys at 2x.
  - The sky's records (about −0.3 to −0.5 ms, with a picture risk) aren't filed yet.
- **Checked:**
  - The five routes' shots are the same as `base-63d80f4`.
  - Counters are the same, except `owner60`'s timing-dependent kept/once split.
  - Native build, host ctest 32/32, `noassets`, and gate PASS.

## 2026-11-21 — a shader cache for start-up (#201)

### D200. The start-up's shaders go through a program cache in `ux0:data/spyro/shader_cache/` (`shader_cache=0` turns it off) (#201)

- **Why:**
  - Every boot logged `gpu: shaders compiled in` 10,035–10,211 ms (13 hardware logs, 2026-11-20/21), behind a black screen.
  - The installed vitaGL is built without `HAVE_SHADER_CACHE`: its `vglSetShaderCachePath` is a bare `bx lr`.
- **What:**
  - **Save and load:** `gpu_gl.c`'s `link()` saves each linked program with `glGetProgramBinary` to `shader_cache/<key>.bin`. The file has a header with a magic number, version, key, length and FNV-1a checksum, and is written as `.tmp` and then renamed. Later boots load it with `glProgramBinary`, with no compile.
  - **Key:** a 64-bit FNV of:
    - the program's sources and attribute bindings;
    - the format version and the compiler options;
    - the SHA-1 of `libvitaGL.a` and `libvitashark.a` (CMake, at configure time);
    - each `libshacccg.suprx` found (size and date).
  - **Stale and bad files:** a `stamp` file empties the folder when the libraries or the compiler change. A file that fails its checks or its link is removed, and the program is compiled.
  - **Log:** one line per program (hit or miss, with its time). The start-up line now gives the totals.
- **Measured** (Vita3K, `runtime/autopilot/runs/auto-201-1`; counts only, since Vita3K's compile times aren't the Vita's):

  | Run | Compiled | From the cache | Start-up line |
  |---|---|---|---|
  | First boot | 15 programs (17 asked for, 2 repeats) | – | 10,771 ms |
  | Second boot | 0 | 17 of 17 | 17 ms |
  | `shader_cache=0` | 17 | 0 | 6,969 ms |

- **Checked:**
  - The five routes' shots are the same as `base-63d80f4`.
  - `CAPTURE=1` window grabs are the same from the cache and from compiling.
  - Counters are the same.
  - Native build, host ctest 32/32, `noassets`, and gate PASS.
- **Not checked on hardware:** the black-screen time, and 17 cache hits on the second boot.

## 2026-11-21 — draw distance's room is the D54 span (#203)

### D201. Draw distance's world bound and limiter take the D54 span as the room (#203)

- **Why:**
  - With the option on, every draw spans both primitive buffers (D54, D61), but `lod.c` kept one buffer's size.
  - Stone Hill's world alone (52 KB at 1x) was 78% of the 66 KB its bound left. So 2x stayed at x1.00 even with the budget off (D199).
- **What:**
  - `draw_room()` is the span when the option is on: 0x37A00, or 0x25A00 in reduced-space levels. With the option off (statistics only) it stays one buffer, so baseline `lod` lines don't change.
  - The world's loops stop `ENV_MARGIN` (0xB000) under its top. The tour re-measured what the margin must cover: at most 30,852 bytes drawn after the world, and a largest sector of 5,776 bytes.
  - The limiter measures used bytes against the span, and the world's bytes against the span less the margin. The thresholds are unchanged.
  - Pass 2's moby share stays at 60% of one buffer, and the 60 Hz budget is unchanged.
  - The `lod` line reads `of 222 KB` (150 KB in a reduced-space level).
- **Checked** (`runtime/autopilot/reports/203-1.md`, runs in `runtime/autopilot/runs/auto-203-1`):
  - **Host whole-game tour** at `--widescreen --draw-distance 2`, 100,000 vblanks: no crash, and traces and memory card identical to the build before.

    | | Before | After |
    |---|---|---|
    | Average scale | x1.23 | x1.81 |
    | Back-offs | 25,400 | 0 |
    | Peak | – | 149 KB of 222 |

    `--lod-guards-only` (2x throughout) also ran to the end.
  - **Host Stone Hill:** with `--lod-budget 0`, x1.00 → x2.00, with no loop stopped. With the default budget it is unchanged and the trace is byte-identical.
  - **Vita3K `owner60` at 60 Hz:** shots and counters are the same. With `dd_budget=0`, 6,900–7,800 draw at x2.00.
  - **Pictures that changed:** where 2x has no budget (the title flyby, 600–1,200, and the 30 Hz Artisans attract demo, 3,200–5,900), it now reaches x2.00. 31 of `owner60`'s 91 shots differ there. The other four routes are the same.
  - Native build, host ctest 32/32, `noassets`, and gate PASS.
- **Open:**
  - On hardware, check that 30 Hz holds where 2x now draws more (title, attract demos). If it doesn't, give 30 Hz a budget.
  - The near list peaked at 125 of 127 in the tour.

## 2026-11-21 — a cheaper vertex-cache record (#205)

### D202. The vertex cache's record takes the GTE's own sums and the registers by pointer; kept registers are compared by generation (#205)

- **Why:**
  - D199 put the record at 85% of the 60 Hz logic draw's non-raster host time.
  - Each native projection copied 15 registers into the value hook's form, then `rtp_apply` multiplied them again.
  - `same_proj` compared 15 words for each kept projection.
- **What:**
  - **Direct call:** `gte_native.h`'s RTPS and RTPT call the record by name, `ngte_vcache` in `vcache.c` (the presenter's build calls `pres_vcache_ngte`). They pass the `NgteProj *` and an `NgteRtp` with new fields `m1`–`m3`, the 44-bit sums `ngte_rtp_v` already worked out. MAC1-3 come from the same expressions, so no GTE result changes.
  - **Generation number:** `NgteProj.gen` is a new number at every `ngte_proj_get`/`rot`/`tr` while the cache records, and 0 otherwise. `lz_add` compares a kept projection's registers by that one word. A new generation is compared word by word once, then adopted, so the store keeps the same registers in the same places.
  - **Smaller cuts:** `rtp_apply` converts the 12 low bits as an int.
  - **Weak symbols:** `g_ngte_gen` and `ngte_vcache` are weak, so the GTE tests link without the cache.
  - **Unchanged path:** the sky's records and the register file's still go through the hooks.
  - **Rule for renderers:** a renderer that changes `NgteProj` fields itself after a projection must call `ngte_proj_regen`. None does today.
- **Checked** (`runtime/autopilot/reports/205-1.md`):
  - **Same answers:** host FNV dumps per polygon are byte-identical. Route A (6.13M polygons) was run with deferral auto, off and forced, and with `--walk-lazy`. Route B (8.98M polygons) was run with auto, forced, and the presenter's `VC_LAZY=0`. Memory cards are identical too.
  - **Pictures and counters:** the five routes' shots are the same, and so are every `vc.*`, GL and ot counter.
  - **Host cost:** gprof shows the record's functions 21–34% cheaper (qemu).
  - **Vita code:** on ARM, `rtp_apply` loses 9 multiplies (737 → 681 instructions). The 15-word copy, the indirect call and `rtp_value_hook` are gone.
  - **Builds:** native build, host ctest 32/32 (`pres_link`, `render_ctx_link`, `savestate_globals`, `gte_native` and its mutants), `noassets`, and gate PASS.
  - **Not checked on hardware.**
- **Left:**
  - `render_ctx.c`'s `pres_ngte_vcache` and its copy are now dead code.
  - Storing the sums for the walk, which needs a 32-byte store record, is a trade-off for hardware to judge.

## 2026-11-21 — KB of primitives per draw part on the Vita (#207)

### D203. The Vita's draw profile gives each part's KB of primitives; `host-build.md` describes D199's lines (#207)

- **Why:** to get each part's cost per KB from a hardware log directly (D199's estimates came from ratios). `host-build.md` still described the old `--lod-stats` and `--prof-draw` formats.
- **What:**
  - `src/platform/vita/prof_draw.c` counts, per part, how far the primitive pointer (`D_800757B0`) moved across each wrapped call. That's the same count as the host's `--prof-draw` (D199).
    - Moves of 0x100000 or more are left out (the HUD's mobys draw from the top).
    - The sky main counts only outside the cyclorama, as its time does.
  - A new line follows each `draw N:` line: `draw kb N: per vblank KB of primitives update U env E mobys M spyro S fx F particles P sky K`. The `draw N:` line and its parsers are unchanged.
  - Docs: `host-build.md` describes D199's `lod` back-offs, plus the `--prof-draw` KB, `renderers` and `sky main` lines. `vita-testing.md` documents `draw kb`.
- **Checked** (`runtime/autopilot/reports/207-1.md`):
  - In Vita3K `owner60`, the parts add up to the `lod` line's averages: 67.7 against 67 KB at 7,200, and 37.9 against 38 at 8,400.
  - `owner60`'s shots are the same as `base-63d80f4`.
  - `fps60_summary.py`, `native_share.py` and `hw_cmp.py` still read the log.
  - Native build, host ctest 32/32, `noassets`, and gate PASS.

## 2026-11-21 — cheaper walk lookups (#206)

### D204. The walk's lookups compare depths on their bits and find a key's near projections in one walk (#206)

- **Why:** this is #200's cut 4 (D199). The walk's `psx_vcache_poly`, `choose`, `resolve` and `res_place` run on core 1. That core is 68% busy in the owner's Stone Hill window, and `resolve` grows at 2x. A cheaper walk gives #204's quota more room.
- **What** (`src/platform/common/vcache.c` only):
  - **Depth tests on the bits.** `choose` and `resolve` test depth ratios against 4, 3.999 and 1.02 on the depths' integer bits, where that is exact (positive normal floats; multiplying by 4 adds 2 to the exponent). Between the bounds, and for other values, the float tests decide as before. Checked against the float tests: 0 differences in 197.7M pairs (arm64) and 19.8M (i686 x87).
  - **One test per candidate.** `choose` tests each candidate against 4 once, keeping a bitmask.
  - **One walk in `resolve`.** It keeps its anchors (at most 32) sorted largest first and walks them alongside its falling chain, stopping when none is left. That is O(chain + anchors) where it was O(chain × anchors).
  - **One mode check.** `psx_vcache_poly` asks `psx_gpu_game_walk()` once a polygon.
  - D202's record side is untouched. `add_cand` keeps the float test.
- **Checked** (`runtime/autopilot/reports/206-1.md`):
  - **Same answers.** Host FNV dumps per polygon are byte-identical in all 10 pairs:
    - route A: 1x; dd2 with auto, off, forced and `--walk-lazy`; forced 2x;
    - route B: auto, forced, `VC_LAZY=0`;
    - Stone Hill at 60 Hz, forced 2x (20.1M polygons).
  - **Less work.** Host gprof, scaled against the unchanged raster time:

    | Route | Scale | Lookup cost |
    |---|---|---|
    | A | 1x | −28% |
    | A | forced 2x | −21% |
    | Stone Hill | 1x | −19% |
    | Stone Hill | forced 2x | −21% |

    `choose` falls 46–54%. Divisions and probes per vertex are unchanged by construction; table lines fall 0.05–1.1%.
  - **Pictures.** The five routes' shots are the same. `owner60` was checked against #203's run, because D201 changed its 30 Hz shots.
  - Native build, host ctest 32/32, `noassets`, and gate PASS.
  - Not checked on hardware. The host's gain is weighted by qemu's emulated x87, so the Vita's will be smaller.
- **Left:** dividing in `choose`'s multi-candidate path (`ratio`), and `res_place`/`near8` compared as integers.

## 2026-11-22 — the last wobble in the portal fly-in and Stone Hill (#202)

### D205. With the fix on, sky cuts at one portal edge's pixel share their mean place; a derived grid point the plane gives no depth takes its projection's own; a shadow's or glow's ambiguous source is resolved from the frame's order (#202)

- **Why:**
  - The owner's log `logs/owner-20261121-1200` (PAL, 60 HZ, 2x) shows 96.60% of polygons found whole in the 7,800 window and 99.76–99.93% in Stone Hill.
  - Tagged by renderer on host routes (`runtime/autopilot/reports/202-1.md`):
    - **Gamestates 1 and 9 lose nothing.** The 7,800 window's losses are the portal fly-in in Artisans (gamestate 0, just before 1), where the sky through the portal fills the screen. There, PortalSkyPlain's cuts were dropped when two cuts placed one key differently: about 89k vertices on host route P.
    - **Stone Hill's losses** are mostly the particles' screen-facing quads (ParticlesDraw case 3, about 6.6k of 7.4k on route S).
- **What** (`vcache.c`, `psx_vcache.h`):
  - **Sky cuts:** cuts from one renderer within 2 px at one key share their mean place, which lies on the edge (`HK_MERGE`). A hand-over first takes its sources exactly as before. A place read from a merged one is tainted: it counts only where nothing else placed the key, and gives way to any untainted place.
  - **Derived grid points:** a derived grid point at the projection the frame's order named, where the plane through the other corners gives no depth, takes that projection's own depth.
  - **Ambiguous sources:** a shadow corner (alias) or glow rim (offset) whose source key is ambiguous is resolved from the frame's order, like an ambiguous vertex, and drawn at its own integer key.
  - **New counters:** `der_own` on the `derived` line; `hk_merged`, `hk_ambsrc`, `res_alias` and `particles` on the `hooked` line. `VCD_PARTICLES` is reserved for #209.
- **Checked:**
  - **Host FNV per polygon:** 0 polygons found before changed, and 0 became affine, in 8 pairs (routes P, S, A, B; lazy logic 0 and 2; `VC_LAZY=0`; no lazy walk).
  - **Host routes, old path against new:**

    | Route | No-entry vertices | Logic polygons whole |
    |---|---|---|
    | P | 133,758 → 40,789 | 99.1% → 99.7% (69,574 polygons newly found) |
    | S | 35,713 → 35,375 | – |

  - **Vita3K:** `owner60` whole 99.569% → 99.576%, `vc.misses` −21.6%. `wobble60` `vc.misses` −15.5%. No Vita3K route flies through a portal.
  - **Pictures:** the five routes' shots and the `CAPTURE=1` grabs are the same. `owner60` was checked against #206's run.
  - **Builds:** native build, host ctest 32/32, `noassets`, and gate PASS.
  - **Not run:** hardware, and the PAL disc on the host.
- **Left:**
  - The particles' quads (#209: a hand-over in `particles_hand.c`).
  - 2D polygons, by design.
  - Far mobys' clamped points among several (D190).
  - Sky cuts more than 8 px from their key or from an ambiguous end: a crack risk.
  - Cuts more than 2 px apart.

## 2026-11-22 — a quota of kept projections for the walk (#204)

### D206. A `vc_lazy_logic=3` quota: the logic draw also keeps a frame's first q projections while the walk still runs, with q sized from the walk's idle time (#204)

- **Why:**
  - #200's cut 1 (D199). With the idle rule (mode 1, D188), a projection is kept only while the previous frame's walk is idle at that moment, so the walk's slack after it ends goes unused.
  - In the owner's Stone Hill window, 76% of projections were worked out at once on the game thread.
  - Keeping everything (mode 2) overloaded the walk (D188).
- **What** (`vcache.c`, `psx_vcache.h`; coordinator: `main.c`'s key clamp raised to 3):
  - **What mode 3 keeps:** everything mode 1 keeps, plus the frame's first q projections while the walk still runs. Past q, the entry's kept list is worked out first, so the order and the answers don't change.
  - **Measuring the walk:** the walk's thread stamps its start and end. The game thread stamps the clock every 128 records.
  - **Sizing q:**
    - Idle time is the walk's end to the game thread's last stamp; the vblank wait after the join is left out on purpose.
    - Headroom is an eighth of the walk's window, at least 250 µs.
    - q grows by the smaller of the last two frames' surplus, at 1 µs a projection.
    - It falls by the shortfall, at most a quarter a frame, and is capped at the frame's records + 256.
  - **No walk thread:** with the walk in place, mode 3 keeps all, as mode 1 does.
  - **Log line:** the new `vcache quota:` line appears only in mode 3.
  - **Default:** stays 1.
- **Checked** (`runtime/autopilot/reports/204-1.md`):
  - **Same answers:** host FNV dumps per polygon are byte-identical in 10 dumps. They cover modes 0, 1 and 3; a fixed and a random quota; a simulated walk-busy split (kept 14.2M / at once 4.0M, and 5.0M / 13.2M); `--walk-lazy`; and `--tick60`.
  - **Pictures:** the five routes' shots are the same.
  - **Vita3K `owner60`**, same session, 6,900–9,000. These are relative timings, inside Vita3K's noise:

    | `owner60` | Mode 3 against mode 1 |
    |---|---|
    | Logic draw | −8% |
    | Kept projections | +10% |
    | Join wait | 0.25 → 0.17 ms a vblank |

  - **`wobble60`:** unaffected by design (30 Hz logic, D188).
  - **Builds:** native build, host ctest 32/32, `noassets`, gate PASS. The Vita build was redone with the clamp raised to 3.
- **Open:** a hardware A/B of `vc_lazy_logic=1` against `3` in the owner's 60 HZ Stone Hill and Artisans windows. If 3 wins, make it the default.

## 2026-11-22 — particle quads in the vertex cache (#209)

### D207. With the fix on, the particles' screen-facing quads hand their corners to the vertex cache, and a particle corner gives way to every other renderer's hand-over (#209)

- **Why:** #202 (D205) found that ParticlesDraw case 3's quads are Stone Hill's largest class of no-entry vertices.
- **What:**
  - **The hand-over** (`particles_hand.c`): case 3 hands over each of its 4 corners as `VCD_OFFSET` from the projected centre `sxy`, after `AT32(prim + 0x8) = v0 - a3 - a2;`. The offsets are read from the corner words as the GPU reads them (signed low half for x, signed high half for y), so an x borrowing from y gives the y drawn. The call is direct; the presenter's build remaps it.
  - **Priority** (`vcache.c`, `psx_vcache.h`):
    - A particle corner (`Hk.low`) counts only where no other renderer placed the key, dead keys included.
    - Any other renderer's hand-over replaces it.
    - It is never a source for another hand-over.
    - Without this rule, the presenter's frames lost 3 shadow polygons on host route S.
  - **Docs:** the `particles` counter in `vita-testing.md`.
- **Checked** (`runtime/autopilot/reports/209-1.md`):
  - **Host FNV per polygon, 6 pairs:** 0 polygons found before changed. The pairs were S at 30 Hz, S with `--interp`, S with `--tick60 --interp`, P, and P with flame.

    | Route | No-entry vertices | `particles` | Newly found polygons (all particle quads) |
    |---|---|---|---|
    | S | 25,048 → 21,401 | 3,647 | 889 |
    | S with `--interp` | 50,039 → 42,732 | – | 1,781 |

  - **Vita3K:** the five routes' shots and counters are the same. No Vita3K route draws a case-3 quad (#210).
  - **Builds:** native build, host ctest 32/32, `noassets`, gate PASS.
- **Left:**
  - A Vita3K route through Stone Hill with particles on screen (#210).
  - Watch `hk_full` (1,024 hand-overs a frame) in busy particle scenes.

## 2026-11-22 — a Vita3K route through Stone Hill (#210)

### D208. The Vita3K route `stone60` covers a portal fly-in and Stone Hill play with the owner's settings; the host route pads are checked in (#210)

- **Why:** none of the five Vita3K routes reached Stone Hill, a portal fly-in, or the particles' case-3 quads. So D205's and D207's fixes couldn't be measured or regression-checked on the Vita build. #202's host route pads were also lost when its worktree went.
- **What:**
  - **The route:** `tools/vita/hw-routes/stone60.pad` and `.cfg`, with the owner's settings (16:9, `interp=4`, wobble fix on, `draw_distance=200`, frames 9,500). It is an absolute pad, steered on the host at `--tick60`, with no pokes:
    - from New Game, a walk and jump into Stone Hill's portal (fly-in 6,805–7,225; play from 7,293);
    - a ram's butt for the particles' star quads (about 7,780);
    - then #209's route S actions (run, flame, charge, jump, glide) to 9,349.
  - **Autopilot config:** `stone60` is in `autopilot/config.json`'s routes, and its benchmark window is 7,300–9,400. The baseline is `runtime/autopilot/runs/base-bac5699/stone60`.
  - **Host pads:** S, P and Pf are checked in as `tools/oracle/pads/route_stone_s.txt`, `route_portal_p.txt` and `route_portal_pf.txt`. Each header gives its command line.
  - **Docs:** `vita-testing.md`.
- **Checked** (`runtime/autopilot/reports/210-1.md`):
  - **Coverage:** on #209's VPK the route counts `particles` 2,161 and `hk_merged` 6,599. The host counts the same with the same pad.
  - **Determinism:** three Vita3K runs (about 160 s each) gave 95 of 95 shots the same.
  - **Builds:** native build, host ctest 32/32, `noassets`, gate PASS.
- **Timing-dependent counters:** these move between identical runs on this route, as on `owner60` (D188):

  | Counter | Spread between identical runs |
  |---|---|
  | `vc.invalid` | up to 31% |
  | `vc.clamped` | up to 27% |
  | `vc.rec.*`, `vc.walk.*` | up to 14% |

  Gates must read them as noise on `stone60`.

## 2026-11-22

### D209. Hardware session 6: round 11's build (85a7b2c) on the Vita (2026-11-22)

- **Run:** with the owner's yes ("the vita is free for hw runs"), AP16a–AP16d and AP16t (`runtime/autopilot/hw-session6.sh`, logs `logs/hw-ap16a`–`hw-ap16t`). Builds: `autopilot-85a7b2c.vpk` (SHA-1 0c08196c, D199–D208) and `autopilot-63d80f4.vpk` (round 10's, D192), US disc. New run files `tools/vita/fps-runs/ap16-stone60.txt` and `ap16-ll3.txt`: the stone60 route (#210) with the owner's settings (16:9, 60 HZ, the wobble fix on, `draw_distance=200`) and virtual vblanks, as AP8.
- **Shader cache (#201, D200):**

  | Boot | Shader time |
  |---|---|
  | 63d80f4 (no cache) | 10,281 ms, every boot |
  | 85a7b2c, first boot | 9,604 ms (15 compiled, 2 hits) |
  | 85a7b2c, second boot | 135 ms (17 of 17 from the cache) |

- **Stone Hill play on the stone60 route, 7,300–9,400 (AP16b against AP16c):**
  - The game thread was busy 73.2% → 71.5% on average, with a worst of 87% → 85%. Virtual vblanks, so 0 late by construction; busy % is the measure.
  - The draws are identical in both builds. Since D201 the 48 KB budget, not the world bound, is what holds 2× back. The lod lines show x1.00 at 8,400–9,400, with 271–300 back-offs per 300, all for the budget and held at 1×. The thread still had 15–28% to spare there, so the fixed byte budget is too careful (#212).
- **`vc_lazy_logic=3` (#204, D204), AP16d against AP16b:** busy 71.7% against 71.5%, with the same worst. That's no gain on hardware, so the default stays 1.
- **Toasty route, real time (AP16t against AP14b, D192):**
  - Artisans: late vblanks per 300 went from 16.5 to 0.5, and in-between frames skipped from 1,026 to 0.
  - Toasty: late per 300 went from 0.3 to 0.2, and the render thread from 9.61 to 6.08 ms a picture.
  - Stone Hill's attract demo at the title (4,200–6,300) is at about 177 late per 300 in both builds. The presenter's frames all run inline on the game thread (`0 frames on core 1, 150–216 inline`, 32 ms a tick): the attract demos' 16:9 real frame runs inline by design (D128). Filed as #220.
- **Left on the Vita:** 85a7b2c's eboot (SHA-1 57e00232, READBACK IDENTICAL).
- **Plan:** #212 (a time-based 60 Hz budget) is now backed by hardware numbers, and #220 is filed. A scene under 60 resets `rounds_without_gain` to 0. The next session is the #212 A/B: auto against 48 KB, on stone60 and owner60-style runs.

### D210. The vertex cache sorts the vertices it leaves unplaced into missed and exempt, by renderer (#211, 2026-11-22)

- **Why:** the old count of vertices with no entry mixed two kinds. Some are 3D vertices the wobble fix should place; others are 2D by design (fades, HUD text, the portals' flat faces) and never had an entry. With both in one number, it couldn't say when the wobble fix is finished.
- **How:** as each renderer returns (`HAND_RENDERER`/`HAND_UNIT_FN_RC` in `src/game/render_hand.h`), it gives the frame's table its range of primitives and of `native_draw` records (`psx_vc_ctx`, and `pres_vc_ctx` for the presenter), named by its hand C. At the `miss_none` spot, the walk files each vertex with no entry under the smallest range that holds the polygon:
  - **missed:** a 3D renderer's vertex, named, e.g. PortalSkyPlain;
  - **exempt:** a portal's face, or no renderer at all;
  - **unmarked:** a frame with no ranges.

  The class comes from what the frame recorded, not from the walk's timing. The `vcache derived:` line now carries `missed N (top: ...)`, and the wobble fix is finished where missed is 0. The `render_hand.h` hook was outside the issue's first file list; the worker left a checked patch, and the coordinator widened the Files and applied it.
- **Measured (host, 2×, 16:9):**

  | Route | No entry | Missed | Biggest class |
  |---|---|---|---|
  | S, 30 Hz | 21,401 | 1,870 | PortalSkyPlain 1,230 |
  | S, `--interp` | 42,732 | 3,725 | PortalSkyPlain 2,440 |
  | P, 30 Hz | 20,843 | 1,312 | PortalSkyPlain 1,230 |
  | P, `--interp` | 41,616 | 2,609 | PortalSkyPlain 2,440 |
  | Pf, 30 Hz | 19,076 | 718 | PortalSkyPlain 660 |
  | Pf, `--interp` | 38,099 | 1,457 | PortalSkyPlain 1,329 |

  Next after PortalSkyPlain come EnvDraw (Stone Hill, 451 / 883), SkyDraw (111 / 244), and GlowsAndSparkles and MobyShadows. More than 90% of what's left with no entry is exempt. In Vita3K owner60 the line reads `missed 8 (MobyShadows 6, GlowsAndSparkles 2)` at 8,700.
- **Checks:**
  - Counters only: the host's per-polygon FNV is identical on six route pairs.
  - With the hook, the Vita build exits 0 (VPK 54d28d20). All six routes exit 0 on the first try, with shots identical to `base-85a7b2c` (10, 91, 91, 91, 91 and 95 same). Counters are the same on all four benchmark scenes (`vc.invalid` and `vc.clamped` ignored as timing noise). Logs: `runs/auto-211-1`, `runs/auto-211-2`.
  - Host ctest passes 32 of 32.
- **Follow-ups:**
  - #215: PortalSkyPlain.
  - #216: glows and shadows.
  - #217: EnvDraw.
  - #221: SkyDraw.
  - #222: 1,086 "outside" textured-quad vertices whose polygon has a placed vertex, to settle so that missed 0 really means no wobble.

### D211. The attract demos' 16:9 frames run on the presenter's worker (#220, 2026-11-22)

- **Why:** on hardware, Stone Hill's attract demo dropped about 177 vblanks in every 300 on the owner's settings (D209). The 16:9 real frame and the in-between frame both ran inline on the game thread, at 32 ms a tick. D128 had tied them there for two reasons:
  - the real frame's due vblank is only known after the logic draw;
  - the 16:9 visibility hand-off.
- **How** (`src/game/present.c`):
  - `presenter_pass(..., file_late)` runs the real frame on the worker with `submit(NULL)`, so its buffer files no frame. The game thread puts the frame's `C_BEGIN` in the ring before the buffer. After the hidden 4:3 logic draw, `PortPresentDraw` joins the job and `push_capture` files the frame, with the same `reserve()`, due vblank and `C_COMMIT` as the inline path.
  - The visibility hand-off is split. `wide_state_out` runs when the job starts, which is exact because the presenter writes no game memory (0 bytes in 4,338 frames, `--pres-write-check`). `pres_draw_visibility` (now `job_vis()`) runs at the join, and the next pass joins before it reads.
  - The in-between frame is still waited for before the real frame starts (one snapshot), so only the real frame overlaps the logic draw.
- **Checks:**
  - **Host:** FNV per polygon identical on all 9,124,329 polygons (title and attract demos to 7,000, `--interp`, 16:9, 2×). `--pres-check-thread` matches on 3,475 of 3,475 frames. ctest passes 32 of 32. The native build exits 0.
  - **Vita3K:** shots identical on eight routes, including new `demo60` and `demo30` runs of the attract demos (66 of 66 each). In the demo windows (4,200–5,700), frames on core 1 went from 0–31 per 300 to 280–300, and inline frames from 262–300 to 0. The game thread's presenter time per tick fell from 52–74 ms to 10–17 ms; Vita3K timing is relative only.
  - Logs: `runs/auto-220-1`.
- **Left:**
  - Measure FPS on hardware: the Toasty route's demo windows, 4,200–6,300.
  - If the wait on the in-between frame shows there, try that frame inline and keep only the real frame on the worker.

### D212. The Vita sizes the 60 Hz draw-distance budget from measured time (`dd_budget=auto`, the default; #212, 2026-11-22)

- **Why:** the fixed 48 KB byte budget (`LOD_BUDGET_60`, #180) couldn't see that D202, D204 and D206 made each draw cheaper. On hardware, Stone Hill at 2× held at 1× with the game thread busy only 71.5% (D209).
- **How:**
  - `lod.c` gains `lod_set_budget()`, `lod_last_draw()` and `lod_set_budget_hook()`. The hook is called at the start of each shown draw. A `GamestateUpdate` wrap would miss `GamestateUpdate_60`, which 60 Hz logic uses.
  - The Vita's `src/platform/vita/lod_time.c` measures the game thread's work for the frame just done from `g_present_stats` (update plus logic draw, VSync waits left out, the walk join included; needs `stats=1`, the default), and compares it with a target of 14.5 ms of the 16.68 ms frame.
    - A frame over the target cuts the budget at once to bytes × target / used.
    - After 8 frames in a row each at least 1 ms under the target, the budget grows 2 KB per frame, up to 1.25 × the draw + 8 KB.
  - `lod.c` clamps the budget between 48 KB and `draw_room`, and stays the only place that limits.
  - Config: `dd_budget=auto` is the default, `1` the old fixed 48 KB, `N` (2–1024) a fixed N KB, `0` off.
  - The lod line ends in `budget N KB (auto, X ms left; K over)`, or `budget held at 1x: U ms used of 14.5`.
  - The host keeps the fixed budget.
- **Measured (Vita3K, relative timing):**
  - stone60's Stone Hill play went from x1.00 (271–300 budget back-offs per 300) to x1.82–2.00, with the budget at 87–119 KB.
  - owner60 at 6,900–7,800 went from x1.00 to x1.67–1.92.
  - 2× costs GL work as it should: stone60 `gl.draws` +40%.
- **Checks:**
  - Shots differ only in the 60 Hz windows where 2× now holds longer. A `dd_budget=1` run of stone60 is 95 of 95 identical to base.
  - Host logs, traces and saves are byte-identical, on route S at 30 Hz and with `--tick60`. The host per-polygon FNV wasn't run: no writer is in the tree, and on the host the hook is never set.
  - ctest passes 32 of 32. Logs: `runs/auto-212-1`.
- **Left:** the hardware A/B, auto against `dd_budget=1`, on stone60 (`ap16-stone60.txt`) and owner60-style runs.

### D213. With the wobble fix on, a portal sky cut that the hand-overs leave unplaced takes its own fraction of the side (#215, 2026-11-22)

- **Why:** PortalSkyPlain topped the missed list on every route (D210): 1,230 on routes S and P at 30 Hz, 2,440 with `--interp`. Tagged on route P:
  - **Cuts 8–64 px from their key (660).** The portal outline is an arc of edges a few pixels long and nearly in line. Sutherland–Hodgman cuts against each edge's whole line, so a side often crosses an edge's line just past the edge's end. The line through the exact ends, extended, then runs pixels away from the integer one.
  - **An ambiguous outer end (378).**
  - **Two cuts at one key more than 2 px apart (185).**
- **How:**
  - `sky_clip_hand.c` `CutSide` hands the sky side's own fraction (IR0) to the vertex cache with `VCD_T`, for `ClipSky`'s cuts only.
  - Where the hand-overs leave the key with no place, `frac_hand` (`vcache.c`) places the cut t of the way between the side's exact ends, at 1/w-linear depth, within 8 px. An ambiguous end is its last projection in the frame's order, through `res_place`.
  - The places sit in a per-table `fr[256]` that no hand-over reads.
  - The walk takes one only for a vertex nothing else finds, not where #197 would take the polygon, and only in a PortalSkyPlain, PortalSkyFog or unnamed polygon. Without that renderer gate, a glow's vertex at a cut's pixel took the cut's place.
  - New counters `hk_frac`, `fr_kept` and `fr_nosrc`, on a new `vcache cut fraction:` line.
  - D182's hand-over rules and D205's merge order are unchanged; the fraction comes after both.
- **Measured (host, 2×, 16:9):**

  | Route | Missed | PortalSkyPlain |
  |---|---|---|
  | S, 30 Hz | 1,870 → 645 | 1,230 → 0 |
  | S, `--interp` | 3,725 → 1,294 | 2,440 → 0 |
  | P, 30 Hz | 1,312 → 87 | 1,230 → 0 |
  | P, `--interp` | 2,609 → 178 | 2,440 → 0 |
  | Pf, 30 Hz | 718 → 72 | 660 → 14 |
  | Pf, `--interp` | 1,457 → 151 | 1,329 → 28 |

  What's left is EnvDraw, SkyDraw, GlowsAndSparkles and MobyShadows.
- **Checks:**
  - Host FNV per polygon: 0 changed in 6 pairs, with 564 to 2,121 polygons newly found.
  - Vita3K: shots identical on six routes.
  - `CAPTURE=1` at stone60's worst fly-in frames: only the portal sky's horizon inside the portal changes (0.008% and 0.028% of pixels). It is straightened, and no gaps open.
  - ctest passes 32 of 32.
  - Logs: `runs/auto-215-1`.
- **Found:** on the Vita build, the portal sky's unplaced vertices are filed as "outside the renderers" (exempt) rather than under PortalSkyPlain or PortalSkyFog. On the Vita, D210's missed count under-reports them; added to #222.

### D214. The Vita's `draw N:` lines time the 60 Hz update, and the lod line gets a 640-byte buffer (#223, 2026-11-23)

- **Why:**
  - With 60 Hz logic the update is `GamestateUpdate_60` (`tick60.c`). The `GamestateUpdate` link wrap in `prof_draw.c` never saw it, so `update` read 0.00 in every 60 Hz window of the hardware logs.
  - `picture.c`'s 400-byte line buffer made D212 keep its budget text short. The line already reaches 385 bytes.
- **How:**
  - The Vita link wraps `GamestateUpdate_60` too, under `SPYRO_TICK60` (the moved-arena build has no 60 Hz path). It is charged to the same `update` column; `PortGamestateUpdate` runs exactly one of the two, so nothing is counted twice.
  - The line buffer is now 640 bytes; `vita_log`'s own buffer is 1,024. The line formats are unchanged, so the parsers need nothing.
- **Checks (logging only):**
  - Vita3K, with `dd_budget=1`: owner60's `update` in 6,900–8,700 reads 0.27–0.70 ms where base read 0.00, and stone60's 0.33–0.54.
  - Shots are identical on six routes.
  - The deterministic counters are identical. owner60's timing-dependent vc counters moved past two base runs' spread under machine load.
  - ctest passes 32 of 32.
  - Logs: `runs/auto-223-1`.
- **Left:** `lod.c`'s budget text can now carry the worst frame time and the budget's range (#224).

### D215. With `dd_budget=auto`, the Vita's lod line gives the budget's range and the worst frame time (#224, 2026-11-23)

- **Why:** an average alone can't tell apart a window that swung between the 48 KB floor and a long distance and one that held steady, nor one long frame and a scene that is over the target.
- **How:** `lod_set_budget` keeps the window's lowest and highest budget and its longest measured game-thread frame (`s_bt.lo`, `.hi`, `.worst`, in `LOD_STATE`). `budget_report` prints them as `budget 61 KB (auto, 48-96 KB, worst 15.9 ms, 2.1 ms left; 3 over)`, or `budget held at 1x: U ms used of 14.5, worst W ms (LO-HI KB)`. This uses D214's 640-byte line; the longest line seen is 433 bytes. It is logging only: the budget is chosen as in D212.
- **Measured (Vita3K, default auto, relative timing):**
  - owner60 at 6,900–9,000: `126 KB (auto, 126-126 KB, worst 3.8–11.4 ms, 0 over)`.
  - stone60 at 7,800–9,300: 112–135 KB, worst 5.9–12.1 ms, 0 over.
  - The level entries show a 72–82 ms worst frame.
- **Checks:**
  - With `dd_budget=1`, owner60 and stone60 shots are identical to `base-278ebf8`, and the deterministic counters are equal.
  - Host logs, traces, saves and per-polygon FNV are identical: 13,206,877 polygons at 30 Hz and 21,665,448 with `--tick60 --interp`.
  - ctest passes 32 of 32, and the native build exits 0.
  - Logs: `runs/auto-224-1`.

### D216. The sky's vertex-cache records go by name, and the portal sky's projections take one generation per draw (#213, #200 cut 5; 2026-11-23)

- **What:**
  - `sky_hand.c` `SkyRecord` calls `ngte_vcache` with the draw's `NgteProj`, its generation, SXY2 and SZ3. Before, it called the value hook with the 15 registers as words, which the record compared word by word with the last ones kept.
  - It passes no sums (`NGTE_NO_SUMS` in `gte_native.h`, a FLAG bit the GTE never sets). The record works them out from the `NgteProj` (`pt_np` in `vcache.c`) only for a projection it works out at once, exactly as it used to from the words. A kept projection compares one generation word (`same_ngte`).
  - Keeping D202's sums per projected vertex cost more in SkyDraw's loop than it saved: route P's host SkyDraw self time went 0.63 → 1.02 s with it. So the sky passes none.
  - `sky_clip_hand.c` (`FogSky`, `PortalSky`) reads the `NgteProj` once per draw; between sectors only the far colour changes.
  - Only the register file's records (`rtp_hook`) still pass words. `cyclorama_hand.c` records nothing itself.
- **Checked:**
  - Host FNV per polygon is byte-identical on routes A, B and P with deferral auto, forced and off (9 pairs; 6.2 M, 12.4 M and 14.1 M polygons).
  - Vita3K shots are identical on all seven routes, demo60 included (owner60 and stone60 with `dd_budget=1`). The counters are the same apart from the timing-dependent split.
  - Host gprof (qemu, relative), sky record per projection:

    | Route | Before | After |
    |---|---|---|
    | A | 294 ns | 277 ns |
    | P | 249 ns | 208 ns |
    | Presenter, route B | 236 ns | 183 ns |

  - ctest passes 32 of 32. Not checked on hardware.
  - Logs: `runs/auto-213-1`.
- **Found:**
  - `rtp_value_hook` and `pres_rtp_value_hook` now only flag that the cache is recording (added to #219).
  - The `ngte_proj_regen` audit covered only the sky files (added to #218).

### D217. The walk's depth choice compares ratios without dividing, and `res_place` tests depths on their bits (#214, 2026-11-23)

- **Why:** these were D206's leftovers. `choose`'s ratio path divided and `res_place` compared in float, on core 1. With 60 Hz logic the walk's join is on the game thread's critical path.
- **What** (`src/platform/common/vcache.c` only):
  - **`choose` without dividing:** `choose` keeps each ratio as its two depths' 24-bit significands and their exponent difference, and compares two ratios by cross-multiplying in 64 bits. The tests against 4 and 1 are integer tests: a float quotient is under 4 exactly when n/d < (2^25−1)/2^23.
  - **Float platforms (the Vita, arm64):** two ratios within 2^-22 of each other are still divided as before (`rat_lt_div`), because their floats could round to one value.
  - **The host's x87:** the old code compared the quotients on the x87 stack at 64-bit precision, which is the real order. There the real order is the answer (`RAT_REAL`), and nothing divides.
  - **Odd depths:** depths that aren't positive normal floats use the old function (`choose_f`). It ran 0 times in the profiled runs.
  - **`res_place`:** it reads lc's depth from its bits when it is a normal float, and uses the old float tests otherwise, which keeps the Vita's flush-to-zero behaviour. It returns 2 for candidate 0's point, so `resolve` no longer compares `w` with 0.
- **Checked:**
  - **Against the old code:** 0 mismatches in 400 M polygons on arm64 and 60 M on i686, in every `res_place` bit pattern on arm64, and in 200 M on i686. The harness was confirmed to fail when it should.
  - **Host FNV per polygon:** byte-identical on A 1×, A 2×, B, and Stone Hill forced 2× at 60 Hz (19.0 M polygons).
  - **Fewer divisions:** host lookup divisions fall 92–94% (Stone Hill 0.50 → 0.03 per vertex). In Vita3K, `vc.div` falls 41–43% on the busy routes.
  - **Host gprof (normalised):** `choose` −13% (A 2×) and −22% (Stone Hill); `res_place` −26% and −40%; `resolve` −5% and −12%.
  - **Vita3K:** shots identical on all seven routes.
  - **Builds:** ctest passes 32 of 32. Not checked on hardware.
  - Logs: `runs/auto-214-1`.
- **Left:**
  - `derive_depth`'s and `same_depthb`'s divisions, and `near8`'s float compare (#225).
  - A tooling trap: under `tools/tc`, swapping a source file in can leave a stale object. Base/fix scripts must delete the object or check the binary's hash.

### D218. With the fix on, glows and moby shadows name their screen points by the projection that made them (#216, 2026-11-23)

- **Why:** after D213, GlowsAndSparkles had 36–86 missed vertices and MobyShadows 16–49 on the host routes (D210's classes). Tagged:
  - glow rims whose key a portal sky cut also placed at another point, so neither was kept;
  - two mobys' shadow corners nudged onto one pixel;
  - rims and corners of an ambiguous source that #202's order-based resolution couldn't give a depth;
  - the rims of a glow whose centre is behind the eye (SZ3 0, MAC3 < 0), which the PS1 still draws.
- **What:**
  - `Glows` (`particles_hand.c`) names its centre (`VCD_SELF`) right after the centre's RTPS. `MobyShadows` (`shadows_hand.c`) names its centre and four corners the same way, by how many projections back each was made.
  - Rims and nudged corners add `VCD_OWN`: the source's own point plus the offset, anchored on the fan's centre. `hk_place` ignores the anchor, so the existing hand-overs are unchanged.
  - Own points are kept per key and anchor in a per-table `ow[2048]` that no hand-over reads; two different points under one key and anchor are dropped.
  - The walk takes own points only for a GlowsAndSparkles or MobyShadows polygon that every earlier step (#183, #189, #215, #181, #197, #202) left unfound, and only when every unfound vertex has one. Then that polygon's found vertices use theirs too, so all its depths come from one projection. They are drawn at their exact place (`VREF_CLAMP`).
  - A vertex whose own projection has no answer is exempt, in a new class `VCX_NO_PROJ` ("no projection").
  - New counters `own_*` and a `vcache own points:` line.
  - D182, D207, D213 and D215's rules are unchanged.
- **Measured (host, 2×, 16:9, missed before → after):**

  | Route | Missed (30 Hz) | Missed (`--interp`) |
  |---|---|---|
  | P | 87 → 25 | 178 → 51 |
  | S | 645 → 581 | 1,294 → 1,166 |
  | Pf | 72 → 36 | 151 → 68 |

  - GlowsAndSparkles is 0 on all six. MobyShadows is 0 except on S (4 at 30 Hz, 5 with `--interp`).
  - "No projection": 32 at 30 Hz, 64 with `--interp`.
  - In Vita3K, stone60's `vc.misses` is −50.7%. owner60's `vc.rec.probe` is +2.4%, the cost of the own-point records.
- **Checks:**
  - Host FNV per polygon: 0 changed in 6 pairs, with 8–112 polygons newly found. `hk_full` and `own_full` are 0 on every route.
  - Vita3K: shots identical on all seven routes (owner60 and stone60 with `dd_budget=1`).
  - ctest passes 32 of 32. The native and Vita builds exit 0.
  - Logs: `runs/auto-216-1`.
- **Left:** MobyShadows on S (4/5), and docs for the new class and line (#226).

### D219. Hardware session 7: autopilot 687ca1d on the Vita, `dd_budget=auto` and the attract demos (2026-11-23)

- **Run:** with the owner's yes ("vita is free if you want to test on it"), AP18a–AP18c, AP18o and AP18t (`runtime/autopilot/hw-session7b.sh`, logs `logs/hw-ap18a`–`hw-ap18t`). Builds: `autopilot-687ca1d.vpk` (SHA-1 173ae0fb, D210–D218) and, for an A/B, `autopilot-53a6a5d.vpk` (933abfe9, D210–D215). US disc. Stone Hill runs use `ap16-stone60.txt` with virtual vblanks, so busy % is the measure there.
- **`dd_budget=auto` against the fixed 48 KB (AP18a against AP18b), Stone Hill play, 7,500–9,500:**
  - Busy 69.1% against 68.3%; worst 88% against 87%.
  - Auto reaches x1.03–x1.41 where 48 KB stays at x1.00–x1.31. Most windows still hold at 1×, now with a measured reason on the lod line: `budget held at 1x: 15.2 ms used of 14.5, worst 22.3 ms`. So the byte budget was not the only limit: the frame itself is near the target at 1×.
- **D216–D218's cost (AP18a against AP18o, 53a6a5d):** busy 69.1% against 68.6%, sky 0.50–1.05 ms against 0.51–1.01 ms per vblank. Nothing measurable.
- **AP18c (real time, stone60) isn't usable:** the route saves a shot every 100 frames, which costs about 9 late vblanks each in real time (27 per 300 even on the title screen), and the pad drifted before Stone Hill. Real-time checks use the Toasty route, which takes no shots.
- **Toasty route, real time, owner's settings (AP18t against AP16t, D209):**

  | Level | Late per 300, session 6 | Session 7 | Busy % |
  |---|---|---|---|
  | Title screen | 0.0 | 0.2 | 48.9 |
  | Stone Hill attract demo | 177.4 | 166.3 | 28.1 (was 89.0) |
  | Artisans | 0.5 | 2.5 | 56.6 |
  | Toasty | 0.2 | 0.9 | 64.5 |

  - **Attract demo:** D211 took the 16:9 real frame off the game thread (busy 99% → 28%), but the game thread now waits 11.8–12.0 ms a vblank for the presenter's worker (`the presenter on this thread` 20.5–26.8 ms per tick), and ticks per 300 are still 92–119 instead of 150. Filed as #227.
  - **60 Hz play:** in Artisans play (11,100) the game thread takes 15.93 ms a vblank at 1× (env 7.80, mobys 4.20, sky 1.77), with 14 late. In Toasty it's 11.6–13.8 ms. `dd_budget=auto` holds at 1× there (`16.4–17.4 ms used of 14.5`), reaches x2.00 where the frame is light (18,300–20,400), and backs off as designed elsewhere. Artisans' sky went from 1.20 to 1.77 ms between sessions, most likely D213. Filed as #228.
- **Left on the Vita:** 687ca1d's eboot (SHA-1 7ad9cdc5, READBACK IDENTICAL).
- **Goal 2 (phase 8):** draw distance doesn't hold at 2× on the owner's settings. The lod line now shows the measured reason (the 60 Hz frame is over 14.5 ms at 1× in Artisans and Toasty), but late vblanks remain, so goal 2 stays open on #227 and #228. A scene under 60 resets `rounds_without_gain` to 0.

### D220. With the fix on, the world's split pieces name their corners by the projections that made them; a split point with an end behind the eye is exempt (#217, 2026-11-23)

- **Why:** after D218, EnvDraw had the most missed vertices: 451 on route S at 30 Hz and 883 with `--interp`, and 20–40 on P and Pf. A temporary tag (`runs/auto-217-1/host/diag217.py`) sorted them:
  - Almost all were `SplitLast`'s split points (`SplitMid`, a `VCD_PERSP` hand-over) on a piece corner the GTE had clamped to the screen's limit, e.g. (1023,-1024). Other clamped points share that key, so the hand-over had no single source, and the split points made from those had none either: 341 of 447 on S at 2×.
  - 39 had a corner behind the eye, so there was no projection at all.
  - 58 were quads whose derive (#183) took no place and whose plane gave no depth.
  - "Source evicted" never happened (`hk full 0`).
- **How** (`src/game/env_hand.c`, `src/platform/common/vcache.c`, `psx_vcache.h`):
  - A piece queued for the last list (`Subdiv4x4Quad`/`Tri`) names each corner (`VCD_SELF`, p = 24 − grid index for a quad, 14 − grid index for a tri) under the anchor `VCD_CORNER(first vertex's key, corner number)`, so two corners on one clamped key stay apart. `SplitMid` passes `VCD_OWN | VCD_ENDS(ia, ib)` with that anchor, and each GT3 that `SplitLast` emits re-names the corners it keeps under its own anchor (`own_move`).
  - A third pass in `derive_hand`, only after the hand-over's two passes (#202) fail, takes an unusable end at its own point: the projection's exact place, at most 16,384 px off (a clamped end's true point lies far off screen). The result is tainted, so it counts only where nothing else placed the key.
  - A split point with an end that has no projection marks its key `HK_NOPROJ` (an Hk slot not placed, `low` set). No hand-over reads it, the next one that places the key overwrites it, and the walk counts the vertex as `VCX_NO_PROJ`, exempt.
  - The walk's own-point step (D218) takes EnvDraw polygons too.
  - Where derive took no place and the plane gives none, the vertex keeps its place and takes a named projection's own depth (`der_named`).
  - New counters `hk_own`, `der_named`, `hk_noproj` and a `vcache world:` line. D182's hand-over rules, D205's pass order, D213's `fr` store and D218's `ow` store are unchanged.
- **Measured (host, 16:9, 2×, base 272fb7e):**

  | Route | Missed, all renderers | EnvDraw missed | No projection (exempt) |
  |---|---|---|---|
  | S | 581 → 160 | 451 → 30 | 32 → 90 |
  | S `--interp` | 1,166 → 343 | 883 → 60 | 64 → 187 |
  | P | 25 → 9 | 20 → 4 | 32 → 32 |
  | P `--interp` | 51 → 24 | 35 → 8 | 64 → 64 |
  | Pf | 36 → 18 | 22 → 4 | 32 → 32 |
  | Pf `--interp` | 68 → 36 | 40 → 8 | 64 → 64 |

  Vita3K stone60 at 6,600: EnvDraw missed 771 → 100, with 564 vertices now exempt. owner60: 32 → 6 at 6,600.
- **Checks:**
  - Host FNV per polygon (D168), six pairs from freshly built objects: same polygon counts, 0 found-before polygons changed, 0 became affine; 13–487 newly found per route.
  - Vita3K: all seven routes exit 0 on the first try, with shots identical to `base-278ebf8` (owner60 and stone60 with `dd_budget=1`). Draws, triangles, `vc.proj`, `vc.polys`, `vc.verts`, `vc.probe` and `vc.lines` unchanged; only the timing counters move, as much as two runs of one VPK do.
  - Host ctest 32 of 32, native and Vita builds, `noassets`. Logs: `runs/auto-217-1`.
- **Left:** EnvDraw keeps 29–30 on S (59–60 with `--interp`) and 4/8 on P and Pf:
  - two split points of different pieces on one pixel, 600–1,300 px apart, which D182 drops (all of P's and Pf's);
  - split ends whose depths are 4× apart (`hk_pair`'s rule);
  - quads on the #197 path, and named projections `choose` rejects;
  - own points not named or dead (two corners of one piece on one key).
  
  Filed as #229. The `vcache world:` line and the wider "no projection" class go into #226's docs.

### D221. The vertex cache skips an exact repeat's maths; the main sky's rise between hardware sessions 6 and 7 is not in its code (#228, 2026-11-23)

- **Why:** in hardware session 7 (D219), Artisans play at 60 Hz and 1× took 15.9 ms a vblank on the game thread, over the auto budget's 14.5 ms target, and the sky rose from 1.20 to 1.77 ms. Host gprof on an owner60-like route at 60 Hz (`runs/auto-228-1/host`) puts the record's maths first: `rtp_apply` 18.2% self, `psx_vcache_poly` 6.1%, `choose` 1.5%, `add_cand` 1.4%; the renderers' own code is small. About a fifth of the projections the record works out are exact repeats (same register generation, same vertex, same key in one frame): 4.3 M of 17.3 M on that route, e.g. a point two sky sectors share or a world corner projected again.
- **How (`src/platform/common/vcache.c`):**
  - Each table keeps, per entry, the last projection worked out (`Rp`: generation, vertex, clamped; 12 bytes, `rp[VC_SIZE]`), cleared when `find` makes the entry.
  - While the entry is valid with one point (`E_VALID`, not `T_MULTI`, not `T_PEND`), a repeat changes nothing: its answer is already among the candidates, `add_cand` stops at the same one, and nothing is logged (#181 logs only keys with different points). So only its counts are kept (`VCR_WORKED`, clamped) and its maths is skipped, both for projections worked out at once (`rtp_point`) and for kept ones as `lz_flush` works them out. `rtp_apply` returns its clamped flag.
  - A `vcache repeats:` line reports the skips; `psx_vcache_set_repeat(0)` turns it off. The `vc_repeat=` config key needs `src/platform/vita/main.c`, outside the issue's files (#232).
- **The sky:** the rise is the main sky (`SkyDraw`, `sky main` 1.07 → 1.63 ms); the portal sky's part, D213's, stayed at 0.13–0.14 ms. `SkyDraw`'s path is unchanged since 85a7b2c apart from D216's cheaper `pt_np`; its time is the same on the host side by side (3,878 against 3,855 ms) and in Vita3K, and its per-frame work on hardware is equal (`vc.proj` 5,672 against 5,678 a frame). So it's a hardware timing or memory effect, or one-run spread: nothing to remove. Session 7 against 6 is +15–52% in Artisans windows but −14% to +14% in Toasty.
- **Measured:**
  - Host, base and fix side by side at 60 Hz (relative): env −22%, sky −19%, renderers −13%; record divisions 14.07 M → 11.12 M.
  - Vita3K owner60, `dd_budget=1`, 6,900–8,700, two runs each (relative only): game thread 4.09–4.17 → 3.94–3.98 ms a vblank, env 1.41–1.47 → 1.27–1.29; `vc.div` −15.7% against two head runs (spread 0.1%), every other counter the same.
  - Missed counts on host S, P and Pf unchanged (160, 9, 18).
- **Checks:** host FNV per polygon identical on 7 pairs (O60, S, Si, P, Pi, Pf, Pfi; 136 M polygons); Vita3K shots identical to `base-278ebf8` on all seven routes; host ctest 32 of 32; Vita build; `noassets`. Logs: `runs/auto-228-1`.
- **Left:** the hardware A/B, since the 96 KB of `Rp` per table adds a memory access per record on the A9 (#232, with the key). Toasty is walk-bound in session 7 (the game thread's join wait 1.71 → 2.51 ms, the walk on core 1 93% busy) and has no Vita3K or host route (#233). The sky's run-to-run spread on hardware goes in the next session: two runs of one VPK.

### D222. In an attract demo at 16:9 the in-between frame runs inline, and only the real frame goes to the presenter's worker (#227, 2026-11-23)

- **Why:** on hardware (D219) the game thread waited 11.8–12.0 ms a vblank for the presenter's worker in the attract demos, and they still ran 166 late vblanks in 300.
- **Measured:** a temporary per-job split in `present.c` (Vita3K `demo60`, three base runs; host `--pres-check-thread`) shows the wait is the **in-between frame's job**: the real frame's pass joins it before it can start, because the presenter has one snapshot and the real frame starts from the visibility state the in-between frame leaves (13.5–13.9 ms of 14.4–14.8 ms "on this thread" per tick). The real frame's own join after the hidden logic draw is ~0 in Vita3K. On hardware (`logs/hw-ap18t`, 4,500 window) a demo presenter frame takes about 24 ms: the worker ran 200 jobs at 16.06 ms a vblank (95% busy), the game thread waited ~25 ms a tick for the in-between frame and ~11 ms after the logic draw for the real one.
- **How (`src/game/present.c`):** `presenter_pass` gets `no_worker`; with `g_present_demo_inline` (1, the default) the in-between frame of an attract demo at 16:9 runs inline before the real frame goes to the worker. 0 gives D211's order back. `docs/presentation.md` describes it.
- **Result:**
  - Vita3K `demo60`, base and fix twice each, interleaved: the game thread's presenter waits fall from 11.3–13.9 ms a tick to under 0.05 ms; the inline frame takes 11–12 ms a tick in their place; wall time per 300 vblanks stays within the noise (5,809–6,247 ms against 5,819–6,581). Vita3K's routes use virtual vblanks, so ticks per 300 can't move there.
  - Host FNV per polygon identical on all 9,124,329 polygons (title and demos to 7,000, `--vcache --widescreen --interp --draw-distance 2`); `--pres-check-thread` matches 3,475 of 3,475 frames; the seven routes' shots identical to `base-278ebf8`; host ctest 32 of 32; Vita build; `noassets`. Logs: `runs/auto-227-1`.
- **Limit:** the two presenter frames still run one after the other, so on hardware a demo tick still costs the update plus two ~24 ms frames, over 33 ms. Little change in late vblanks is expected; the next hardware session judges it (Toasty route, 4,200–6,300). The real gain needs fewer or cheaper presenter frames in the demos (#234). The `pres_demo_inline` config key and `pres_thread.c`'s stale comment are outside the issue's files (#232).

### D223. With the fix on, a world split point and a corner the grid moved are kept as their piece's own, and a world piece's anchor mixes its corners' keys and u/v (#229, 2026-11-23)

- **Why:** after D220, EnvDraw kept 30 missed vertices on host route S (60 with `--interp`) and 4 (8) on P and Pf. #217's tags (`runs/auto-217-1/host/reasons_2x.txt`): two split points of different pieces on one pixel, which D182 drops (4 / 8); split ends 4× apart in depth (9 / 13); quads whose moved corner derive couldn't place (8 / 24); own points dead under a shared first-vertex anchor (8 / 12). The last was also Vita3K stone60's 100 at window 6,600.
- **How** (`src/game/env_hand.c`, `src/platform/common/vcache.c`, `psx_vcache.h`):
  - A per-table `Sp` store (1,024 records of 44 bytes, about 45 KB a table), apart from hk, keeps each `SplitMid` point with its place and depths under its piece and edge (`VCD_CORNER(anchor, VCD_EDGE(ia, ib))`). Each GT3 that `SplitLast` emits moves it to its own anchor (`own_move` with p = 1, falling back to `Sp`), and a re-split GT3 finds its ends there (`sp_end`). The value is the key-sourced pass 1's when that succeeds; otherwise it's made from the piece's own ends (`own_end`, then `sp_end`), paired even across 4× depths (`hk_pair`'s `wide`, for this record only). No hand-over reads `Sp`, so hk is unchanged.
  - GridCorrect's moved points are named once at their pre-move projection (`NameMoved` in `Grid5x5Inner`/`Grid3x3Centre`, `VCD_MOVED`), and a moved queued corner is also kept in `Sp` under its piece.
  - The piece anchor (`PieceAnchor`, `VCD_PIECE_V`) is all its corners' keys plus their u/v, not the first vertex's key: two GT3s of one piece that start at one point, with two corners clamped onto one key, had named both under one anchor and killed both.
  - In the walk, for EnvDraw polygons nothing else found: a corner's own point may lie up to 2^20 px off its key (a clamped corner nearly level with the eye projects far off screen); a polygon whose own-point depths no choice brings under 4× apart is found and drawn affine (`spread`); a quad's ambiguous corner is resolved with its moved corner's own projection, or the moved corner alone is found and the polygon stays affine (`sp_part`).
  - New counters and a `vcache world own:` line (`sp_found`, `sp_kept`, `sp_full`, `sp_wide`, `sp_moved`, `sp_spread`, `sp_res`, `sp_part`). D182, D205, D213, D218 and D221 are unchanged.
- **Measured (host, 2×, 16:9, base 8bdf7d4):**

  | Route | EnvDraw missed | All missed |
  |---|---|---|
  | S | 30 → 6 | 160 → 136 |
  | S `--interp` | 60 → 6 | 343 → 289 |
  | P | 4 → 0 | 9 → 5 |
  | P `--interp` | 8 → 0 | 24 → 16 |
  | Pf | 4 → 0 | 18 → 14 |
  | Pf `--interp` | 8 → 0 | 36 → 28 |

  #217's four reasons all go to 0 except "own point not named or dead" (8 → 6, 12 → 6). Vita3K EnvDraw missed: owner60 6 and 12 → 0, stone60 100 → 0; `vc.probe` +0.2%, `vc.lines` +0.3–0.4%, `vc.div` +0.2–0.6%; draws, triangles, `vc.proj`, `vc.polys`, `vc.verts` unchanged. `Sp` held at most 5,214 records a 300-vblank window and never filled (`sp_full` 0).
- **Checks:** host FNV per polygon, 6 pairs from freshly built objects: same polygon counts, 0 found-before changed, 0 became affine (4–39 newly found). Vita3K shots identical to `base-278ebf8` on all seven routes. Host ctest 32 of 32, native and Vita builds, `noassets`. Logs: `runs/auto-229-1`.
- **Left:** the 6 on S and S `--interp` are all in one frame (g6742) whose order log is full (`VC_LOG`, 8,192 projections), so the cache never named those projections. Vita3K never fills the log. Raising `VC_LOG` to 16,384 fixes them but changes 50 / 124 found-before polygons in that frame and costs about 224 KB a table (#237). Relaxing hk's own 4× rule for split points changed no found-before polygon on S in an experiment, but isn't adopted. Docs for the `vcache world own:` line go with #226.

### D224. The attract demos' presenter frames already draw at 1× whatever the draw distance, so a demo draw-distance budget has nothing to cut (#234, 2026-11-23)

- **Why:** #227 (D222) put a demo presenter frame at about 24 ms on hardware and supposed it drew 16:9 at 2×.
- **Measured:**
  - The presenter's frames take the game's lod state through `pres_draw.c` `lod_begin` (`port_lod_draw_begin(1)`, result kept for the presenter, the game's state put back). In a demo the game's own draws are the hidden 4:3 logic draws (`shown` 0), so the game's limiter (`port_lod_draw_end` → `limit`) never runs, and the hold of 60 that the last shown draw before the demo set (level 11's reset to 1×) never counts down. A temporary `T234` line read scale 256 (1×) with the hold at 60 in every window from 4,080 to 5,580, on the host and in Vita3K.
  - So hardware's 19–28 ms a demo presenter frame (`logs/hw-ap18t`, 4,500–6,000, worker busy × 300 / jobs) is 16:9 at 1×.
  - Forcing 2× in the demos (a temporary build, measurement only): Vita3K `demo60` GL draws +34%, triangles +15%, presenter projections 4,398 → 5,185 a frame, in-between frame 7.9 → 8.3–10.0 ms; on the host about +18% a demo frame.
  - With the vertex cache off (`persp=0 subpixel=0`), Vita3K's demo frames fall about 70%: in-between 7.9 → 2.5 ms, real frame 7.3 → 2.0, hidden logic draw 4.8 → 1.2. Its share on the A9 is unmeasured.
- **Result:** no code change; `docs/presentation.md` records why the demos draw at 1× and the numbers. All Vita3K shots identical (demo60 66/66, owner60 91/91, stone60 95/95).
- **Option for the owner (not implemented):** skip the in-between frame in a demo tick when two presenter frames and the update don't fit the tick. On hardware a tick would then be ~1 ms plus max(logic draw 9–13, real frame 19–28) ≈ 21–29 ms, so the demos would play at the original speed (about 150 ticks per 300, against 85–124 now) at 30 fps. It would be on for the whole Stone Hill demo on hardware and never in Vita3K. Asked in #235.
- **Left:** the next hardware session sizes the vertex cache's share of a demo frame (Toasty route, 4,200–6,300, owner's settings against `persp=0 subpixel=0`). Making the demos' 1× an explicit rule in `lod.c`, so a later limiter fix can't quietly move them to 2×, is #236.

### D225. The attract demos' presenter frames draw at 1× by an explicit rule in `lod.c` (#236, 2026-11-23)

- **Why:** D224 found the demos' 16:9 presenter frames at 1× only because the game's hold (60, from the last shown draw's level reset) never counts down while the logic draws are hidden. A later limiter fix could quietly move them to 2×, about +18% a demo frame (D224), on frames already over the tick on hardware.
- **How (`src/game/lod.c`, comment in `lod.h`):**
  - `s_demo_shown` (part of `LOD_STATE`, so `lod_begin`'s save and restore and `--render-ctx-check` carry it) is "the game's last draw was a shown attract-demo draw", set in `port_lod_draw_end`, which only `game_draw` calls.
  - In the game's `port_lod_draw_begin` (`#ifndef RENDER_PRES`), after the existing clamp: an attract demo draws at 1× until the game's last draw is one of the demo's own shown draws. That covers the presenter frames of 16:9 demos, including the first tick, whose 16:9 frame runs before its first hidden logic draw (a "last draw hidden" rule missed that one frame at 3,931). Demos whose logic draw is shown (4:3, `ws_logic`, no presenter) keep the limiter.
- **Checked:**
  - With 2× forced in front of the rule (temporary `T236` build), Vita3K `demo60` reads 1× in every window from 4,080 to 5,580, and its shots are 66/66 identical to base.
  - Final build: shots identical to `base-278ebf8` on all seven routes; owner60, stone60 and demo60 lod lines byte-identical (`dd_budget=1`); demo60's counters all the same against a same-code head build run twice. Host ctest 32 of 32, Vita build, `noassets`. Logs: `runs/auto-236-1`.

### D226. The walk's lookup of a polygon found whole takes a short path of its own; Toasty gets a host route and a Vita3K route (#233, 2026-11-24)

- **Why:** in hardware session 7 (D219) Toasty at 60 Hz was walk-bound: the walk on core 1 93% busy, the game thread's join wait 2.51 ms a vblank. Neither the host nor Vita3K had a route there.
- **Routes:**
  - Host route T: `--pad tools/oracle/pads/newgame_host.txt --tour 14 --tour-stay 300 --pad-after tools/oracle/pads/route_toasty.txt --tick60 --interp --frames 10000 --vcache --widescreen --draw-distance 2` (Toasty play anchored at 7,049).
  - Vita3K `toasty60` (added to `autopilot/config.json`): `o-toasty.pad` with `o-toasty.cfg`'s menu warp, the owner's settings with `dd_budget=1`, `shot_every=100` to 15,000. Artisans play from 10,648, the warp at 11,400, Toasty play from 11,440; 1,821 polygons a frame against 1,824 on hardware. Timing windows 11,700–12,600. Picture baseline: `runs/auto-233-1/fix-toasty60` (150/150 identical to its base).
- **Found** (Vita3K Toasty, 11,700–12,600, `vc_lazy_logic=0` so the walk only looks up, as on hardware; relative): the walk 3.7–4.1 ms a vblank against 0.7 with `persp=0`. The vertex cache's lookup is about 80% of it (about 2.4 ms the lookups, 0.5–1 ms the choice and answers); conversion, splits and GR calls about 0.7 ms. `gpu_walk.c`'s own code is small.
- **How** (`src/platform/common/vcache.c`, `psx_vcache.h`): `psx_vcache_poly` does the lookups (`look`, `lz_need`) and, when every vertex is found valid (99% in Toasty), finishes the polygon on a short path (choice, counts, references, side answers); anything else goes to `poly_full`, the old body, which looks up again with the first pass's counts dropped. `poly_full` had set up about 1.4 KB of stack and ~30 stores a polygon. Mode 2 (default) also reads each vertex's first probe up front so a polygon's table misses can overlap on the A9. `psx_vcache_set_found_fast(0|1|2)`; the config key is #232.
- **Measured:** host gprof walk functions −5.5% (63.0 → 59.6 s, route T). Vita3K within its noise (base 2.82–4.06 ms, fix 3.21–3.93).
- **Checks:** host FNV per polygon identical on 8 pairs (T, O60, S, Si, P, Pi, Pf, Pfi; 150 M polygons); host logs identical apart from timing lines; missed counts unchanged; shots identical on the seven routes and on `toasty60`; host ctest 32 of 32; Vita build; `noassets`. Logs: `runs/auto-233-1`.
- **Left:** a hardware A/B of `found_fast` 0/1/2 on the Toasty route (#232 adds the key). Route T's missed line reads 1,203 (SpyroDraw 981, MobyShadows 220), a class S, P and Pf never showed: #242. A key/tag array apart from the candidates would cut table lines per lookup from ~2.45 to 2, but it's a large `Entry` change and isn't filed yet.

### D227. With the fix on, Spyro's clip points are kept as their polygon's own, and a far moby shadow's nudged corner takes its corner's own projection (#242, 2026-11-24)

- **Why:** host route T (Toasty, 60 Hz, D226) read `missed 1203 (SpyroDraw 981, MobyShadows 220, EnvDraw 2)`, a class S, P and Pf never showed. Tagged (`runs/auto-242-1/host/diag242.py`, `classify242.py`):
  - Spyro partly under the floor (`ClipTriangle`): two clip points of different edges round to one pixel at different exact places, so D182 drops both (951); and clip edges whose end key holds two of his vertices (30).
  - Shadows a few pixels wide with two corners on one key: the nudged corner's alias was dead or ambiguous (175 / 45), and #216's fallback had nothing, because the own point under (source key, centre) held two corners.
  - All in the logic frames, none in the presenter.
- **How** (`src/game/pete_render_hand.c`, `shadows_hand.c`, `src/platform/common/vcache.c`, `psx_vcache.h`):
  - `ClipTriangle` hands its clip points over once the primitive is whole, in the same order with nothing between, so hk sees exactly what it saw. With `VCD_KEEP` each is also kept in D223's `Sp` store under `VCD_CORNER(the primitive's VCD_PIECE_V anchor, its place)`; where hk's sources give no place, `pk_place` takes the ends from their own projections (end indices in `VCD_PETE_ENDS`).
  - While Spyro is clipped and the cache records, `PartVertices` names each vertex slot's final projection (`VCD_SELF`) under `VCD_PETE_V(index)` (anchor y = 32767, which no screen key has); a clipped primitive's real vertices are kept in `Sp` the same way.
  - The walk takes these only for a SpyroDraw polygon nothing else found, for each vertex with no entry or an ambiguous key, and only when every such vertex has one.
  - A shadow's nudge passes its corner's projection count back (`VCD_ALIAS|VCD_OWN`, q = 5 − (c − v)/4). Where the source's own point is dead, `own_corner` keeps that projection (key-checked) as a separate class (`placed = 2`) that gives way to every other own point, is no hand-over's source, leaves neither when two differ under one key and anchor, and serves a found vertex only after #216's two tries fail.
  - New counters `pk_found`, `pk_kept`, `pk_full`, `own_corner` and a `vcache Spyro and shadows own:` line. hk is unchanged; D182, D205, D213, D218, D220, D221, D223 and D226 are kept.
- **Measured (host, 2×, 16:9):** T 1,203 → 2 (EnvDraw 2); SpyroDraw and MobyShadows 0 on every route; S 136, Si 289 → 288, P 5, Pi 16, Pf 14, Pfi 28, O60 0. `own_full`, `sp_full`, `pk_full` 0 everywhere. Vita3K `toasty60`: SpyroDraw and MobyShadows windows read 0; `vc.misses` −54%; `vc.rec.probe` +0.6% (about 31 probes a frame; two runs of the base VPK differ by 0.0%), `vc.probe`/`vc.lines` +0.3%; draws and triangles the same.
- **Checks:** host FNV per polygon on 8 pairs (T, O60, S, Si, P, Pi, Pf, Pfi; 157 M polygons), base and fix from clean objects: 0 found-before changed, 0 became affine (1,151 newly found on T). Vita3K shots identical on all eight routes (toasty60 150/150 against `auto-233-1/fix-toasty60`). Host ctest 32 of 32, native and Vita builds, `noassets`. Logs: `runs/auto-242-1`.
- **Left:** toasty60's attract-demo windows (4,200–5,700) show MobyDraw 2,508, ParticlesDraw 239 and 146, SkyDraw 6 missed, untagged and unchanged here (#243). Docs for the new line go with #226.

### D228. Host builds can check that every change to an NgteProj after use takes a new generation (`VC_CHECK_GEN=1`, `gen_stale`) (#218, 2026-11-24)

- **Why:** since D202 the record treats two projections with the same generation as having the same registers (`same_ngte`, and `rp_skip` since D221). A renderer that changed a field without `ngte_proj_regen` would place vertices wrongly, and FNV dumps only catch that on a route that happens to draw it.
- **How** (`src/platform/common/vcache.c`, `psx_vcache.h`; comment in `src/game/gte_native.h`): in host builds only, with `VC_CHECK_GEN=1` or `psx_vcache_set_check_gen(1)`, `ngte_vcache` and `pres_vcache_ngte` (every native projection reaches the record through them, so `rp_skip` and every later reuse are covered too) keep each generation's 17 fields (r11–r33, tr, ofx, ofy, h, dqa, dqb) as its first projection had them, for the last 8,192 generations per kind. Every later projection of that generation is compared word by word; each that differs counts as `gen_stale` under the renderer context that returns next (#211's names), and the first 16 are logged with the field, old and new value and the caller's address. A `vcache gen check:` line comes with each report while it's on. Generation 0 is skipped. Off by default, one load and branch a projection when off, and compiled out of the Vita build (`#ifndef __vita__`). ctest `vcache_gen` covers it.
- **Result:** `gen_stale` 0 on 36 host runs: S, P, Pf, T, O60 and the title and attract demos, each at 30 Hz, `--interp` and `--tick60 --interp`, at 1× and 2× (up to 54 M logic and 38 M presenter projections compared a run). Missed counts with the check on match D227 exactly. No renderer needs a follow-up.
- **Checks:** the Vita ELF's `.text` and `.data` are byte-identical to the head's (only the build-date string in `.rodata` differs); Vita3K shots identical on all eight routes; host ctest 33 of 33; native build; `noassets`. Logs: `runs/auto-218-1`.

### D229. With the fix on, a record whose probe runs out makes its entry in an overflow apart from the table's slots, read only for SkyDraw polygons (#221, 2026-11-24)

- **Why:** SkyDraw's missed vertices (D210: 111 on host S at 30 Hz, 244 with `--interp`, 2 on Pi) all had one cause, "no record" (tagged with `runs/auto-221-1/host/diag221.py`, `classify221.py`). Late in a busy Stone Hill frame at 2× (about 4,000–5,000 keys in 8,192 slots), the record's linear probe met `VC_PROBE` = 32 taken slots in a row and gave up, counted only in `full`. The main sky records last, so now and then one fan-centre vertex had no entry, and each of its 4–9 triangles was missed; the rim vertices were found through neighbouring sectors. `sky_hand.c` was already correct.
- **How** (`src/platform/common/vcache.c`, `psx_vcache.h`):
  - A record whose 32-slot probe fails makes its entry in a 64-entry overflow after the table's slots in the same array (`ov_find`; `Entry s[VC_SIZE + VC_OV]`, with `last`, `rp` and the lazy store's `head` widened to match).
  - The slots hold exactly what they held before, so every key's probe ends where it did. An overflow entry gets no place in the frame's order (`VC_NOSEQ`), is worked out at once and never kept for the walk, and is no hand-over's source (sources are still looked up with the 32-slot `find`).
  - The walk reads the overflow only for a vertex not found in the slots in a polygon `cx_find` names SkyDraw, and only in a frame whose records made one (`Table.ov_gen`). `psx_vcache_get`'s bound includes the overflow.
  - Probing on to the first free slot instead (two earlier versions) changed 116–122 found-before polygons on S: an entry in the free slot after a full run displaces later keys, and the slot number is part of a vertex's ref.
  - Off switch `psx_vcache_set_overflow(0)` / `VC_OVERFLOW=0` on the host (on S it gives back the head's numbers exactly: missed 136, SkyDraw 111, 45 not recorded). New counters `ov_kept`, `ov_found` on a `vcache overflow:` line. D182, D205, D213, D216, D218, D220, D223, D227 and D228 are unchanged.
- **Measured (host, 2×, 16:9, base b17a0de):**

  | Route | Missed | SkyDraw | Overflow records / SkyDraw vertices found there |
  |---|---|---|---|
  | S | 136 → 25 (ParticlesDraw 10, EnvDraw 6, PortalSkyPlain 5) | 111 → 0 | 45 / 111 |
  | S `--interp` | 288 → 44 (ParticlesDraw 20, PortalSkyPlain 14, EnvDraw 6) | 244 → 0 | 99 / 244 |
  | P | 5 → 5 | 0 | 0 / 0 |
  | P `--interp` | 16 → 14 | 2 → 0 | 1 / 2 |
  | Pf | 14 → 14 | 0 | 1 / 0 |
  | Pf `--interp` | 28 → 28 | 0 | 1 / 0 |
  | T | 2 → 2 | 0 | 0 / 0 |
  | O60 | 0 → 0 | 0 | 0 / 0 |

  Vita3K demo60/toasty60's SkyDraw 6 and 4 (windows 4,200 and 5,400) → 0; owner60, stone60 and wobble60 make no overflow records. At most 3 overflow records were seen in one frame.
- **Checks:** host FNV per polygon on 8 pairs (151 M polygons), base and fix from clean objects: 0 found-before changed, 0 became affine (111 / 244 / 2 newly found). Vita3K shots identical on all eight routes. `CAPTURE=1` grabs at the only Vita3K frames that make overflow records (demo60 3,952–3,954 and 5,175–5,177, presenter frames, sky in view) are byte-identical to fix-off: no cracks. Counters the same against a second head run apart from timing ones inside the head's own spread. Host ctest 33 of 33, native and Vita builds, `noassets`. Logs: `runs/auto-221-1`.
- **Left:** on S, 19 of the 45 overflow records are other renderers' keys, unread under the SkyDraw-only rule; whether any of S's remaining ParticlesDraw 10 / EnvDraw 6 come from them is noted on #231 and #237. A better hash or bigger table would shorten probe runs (the multiplicative hash on y<<16|x clusters at 60% load), but every slot would change. Docs for the new line go with #226.

### D230. The vertex cache's counters find a renderer's primitives by their offset in the game's memory, the walk's copy's too (#222, 2026-11-24)
- **Why:** D210 filed 1,086 code-2c vertices on host S (2,175 with `--interp`) as outside although their polygon had a placed vertex. #222 traced every one. All are 2D, drawn by the game's C into the HUD OT (`func_800168DC`):
  - the title menu's border and menu sprites (`TitlescreenDrawSprite`), read from present.c's narrowed copies;
  - the screen copy (`func_8001A40C`).

  One corner of each shares an integer pixel with a 3D projection behind it, so they stay exempt. No renderer range was dropped and `VC_CX` was never full on S, Si, P, Pi, Pf, Pfi or a 60 Hz stone60 host route.
- **The Vita gap (D213):** on the native build the walk reads PS1 primitives (portal sky, portal faces) from walk.c's malloc'd copy, and neither that copy nor the game's block is 16 MB-aligned. So the low-24-bit compare filed them as outside, and D210's missed count under-reported the portal sky on the Vita.
- **How:** `cx_ofs()` maps an address to its offset in the game's memory; a polygon in the walk's copy (`psx_gpu_game_walk()->copy`) maps to the same offset. Other addresses keep their low 24 bits, tagged so they can't match. Each `Cx` keeps both forms. Only the counter's lookup at `miss_none` uses the offset form (`cx_find_(…, 1)`); the placement gates (#215, #217/#229, #221, #227) still compare the low 24 bits.
- **Measured (Vita3K, head → fix, run totals):**

  | Route | Missed | Outside | Portal faces |
  |---|---|---|---|
  | stone60 | 8 → 46 | 23,176 → 22,925 | 0 → 213 |
  | owner60 | 0 → 0 | 31,350 → 30,681 | 0 → 669 |
  | wobble60 | 0 → 0 | 31,348 → 30,677 | 648 → 1,319 |
  | pause-ws-wobble | 0 → 0 | 31,236 → 30,681 | 0 → 555 |

  stone60 window 6,600 now reads `missed 38 (PortalSkyPlain 23, PortalSkyFog 15)`, the same as the host's 60 Hz stone60 route; host O60's outside and portal faces now equal Vita owner60's. Title, pause-ws, demo60 and toasty60 are unchanged.
- **Checks:** host FNV per polygon on 8 pairs: 0 changed, and every host `vcache derived:` line identical. Vita3K shots identical on all eight routes. Counters against the head's run the same apart from timing ones inside their spread. Host ctest 33 of 33, native and Vita builds, `noassets`. Logs: `runs/auto-222-1`.
- **Left:** the Vita's placement gates still use the low-24 compare (filed); the portal sky misses now visible on the Vita go to #230; HUD sprites with a found corner take a 3D projection's sub-pixel place (filed).

## 2026-11-23 — the PC build (Linux; Windows to follow)

### D240. A native desktop build: SDL3 for the window, input and sound, OpenGL 3.3 core for the picture, in the low-image model

(Numbered D240 to keep clear of the autopilot branch, which is at D224 and counting: D199–D239 are left to it.)

- **Why:** Perch Platform's porting work starts on PC (Linux and Windows, then Steam): no devkit, no NDA. linux64 (D173, D174) already runs the whole game byte-identical to linux32 as a 64-bit x86 program, headless; the PS4 build (D193) showed the platform layer that a real device needs is small (window, pad, audio, clock: ~850 lines). What was missing for a desktop: a window that resizes and goes fullscreen, a keyboard and gamepads, sound through the system's mixer, pacing against a display of any refresh rate, and a dynamically linked executable (a desktop's GL and sound libraries are loaded at run time).
- **What:** `SPYRO_HOST=pc` (`src/platform/pc/`, `cmake/pc-linux.toolchain.cmake`, `tools/docker/pc.Dockerfile`, `tools/pc/tc`; `docs/pc-build.md`):
  - **SDL3** (3.4.18, zlib licence, linked statically): window, GL context, events, the gamepad API (XInput, DualShock 4, DualSense, Switch Pro and the rest SDL knows), audio streams that convert to the device's rate. SDL2 is what the Vita uses, but SDL3 is current, has the better gamepad and audio-stream APIs, and is what Steam's own runtime ships.
  - **OpenGL 3.3 core, not Vulkan.** The picture today is one textured quad (the software rasterizer's frame, as the PS4's); the GPU renderer that comes next is the Vita's `gpu_gl.c`, whose shaders are GLSL ES 1.0 on vitaGL: on desktop GL 3.3 they need a prologue (`attribute`/`varying` as `in`/`out`), not a rewrite. Vulkan would mean a new renderer, MoltenVK for any Mac, and far more code for a PS1 game's few thousand triangles. GL 3.3 runs on every PC GPU of the last 15 years, Mesa's llvmpipe included (how CI and a Mac test it), and on macOS (4.1 core) if a Mac build ever comes.
  - **Two threads:** the main thread owns SDL and shows, at every display refresh, the newest frame whose due vblank a 60 Hz clock of real time has reached; the game thread runs the game with the Vita's and the PS4's catch-up model (`common/clock.c`) against that clock and files every frame with its due vblank, as the Vita's render thread takes them with `present_due=1` (D112). The presenter's 16:9 and in-between frames are on by default, as on the Vita. The game never waits for the display, and a 144 Hz or variable-refresh display shows the same frames at their times.
  - **The low image on a dynamic executable (Linux):** non-PIE at 0x400000 (the image, the arena and the game stack below 2 GB, checked at start); every free page from 64 KiB to 4 GB reserved `PROT_NONE` from `.preinit_array` (`low_linux.c`), before any shared library runs, so glibc's own malloc, SDL, the drivers and every thread stack land above 4 GB and a host pointer cut to 32 bits still crashes. linux64's own malloc (one mmap per block) isn't needed: static glibc's sbrk heap is the reason for it, and a dynamic glibc's can't grow into the reservation. `SPYRO_LOW_TRAP=0` turns it off if a driver ever insists on low memory.
  - **The presenter's object keeps its common symbols global** whenever the objcopy is llvm-objcopy (`cmake/presenter.cmake`; D193's `keep_commons.py`, until now the PS4's only). The PC toolchain picked llvm-objcopy, which localized `g_pres_snap_D_80075950`: the presenter's title screen wrote to address 8. linux64 uses GNU objcopy and never had it.
  - A crash's address and backtrace go to `spyro.log` (`crash.c`), the user folder is SDL's (`~/.local/share/spyro-vita/Spyro/`), the disc folder `--disc`, a `disc` folder beside the executable, or the user folder. `pad=` scripts (the Vita's format) drive unattended runs; `tools/pc/routes/play.pad` is the K route into Artisans and some play.
- **Checked** (`logs/pc-*`, amd64 Docker under Rosetta on the Mac, Xvfb and llvmpipe):
  - the US disc image from boot to the title (16:9), New Game, the intro and Artisans; walking, the free camera, jumping and gliding, flame and charge, Nestor rescued, gems: 14,100 vblanks, no fault.
  - 60.0 vblanks a second; with a small window (llvmpipe under Rosetta draws a 1280×720 picture in 15–20 ms, slower than the refresh) 297 of 298 frames shown in a 5 s window, 0 late.
  - No disc: the error, exit 1. Sound: SDL's stream takes the SPU's 44.1 kHz, trimmed, not cleared, after a catch-up.
  - Not checked yet: a real GPU, a real sound card, a real gamepad (none in the container), Windows (next), and a person playing it.
- **Not possible:** an ARM64 build (Apple Silicon, ARM Linux): clang's `__ptr32` is an x86 feature, and the model rests on it.
- **Next:** Windows (cross-built with llvm-mingw, not large-address-aware so the whole process lies below 2 GB), CI for both, then the GPU renderer at the window's resolution.
- **Next free number (this branch):** D241.

### D241. The PC build for Windows: cross-compiled with llvm-mingw, a fixed base and not large-address-aware

- **Why:** D240's next step. No Windows machine here: the build is cross-compiled in the PC image, checked under Wine, and gated in CI on a real Windows runner.
- **What:** `cmake/pc-windows.toolchain.cmake` (llvm-mingw 20261104, clang for `x86_64-w64-mingw32`, the UCRT), SDL3 for Windows built static in the image; one static `spyro.exe` that needs only Windows' own DLLs. `docs/pc-build.md` "Windows" has the list; in short:
  - **The low image:** a fixed base of 0x400000, no ASLR, **not large-address-aware**: Windows keeps the whole process below 2 GB, so every address fits a `P32` field and nothing is cut. No reservation is needed (or possible). The game thread is an ordinary thread (SDL's, 8 MB), its stack below 2 GB like everything else; `main.c` checks it.
  - **mingw's `_mingw.h` defines `__ptr32` and `__ptr64` as empty macros.** Every `P32` field silently became 8 bytes once any system header was in; the hand C's `static_assert`s on `Moby` (104 bytes, not 88) caught it. `src/compat/p32.h` includes `_mingw.h` first and undefines the two (it is guarded, so it never defines them again): clang's keywords remain. Only `_mingw.h`: the overlays are preprocessed and compiled again with `p32.h` forced in, and a second copy of `<sys/types.h>`'s structs is an error.
  - `-mno-ms-bitfields` (the PS1's structs keep their GNU layout), `src/compat/pc_win.h` (the BSD type names), `-Wno-error=incompatible-pointer-types` (Windows' `long *` for an `int *`: the same 32 bits).
  - `tools/data2s.py --coff`, `pres_snap_syms.py --coff`: COFF assembly. The relocatable links (`-r`, the presenter and the 60 Hz update) through GNU ld for PE, which lld's MinGW driver can't do; GNU objcopy edits them.
  - `_set_fmode(_O_BINARY)` (the disc's bytes untouched by text mode), `pread` as a positional `ReadFile`, `mkdir` without a mode, `etext`/`_end` as marks (a save state's layout stamp), winpthreads.
- **Checked:** `llvm-readobj`: base 0x400000, no `DYNAMIC_BASE`, `HIGH_ENTROPY_VA` or `LARGE_ADDRESS_AWARE`; imports Windows' DLLs and the UCRT only. Under Wine 8 (amd64 Docker under Rosetta, Xvfb, llvmpipe): the US disc to the 16:9 title at 60.0 vblanks a second, and the play route (`logs/pcw-run2.log`). CI: the `pc` job builds both; `pc-windows-smoke` runs `spyro.exe` on `windows-latest` with Mesa's llvmpipe `opengl32.dll` beside it, which must report its heap, a fresh mapping and its stack below 2 GB, a GL 3.3 window and the missing disc. (`GetSystemInfo`'s maximum address is the system's, 128 TB, whatever the program: the first CI run showed it.)
- **Not checked:** Windows itself with a GPU, a sound card and a gamepad.
- **Next free number (this branch):** D242.

### D242. The PC build's low image and Windows shims come from Encore, vendored

- **Why:** the task's phase 2: once Encore's rename had landed, the PC build's game-free pieces went to the kit (`ShayVD/encore` #64: the SDL3 backend of its platform interface, lp64's low image for a dynamic Linux executable and for Windows, the Windows POSIX shims), and come back here vendored, so the two never drift.
- **What:** `third_party/encore` (`tools/vendor.sh --dir third_party/encore`, Encore f505f2e): `lp64/runtime/low_image` and `targets/pc/runtime/winposix`. `src/platform/pc/low_linux.c` and `low_win.c` are gone; `low.c` reports, and the build links `lp64_low_image_dyn.c` (Linux) or `lp64_low_image_win.c` and `enc_winposix.c` (Windows). The environment switch is now Encore's `LP64_LOW_TRAP=0` (was `SPYRO_LOW_TRAP=0`).
- **Fixed on the way:** Windows moves a CRT descriptor's file pointer even for a positional `ReadFile`, so D241's `pread` changed the offset; Encore's puts it back (its test caught it under Wine). Spyro reads the disc only through `pread` on its descriptor, so nothing here had gone wrong yet.
- **Checked:** the Linux build and the Windows build, and the play route on each (Linux under Xvfb, Windows under Wine), as D240 and D241 (`logs/pc-vendor-*`).
- **Next free number (this branch):** D243.

### D243. The PC build's GPU renderer: a trimmed copy of the Vita's, on a second GL context the game thread owns

- **Why:** D240's picture was the software rasterizer's 512×240 frame scaled up to the window. A PC can draw the game at its own resolution, and the Vita's renderer already does that on vitaGL (D35 and on): its shaders are GLSL ES 1.00, which desktop GL 3.3 core takes once it is given a version line and the old words are swapped.
- **What:** `gpu=1`, the default; `gpu=0` is the software rasterizer, as before. `docs/pc-build.md` "The GPU renderer" has the details.
  - **A copy, not a shared file.** `src/platform/pc/gpu_gl.c` (about 1,500 lines) is `src/platform/vita/gpu_gl.c` (4,400) with the Vita's parts taken out: vitaGL, `sce*`, CDRAM, the GXM meters and timers, the render thread, and everything that exists for the Vita's budget: the decoded-texture atlas (`tex_cache`, `tex_filter`), `gpu_lean`, `gpu_fused`, `nd_direct`, `gpu_join`, `gpu_opaque`, `gpu_uber`, dynres, the GL state shadow (D171). Kept as they are: targets, their logs and the software replay for read-backs, the read-back drawn down from the GPU when a log overflows, LoadImage/MoveImage/ClearImage into targets, the PS1's semi-transparency, the shaders' text, `persp=` and `subpixel=` (the vertex cache's exact projections, on by default as on the Vita), `place()` and the presentation pictures. Sharing one file would have meant `#ifdef`s through every function the Vita has tuned on hardware (D63–D171), each a chance to change the Vita's output; the Vita build doesn't compile one line differently. The cost: a fix to the model has to be made in both (the Vita's has the history; the PC's names it as its source).
  - **GLSL:** `glsl330()` joins the Vita's pieces, puts `#version 330 core` first and swaps `attribute`, `varying`, `texture2D` and `gl_FragColor` where they stand as whole words (`#define`ing keywords or `gl_` names is undefined behaviour in GLSL, so the text is changed instead). VBOs and two VAOs (the primitives', the quads'): the core profile has no client-side arrays, which the Vita's quads used.
  - **Threads:** the game thread draws, as on the Vita with `render_thread=0`, on a GL context of its own that `video.c` makes on the main thread, shared with the main one (`SDL_GL_SHARE_WITH_CURRENT_CONTEXT`), and that the game thread makes current on the window. SDL marks `SDL_GL_MakeCurrent` main-thread-only; the game thread never swaps or draws to the window, only into FBOs, which GLX, WGL and EGL all allow (and it ran under Xvfb's GLX and Wine's WGL, below). The other way (the game thread's GR_* calls recorded and run on the main thread, the Vita's render thread) is far more code for no gain here. The main thread still owns the window and shows frames by due vblank (`frames.c`): a presentation capture or a display picture is drawn into the texture of the ring slot it will be filed in, then fenced (`glFenceSync`, `glFlush`); the main thread `glWaitSync`s before drawing it, and fences its own draw, which the game thread waits on before drawing that slot again. The slot the main thread shows stays held until a newer frame is taken.
  - **Scale:** targets at the window's pixels (the 16:9 or 4:3 box, from `video.c`'s drawable size), or `res=N` times the PS1's. A resize or fullscreen remakes the targets at the next picture (at most every quarter second). The display picture (no presenter frame) is drawn at the targets' own scale, 1:1, and shown 4:3.
  - **`virtual=1`** (tests): vblanks counted by the game's ticks, never late, as linux32's virtual vblanks; with it, two renderers on the same pad script show the same frames at the same vblanks however slowly one draws. And a shot is now of the first picture whose frame is due at its vblank, not of whatever the window had last.
- **Checked** (`logs/pc-gpu-cmp/`, amd64 Docker under Rosetta on the Mac, Xvfb, Mesa llvmpipe; `tools/pc/routes/pause.pad`: `play.pad` then the pause menu and the inventory):
  - **The default picture against `gpu=0`** (`virtual=1`, 1280×720, presenter on, `persp=1 subpixel=1`; `hd-*.png`, left `gpu=0`, right `gpu=1`): the intro flyby (2,400), the attract demo (5,900), New Game's transition (6,950) and Artisans (9,000) are the same frames, drawn at 1280×720; pixels more than 20% apart: 2.4%, 4.5%, 0.5%, 4.2% of the window, all at edges the higher resolution and the exact projections move.
  - **The pixels against `gpu=0`** (`virtual=1 res=1 persp=0 subpixel=0 filter=0`, the presenter off; `res1-*.png`): title, the attract demo, Artisans, the pause menu (14,350), the inventory (14,700) and back: pixels more than 20% apart: at most 1.1% of the window in motion, 159 on the pause menu and 215 on the inventory (of 921,600). The pause's StoreImage of the last frame comes back through a replay (`1 readbacks (1 replayed)`). Not exact by design, as on the Vita: the shaders modulate textures in float and expand 5 bits as ×8, where the software rasterizer works in 15-bit colour.
  - **Windows:** `spyro.exe` under Wine 8 (WGL on llvmpipe, `res=1`): the shared context, the renderer up, the title at 60.0 vblanks a second (`wine-gpu1-res1-2400.png`).
  - **The Vita build is untouched:** no file under `src/platform/vita/` or `src/platform/linux32/` changes (`git diff --stat origin/main`); the PC build only reads the Vita's `gpu_soft_names.h`. Nothing shared is compiled differently, so there is no Vita binary to compare.
- **Found, not fixed (before this change):** pausing with the presenter on crashes the PC build. `origin/main`'s binary, alone and in real time on `pause.pad`, faults at vblank 14,202 in `Memset` from `func_80017FE4` (the pause's "PAUSED" letters, `g_HudMobys - 1` at 0xfffe3da8) inside the presenter's pass (`job_run` → `func_8001A40C`); this branch does the same with `gpu=0` and with `gpu=1`. With `widescreen=0 interp=0` the pause and inventory work. A separate fix.
- **Not checked:** a real GPU, so 60 fps at 1080p is unverified (llvmpipe under Rosetta, 4 shared cores, takes 100–150 ms for a 1280×720 picture: the GPU renderer's per-pixel VRAM decode is the costliest part for a CPU rasterizer, and the cheapest for a GPU). A real driver's behaviour with the shared context (NVIDIA, AMD, Intel; Wayland's EGL). Windows on real hardware.
- **Next:** a person with a GPU runs `--window 1920x1080 --fullscreen` and reads the `video` and `gpu` lines (refreshes, new frames, the game thread's time per picture); `tex_filter` (needs the atlas); the pause crash above.
- **Next free number (this branch):** D244.

### D244. A moby shadow's centre is also named under an anchor of its own, and `own_nosrc` counts only own points that truly have no source (#226, 2026-11-24)
- **Why:** after D227, MobyShadows kept 4 missed vertices on host S and on Si. The `missed` bracket names only its top three classes, so they didn't show (S: `missed 25 (top: ParticlesDraw 10, EnvDraw 6, PortalSkyPlain 5)`). #226 tagged them: all were logic frames, two shadows, at vblanks 14,152 and 14,156.
  - A shadow seen nearly edge-on has a corner (RTPT) on its centre's pixel. That corner's own point is named under (key, anchor) = (centre, centre), the same pair as the centre's.
  - D218 drops two different points under one pair, so the centre's own point died and its key stayed ambiguous. The walk's own-point step needs every unfound vertex to have an own point, so the polygon stayed unfound.
  - `own_nosrc` (about 60,000 a route) was 99% world corners that GridCorrect moved and that D223 already keeps in `Sp`. The rest was the shadow fan's fifth corner word, which repeats corner 0 but named its projection 0 back instead of 3 back.
- **What** (`shadows_hand.c`, `vcache.c`, `psx_vcache.h`):
  - `MobyShadows` also names its centre (`VCD_SELF`) under `VCD_CENTRE(c) = VCD_CORNER(c, 15)`. That flips y's top nibble, so the anchor matches no screen key.
  - The walk takes that point only for a shadow polygon's centre that nothing found and whose (centre, centre) point is missing or dead. Every earlier step left such a polygon unfound, so no polygon found before can change. New counter `own_centre`, at the end of the `vcache Spyro and shadows own:` line.
  - The fan's fifth word passes q = 4, as corner 0 does. This is state-identical; it only removes `own_nosrc` counts.
  - A moved world corner kept in `Sp` no longer counts in `own_nosrc`. What's left is world corners named in frames whose order log is full (`VC_LOG` 8,192): S 28, Si 74, 0 on every other route. That is #237's limit.
  - Docs (`vita-testing.md`, `host-build.md`):
    - the "no projection" exempt class (`VCX_NO_PROJ`, `HK_NOPROJ`);
    - every field of the `vcache own points:`, `world:`, `world own:`, `overflow:` and `Spyro and shadows own:` lines;
    - `VC_OVERFLOW=0`;
    - host route T beside S/P/Pf.
- **Measured** (host, 2×, base 33a9992 → fix):
  - MobyShadows missed: S 4 → 0, Si 4 → 0, 0 on P, Pi, Pf, Pfi, T and O60.
  - Total missed: S 25 → 21, Si 44 → 40, unchanged elsewhere.
  - `own_nosrc`: S 60,348 → 28, Si 120,612 → 74, others 0.
- **Checks:**
  - Host FNV per polygon on 8 pairs (T, O60, S, Si, P, Pi, Pf, Pfi; 157 M polygons): 0 found-before polygons changed; 23 newly found on S, 47 on Si.
  - Vita3K: shots identical on all eight routes (toasty60 against `auto-233-1/fix-toasty60`). Counters are the same apart from `vc.probe` +0.1% and `vc.rec.probe` +0.1–0.2% (the extra naming), and timing-dependent ones.
  - Host ctest 33 of 33, native and Vita builds, `noassets`, the coordinator's gate. Logs: `runs/vita/issue-226-1`.
  - Not run: the PC build (the `spyro-pc` image isn't on this machine). The change is plain unsigned arithmetic in common code.
- **Left:** the log-full own points go with #237. The `missed` bracket should list every non-zero class, not the top three (filed).

## 2026-11-22 — the PS4's GLES2 renderer and options menu (PS4 plan P4)

### D245. The PS4 draws with GLES2 on the firmware's Piglet from shader binaries captured once and shipped in the release fpkg (the owner's option C); the options menu has the Vita's rows (PS4 plan P4, 2026-11-21/22)

- **Asked:** P4 of `docs/ps4-plan.md` (the session that ran P3). Numbered D245: this branch called it D194, then D199, and main took both (D194–D198 with PR #176's merge, D199–D244 by 2026-11-24); every reference to it was renumbered when main was merged in.
- **The owner's choices:**
  - **Shaders, option C** (2026-11-21): players install only the pkg and give their `.cue`/`.bin`. Piglet is in every PS4's firmware; its compiler (`libSceShaccVSH.sprx`) isn't. So the shaders are compiled once on a PS4 with the Piglet pair (`shader_compile=1`; `glPigletGetShaderBinarySCE` saves each to `/data/spyro/shaders/NAME-vs|fs-HASH.bin`), copied to the gitignored `disc/ps4-shaders/`, and embedded by `tools/ps4/shaders_c.py`; at start the firmware's Piglet (`/<sandbox word>/common/lib/`) loads them with `glShaderBinary`.
  - **The binaries are never committed** (2026-11-22): they go only into the packaged release fpkg. The pair may be linked to ([sm64-ps4-osirizx](https://github.com/mindfreakdev/sm64-ps4-osirizx)); this repo never fetches or ships it.
  - **The pkg uses its own shaders** even with the pair installed (2026-11-22); the pair only when the build has none, when `shader_compile=1`, or when the shipped ones fail (then a `shader_compile` marker makes the next start use it).
  - **Touch pad tap opens the menu** by default (`touch_menu=1`, 2026-11-21).
  - **One PSX WOBBLE row** (2026-11-22) for perspective + sub-pixel: the Vita's row (D194), same key and default (OFF: the fix on).
- **Built** (`src/platform/ps4/`):
  - `gpu_gles.c`: the Vita's renderer design on GLES2/EGL. The software rasterizer still draws every split into the CPU's VRAM, so the game's VRAM reads stay exact with no readback; the GPU redraws the same splits into 1080p targets (8, LRU), samples VRAM from a texture uploaded by dirty rows, blends as the PS1 (two passes for subtract), and keeps the presenter's frames in slot textures. The PICTURE rows: PSX WOBBLE (the vertex cache's dx, dy, w as a second vertex stream, `w` times the clip coordinates), RESOLUTION (1080p or the PS1's pixels), DRAW DISTANCE (`lod.c`, both renderers).
  - `raster_mt.c`: the software rasterizer on 4 threads (rows by `(y>>2)%4`); `tests/ps4_raster` checks it against `gpu_soft.c`, 0 differing words. The software path went from ~33 to 60 fps with 60 FPS on.
  - `menu.c`, `pad.c`, `menu_game.c`, `present_ps4.c`, `picture_ps4.c`, `savestate_ps4.c`: the Vita's menu and rows on the DualShock 4 (REAR TOUCH L2/R2 left out: the DS4 has them). `config.txt` keeps unknown lines.
  - `main.c`: a watchdog thread logs where the game thread stalls; markers (`gpu_novsync`, `shader_compile`) carry the build's stamp, so another build tries again.
  - Shared: the presenter's snapshot externs of PS1 labels and its `g_Buffers` field copy are `P32` (`pres_draw.c`, `pres_snap.c`, `render_ctx_check.c`). An 8-byte host pointer copied over a 4-byte field zeroed the next one (`m_HudOTStart`): the pause screen crashed on PS4 GPU run 1 and on linux64.
- **What Piglet needed:**
  - `param.sfo` CATEGORY `gde`, FORMAT `obs` (as OpenOrbis's sample): with `gd`, `eglGetDisplay` returned `EGL_NO_DISPLAY` and no error.
  - Swap interval 0 (the sample's): with 1 the first `eglSwapBuffers` never returned (GPU runs 3–5). Without a `sceVideoOut` handle of our own (Piglet opens the display), the vblank count is a 60 Hz clock, and the wait sleeps to its next tick.
  - The movies' 24-bit frames decoded from VRAM into RGBA at their own size: through `host_display_rgb` and a 1024×512 `GL_RGB` texture they took 58 ms a picture; now 0.8 ms.
- **Checked:**
  - **Hardware** (`logs/p4/gpu-run{1..11}/`, `docs/ps4-build.md`'s table): run 6 the GPU picture at 60.0 vblanks/s, 0 late, no GL errors; run 8 the firmware's Piglet with the embedded binaries, renderer up in 324 ms; run 9 the menu opened, changed and saved with no crash, 3 late vblanks in the run; run 11, after merging `main` (#178's float `VCacheVert`, now read as floats and checked by a `_Static_assert`), the picture right, 11,400 vblanks, 15 late, a memory card save written.
  - **The endings on the host** (linux32, base and branch, `--tick60`, `logs/p4end-*`): ending 2 plays into the credits, 30 of 30 shots the same.
  - **Unchanged elsewhere** (base `5c6021c`, the three shared files swapped back, the same build directories): linux32's and the Vita's code and data sections are the base's byte for byte; the whole files differ only in DWARF declaration columns (inserting `P32 ` moves a name) and linux32's build ID, which hashes them (stripped of debug info and the build ID, linux32 `072975…` both, the Vita `bbcaf5…` both). linux64 changes: the fix.
  - **linux32 against linux64 with the presenter** (`--widescreen --interp --tick60`, `tools/host_checks.sh`, `logs/p4pres-*`): demo 24, routeA 53, routeB 54 shots and the traces the same.
- **Still to do for P4:** the whole game played to the end on hardware (both endings, the credits). The owner skipped the Store copy's `EBOOT.PBP` for now.
- **Next free number:** D246 (taken by P5, below).

### D246. The PS4 release fpkg: title ID SPYP00001, built on the owner's Mac by `tools/ps4/release.sh`, CI builds the test fpkg without shader binaries as a check; the Vita's LiveArea images on the PS4; the fpkg GPL-3.0 as a whole (PS4 plan P5, 2026-11-24)

- **Asked:** P5 of `docs/ps4-plan.md` (the session that ran P4).
- **The owner's choices (2026-11-24):**
  - **Art: the Vita's LiveArea images** (the Spyro logo and Simon Kopp's Reignited concept art) on the PS4 too, extending D59's exception; they stay outside the MIT licence (`assets/livearea/README.md`). **Then, after release run 1: no background at all.** The painting as `sce_sys/pic1.png` stayed on screen over the game the whole run, so the owner had it removed; the package keeps only the icon.
  - **Workflow: a local release script and a CI build check.** The shader binaries (D245) are never committed, so CI can't build the release; it builds the test fpkg without them on every pull request and keeps it as a CI artifact, never attached to a release.
  - **Licence: the fpkg is GPL-3.0 as a whole** (D180's open question). The repository stays MIT.
  - **PS5: later.** The owner has one; its etaHEN test isn't in this milestone.
- **Chosen here:** title ID `SPYP00001` (P for PS4; `SPYT00001`/`SPYT00002` stay the spike's and the test build's, so a test fpkg and the release install side by side and share `/data/spyro/`), TITLE `Spyro PS4`, CONTENT_ID `IV0000-SPYP00001_00-SPYROPS4RELEASE0`; APP_VER and VERSION from `cmake/version.cmake`, which the Vita now reads too (`01.22`, its VPK unchanged), so one version-bump PR sets both; CATEGORY `gde` and FORMAT `obs` kept (Piglet, D245).
- **Built:**
  - `SPYRO_PS4_RELEASE` (`src/platform/ps4/CMakeLists.txt`): the release's identity, `sce_sys/icon0.png` (512×512) by `tools/ps4/art.py` (plain Python, Catmull-Rom; the same bytes every run), `licenses/` in the package (`LICENSE-MIT.txt`, `THIRD_PARTY_NOTICES.md`, `GPL-3.0.txt`, `CC0-1.0.txt`, `ART.md`; `pkg.sh` adds the folder to `create-gp4`'s fixed `<rootdir>`, or PkgTool throws "Sequence contains no elements").
  - `tools/ps4/shader_check.py`: preprocesses `gpu_gles.c`'s shader block with the compiler, evaluates each `k_vs_*`/`k_fs_*` string, hashes it as `src_hash()` does, and fails the release build unless `NAME-vs|fs-HASH.bin` exists for every shader `link()` uses. Its hashes match the six the PS4 computed (GPU run 8's binaries).
  - `tools/ps4/release.sh`: a clean tree, two clean builds with every SHA-256 compared (`SOURCE_DATE_EPOCH` the commit's time: `__DATE__`/`__TIME__` in `main.c` and `savestate.c` made the first two builds' ELFs differ), `build/release-ps4/<APP_VER>-<commit>/` (the pkg, `.sha256`, `SHA256SUMS`, the unstripped `spyro.elf` and `spyro.map`, `manifest.txt`; never overwritten), `--upload vX.Y.Z` to a draft release only.
  - CI's `ps4` job: the arm64 OpenOrbis image on `ubuntu-24.04-arm` (the repository is public); the image gains cmake, ninja and PyYAML, and `PS4_CC`/`PS4_LD` point into `/usr/lib/llvm-18/bin`, where Ubuntu keeps `llvm-ar` and `clang++` (the toolchain file looks beside `PS4_CC`). It checks that `shader_check.py` refuses a folder without binaries.
  - The PS4 link writes `spyro.map`: code comes only from this repository, OpenOrbis's `crt1.o` (GPL-3.0) and its musl `libc.a` (MIT, 116 objects); the rest are import stubs. `THIRD_PARTY_NOTICES.md` has the PS4 section and musl's notice; the package also carries `libc.prx`, `libSceFios2.prx` and `right.sprx` from OpenOrbis's Piglet sample, whose own licence OpenOrbis doesn't state (noted there, as for the test builds since P3).
  - README: "On a PS4" (GoldHEN's Package Installer, the `.cue`/`.bin` in `/data/spyro/`, controls, the menu, saves, the Piglet pair linked only as sm64-ps4-osirizx, PS5 untested, the licence).
- **Checked:** [[filled in after the owner's run]]
- **Unchanged elsewhere:** the Vita's CMake reads the same `01.22` from `cmake/version.cmake`; linux32, linux64 and the Vita build no PS4 file. [[filled in]]
- **Next free number:** D247.

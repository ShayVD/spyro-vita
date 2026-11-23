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

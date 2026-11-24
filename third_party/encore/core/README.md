# core: the platform interface and what every target shares

| Where | What |
|---|---|
| [`include/encore/platform.h`](include/encore/platform.h) | the interface a port's platform layer calls and every target implements ([`docs/platform-interface.md`](../docs/platform-interface.md)) |
| [`include/encore/backend.h`](include/encore/backend.h) | helpers for backend authors (the CPU picture path, the event queue) |
| `include/encore/` modules | portable modules a port's platform layer calls directly, on any target (roadmap E4, from `targets/vita/runtime/`): [`ring.h`](include/encore/ring.h) (per-frame vertex / uniform rings), [`audio_ring.h`](include/encore/audio_ring.h) (the mixer-to-device sound ring, plain or held level), [`discimage.h`](include/encore/discimage.h) (the user's disc image by sector, cue sheets, the folder search, SHA-1), [`menu.h`](include/encore/menu.h) and [`menu_panel.h`](include/encore/menu_panel.h) (the options table, its CPU-drawn panel, the `config.txt` rewrite), [`config.h`](include/encore/config.h) (`config.txt`'s `key=value` lines). Each has its doc in [`docs/`](docs/) |
| [`src/`](src/) | portable C every backend links: `enc_common.c` (error names, the aspect box), `enc_blit.c` (scale and convert a frame), `enc_disc.c` (disc images, over `enc_discimage.c`), `enc_co.c` (coroutines over `<ucontext.h>`), `enc_event.c` (the event queue behind `enc_event_poll`); and the modules' code: `enc_ring.c`, `enc_audio_ring.c`, `enc_discimage.c`, `enc_menu.c`, `enc_menu_panel.c`, `enc_config.c` |
| [`host/`](host/) | the headless POSIX backend: tests, CI, a debugging build; `enc_host.h` drives it (scripted pad, touch, motion, text, suspend and focus; the display as presented, the fake speaker, the last rumble) |
| [`test/`](test/) | `conformance.c` (every backend), `host_test.c` (host extras), the modules' tests (`ring_test.c`, `audio_ring_test.c`, `discimage_test.c` with two mutants, `menu_test.c`, `config_test.c`), `run_tests.sh` |

Prefix `enc_` (types `Enc...`, macros `ENC_`); the disc image layer is `enc_dimg_` (`EncDimg`, `ENC_DIMG_`), since
`enc_disc_` is the interface's. Nothing here names a game or a source console, and only one file a target SDK:
`enc_discimage.c` reads through `sceIo` under `__vita__` (newlib's `fseek` is 32-bit).

**The old paths.** Games vendored before E4 build `targets/vita/runtime/{ring,config,discimage,modmenu}` and
`audioout/vpk_audio_ring.*` by their `vpk_` names. Those files are now compat shims: each header includes core's and
maps the `vpk_` names onto the `enc_` ones, each `.c` compiles core's, and a `NEEDS` file beside them lists the core
files, which `tools/vendor.sh` vendors with the directory. New code includes `encore/<module>.h` and calls `enc_`.

```bash
core/test/run_tests.sh            # the host: conformance + host checks
core/test/run_tests.sh -m32       # a 32-bit host, as the consoles' 4-byte pointers are
targets/vita/encore/build_conformance.sh   # the same checks as a Vita app (needs vitasdk; built, not run)
```

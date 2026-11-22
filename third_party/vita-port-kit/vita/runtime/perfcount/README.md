# perfcount

Per-frame work counters for the port autopilot's Vita3K gate (`../../../playbooks/port-autopilot`): Vita3K's timing
is not evidence, but how much work a frame does is the same there and on the Vita. A counter is a name and a sum;
every `window` frames one line goes to the log:

    vpk-perfcount: w FIRST LAST FRAMES draws=1234 tex.uploads=56 ...

- `vpk_pc_init(&config)` (optional): `window` (default 300), `prefix`, `log` (`vpk_log`).
- `id = vpk_pc_register("draws")` once per counter (up to 32; names of letters, digits, `_` and `.`, at most 23).
- `vpk_pc_add(id, n)` where the work happens, from any thread (a relaxed atomic add).
- `vpk_pc_frame(frame)` once per game frame on one thread; `vpk_pc_flush()` at the run's stop.
- `../../tools/perf_report.py` reads the lines: per-frame means of one run, or two runs compared with a noise
  threshold (a counter more than 2% higher is worse).

Which counters a game registers, and where it adds, is the game's (its phase-7 and phase-8 issues); keep the names
stable across builds so runs compare.

**Test:** `test/perfcount_test.c`: two windows, a flush, bad names and ids, a full table. The mutant
(`-DVPK_PERFCOUNT_MUTANT=1`: totals kept after a line) must fail.

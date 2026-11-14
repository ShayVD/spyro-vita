# vcount

`sceDisplayGetVcount` counts in **16 bits** on hardware: 65,536 vblanks after boot (18 min 13 s at 59.94 Hz) it is 0
again. A port that compared it with a 32-bit target froze every session at that minute, invisible in any run shorter
than 18 minutes (spyro-vita D90, found from its hang watchdog's crash dump). `vpk_vcount_widen` counts the counter's
steps modulo 0x10000 instead, so the count only grows; read it at least once per wrap. Header only, plain C.

- `VpkVcount v = {0}; unsigned n = vpk_vcount_widen(&v, sceDisplayGetVcount());` on every read.
- `vpk_vcount_reset(&v)` to count afresh after a pause you do not want to catch up on.
- Give the game a test knob that moves the wrap close to start-up (spyro-vita's `vcount_test=N` adds an offset to the
  raw counter), so a hardware run can check it in 30 seconds instead of 18 minutes.

Frame pacing built on it (due-vblank presents, the 60 / 30 hold) is `../pacing/` (the rules as code) and `../../docs/presentation.md` (why).

**From:** spyro-vita `src/platform/vita/vcount.h` @ `bb62f38`.

**Test:** `test/vcount_test.c` (any host compiler) plays a game loop's waits against a simulated 16-bit counter across
several wraps; the mutant (`-DVCOUNT_MUTANT=1`, the raw counter) must fail:

```bash
cc -std=c99 -Wall -I. test/vcount_test.c -o vcount_test && ./vcount_test
cc -std=c99 -Wall -I. -DVCOUNT_MUTANT=1 test/vcount_test.c -o vcount_mutant && ! ./vcount_mutant
```

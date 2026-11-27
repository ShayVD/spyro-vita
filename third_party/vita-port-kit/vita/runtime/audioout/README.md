# audioout

Sound out of the Vita: a ring between the mixer and the device (`enc_audio_ring` in `core/`, portable C) and the
output thread that feeds an `sceAudioOut` port from it (`vpk_audio_out`).

## The ring

The ring moved to `core/` (roadmap E4) as `enc_audio_ring` ([`core/docs/audio_ring.md`](../../../../core/docs/audio_ring.md)):
`core/include/encore/audio_ring.h`, `core/src/enc_audio_ring.c`. `vpk_audio_ring.h` and `vpk_audio_ring.c` here keep
the old path and names for games built before the move (they include core's; [`NEEDS`](NEEDS) lists what
`tools/vendor.sh` vendors with this directory). The output thread below takes a `VpkAudioRing`, which is an
`EncAudioRing`.

## The output thread

`vpk_audio_out_open` opens the port (type, grain, rate, volume as a percentage of 0 dB); `vpk_audio_out_start`
starts the thread, which pulls a grain, calls `after_pull` (for the game's counters or a semaphore that wakes its
mixer) and hands the grain to `sceAudioOutOutput`, which blocks until the port wants the next one. It alternates
two buffers. Give it a priority above the mixer: it only copies and waits. `vpk_audio_out_volume` changes the
volume (the mod menu's), `vpk_audio_out_close` releases the port.

**From:** spyro-vita `src/platform/common/psx_audio_ring.h` / `audio_ring.c` (the held level, decisions D88) and
`src/platform/vita/audio.c` @ `987363e`; kh1-decomp `host/vita/audio.c` @ `85befa4a` (the plain ring, the volume). The
held level's constants became the config; the code is otherwise spyro-vita's, and its simulation gives the same
numbers.

**Tests:** the ring's are in `core/test/audio_ring_test.c`; `test/audio_out_test.c` runs the output thread's step against fakes of
`sceAudioOut` (`../test/fake_psp2/`). It runs from `../test/run_host_tests.sh`.

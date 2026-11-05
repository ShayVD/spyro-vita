# audioout

Sound out of the Vita: a ring between the mixer and the device (`vpk_audio_ring`, portable C) and the output
thread that feeds an `sceAudioOut` port from it (`vpk_audio_out`).

## The ring

One producer thread (the mixer), one consumer (the output thread), no locks. Two ways to play it, set by
`VpkAudioRingConfig.rate_control`:

- **0, plain.** The consumer takes what is there, up to a grain, and pads the rest with silence (counted in
  `underruns`). For a mixer that is paced by the output: it mixes while `vpk_audio_ring_level` is below its lead
  and waits otherwise, so its samples come at the device's own rate. kh1-decomp works this way (its IOP sound
  driver ticks and mixes 64 ms ahead of a 48 kHz port).
- **1, held level.** For a producer paced by another clock: a game that pushes a vblank's samples each vblank runs
  on the display's 59.94 Hz, while the port plays 44,100 or 48,000 frames a second by its own clock. A ring that only
  passes frames on runs dry by about 1 ms a second whatever its size. So the consumer plays at a rate within 0.5% of
  1, linearly interpolated, set by a slow controller that holds the ring's low point at `low` frames. When the ring
  runs out (a load stops the game) it plays silence until it holds `prime` again and drops what is above that;
  above `max` it drops back to `prime`. spyro-vita works this way, with `{16384, 1, 2304, 3840, 6615, 16384,
  2.0f}` at 44.1 kHz and 735 frames a vblank.

`vpk_audio_ring_clear` drops what was pushed so far (a save state was loaded). The buffer is inside the struct
(`VPK_AUDIO_RING_MAX_FRAMES` frames, 64 KiB); `cfg.frames` may use less of it.

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

**Tests:** `test/audio_ring_test.c` is spyro-vita's model of the Vita's clocks (59.94, 60 and 60.1 Hz, loads) for
the held level, plus the plain ring's cases; `test/audio_out_test.c` runs the output thread's step against fakes of
`sceAudioOut` (`../test/fake_psp2/`). Both run from `../test/run_host_tests.sh`.

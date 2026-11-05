# Decisions, part 2

Decisions from D103 on; D1–D102 are in [decisions.md](decisions.md). Record each decision with its date, what was chosen, and why.

## 2026-11-05 — hardware runs

### D103. Hardware runs use vita-port-kit's scripts; this game's details are in `tools/vita/hw.game`
- **Why and what:** see `docs/vita-testing.md` "Hardware runs from the Mac". The scripts are vendored in `third_party/vita-port-kit`, and `tools/vita/hw_*` wrap them. The lock moved to `~/.cache/vita-port-kit` and is shared with kh1-decomp. Checked against the mock (2026-11-05); not run on hardware yet.

## 2026-11-05 — vita-port-kit runtime modules

### D104. The audio output ring and its thread are vita-port-kit's `vita/runtime/audioout`
- **What:** `src/platform/common/audio_ring.c` moved to vita-port-kit as `vpk_audio_ring` (vendored in `third_party/vita-port-kit/vita/runtime/audioout`), merged with kh1-decomp's simpler ring. D88's held level is the ring's `rate_control 1`; its constants stayed in `psx_audio_ring.h`, which now maps the `psx_audio_ring_*` names to the kit's and passes them as the ring's config. `audio.c`'s output thread (the BGM port, 512-frame grains, core 1, priority 0x68) is the kit's `vpk_audio_out` with those settings.
- **Why:** kh1-decomp had a second copy of the same output thread and a ring without the level control; one copy in the kit, used by both games (vita-port-kit roadmap K4).
- **Checked:** the clock model (`tests/audio`, 10 minutes) prints the same numbers as before, byte for byte; the VPK builds. Device behaviour: a hardware run before merging.

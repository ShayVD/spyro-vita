# Decisions, part 2

Decisions from D103 on; D1–D102 are in [decisions.md](decisions.md). Record each decision with its date, what was chosen, and why.

## 2026-11-05 — hardware runs

### D103. Hardware runs use vita-port-kit's scripts; this game's details are in `tools/vita/hw.game`
- **Why and what:** see `docs/vita-testing.md` "Hardware runs from the Mac". The scripts are vendored in `third_party/vita-port-kit`, and `tools/vita/hw_*` wrap them. The lock moved to `~/.cache/vita-port-kit` and is shared with kh1-decomp. Checked against the mock (2026-11-05); not run on hardware yet.

### D105. The disc image's reader is vita-port-kit's `vita/runtime/discimage`
- **What:** `cdimage.c`'s sector reading (the two 32-sector windows), its sniffing of the file (sync pattern, sector size, first LBA; .iso, CHD, PBP, ECM), the ISO9660 walk and SHA-1 moved to vita-port-kit as `vpk_discimage` (vendored in `third_party/vita-port-kit/vita/runtime/discimage`), merged there with kh1-decomp's reader. `cdimage.c` keeps what is Spyro's: the search of the data folder, the cue sheet, every message to the player, the cut-short and SCUS-94228 / WAD.WAD checks, the marker. `psx_sha1_*` map to the kit's.
- **Why:** kh1-decomp had a second reader of the same sectors (vita-port-kit roadmap K4). The kit's opens with 64-bit offsets (sceIo on the Vita, `fseeko` on hosts); images here are under 2 GB, so nothing changes for this game.
- **Checked:** `ctest` and `tests/cd/image_variants.py` (every good dump and every refusal) on the host; the VPK builds. Device behaviour: a hardware run before merging.

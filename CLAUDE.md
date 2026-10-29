# Spyro the Dragon (PS1, NTSC-U SCUS-94228) → PS Vita native port

## Goal
A native PS Vita homebrew port of Spyro the Dragon (1998), released as a VPK. It must play the whole game to the end, faithfully, at 960×544 / 16:9, with logic running at the original ~30 Hz and an optional 60 fps presentation. Players supply their own disc. Target: claim the zealouschuck.com "Spyro 1 Decomp (Open Pete)" Vita bounty.

## Foundation (and what is off-limits)
- **Use:** `third_party/spyro-1` (TheMobyCollective decomp, CC0) and `third_party/PsyCross` (PsyQ reimplementation, MIT).
- **Reference only:** `../psxrecomp-ref` (licence unclear). Read it to learn techniques. Don't copy its code into this repo.
- **Off-limits:** OpenPete (closed source, PolyForm Noncommercial). Don't copy or reverse its code or assets.
- **Never commit copyrighted game data:** no disc images, PSX.EXE, WAD.WAD, extracted assets or BIOS. All of it lives in the gitignored `disc/` folder.

## Known state of the decomp (Oct 2026)
- **Main executable:** 254/270 functions matched (94.1%), but only 65% of bytes. All level overlays are matched. Two credits-overlay functions remain.
- **What's left is the big, hard code**, in `asm/nonmatchings/`:
  - `pete/` (the player controller; `func_80043FE4` is about 227 KB of assembly)
  - `gamestates/update`
  - `camera/`
  - `cyclorama` (the skybox)
  - `moby_helpers`
- **For the port, these only need to be functionally equivalent, not byte-matching.** Get them to C by hand-decompiling or with a MIPS→C translator in `tools/`. Check each against the original's behaviour.
- **The repo also has `asm/` code that was originally written in assembly, plus PsyQ 4.0 libraries in disassembled form.** Replace the PsyQ libraries with PsyCross. Translate the hand-written assembly.
- **Overlays** are built from `src/overlays/` according to `overlays/*.yaml`. The port links all overlay code statically and dispatches by level instead of loading code into RAM. The Vita doesn't allow JIT for this project, so never generate code at run time.

## Platform facts
- **CPU:** ARMv7 Cortex-A9, 4 cores at 333 MHz (444 MHz overclocked), **32-bit**. The game's 32-bit pointers fit as-is. Never assume `sizeof(void*)==8`, and never write code that only works where it's 4: keep the host debug build honest.
- **GPU:** SGX543MP4+ via vitaGL. Keep draw calls batched. Don't do per-primitive state changes.
- **Rendering:** PS1 primitives go through PsyCross's ordering tables into vitaGL. Depth-buffer or perspective-correct improvements come later and must be optional.
- **Files:** `ux0:data/spyro/` (files extracted from the user's disc). Saves go to `ux0:data/spyro/save/`.
- **Disc data on the host** (gitignored):
  - `disc/image/` holds the original bin/cue.
  - `disc/files/` holds the extracted files, written by `tools/extract_disc.py`, with a `manifest.json` of each file's LBA, size and type.
  - The executable on the disc is `SCUS_942.28`. It's the same file the decomp builds as `PSX.EXE` (SHA-1 verified).
  - Level overlays live inside `WAD.WAD`.
  - The `PETEXA*.STR` music streams are XA, stored as 2336-byte sectors (subheader + data).
- **Audio:** the SPU and CD-XA music go through PsyCross, with SDL2 audio on the Vita.

## Builds
1. **Matching build** (Docker, the decomp's own Makefile). This is the oracle for symbols and layouts. It must stay green.
2. **Host debug build:** 32-bit Linux (`-m32`) in Docker, using the same `src/` code. Use it for gdb and quick iteration.
3. **Vita build:** VitaSDK, CMake, vitaGL, SDL2, packaged as a VPK. Test in Vita3K first. A milestone only counts as done on real hardware.

## Conventions
- **Never edit submodules in place.** Our changes live in `patches/` and are applied by CMake or a script.
- **Code we write** goes in `src/game/` (translated functions, keeping the original `func_XXXXXXXX` names plus a short comment saying where they came from) and in `src/platform/<target>/`.
- **Every translated function** gets a note in `docs/translation-log.md`: method used, how it was verified, known differences.
- **Logs and screenshots** from runs go in `logs/runN-*`, the same as the KH ports.
- **Record decisions** with their reasons in `docs/decisions.md`.

## Reference projects (ours)
- `~/Projects/kh1-decomp`: decomp layout, host shims, data relocations (`data_relocs.tsv`), overlay handling.
- `~/Projects/kh-bbs-vita`: Vita CMake, vitaGL setup, runtime and host layering, test harness, VPK packaging.
- `~/Projects/duke-nukem-advance-tc-vita`: EDuke32 Vita port (`platform/PSVita`, `package/`). Input mapping, LiveArea, release packaging, and a README for user-supplied game data.

Copy build and packaging patterns from these instead of inventing new ones.

## Milestones
- **M0:** repo, toolchains, matching build passing, disc extracted and hash checked, porting plan written.
- **M1:** host 32-bit build boots to the title screen. Unmatched functions may be stubs.
- **M2:** all unmatched or assembly-only functions translated to C. Artisans home world fully playable on host.
- **M3:** Vita build reaches the title screen, then plays a level, with input, audio and saves working.
- **M4:** the whole game can be finished on hardware. Stable 30 Hz logic, then optional 60 fps interpolation and widescreen field of view.
- **M5:** VPK, LiveArea, README with disc-dump instructions, bounty claim.

## Working rules for Claude
- Investigate before building. Say which file and function a claim comes from.
- Report test results as they happened, with log paths. "Builds" is not "runs", and "runs in Vita3K" is not "runs on hardware".
- Ask before large restructures, new dependencies, or anything touching licensing.

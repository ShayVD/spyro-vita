# Bounty claim (DRAFT, not submitted)

Draft for the zealouschuck.com bounty **"Spyro 1 Decomp (Open Pete)"**, the PS Vita port. The
bounty's poster confirmed that a native port built on the Spyro 1 decomp counts, with no extra
rules (`docs/SETUP.md` §1, `CLAUDE.md`).

**Status: draft.** Nothing here has been posted or sent. Everything in `[[double brackets]]` is
for the user to fill in or check before submitting. The user submits.

---

## Before submitting: checklist

- [ ] `[[Licence confirmed: MIT for the port's own code (decisions D56)]]`
- [ ] The repository is public, or the source of the release is otherwise published. The VPK
      statically links LGPL libraries (vitaGL, vitaShaRK, SceShaccCgExt), and users must be able
      to rebuild it (`THIRD_PARTY_NOTICES.md`). The repository is private as of 2026-10-31.
- [ ] The credits crash with widescreen on (the full credits after Gnasty's Loot, decisions D54) is
      fixed and merged, and the release VPK is built after it.
- [ ] Release VPK built by CI from a `v1.00` tag (draft release, `.github/workflows/build.yml`).
      Version `[[v1.00]]`, commit `[[sha]]`.
- [ ] That exact VPK played on hardware from a new game to the end credits (evidence below).
- [ ] Videos and screenshots uploaded: `[[links]]`.

---

## The claim (text to post)

> **Spyro Vita: a native PS Vita port of Spyro the Dragon, built on the Spyro 1 decomp**
>
> `[[Repository link]]` · `[[Release / VPK link]]` · `[[Video link]]`
>
> Spyro Vita runs *Spyro the Dragon* (USA, SCUS-94228) natively on the PS Vita. It is not an
> emulator. The game's own code from TheMobyCollective's Spyro 1 decompilation, plus C
> translations of the parts still in assembly, is compiled for the Vita's ARM CPU. PsyCross stands
> in for Sony's libraries, and the Vita's GPU draws through vitaGL. Players dump their own disc;
> the VPK contains no game data.
>
> - **Native:** one ARM executable. Every level overlay is linked in statically. No emulator, no
>   JIT, no recompilation at run time.
> - **The whole game plays to the end:** every homeworld and level with its boss, Gnasty Gnorc,
>   Gnasty's Loot, both endings and both credits sequences. `[[Played start to finish on hardware: date, model, firmware]]`.
> - **16:9 at 960×544:** the picture fills the Vita's screen in widescreen at its native
>   resolution. The wider view shows more of the world; the game itself plays as in 4:3.
> - **30 Hz logic:** the game ticks at its original rate, so timing, physics and difficulty are
>   unchanged. Experimental 60 fps interpolation and a longer draw distance are in the options.
> - **Faithful audio:** the PS1's SPU and XA music in software, on their own CPU core.
> - **Vita extras:** analog movement and a free camera on the right stick, an options menu (SELECT +
>   START or a tap), picture options (texture filtering, perspective-correct textures, sub-pixel
>   vertices, render resolution), quick save states in three slots, the game's cheats, skip intro
>   logos.
>
> Install: the README's steps (dump your disc, run `tools/extract_disc.py`, copy the files to
> `ux0:data/spyro/`, install the VPK, add `libshacccg.suprx`).
>
> `[[Short personal note / thanks]]`

---

## How it meets the requirements

| Requirement | How | Evidence |
|---|---|---|
| Built on the Spyro 1 decomp | `third_party/spyro-1` (TheMobyCollective, CC0) is compiled for ARM. Its unmatched and hand-written assembly is translated to C (`tools/mips2c`, hand-written C for pete and the camera; `docs/translation-log.md`) | `docs/porting-plan.md`, `docs/translation-log.md` |
| Native Vita build | VitaSDK + vitaGL + SDL2, `eboot.bin` in a VPK; overlays linked statically (D3); no code generated at run time | `src/platform/vita/`, CI build log `[[link]]` |
| Whole game, to the end | Host: every level overlay 10–64, both endings, both credits sequences, 3,707 of 3,707 function samples match the PS1 in PCSX-Redux (`docs/whole-game-host.md`, D31). Vita: `[[hardware playthrough]]` | `logs/run21-host-whole`, `[[hardware video]]` |
| 16:9, 960×544 | Widescreen and native resolution are the defaults (D30, D40, D53) | `[[screenshots]]` |
| Original ~30 Hz logic | The logic runs once per game tick, as on the PS1. The presenter only adds pictures (D30, D44) | `spyro.log` `vblank` lines: 60 vblanks/s, about 30 pictures/s |
| Players use their own disc | No game data in the repository or the VPK; `tools/check_no_assets.sh` runs in CI; `extract_disc.py` checks the dump against known hashes | `README.md`, CI |

## Evidence to gather

What exists already (on the dev machine, in the gitignored `logs/`):

- Host whole-game run: `logs/run21-host-whole` (new game → every level → both endings and
  credits → save and reload), with the PCSX-Redux comparison in `logs/run14-oracle-tour`
  (`docs/whole-game-host.md`).
- Hardware logs from the user's Vita: `logs/hw-14380ab-user/`, `logs/hw-d67e6fc-user/`,
  `logs/hw-be37191-bench/`, and the results table in `docs/vita-testing.md` "On hardware" (GPU
  renderer: 60 vblanks/s, 29–30 pictures/s in gameplay).

What only the user can provide:

- [ ] `[[Video: boot → title → new game → Artisans, on hardware, with the default settings]]`
- [ ] `[[Video or timestamped series: the final boss and the ending/credits, on hardware]]`
- [ ] `[[Full playthrough proof: the spyro.log of the run (it records every level loaded), or a
      long video, or 100% save screenshots]]`
- [ ] `[[Screenshots: LiveArea bubble and gate, title screen, 16:9 gameplay in 2–3 worlds, the
      options menu]]`
- [ ] `[[Device: Vita model (1000/2000/PS TV), firmware, HENkaku/Ensō]]`
- [ ] `[[spyro.log from the release VPK's playthrough, attached or linked]]`

## Build and install (for the claim's readers)

**Install (players):** `README.md` sections 1–5:

1. Dump your own *Spyro the Dragon* (USA) disc to bin/cue.
2. `python3 -I tools/extract_disc.py "Spyro the Dragon (USA).cue" spyro-files` (checks the dump).
3. Copy `spyro-files/*` to `ux0:data/spyro/`.
4. Install `spyro-vita.vpk` with VitaShell.
5. Put `libshacccg.suprx` in `ur0:data/` (ShaRKBR33D).

**Build (anyone):** VitaSDK with vitaGL and SDL2, then

```bash
cmake -S . -B build/vita -G Ninja -DSPYRO_HOST=vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
```

```bash
cmake --build build/vita
```

or `tools/ci/build_vpk.sh` in the `vitasdk/vitasdk` Docker image, as CI does. No game data is
needed to build.

## Credits to include

TheMobyCollective (the Spyro 1 decompilation), the REDRIVER2 / OpenDriver2 project (PsyCross),
Rinnegatamante (vitaGL, vitaShaRK), bythos14 (SceShaccCgExt), the SDL project, the VitaSDK team,
the PCSX-Redux team (used as the behaviour reference), `[[anyone else]]`.

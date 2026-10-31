# Spyro Vita

A native PlayStation Vita port of *Spyro the Dragon* (1998, PlayStation, USA release SCUS-94228).

This is not an emulator. The game's own code, from the
[TheMobyCollective/spyro-1](https://github.com/TheMobyCollective/spyro-1) decompilation, is compiled
for the Vita's ARM CPU. [PsyCross](https://github.com/OpenDriver2/PsyCross) stands in for Sony's
PlayStation libraries, and the PS1's graphics are drawn by the Vita's GPU (vitaGL). The game logic
runs at its original ~30 Hz, so it plays exactly as on a PS1, and the picture fills the Vita's
960×544 screen in 16:9.

**You need your own copy of the game.** The VPK contains no game data at all: no code, levels,
textures, music or logos from the disc. You dump your own *Spyro the Dragon* (USA) disc and copy
the files to your Vita.

*Spyro the Dragon* and its characters are trademarks and copyrights of their respective owners.
None of the game is included here. This project is not affiliated with, endorsed by or connected
to Sony Interactive Entertainment, Insomniac Games, Activision or Universal.

## What you need

- A PS Vita or PS TV that runs homebrew (HENkaku / Ensō), with
  [VitaShell](https://github.com/TheOfficialFloW/VitaShell).
- Your own **Spyro the Dragon, USA, SCUS-94228** disc. The PAL and Japanese releases and the demo
  discs are different builds and won't run (the extractor checks which one you have).
- A computer with a CD/DVD drive and **Python 3.6 or newer** (nothing else to install).
- `ur0:data/libshacccg.suprx` on the Vita (Sony's shader compiler module; see step 4).

## 1. Dump your disc to bin/cue

The game's music is stored as XA audio, which only a raw dump keeps. You want a `.bin` with
2352-byte sectors and its `.cue` (MODE2/2352). A plain `.iso` is not enough.

- **Windows:** [ImgBurn](https://www.imgburn.com/): *Create image file from disc*. For a PS1 disc
  it writes a `.bin` and a `.cue`. Redump's tools (MPF with DiscImageCreator or Redumper) give the
  same thing, verified.
- **Linux / macOS:** [cdrdao](https://cdrdao.sourceforge.net/):

  ```bash
  cdrdao read-cd --read-raw --datafile spyro.bin --driver generic-mmc-raw spyro.toc
  ```

  ```bash
  toc2cue spyro.toc spyro.cue
  ```

**Already have a dump of your own disc in another format?** A `.chd` converts back with
`chdman extractcd -i spyro.chd -o spyro.cue` (from MAME's tools). An `.ecm` unpacks with `unecm`.
A CloneCD `.img` (2352-byte sectors) works as it is, and so do images with 2448-byte sectors
(subchannel data) or a pregap. PSP `EBOOT.PBP` files and 2048-byte `.iso` files can't be used: they
have lost the music.

## 2. Extract the game files

From this repository (or the `tools/extract_disc.py` file alone):

```bash
python3 -I tools/extract_disc.py "Spyro the Dragon (USA).cue" spyro-files
```

You can give it the `.cue` or the `.bin`. It reads the disc's file system and writes every file
into `spyro-files/` (about 590 MB): `SCUS_942.28`, `WAD.WAD`, `PETEXA0.STR` … `PETEXA5.STR`,
`SYSTEM.CNF`, the `S0/` and `SOURCE/` folders, and `manifest.json` (where each file was on the
disc; the port finds every file through it). The music streams (`.STR`) are written as 2336-byte
XA sectors, which is what the port reads.

Then it **checks every file against the SHA-1 of a good dump**. The executable `SCUS_942.28` must
be `84e3728ab94720d0873e2514adf4aade4935e0c5` (the same file the decomp rebuilds byte for byte)
and `WAD.WAD` must match too. If either differs, the script says so and exits with an error: the
disc is another version, or the dump is bad. Other files that differ only give a warning. When
everything matches it ends with `All 16 known files match a good dump of SCUS-94228.`

## 3. Install the VPK

1. Copy `spyro-vita.vpk` to the Vita (in VitaShell, SELECT starts the FTP server; USB works too)
   and press ✕ on it in VitaShell to install. A bubble called **Spyro Vita** appears.
2. Copy the **contents** of `spyro-files/` to `ux0:data/spyro/`, so that the Vita has
   `ux0:data/spyro/manifest.json`, `ux0:data/spyro/WAD.WAD`, `ux0:data/spyro/PETEXA0.STR` and so
   on. Copy the `.STR` files as they are.

## 4. Get `libshacccg.suprx`

The port draws with the Vita's GPU through vitaGL, which compiles its shaders on the device with
Sony's shader compiler, `libshacccg.suprx`. It's part of Sony's PlayStation Mobile runtime, so it
can't be included here. You extract it from your own Vita once, and every vitaGL homebrew uses
the same file:

- **ShaRKBR33D** (by Rinnegatamante, on VitaDB): install its VPK and run it. It fetches the
  PlayStation Mobile runtime package from Sony, decrypts the module on your own console and
  writes `ur0:data/libshacccg.suprx`.
- **ShaRKF00D**, the older tool, does the same from the *PSM Dev* app if you have it installed.

The port also looks in `ur0:data/external/libshacccg.suprx`. Without the module it still runs, but
draws on the CPU, which is too slow on a Vita (`spyro.log` says
`gpu: no ur0:data/libshacccg.suprx ...`).

## 5. Play

Start **Spyro Vita**. The first start takes about 8 seconds longer while the shaders compile. If
the screen turns red, the game files aren't in `ux0:data/spyro/` (START quits).

### Controls

| Vita | PS1 | In the game |
|---|---|---|
| Left stick (or d-pad) | Left stick / d-pad | Move (the stick is analog: any direction, any speed) |
| ✕ | ✕ | Jump; ✕ again to glide |
| □ | □ | Charge |
| ○ | ○ | Flame |
| △ | △ | Look around |
| L / R | L1 / R1 | Sidestep |
| Right stick | (free camera) | Turn and tilt the camera; let go and the game's camera takes over |
| START | START | Pause |
| SELECT | SELECT | Inventory |
| SELECT + START, or tap the middle of the screen | | **Options menu** |

The PS1's L2/R2 (turn the camera 90°) are the right stick pushed left/right when the free camera
is off, or the rear touch pad's halves with *Rear touch L2/R2* on. A PS TV's DualShock has real
L2/R2.

### Options menu

Hold SELECT and press START (or tap the middle of the front screen). The game stops while the menu
is open. Up/down picks a row, left/right or ✕ changes it, ○ closes it. Settings are saved to
`ux0:data/spyro/config.txt`.

| Section | Rows | Default |
|---|---|---|
| PICTURE | Widescreen 16:9 | on |
| | 60 FPS (experimental) | off |
| | Texture filter (off / bilinear / 3-point) | off (the PS1's sharp texels) |
| | Perspective-correct textures | off |
| | Sub-pixel vertices (no PS1 wobble) | off |
| | Resolution (native / PS1 / 2×–4×) | native (960×544) |
| | Draw distance (experimental: off / 1.5× / 2×) | off |
| CONTROLS | Analog stick | on |
| | Free camera (right stick) | on |
| | Invert camera X / Invert camera Y | on / on |
| | Camera speed | 100% |
| | Rear touch L2/R2 | off |
| | Touch to open menu | on |
| GAME | Skip intro logos | off |
| | 99 lives, levels open, credits, warp to level | the game's own cheats, applied once |
| SAVE | State slot, save state, load state | slot 1 |

Every option, and the extra ones `config.txt` takes, is described in
[docs/vita-testing.md](docs/vita-testing.md#4-optional-ux0dataspyroconfigtxt).

### Saving

- **The game's own saves** (save fairies) go to a memory card file,
  `ux0:data/spyro/save/card0`, created the first time the game saves.
- **Quick save:** the menu's SAVE section keeps three state slots. *Save state* saves the whole game
  at that moment and *Load state* puts it back exactly. States are
  `ux0:data/spyro/save/state1.bin` … `state3.bin` (3.8 MB each). They are separate from the memory
  card, and a state only loads in the build that made it: after installing a new VPK, old states
  show `OTHER BUILD`. Nothing is saved automatically when you close the app.
- The log of the last run is `ux0:data/spyro/spyro.log`. Please include it with any bug report.

## Known issues

- **60 fps is experimental** and off by default. It draws an in-between picture each tick, but the
  Vita's CPU can't fit that at 16:9 in most scenes, so most of the time it falls back to 30 fps.
- **2× draw distance is experimental** and drops frames in big open levels. 1.5× costs less.
- **The first time you pick a texture filter, perspective or sub-pixel setting**, the menu pauses
  for a few seconds while its shaders compile. That happens once per start-up.
- **Opening the game's pause or inventory screen hitches for a moment.** The game copies the frame
  for the menu's background, and the GPU renderer has to rebuild that copy on the CPU.
- Not yet tested on every Vita model and firmware. Report what you find with `spyro.log`.

## Building from source

The VPK builds without any game data. You need [VitaSDK](https://vitasdk.org) (with its `vitaGL`
and `SDL2` packages: `vdpm install vitaGL sdl2`), CMake 3.20+, Ninja and Python 3 with PyYAML.

```bash
git clone --recursive https://github.com/ShayVD/spyro-vita
```

```bash
cmake -S spyro-vita -B spyro-vita/build/vita -G Ninja -DSPYRO_HOST=vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
```

```bash
cmake --build spyro-vita/build/vita
```

The result is `build/vita/spyro-vita.vpk`. GitHub Actions builds the same VPK for every pull request and push to `main`
(`.github/workflows/build.yml`).

The other builds are for development:

- **Matching build** (`tools/build_matching.sh`, Docker): rebuilds the original `SCUS_942.28`
  byte for byte from the decomp and checks its SHA-1. It needs the PsyQ SDK headers from your own
  PsyQ SDK (`docs/SETUP.md`). Its `psx.map` gives the game's memory layout. Without it, the port
  uses the reduced copy in `tools/layout/psx.map` (`tools/reduce_map.py`).
- **Host debug build:** 32-bit Linux in Docker, headless, with pad scripts and screenshots:
  [docs/host-build.md](docs/host-build.md).
- **Vita build and testing in Vita3K:** [docs/vita-testing.md](docs/vita-testing.md).

How it works: [docs/porting-plan.md](docs/porting-plan.md), decisions with their reasons in
[docs/decisions.md](docs/decisions.md), and every translated function in
[docs/translation-log.md](docs/translation-log.md).

## Licence and credits

The code written for this port is under the [MIT licence](LICENSE). The libraries it builds on
keep their own licences, listed with their full texts in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) (also inside the VPK, in `licenses/`):

- [TheMobyCollective/spyro-1](https://github.com/TheMobyCollective/spyro-1): the decompilation of
  Spyro the Dragon this port is built on (CC0).
- [PsyCross](https://github.com/OpenDriver2/PsyCross) by the REDRIVER2 / OpenDriver2 project: the
  PsyQ library reimplementation (MIT).
- [vitaGL](https://github.com/Rinnegatamante/vitaGL), vitaShaRK and math-neon (Rinnegatamante,
  Lachlan Tychsen-Smith): OpenGL on the Vita's GPU (LGPL-3.0, MIT).
- [SDL2](https://www.libsdl.org) (zlib), and the [VitaSDK](https://vitasdk.org) toolchain,
  headers and libraries.

The LiveArea icon and art are original, drawn by `tools/vita/make_livearea.py`.

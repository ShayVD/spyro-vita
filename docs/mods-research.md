# Mods for Spyro the Dragon: what exists, and how Spyro Vita takes them

Research and plan, 2026-11-05. No engine code, no hardware runs, nothing downloaded. The owner asked which existing mods for Spyro the Dragon (PS1, 1998) could be added to Spyro Vita, and how. Anything added must be optional and off by default, with the default game unchanged (CLAUDE.md, D50).

**Short answer:** about 25 Spyro 1 mods exist. Almost all of them are **code** mods: MIPS code injected into the PS1 executable and shipped as xdelta or PPF patches. Spyro Vita never runs the disc's code (§2), so those can't be applied as patches. The plan (proposal D102) has two parts:

- **Built-in mods** ([§3.1](#31-built-in-mods)): code mods are written again as C in the port and ship in the standard VPK. Each is a row in a new **MODS** section of the options menu, off by default, credited to its author in the README and on the row's help line.
- **Drop-in mods** ([§3.2](#32-drop-in-mods)): mods that change only game data (custom levels, the collectible shuffler, skins) can't ship with the port, because they carry game data. The player drops the author's patch, or their own patched image, into `ux0:data/spyro/mods/`. The port applies it if it changes data only, and refuses it with a clear message if it changes code.

[§3.3](#33-not-supported) lists what neither covers, and why. Co-op split-screen is set aside for now: it's a large feature, and the Vita handheld has only one controller.

How sure each claim is: **verified** means read on the page, in the mod's repository or in our code by this session. **Inferred** means reasoned from what was read, not checked.

## 1. What exists

### Sources

- **Read:**
  - the owner's [Google Doc](https://docs.google.com/document/d/1LssltF91pcsDdUTTPZOkIw1LJ763GPLkuNYt03c71WA/edit) (plain-text export; 15 Spyro 1 rows, plus Spyro 2 and 3);
  - GitHub (`gh api`: repository metadata, READMEs, file trees, release notes; no code was run);
  - [ModDB](https://www.moddb.com/games/spyro-the-dragon/mods) and [spyrospeedruns.com](https://www.spyrospeedruns.com/category/game-mods/), both through the built-in browser;
  - [mods.spyro.one/reversemod](https://mods.spyro.one/reversemod/);
  - [speedrun.com's Spyro 1 resources](https://www.speedrun.com/spyro1/resources) and press articles;
  - web search.
- **Couldn't read:**
  - romhacking.net and RetroAchievements: both stop at a Cloudflare bot check, which we don't get around. We have romhacking.net's co-op entry (hack 9961) only through press and the author's README.
  - TCRF, the Spyro Forum and Reddit: these blocked our fetcher in earlier sessions (`hd-texture-packs.md`, the French investigation) and weren't tried again.
  - Discord servers (the Spyro speedrunning Discord, the modders' contact points).
  - YouTube videos: only their titles, as the owner's doc gives them.
  - Mega and Google Drive download folders: not opened, because they are downloads.

### Spyro 1 (PS1) mods

"No licence" means the repository has no LICENSE file. By default that keeps all rights with the author.

| Mod | Author | What it changes | Disc | Form | Licence / terms | Maintained |
|---|---|---|---|---|---|---|
| [Spyro 1 Co-Op Mod](https://github.com/Tokatta007/Spyro-1-Co-Op-Mod) | Tokatta007 | Gameplay: drop-in two-player split-screen. Its own Multiplayer pause page, a colour per player, shared lives, enemies and pickups react to the nearest player. Ported from Spyromain's MIT [Spyro2x2](https://github.com/Spyromain/Spyro2x2) | **NTSC-U**, SHA-1 `84e3728a…` (verified, `CHANGES.md`) | xdelta. Source: C plus 24 replaced instructions in the exe, payload in BIOS scratch RAM. **Built on TheMobyCollective's decomp** | **MIT** (verified, LICENSE) | v0.3, 2026-09-07. Frozen: development moved to an OpenPete version (existence only, see §5) |
| [Practice Rom](https://github.com/C0mposer/Spyro-1-Practice-Rom) (formerly Practice Codes) | C0mposer | QoL and speedrun: save states, ghost replays, level select, timers, hitbox and Sparx-range views, custom skins | NTSC-U (PS1, PS2 and DuckStation builds) | Zips of a patched image, plus a web patcher. C source (C/C++ Game Modding Utility) | No licence | **Yes**, 5.0, 2026-05-23 |
| [Supercharge Anywhere](https://github.com/C0mposer/Spyro-1-Supercharge-Anywhere) | C0mposer | Gameplay: □□ in the air starts a supercharge anywhere, R1/L1 changes its speed | NTSC-U (PS1 and PS2 builds) | xdelta, C source | No licence | 1.1, 2024-03 |
| [Gem Redistributer](https://github.com/C0mposer/Spyro-1-Gem-Redistributer-Mod) | C0mposer | Randomizer: shuffles gem values among a level's gems, keeping the total. Seed menu, quick reset | NTSC-U | xdelta (beta 0.6), C source | No licence | 2023-01 |
| [Skip Intro](https://github.com/C0mposer/Spyro-1-Skip-Intro-Mod) | C0mposer | QoL: skips the title screen and intro | NTSC-U | xdelta, C source | No licence | 2023-01. **Already in the port** as SKIP INTRO LOGOS (D50) |
| [April Hack](https://github.com/C0mposer/Spyro1AprilHack) | C0mposer | Joke ("might induce insanity") | NTSC-U (inferred) | Patched bin/cue zip | No licence | 2024-04-01 |
| [Collectible Shuffler](https://github.com/IikkaF/Spyro-Collectible-Shuffler) | IikkaF | Randomizer: moves gems, chests, keys, dragons and vortexes to new spots, with seeds and weights by difficulty | **NTSC-U** only | A Python tool that writes a new image. It rewrites only moby positions in each level's `sub4` data, in place (verified, `src/modify_data.py`) | No licence. Uses AlDeezy's level models and `spyro-unwad` | 2024-03 |
| [Requirement Randomizer](https://github.com/OddKara/spyro1-mods/releases) | OddKara | Randomizer: balloonist requirements and homeworld order | NTSC-U (inferred) | xdelta (with and without analog) | No licence | v1.0.0, 2023-08 |
| [Easy Supercharge Sharp Turning](https://github.com/OddKara/spyro1-mods/releases) | OddKara | Gameplay: d-pad left or right turns sharply during a supercharge | NTSC-U (inferred) | xdelta | No licence | 2.0.0, 2023-08 |
| [Stat Growth](https://github.com/AlDeezy/spyro-stat-growth) | AlDeezy | Difficulty: jump height and charge speed grow with gems, Sparx's range grows with eggs and drops with his health, early charge jumps nerfed | NTSC-U (verified, `disc.json`) | xdelta, C source | No licence | 2022-12 |
| [Sparxless](https://github.com/LuminescentSky/Sparxless) | LuminescentSky | Difficulty: no Sparx for the whole game | NTSC-U, PAL, JP (inferred from its build files) | Source only, no release found | No licence | 2025-03 |
| [Speedway Mod](https://github.com/LuminescentSky/Speedway-Mod) | LuminescentSky | Gameplay: every charge is a supercharge, sharp turns, Spyro 2-style flapping in free flight, every level a saved time trial, instant dragons, quit from any level | **PAL only** (PPF) | PPF, C source built on C0mposer's code with his permission | No licence | 1.0.3, 2025-03 |
| [ReverseMod + cheats menu](https://mods.spyro.one/reversemod/) | Husbjörn ([article](https://www.spyrospeedruns.com/spyro-the-dragon-cheats-menu/)) | ReverseMod: each level starts at its exit, balloonist requirements re-done, optional start in Gnasty's Loot. Cheats menu: free flight with no height cap, a speed modifier, gem swapping, dragon-cutscene skip with a crash fix | **NTSC-U** only (its patcher checks) | Windows patcher, closed source | None stated | 2025-10 |
| Pete's Gauntlet | Husbjörn | Difficulty: each level becomes a timed challenge | unknown | Patch. The original link is dead; the owner's doc gives a re-host | None found | Unmaintained |
| [Grim Garden](https://www.moddb.com/mods/grim-garden) | Cyreides, Ello Officer | **New level** (a Dark Hollow-style level), made with Altro50 and Hwd405's unreleased WadEdit toolkit ([article](https://www.spyrospeedruns.com/in-engine-levels-now-available-in-spyro-the-dragon/)) | PS1, region not stated | xdelta (an OpenPete build also exists, existence only) | None stated | 2026-08/09 |
| [Spyro 1 Editor](https://github.com/monty19933-hub/Spyro1Editor) | monty19933-hub | **Tool**, not a mod: edits levels, objects, skies and music, adds new worlds, and writes a patched BIN | **NTSC-U** ("supported original USA disc") | A Mac or Windows app that writes BIN/CUE. Some edits relocate executable data (verified, README) | No licence | **Yes**, v1.4, 2026-10-19 |
| [SCALES](https://github.com/Hwd405/SCALES) | Hwd405 | Graphics: recolours Spyro's skin from a BMP | US, PAL and JP each have their own template | A Windows tool that patches the image | No licence | 2024-07 |
| Spyro 1.5 Cliff Town demo | unknown | New content (a demo) | unknown | No download found. RetroAchievements says it "may become a full game" | — | unknown |
| SpeedMod | unknown | unknown (title only) | unknown | "Direct download in video description" | — | unknown |
| Fatty ("gems make you heavy") | unknown | Difficulty, the same idea as Stat Growth | unknown | Google Drive file | — | unknown |
| Christmas Stone Hill | unknown | Graphics: snow in Stone Hill | unknown | No link | — | unknown |
| Desert Bus | unknown | Joke | unknown | "No link that I know of" (owner's doc) | — | — |
| "Chinese Localization Build" | unknown | Parody | unknown | Google Drive file | — | — |
| GameShark / CodeBreaker codes | various ([Almar's Guides](https://almarsguides.com/retro/walkthroughs/ps1/games/spyrothedragon/codebreaker/), [GameRevolution](https://www.gamerevolution.com/guides/31595-spyro-the-dragon-ps-cheats)) | Cheats: infinite lives (`8007582C 0009`), infinite health (`80078BBC 0003`), moon jump, all eggs, always have the key | NTSC-U | RAM pokes | Facts, no terms | Static. Codes are from search snippets, not checked against the decomp |

**Not for this game, or out of scope:**
- [BizHawk Shuffler plugin](https://github.com/dessertmonkeyjk/Spyro-Shuffler-Plugin) (GPL-3.0): an emulator-side game shuffler.
- Archipelago: there are implementations for Spyro 2 ([S2AP](https://github.com/Uroogla/S2AP)) and Spyro 3 ([S3AP](https://github.com/ArsonAssassin/S3AP)), but **none for Spyro 1** (searched GitHub).
- The Reignited Trilogy (Unreal) mods and the "Spaghetti Spyro" Unreal remaster project.
- Hidden Palace's prototype patches: they patch prototypes, not retail. A restoration hack for retail Spyro 1 (cut levels, unused content) wasn't found.
- The owner's doc's Spyro 2 and 3 rows.

## 2. How these mods work, and why that decides the route

A Spyro 1 mod changes one or more of three things on the disc:
- **code in `SCUS_942.28`**: the PS1 executable;
- **overlay code inside `WAD.WAD`**: each level's code;
- **data inside `WAD.WAD`**: level geometry, moby lists and positions, textures, palettes, skies, sounds.

Most mods above are built with Redhot's [psx-modding-toolchain](https://github.com/mateusfavarin/psx-modding-toolchain) (GPL-3.0) or C0mposer's [C/C++ Game Modding Utility](https://github.com/C0mposer/C-Game-Modding-Utility) (GPL-3.0). Both compile C to MIPS, insert jumps ("hooks") into the retail code, and put the new code in free RAM. The co-op mod does the same by hand, with 24 replaced instructions (verified, its `CHANGES.md`).

**What Spyro Vita does with the disc (verified, our code and docs):**
- **It never runs the executable's code.** The game is the decomp's C, compiled for the Vita. `SCUS_942.28` is opened only to hash it.
- **It never runs overlay code from the WAD either.** The overlay bytes are still read, so timing and buffers match the PS1, "but the bytes are never executed" (`docs/porting-plan.md` §4.1, step 3). The 37 overlays are linked in statically, and the Vita allows no generated code (CLAUDE.md).
- **It checks the disc:**
  - every launch: the SHA-1 of `SCUS_942.28` against `84e3728a…`;
  - once per image: the SHA-1 of `WAD.WAD` against `ab680013…`;
  - either mismatch stops the game with a panel that says why (`src/platform/common/cdimage.c:25-29`, `:614-634`; D60).

  We didn't check whether the extracted-files layout, with its `manifest.json`, checks the WAD too.
- **The executable's data is compiled into the port as well,** from the decomp's C and data. So tables such as names, requirements and the cheat table come from the build, not the disc (inferred, from how the decomp builds `PSX.EXE`).

**So, for this port:**
- **A code mod can't be applied as a patch.** Its MIPS would never run. Worse, if the D60 check were relaxed, an xdelta that changes code *and* data would half-apply: the data changes without the code that expects it. That's why the check must stay as it is for code mods.
- **A code mod can only come in as C written for the port**, hooked where the decomp's C does the same thing. That is the D50 pattern: a numbered patch in `patches/spyro-1-port/`, port code in `src/game/`, a menu row (the GAME section's cheats are in `src/platform/vita/menu_game.c`; mods get their own MODS section, §3.1), and a host flag for checks.
- **A data-only mod** (WAD data changed, executable and overlay code untouched) would load as it is. The decomp reads levels through the WAD header the same way the PS1 does. Today D60's check refuses a changed WAD. §3.2 lets data changes through as an overlay on the verified retail image, and keeps refusing code changes.
- **RAM-address cheats (GameShark)** work today only because the arena holds PS1 RAM at fixed offsets. Stage 4f–4i makes the data labels ordinary globals, with the game's memory from the heap (`docs/native-plan.md`, "Stage 4's steps 4f–4i"). So a generic "poke this PS1 address" engine would break when that stage lands. **Turn each wanted code into a named toggle on the named variable** instead (inferred).

## 3. The plan: built-in mods and drop-in mods (proposal D102)

### 3.1 Built-in mods

The mod's behaviour is written as C in the port, from the decomp. The mod's own code targets the PS1 executable's addresses, so it can't be reused anyway. It ships in the standard VPK as one row in a new **MODS** section of the options menu (hold SELECT, press START), off by default.

| Menu row (proposed) | What it does | Based on (author) | Licence | Kind | Effort (inferred) | Saves |
|---|---|---|---|---|---|---|
| SUPERCHARGE ANYWHERE | □ twice in the air starts a supercharge anywhere; R1 and L1 change its speed | Supercharge Anywhere (C0mposer) | none | gameplay | small | normal card |
| SHARP TURN | d-pad left or right turns sharply during a supercharge | Easy Supercharge Sharp Turning (OddKara) | none | gameplay | small | normal card |
| NO SPARX | Sparx never comes back, so one hit costs a life | Sparxless (LuminescentSky) | none | difficulty | small | normal card (inferred) |
| SKIP DRAGON CUTSCENES | a freed dragon counts at once, with no cutscene | Husbjörn's cheats menu (also in the Speedway Mod) | none stated | QoL | small to medium: it must set every flag the cutscene's end sets, checked against a full cutscene on the host. Earlier mods crashed here (shuffler README, spyrospeedruns article) | normal card |
| FREE FLIGHT | fly anywhere, with no height cap | Husbjörn's cheats menu; a common cheat | none stated | cheat | small to medium | normal card |
| SPEED | a multiplier on Spyro's speed while walking, gliding and flying | Husbjörn's cheats menu | none stated | cheat | small | normal card |
| INFINITE HEALTH | Sparx never loses health | a GameShark code (`80078BBC 0003`), written as a toggle on the named variable | facts | cheat | small | normal card |
| STAT GROWTH | jump height and charge speed grow with gems; Sparx's range grows with eggs and shrinks with his health | Stat Growth (AlDeezy) | none | difficulty | medium | normal card |
| GEM SHUFFLE (seed) | gem values swapped among a level's gems, keeping its total; the same seed gives the same layout after a reload | Gem Redistributer (C0mposer) | none | randomizer | medium | own card |
| REQUIREMENT SHUFFLE (seed) | balloonist requirements and homeworld order shuffled | Requirement Randomizer (OddKara) | none | randomizer | medium | own card |
| REVERSE LEVELS | each level starts at its exit and ends at its start; balloonist requirements re-done; optionally start in Gnasty's Loot | ReverseMod (Husbjörn) | none stated | gameplay | large: the start and exit of every level read from its data at load, and every level played through | own card |
| LEVEL TIMER | an on-screen timer per level, and a best time per level | Practice Rom timers (C0mposer); Speedway Mod time trials (LuminescentSky) | none | QoL | small to medium: drawn by the port's own panel (`menu_panel.c`), not the game's HUD | best times in a port file, not the card |
| SPYRO COLOUR | tint dials for Spyro's scales | SCALES (Hwd405); the co-op mod's colour page (Tokatta007, MIT) | none; MIT | graphics | small to medium: the palette is changed at run time from the dials, so no art is shipped | `config.txt` only |

Already in the port, with nothing to add: SKIP INTRO LOGOS (D50; C0mposer's Skip Intro does the same on the PS1), save states (D51) and level warp (D50's WARP TO). The Practice Rom has those on the PS1.

**Suggested order:**
1. The small rows that don't touch progress: SUPERCHARGE ANYWHERE, SHARP TURN, NO SPARX, FREE FLIGHT, SPEED, INFINITE HEALTH.
2. SKIP DRAGON CUTSCENES, LEVEL TIMER and SPYRO COLOUR.
3. The two randomizers and STAT GROWTH, with the per-mod card.
4. REVERSE LEVELS.

**How each one is built (as D50's cheats were):**
- Code in `src/game/mods/<name>.c`. The game's hooks go in a numbered `patches/spyro-1-port/00NN-mods-*.patch`, each a branch on an option that defaults to 0.
- The rows go in `src/platform/vita/menu_mods.c`, which defines `g_menu_mods`. `menu.c` gains one weak name for it, as the other sections have. Keys are saved in `config.txt`, and the host gets `--mod NAME[=VALUE]`.
- Mods are always off in attract demos, which must replay exactly. Oracle replays never see them.
- Mods are written against named variables (`g_Spyro`, the moby lists), not PS1 addresses, so they keep working when stage 4 moves the game's memory (§2).
- **Checks:**
  - With every mod off, `tools/host_checks.sh` (attract demos, the whole-game tour, routes A and B) stays byte-identical to main.
  - Each mod gets a host script that shows its effect. For the randomizers: one seed gives one layout, across reloads and a save and load.
  - Then Vita3K, then hardware with the owner's yes.
- **Saves:**
  - Rows marked "own card" use `save/mods/<mod>-<seed>/card0`, so a shuffled run never writes the normal card (D19's layout).
  - The rest use the normal card.
  - Save states (D51) record which mods were on, and loading one turns the same mods on.

### 3.2 Drop-in mods

These change only game data. They can't ship with the port, because each one carries or derives from the game's data. The player gets the mod from its author, and Spyro Vita applies it to their own disc.

| Mod | Author | What it changes | What the player drops in | Data only? |
|---|---|---|---|---|
| [Grim Garden](https://www.moddb.com/mods/grim-garden) | Cyreides, Ello Officer | a new level | the author's `.xdelta` | Expected (made with WadEdit "within the PS1's restrictions"), **not yet checked** |
| [Collectible Shuffler](https://github.com/IikkaF/Spyro-Collectible-Shuffler) | IikkaF | gems, chests, keys, dragons and vortexes moved | the `.bin`/`.cue` the author's tool writes | **Yes** (verified: it rewrites only moby positions in each level's `sub4` data) |
| [SCALES](https://github.com/Hwd405/SCALES) skins | Hwd405 | Spyro's colours | the `.bin`/`.cue` the tool writes | Expected (it edits palettes), not checked |
| [Spyro 1 Editor](https://github.com/monty19933-hub/Spyro1Editor) output | monty19933-hub | edited levels, objects, skies and music | the `.bin`/`.cue` from Create BIN | Only some edits. New worlds, renamed levels and larger skies change the executable (verified, its README), and those are refused |
| Levels from WadEdit, when it's released | Altro50, Hwd405 | new levels | as Grim Garden | Expected |

**How it works** (design, inferred; nothing built):
- **The folder:** `ux0:data/spyro/mods/`. A **DISC MOD** row in the MODS section picks one entry, or none (the default: retail). The choice takes effect at the next start.
- **What it accepts:** an `.xdelta` (VCDIFF) or `.ppf` made against the retail `.bin`, or a whole patched `.bin`/`.cue`. The retail image stays where it is and is verified exactly as today (D60).
- **First use:** the port builds the patched result once, and keeps only the sectors that differ from the retail image: `mods/<name>/changed.bin` plus an index. It shows a progress panel, as D60's first-start check does. After that, reads take changed sectors from there and every other sector from the retail image. Nothing large is copied, and the retail image is never written.
- **What it refuses:** any changed sector inside `SCUS_942.28`, or inside any level overlay's code in `WAD.WAD`. The panel says the mod changes the game's code, which Spyro Vita can't run. The overlays' code ranges come from the WAD header and the overlay sizes, checked against the matching build's overlay hashes. Sizes and hashes aren't game data.
- **Saves:** `save/mods/<id>/card0`, with the id taken from the changed sectors' hash. Save states go there too. A modded run never writes the retail card.
- **The log and boot panel** name the active mod. With no mod picked, the disc code path is today's, byte for byte.
- Built-in mods and a drop-in mod can be on together. Saves are then keyed by both.

### 3.3 Not supported

| Mod | Why |
|---|---|
| Spyro 1 Co-Op Mod (Tokatta007) | Set aside for now: a large feature, and the Vita handheld has only one controller. It stays the best-documented candidate if it's taken up later (MIT, the same decomp, every hook listed in its `CHANGES.md`) |
| Practice Rom's ghost replays, hitbox views and skin presets (C0mposer) | Code that is specific to speedrunning, and large. Its timers are covered by LEVEL TIMER, and its states and level select already exist |
| Speedway Mod (LuminescentSky) as a whole | PAL only. Its supercharge, sharp-turn, dragon-skip and time-trial parts are covered by built-in rows. Its Spyro 2-style flapping could be a later row |
| Pete's Gauntlet (Husbjörn) | The download is gone and there's no description of the rules. It could become a row if the author describes it |
| April Hack, Desert Bus, the "Chinese Localization Build" parody, "Fatty", SpeedMod, Christmas Stone Hill, the Cliff Town demo | Code mods with no source, or no download, or no known author. Christmas Stone Hill could be a drop-in if it turns out to be data only |
| Running the original MIPS patches | The port has no MIPS code to patch, and the Vita can't generate code (§2). An interpreter for patch code is ruled out |
| A generic GameShark engine | It breaks with stage 4 (§2). Wanted codes become named rows instead, like INFINITE HEALTH |
| Plugin modules (compiled mods dropped in) | Possible later, but not before stages 4 and 7 settle the game's internals. It would need a stable hook API, and a check that HENkaku lets unsigned homebrew load its own modules from `ux0:` |

## 4. Routes considered

| Route | Verdict |
|---|---|
| C in the port behind a menu row | **Chosen** for code mods (§3.1) |
| Apply the player's patch file to the disc as it's read | **Chosen** for data-only mods, as a changed-sector overlay (§3.2) |
| The player's own patched image, data only | **Chosen**, the same path as the patch file (§3.2) |
| Run the mods' MIPS code | Not possible (§2) |
| A GameShark address engine | Not chosen: it breaks with stage 4 (§2) |
| Plugin modules | Deferred (§3.3) |

## 5. Legal and credits

- **Nothing in the repo or VPK carries game data:**
  - no patch, patched WAD or level;
  - no skin;
  - no location table made from levels;
  - no test fixture made from a mod.

  Drop-in mods are files the player gets from the author and keeps on their own card (CLAUDE.md). Tests use synthetic sectors.
- **Licences as found:**
  - **MIT:** the co-op mod and Spyro2x2.
  - **GPL-3.0:** the modding tools (psx-modding-toolchain, the C/C++ Game Modding Utility, Spyro-Scope, the BizHawk plugin). None of their code goes into this MIT repo.
  - **Everything else:** no licence, or not stated. A built-in row based on a mod with no licence ships only with its author's permission. The "Licence" column above shows which rows that is.
- **Built-in rows are our own C,** written from the decomp, not from the mods' sources.
- **Credits:**
  - The README gets a "Mods" section. Each built-in row names the mod it's based on, its author and the author's project page. Each supported drop-in mod is listed by name, author and project page.
  - Each row's help line in the menu names the author.
  - Link to authors' pages, never to download files.
- **OpenPete stays off-limits.** Grim Garden and the co-op mod have OpenPete versions. Only their existence is noted, and the PS1 versions are the reference. `../psxrecomp-ref` wasn't used.

## 6. Open questions for the owner

1. **The xdelta decoder:** write a small VCDIFF decoder in-tree, as `cdimage.c` did for SHA-1 (D60)? Or add xdelta3 as a dependency? Its licence needs checking first. PPF is simple either way.
2. **Inspecting Grim Garden's patch:** may a session apply it to your own image, outside git, to confirm it changes data only? The DISC MOD design depends on custom levels passing the code check.
3. **The Spyro 1 Editor:** list it as a supported drop-in, with the warning that some edits are refused? Or leave it out until its output is checked?
4. **The order of the built-in batches** in §3.1, and whether REVERSE LEVELS is wanted at all, given its size.

## Sources

- Owner's list: [Google Doc](https://docs.google.com/document/d/1LssltF91pcsDdUTTPZOkIw1LJ763GPLkuNYt03c71WA/edit).
- GitHub (READMEs, trees, licences and releases via `gh api`):
  - [Tokatta007/Spyro-1-Co-Op-Mod](https://github.com/Tokatta007/Spyro-1-Co-Op-Mod) (`README.md`, `CHANGES.md`, LICENSE), [Spyromain/Spyro2x2](https://github.com/Spyromain/Spyro2x2);
  - [C0mposer](https://github.com/C0mposer)'s Practice Rom, Supercharge Anywhere, Gem Redistributer, Skip Intro and April Hack;
  - [IikkaF/Spyro-Collectible-Shuffler](https://github.com/IikkaF/Spyro-Collectible-Shuffler) (`README.md`, `config.ini`, `src/modify_data.py`);
  - [OddKara/spyro1-mods](https://github.com/OddKara/spyro1-mods), [AlDeezy/spyro-stat-growth](https://github.com/AlDeezy/spyro-stat-growth), [LuminescentSky/Sparxless](https://github.com/LuminescentSky/Sparxless), [LuminescentSky/Speedway-Mod](https://github.com/LuminescentSky/Speedway-Mod);
  - [monty19933-hub/Spyro1Editor](https://github.com/monty19933-hub/Spyro1Editor), [Hwd405/SCALES](https://github.com/Hwd405/SCALES);
  - [mateusfavarin/psx-modding-toolchain](https://github.com/mateusfavarin/psx-modding-toolchain), [C0mposer/C-Game-Modding-Utility](https://github.com/C0mposer/C-Game-Modding-Utility).
- Web:
  - [ModDB: Grim Garden](https://www.moddb.com/mods/grim-garden);
  - spyrospeedruns.com on [in-engine levels](https://www.spyrospeedruns.com/in-engine-levels-now-available-in-spyro-the-dragon/) and on [the cheats menu and ReverseMod](https://www.spyrospeedruns.com/spyro-the-dragon-cheats-menu/);
  - [mods.spyro.one/reversemod](https://mods.spyro.one/reversemod/);
  - [speedrun.com resources](https://www.speedrun.com/spyro1/resources);
  - [Generation Amiga on the co-op mod](https://www.generationamiga.com/2026/09/03/the-original-spyro-the-dragon-now-has-two-player-split-screen/);
  - [Almar's Guides CodeBreaker codes](https://almarsguides.com/retro/walkthroughs/ps1/games/spyrothedragon/codebreaker/).
- Our code and docs: `src/platform/common/cdimage.c` (:25-29, :614-634), `src/platform/vita/menu_game.c`, `patches/spyro-1-port/0008`, `0009`; `docs/porting-plan.md` §4.1; `docs/native-plan.md` (stage 4f–4i); `docs/decisions.md` D19, D50, D51, D60, D97, D100, D101, D102 (this plan, a proposal).

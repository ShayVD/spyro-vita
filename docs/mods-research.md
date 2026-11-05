# Mods for Spyro the Dragon: what exists, and what could come to Spyro Vita

Research only, 2026-11-05. No engine code, no hardware runs, nothing downloaded. The owner asked which existing mods for Spyro the Dragon (PS1, 1998) could be added to Spyro Vita, and how. Anything added must be optional and off by default, with the default game unchanged (CLAUDE.md, D50).

**Short answer:** about 25 Spyro 1 mods exist. Almost all of them are **code** mods: MIPS code injected into the PS1 executable and shipped as xdelta or PPF patches. **Spyro Vita can't run any of those as patches**, because it never runs the disc's code (§2). They can only come in as C written for the port, behind a menu row, the way the cheats did (D50). The few **data-only** mods (custom levels, the collectible shuffler, Spyro skins) could in principle be loaded from a player's own patched disc image. Today the port refuses such an image on purpose (D60's SHA-1 check), so that is the owner's call.

The best candidates, ranked (detail in [§4](#4-shortlist)):
1. **A few small gameplay toggles** (supercharge anywhere, Sparx-less hard mode, skip the dragon cutscenes, free flight): C rows in the GAME section.
2. **Seeded randomizers** (gem values, balloonist requirements): C, with saves kept apart.
3. **Data-only mods from the player's own patched image** (Grim Garden, the Collectible Shuffler, skins): a loader rule, not per-mod work.
4. **Split-screen co-op**: MIT-licensed and built on our own decomp, but it needs a second controller, which the Vita handheld doesn't have.
5. **Per-level timer and time trials** (Practice Rom, Speedway Mod ideas).

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
- **A code mod can only come in as C written for the port**, hooked where the decomp's C does the same thing. That is the D50 pattern: a numbered patch in `patches/spyro-1-port/`, port code in `src/game/`, a row in the GAME section (`src/platform/vita/menu_game.c`), and a host flag for checks.
- **A data-only mod** (WAD data changed, executable and overlay code untouched) would load as it is. The decomp reads levels through the WAD header the same way the PS1 does. It would need the D60 check relaxed for the WAD only (§3, route C).
- **RAM-address cheats (GameShark)** work today only because the arena holds PS1 RAM at fixed offsets. Stage 4f–4i makes the data labels ordinary globals, with the game's memory from the heap (`docs/native-plan.md`, "Stage 4's steps 4f–4i"). So a generic "poke this PS1 address" engine would break when that stage lands. **Turn each wanted code into a named toggle on the named variable** instead (inferred).

## 3. Integration routes

| Route | What it is | Works for | Effort | Default unchanged? | Saves | Host / oracle checks |
|---|---|---|---|---|---|---|
| **A. C toggle** (as D50) | The mod's behaviour written as C in `src/game/`, hooked by a `patches/spyro-1-port/00NN-*.patch`, behind a GAME-section row and a host `--flag` | Every code mod | Small to large, per mod | **Yes**: the hook is a branch on an option that defaults to 0. `tools/host_checks.sh` (attract demos, tour, routes A and B) must stay byte-identical with it off, as D50's runs did | Toggles that change progress (randomizers, requirements) write it to the card. Keep those saves apart: a per-mod card directory, `save/<mod>/card0` (D19's layout) | Unaffected with the option off. With it on there is no oracle: the PS1 doesn't have the feature. Check with host scripts, plus Redux running the author's patched image as a reference, if the owner allows us to apply a patch |
| **B. Apply the player's patch at load** | Read an xdelta or PPF file next to the image and patch sectors as they are read | Nothing useful: code changes are ignored, data-only mods are better served by C | Medium (an in-tree VCDIFF decoder; xdelta's library would be a new dependency, so ask first) | Yes, if off | As C | As C |
| **C. The player's own patched image, data-only** | Accept an image whose executable matches retail and whose WAD differs, **only if every overlay's code in the WAD matches retail** (hash each overlay against the matching build's `wad/*.ovl`; hashes aren't game data). Refuse anything else with a panel ("this mod changes the game's code, which Spyro Vita can't run"). Show "MODDED DISC" on the boot panel, and use a separate save directory per WAD hash | Custom levels (Grim Garden), the Collectible Shuffler, skins (SCALES), editor output that stays within the original layout | Medium: `cdimage.c` gains a second verdict, the per-overlay hash table and the save-directory switch. The one-time WAD hash (D60) already identifies the image | Yes: the retail image takes exactly today's path | Separate card directory per WAD hash, so a modded run never writes the retail card | The oracle can run the same patched image in Redux (Redux runs it natively), so data-only mods can be checked like retail: the oracle on the player's side, never committed |
| **D. Cheats-style actions** | One-shot actions through the inventory, as 99 LIVES does | GameShark-style "give all eggs" or "max gems", if wanted | Small each | Yes | Permanent ones ask first (LEVELS OPEN's `PERMANENT? X`) | As D50 |

Route C is the one place where something like a "mod framework" would be built. It isn't recommended here as a decision: whether to relax D60 at all is an open question for the owner (§6). No decision number is recorded.

## 4. Shortlist

### 1. Small gameplay toggles (route A)
- **What:** a few rows, each off by default:
  - **SUPERCHARGE ANYWHERE** (C0mposer). The same core as Speedway's "every charge supercharges".
  - **SPARX OFF**, a hard mode (LuminescentSky's Sparxless; the GameShark "infinite health" is the opposite toggle).
  - **SKIP DRAGON CUTSCENES** (Husbjörn's cheats menu, Speedway; the Collectible Shuffler does it with data).
  - **FREE FLIGHT** with no height cap (Husbjörn).
  - optionally **SHARP TURN** during a supercharge (OddKara).
- **Why first:**
  - Each is a small change to Spyro's state machine, which the port already has as hand-written C (`src/game/pete_hand.c`, `pete_state_hand.c`, `pete_transition_hand.c`), so it doesn't touch the renderers or the disc.
  - The menu table (D50) takes a row without touching `menu.c`.
  - Saves are unaffected: no progress changes. One exception: skipping a dragon's cutscene must still free the dragon and set its save flag the way the cutscene's end does. The shuffler's README warns that crashes and despawns are possible here, so verify it against a full cutscene on the host.
- **Effort:** small each (inferred: a hook in the player or dragon update, plus a row). Skip-dragon-cutscenes is the riskiest of these.
- **Faithfulness:** default byte-identical if every hook is `if (opt) …`, checked by `tools/host_checks.sh`.
- **Permission:** none of these authors licenses their code, so write the C from the decomp, not from their source, and **ask C0mposer, LuminescentSky, Husbjörn and OddKara** before shipping a feature that is recognisably theirs. Credit them in the README either way. Free flight and infinite health are generic cheats.

### 2. Seeded randomizers (route A)
- **What:**
  - **GEM SHUFFLE** (C0mposer's Gem Redistributer): swaps gem values within a level, keeping its total.
  - **REQUIREMENT SHUFFLE** (OddKara): balloonist requirements and homeworld order.
  - Both take a seed shown in the menu and saved with the run.
- **Why:** they change how the game plays without new assets. The data they need is already in the level's mobys (gem classes `MOBYCLASS_GEM_1`–`GEM_25`, `include/moby.h:326-330`) and in the balloonist code (`src/overlays/balloonist.c`; exactly where the requirements live wasn't checked).
- **Effort:** medium (inferred). Gem shuffle: one pass over the level's mobys after load, deterministic from the seed and level ID, so a reload gives the same layout. The Redistributer's README lists exactly this bug: totals drift after enemies drop orbs on re-entry.
- **Saves:** a randomized run must not write the retail card. Use route A's per-mod card directory, keyed by mod and seed.
- **Permission:** ask C0mposer and OddKara (no licence).

### 3. The player's own data-only patched image (route C)
- **What:** Grim Garden, Collectible Shuffler output, SCALES skins, and editor output that keeps the executable as retail. The player patches their own image with the author's patch or tool, then copies it to `ux0:data/spyro/`.
- **Why:** it is the only way custom levels and the shuffler can come in at all. We ship nothing: no patch, no data, no link to one. That is the cleanest position for game data (CLAUDE.md) and for the authors' terms, since players use the author's own release as the author intended.
- **Effort:** medium (§3). The risk is mods we can't predict: a WAD that's bigger than retail, or levels that use more memory. The arena is laid out as on the PS1 (D23, `docs/porting-plan.md` §4), so what fits on a PS1 should fit here (inferred). The overlay-code check refuses mods with code. Mixed mods like the Spyro 1 Editor's "New World" or sky relocation change the executable and would be refused (verified: its README says "executable relocation").
- **Not checked:** whether Grim Garden's patch touches the executable or overlay code. That needs the patch inspected (§6).
- **Permission:** none needed to *support* the format. Respect each author's stated terms on redistribution (none stated so far).

### 4. Split-screen co-op (route A, large)
- **What:** two dragons, two cameras, one screen split.
- **Why it is attractive:**
  - It is **MIT**, so its design and code may be reused with its copyright notice kept. Ask anyway, as a courtesy.
  - It targets our exact disc (same SHA-1).
  - It was built against **TheMobyCollective's decomp**, the same base as ours.
  - Its `CHANGES.md` lists every hook, allocation and swapped state. Its method maps directly onto the decomp's C: run Spyro's tick, the camera, the moby pass and the scene build twice per frame, with a second state swapped in.
  - The PS1's limits that stopped it (11 KB of scratch RAM, half the frame rate at a 300% overclock) are not ours.
- **Why it is fourth:**
  - The **Vita handheld has one built-in controller.** A second player needs a PS TV, or Bluetooth controller plugins on the handheld (inferred, not checked).
  - Drawing the scene twice doubles the render cost against our 60 fps budget (D75, D77).
  - It touches the game loop, the camera, the moby update and the HUD: the same areas stages 4 and 7 are changing now (D97, D100, D101).
  - The author's own `BUGS.md` lists open issues: a camera glitch on hits, and pitch in flight levels.
- **Effort:** large (inferred: weeks, with hardware time).
- **Saves:** shared lives, so normal card saves are plausible. Check how its pause-menu page and per-player colour are stored before deciding.

### 5. Timer and time trials (route A)
- **What:** an on-screen per-level timer and a saved best time per level (the Practice Rom's timers, the Speedway Mod's time trials).
- **Why:** QoL that players ask for, and it doesn't change gameplay.
- **Effort:** small to medium. A timer drawn by the port's overlay panel (`menu_panel.c`) avoids touching the game's HUD. Best times go in a port file, not the memory card.
- **Permission:** the feature is generic. Ask C0mposer and LuminescentSky before copying how theirs look or behave.
- The Practice Rom's save states and level select are already in the port (D51, D50 WARP TO). Ghost replays and hitbox views are larger and specific to speedrunners: not shortlisted.

**Not shortlisted:**
- **ReverseMod:** closed source, and it would need spawn and exit swaps for every level re-made by hand.
- **Stat Growth and "Fatty":** niche.
- **The joke and parody mods:** no source, and some no download.
- **Pete's Gauntlet:** dead link.
- **Speedway Mod:** PAL only, but its features are covered by items 1 and 5.
- **GameShark codes as an engine:** they break with stage 4 (§2). Individual codes become rows where wanted.

## 5. Legal and permissions

- **We ship no game data and nothing derived from it.** That rules out committing or shipping any patch, patched WAD, level, skin, location table extracted from levels, or test fixture made from a mod (CLAUDE.md). Route C only reads what the player made on their own machine.
- **Licences as found:**
  - **MIT:** the co-op mod and Spyro2x2. Reuse is allowed with the notice kept.
  - **GPL-3.0:** psx-modding-toolchain, the C/C++ Game Modding Utility, Spyro-Scope and the BizHawk plugin. These are tools; don't copy their code into this MIT repo.
  - **Everything else:** no licence, or not stated.
- **Ask first:** C0mposer (Supercharge Anywhere, Gem Redistributer, Practice Rom features); OddKara (sharp turn, Requirement Randomizer); LuminescentSky (Sparxless, Speedway features); Husbjörn (free flight, dragon-skip, ReverseMod); IikkaF (if the shuffler's location tables are ever wanted in C). Each needs the owner's go-ahead to contact them. Most give a Discord handle as their contact.
- **Ideas aren't code** (not legal advice). Writing a feature like "supercharge anywhere" from the decomp, without reading the mod's source, is our own code. The brief asks to treat a recognisable re-implementation as needing permission anyway, and this doc does.
- **OpenPete stays off-limits.** Two mods here have OpenPete versions (Grim Garden; the co-op mod's successor, Tokatta007's `Spyro-1-Co-Op-OpenPete`, MIT). Only their existence is noted. Nothing from them was read beyond the co-op repository's README, which was used to find the PS1 mod. Use the PS1 co-op repository as the reference, not the OpenPete one.
- `../psxrecomp-ref` wasn't used.

## 6. Open questions for the owner

1. **Do mods belong in Spyro Vita at all before stage 4 and stage 7 land?** Items 1 and 5 are small and independent. Items 2 and 4 touch the game loop that those stages are changing.
2. **Route C:** relax D60 to accept a player's data-only patched image (executable and overlay code still checked), with a "MODDED DISC" panel and separate saves? Or keep the port strictly retail?
3. **May we contact the authors?** C0mposer, OddKara, LuminescentSky, Husbjörn and IikkaF, and Tokatta007 for co-op as a courtesy (MIT doesn't require it).
4. **May a session apply a mod's patch to the owner's own image, outside git**, to check what it changes (code or data) and to use Redux as a reference? Grim Garden's patch is the one route C depends on. The brief says to ask before inspecting any mod file.
5. **Saves:** is a per-mod card directory (`save/<mod>/card0`) acceptable, or should modded runs share the retail card?
6. **Co-op:** is a PS TV / two-controller feature worth large work on a handheld-first port?
7. **Where do mod rows go:** in the GAME section beside the cheats, or in a new MODS section?

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
- Our code and docs: `src/platform/common/cdimage.c` (:25-29, :614-634), `src/platform/vita/menu_game.c`, `patches/spyro-1-port/0008`, `0009`; `docs/porting-plan.md` §4.1; `docs/native-plan.md` (stage 4f–4i); `docs/decisions.md` D19, D50, D51, D60, D75, D77, D97, D100, D101.

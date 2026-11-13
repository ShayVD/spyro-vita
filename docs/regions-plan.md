# The PAL and Japanese discs: what differs, and a staged plan (D131)

Written 2026-11-10 on main `bb62f38`. This document changes no engine code. It adds one measuring tool, `tools/regions/compare_code.py` (§2.1). The owner reviews the plan before any engine work starts. Each stage below is its own PR, and the owner merges every one.

**Where the numbers come from:**
- **Inputs:** the PAL and Japanese images the owner supplied (§1), extracted with `tools/extract_disc.py --no-verify`, and the matching build's ELF (`build/s1-matching/build/psx.elf`).
- **Measured:** the counts from the tool, the WAD headers, the XA channel hashes and the level headers.
- **From reading disassembly:** the per-function findings in §3–§6. That reading was done with `mipsel-linux-gnu-objdump` from the decomp's Docker image, on diffs aligned by the tool. The working notes, with more addresses than fit here, are in the session notes folder (`regions-pal-code.md`, `regions-jp-code.md`). This document carries everything the stages need.
- **Marked *estimate*:** reasoning, not measurement.

**Addresses:**
- "US 0x…" is the NTSC-U executable's address, which is the decomp's.
- "PAL 0x…" and "JP 0x…" are addresses in those executables as loaded (base 0x80010000, file offset 0x800).

**No game data is quoted or committed:** text is described, not reproduced.

## Summary

- **The discs.** I couldn't confirm either dump against Redump: redump.org refused the connection, and both images came from a ROM site. Neither image has FMV; every `.STR` file is XA audio.

  | | NTSC-U (supported) | PAL (supported since R4, D154) | Japan |
  |---|---|---|---|
  | Serial | SCUS-94228 | SCES-01438 | SCPS-10083 (Limited Edition, disc 1 of 2) |
  | Languages | English | English, French, German, Spanish, Italian | Japanese |
  | Executable | `SCUS_942.28` | `SCES_014.38` | `SCPS_100.83` |
  | `WAD.WAD` | 110 MB | 211 MB | 113 MB |
  | Music files | `PETEXA0–5.STR` | `MUSIC1–6.STR` | `MUSIC1–6.STR` (byte-identical to PAL's) |
  | Image SHA-1 | `cf3ce6be…` | `52bba2f6…` | `e89da928…` |

- **PAL is NTSC-U plus edits.**
  - **Main executable:** 61.4% of the US function bytes are the same, modulo relocations. 96.7% of the instructions are covered. That includes five big renderers that differ only in relocations the matcher can't mask.
  - **Level overlays:** 96% covered.
  - **About 50 functions changed**, in four kinds:
    - text and the five languages;
    - the 512×256 PAL picture;
    - a 50 Hz retune;
    - a few fixes.
  - **Real rewrites:** the letter-model spawner (accents), the pause and inventory screens, the title screen with its language select, the level-transition gems, the pause menu (a screen-position option), and one credits function.
- **PAL keeps NTSC's tick and retunes thousands of constants.**
  - **The tick:** `main`, `GamestateDraw`, `GamestateUpdate` and `PadVSync` are the US code. That means the same `g_DeltaTime` clamp to 2..4 and the same 2-vblank hold, so PAL's logic ticks at 25 Hz.
  - **The retune:** speeds are ×1.2, accelerations ×1.44 and durations ×5/6. There are about 250 sites in Spyro's controller and physics, and about 2,740 + 910 + 42 constant sites over the level overlays. Some of it is changed instruction sequences, not constants. The camera isn't retuned.
  - **So:** PAL's own behaviour means running PAL's own code at 25 Hz. A table of constants applied to the US code can't reproduce it.
- **JP is a different game in places.**
  - **Shared with PAL:** both come from a post-US branch. They share the 16-slot cutscene table, the `MobyMove` fixes and the WAD at LBA 500.
  - **Not shared with PAL:** JP has none of PAL's 50 Hz retune, language system or CD loader.
  - **JP's own changes:**
    - a 2D font in a custom single-byte encoding, replacing the 3D letter models;
    - a 512×232 picture;
    - new game states that look like added hint dialogues;
    - about 15 KB of new camera code;
    - a field-of-view setting that eases every tick;
    - shifted struct layouts;
    - an anti-mod-chip check.
  - **The numbers:** 47.0% of the US function bytes are the same and 92.6% of instructions are covered. Level overlays are about 72% covered, and three of them weren't measured. **About 13,000 instructions of new JP code are in the main executable alone** (*estimate* from the uncovered spans, §4.4).
- **Voices:**
  - **PAL:** dubbed dragon voices in all five languages. Each level holds five sets, and the intro and the two endings exist once per language.
  - **JP:** dubbed throughout. No dragon voice block and almost none of the intro or ending data is shared with the US disc.
- **Text:**
  - **In the executable:** on every disc the game's text is in the executable.
  - **Fonts:** PAL draws accents as extra models; JP has a font sheet in its WAD.
  - **We can't commit any of it:** the PAL and JP text, fonts and tables are the player's disc data. The port must **read them from the player's verified executable at start-up**, through address maps we write (§6.2).
- **What I recommend** (D131):
  - **(c), a mix.** One codebase. Small edits become region switches in the decomp's C and our hand C. Rewritten functions get region versions. Region data is loaded from the disc.
  - **The check for decomp C:** a per-region matching build compared function by function with the disc's code. That's this tool, with the target "same" for every function.
  - **The check for hand C:** PCSX-Redux replays on that region's disc.
  - **The gate on every stage:** the US checks' 279 files stay byte-identical.
  - **PAL first** (stages R0–R4, *estimate* 8–11 PRs). **JP after a sizing stage** (J0–J3, *estimate* 10–15 PRs, more uncertain).
  - **PAL runs at its own 25 Hz by default**, interpolated to 60 fps by the presenter. A "PAL 60" option that puts back the US timing constants costs little once the sites are tagged. It's the owner's decision.

## 1. The discs

Copied from `~/Downloads` (7-Zip archives) into the gitignored `disc/` tree (`.gitignore`: `/disc`, `*.bin`, `*.cue`), then extracted with `tools/extract_disc.py --no-verify`. **The PAL archive held only a `.bin`, no `.cue`.** I wrote a one-track cue for it (`TRACK 01 MODE2/2352`, which is how the JP and US cues are laid out). The bin is exactly 311,391 raw sectors with sync at sector 0.

| | NTSC-U | PAL | Japan |
|---|---|---|---|
| Folder | `disc/image/` → `disc/files/` | `disc/image-pal/` → `disc/files-pal/` | `disc/image-jp/` → `disc/files-jp/` |
| Image (bytes, SHA-1) | 661,547,040, `cf3ce6bedeb89dfbc40990336180f3b9b0f40d9f` | 732,391,632, `52bba2f6a7eec6ac7dc4d08de13a01b0bbce30b6` | 727,179,600, `e89da92828a7f199b26623023bc7f36d0dc4eb88` |
| `SYSTEM.CNF` BOOT | `SCUS_942.28` | `SCES_014.38` | `SCPS_100.83` |
| Executable (bytes, SHA-1) | 417,792, `84e3728ab94720d0873e2514adf4aade4935e0c5` | 444,416, `fdd04c044cd1892ae28c43e9e619dd41a848058a` | 458,752, `3818ddb9520e2fe0114de48f5392972c33690399` |
| Exe header: text size, entry | 0x65800, 0x8005B8E0 | 0x6C000, 0x80060D10 | 0x6F800, 0x8006076C |
| `WAD.WAD` (LBA, bytes, SHA-1) | 37, 110,260,224, `ab680013…0b` | 500, 211,079,168, `f1fc05d7cf197ba9cfc693738b44020d8798f99d` | 500, 112,793,600, `812f63ff07366cf80f152ffd5bdd2e03e0582abb` |
| Music | `PETEXA0–5.STR` (LBA 60000…) | `MUSIC1–6.STR` (LBA 104000…) | `MUSIC1–6.STR`, same LBAs and bytes as PAL's |
| `S0/` (the Crash demo) | yes | yes (different files) | **none** |
| `SOURCE/SOURCE.TRD` | `498601c3…` | the same | the same |

The other PAL and JP files' SHA-1s are in each `disc/files-*/` folder (`shasum` of the extractor's output, XA as 2336-byte sectors). Stage R0 puts them in `extract_disc.py`'s table.

**Caveats:**
- **Provenance.** Both archives carry a ROM site's readme, and the PAL image is named without Redump's language tag. redump.org refused the connection from here, so **neither image's hash is confirmed as a good dump.** The owner should check both against redump.org before R0 hard-codes them (§10, decision 6).
- **Other editions.**
  - **Japan:** this is the Limited Edition's disc 1, SCPS-10083. The Limited Edition's disc 2 is SCPS-10084. The standard release is SCPS-10085, and SCPS-45395 is an Asian NTSC-J release (psxdatacenter.com). They may differ, at least in the executable's name.
  - **PAL:** reprints (Platinum) keep the serial SCES-01438, but may not keep the hashes.
  - **What the port does:** it accepts known hashes only, so each new edition needs a dump's hashes.
- **No FMV.** Every `.STR` sector on all three discs is XA audio (submode 0x04 with stereo coding) or an empty sector. Spyro's cutscenes are in-engine (WAD data).

**The decomp has no PAL or JP configuration.** `third_party/spyro-1` has one mention of PAL: `src/spu.c:755`, "Removed after PAL Review, not present in NTSC final". Its `readme.md`, `overlays/*.yaml`, `Makefile` and tools are NTSC-U only. Its branches are `main` and `experiment-overlays`. The French-language investigation (2026-10-31) found no public PAL or JP symbol map. alexbeavs' PAL recomp is GPL-3.0 and built on the off-limits psxrecomp, so it's unusable here.

## 2. How much code is shared

### 2.1 The tool

`tools/regions/compare_code.py` takes each function symbol of the matching build's ELF. It masks relocations: the `j`/`jal` targets, `lui` immediates, and the immediates of `addiu`/`ori`/loads/stores whose base isn't `$sp`. Then it looks for the same instruction words in the other release.
- **"Same":** found once.
- **Coverage:** for the others, the share of the function's masked words that `difflib` finds, in order, in the gap between the neighbouring functions that were found.
- **Limits:** coverage is an estimate. The mask also hides small `li`/`slti` constants, so "same" means same modulo relocations *and* small constants. Function boundaries in the other release are inferred, not known.

It prints names, addresses, sizes and ratios only.

```
python3 -I tools/regions/compare_code.py exe disc/files-pal/SCES_014.38 --list
python3 -I tools/regions/compare_code.py wad disc/files-pal/WAD.WAD pal --list
```

### 2.2 Results

| | US against itself | PAL | JP |
|---|---|---|---|
| Main exe: functions the same (of 581) | 581 | 521 | 487 |
| Main exe: bytes the same (of 367,068) | 100% | 61.4% | 47.0% |
| Main exe: weighted coverage | 100% | 96.7% | 92.6% |
| 35 level overlays: weighted coverage | — | 96.0% (90.4–98.6% each) | 72.3% (LEVEL_13, 20, 21 not measured: alignment failed) |
| Title-screen overlay | — | 63% | 40% |
| Credits overlay | — | 89.7% | 100% |

- **PAL's real share of unchanged code is higher than 61%.** Seven large functions (US 0x8001F798, 0x80020F34, 0x80022A2C, 0x80023AC4, 0x800258F0, 0x800573C8 and 0x80058D64) differ only in relocated addresses inside hand-written code that the mask can't see. Counting them, **about 80% of PAL's main-executable code bytes are unchanged**.
- **Overlay coverage is the meaningful figure.** An overlay is a few giant functions (`UpdateMoby` is 32–40 KB), so "same" is rare there.

### 2.3 The WAD

All three WADs start with an `OffsetLength` table (`include/wad.h:17`, `g_WadHeader`), but the tables differ:

| Entry | US (102 entries) | PAL (114) | JP (116) |
|---|---|---|---|
| Logo, title graphics (`m_wad1`) | 0, 1 (256 KB) | 0, 1 (528 KB) | 0, 1 (336 KB) |
| Font sheet | — | — | **2** (40 KB, 4 bpp) |
| Title overlay | 2 | 2 | 3 |
| Cutscenes | 3–6 (4: title, intro, outro 1, outro 2) | 3–18 (16: title, then intro/outro 1/outro 2 × 5 languages) | 4–19 (16 slots; 12 empty) |
| Game-over skybox, PETE | 7, 8 | 19, 20 | 20, 21 |
| Levels (overlay, data) × 36 | 9–80 | 21–92 | 22–93 |
| Credits text | — | — | **94** (50 KB) |
| Credits overlay, credits data × 20 | 81, 82–101 | 93, 94–113 | 95, 96–115 |

The US code reads the header at a fixed sector (37) with a fixed struct. PAL and JP read it at LBA 500; JP's `WadInitialize` (JP 0x80011924) reads 928 bytes, PAL's 912. So each region needs its own header layout. It's a small table.

## 3. PAL: what differs

### 3.1 Timing

**The frame loop is the US one.** These are identical modulo relocations:

| Function | US | PAL |
|---|---|---|
| `main` (`src/main.c:13–30`) | 0x80012204 | 0x80015DCC |
| `GamestateDraw` (`src/gamestates/draw.c:856`, the 2-vblank hold) | 0x8001ED5C | 0x80023D94 |
| `GamestateUpdate` | 0x8003385C | — |
| `PadVSync` | 0x80053C68 | — |

So PAL's design rate is delta 2 at 50 Hz: **a 25 Hz tick**.

**PAL scaled its constants to keep real-time speed.** Speeds are ×1.2 (6/5), accelerations ×1.44 and durations ×5/6:

| Where | Examples (US → PAL) | Sites (×1.2 / ×1.44 / ×5/6) |
|---|---|---|
| Spyro's controller `func_80043FE4` (`src/game/pete_hand.c:1595`) → PAL 0x800494D8 | 5120→6144, 7680→9216; acceleration 192→276, 320→461; timers 45→38, 13→11 | 132 / 35 / 5 |
| Spyro's physics `func_8003EA68` (`pete_hand.c:314`) → PAL 0x80043EE8 | gravity −192→−276, −3072→−4424; caps 6400→7680 | 18 / 35 / 10 |
| `MobyMoveWithGravity` US 0x80039910 → PAL 0x8003EBB4 | 500→600, terminal velocity −260→−311 | 3 |
| `MobySpawnDrop` US 0x8003ABC0 → PAL 0x8003FED0 | 300→360, 140→168 | 3 |
| `func_800334D4` (`update.c:1291`) → PAL 0x800386B8 | vblank durations 720→600, 1168→972 | 2 (×5/6) |
| `Initialize` (`initialization.c:323,366`) → PAL 0x800163A4 | logo hold 210→160 vblanks | 2 |
| `CDLoadSync`, `CDLoadAsync` → PAL 0x8001A190, 0x8001A364 | timeout t → (5t+3)/6 for t ≥ 6 (new code) | — |
| Cutscene camera (`func_8002BFE0`, `cutscene.c:14`; `GamestateCutsceneTransition`) | `tick>>1` → `(tick*3)>>2`; a /60 divide → /50 (PAL 0x80031144) | — |
| Level overlays' `UpdateMoby`, `SpawnMoby`, `SpawnParticle`, balloonist | e.g. Artisans: timer 90→75, 200→240, 130→156 | **about 2,740 / 42 / 910 over 37 overlays** |

**Not retuned:**
- ~~the camera (`func_80033F08`, `func_80034480`, `func_80034CE8`, `func_800357A4`, `func_80035FB4` are identical), so it moves 5/6 as fast in real time;~~ **Corrected in D133:** these were "identical" only because the comparison masks `li` constants. With `compare_code.py --exact`, `func_80034CE8` (1024→1475, 2048→2457), `func_800357A4` (45→38 twice, 8→7, 90→75), `func_80035F58` (1024→1229) and `func_80035FB4` (90→75 four times) are retuned too. `func_80033F08` and `func_80034480` stay identical. Stage R3 takes the camera's sites as well.
- the cyclorama (its only constants, 240→256 four times in `func_80050BD0`, are the 256-line picture's: R2);
- the HUD, which already counts vblanks through `g_DeltaTime`.

The overlay counts only include constants that align one to one. Each level also has a few hundred inserted or changed instructions.

**The retune doesn't factor out.** It sits in thousands of places, some in changed instruction sequences. Two of the cheap options are wrong:
- **PAL's code at a 30 Hz tick** runs PAL's gameplay 20% fast.
- **NTSC's code with PAL's text** is NTSC's gameplay with PAL words.

Only PAL's code at 25 Hz is PAL.

### 3.2 Video

- **`GraphicsInitialize`** (US 0x800122A8, `initialization.c:35`) → PAL 0x80015E70:
  - adds `SetVideoMode(MODE_PAL)`;
  - draws and displays **512×256**, where US draws 512×224 inside a 512×240 display;
  - reads the display offsets from two user screen-position globals (PAL 0x8007B74C, 0x8007B750).
- **`Initialize`:** `SetGeomOffset(256,120)` → `(256,128)`, with the same `SetGeomScreen(341)`. So PAL sees more vertically.
- **Layout constants for 256 lines** change in `func_8001A40C`, `func_8001C694` and `func_8001973C`, for example 224→256 and 208→224.

### 3.3 Languages and text

- **Five languages:** 0 English, 1 French, 2 German, 3 Spanish, 4 Italian. The index is a global at PAL 0x8007C0CC, read 89 times in the main executable.
- **Only the title overlay sets it.** Left and right step it modulo 5; the title overlay loads at PAL 0x80081428, against US 0x8007AA38. A memory-card load restores it from the save's header. **The language can't be changed in game:** the main executable never stores it.
- **The string pool is about 1,050 strings (18 KB)**, at PAL 0x80010000–0x800149EF. US has 289 strings (4.7 KB) at 0x80010000–0x80012203.
- **The lookup tables:** about 40 five-pointer tables (EN, FR, DE, ES, IT) at PAL 0x80073D34–0x80075750, some of them 7–72 pointers long. The code indexes them as `TABLE + lang*4`, for example at PAL 0x8001D50C.
- **Accents are a capital letter followed by a lowercase marker byte.**
  - The two US letter-model spawners, `func_80017FE4` and `func_800181AC` (`draw.c:46`, `:82`), are merged into one rewritten function at PAL 0x8001BD5C (1,100 bytes).
  - Markers `'a'`–`'e'` spawn accent-mark models, classes 365–369, on the letter before. `'f'` is another special.
  - It adds `-`, `,` and `)` to US's `! ' ? . / % ^ +`.
  - The same handling is added to the floating level names (`func_8003C358`, `moby_helpers.c:2199` → PAL 0x80041698) and the rescued-dragon names (`func_80018728`, `draw.c:207`, rewritten).
- **Rewritten around the tables:**
  - `func_8001A40C` (`draw.c:880`, 8,840 → 11,516 bytes: the pause and inventory screens);
  - `func_8001D718` (`draw.c:2039`);
  - `func_8001973C`;
  - `func_8001E24C`, `func_8001E6B8`, `func_8001EB80`;
  - `func_80018908`, `func_800190D4`, `func_80019300`;
  - `func_8001CA38`, `func_8001C694`;
  - the balloonist's dialogue in each home world's overlay;
  - the credits overlay's `func_credits_8007BFD0`;
  - the title overlay's `TitlescreenDraw`, which matches nothing.
- **The pause menu gained a screen-position option** (`func_8002E12C`, `update.c:179` → PAL 0x80032D58, 5→6 and 6→7 options).

### 3.4 Cutscenes, dragon voices, loading

- **Cutscenes:** `LoadCutscene` (US 0x80014564 → PAL 0x80018180) takes WAD entries from a 16-slot table. The intro is slot `1 + lang*3`, set in `GamestateCutsceneTransition` at PAL 0x80037CE8.
- **`LevelHeader` (`include/wad.h:42`) grows from 464 to 656 bytes:** `m_Dragons[6]` becomes `m_Dragons[5][6]`, by language. `LoadDragonCutscene` (US 0x80014B70 → PAL 0x800187B4) takes `m_Dragons[lang*6 + dragon]`.
- **Level data:** the PAL level-data entries are 2–3 times the US ones: five languages of dragon audio. A listed PAL dragon block shares about half its 2 KB blocks with the US one: presumably its English audio, not checked by listening.
- **The rest:** `LoadLevel` is the US one apart from the header size and two `printf`s. `SetOverlayPointers` has PAL's own overlay addresses.
- **Music:** loaded by the same code as US, from `MUSIC1–6.STR`.

### 3.5 Other changes

- **The level-warp cheat is gone** (`CheatProcessLevelWarp`, `cheats.c:177`, and its call in `func_8002EB2C`).
- **`MobyMove`** (US 0x80039688 → PAL 0x8003E8F8) re-runs the collision test after a correction call and can return 4. **`MobyMoveWithGravity`** then scales vertical speed by 3/4 when an angle test passes. JP has this fix too (§4).
- **Small edits:** `UpdateSlopeFloorCollision` (two offsets), `func_8003E90C` (`>>4` → `>>12`), `func_80047B60`, `ColMoby` and `func_8004DF24` (about 5 instructions), `func_80032AB0` (a constant 1→7), and the cutscene start and end clears (512×256).
- **Corrected in D134** (R2 checked each against the disc):
  - `UpdateSlopeFloorCollision`'s change is a test of Spyro's state (not 11, not 2) before its fallback ray; the offsets were register addressing.
  - `func_8003E90C`'s is `x >> 4` → `(x × 307) >> 12`, ×1.2: a 50 Hz speed retune (R3), not a fixed-point change.
  - `MobyMoveWithGravity`'s slowdown on a `MobyMove` result of 4 scales the *horizontal* speed by 3/4.
  - `ColMoby` is identical apart from relocations. *(Wrong, D149: in its four volume kinds PAL records the hit moby after the pass-through test, not before, so a pass-through volume doesn't block in PAL.)* `func_8004DF24` also keeps the floor's normal in `g_CollisionNormal`. `func_80047B60` copies only x and y of the post-collision velocity in the air.
  - The title flyby's `(Sin >> 1) + 0x2980` → `Sin × 3/4 + 0x2AA0` in `GamestateCutsceneTransition` is the 256-line picture's (Spyro's path raised), not timing.
- **Debug `printf`s** remain in the CD functions, `LoadLevel` and `KillOrphans`. They have no behaviour.

### 3.6 Memory card

- **The file name** is `BESCES-01438SPYRO`, against US `BASCUS-94228SPYRO` (`titlescreen.c:37`).
- **The save itself** goes through the same path (`func_800314B4`).
- **New: a 256-byte header.** PAL copies a 256-byte header template and packs `lang + (screenX<<4) + (screenY<<10)` into its halfword at +0x7C. It writes that at file offset 0 when the title screen marked it changed (PAL 0x800373C8–0x80037470).
- **Checked in D139:** PAL's template (0x80074C24) is the standard header, "SC", one icon frame, one block. +0x7C and +0x7E are the icon palette's colours 14 and 15, 0 in the template: PAL keeps its language and screen position, and the save's id, in the last two palette entries. The save stays one block.

## 4. Japan: what differs

**Lineage.** JP and PAL share code that US lacks: 1,938 eight-instruction sequences, against 1,057 shared by JP and US but not PAL. The shared code:
- the 16-slot `LoadCutscene` (JP 0x80013B18);
- the `MobyMove` and `MobyMoveWithGravity` fix;
- `UpdateSlopeFloorCollision`, `ColMoby`, `func_8004DF24`;
- `SetOverlayPointers`;
- `func_8002DA74`;
- the WAD at LBA 500.

JP keeps US's CD loader and `KillOrphans`. **JP has none of PAL's retune**: `MobyMoveWithGravity` keeps 500 and −260, and `main` (JP 0x800115D0) is the US one, constants included. JP is a 60 Hz game with US timing on a post-US branch, plus a great deal of its own work.

### 4.1 Text and font

- **The 3D letter models are gone.** `func_80017FE4` and `func_800181AC` are 3% similar, and `func_80018728` has no counterpart.
- **The font sheet:** a 4 bpp sheet, WAD entry 2, is loaded to VRAM at (0, 472, 512×40) by `Initialize` (JP 0x800120F0).
- **The glyph emitter:** JP 0x80022868 (724 bytes) builds textured quads (POLY_FT4) with the palettes at (352, 510–511).
- **New helpers:** JP 0x800175C8 (number and string utilities), 0x800176F0 (string width: digits 18 pixels wide, glyphs 24 or 32, combining marks and control codes 1) and 0x80017798 (centring).
- **The encoding is a custom single byte, not Shift-JIS.** Glyph indices are ≥0x80, digits are ASCII and strings end in 0.
- **Where the text is:** in the executable's data around JP 0x80078640. Dragon names are fixed 8-byte slots from JP 0x80078724, not US's `g_DragonNames` pointer table (`strings.c:4`).
- **Making room for the sheet:**
  - the framebuffers shrink to **512×232** at y = 0 and 240 (`GraphicsInitialize`, JP 0x80011674), so JP shows 8 fewer lines;
  - the world ordering table shrinks from 0x800 to 0x600 entries (`func_8001A050` at JP 0x8001A3E8, `func_8001E9C8`);
  - `AllocateBuffers` (`4BEF8.c:13`) clears 0x3000 bytes, not 0x4000.
- **Rewritten as new code:** the pause, inventory, options and game-over screens, at JP 0x80019658 (7,704 bytes), 0x8001B7CC (1,320), 0x8001C420 (5,044) and 0x8001D220 (964). They replace `func_8001A40C`, `func_8001CA38`, `func_8001D718`, `func_8001E24C` and `func_8001E6B8`, which are 2–40% similar.
- **The credits:** the credits overlay is US code, but `InitCreditsSequence` (JP 0x8002C42C) loads WAD entry 94. That's probably the Japanese credit text.

### 4.2 Gameplay and other JP-only code

These are from static reading. The bullets marked *(to confirm in Redux, J0)* need watching in the emulator.

- **New game states 16 and 17,** dispatched by `GamestateUpdate` (JP 0x80033F18) to JP 0x8002CF7C and 0x8002DB14.
  - State 16 is entered from `LoadLevel`: a level-entry state.
  - State 17 is entered from every non-flight level's overlay.
  - A new proximity test (JP 0x8002BE98) enters the fairy state from every ground level but two.
  - The fairy cutscene (`InitFairyCutscene`, `func_8002CB6C`) is rewritten.
  - *Reading: added hint or talk dialogues (to confirm in Redux, J0).*
- **A field-of-view setting that eases.** `GamestateUpdate` calls `SetGeomScreen` every tick (JP 0x80034364–0x8003440C). The value moves 2 per tick toward 341 + 30·v, where v lives at JP 0x8007F220 and starts at 1. In the dragon and cutscene states the target is a fixed 341. US sets 341 once (`initialization.c:261`). *What writes v: not found (J0).*
- **The camera is rewritten:**
  - `CameraUpdate` has no counterpart;
  - six camera functions are 1–52% similar;
  - there's about 15.6 KB of new camera code at JP 0x80036C9C.
- **Spyro and input:**
  - `func_80043FE4` is called with `i>>1` (JP 0x8004CFF8);
  - `PadVSync` changed its input buffering and gained a vblank counter (JP 0x80058260);
  - `HandleSpyroDamage` masks an input and plays a sound.
- **Struct layouts shift:**
  - a field of Spyro's struct moves from +628 to +632;
  - the glow particle grows from 0x24 to 0x28 bytes (`r_particles.s:1614`).

  So the decomp's structs don't fit JP as they are.
- **Mobys:** `MobyMarkCollected` plays a sound at a collection threshold. Every overlay's `SpawnParticle` and `UpdateMoby` differ.
- **Removed:** the Crash demo launcher (the JP disc has no `S0/`) and the level-warp cheat.
- **An anti-mod-chip check.** `Initialize` loops on JP 0x8005FF0C; JP 0x800602E4 shows a Shift-JIS warning screen. The port must skip that code and continue as an unmodified console would.
- **The title overlay** is 40% similar (`TitlescreenDraw` 0%).

### 4.3 Voices, cutscenes, memory card

- **Dragon voices are Japanese.** No US dragon block appears in JP's level data, and the blocks' lengths differ.
- **Cutscenes:** the intro and endings (WAD entries 5–7) share 7–32% of their 2 KB blocks with US. The title cutscene shares 99.7%.
- **Each level's VRAM/SRAM block is about 40 KB larger.**
- **The save:** the file name is `BISCPS-10083SPYRO` (JP 0x800100C8). `MemCardSync` changed, and there are new save helpers next to `SaveCreate` (JP 0x8005DBFC, 0x8005E10C, 0x8005E180). *Layout to check (J0).*

### 4.4 How much new code

JP spans with no US counterpart in the main executable add up to about **52 KB, roughly 13,000 instructions** (*estimate*). That leaves out the string data at the start and the end of `.data`, and the US functions JP edited. The largest:

| JP address | Bytes | What |
|---|---|---|
| 0x80036C9C | 15,636 | camera |
| 0x80030698 | 9,428 | after `CheckMemcardsExist` |
| 0x80019658 | 7,704 | pause, inventory |
| 0x8001C420 | 5,044 | options |
| 0x800330B4 | 2,492 | |
| 0x8005FF0C | 2,144 | the mod-chip check (not translated) |
| 0x80034B44 | 1,972 | |

The overlays add more. Their 72% coverage mixes real changes with noise from register allocation, so **J0 has to measure them properly** before JP's size is known.

## 5. Data, audio and assets

| | PAL | JP |
|---|---|---|
| Level data format | `LevelHeader` 656 bytes (`m_Dragons[5][6]`); the US sections otherwise | US sections, 464 bytes; VRAM/SRAM sections about 40 KB bigger |
| Text | executable: 5 languages, pointer tables | executable: custom encoding |
| Font | US 3D letters plus accent-mark models (classes 365–369) | 2D sheet, WAD entry 2 |
| XA music | `MUSIC1–6.STR` | the same files as PAL |
| Dragon voices | 5 languages | Japanese |
| Intro, endings | per language | Japanese |
| FMV | none | none |

**Music.** Hashing every XA channel (file, channel, sectors) shows:
- **39 of the 62 channel streams** are byte-identical between `PETEXA*` and PAL's `MUSIC*`, in different files and channels (for example US `PETEXA1` channel 5 is PAL `MUSIC3` channel 0).
- **Some streams exist on one side only** (different lengths or mixes).

So each region needs its own track table (file and channel per level). It lives in the executable's data, so the stage that loads the data maps it.

## 6. What the port needs

### 6.1 Code: the approach

Three candidates from the brief:
- **(a) The US code with each region's data,** plus small switches. That's the cheapest, but it's wrong for both regions:
  - PAL's gameplay is in its code (§3.1), and its language system is code too (the merged spawner, the title's select, the language-indexed cutscene and dragon slots);
  - JP's font, screens, camera and new states are code.
- **(b) Region translations of each differing function.** Cleanest for real rewrites. For PAL's retune it would mean copying whole 15–40 KB functions (`func_80043FE4`, every `UpdateMoby`) to change a few hundred constants.
- **(c) A mix. This is what I recommend:**
  - **Small edits are switches in the shared source.**
    - **Constants:** `RV(us, pal, jp)`.
    - **Inserted or removed code:** `if (REGION_IS(PAL)) { … }`.
    - **Where they go:** the decomp's C (through `patches/spyro-1-port`, never in place) and our hand C (`src/game/`).
    - **Every PAL retune site is tagged as timing:** `RT(us, pal)`. That's what makes "PAL 60" (§7.4) cheap.
  - **Rewritten functions get a region version.** For example `func_8001A40C_pal`, JP's font and screens as `src/game/jp_*.c`, and the PAL title overlay as its own overlay source. The US function dispatches to it at its top.
  - **The region is fixed at start-up.** `g_Region` is set once, before the game's first instruction.

**How (c) is checked.**
- **Functions with decomp C** (all the overlays and 254 main-executable functions):
  - `tools/build_matching.sh` gets a `REGION=pal|jp` form. It compiles the decomp with the same patches and `REGION` fixed at compile time. Then `RV()` is a literal and `if (REGION_IS(…))` is a constant, which gcc 2.7.2 folds.
  - It runs `compare_code.py` against that region's disc.
  - **The target is "same" for every function a stage touches.** That's an instruction-level proof of the edit, modulo relocations and small constants. The constants are checked separately: the stage's diff lists every site, and a second masked pass with constants unmasked runs over the edited functions.
  - **Exceptions:** where gcc 2.7.2's register allocation doesn't reproduce, the function falls back to the next check and is listed in `docs/translation-log.md`.
  - The US matching build must stay SHA-1-exact, as now.
- **Our hand C** (pete, camera, update, cyclorama, moby helpers and the asm-only code):
  - **PCSX-Redux on that region's disc.** `tools/oracle/replay.sh` already replays per-function samples against translated C through a function map; `compare_code.py --json` gives the region's function map.
  - **Whole runs:** that region's attract demos and pad routes, compared between Redux and the host (§8).
- **US must not change.** `tools/host_checks.sh` (279 files) stays byte-identical on every region PR, as does the US matching build. A wrong `RV()` can only reach US through a typo, and these catch it.

### 6.2 Data: read from the player's executable, never committed

- **What PAL and JP need at run time** is data from their executables:
  - text, pointer tables and track tables;
  - the PAL header template;
  - JP's dragon-name slots.
- **What can't go in the repo:** the US port compiles the decomp's data, which upstream committed. **We can't add PAL or JP data** (CLAUDE.md: never commit extracted assets).
- **So each region gets a data map in the repo.** For each data block the port needs, it records the region's address, its size, the port symbol it fills, and which words are pointers into the executable. That's addresses, not content.
- **The flow at start-up:**
  1. Once the executable's SHA-1 has been checked (D60, §7.1), the offsets are known to be exact.
  2. A loader copies each block into the port's memory and relocates its pointers.
  3. It checks every mapped range against the executable's size.
- **Fonts and voices** stay where they are: the WAD (JP's font sheet, PAL's accent models, everyone's dragon audio) is read from the disc already.
- **The maps are written per stage**, from the disassembly's references. The first is PAL's string pool and tables (R1).

### 6.3 Timing: the platform side of 25 Hz

PAL's tick is 2 vblanks of a 50 Hz clock. The Vita's display runs at 60 Hz, and today the port ties the game's vblank to it.

| Part | Today | PAL needs |
|---|---|---|
| Vblank clock (`src/platform/common/clock.c`, `psx_clock_due`) | the display's vcount | its own 50 Hz count in real time, 5 for every 6 display vblanks; the presenter maps display vblanks onto it |
| CD model | `psx_cd_vblank` advances the drive each vblank, 2.5 sectors at double speed (D62) | the drive advances by real time: 3 sectors per 50 Hz vblank, XA kept fed |
| SPU | 735 samples a vblank (`spu_native.c:18`) | 882 a 50 Hz vblank, or samples by real time |
| Presenter (D126–D129) and due frames (D112) | an in-between frame between 30 Hz ticks, due at a display vblank | 25 Hz ticks shown at 60 fps: 2.4 display frames a tick, blended by time |
| 30 fps mode | one tick every two display vblanks | 25 Hz on 60 Hz can't be even: each tick shows for 2 or 3 vblanks. The presenter should stay on for PAL |
| 60 Hz logic pilot (D130) | opt-in, NTSC | out of scope for PAL and JP at first; its site macros and `RT()` touch the same files, so the stages coordinate |

JP is 60 Hz and needs none of this. It needs its 512×232 picture, the ordering-table size and its font in VRAM, and those already go through the port's own GPU path.

### 6.4 Picture

- **PAL:** 512×256 and JP 512×232 against US 512×240. The presentation (`docs/presentation.md`: the 16:9 widening, HUD narrowing, letterboxing) assumes 240 lines in places. Each region's stage measures where, and the 16:9 field of view is set per region.

## 7. The port's side

### 7.1 Which disc, and the retail check (D60)

- **Today:** `src/platform/common/cdimage.c` has one `EXE_NAME`, `EXE_SHA1` and `WAD_SHA1`, and refuses anything else. A non-US executable already gets a panel naming its serial and region from `SYSTEM.CNF`.
- **R0 replaces the constants with a region table.** Each region has its `SYSTEM.CNF` executable name, the executable's and the WAD's SHA-1, the WAD's header layout and the music names.
- **Detection:** read `SYSTEM.CNF`'s BOOT line, look the name up, hash the executable, and the region is the one whose hash matches.
- **Refusals:**
  - an unknown hash of a known name is refused as now, naming the edition (e.g. a JP SCPS-10085, until someone supplies its hashes);
  - a disc whose region isn't built yet gets "recognised, not supported yet" until its stages land.
- **The extracted-files layout** (`manifest.json`) gets the same table. `tools/extract_disc.py`'s `KNOWN` becomes per-region and verifies whichever region it finds.
- **The WAD check** (once per image, marker `save/disc-verified.txt`) keeps working; the marker gains the region.

### 7.2 Saves

- **Card files are named by product code** (`BASCUS-94228SPYRO`, `BESCES-01438SPYRO`, `BISCPS-10083SPYRO`). One card folder can hold all three without clashing, the way one real memory card could. Each game sees only its own file, so a PAL card is never read as a US one.
- **What isn't region-safe:**
  - the save states (D51);
  - the quick saves;
  - `disc-verified.txt`.

  These are snapshots of one region's memory.
- **Recommendation:**
  - `ux0:data/spyro/save/` stays the US folder, so nothing moves for current players;
  - PAL and JP get `save/pal/` and `save/jp/`, each with its own `card0/`, states and marker.
- **A shared card instead** is the owner's choice (§10, decision 4). It's faithful to a real card, but it mixes regions' states unless only the card is shared.

### 7.3 The port's own text

- **The menu panel's font** is vita-port-kit's hand-drawn 5×7 ASCII font (`third_party/vita-port-kit/vita/runtime/modmenu/vpk_menu_panel.c:10`), so a change goes into vita-port-kit, not here.
- **French, German, Spanish and Italian** need a few accented glyphs drawn the same way.
- **Japanese needs a font:**
  - kana could be drawn by hand at that size;
  - kanji can't, so a Japanese menu needs either kana only or a font file. That's a new dependency and a licence: the owner's decision (§10, decision 7).
- **The translations themselves** (about 40 menu rows, plus the disc panels and the README) would be drafted by us and reviewed by speakers.
- **Until then** the menu stays English, which is a reasonable first release.
- **The menu's language** follows the game's: PAL's index once the title has set it, Japanese for JP.

### 7.4 Options

- **PAL timing:** "PAL 50 Hz" (default) or "PAL 60". PAL 60 is PAL's code with every `RT()` site at its US value, at the US tick. It's PAL's text, voices and 256-line picture at NTSC speed. It isn't the original, but it's how many PAL players would like to play.
  - **Decided (D150, the owner, 2026-11-12):** PAL runs as PAL 60 only, with the 60 FPS row's three values (60 HZ, 60 Hz logic, the default) as on the US disc. PAL's own 50 Hz stays for developers (`--pal50`, `pal50=1`), for the checks against the PS1.
- **PAL's pause-menu screen position** is part of the original and stays, but it moves a 512×256 picture inside the Vita's frame. The stage decides whether it does anything visible or is kept as a no-op that still saves its value.
- **No language select for US or JP:** those discs have one language. PAL's language is chosen on its own title screen, as on the PS1.

### 7.5 LiveArea, README, bounty

**Done for PAL (R4, D154, 2026-11-13):** `region.c` marks SCES-01438 supported, so a player's PAL disc opens without `region_dev=1`; the README lists both releases with their executables' hashes and music names, PAL's language choice, its `save/pal/` folder and the known difference (PAL's animations ~20% fast at US timing, until that fix lands). The LiveArea needed no change. JP is still refused with "NOT SUPPORTED YET".

- **One VPK, title ID `SPYV00001`,** for every region. The LiveArea art (`assets/livearea/`, the owner's D59 exception) needs no change.
- **The README** lists the supported serials and hashes, the file names (`MUSIC*.STR`) and the editions not yet known.
- **The bounty** (zealouschuck.com, "Spyro 1 Decomp (Open Pete)" for the Vita) asks for the game on the Vita, and the US release meets it. Region support doesn't change the claim. It can be mentioned in it if it has landed by then.

## 8. Checks per region

| Check | US | PAL | JP |
|---|---|---|---|
| Matching build (`tools/build_matching.sh`) | SHA-1-exact, as now | `REGION=pal`: every touched function "same" by `compare_code.py` | `REGION=jp`: the same |
| Oracle (PCSX-Redux, `tools/oracle/`) | as now | **boots** with the current BIOS (SCPH-1001): PAL `main` (0x80015DCC) in RAM by vblank 1,200, checked 2026-11-10. **Still to check:** that Redux runs the disc at 50 Hz (its automatic video standard), and a full attract demo | **boots** the same way (JP `main` in RAM by 1,200) |
| Function replay (`replay.sh`) | as now | samples from PAL runs, the function map from `compare_code.py --json` | the same |
| Host whole runs | 279 files, byte-identical on every region PR | the attract demos and two pad routes, against Redux; the tour to both endings once per language (shots) | the attract demos, a route through the hint states, the tour |
| Vita3K | as now | a level in each language, music and dragon voices | a level, the font and the hint states |
| Hardware (the owner's yes, every run) | as now | R4 | J3 |

- **`tools/oracle/run.sh` hard-codes the US cue.** R0 gives it `DISC=` and `BIOS=`, as the host's scripts have.
- **A PAL or JP BIOS** is only needed if Redux won't run 50 Hz with SCPH-1001.

## 9. Stages

**Sizes** are *estimates*, in hand-translated PAL/JP instructions where that's the work.

**Every stage's PR runs:**
- the US host checks (279 files);
- the US matching build;
- `tools/check_no_assets.sh`.

**Every PR for a disc also** names the disc hashes it was checked on.

| Stage | What | Size | Checks | Risks | Depends on, parallel with |
|---|---|---|---|---|---|
| **R0** groundwork | Region table (§7.1) in `cdimage.c` and `extract_disc.py`; detection; the "recognised, not supported yet" panel; `g_Region`; save folders per region (§7.2); `DISC=`/`BIOS=` for the oracle; the oracle's 50 Hz check for PAL | 1 PR, ~400 lines | US unchanged; PAL and JP images detected and refused with the right panel (host, Vita3K); `tests/cd/image_variants.py` | hashes not Redump-confirmed (decision 6) | — |
| **R1** region plumbing, PAL data | `RV`/`RT`/`REGION_IS`; the `REGION=pal` matching build; the data-map loader (§6.2); PAL's WAD header layout, `LevelHeader` 656, overlay base 0x80081428; PAL's string pool, tables and track table mapped | 1–2 PRs | loader self-test against the PAL exe; `REGION=pal` builds and `compare_code.py` reports the untouched functions "same"; US unchanged | the port's data labels are compiled globals (stage 4, D106): a PAL block that isn't the US block's shape needs its own symbol | R0; **J0 in parallel** |
| **R2** PAL language and picture (part A, D134: headers, picture, shared fixes; part B, D139: the text, menus, title, card and cutscene voices; part C, D145: the overlays' dialogue) | The merged letter spawner and accents (classes 365–369, models from the PAL WAD); text and menu rewrites (§3.3); the title overlay with its language select; the cutscene and dragon slots by language; the 256-line picture (§3.2, §6.4); the pause screen-position option; the card header (§3.6, with the icon-palette and block-count check); the shared fixes (§3.5); the level-warp cheat gone | 2–3 PRs; ~6,000 PAL instructions to translate (rewrites); ~50 functions | `compare_code.py` "same" per function; Redux replay; host shots of each language's title, pause and inventory screens, against Redux | the title overlay and `func_8001A40C` are large rewrites; the presentation's 240-line assumptions | R1; **R3 in parallel** (different files: draw, title, cutscene against pete, mobys, overlays' `UpdateMoby`) |
| **R3** PAL timing | Every retune site as `RT(us, pal)`: Spyro (~250), the moby helpers, all 37 overlays (about 3,700 constant sites plus inserted sequences); the 50 Hz clock, the CD model and SPU by real time, the presenter at 25→60 (§6.3); PAL 60 (§7.4) | 2–3 PRs. A script aligns US and PAL per function and proposes the edits, mapped to source lines by recompiling; each is checked by the `REGION=pal` build | every overlay function "same" with `REGION=pal`; Redux traces of Spyro's speed and a moby's path, PAL against the host at 25 Hz; PAL 60 equals US timing on the shared paths | the site count; sites shared with the 60 Hz pilot's macros (D130); the presenter's uneven 2.4 frames a tick | R1; R2 in parallel |
| **R4** PAL whole game (D147, D149; PAL supported for players, D154) | Attract demos and routes against Redux; the tour to both endings in each language; Vita3K; then hardware (the owner's yes) | 1–2 PRs | §8's PAL column | Redux at 50 Hz (R0) | R2, R3 |
| **J0** JP sizing (read-only) | Measure the overlays properly (LEVEL_13, 20, 21 included); watch states 16 and 17, the fairy talk and the field-of-view setting in Redux; the save layout; the struct deltas; refine J1–J3 | 1 PR (docs) | — | — | R0; in parallel with R1–R3 |
| **J1** JP data and font | The JP data map; the font sheet in VRAM and the glyph emitter; the text helpers; 512×232, the 0x600-entry ordering table; the mod-chip check passed over; the WAD layout (entries 2, 94) | 1–2 PRs, ~2,000 instructions | shots of JP text against Redux; `REGION=jp` "same" per function | the struct layouts (§4.2): a JP field can't be added to a struct the US matching build needs, so `#if` in the patched headers for the matching builds and a superset in the port | R1, J0 |
| **J2** JP code | The ~13,000 new main-executable instructions (camera, screens, states 16 and 17, fairy, saves, input) and the overlays' changes | 4–8 PRs; the largest stage | as R2 | size uncertain until J0; hand C's checks are the oracle's only | J1; parallel inside by area (camera, screens, states) |
| **J3** JP whole game | as R4 for JP | 1–2 PRs | §8's JP column | — | J2 |
| **P** the port's text | Accented glyphs in vita-port-kit's font; menu translations; a Japanese menu if decided; README and release notes | 1–2 PRs | Vita3K shots | a font licence (decision 7) | R0; any time |

**The order I recommend:** R0 → R1 (with J0 alongside) → R2 ∥ R3 → R4 is a PAL release. JP's J1 → J2 → J3 come after, or alongside R2–R4 if the owner wants both at once. In that case a JP writer and a PAL writer never touch the same files.

## 10. Decisions for the owner

1. **The approach:** (c), a mix (§6.1): region switches in the shared source, region versions of rewritten functions, data from the disc, and per-region matching builds as the check. Or (a) or (b).
2. **PAL timing:** PAL's own 25 Hz by default, with "PAL 60" as an option (my recommendation). Or 25 Hz only, or PAL 60 only.
3. **Order:** PAL first, then JP (recommended: PAL is about 96% US code; JP's size is unknown until J0). Or both at once, or JP first.
4. **Saves:** per-region folders `save/pal/` and `save/jp/`, with `save/` staying US (recommended). Or one shared card folder with per-region states.
5. **Data from the disc** (§6.2): PAL and JP text and tables read from the player's executable through committed address maps, never in the repo or the VPK. Confirm.
6. **The hashes:** please check these images against redump.org (unreachable from here) before R0 hard-codes them:
   - PAL image `52bba2f6…`;
   - JP image `e89da928…`;
   - or the Redump track SHA-1s.

   Do you want other editions (JP standard SCPS-10085, SCPS-45395, PAL Platinum) supported as players send hashes, or only these two?
7. **The port's menu in other languages:** English only at first (recommended), then FR/DE/ES/IT with hand-drawn accents. For Japanese: kana-only hand-drawn glyphs, or a font file (a new dependency and licence).
8. **Hardware:** R4 and J3 each need your yes for their Vita runs, as always.

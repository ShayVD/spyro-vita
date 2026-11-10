# RetroAchievements in Spyro Vita: research and a staged plan (D140)

Written 2026-11-10 on main `8f96a63`. This document changes no engine code. It adds one small tool, `tools/ra/ra_addrs.py` (§3.2). The owner decides the open questions in §7 before any integration work starts. Each stage in §6 is its own PR, and the owner merges every one.

**Where the facts come from** (all read on 2026-11-10):
- **RA's own documents:** the `RetroAchievements/docs` repository at `5fa3d58` (2026-11-06), the source of docs.retroachievements.org, and `RetroAchievements/api-docs` at `9d0aeb7`. Pages are cited by their path in those repositories.
- **RA's server code:** `RetroAchievements/RAWeb` at `0b7de56` (2026-11-10). This shows what the server *does*, which the documents don't always say.
- **rcheevos:** `RetroAchievements/rcheevos` at `eea6461` (2026-11-05, v12.5.0).
- **RA's public Connect API, without logging in:** `dorequest.php?r=gameid` (hash → game) and `r=codenotes2` (an address's notes). The requests were made with the user agent `spyro-vita-research/0.1 (planning; no client)`, never an emulator's. The responses aren't committed.
- **Not available without a login:** the set's logic (`r=achievementsets` needs a Connect token; the website shows logic only to RA developers, `RAWeb app/Policies/AchievementPolicy.php` `viewLogic`). The website's pages sit behind a bot check, so they were not read either. §3 therefore works from the code notes, and stage RA-1 gets the exact list.

**Nothing of RA's or the game's is committed:** no logic, no code-note text, no badges, no disc data.

## Summary

- **Policy: unlocks from Spyro Vita would break RA's rules as written today.**
  - RA's player rules say plainly: "Unlocking achievements using methods outside hardcore-compliant emulators results in having your account set as untracked", with "no exceptions and no appeals" (`docs/guidelines/users/global-leaderboard-and-achievement-hunting-rules.md`, §Untracked).
  - Spyro Vita is not an emulator, and RA lists no path for a native port to use a console's set. Its one page about non-emulator clients excludes us by name: "Decompilations, recompilations, and other unofficial ports are not eligible for standalone sets" (`docs/general/standalone-support.md`).
  - On the server, a client with an unrecognised user agent is `ClientSupportLevel::Unknown`. Its hardcore unlocks are quietly turned into casual ones, the game's list gets a "Warning: Unknown Emulator" entry, and every unlock is logged as an `unknown_client` warning (§1.2).
  - **So:** nothing that submits unlocks to RA ships without RA's written yes. The owner asks RA's admins (the site user `RAdmin`); there's a draft message in Appendix A. If they say no, the fallback is local achievements of our own that never touch RA (§1.4).
- **Game identity: confirmed.** rcheevos hashes a PS1 disc as MD5(boot executable name + the executable's first `t_size + 2048` bytes) (`rcheevos src/hash/rc_hash_disc.c` `rc_hash_psx`). From the owner's US image this is `3e9eb765a7b2912e60c30f363f0a3574`. RA's public `r=gameid` maps that hash to game **11279**, Spyro the Dragon. The PAL disc's hash maps to the **same game**. The Japanese disc maps to its own game, **6800**.
- **Memory: possible, with three special cases and one real risk.**
  - The set's 997 code notes all land somewhere the port can serve. 989 are in the data labels or the heap block, at their PS1 offsets.
  - The special cases:
    - five notes in kernel RAM: the BIOS's copy of the boot path, which the set uses to tell US from PAL;
    - two in the executable's own image: the `"DRAGON X"` string at 0x80010000, the other region marker;
    - one PsyQ variable the port keeps elsewhere: libetc's vblank counter.
  - **The risk:** in the native-memory build every pointer stored in game RAM is a host address. The set follows pointers (`g_LevelMobys` and others, through AddAddress), so the read callback translates pointer values back to PS1 addresses.
  - Each piece has its own check (§3.4), against the arena build and against PCSX-Redux.
- **Library:** rcheevos is MIT. It would be pinned as a submodule, which needs the owner's approval. The HTTP stack is the open question: does the Vita's own `SceHttp`/`SceSsl` accept retroachievements.org's ECDSA certificate chain? If not, libcurl and VitaSDK's OpenSSL, with a pinned CA file. Login is by token. The password goes once to RA's `login2` and is never stored.
- **Frames:** RA evaluates once per *PS1 vblank*, not once per logic tick. Emulators do this, and the set's timers count those frames (§5.3).

## 1. Policy

### 1.1 What RA's documents say

- **Hardcore** is "approved" only for emulators that meet the Hardcore Compliance Requirements (`docs/general/hardcore-compliance-requirements.md`). That page's rules (A–H) are written for emulators:
  - a unique user agent;
  - at least 6 months of public availability (D);
  - no state loading, cheats, rewind, slow-motion or frame advance in hardcore (B, G);
  - switching to hardcore resets the game (B);
  - an achievement list in the client, with Rich Presence and leaderboards working;
  - offline unlocks cached and synced (A);
  - a licences page and a privacy policy (F).
  
  Approved emulators are listed on the Downloads page. Nothing on the page covers ports or recompilations.
- **The player rules** (`docs/guidelines/users/global-leaderboard-and-achievement-hunting-rules.md`):
  - "Not allowed 2" says only hardcore-compliant emulators may be used for hardcore.
  - The §Untracked warning, quoted in the summary, isn't limited to hardcore. Read as written, a casual unlock from Spyro Vita would also put the player's account at risk of being untracked.
- **Standalone support** (`docs/general/standalone-support.md`) is RA's route for software that isn't an emulator, such as games with their own sets. It needs:
  - an RA developer as the proposer;
  - a set designed for the game;
  - hardcore enforcement;
  - the admins' approval.
  
  Decomps, recomps and unofficial ports are explicitly not eligible. That page is about *new* sets for such software. Our case, an existing PS1 set played through a port, isn't addressed anywhere. The nearest rule, the one above, points to "no".
- **The Connect API** (`api-docs docs/connect/standalone.md`) is described as "private and locked down, used by emulators". Clients must send a user agent in the form `{Name}/{x.y.z} ({platform}) {Integration/x.y.z}`.

### 1.2 What the server does with an unknown client

From RAWeb's code:
- **Classification:** `app/Platform/Services/UserAgentService.php` `getSupportLevelAndCoreRestriction()` returns `ClientSupportLevel::Unknown` for a user agent that matches no registered emulator (`app/Enums/ClientSupportLevel.php`).
- **Hardcore unlocks:** `app/Connect/Actions/AwardAchievementAction.php` sets `$this->hardcore = false` when `!allowsHardcoreUnlocks()`. Only `Full`, `Warned` and `CasualPending` allow hardcore, so a hardcore unlock from an unknown client is stored as casual.
- **Warnings:** `app/Connect/Support/GeneratesConnectWarnings.php` records an `unknown_client` warning for each request.
- **Load-time warning:** `app/Connect/Actions/GetAchievementSetsAction.php` `injectClientSupportWarning()` adds a warning achievement, "Warning: Unknown Emulator" ("Hardcore unlocks cannot be earned using this emulator"), to the set that the client loads.
- **Blocking:** a `Blocked` client is refused outright. An unknown one isn't.

So the server would technically accept casual unlocks from Spyro Vita, and RA would see each one flagged. The rules decide what happens to the account, not the server. **We don't ship against "technically accepted"**.

### 1.3 What approval would take, and whom to ask

- **Whom:** the admins, through the site account [RAdmin](https://retroachievements.org/user/RAdmin). Both the standalone page and the untracked rules name it. The owner sends the message from their own account; Appendix A is a draft.
- **What to ask:**
  1. Can a decomp-based native port send unlocks for the existing PS1 set (game 11279), in casual mode, hardcore mode, or neither?
  2. If casual only: will RA register a user agent for it (`SpyroVita`), so its unlocks stop being "unknown"? Will players be safe from the untracked rule?
  3. If hardcore is possible at all: which of the compliance requirements apply to a port, and does the 6-month clock start at v1.0 (2026-10-31, so 2027-04-30)?
  4. Do the set's developers want anything from us? For example, a way to tell port unlocks apart in tickets, beyond the user agent.
- **What we'd offer them:**
  - the source (it's public);
  - the memory-read design and its checks (§3);
  - the hardcore rules we'd enforce (§5.1);
  - a unique user agent;
  - no emulator identity anywhere.
- **Our best guess at the answer** (an *estimate*, not RA's word): a no, or casual only. Two reasons:
  - The standalone page names decomps.
  - RA's ticket process relies on developers reproducing issues in emulators with RAIntegration, which a port can't offer.

### 1.4 What's possible without RA's yes

- **(a) Local achievements of our own: no RA at all.** We'd write a short list of our own, in C, from the game's state: the gem, egg and dragon totals, the bosses, the flight levels, 120%.
  - It's shown and stored on the Vita, in `ux0:data/spyro/save/achievements.bin`.
  - It doesn't copy RA's set or its wording. RA's sets are their contributors' work, and `docs/guidelines/content/set-plagiarism.md` cares about that.
  - It needs no network, no new dependency and no memory mapping, because it reads the globals by name.
  - About one PR. It's the fallback if RA says no, and it can ship either way.
- **(b) RA's set, evaluated locally, never submitted.** This still downloads the set through the Connect API with a player's token, which §1.1 calls private. It's an RA question too, so it isn't proposed without their yes.
- **Not done in any case:** sending another emulator's user agent, hiding the client, or submitting "offline" through a third-party tool. The server also treats offline-submission clients as casual-only (`UserAgentService::hasOfflineSubmissionClient`).

## 2. Game identity

- **The algorithm** (`rcheevos src/hash/rc_hash_disc.c`, `rc_hash_find_playstation_executable`, `rc_hash_psx`):
  - Read `SYSTEM.CNF`, take `BOOT =`, strip `cdrom:` and the backslashes, and stop at `;`. That gives `SCUS_942.28`.
  - Hash MD5(`"SCUS_942.28"` + the executable's first `t_size + 2048` bytes), where `t_size` is the header's word at 28.
  - Without a `SYSTEM.CNF` it uses `PSX.EXE`.
- **Our inputs** (the owner's images in `disc/`, the extracted executables; computed with a 6-line Python version of the same steps):

  | Disc | Boot file | `t_size` | RA hash | `r=gameid` |
  |---|---|---|---|---|
  | NTSC-U SCUS-94228 | `SCUS_942.28` | 0x65800 | `3e9eb765a7b2912e60c30f363f0a3574` | **11279** |
  | PAL SCES-01438 | `SCES_014.38` | 0x6C000 | `c34d0d04488fa2ea7f1abf1d4c4e92e8` | **11279** (same game) |
  | Japan SCPS-10083 | `SCPS_100.83` | — | `920f473f3a654af8a7d1970a4cc215af` | **6800** |

- **The US executable** is byte-identical to the decomp's `PSX.EXE` (SHA-1 `84e3728a…`, `CLAUDE.md`, D60's retail check). The hash only depends on that file and its name. So every path the port supports gives the same hash: the `.bin`/`.cue` read directly (D60) and the extracted files with `manifest.json`. RA recognising it as game 11279 is the confirmation.
  - redump.org wasn't reachable for the regions plan either (D131), so "the Redump disc" rests on that recognition and on the executable's SHA-1.
- **In the port:** the hash is computed once at start-up from the executable the region code already reads (`src/platform/common/region.c`, D132). It's a few lines over the MD5 that rcheevos ships (`src/util/md5.c`). A host test checks it against `rc_hash_generate` on the `.cue`. Using rcheevos' CD reader on the Vita would only duplicate our own `libcd`.
- **Japanese discs** get their own set (6800, 148 code notes). **PAL discs** share the US set: the set's own logic tells the two apart (§3.1).

## 3. Memory

### 3.1 What the set reads

RA's PS1 memory map (`rcheevos src/runtime/rc_consoleinfo.c`):
- `0x000000–0x00FFFF`: kernel RAM;
- `0x010000–0x1FFFFF`: RAM;
- `0x200000–0x2003FF`: scratchpad.

That's PS1 0x80000000 + *address*, and 0x1F800000 for the scratchpad.

The set's logic can't be read without a login (see "Where the facts come from"). The public code notes for game 11279 are the best stand-in: 997 addresses that the set's developers wrote notes for. `tools/ra/ra_addrs.py` sorts them against the matching build's `psx.map`:

| Class | Notes | Where it is in the port (native memory, the Vita's default, D106) |
|---|---|---|
| labels: 0x06BBE0–0x07AA37 | 768 | `s1_labels + (a − 0x8006BBE0)`: the data image in PS1 order (`cmake/game.cmake`, `tools/data2s.py --image`) |
| block: 0x07AA38–0x1FFFFF | 221 | `g_psx_arena_base + (a − 0x8007A000)` (`src/platform/common/psx_arena.h`). Level data and overlay space are at their PS1 offsets. |
| kernel: 0x00B8B0–0x00B8C3 | 5 | **not there:** the BIOS's copy of the boot path, `cdrom:\SCUS_942.28;1`. The notes' 0x00B8C0 values tell J 1.0, J 1.1, US and EU apart. |
| executable: 0x010000, 0x066FA9 | 2 | **not there:** rodata starts with `"DRAGON X"` (US) or `"COMPLETO"` (PAL), another region check. 0x066FA9 is the Crash Bandicoot 3 demo's level byte; that demo is another executable, which the port doesn't run (D118's notes on `crash_demo.c`). |
| PsyQ data: 0x0749E0 | 1 | **not there:** libetc's vblank counter (`D_800749E0`, read by `VSync`, `asm/psyq.s` 0x8005DC04). The port's is `psx_clock_count()` (`src/platform/common/libetc.c`). |
| Holes in the labels image | 0 | C-defined or PsyQ objects that `data2s.py` leaves as zeros in the labels: strings, `CheatTable`, `draw.c`'s sdata, PsyQ data/sbss/bss. No note falls in one, but the callback still reports them. |
| scratchpad | 0 | `g_psx_scratch` |

- **Pointers.** Eight notes describe pointers:
  - `g_LevelMobys` 0x075828, `g_MobyAllocPtr` 0x07573C, `g_PropsAllocPtr` 0x075930, `g_MobyPods` 0x0757F8, `g_EnvironmentAnimations+0x1C`, one in level data at 0x17C600, and two PAL ones.
  
  The set reads them as 24-bit values and adds them to an offset (AddAddress). **In the native build these words hold host addresses**, so the set would follow them into nothing. In the "moved" arena build (16 MB-aligned, D97) their low 24 bits are already the PS1 address.
- **PAL.** The notes also list PAL addresses, e.g. "Level Mobys (EU)" at 0x07C0FC. The set is one set for both discs, with region-specific groups. Those groups are keyed on the two markers above, so the port has to serve the markers exactly.

### 3.2 The read callback

`rc_client` reads through `uint32_t read(uint32_t address, uint8_t *buf, uint32_t n, rc_client_t *)` (`include/rc_client.h`, `rc_client_set_read_memory_function`). Ours, `ra_mem_read` in a new `src/game/ra_mem.c`, resolves each byte:

1. **0x000000–0x00FFFF:** the 20 bytes at 0xB8B0 come from a constant buffer built from `SYSTEM.CNF`'s `BOOT` line at start-up, exactly what the BIOS copies. Everything else comes from `g_psx_kernel_ram`. Nothing is written into the game's memory, so save-state hashes don't change.
2. **0x010000–0x06BBDF:** the player's executable as loaded, kept read-only. The region code already reads the executable (`src/game/region_data.c`), and this is its `t_size` bytes, 406 KB. On the PS1 this range is code and rodata, which never change.
3. **The labels and the block:** `psx_mem_host()` (`src/game/port.c`), which already does exactly this mapping for the host's `--peek`.
4. **A short table of PsyQ objects:** 0x800749E0 → `psx_clock_count()`; more if RA-1's exact list finds others.
5. **The scratchpad:** `g_psx_scratch`. Anything else reads 0, as an emulator reads unmapped memory.
6. **Pointer translation (native build only):**
   - For every byte of the request, take the aligned word that holds it.
   - If that word's value lies in `[s1_labels, s1_labels_end)` or in the heap block, rewrite it to the PS1 address it stands for: `0x8006BBE0 + (v − s1_labels)` or `0x8007A000 + (v − g_psx_arena_base)`. Then return the requested bytes of the rewritten word.
   - This covers 8-, 16-, 24- and 32-bit reads of a pointer, aligned or not.
   - Host addresses that aren't PS1 memory, such as code or rodata pointers, are left as they are; no condition can use them.
   - **The risk is a false positive:** a non-pointer word that happens to fall in those host ranges. The check below measures it on the host. On the Vita the ranges sit at 0x81xxxxxx (`build/vita/spyro.map`: `s1_labels` 0x81385BE0), a range game data rarely holds. Measured, not assumed: §3.4.
   - If it fails, there are two fallbacks. One is a per-address list of pointer words (the D97 inventory names every decode site). The other: RA mode is offered only in the moved-arena build, where no translation is needed.
7. **PAL** (stage RA-7): the PAL build keeps the US label layout (D134). A PAL address from the set is therefore translated first through PAL's matching build map (`tools/build_matching.sh REGION=pal`): PAL address → symbol → US address. Then steps 1–6 apply. The markers in steps 1 and 2 come from the PAL executable. **JP** (set 6800) works the same way, after the J stages.

`tools/ra/ra_addrs.py` takes either the code notes or the set's patch data (the response of `r=achievementsets`). From the patch data it reads every operand (`0xH…`, `0xX…`, `fF…`) and marks offsets after an `I:` as relative. It prints one line per address with its class and nearest symbol. It's how RA-1 turns the real set into the list the checks use.

### 3.3 Values, not just places

- **The same place doesn't guarantee the same value.** Places beyond pointers where the port's value could differ from the PS1's:
  - fields the port changed (the 60 Hz rules keep every value in the original's units, D130, but in-between values differ);
  - the mips2c stack;
  - anything the set reads mid-transition.
- **The comparison in §3.4 is the real test.**

### 3.4 How it's checked

- **Translation, exactly** (host, no RA, no network):
  - Run the same route on the host's native build and on its arena build. On linux32 the arena build's PS1 addresses *are* host addresses (`arena.c`), so its pointers hold real PS1 values.
  - Dump `ra_mem_read` over the whole 2 MB plus kernel RAM and the scratchpad every N vblanks on both. The dumps must be byte-identical.
  - This proves steps 1–6. It also counts false positives directly: a word the translation rewrote that the arena build doesn't have.
  - Routes: `tools/host_checks.sh`'s gems script, route A/B, the whole tour.
- **Values against the PS1** (oracle):
  - `tools/oracle/oracle.lua` gets `ORACLE_PEEK=file`, a list of addresses and sizes, and writes them every vblank.
  - The host gets `--ra-peek FILE` through `ra_mem_read`.
  - Both run the same pad scripts (`tools/oracle/pads/`). A new `tools/oracle/peek_pairs.py` pairs the runs per (gamestate, level) segment, as `route_pairs.py` does, and compares the values:
    - counters and flags must agree at segment ends;
    - per-vblank values must agree within the known timing drift.
  - The address list is RA-1's exact one; until then, the 997 code-note addresses.
- **On the Vita:** the same `--ra-peek` dump through `config.txt` (`ra_peek=`), compared with the host's. Vita3K first, hardware with the owner's yes.

## 4. The library and the platform side

### 4.1 rcheevos and `rc_client`

- **Licence:** MIT (`rcheevos/LICENSE`, "Copyright (c) 2018 RetroAchievements.org"), the same as ours.
- **How it would come in:** a submodule pinned at a release tag, `third_party/rcheevos` at v12.5.0. Any change goes through `patches/rcheevos/`, applied by CMake as with our other submodules; none is expected. **Not vendored until the owner approves (§7).**
- **What's built:** `src/client`, `src/api`, `src/runtime` and `src/util`. `src/hash` is built only for the host test of §2. It's compiled with `RC_NO_THREADS`, because every `rc_client` call happens on the game thread (§4.2). rcheevos already has a Vita mutex (`src/util/rc_compat.c`, `#elif defined(VITA)`) if that ever changes.
- **Calls we use:**
  - `rc_client_create(ra_mem_read, ra_server_call)`;
  - `rc_client_set_host` (tests only), `rc_client_set_event_handler`, `rc_client_set_hardcore_enabled`;
  - `rc_client_begin_login_with_token`;
  - `rc_client_begin_login_with_password` (first login only);
  - `rc_client_begin_load_game(hash)`;
  - `rc_client_do_frame`, `rc_client_idle`, `rc_client_reset`;
  - `rc_client_serialize_progress_sized` / `rc_client_deserialize_progress_sized`;
  - `rc_client_create_achievement_list`.
- **The user agent:** `SpyroVita/<version> (PSVita <fw>) ` followed by `rc_client_get_user_agent_clause()` (rcheevos/12.5.0). The host's is `SpyroVita/<version> (Linux i686; debug)`, used only against the fake server. It's never another client's.

### 4.2 Threads and the frame point

- **Evaluation:** happens at the main loop's top, in `PortPresentBeforeUpdate()` (`src/game/present.c`). That's where `savestate_point()` runs, before the update, on the game thread (§5.3 says how many frames).
- **HTTP:**
  - Requests run on one worker thread.
  - Responses are queued and handed to `rc_client`'s callbacks at the next frame point, or at the menu's own loop while the menu is open. That loop also calls `rc_client_idle` for the pings.
  - The presenter thread never touches RA.

### 4.3 HTTP on the Vita

- **Today:** the Vita build has no networking (`src/platform/vita/CMakeLists.txt` links no `SceNet`/`SceHttp`/`SceSsl`). VitaSDK has the stubs for all three, and OpenSSL's `libssl`/`libcrypto`, but not libcurl (vdpm has it).
- **The certificate chain:** retroachievements.org serves an ECDSA leaf (Google Trust Services WE1) under GTS Root R4. R4 is cross-signed by GlobalSign Root CA (1998, RSA), and the session negotiates TLS 1.2 or 1.3 (`openssl s_client`, 2026-11-10).
- **Option 1:** `SceHttp` + `SceSsl`, the firmware's own stack. No new third-party code. The question is whether the firmware verifies that chain; old firmware stores often can't. A hardware spike (RA-4a) answers it. **Turning verification off is not an option:** the token is a password-equivalent (`api-docs`: "Treat both your Web API key and Connect API token as secrets akin to your password").
- **Option 2:** libcurl (curl licence, MIT-style) over VitaSDK's OpenSSL (Apache 2.0), with a CA file in the VPK. That file holds the two roots RA needs, or Mozilla's bundle (MPL 2.0). It's two new dependencies plus a data file, so it needs the owner's approval.
- **Both options** need `sceNetInit` with a memory pool, `sceNetCtlInit` and `SCE_SYSMODULE_NET`/`HTTPS`, plus a "no network" path that leaves the game untouched.

### 4.4 Login and the token

- **First login:**
  - The menu's LOG IN row opens the Vita's IME dialog (`SceIme` is already linked) for the user name, then the password.
  - The password goes to `rc_client_begin_login_with_password`, which sends RA's `login2`. It's then zeroed in memory and never written anywhere.
  - RA's response holds a token.
- **Where the token lives:** `ux0:data/spyro/retroachievements.txt` (`user=`, `token=`). It's a separate file, not `config.txt`, because people share their config in bug reports. The log never prints it: `rc_client`'s log callback redacts `t=` and `p=`.
- **Later starts** use `rc_client_begin_login_with_token`. The LOG OUT row deletes the file.
- **The host** reads `--ra-token-file` and only ever talks to the fake server.

### 4.5 Menu rows

There's a new section, `MENU_ACHIEVEMENTS`, between GAME and SAVE (`src/platform/common/menu_opts.h`, D45). Its rows come from one list, `g_menu_achievements`:

| Row | Values | Notes |
|---|---|---|
| RETROACHIEVEMENTS | OFF / ON | off by default. Off means no network at all. |
| ACCOUNT | LOG IN / `<user>`: LOG OUT | the IME flow above. Shows OFFLINE or LOGIN FAILED. |
| HARDCORE | OFF / ON | Turning it on asks to restart the game, with the D136 question (`vita_menu_ask`), because RA requires a reset. Turning it off works at once. |
| NOTIFICATIONS | ALL / UNLOCKS / OFF | popups only. Leaderboards and rich presence keep running (B). |
| ACHIEVEMENTS | press ✕ | the list: locked or unlocked, progress (Measured) and points. RA requires it (A). |

- **Labels:** the font has A–Z, 0–9 and a few signs (D45). Titles from RA are upper-cased, and any glyph we don't have becomes a space; the font gets `' ! &` added.
- **Badges:** they need a PNG decoder, a new dependency. They're left for later; until then the list shows text only.

### 4.6 Popups

- **What's shown:**
  - "UNLOCKED: <TITLE> (<points>)";
  - "HARDCORE ON" / "CASUAL" at game load (E);
  - "OFFLINE: ACHIEVEMENTS PAUSED";
  - leaderboard start, fail and submit, and challenge/progress indicators (A).
- **How they're drawn:** as an overlay with `menu_panel`'s font after the game's frame, so on both the real frame and the presenter's in-between frame. Each lasts about 4 s, from a small queue the game thread fills from `rc_client`'s events. The presenter only reads a copied snapshot of the queue.
- **Sound:** none at first. A sound would go through our SPU mixer and needs a sample we can ship; RA's sounds are RA's.

## 5. Interactions

### 5.1 Hardcore: what it turns off

Compliance B and G, applied to what the port has (the Vita's `config.txt` lines from `src/platform/vita/main.c` `read_config`, the menu rows from `menu_game.c` and `savestate_vita.c`):

- **LOAD STATE** is refused: the row says OFF IN HARDCORE. **SAVE STATE** still works (B allows it).
- **The menu's cheats** (99 LIVES, LEVELS OPEN, CREDITS, WARP TO, LEVEL WARP; `port_cheat_request`) are hidden through the rows' `visible` hook and refused in `port_cheat_request` itself.
  - The game's own button codes (`CheatTable`) are game content. Emulators leave those to the set's logic.
- **Test hooks:** while any test hook is set, RA won't start in hardcore. These are recorded input, memory pokes and pacing changes:
  - `pad`, `poke`, `menu_test`, `menu_keys`, `state_test`, `bench`, `vcount_test`;
  - `realtime` other than the default.
  
  The log says which hook it was.
- **60 Hz logic** (`interp=4`, D130) changes the game's rules: half steps in play. **Proposed:** hardcore only with 30 Hz logic. The 30 fps and interpolated modes, widescreen and draw distance are presentation; compliance B exempts that kind of quality-of-life change ("widescreen hacks").
  - Draw distance gets a check that it never changes which Mobys update, and if it does, it's pinned in hardcore.
  - Casual allows all of it.
- **The order on the way in:** HARDCORE on → the restart question → the game resets → `rc_client_set_hardcore_enabled(1)` → `rc_client_reset`. Off → `rc_client_set_hardcore_enabled(0)`, no reset (B).
- **The host debug build** never talks to the real server. Its `ra_server_call` is the fake server (§5.6) and nothing else, so neither hardcore nor casual can reach RA from it.

### 5.2 Save states (D51)

- **A new chunk:** a save state gets an `RACH` chunk with `rc_client_serialize_progress_sized`, the hit counts. RA recommends it (A).
- **Loading in casual:** `rc_client_deserialize_progress_sized`, then RA carries on.
- **Loading in hardcore:** refused (above).
- **Not-RA states:** a state made with RA off, or for another game, loads with `rc_client_reset`'s fresh counts.
- **Checks:** the `savestate_globals` check (`tools/savestate_globals.py`) lists the new port globals as SETTINGS or SAVED.

### 5.3 What a "frame" is

- **What an emulator does:** it calls `rc_client_do_frame` once per emulated video frame. That's 60 a second for NTSC and 50 for PAL, whatever the game's own rate. Spyro updates every 2 to 4 vblanks (`main.c`'s clamp of `g_DeltaTime`), so the set's hit counts and timers are written in vblanks. The code notes even name a "Level Timer 60fps" (0x0758C8) beside a 30 Hz one.
- **The rule for the port:** at each frame point, call `rc_client_do_frame` once for every vblank since the last frame point. `psx_clock_count()` is the PS1-equivalent vblank count the port already keeps for `g_UnprocessedFrames`.
  - At 30 Hz logic that's usually twice per update.
  - At 60 Hz logic (`interp=4`), once per update in play.
  - On PAL, the 50 Hz count.
  - When the loop runs twice in one vblank during loads (D51's notes), it's zero.
- **Why it matches the emulator:** the extra calls see unchanged memory, just as an emulator's in-between vblanks see a game that's waiting for its draw. Deltas and hit counts come out as in an emulator.
- **What "once per 30 Hz logic tick" would do:** halve every frame-counted timer in the set. That's why this plan doesn't use it, although the brief suggested it.
- **The oracle comparison (§3.4) checks this too:** the runs are paired by vblank.

### 5.4 PAL and Japanese discs

- **PAL** shares game 11279. It needs the PAL address translation (§3.2 step 7) and the PAL markers. PAL itself is still refused for players until R4 (D134), so RA-7 follows R4.
- **JP** is game 6800, a separate set. It comes after the J stages, with JP's map.
- **The design:** the region table (`src/platform/common/region.c`) gets a per-region `ra` entry: the boot path, and the matching map to translate through. A region without one runs with RA off and says so.

### 5.5 Offline

- **During a session:** `rc_client` retries failed unlock requests by itself while the game runs. The popup says OFFLINE and the list marks the unlock as pending.
- **Across restarts:** RA wants unlocks cached and synced (A, "securely cached"). That needs a queue file whose format RA accepts. RA's submissions carry an offset in seconds (`o=`, checked by the server's validation hash in `AwardAchievementAction`). **Proposed:** in-session retries only in the first stages, and a persistent queue only if RA approves the client and asks for it.
- **No network at start:** the game runs normally, and RA retries the login from the menu.

### 5.6 The host debug build

- **The fake server:** `ra_server_call` on the host serves canned JSON from `tests/ra/fake/` for login, the game ID, the patch, start session, awards and pings. Those files are **our own synthetic set**: a handful of achievements on the gem total, the level ID and one pointer chain. RA's data is never committed.
- **What ctest runs on it:**
  - log in by token;
  - load by hash;
  - unlock on the gems script;
  - the hardcore gates (state load refused, cheats refused, test hooks refused);
  - a save/load round trip of the `RACH` chunk;
  - the frame rule (`--tick60` and 30 Hz give the same unlock vblank for a frame-counted trigger).
- **The RA-1 run:** the owner's real patch file (kept in `logs/`) can be loaded into the same fake server. The host then evaluates the real set on our routes, offline and with nothing submitted, and reports which achievements trigger where.

## 6. Stages

Each stage is one PR. None of them runs on hardware without the owner's yes in that session; kh1 shares the Vita (`~/.cache/vita-port-kit/hw.lock`).

| Stage | What | Size | Tested by | Needs |
|---|---|---|---|---|
| **RA-0** (this PR) | this plan, D140, `tools/ra/ra_addrs.py` | docs + 1 tool | the tool on the public code notes (§3.1's table) | — |
| **RA-1** owner | Ask RA (Appendix A). Fetch the set's patch data with the owner's own login into `logs/ra/` (not committed). Run `ra_addrs.py` on it and commit the address list (addresses, classes, symbols: no logic, no notes). | owner + 1 small PR | — | the owner's account |
| **RA-2** memory | `src/game/ra_mem.c` (§3.2) without rcheevos. Host `--ra-peek`, `--ra-dump`; `oracle.lua` `ORACLE_PEEK`; `peek_pairs.py` | ~500 lines | §3.4: native vs arena byte-identical over the routes, false positives counted; host vs PCSX-Redux on the set's addresses | RA-1's list (the notes list until then) |
| **RA-3** core, host | rcheevos submodule; `ra.c` (`rc_client` glue, frame rule, `RACH` chunk, hardcore gates, test-hook refusal); the fake server; ctest | ~900 lines | ctest with the fake server; `host_checks.sh` unchanged with RA off (byte-identical files) | **owner: rcheevos approved** |
| **RA-4a** network spike | Vita `SceHttp`/`SceSsl` GET of `https://retroachievements.org/dorequest.php?r=latestintegration` (needs no login) | ~150 lines | **hardware, owner's yes**: does TLS verify? | — |
| **RA-4** Vita | network (option 1 or 2), IME login, token file, the menu section, popups, the list, the frame point | ~1,200 lines | Vita3K against the fake server on the LAN (`rc_client_set_host`); hardware with the owner's yes | RA-4a; option 2 needs **owner: libcurl/OpenSSL approved** |
| **RA-5** real server, casual | the switch that allows the real host; user agent registered by RA | small | **hardware, owner logs in**: a few easy unlocks (Artisans' gems), checked on the site | **RA's yes** |
| **RA-6** hardcore | hardcore against the real server, only if RA grants it | small | hardware, owner | **RA's compliance approval** |
| **RA-7** PAL / JP | address translation through the region maps; JP set 6800 | ~300 lines each | oracle comparison on the PAL disc (D132's `DISC=`) | R4 (PAL), J stages (JP) |
| **L-1** fallback | local achievements of our own (§1.4 a) | ~600 lines | host routes trigger each; Vita3K | only if RA says no, or the owner wants both |

RA-2 needs neither the network nor the dependency, so it can start as soon as the owner agrees with the plan. It's also what we'd show RA.

## 7. Decisions for the owner

1. **Policy.** Send RA the question in Appendix A from your account before any stage that talks to RA's real server (RA-5 on)? Recommended: yes. Until RA answers, the work stays on the host with the fake server (RA-2, RA-3).
2. **rcheevos** (MIT) as a submodule pinned at v12.5.0. Approve?
3. **The HTTP stack.** Try the firmware's `SceHttp`/`SceSsl` first (RA-4a, hardware). If it can't verify the chain, use libcurl + OpenSSL + a CA file (MPL 2.0 if it's Mozilla's bundle). Approve the fallback now or decide after RA-4a?
4. **Hardcore rules** (§5.1): no state loads, no menu cheats, no test hooks, 30 Hz logic only, turning it on restarts the game. And the default: RA off; hardcore on once you log in (RA's recommendation, E)?
5. **Save states in casual:** allowed, with the hit counts saved in the state (§5.2)?
6. **The frame rule** (§5.3): one RA frame per PS1 vblank, not per logic tick. Agree?
7. **The set's logic:** fetch it with your login for RA-1 (I write the fetch script, you run it with your own token, and the output stays in `logs/`)? Or wait for RA's answer first?
8. **The fallback:** if RA says no, build our own local achievements (L-1)? Or drop the feature?

## Appendix A: a draft message to RAdmin

> Hello. I maintain Spyro Vita (github.com/ShayVD/spyro-vita), a native PS Vita port of Spyro the Dragon (NTSC-U, SCUS-94228). It's built from the community decompilation, so it's not an emulator, and players supply their own disc. Several players have asked for RetroAchievements.
>
> I know decompilations aren't eligible for standalone sets. My question is different: could a port like this ever earn the existing PlayStation set (game 11279) for its players, in casual or in hardcore? Or would its unlocks count against your rule on clients other than hardcore-compliant emulators? I don't want to put anyone's account at risk, so nothing will be released without your answer.
>
> If there's a path, this is what we'd do:
> - a unique user agent (`SpyroVita/x.y.z`, never another client's);
> - the original's RAM served to rcheevos at the PS1 addresses, checked against PCSX-Redux on the same routes;
> - one evaluation per PS1 vblank;
> - in hardcore: no state loading, no cheats, no test hooks, the original 30 Hz logic, and a reset on enabling.
>
> The plan is public at docs/retroachievements-plan.md. Who should I talk to, and what would you need from us?

## Sources

- RA documents (`github.com/RetroAchievements/docs` `5fa3d58`, published at docs.retroachievements.org):
  - `docs/general/hardcore-compliance-requirements.md`
  - `docs/guidelines/users/global-leaderboard-and-achievement-hunting-rules.md`
  - `docs/general/standalone-support.md`
  - `docs/developer-docs/unsupported-emulators-and-cores.md`
- RA API docs: `github.com/RetroAchievements/api-docs` `9d0aeb7`, `docs/connect/standalone.md`.
- RA server (`github.com/RetroAchievements/RAWeb` `0b7de56`):
  - `app/Enums/ClientSupportLevel.php`
  - `app/Platform/Services/UserAgentService.php`
  - `app/Connect/Actions/AwardAchievementAction.php`
  - `app/Connect/Actions/GetAchievementSetsAction.php`
  - `app/Connect/Support/GeneratesConnectWarnings.php`
  - `app/Policies/AchievementPolicy.php`
  - `public/dorequest.php`
- rcheevos (`github.com/RetroAchievements/rcheevos` `eea6461`, v12.5.0):
  - `src/hash/rc_hash_disc.c`
  - `src/runtime/rc_consoleinfo.c`
  - `include/rc_client.h`
  - `LICENSE`
- Public Connect requests (2026-11-10): `retroachievements.org/dorequest.php?r=gameid&m=<hash>` for the three hashes in §2; `r=codenotes2&g=11279` (997 notes) and `g=6800` (148).

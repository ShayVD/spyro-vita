# HD texture packs: what exists, and could Spyro Vita use them?

Research only, 2026-11-04. No engine code. The owner asked whether HD texture packs, like the ones OpenPete supports, could be used in Spyro Vita. Any such feature would be optional and off by default, as `tex_filter=` already is (D39).

**Short answer:** no public HD texture pack for Spyro the Dragon (PS1) exists for any open-source emulator, in any region. The only HD texture route for this game that we found is OpenPete's own PNG replacement, which is off-limits. With nothing to be compatible with, and both emulator formats keyed by machinery we can't or shouldn't reproduce, the only workable path is **our own format with a dump mode**. It should be small, 2× only to start with, and it needs CDRAM we don't have free today. Whether to do it at all is the owner's call ([Recommendation](#recommendation)).

How sure each claim is: **verified** means read on the page or in our code by this session. **Inferred** means reasoned from what was read, and not checked.

## 1. Packs found

Searched 2026-11-04: general web, YouTube, GitHub (`gh` and web), GBAtemp, the libretro forums and docs, the DuckStation wiki, ModDB, Nexus Mods, GameBanana and itch.io. Three sites could not be read: Reddit (blocked to our fetcher), Spyro Forum and the Emulation General wiki (both behind Cloudflare bot checks, which we don't get around). Nothing was downloaded.

| What | Author | For | Format | Regions | Scale / method / size | Complete? | Date, maintained | Licence / terms | Status |
|---|---|---|---|---|---|---|---|---|---|
| **No pack found** for DuckStation | — | DuckStation | `texupload-…` PNG/JPG/WebP | — | — | — | — | — | No hit in any search (verified absence in what we could read) |
| **No pack found** for Beetle PSX HW | — | RetroArch, Beetle PSX HW (Vulkan) | numbered PNGs | — | — | — | — | — | Same |
| **No pack found** for SwanStation | — | RetroArch, SwanStation | DuckStation's older VRAM-write packs | — | — | — | — | — | Same |
| OpenPete's texture replacement | Amec, tyscorp | OpenPete only | "swapping … PNGs" (press) | NTSC-U disc required | unknown | unknown | OpenPete v0.1.3, Aug 2026 | OpenPete's terms, not stated in the article | **Out of scope, off-limits.** The feature exists ([Time Extension, Aug 2026](https://www.timeextension.com/news/2026/08/ps1-classic-spyro-the-dragon-comes-to-pc-with-the-help-of-ai-assisted-tool-psxrecomp)). We found no published pack for it either. Not examined further. |
| [AI upscaler for DuckStation](https://jonasz-o.itch.io/duckstation-upscaler) | Jonasz O. | DuckStation (Windows, Linux, macOS) | A tool, not a pack: it watches DuckStation's dumps, upscales them with one of several ESRGAN-type models (1.5×–6×) and feeds them back live | any | AI, 0.9–2.7 GB download | n/a | v1.08, "in development" | Paid ($5 minimum), no licence stated | Doesn't name Spyro. Shows that a "dump, AI-upscale, replace" workflow is what people actually do now. |
| "Spyro Texture Hacks" thread | various | the original PS1 games | Edited textures patched into the game | — | 1×, hand-edited | — | 2015 | — | Not read (Cloudflare). From the search snippet: recolours and edits at the original resolution, limited by the PS1's memory. Not an HD pack. |

Often confused with the above, and **not** for the PS1 game:
- [Spyro 1 enemy and boss 4K packs](https://www.jikguard.com/news/these-mods-for-the-first-spyro-game-upscale-all-of-the-enemy-and-boss-textures-to-4k) (Cramped Misfit, Nexus Mods) and [PS1 Textures and Proportions](https://www.moddb.com/addons/spyro-ps1-textures): both for *Spyro Reignited Trilogy* (Unreal Engine).
- [Enter the Dragonfly](https://forums.dolphin-emu.org/Thread-spyro-enter-the-dragonfly-texture-pack) and [A Hero's Tail](https://forums.dolphin-emu.org/Thread-spyro-a-hero-s-tail-texture-pack) packs (Dolphin), and [The Eternal Night](https://gbatemp.net/threads/the-legend-of-spyro-the-eternal-night-pcsx2-6x-upscaled-hd-texture-pack.679576/) (PCSX2): later games.
- YouTube videos titled "Spyro … 4K … DuckStation" (for example [Artisans at 4K](https://www.youtube.com/watch?v=9OlaTxg3ZII)): those use the emulator's internal resolution, not a texture pack.

The decomp the port is built on targets only NTSC-U SCUS-94228. A pack for another region would be keyed to different data anyway (inferred).

## 2. The formats, and whether we could compute the same keys

### How Spyro puts textures in VRAM (verified, decomp)

A level's textures arrive as **one 512×512 image, in two `LoadImage` calls of 512×256 each**, into VRAM x 512–1023 (`third_party/spyro-1/src/loaders.c:1425-1450`, `case 5`; the first level in `initialization.c:396-397`). Cutscenes load a 512×N image to the same place (`loaders.c:830-851`). Then VRAM doesn't change for minutes in gameplay (D41). About 88% of the world's textured triangles map one whole aligned 32×32 tile (D39's measurement). Other uploads are small and fixed: the HUD and font at start, and the pause screen's grey copy of the frame (`gamestates/draw.c:931-935`).

This matters for every format below. An emulator that keys on "a VRAM upload" sees two huge uploads per level, not one upload per texture.

### Our renderer's key (verified, `src/platform/vita/gpu_gl.c`)

`atlas_find` (line 1315) keys a decoded block by **position**: texture format, texture page, CLUT (VRAM position), and the box of 32×32 blocks a polygon's UVs cover, plus the wrap flag (`k0`, `k1`). It decodes from `s_vram`, the CPU's copy of VRAM that `gl_GR_CopyVRAM` (PsyCross's `LoadImage` path, line 2909) writes. The atlas is 2048×2048 RGBA8, two of them (16 MB each), and alpha holds the PS1 texel's kind (transparent, opaque, opaque with STP). It serves filtered polygons, or all of them with `tex_cache=1`. Sprites and draws over a render target sample VRAM directly.

So we already know, for every textured polygon, its format, page, CLUT and block. Turning that into a *content* key (a hash of the block's texels and the CLUT entries) is cheap: it only runs on an atlas miss, 824–900 blocks per level (the hardware count with the 1024 atlas, in the comment above `#define ATLAS`).

### DuckStation (verified from its [wiki page](https://github.com/stenzek/duckstation/wiki/Texture-Replacement))

- **Key:** file names like `texupload-P4-<16 hex>-<16 hex>-64x256-0-192-64x64-P0-14`: format (`P4`, `P8`, `C16`), a 64-bit hash of the texture's index data, a 64-bit hash of its palette, the size of the original VRAM upload, the sub-texture's offset in that upload, its size in 16-bit pixels, and the palette range hashed. C16 textures have no palette fields. PNG, JPG or WebP. Files go under `textures/<SERIAL>/`, with a per-game `config.yaml` and an `Aliases:` section.
- **What the wiki doesn't say:** the hash algorithm, and exactly which bytes are hashed. The sub-texture boxes come from tracking VRAM writes and how they are drawn. Many `config.yaml` options tune that tracking per game (`MaxVRAMWriteSplits`, `MaxVRAMWriteCoalesceWidth/Height`, `ReducePaletteRange`, `ConvertCopiesToWrites`, `DumpTexturePages`), and the wiki says the system "is not going to be compatible with all games".
- **Older mode, "VRAM write replacement":** replaces a whole upload by a hash of its contents. RetroDECK's install guide still uses it. For Spyro that would be one replacement image per 512×256 half of a level's VRAM (inferred from the upload sizes above).
- **Licence (verified, `gh api`):** **CC BY-NC-ND 4.0** since September 2024 (GPLv3 before, then PolyForm Strict for two weeks; [GamingOnLinux](https://gamingonlinux.com/2024/09/playstation-1-emulator-duckstation-changes-license-for-no-commercial-use-and-no-derivatives)). Not an open-source licence, and it forbids derivatives.
- **Could we compute the same keys?** Only by reproducing an undocumented hash and DuckStation's write-tracking heuristics, with Spyro-specific settings nobody has published. The hash and byte layout are only in its source, and reading a no-derivatives codebase to reimplement its keying is a licensing question for the owner, not something to start on. Matching it would gain nothing anyway: no Spyro pack exists in this format.

### Beetle PSX HW (verified from [libretro docs](https://docs.libretro.com/library/beetle_psx_hw/), the [2020 announcement](https://www.libretro.com/index.php/category/texture-replacement), the [testing thread](https://forums.libretro.com/t/beetle-psx-hw-texture-replacement-testing-feedback/28954))

- **Key:** not documented. Dumps are PNGs with numeric names in `<game file name>-texture-dump/`, and replacements go in `<game file name>-texture-replacements/`, matched by name. Replacements must be PNGs of at most 8 bits per channel, at a power-of-two multiple of the original size. Vulkan renderer only. The announcement calls the format "not set in stone". A 2026 fork's notes ([GBAtemp](https://gbatemp.net/threads/beetle-psx-hw-with-vram-cache-for-texture-replacements.682614/)) say texture data and palettes are hashed separately, and offer 256 MB–8 GB caches for packs that "upscale everything (all palettes)".
- **Licence:** GPLv2 (verified, `gh api`).
- **Could we compute the same keys?** Not from the docs. Learning the key means reading GPL code, and we must not copy GPL code into this MIT repo. A clean reimplementation from a written description might be possible, but that is the owner's call. Again, no Spyro pack exists to justify it.

### SwanStation (verified from its core option text, `libretro/swanstation` `src/libretro/libretro_core_options.h`)

A GPLv3 fork of DuckStation from before the licence change. Its "Enable VRAM Write Texture Replacement" option loads "DuckStation-formatted texture packs" from `system/swanstation/textures`, with Vulkan or D3D11 only. That is DuckStation's older whole-upload format, above.

### Other emulators

PCSX-Redux (our oracle), ePSXe, Mednafen standalone and the PSX GPU plugins have no texture replacement that we could find.

## 3. Recommendation

**Don't support an existing format.** No Spyro packs exist for any of them. DuckStation's key is undocumented and its code is no-derivatives. Beetle's key is undocumented and its code is GPL. And Spyro's two 512×256 uploads per level fit "key by VRAM upload" schemes badly.

**If the owner wants the feature, design our own, with a dump mode, and keep it small:**
1. **Key by content, per 32×32 block:** format, a 64-bit hash of the block's raw VRAM words, and a hash of the CLUT entries it uses (16 or 256). Not by VRAM position, so a tile that appears in several levels at different places is one file. Use an in-tree hash, as `cdimage.c` did for SHA-1 (D60): adding xxHash would be a new dependency, so ask first. Blocks match what `atlas_find` already caches, so the hook point is the atlas miss.
2. **Dump mode** (`tex_dump=1`, off by default): on each new block key, write the decoded 1× block as a PNG to `ux0:data/spyro/textures/dump/`. A user plays through, then edits or AI-upscales the files on a PC. The files come from the user's own disc and stay on their card.
3. **Replace mode** (`tex_pack=1`, off by default): at level load, read the level's replacements into an **HD atlas** instead of decoding 1× blocks. Transparency and STP come from the original texel at that point, not from the PNG's alpha, so PS1 blending stays exact. Convert packs offline with a host tool (Python, in `tools/`) into a per-level binary with mipmaps, in a format GXM samples directly (UBC1/UBC3 or PVRTC, to be checked). That way the Vita never decodes PNGs at load.
4. **2× only to start.** At 960×544 the PS1's 512×240 picture is scaled about 2×, so 2× textures roughly match the screen's texel density on near geometry (inferred). 4× costs four times the memory and mostly gets minified.
5. **Proof before any upscale:** replace every block with its own 1× dump. The picture must be byte-identical to the pack being off, on the host and in Vita3K, then on hardware with the owner's approval.

Effort (inferred): the dump and the key are small, inside `gpu_gl.c`. The HD atlas, the converter, the memory changes below and the hardware checks are the real work, about the size of D79 (atlas on the CPU) plus D69 (CDRAM placement).

**Or don't pursue it.** Nobody has made a Spyro 1 pack in eight years of emulator support, and the bounty doesn't ask for one. If the owner's priority is finishing the native stages and v1.x stability, this can wait.

## 4. Risks

### Vita memory (verified from `docs/decisions.md`)

- vitaGL's CDRAM pool is **96 MB on hardware**, and in play it is **about 3 MB from full** (D69; D75's `vitaGL free KB` lines). The brief's 128 MB is the whole of CDRAM. The OS and the framebuffers take the rest.
- What's there today: two 2048×2048 RGBA8 atlases (32 MB), the render targets, and a D32 depth buffer vitaGL gives every FBO and we never use (16 MB each for the atlases, D69). A texture that doesn't fit lands in main RAM, which is slower for the GPU (D69).
- **Sizes (inferred, from the ~900 blocks per level the 1024 atlas held on hardware):** about 0.9 M texels at 1×. At 2× that's 3.7 M texels: 15 MB RGBA8, about 2 MB as UBC1 or 4 MB as UBC3, plus a third for mipmaps. At 4×: 59 MB RGBA8, or 8–16 MB compressed. So 2× compressed could fit **only if CDRAM is freed first**, for example by dropping the atlases' unused depth buffers or replacing the 1× atlas when a pack is active. Each of those is an engine change of its own, checked on hardware.
- Palette variants multiply this: each CLUT a block is drawn with is a separate image.

### GPU cost at 60 fps (inferred, unmeasured)

- The shader work per pixel is the same as `tex_cache=1`'s. The cost is texture bandwidth and cache misses from larger textures, which mipmaps keep down.
- An HD atlas that fills up falls back to 1× blocks (today's `s_afull` behaviour), so a mixed picture is possible. It has to be sized per level, or evict.
- Sprites (HUD, font) and draws over render targets don't use the atlas today. They would stay 1× unless covered separately.
- Level loads get longer: reading 2–8 MB from the memory card per level. Not measured.
- Each step needs the `gpu_meter=1` and `dynres` numbers on hardware, with the owner's approval (D75, D77).

### Legality and redistribution

- **We ship no textures.** Dumps are made from the user's own disc, on their device, like `tools/extract_disc.py`. The repo and VPK must never hold a dump, a pack, or a converted pack (CLAUDE.md). That includes test fixtures: tests should use synthetic blocks.
- **Packs are derivative works of the game's art** (Insomniac and Sony's copyright, inferred, not legal advice). Whoever publishes one carries that risk. The README should say packs are third-party and unsupported, and link to none.
- **Keep it independent of OpenPete.** Design our format without reading OpenPete's, and never claim compatibility with OpenPete packs.
- **Emulator code:** DuckStation (CC BY-NC-ND) and Beetle (GPLv2) can't be copied into this MIT repo. Even reimplementing their keys from their sources is the owner's decision. This design doesn't need either.

## Sources

- DuckStation: [Texture Replacement wiki](https://github.com/stenzek/duckstation/wiki/Texture-Replacement); licence via `gh api repos/stenzek/duckstation` (CC BY-NC-ND 4.0 LICENSE file); [GamingOnLinux on the change](https://gamingonlinux.com/2024/09/playstation-1-emulator-duckstation-changes-license-for-no-commercial-use-and-no-derivatives); [RetroDECK install guide](https://retrodeck.readthedocs.io/en/latest/wiki_emulator_guides/duckstation/duckstation-texture-pack/).
- Beetle PSX HW: [libretro docs](https://docs.libretro.com/library/beetle_psx_hw/); [announcement](https://www.libretro.com/index.php/category/texture-replacement); [testing thread](https://forums.libretro.com/t/beetle-psx-hw-texture-replacement-testing-feedback/28954); [VRAM-cache fork, GBAtemp](https://gbatemp.net/threads/beetle-psx-hw-with-vram-cache-for-texture-replacements.682614/); licence via `gh api repos/libretro/beetle-psx-libretro` (GPL-2.0).
- SwanStation: [libretro/swanstation](https://github.com/libretro/swanstation) (GPL-3.0), core option text in `src/libretro/libretro_core_options.h`.
- OpenPete (existence only): [Time Extension, Aug 2026](https://www.timeextension.com/news/2026/08/ps1-classic-spyro-the-dragon-comes-to-pc-with-the-help-of-ai-assisted-tool-psxrecomp).
- Tools: [Jonasz O.'s DuckStation upscaler](https://jonasz-o.itch.io/duckstation-upscaler).
- Our code: `src/platform/vita/gpu_gl.c` (`atlas_find` :1315, `gl_GR_CopyVRAM` :2909); decomp `src/loaders.c` :830-851, :1425-1450, `src/initialization.c` :396-397; `docs/decisions.md` D39, D41, D60, D69, D75, D77, D79.

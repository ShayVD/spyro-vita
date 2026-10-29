# Research summary (29 Oct 2026)

## Bounty
- **Listing:** zealouschuck.com/ps-vita-bounty, "Bounty for a Spyro 1 Decomp ( Open Pete )".
- **Amount:** $32.50, active, unclaimed.
- **Terms:** confirmed by the poster on 2026-10-29: a decomp-based native port counts, and there are no extra rules. Our own goal is high frame rate, 60 fps if possible.

## Who else is working on it
- **No public Vita, Switch, Android or 32-bit ARM effort found.** GitHub search for "spyro vita" returns nothing.
- **OpenPete** (tyscorp, with thanks to altro50) is at v0.4.2, Windows and Linux x86-64. A macOS AArch64 build is planned.
  - It needs Vulkan 1.2.
  - Its source is "coming soon": the tree contains copyrighted data that still has to be separated out.
  - Licence: PolyForm Noncommercial 1.0.0.
- **OpenPete-based mods** exist: split-screen co-op (Tokatta007) and rainbow-spyro.
- **Frogbull's N64 port** stalled at the first hub world.

## Components
| Piece | State | Licence |
|---|---|---|
| TheMobyCollective/spyro-1 | 254/270 functions (94.1%), 65% of bytes. Overlays matched except 2 credits functions. Active as of Oct 2026, builds on a modern GCC. | CC0 |
| OpenDriver2/PsyCross | PsyQ reimplementation (GPU, GTE, SPU, CD) on SDL and OpenGL | MIT |
| RetroPortingToolKit/psxrecomp | PS1 MIPS → C static recompiler. SDL3, OpenGL or Vulkan. Desktop only. | PolyForm Noncommercial 1.0.0 (its `LICENSE`, checked 2026-10-29). Reference only. |
| OpenPete | Recomp plus decomp hybrid with its own renderer (reads game state directly, Vulkan) | Noncommercial, closed source |

## Unmatched functions (size of the .s file)

File size overstates the work. Instruction counts are in `docs/porting-plan.md` §2: `func_80043FE4` is 3,865 instructions, and all 18 together are ~20,000. The hand-written assembly is ~24,500.
| Size | Function |
|---|---|
| 227 KB | `pete/func_80043FE4` |
| 153 KB | `pete/func_80041670` |
| 128 KB | `gamestates/update/func_8002F3E4` |
| 86 KB | `credits/func_credits_8007AA50` |
| 80 KB | `camera/func_80035FB4` |
| 80 KB | `pete/func_8003EA68` |
| 73 KB | `cyclorama/func_80050BD0` |
| 65 KB | `pete/func_8003FE40` |
| 62 KB | `update/func_800314B4` |
| 50 KB | `pete/func_80047B60` |
| 41 KB | `camera/func_80034CE8` |
| 33 KB | `pete/func_80048D10` |
| 30 KB | `camera/func_80034480` |
| 30 KB | `camera/func_800357A4` |
| 14 KB | `credits/func_credits_8007BFD0` |
| 13 KB | `moby_helpers/func_8003BFC0` |
| 10 KB | `camera/func_80033F08` |
| 9 KB | `pete/func_80049FAC` |

## Feasibility
- **Technically high, and simpler than KH1 PS2.** There's much less code and no VU/EE work.
- **Pointers:** 32-bit ARM keeps the game's 32-bit pointers as they are.
- **Performance:** plenty of CPU headroom, and 960×544 is native 16:9.
- **No JIT needed:** all overlays are known, so everything can be linked statically.
- **Risks:**
  - The large translations of the player and camera code.
  - The renderer's heavy use of the PS1's geometry coprocessor (GTE), and the hand-written assembly.
  - Whether a port built on the decomp counts for the "(Open Pete)" bounty.

## Recommendation
Build on the decomp + PsyCross, with vitaGL and SDL2, and translate the unmatched functions to C. Keep porting OpenPete as a fallback if its source is released under usable terms.

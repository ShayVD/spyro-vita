# LiveArea art

The three images here go into the VPK's `sce_sys/` as the bubble icon, the LiveArea background
and the start gate. They're 8-bit indexed PNGs at the Vita's sizes. The build converts them with
vita-port-kit's `livearea_png.py from` and checks them with its `check`
(`third_party/vita-port-kit/vita/tools/`; decisions D59).

| File | Size | What it is |
|---|---|---|
| `icon0.png` | 128×128 | the *Spyro the Dragon* logo |
| `startup.png` | 280×158 | the same logo, cropped to its middle band |
| `bg.png` | 840×500 | concept art by Simon Kopp (2017/18) for *Spyro Reignited Trilogy*, Airborn Studios, scaled whole with the signature kept |

**The PS4 release fpkg uses the icon too** (the owner's decision, 2026-11-24, D246):
`tools/ps4/art.py` rescales `icon0.png` to the PS4's 512×512 icon. The PS4 package has no
background picture: on the owner's PS4 it stayed over the game, and the owner had it removed. The
gitignored `disc/livearea/` copy, when present, is the better source and is used first.

**Not ours, and not under the MIT licence.** Spyro and Spyro the Dragon are trademarks of
Activision Publishing, Inc. The background painting is © its artist and rights holders. No licence
has been obtained; they're included at the project owner's decision, and will be replaced on
request from the rights holders.

Delete this folder and the build draws original art instead (D55). To use other images, put
them in the gitignored `disc/livearea/` (`tools/vita/livearea_local.sh`), which takes precedence.

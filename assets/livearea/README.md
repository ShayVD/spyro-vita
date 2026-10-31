# LiveArea art

The three images here go into the VPK's `sce_sys/` as the bubble icon, the LiveArea background
and the start gate. They're 8-bit indexed PNGs at the Vita's sizes. The build converts them with
`tools/vita/make_livearea.py --from` and checks them with `--check` (decisions D59).

| File | Size | What it is |
|---|---|---|
| `icon0.png` | 128×128 | the *Spyro the Dragon* logo |
| `startup.png` | 280×158 | the same logo, cropped to its middle band |
| `bg.png` | 840×500 | concept art by Simon Kopp (2017/18) for *Spyro Reignited Trilogy*, Airborn Studios, scaled whole with the signature kept |

**Not ours, and not under the MIT licence.** Spyro and Spyro the Dragon are trademarks of
Activision Publishing, Inc. The background painting is © its artist and rights holders. No licence
has been obtained; they're included at the project owner's decision, and will be replaced on
request from the rights holders.

Delete this folder and the build draws original art instead (D55). To use other images, put
them in the gitignored `disc/livearea/` (`tools/vita/livearea_local.sh`), which takes precedence.

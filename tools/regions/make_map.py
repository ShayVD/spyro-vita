#!/usr/bin/env python3
"""Write a release's data map, src/game/region_map_<region>.c (D133,
docs/regions-plan.md §6.2), from the player's own executable.

The map holds addresses, sizes and word offsets only, never the data: the
port reads the data from the player's verified executable at start-up
(src/game/region_data.c).

- Blocks (BLOCKS below): the executable's ranges the port copies. The first
  is the string area before `main`; a block marked "relocate" has every word
  that aims inside a block listed as a pointer to relocate. Words aimed at
  the executable's code (jump tables, function pointers) are left alone.
- Fills (FILLS below): US data labels whose table the release keeps in the
  same shape at another address (tools/regions/data_map.py places them, with
  --aligned since D143). The table must be as long as the US label, end where
  the release's next placed label starts, and hold no pointer into the
  executable, or the tool stops. A fill is copied over its US label at
  start-up. A "timing" fill (PAL's speeds, turn rates and the camera's, x1.2
  and x1.44 of the US values) only when the release runs at its own timing
  (g_RegionTiming): PAL 60 keeps the US table, as RT() keeps the US constant.

Usage (from the repo root):
  python3 -I tools/regions/make_map.py pal disc/files-pal/SCES_014.38 [--check]
--check compares with the committed file instead of writing it.
"""
import argparse
import hashlib
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

REGIONS = {
    "pal": {
        "id": "PSX_REGION_PAL",
        "serial": "SCES-01438",
        "sha1": "fdd04c044cd1892ae28c43e9e619dd41a848058a",
        # (name, start, end or None for the end of the text, relocate)
        "blocks": [
            # the strings before main (0x80015DCC): ~1,050 strings in five
            # languages, the card's file name, jump tables (regions plan §3.3)
            ("strings", 0x80010000, 0x80015DCC, False),
            # .data: from the end of the code (PadInitDirect's end, US
            # 0x8006BBE0 + 0x5474) to the end of the text: the five-language
            # tables, the short strings, the card header template
            ("data", 0x80071054, None, True),
        ],
        # the music: XA files' first sectors, track lengths, track numbers,
        # each level's choice of three (src/music.c, initialization.c:152)
        "fills": ["D_8006EEDC", "D_8006EF9C", "D_8006F05C", "D_8006F200",
                  # D143, PAL's own whatever the timing: Spyro's animations'
                  # first and last frames (PAL's model has fewer frames:
                  # with the US table he stepped past its last one and the
                  # renderer read garbage, the host's first PAL level), his
                  # animation transitions, the sky's clip at 256 lines, the
                  # HUD's targets in the 512x256 picture, the attract demos'
                  # lengths (PAL's recordings) and two of LoadLevelScene's
                  # sound entries
                  "spyro_AnimationDetails", "D_8006BC84", "D_8006CB8C",
                  "g_HudMobyTargetPos", "g_HudEggTargetRect", "g_DemoLengths",
                  "D_8006F140",
                  # six of the particle spawner's positions (D_8006E4E0[],
                  # particle_spawn.c), 144 further in x in PAL. No function
                  # data_map pairs reads D_8006E52C, so its PAL address is
                  # given (from aligning the two releases' data, D143)
                  ("D_8006E52C", 0x800739A0)],
        # D143, PAL's 50 Hz retune in tables R3's constant sites couldn't
        # see: Spyro's slope speeds (func_8003EA68, func_80047B60), turn
        # rates (UpdateSpyroTurnMomentum, func_80043FE4's turn on the spot
        # and flight turn), the camera's (func_80034CE8)
        "timing_fills": ["D_8006C5B8", "D_8006C5BC", "D_8006C5C0", "D_8006C5C4",
                         "D_8006C5C8", "D_8006C5CC", "D_8006C5F0", "D_8006C6F8",
                         "D_8006C704", "D_8006C70A", "D_8006C710"]
                        + ["D_8006C%03X" % (0x754 + 12 * k) for k in range(18)],
    },
}


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("region", choices=sorted(REGIONS))
    ap.add_argument("exe")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    R = REGIONS[a.region]
    exe = open(a.exe, "rb").read()
    sha = hashlib.sha1(exe).hexdigest()
    if sha != R["sha1"]:
        sys.exit(f"{a.exe}: SHA-1 {sha}, the {R['serial']} map is for {R['sha1']}")
    t_addr, t_size = struct.unpack_from("<II", exe, 0x18)
    text_end = t_addr + t_size

    def word(addr):
        return struct.unpack_from("<I", exe, 0x800 + addr - t_addr)[0]

    blocks = [(n, s, e or text_end, rel) for n, s, e, rel in R["blocks"]]

    def in_blocks(v):
        return any(s <= v < e for _, s, e, _ in blocks)

    out_blocks = []
    for n, s, e, rel in blocks:
        relocs = [(o - s) // 4 for o in range(s, e, 4) if rel and in_blocks(word(o))] if rel else []
        if relocs and max(relocs) > 0xFFFF:
            sys.exit(f"block {n}: offsets past 16 bits")
        code = sum(1 for o in range(s, e, 4) if rel and t_addr <= word(o) < text_end
                   and not in_blocks(word(o)))
        out_blocks.append((n, s, e - s, relocs, code))

    dm = load("data_map")
    placed, _ = dm.build(a.exe, os.path.join(ROOT, "build/s1-matching/build/PSX.EXE"),
                         os.path.join(ROOT, "build/s1-matching/build/psx.elf"),
                         os.path.join(ROOT, "build/s1-matching/build/psx.map"), loose=True)
    by_label = {n: (u, o, size) for n, u, o, size, _ in placed}
    others = sorted(o for _, _, o, _, _ in placed)
    fills = []
    labels, order = dm.map_labels(os.path.join(ROOT, "build/s1-matching/build/psx.map"))
    for label, timing in ([(x, 0) for x in R["fills"]] +
                          [(x, 1) for x in R.get("timing_fills", [])]):
        R_label = label
        if isinstance(label, tuple):  # (label, the release's address): placed by hand
            label, o = label
            u = next(a for a, n in labels.items() if n == label)
            size = next(x for x in order if x > u) - u
            # no placed label to end it: the 16 bytes before and after it
            # must be the same in both releases (the data aligned there)
            us_exe = open(os.path.join(ROOT, "build/s1-matching/build/PSX.EXE"), "rb").read()
            if any(struct.unpack_from("<I", us_exe, 0x800 + u + d - 0x80010000)[0] != word(o + d)
                   for d in (-16, -12, -8, -4, size, size + 4, size + 8, size + 12)):
                sys.exit(f"{label}: the data around {o:08x} isn't US's around {u:08x}")
        elif label not in by_label:
            sys.exit(f"{label}: not placed in {R['serial']} by data_map.py")
        else:
            u, o, size = by_label[label]
        nxt = next((x for x in others if x > o), None)
        if not isinstance(R_label, tuple) and nxt != o + size:
            sys.exit(f"{label}: {size} bytes in US, but {R['serial']}'s next label is at "
                     f"{nxt:08x}, not {o + size:08x}")
        if any(t_addr <= word(x) < text_end for x in range(o, o + size, 4)):
            sys.exit(f"{label}: holds a pointer into the executable")
        fills.append((label, o, size, timing))

    name = a.region.capitalize()
    L = [f"/* Generated by tools/regions/make_map.py {a.region} from {R['serial']}'s executable",
         f" * (SHA-1 {R['sha1']}, {len(exe)} bytes): do not edit.",
         " * Addresses, sizes and word offsets only; the data is read from the",
         " * player's own disc at start-up (src/game/region_data.c, D133). */",
         '#include "region_data.h"', '#include "psx_region.h"', ""]
    for label, _, size, _ in fills:
        L.append(f"extern unsigned char {label}[];")
    L.append("")
    for n, s, size, relocs, code in out_blocks:
        if not relocs:
            continue
        L.append(f"/* {n}: {len(relocs)} pointers into the blocks; {code} words aimed at the "
                 "code, left alone */")
        L.append(f"static const uint16_t k_{n}_relocs[{len(relocs)}] = {{")
        for i in range(0, len(relocs), 12):
            L.append("    " + ", ".join(str(x) for x in relocs[i:i + 12]) + ",")
        L.append("};")
        L.append("")
    L.append(f"static const RegionBlock k_blocks[{len(out_blocks)}] = {{")
    for n, s, size, relocs, _ in out_blocks:
        r = f"k_{n}_relocs, {len(relocs)}" if relocs else "NULL, 0"
        L.append(f'    {{"{n}", 0x{s:08X}, 0x{size:X}, {r}}},')
    L.append("};")
    L.append("")
    L.append(f"static const RegionFill k_fills[{len(fills)}] = {{")
    for label, o, size, timing in fills:
        L.append(f'    {{"{label}", 0x{o:08X}, {size}, {timing}}},')
    L.append("};")
    L.append("")
    L.append("static void *fill_dst(int i) {")
    L.append("  switch (i) {")
    for i, (label, _, _, _) in enumerate(fills):
        L.append(f"  case {i}:")
        L.append(f"    return {label};")
    L.append("  }")
    L.append("  return NULL;")
    L.append("}")
    L.append("")
    L.append(f"const RegionMap g_RegionMap{name} = {{")
    L.append(f"    {R['id']}, \"{R['sha1']}\", {len(exe)},")
    L.append(f"    k_blocks, {len(out_blocks)}, k_fills, {len(fills)}, fill_dst}};")
    text = "\n".join(L) + "\n"
    path = os.path.join(ROOT, "src/game", f"region_map_{a.region}.c")
    if a.check:
        same = os.path.exists(path) and open(path).read() == text
        print(f"{path}: {'up to date' if same else 'DIFFERS'}")
        return 0 if same else 1
    open(path, "w").write(text)
    for n, s, size, relocs, code in out_blocks:
        print(f"block {n}: {s:08x}, {size} bytes, {len(relocs)} pointers, {code} code words left")
    for label, o, size, timing in fills:
        print(f"fill {label}: {o:08x}, {size} bytes{' (timing)' if timing else ''}")
    print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

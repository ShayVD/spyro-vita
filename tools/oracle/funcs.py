#!/usr/bin/env python3
"""List the translated entry points for tools/oracle/capture.lua: "ADDR NAME"
lines from the generated m2c_index.c. Overlay code (0x8007xxxx and up) is
left out unless --overlays: several overlays share those addresses, so a
breakpoint there would also stop in the wrong overlay."""
import argparse
import re
import sys

ap = argparse.ArgumentParser()
ap.add_argument("index", help="build/linux32/gen/mips2c/m2c_index.c")
ap.add_argument("--match", help="regex on the name")
ap.add_argument("--overlays", action="store_true")
a = ap.parse_args()
for m in re.finditer(r'\{0x([0-9A-F]{8})u, "(\w+)"', open(a.index).read()):
    addr, name = int(m.group(1), 16), m.group(2)
    if addr >= 0x80070000 and not a.overlays:
        continue
    if a.match and not re.search(a.match, name):
        continue
    print(f"{addr:08X} {name}")

#!/usr/bin/env python3
"""Print game fields at the entry of each oracle sample (capture.lua NAME_K.in),
to see which paths of a function the samples cover.

    sample_fields.py --func NAME [--map psx.map] DIR... FIELD...

FIELD is SYMBOL[+OFFSET][:SIZE] (SIZE 1, 2 or 4, default 4; signed), e.g.
g_Spyro+0x78 (Spyro's state) or g_SpyroFlame+0x98:1."""
import argparse
import glob
import os
import re
import struct

HDR = (5 + 34 + 32 + 32) * 4  # header words, gpr+lo/hi, cp2d, cp2c
SCRATCH = 1024

ap = argparse.ArgumentParser()
ap.add_argument("--func", required=True)
ap.add_argument("--map", default="build/s1-matching/build/psx.map")
ap.add_argument("args", nargs="+")
a = ap.parse_args()

syms = {}
for line in open(a.map):
    m = re.match(r"\s+0x([0-9a-f]{8,16})\s+(\w+)\s*$", line)
    if m:
        syms[m.group(2)] = int(m.group(1), 16) & 0xFFFFFFFF

dirs = [x for x in a.args if os.path.isdir(x)]
fields = []
for f in a.args:
    if f in dirs:
        continue
    m = re.match(r"(\w+)(?:\+(0x[0-9a-fA-F]+|\d+))?(?::([124]))?$", f)
    if not m or m.group(1) not in syms:
        raise SystemExit(f"bad field {f}")
    addr = syms[m.group(1)] + (int(m.group(2), 0) if m.group(2) else 0)
    fields.append((f, addr, int(m.group(3) or 4)))

print("\t".join(["sample", "vblank"] + [f for f, _, _ in fields]))
for d in dirs:
    paths = glob.glob(f"{d}/{a.func}_*.in")
    for p in sorted(paths, key=lambda p: int(p.rsplit("_", 1)[1][:-3])):
        data = open(p, "rb").read()
        vblank = struct.unpack_from("<I", data, 16)[0]
        row = [os.path.basename(d) + "/" + os.path.basename(p)[:-3], str(vblank)]
        for _, addr, size in fields:
            off = HDR + SCRATCH + (addr - 0x80000000)
            fmt = {1: "<b", 2: "<h", 4: "<i"}[size]
            row.append(str(struct.unpack_from(fmt, data, off)[0]))
        print("\t".join(row))

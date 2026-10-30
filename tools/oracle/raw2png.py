#!/usr/bin/env python3
"""Convert oracle VRAM dumps (vram-<n>.bin from tools/oracle/oracle.lua,
1024x512 at 16 bpp) to shot-<n>.png next to them, so tools/contact_sheet.py
can tile them.  tools/oracle/raw2png.py DIR [--rect X,Y,W,H] [--24]
--rect crops (default: all of VRAM); --24 reads the rect as 24-bit pixels
(the logo screens), W in output pixels."""
import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from contact_sheet import write_png  # noqa: E402


def convert(path, rect, bpp24):
    d = open(path, "rb").read()
    x0, y0, w, h = rect
    rgb = bytearray(w * h * 3)
    for y in range(h):
        row = (y0 + y) * 2048 + x0 * 2
        if bpp24:
            rgb[y * w * 3:(y + 1) * w * 3] = d[row:row + w * 3]
            continue
        for x in range(w):
            v = d[row + x * 2] | d[row + x * 2 + 1] << 8
            r, g, b = v & 31, v >> 5 & 31, v >> 10 & 31
            i = (y * w + x) * 3
            rgb[i:i + 3] = bytes((r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2))
    n = re.findall(r"(\d+)", os.path.basename(path))[-1]
    write_png(os.path.join(os.path.dirname(path), f"shot-{n}.png"), w, h, rgb)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--rect", default="0,0,1024,512")
    ap.add_argument("--24", dest="bpp24", action="store_true")
    a = ap.parse_args()
    for p in glob.glob(f"{a.dir}/vram-*.bin"):
        convert(p, tuple(int(v) for v in a.rect.split(",")), a.bpp24)

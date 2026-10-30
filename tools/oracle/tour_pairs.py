#!/usr/bin/env python3
"""Pair the host's and the emulator's level-tour screenshots (tour-K-LEVEL.png
from spyro --tour, tour-K-LEVEL.raw from capture.lua ORACLE_TOUR; convert the
.raw first with shot2png.py). For each tour entry K: whether each side drew a
picture (share of non-black pixels), and how far apart the two are (mean
absolute difference of 32x15 block averages, 0-255; the camera idles, so a
few units is normal). Writes OUT.png, every pair side by side (host left),
one row per entry, and prints a table.

    tools/oracle/tour_pairs.py HOSTDIR ORACLEDIR OUT.png [--markdown]
"""
import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from contact_sheet import read_png, write_png  # noqa: E402

GW, GH = 32, 15


def shots(d):
    out = {}
    for p in glob.glob(os.path.join(d, "tour-*.png")):
        m = re.match(r"tour-(\d+)-(\d+)\.png$", os.path.basename(p))
        if m:
            out[int(m.group(1))] = (int(m.group(2)), p)
    return out


def blocks(w, h, rows):
    g = []
    for by in range(GH):
        for bx in range(GW):
            s = n = 0
            for y in range(by * h // GH, (by + 1) * h // GH, 2):
                r = rows[y]
                for x in range(bx * w // GW, (bx + 1) * w // GW, 2):
                    s += r[3 * x] + r[3 * x + 1] + r[3 * x + 2]
                    n += 3
            g.append(s / max(n, 1))
    return g


def lit(w, h, rows):
    n = sum(1 for y in range(0, h, 4) for x in range(0, w, 4)
            if rows[y][3 * x] + rows[y][3 * x + 1] + rows[y][3 * x + 2] > 24)
    return n / ((h // 4 + (h % 4 > 0)) * (w // 4 + (w % 4 > 0)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("oracle")
    ap.add_argument("out")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    hs, os_ = shots(a.host), shots(a.oracle)
    keys = sorted(set(hs) | set(os_))
    W, H = 320, 150  # each picture scaled to this
    sheet = bytearray(3 * (2 * W + 8) * H * len(keys))
    SW = 2 * W + 8
    if a.markdown:
        print("| Entry | Level | Host drew | Oracle drew | Difference |")
        print("|---|---|---|---|---|")
    for row, k in enumerate(keys):
        info, grids = [], []
        for col, side in enumerate((hs, os_)):
            if k not in side:
                info.append("-")
                grids.append(None)
                continue
            lvl, p = side[k]
            w, h, rows = read_png(p)
            info.append(f"{lit(w, h, rows):.0%}")
            grids.append(blocks(w, h, rows))
            for y in range(H):
                sy = y * h // H
                for x in range(W):
                    sx = x * w // W
                    o = 3 * ((row * H + y) * SW + col * (W + 8) + x)
                    sheet[o:o + 3] = rows[sy][3 * sx:3 * sx + 3]
        lvl = (hs.get(k) or os_.get(k))[0]
        diff = "-"
        if grids[0] and grids[1]:
            diff = f"{sum(abs(x - y) for x, y in zip(*grids)) / len(grids[0]):.1f}"
        if a.markdown:
            print(f"| {k} | {lvl} | {info[0]} | {info[1]} | {diff} |")
        else:
            print(f"entry {k:2d} level {lvl:2d}  host {info[0]:>4}  oracle {info[1]:>4}  diff {diff}")
    write_png(a.out, SW, H * len(keys), sheet)


if __name__ == "__main__":
    main()

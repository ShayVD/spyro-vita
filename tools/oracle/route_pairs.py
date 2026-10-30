#!/usr/bin/env python3
"""Pair the host's and the emulator's screenshots along a scripted route
(tools/oracle/pads/route_*.txt, docs/whole-game-host.md): both sides' traces
are cut into runs of one (gamestate, level), the runs are paired in order,
and inside each pair the shots nearest to the same vblank offsets from the
run's start are put side by side (host left). Shots are the host's
shot-V.png and the emulator's shot-V.png (shot2png.py from capture.lua's
ORACLE_SHOT_EVERY). Prints one line per pair with its length on each side
and the picture difference (mean absolute difference of 32x15 block
averages, 0-255, as tour_pairs.py).

    tools/oracle/route_pairs.py HOSTDIR ORACLEDIR OUT.png [--gs G,...]
        [--offsets 30,90,...] [--markdown] [--host-shots]

The emulator's shot nearest each offset sets the offset used, and the
host's shot nearest that is taken. --host-shots instead prints the host
vblanks at the same offsets as every emulator shot in those runs, as a
--shot list (at most 256), so a host rerun with it pairs exactly.

Only runs in the listed gamestates (default 1,9,10,12: transitions, the
entrance animation, the level exit and the balloonist) are shown.
"""
import argparse
import csv
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from contact_sheet import read_png, write_png  # noqa: E402
from tour_pairs import blocks  # noqa: E402


def runs(path):
    out, cur = [], None
    for r in csv.DictReader(open(path), delimiter="\t"):
        k = (int(r["gamestate"]), int(r["level"]))
        v = int(r["vblank"])
        if cur and cur[0] == k:
            cur[2] = v
        else:
            cur = [k, v, v]
            out.append(cur)
    return out


def shots(d):
    s = {}
    for p in glob.glob(os.path.join(d, "shot-*.png")):
        m = re.match(r"shot-(\d+)\.png$", os.path.basename(p))
        if m:
            s[int(m.group(1))] = p
    return sorted(s.items())


def nearest(sh, v, lo, hi):
    best = None
    for sv, p in sh:
        if lo <= sv <= hi and (best is None or abs(sv - v) < abs(best[0] - v)):
            best = (sv, p)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("oracle")
    ap.add_argument("out")
    ap.add_argument("--gs", default="1,9,10,12")
    ap.add_argument("--offsets", default="30,120,240,480")
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--host-shots", action="store_true")
    a = ap.parse_args()
    want = {int(g) for g in a.gs.split(",")}
    offs = [int(o) for o in a.offsets.split(",")]
    hr = [r for r in runs(f"{a.host}/trace.tsv") if r[0][0] in want]
    orr = [r for r in runs(f"{a.oracle}/trace.tsv") if r[0][0] in want]
    hs, os_ = shots(a.host), shots(a.oracle)
    if a.host_shots:
        vs = [h0 + sv - o0 for (hk, h0, h1), (ok, o0, o1) in zip(hr, orr) if hk == ok
              for sv, _ in os_ if o0 <= sv <= o1 and h0 + sv - o0 <= h1]
        print(",".join(str(v) for v in vs[:256]))
        return
    W, H = 256, 120
    rows_out = []
    if a.markdown:
        print("| Gamestate | Level | Host vblanks | Oracle vblanks | Picture difference at " +
              ", ".join(f"+{o}" for o in offs) + " |")
        print("|---|---|---|---|---|")
    for (hk, h0, h1), (ok, o0, o1) in zip(hr, orr):
        if hk != ok:
            print(f"runs part: host {hk} oracle {ok}")
            break
        diffs, pics = [], []
        for off in offs:
            op = nearest(os_, o0 + off, o0, o1)
            hp = op and nearest(hs, h0 + op[0] - o0, h0, h1)
            if not hp or not op or off > max(h1 - h0, o1 - o0):
                diffs.append("-")
                continue
            imgs = [read_png(hp[1]), read_png(op[1])]
            g = [blocks(*im) for im in imgs]
            diffs.append(f"{sum(abs(x - y) for x, y in zip(*g)) / len(g[0]):.1f}")
            pics.append(imgs)
        n = h1 - h0 + 1, o1 - o0 + 1
        if a.markdown:
            print(f"| {hk[0]} | {hk[1]} | {n[0]} | {n[1]} | {', '.join(diffs)} |")
        else:
            print(f"gs {hk[0]:2d} level {hk[1]:2d}  host {n[0]:5d}  oracle {n[1]:5d}  diff {' '.join(diffs)}")
        if pics:
            rows_out.append(pics)
    cols = max((len(r) for r in rows_out), default=0)
    SW = cols * (2 * W + 16)
    sheet = bytearray(3 * SW * H * len(rows_out))
    for ri, pics in enumerate(rows_out):
        for ci, pair in enumerate(pics):
            for side, (w, h, rows) in enumerate(pair):
                x0 = ci * (2 * W + 16) + side * (W + 4)
                for y in range(H):
                    sy = y * h // H
                    for x in range(W):
                        sx = x * w // W
                        o = 3 * ((ri * H + y) * SW + x0 + x)
                        sheet[o:o + 3] = rows[sy][3 * sx:3 * sx + 3]
    if rows_out:
        write_png(a.out, SW, H * len(rows_out), sheet)


if __name__ == "__main__":
    main()

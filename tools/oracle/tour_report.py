#!/usr/bin/env python3
"""Per-level table for a whole-game tour (docs/whole-game-host.md): for each
level, when the host and the emulator played it (first vblank in gamestate
0), whether each drew a picture when the tour left it (tour-K-LEVEL.png,
tour_pairs.py's measures), and the oracle function replay of the samples
the emulator took there (replay.txt lines, matched to levels by the
emulator's trace). Also one line each for the cutscenes (gamestate 14) and
credits (15), with their lengths in vblanks.

    tools/oracle/tour_report.py HOSTDIR ORACLEDIR [--host-log LOG]
"""
import argparse
import bisect
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tour_pairs import blocks, lit, shots  # noqa: E402
from contact_sheet import read_png  # noqa: E402

NAMES = {
    10: "Artisans", 11: "Stone Hill", 12: "Dark Hollow", 13: "Town Square", 14: "Toasty",
    15: "Sunny Flight", 20: "Peace Keepers", 21: "Dry Canyon", 22: "Cliff Town",
    23: "Ice Cavern", 24: "Doctor Shemp", 25: "Night Flight", 30: "Magic Crafters",
    31: "Alpine Ridge", 32: "High Caves", 33: "Wizard Peak", 34: "Blowhard",
    35: "Crystal Flight", 40: "Beast Makers", 41: "Terrace Village", 42: "Misty Bog",
    43: "Tree Tops", 44: "Metalhead", 45: "Wild Flight", 50: "Dream Weavers",
    51: "Dark Passage", 52: "Lofty Castle", 53: "Haunted Towers", 54: "Jacques",
    55: "Icy Flight", 60: "Gnasty's World", 61: "Gnorc Cove", 62: "Twilight Harbor",
    63: "Gnasty Gnorc", 64: "Gnasty's Loot",
}


def trace(path):
    with open(path, newline="") as f:
        return [(int(r["vblank"]), int(r["gamestate"]), int(r["level"]))
                for r in csv.DictReader(f, delimiter="\t")]


def segments(rows):
    """(gamestate, level, first vblank, last vblank) runs."""
    out = []
    for v, gs, lv in rows:
        if out and out[-1][0] == gs and out[-1][1] == lv:
            out[-1][3] = v
        else:
            out.append([gs, lv, v, v])
    return out


def first_play(segs):
    seen = {}
    for gs, lv, a, _ in segs:
        if gs == 0 and lv not in seen:
            seen[lv] = a
    return seen


def replay_by_level(replay, rows):
    vbs = [r[0] for r in rows]
    res = {}
    for line in open(replay):
        m = re.match(r"(\w+) \d+ (\d+) (PASS|FAIL|CRASH)\b", line)
        if not m:
            continue
        i = bisect.bisect_right(vbs, int(m.group(2))) - 1
        lv = rows[i][2] if i >= 0 else 0
        c = res.setdefault(lv, [0, 0])
        c[1] += 1
        c[0] += m.group(3) == "PASS"
    return res


def shot_info(d):
    out = {}
    for k, (lv, p) in shots(d).items():
        w, h, rows = read_png(p)
        out.setdefault(lv, (lit(w, h, rows), blocks(w, h, rows)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("oracle")
    a = ap.parse_args()
    hrows, orows = trace(os.path.join(a.host, "trace.tsv")), trace(os.path.join(a.oracle, "trace.tsv"))
    hseg, oseg = segments(hrows), segments(orows)
    hplay, oplay = first_play(hseg), first_play(oseg)
    rep = os.path.join(a.oracle, "replay.txt")
    rbl = replay_by_level(rep, orows) if os.path.exists(rep) else {}
    hs, os_ = shot_info(a.host), shot_info(a.oracle)
    print("| Level | Name | Host plays at | Oracle plays at | Host drew | Oracle drew | Picture difference | Oracle replay in level |")
    print("|---|---|---|---|---|---|---|---|")
    for lv in sorted(NAMES):
        h, o = hs.get(lv), os_.get(lv)
        diff = "-"
        if h and o:
            diff = f"{sum(abs(x - y) for x, y in zip(h[1], o[1])) / len(h[1]):.1f}"
        r = rbl.get(lv)
        print(f"| {lv} | {NAMES[lv]} | {hplay.get(lv, 'not reached')} | {oplay.get(lv, 'not reached')} | "
              f"{f'{h[0]:.0%}' if h else '-'} | {f'{o[0]:.0%}' if o else '-'} | {diff} | "
              f"{f'{r[0]}/{r[1]}' if r else '-'} |")
    print()
    print("| Sequence | Host vblanks | Oracle vblanks |")
    print("|---|---|---|")
    h0, o0 = min(hplay.values()), min(oplay.values())  # after the intro cutscene
    for gs, what in ((14, "cutscene"), (15, "credits")):
        hl = [(lv, b - a + 1) for g, lv, a, b in hseg if g == gs and a > h0]
        ol = [(lv, b - a + 1) for g, lv, a, b in oseg if g == gs and a > o0]
        for i in range(max(len(hl), len(ol))):
            hv = f"{hl[i][1]} (level {hl[i][0]})" if i < len(hl) else "-"
            ov = f"{ol[i][1]} (level {ol[i][0]})" if i < len(ol) else "-"
            print(f"| {what} {i + 1} | {hv} | {ov} |")


if __name__ == "__main__":
    main()

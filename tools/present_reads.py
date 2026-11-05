#!/usr/bin/env python3
"""Sum up stage 7a's read audit (the linux32 host's --present-reads FILE,
src/platform/linux32/readset.c, D104).

Per gamestate: the game's globals (named from tools/layout/psx.map) that the
in-between pass reads, split by how often the logic pass and the next update
change them before the next draw (those a presenter's snapshot must hold);
and, for the other regions (mobys, particles, level data, ...), the pages
read and the bytes of them changed.

    tools/present_reads.py FILE [FILE...] [--map tools/layout/psx.map]
"""
import argparse
import bisect
import collections
import os
import re

GS = ["Playing", "LevelTransition", "PauseMenu", "InventoryMenu", "Respawn", "GameOver",
      "OldDragon", "FlightResults", "Dragon", "EntranceAnimation", "ExitLevel", "Fairy",
      "Balloonist", "TitleScreen", "Cutscene", "Credits"]


def load_map(path):
    syms = []
    for line in open(path, errors="replace"):
        m = re.match(r"^\s+0x(8[0-9a-f]{7})\s+([A-Za-z_]\w*)\s*$", line)
        if m:
            syms.append((int(m.group(1), 16), m.group(2)))
    syms.sort()
    return [a for a, _ in syms], [n for _, n in syms]


def name(addrs, names, a):
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0:
        return "%08x" % a
    off = a - addrs[i]
    return names[i] if off == 0 else "%s+0x%x" % (names[i], off)


def symbols(addrs, names, lo, hi):
    """The symbols a word range [lo, hi) touches."""
    i = max(bisect.bisect_right(addrs, lo) - 1, 0)
    out = []
    while i < len(addrs) and addrs[i] < hi:
        if i + 1 >= len(addrs) or addrs[i + 1] > lo:
            out.append(names[i])
        i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--map", default=os.path.join(os.path.dirname(__file__), "layout/psx.map"))
    ap.add_argument("--top", type=int, default=40, help="symbols listed per gamestate")
    args = ap.parse_args()
    addrs, names = load_map(args.map)

    samples = collections.Counter()
    sym_read = collections.defaultdict(collections.Counter)     # gs -> symbol -> samples read
    sym_changed = collections.defaultdict(collections.Counter)  # gs -> symbol -> samples changed
    pages = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    for path in args.files:
        gs = None
        seen_r, seen_c = set(), set()
        for line in open(path):
            f = line.split()
            if not f:
                continue
            if f[0] == "sample":
                gs = int(f[2])
                samples[gs] += 1
                seen_r, seen_c = set(), set()
                continue
            kind, lo, hi, region = f[0], int(f[1], 16), int(f[2], 16), f[3]
            if kind == "F":
                for s in symbols(addrs, names, lo, hi):
                    if s not in seen_r:
                        seen_r.add(s)
                        sym_read[gs][s] += 1
                    if f[4] == "changed" and s not in seen_c:
                        seen_c.add(s)
                        sym_changed[gs][s] += 1
            elif kind == "P":
                pages[gs][region]["read " + f[4]] += (hi - lo) // 4096
            elif kind == "C":
                pages[gs][region]["bytes changed"] += hi - lo
            elif kind == "W":
                pages[gs][region]["written first"] += (hi - lo) // 4096

    for gs in sorted(samples):
        n = samples[gs]
        label = GS[gs] if 0 <= gs < len(GS) else str(gs)
        print("## gamestate %d (%s): %d samples" % (gs, label, n))
        reads = sym_read[gs]
        ch = sym_changed[gs]
        always = [s for s in reads if ch[s] == n]
        some = [s for s in reads if 0 < ch[s] < n]
        never = [s for s in reads if ch[s] == 0]
        print("data symbols read: %d; changed before the next draw in every sample %d, in some %d, "
              "never %d" % (len(reads), len(always), len(some), len(never)))
        for title, lst in (("changed every sample", always), ("changed in some", some)):
            lst = sorted(lst, key=lambda s: (-ch[s], s))
            shown = ", ".join("%s (%d/%d)" % (s, ch[s], n) if ch[s] < n else s
                              for s in lst[:args.top])
            more = " ... +%d" % (len(lst) - args.top) if len(lst) > args.top else ""
            print("  %s: %s%s" % (title, shown or "-", more))
        print("other regions, per sample (pages of 4 KB; bytes):")
        for region in sorted(pages[gs]):
            c = pages[gs][region]
            print("  %-10s " % region + ", ".join("%s %.1f" % (k, c[k] / n) for k in sorted(c)))
        print()


if __name__ == "__main__":
    main()

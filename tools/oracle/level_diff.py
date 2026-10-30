#!/usr/bin/env python3
"""Per-visit comparison of two game-state traces from a level tour (the
host's --tour and capture.lua's ORACLE_TOUR, docs/whole-game-host.md).

A visit is a stretch of rows with one level and one gamestate class
(play = 0; also 14 cutscene and 15 credits). Visits are paired in order, so
the n-th Artisans visit on one side meets the n-th on the other. Inside a
pair, rows are keyed by the game tick counted from the visit's first row
(the last row of each tick wins, as trace_diff.py --key tick does), since
load times differ. Prints one line per visit: level, gamestate, ticks
compared, and the first divergent tick offset and fields, if any.

    tools/oracle/level_diff.py HOST.tsv ORACLE.tsv [--fields x,y,z,...]
        [--markdown]
"""
import argparse
import csv
import sys

DEFAULT = "x,y,z,state,health,gems,dragons,lives"
CLASSES = {"0": "play", "14": "cutscene", "15": "credits"}


def visits(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    out, cur = [], None
    for r in rows:
        cls = CLASSES.get(r["gamestate"])
        key = (r["level"], cls)
        if cls is None:
            # pause, inventory, loading: ends a play visit only on a level
            # change, so pausing in place doesn't split it
            if cur and cur["key"][1] == "play" and r["level"] != cur["key"][0]:
                cur = None
            continue
        if cur is None or cur["key"] != key:
            cur = {"key": key, "t0": int(r["tick"]), "rows": {}, "vblank": int(r["vblank"])}
            out.append(cur)
        cur["rows"][int(r["tick"]) - cur["t0"]] = r
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--fields", default=DEFAULT)
    ap.add_argument("--markdown", action="store_true")
    args = ap.parse_args()
    fields = args.fields.split(",")
    va, vb = visits(args.a), visits(args.b)
    used, bad = set(), 0
    if args.markdown:
        print("| Level | Gamestate | Host vblank | Oracle vblank | Ticks compared | Result |")
        print("|---|---|---|---|---|---|")
    for x in va:
        y = next((v for i, v in enumerate(vb) if i not in used and v["key"] == x["key"]), None)
        if y is None:
            line = (x["key"][0], x["key"][1], x["vblank"], "-", 0, "no oracle visit")
        else:
            used.add(vb.index(y))
            shared = sorted(set(x["rows"]) & set(y["rows"]))
            res = "identical"
            for t in shared:
                d = [c for c in fields if x["rows"][t][c] != y["rows"][t][c]]
                if d:
                    res = f"tick +{t}: " + ", ".join(
                        f"{c} {x['rows'][t][c]} != {y['rows'][t][c]}" for c in d)
                    bad += 1
                    break
            line = (x["key"][0], x["key"][1], x["vblank"], y["vblank"], len(shared), res)
        if args.markdown:
            print("| " + " | ".join(str(c) for c in line) + " |")
        else:
            print("level %-3s %-8s host@%-7s oracle@%-7s %5s ticks  %s" % line)
    for i, v in enumerate(vb):
        if i not in used:
            print(f"oracle-only visit: level {v['key'][0]} {v['key'][1]} at vblank {v['vblank']}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""First divergence between two per-vblank game-state traces (TSV with a
header row, as `spyro --trace` writes and the oracle's Lua script will).
Rows are matched by the `vblank` column, or with --key tick by the game
tick (the last row of each tick: the emulator and the host don't agree on
vblanks, since the PS1 drops frames and loads take different times);
--where COL=VAL,... keeps only matching rows (say gamestate=0,level=11 for
one attract demo). Columns only in one file are ignored. Exit 0 when the
shared columns agree on every shared key.

    tools/oracle/trace_diff.py A.tsv B.tsv [--key vblank|tick] [--where COL=VAL,...]
        [--from V] [--to V] [--ignore COL,...]
"""
import argparse
import csv
import sys


def load(path, key, where):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    rows = [r for r in rows if all(r.get(c) == v for c, v in where)]
    return {int(r[key]): r for r in rows}  # later rows win: the last of each key


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=1 << 30)
    ap.add_argument("--ignore", default="", help="columns to skip, comma separated")
    ap.add_argument("--key", default="vblank", choices=("vblank", "tick"))
    ap.add_argument("--where", default="", help="COL=VAL filters, comma separated")
    args = ap.parse_args()
    where = [w.split("=", 1) for w in filter(None, args.where.split(","))]
    a, b = load(args.a, args.key, where), load(args.b, args.key, where)
    ignore = set(filter(None, args.ignore.split(",")))
    ra, rb = next(iter(a.values()), {}), next(iter(b.values()), {})
    cols = [c for c in ra if c in rb and c not in ("vblank", args.key) and c not in ignore]
    shared = sorted(v for v in a if v in b and args.lo <= v <= args.hi)
    for v in shared:
        diff = [c for c in cols if a[v][c] != b[v][c]]
        if diff:
            print(f"first divergence at {args.key} {v}:")
            for c in diff:
                print(f"  {c}: {a[v][c]} != {b[v][c]}")
            prev = [p for p in shared if p < v][-3:]
            for p in prev:
                print(f"  ({p}: " + " ".join(f"{c}={a[p][c]}" for c in diff) + ")")
            return 1
    print(f"{len(shared)} {args.key}s, {len(cols)} columns: identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())

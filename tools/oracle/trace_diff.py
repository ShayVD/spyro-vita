#!/usr/bin/env python3
"""First divergence between two per-vblank game-state traces (TSV with a
header row, as `spyro --trace` writes and the oracle's Lua script will).
Rows are matched by the `vblank` column; columns only in one file are
ignored. Exit 0 when the shared columns agree on every shared vblank.

    tools/oracle/trace_diff.py A.tsv B.tsv [--from V] [--to V] [--ignore COL,...]
"""
import argparse
import csv
import sys


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    return {int(r["vblank"]): r for r in rows}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=1 << 30)
    ap.add_argument("--ignore", default="", help="columns to skip, comma separated")
    args = ap.parse_args()
    a, b = load(args.a), load(args.b)
    ignore = set(filter(None, args.ignore.split(",")))
    ra, rb = next(iter(a.values()), {}), next(iter(b.values()), {})
    cols = [c for c in ra if c in rb and c != "vblank" and c not in ignore]
    shared = sorted(v for v in a if v in b and args.lo <= v <= args.hi)
    for v in shared:
        diff = [c for c in cols if a[v][c] != b[v][c]]
        if diff:
            print(f"first divergence at vblank {v}:")
            for c in diff:
                print(f"  {c}: {a[v][c]} != {b[v][c]}")
            prev = [p for p in shared if p < v][-3:]
            for p in prev:
                print(f"  ({p}: " + " ".join(f"{c}={a[p][c]}" for c in diff) + ")")
            return 1
    print(f"{len(shared)} vblanks, {len(cols)} columns: identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Summarise `spyro --replay` output: per function, samples passed, failed,
crashed; for failures, the differing RAM words named by the nearest psx.map
symbol at or below them. Usage: replay_summary.py REPLAY.txt [--map psx.map]"""
import argparse
import bisect
import collections
import re

ap = argparse.ArgumentParser()
ap.add_argument("replay")
ap.add_argument("--map", default="build/s1-matching/build/psx.map")
a = ap.parse_args()

syms = []
for line in open(a.map):
    m = re.match(r"\s+0x(80[0-9a-f]{6})\s+(\w+)\s*$", line)
    if m:
        syms.append((int(m.group(1), 16), m.group(2)))
syms.sort()
addrs = [s[0] for s in syms]


def name(addr):
    i = bisect.bisect_right(addrs, addr) - 1
    if i < 0:
        return f"{addr:08x}"
    base, n = syms[i]
    return f"{n}+0x{addr - base:x}" if addr != base else n


res = collections.defaultdict(collections.Counter)
where = collections.defaultdict(collections.Counter)
cur = None
for line in open(a.replay):
    m = re.match(r"(\w+) (\d+) (\d+) (PASS|FAIL|CRASH|SKIP|NOEXIT)", line)
    if m:
        cur = m.group(1)
        res[cur][m.group(4)] += 1
        if m.group(4) == "FAIL":
            r = re.search(r"v0 (\w+)/(\w+)(\S*) v1 (\w+)/(\w+)(\S*) ram", line)
            if r.group(1) != r.group(2) and not r.group(3):
                where[cur]["v0"] += 1
            if r.group(4) != r.group(5) and not r.group(6):
                where[cur]["v1"] += 1
        continue
    m = re.match(r"\s+ram (\w+) emu", line)
    if m and cur:
        where[cur]["ram " + name(int(m.group(1), 16))] += 1
        continue
    m = re.match(r"\s+(scratch|gte) (\w+)", line)
    if m and cur:
        where[cur][m.group(1) + " " + m.group(2)] += 1

tot = collections.Counter()
for f in sorted(res):
    c = res[f]
    tot.update(c)
    print(f"{f:40s} " + " ".join(f"{k} {c[k]}" for k in ("PASS", "FAIL", "CRASH", "SKIP", "NOEXIT") if c[k]))
    for w, n in where[f].most_common(6):
        print(f"    {n:3d}x {w}")
print("total: " + " ".join(f"{k} {v}" for k, v in sorted(tot.items())))
print(f"functions: {len(res)}, all samples pass: {sum(1 for f in res if set(res[f]) == {'PASS'})}")

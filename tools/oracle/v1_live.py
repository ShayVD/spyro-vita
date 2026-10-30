#!/usr/bin/env python3
"""Is v1 live after calls to a function? For each `jal NAME` in the decomp's
asm, scan the instructions after the delay slot: "dead" if v1 is written
before it's read, "READ" if it's read first, "unknown" if a branch, jump or
the end of the function comes first. C callers can't see v1. Used to justify
"v1 NAME" lines in tools/oracle/ignore_ranges.txt.

    v1_live.py NAME [--asm third_party/spyro-1/asm]"""
import argparse
import glob
import re

ap = argparse.ArgumentParser()
ap.add_argument("name")
ap.add_argument("--asm", default="third_party/spyro-1/asm")
a = ap.parse_args()

INS = re.compile(r"\*/\s+([a-z0-9.]+)\s*(.*)$")
JUMPS = re.compile(r"^(j|jal|jr|jalr|b\w*)$")
res = {"dead": 0, "READ": 0, "unknown": 0}
for path in glob.glob(f"{a.asm}/**/*.s", recursive=True):
    lines = [l for l in open(path, errors="ignore")]
    for i, l in enumerate(lines):
        m = INS.search(l)
        if not m or m.group(1) != "jal" or m.group(2).strip() != a.name:
            continue
        verdict = "unknown"
        for l2 in lines[i + 2:i + 60]:  # past the delay slot
            if l2.startswith("glabel") or ".size" in l2:
                break
            m2 = INS.search(l2)
            if not m2:
                continue
            op, args = m2.group(1), [x.strip() for x in m2.group(2).split(",")]
            srcs = args[1:] if args and not op.startswith(("s", "b", "j", "mt", "ct")) else args
            if op.startswith(("sw", "sh", "sb")):
                srcs = args
            if any("$v1" in s for s in srcs):
                verdict = "READ"
                break
            if args and args[0] == "$v1":
                verdict = "dead"
                break
            if JUMPS.match(op):
                break
        res[verdict] += 1
        if verdict != "dead":
            print(f"{verdict}: {path}:{i + 1}")
print(f"{a.name}: {res['dead']} call sites write v1 first, {res['READ']} read it, {res['unknown']} unknown")

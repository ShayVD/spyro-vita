#!/usr/bin/env python3
"""Fill docs/translation-log.md's "Verified by" column for mips2c functions
from oracle replay results (spyro --replay output, tools/oracle/replay.sh).

    update_translation_log.py REPLAY.txt[=LABEL]... [--log docs/translation-log.md]

A function whose replayed samples all pass gets "Oracle replay: N/N samples
match PCSX-Redux (LABEL...)", one with failures says so; functions with no
samples keep their text. Only rows whose method starts with "mips2c" change."""
import argparse
import collections
import re

ap = argparse.ArgumentParser()
ap.add_argument("replays", nargs="+")
ap.add_argument("--log", default="docs/translation-log.md")
a = ap.parse_args()

res = collections.defaultdict(collections.Counter)
where = collections.defaultdict(list)
for spec in a.replays:
    path, _, label = spec.partition("=")
    label = label or path
    for line in open(path):
        m = re.match(r"(\w+) \d+ \d+ (PASS|FAIL|CRASH)\b", line)
        if m:
            res[m.group(1)][m.group(2)] += 1
            if label not in where[m.group(1)]:
                where[m.group(1)].append(label)

out = []
for line in open(a.log):
    m = re.match(r"\| `(\w+)` \| (mips2c[^|]*)\|([^|]*)\|(.*)$", line)
    if m and m.group(1) in res:
        c = res[m.group(1)]
        n = sum(c.values())
        runs = ", ".join(where[m.group(1)])
        if c["PASS"] == n:
            v = f" Oracle replay: {n}/{n} samples match PCSX-Redux ({runs}). "
        else:
            v = f" Oracle replay: {c['PASS']}/{n} samples match PCSX-Redux, {n - c['PASS']} differ ({runs}). "
        also = re.search(r"Also differential test.*?(?=\s*$)", m.group(3))
        if also:  # tests/m2c_diff results are another source: keep them
            v += also.group(0) + " "
        line = f"| `{m.group(1)}` | {m.group(2)}|{v}|{m.group(4)}\n"
    out.append(line)
open(a.log, "w").write("".join(out))
print(f"{len(res)} functions with samples")

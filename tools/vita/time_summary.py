#!/usr/bin/env python3
"""Average the "time" lines of a Vita spyro.log (src/platform/vita/prof.c):
per-vblank milliseconds by part over the whole run, first report skipped.
  time_summary.py spyro.log [...]"""
import re
import sys

for path in sys.argv[1:]:
    rows = []
    for line in open(path, errors="replace"):
        m = re.search(r"time (\d+): per vblank ms (.*?);", line)
        if m:
            rows.append(dict((k, float(v)) for k, v in re.findall(r"(\w+) ([\d.]+)", m.group(2))))
    rows = rows[1:]
    if not rows:
        print(f"{path}: no time lines")
        continue
    avg = {k: sum(r[k] for r in rows) / len(rows) for k in rows[0]}
    busy = sum(v for k, v in avg.items() if k != "wait")
    print(f"{path}: {len(rows)} reports; " + " ".join(f"{k} {v:.2f}" for k, v in avg.items())
          + f"; busy {busy:.2f} ms per vblank")

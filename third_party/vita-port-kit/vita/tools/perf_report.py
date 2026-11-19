#!/usr/bin/env python3
"""Per-frame work from a log's perfcount lines (vita/runtime/perfcount), for one run or two compared.
  perf_report.py RUN.log [--min-frame N] [--max-frame N] [--prefix vpk-perfcount] [--json]
  perf_report.py BASE.log NEW.log [--noise 0.02] [...]
Lower is better for every counter (they count work). Compared: 'worse' when NEW's per-frame mean is more than
NOISE above BASE's, 'better' when more than NOISE below, 'new' / 'gone' for a counter only one run has.
Exit 0; 1 when a counter is worse; 2 when a log has no lines in the range or cannot be read.
A change of exactly NOISE counts as same. A counter at 0 in BASE and above 0 in NEW is worse (change null / 'new>0').
"""
import argparse
import json
import re
import sys


def read(path, prefix, lo=0, hi=None):
    """(frames, {name: per-frame mean}) over the windows wholly inside [lo, hi]."""
    pat = re.compile(r"(?:^|\s)" + re.escape(prefix) + r": w (\d+) (\d+) (\d+)((?: [\w.]+=\d+)*)\s*$")
    frames = 0
    totals = {}
    with open(path, errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if not m:
                continue
            first, last, n = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if first < lo or (hi is not None and last > hi):
                continue
            frames += n
            for kv in m.group(4).split():
                k, v = kv.split("=")
                totals[k] = totals.get(k, 0) + int(v)
    if not frames:
        return 0, {}
    return frames, {k: v / frames for k, v in totals.items()}


def compare(base, new, prefix, noise=0.02, lo=0, hi=None):
    bf, b = read(base, prefix, lo, hi)
    nf, n = read(new, prefix, lo, hi)
    if not bf or not nf:
        raise ValueError(f"no {prefix} lines in range in {base if not bf else new}")
    rows = []
    for name in list(b) + [k for k in n if k not in b]:
        if name not in n:
            rows.append({"name": name, "base": b[name], "new": None, "change": None, "verdict": "gone"})
            continue
        if name not in b:
            rows.append({"name": name, "base": None, "new": n[name], "change": None, "verdict": "new"})
            continue
        bv, nv = b[name], n[name]
        # exact-noise changes are 'same' whatever the float rounding: compare against noise*bv with a tiny slack
        slack = 1e-9 * max(abs(bv), abs(nv))
        delta, limit = nv - bv, noise * bv
        verdict = "worse" if delta > limit + slack else "better" if delta < -limit - slack else "same"
        change = delta / bv if bv else (0.0 if nv == 0 else None)  # None: from 0 to above 0, no ratio
        rows.append({"name": name, "base": bv, "new": nv, "change": change, "verdict": verdict})
    return bf, nf, rows


def fmt(v):
    return "-" if v is None else f"{v:.3f}"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--prefix", default="vpk-perfcount")
    ap.add_argument("--min-frame", type=int, default=0)
    ap.add_argument("--max-frame", type=int)
    ap.add_argument("--noise", type=float, default=0.02)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if len(a.logs) > 2:
        ap.error("one log, or a base and a new")
    if len(a.logs) == 1:
        try:
            frames, means = read(a.logs[0], a.prefix, a.min_frame, a.max_frame)
        except OSError as e:
            print(f"perf_report: {e}", file=sys.stderr)
            return 2
        if not frames:
            print(f"perf_report: no {a.prefix} lines in range in {a.logs[0]}", file=sys.stderr)
            return 2
        if a.json:
            print(json.dumps({"frames": frames, "means": means}, indent=1, sort_keys=True))
        else:
            print(f"{frames} frames")
            for k in sorted(means):
                print(f"  {k:24} {means[k]:.3f}")
        return 0
    try:
        bf, nf, rows = compare(a.logs[0], a.logs[1], a.prefix, a.noise, a.min_frame, a.max_frame)
    except (OSError, ValueError) as e:
        print(f"perf_report: {e}", file=sys.stderr)
        return 2
    if a.json:
        print(json.dumps({"base_frames": bf, "new_frames": nf, "rows": rows}, indent=1))
    else:
        print(f"base {bf} frames, new {nf} frames")
        for r in rows:
            if r["change"] is not None:
                ch = f"{r['change'] * 100:+.1f}%"
            else:
                ch = "new>0" if r["verdict"] == "worse" else "-"
            print(f"  {r['name']:24} {fmt(r['base']):>10} {fmt(r['new']):>10} {ch:>8}  {r['verdict']}")
    return 1 if any(r["verdict"] == "worse" for r in rows) else 0


if __name__ == "__main__":
    sys.exit(main())

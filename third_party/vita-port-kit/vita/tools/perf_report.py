#!/usr/bin/env python3
"""Per-frame work from a log's perfcount lines (targets/vita/runtime/perfcount), for one run or two compared.
  perf_report.py RUN.log [--min-frame N] [--max-frame N] [--prefix vpk-perfcount] [--json]
  perf_report.py BASE.log NEW.log [--noise 0.02] [--counters a,b] [--ignore c,d] [--rerun BASE2.log ...] [...]
Lower is better for every counter (they count work). Compared: 'worse' when NEW's per-frame mean is more than
NOISE above BASE's, 'better' when more than NOISE below, 'new' / 'gone' for a counter only one run has.
Exit 0; 1 when a counter is worse; 2 when a log has no lines in the range or cannot be read.
A change of exactly NOISE counts as same. A counter at 0 in BASE and above 0 in NEW is worse (change null / 'new>0').
--counters judges only the named counters and --ignore judges all but the named ones; every other counter is still
shown, with verdict 'info', and never makes the exit 1. A judged counter only one log has stays 'new' / 'gone'.
A name may be a glob ('vc.rec.*', 'vcp.*'): timing-dependent counters that move run to run on the same VPK (work
done while another thread is idle, say) are ignored by pattern (spyro-vita D208, D210).
--rerun adds runs of BASE's own VPK (same-VPK noise): a counter is 'worse' only when NEW is worse than every base
run, 'better' only when better than every one, else 'same'; the row's 'spread' is the base runs' max over min - 1.

  perf_report.py --spread RUN1 RUN2 [RUN3 ...] [--min-frame N] [--max-frame N] [--noise 0.02] [--counters/--ignore]
                 [--ignore-list] [--json]
Same-VPK noise of every counter before it gates: two or more runs of one build on one route (a run folder, read as
its run.log, or a log). Each counter's spread (max over min - 1 of the per-frame means) over the range and in each
window all runs have. A counter 'moves' when the gate would call one run worse than another over the range (more
than NOISE apart; 0 in one run and above 0 in another moves too). --counters / --ignore as above: a counter not
judged shows as 'info' and never moves. --ignore-list prints only the counters that move, as an '"ignore": [...]'
line for config.perf_gate or a config.benchmark[] scene (add them to its list). Exit 0 when none moves, 1 when one
does, 2 when a run has no lines in the range or cannot be read.
"""
import argparse
import fnmatch
import json
import os
import re
import sys


def windows(path, prefix, lo=0, hi=None):
    """[(first, last, frames, {name: count})] for the windows wholly inside [lo, hi], in log order."""
    pat = re.compile(r"(?:^|\s)" + re.escape(prefix) + r": w (\d+) (\d+) (\d+)((?: [\w.]+=\d+)*)\s*$")
    out = []
    with open(path, errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if not m:
                continue
            first, last, n = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if first < lo or (hi is not None and last > hi):
                continue
            counts = {}
            for kv in m.group(4).split():
                k, v = kv.split("=")
                counts[k] = counts.get(k, 0) + int(v)
            out.append((first, last, n, counts))
    return out


def read(path, prefix, lo=0, hi=None):
    """(frames, {name: per-frame mean}) over the windows wholly inside [lo, hi]."""
    frames = 0
    totals = {}
    for _, _, n, counts in windows(path, prefix, lo, hi):
        frames += n
        for k, v in counts.items():
            totals[k] = totals.get(k, 0) + v
    if not frames:
        return 0, {}
    return frames, {k: v / frames for k, v in totals.items()}


def verdict(bv, nv, noise):
    # exact-noise changes are 'same' whatever the float rounding: compare against noise*bv with a tiny slack
    slack = 1e-9 * max(abs(bv), abs(nv))
    delta, limit = nv - bv, noise * bv
    return "worse" if delta > limit + slack else "better" if delta < -limit - slack else "same"


def compare(base, new, prefix, noise=0.02, lo=0, hi=None, counters=None, ignore=(), reruns=()):
    """counters: the names to judge (None: all); ignore: names never judged. The rest get verdict 'info'.
    reruns: more logs of BASE's VPK; a verdict must hold against every base run."""
    bf, b = read(base, prefix, lo, hi)
    nf, n = read(new, prefix, lo, hi)
    if not bf or not nf:
        raise ValueError(f"no {prefix} lines in range in {base if not bf else new}")
    extra = []
    for r in reruns:
        rf, rm = read(r, prefix, lo, hi)
        if not rf:
            raise ValueError(f"no {prefix} lines in range in {r}")
        extra.append(rm)
    rows = []
    for name in list(b) + [k for k in n if k not in b]:
        if name not in n:
            rows.append({"name": name, "base": b[name], "new": None, "change": None, "verdict": "gone"})
            continue
        if name not in b:
            rows.append({"name": name, "base": None, "new": n[name], "change": None, "verdict": "new"})
            continue
        bv, nv = b[name], n[name]
        v = verdict(bv, nv, noise)
        change = (nv - bv) / bv if bv else (0.0 if nv == 0 else None)  # None: from 0 to above 0, no ratio
        row = {"name": name, "base": bv, "new": nv, "change": change, "verdict": v}
        if extra:
            bases = [bv] + [m[name] for m in extra if name in m]
            vs = {verdict(x, nv, noise) for x in bases}
            row["verdict"] = vs.pop() if len(vs) == 1 else "same"
            row["spread"] = max(bases) / min(bases) - 1 if min(bases) else None
        rows.append(row)
    for r in rows:
        if (counters is not None and not matches(r["name"], counters)) or matches(r["name"], ignore):
            r["verdict"] = "info"
    return bf, nf, rows


def run_log(path):
    """a run folder's run.log (the runner contract's name), or the path itself"""
    return os.path.join(path, "run.log") if os.path.isdir(path) else path


def spread_of(values):
    """max over min - 1; 0.0 when all are 0; None when one is 0 and another above 0 (no ratio)"""
    lo, hi = min(values), max(values)
    return (hi / lo - 1) if lo else (0.0 if hi == 0 else None)


def rank(s):
    """a spread for ordering: no ratio (0 to above 0) is the widest"""
    return float("inf") if s is None else s


def spread(runs, prefix, noise=0.02, lo=0, hi=None, counters=None, ignore=()):
    """Run-to-run spread of every counter over two or more runs of one build.
    Returns {"frames": [per run], "windows": [[first, last], ...] (those every run has), "moved": [[names that
    move in that window], ...], "rows": [...]}; a row has name, runs (how many runs have it), min, max, spread,
    worst ({"window": [first, last], "spread": s} or None), windows ([spread per common window; None: no ratio])
    and verdict: 'moves' (the gate would call one run worse than another over the range), 'steady', 'partial' (not
    every run has it: no spread, no windows; the gate shows it as new / gone) or 'info' (not judged)."""
    if len(runs) < 2:
        raise ValueError("--spread needs two or more runs")
    means, wins, frames = [], [], []
    for r in runs:
        path = run_log(r)
        ws = windows(path, prefix, lo, hi)
        if not ws:
            raise ValueError(f"no {prefix} lines in range in {path}")
        f, m = read(path, prefix, lo, hi)
        frames.append(f)
        means.append(m)
        wins.append({(a, b): (n, c) for a, b, n, c in ws})
    common = [w for w in wins[0] if all(w in other for other in wins[1:])]
    order = []
    for m in means:
        order += [k for k in m if k not in order]
    rows = []
    moved = [[] for _ in common]
    for name in order:
        vals = [m[name] for m in means if name in m]
        row = {"name": name, "runs": len(vals), "min": min(vals), "max": max(vals), "spread": None, "worst": None,
               "windows": []}
        judged = not ((counters is not None and not matches(name, counters)) or matches(name, ignore))
        if len(vals) < len(runs):
            row["verdict"] = "partial"
            rows.append(row)
            continue
        row["spread"] = spread_of(vals)
        row["verdict"] = "moves" if verdict(row["min"], row["max"], noise) == "worse" else "steady"
        for i, w in enumerate(common):
            per = [c.get(name, 0) / n if n else 0.0 for n, c in (ws[w] for ws in wins)]
            s = spread_of(per)
            row["windows"].append(s)
            if row["worst"] is None or rank(s) > rank(row["worst"]["spread"]):
                row["worst"] = {"window": list(w), "spread": s}
            if judged and verdict(min(per), max(per), noise) == "worse":
                moved[i].append(name)
        if not judged:
            row["verdict"] = "info"
        rows.append(row)
    return {"frames": frames, "windows": [list(w) for w in common], "moved": moved, "rows": rows}


def pct(s):
    return "0>0" if s is None else f"{s * 100:.1f}%"


def print_spread(doc, runs, noise):
    print(f"{len(runs)} runs, frames {' '.join(str(f) for f in doc['frames'])}, "
          f"{len(doc['windows'])} common windows, noise {noise * 100:g}%")
    if len(set(doc["frames"])) > 1:
        print("  note: the runs cover different frame counts in the range (a run stopped early?)")
    print(f"  {'counter':24} {'min':>10} {'max':>10} {'spread':>8}  {'worst window':>22}  verdict")
    for r in doc["rows"]:
        sp = pct(r["spread"]) if r["verdict"] != "partial" else f"{r['runs']}/{len(runs)}"
        w = r["worst"]
        ww = f"{pct(w['spread'])} @{w['window'][0]}-{w['window'][1]}" if w else "-"
        print(f"  {r['name']:24} {fmt(r['min']):>10} {fmt(r['max']):>10} {sp:>8}  {ww:>22}  {r['verdict']}")
    print("windows (judged counters more than the noise apart in that window alone):")
    rows = {r["name"]: r for r in doc["rows"]}
    for i, (a, b) in enumerate(doc["windows"]):
        if doc["moved"][i]:
            print(f"  {a}-{b}: {', '.join(n + ' ' + pct(rows[n]['windows'][i]) for n in doc['moved'][i])}")
    if not any(doc["moved"]):
        print("  none")


def matches(name, patterns):
    """name is one of patterns, or matches one of them as a glob (fnmatchcase: 'vc.rec.*')"""
    return any(name == p or fnmatch.fnmatchcase(name, p) for p in patterns)


def names(s):
    return {n.strip() for n in s.split(",") if n.strip()}


def fmt(v):
    return "-" if v is None else f"{v:.3f}"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--prefix", default="vpk-perfcount")
    ap.add_argument("--min-frame", type=int, default=0)
    ap.add_argument("--max-frame", type=int)
    ap.add_argument("--noise", type=float, default=0.02)
    ap.add_argument("--counters", type=names, help="judge only these counters (comma-separated, globs allowed); the rest are info")
    ap.add_argument("--ignore", type=names, default=set(), help="never judge these counters (comma-separated, globs allowed)")
    ap.add_argument("--rerun", action="append", default=[], help="another run of BASE's VPK (repeatable)")
    ap.add_argument("--spread", action="store_true", help="run-to-run spread of two or more runs of one build")
    ap.add_argument("--ignore-list", action="store_true", help="with --spread: print only the moving counters as an ignore list")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if a.ignore_list and not a.spread:
        ap.error("--ignore-list needs --spread")
    if a.spread:
        if a.rerun:
            ap.error("--rerun is for a base and a new log; --spread takes every run as a log")
        if len(a.logs) < 2:
            ap.error("--spread needs two or more runs of one build")
        try:
            doc = spread(a.logs, a.prefix, a.noise, a.min_frame, a.max_frame, a.counters, a.ignore)
        except (OSError, ValueError) as e:
            print(f"perf_report: {e}", file=sys.stderr)
            return 2
        moving = [r["name"] for r in doc["rows"] if r["verdict"] == "moves"]
        if a.ignore_list:
            print('"ignore": ' + json.dumps(moving))
        elif a.json:
            print(json.dumps(doc, indent=1))
        else:
            print_spread(doc, a.logs, a.noise)
        return 1 if moving else 0
    if len(a.logs) > 2:
        ap.error("one log, or a base and a new")
    if len(a.logs) == 1:
        if a.rerun:
            ap.error("--rerun needs a base and a new log")
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
        bf, nf, rows = compare(a.logs[0], a.logs[1], a.prefix, a.noise, a.min_frame, a.max_frame, a.counters,
                               a.ignore, a.rerun)
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
                ch = "new>0" if r["base"] == 0 and r["new"] else "-"
            sp = f"  (base spread {r['spread'] * 100:.1f}%)" if r.get("spread") is not None else ""
            print(f"  {r['name']:24} {fmt(r['base']):>10} {fmt(r['new']):>10} {ch:>8}  {r['verdict']}{sp}")
    return 1 if any(r["verdict"] == "worse" for r in rows) else 0


if __name__ == "__main__":
    sys.exit(main())

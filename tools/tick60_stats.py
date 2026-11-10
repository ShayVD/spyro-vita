#!/usr/bin/env python3
"""60 Hz logic against 30 Hz, by statistics (decisions D130; docs/tick60.md).

  tools/tick60_stats.py RUN30 RUN60 [--level N] [--from V] [--to V] [--min-samples N]

Each RUN is a host run's folder with trace.tsv (--trace) and mobys.tsv
(--moby-trace), the same pad script in both. A 60 Hz run can't be compared
tick for tick with a 30 Hz one (docs/logic-60hz-audit.md §4), so this
compares rates per second of game time, per class:

  Spyro: his jumps (state 5 entered: height gained, vblanks in the air,
         distance), his speed walking and running, the gems collected.
  Mobys: per class (all slots, every vblank seen in play), the distance
         moved, the turn, the state and substate changes and the animation
         frames, each per 60 vblanks (a second), and the 60/30 ratio.

A class's ratio near 1.00 moves, turns and changes state at the original's
rate; 2.00 runs twice as fast (a site missed), 0.50 half as fast. Small
samples are noisy: the runs diverge (Spyro's path, the random rolls).
"""
import argparse
import collections
import csv
import math
import os


def read_tsv(path):
    with open(path) as f:
        return list(csv.DictReader(f, delimiter="\t"))


def spyro_stats(rows, lo, hi):
    jumps, speeds, prev = [], [], None
    cur = None
    for r in rows:
        v = int(r["vblank"])
        if v < lo or v > hi or r["gamestate"] != "0":
            prev = None
            cur = None
            continue
        x, y, z, st = int(r["x"]), int(r["y"]), int(r["z"]), int(r["state"])
        if prev:
            px, py, pz, pst, pv = prev
            if st in (1, 2) and pst in (1, 2) and v == pv + 1:
                speeds.append(math.hypot(x - px, y - py))
            if st == 5 and pst != 5:
                cur = {"v0": v, "z0": z, "zmax": z, "x0": x, "y0": y}
            if cur:
                cur["zmax"] = max(cur["zmax"], z)
                if st in (0, 1, 2, 3, 4) and pst not in (0, 1, 2, 3, 4):
                    cur["air"] = v - cur["v0"]
                    cur["dist"] = math.hypot(x - cur["x0"], y - cur["y0"])
                    cur["h"] = cur["zmax"] - cur["z0"]
                    jumps.append(cur)
                    cur = None
        prev = (x, y, z, st, v)
    gems = int(rows[-1]["gems"]) if rows else 0
    return jumps, speeds, gems


def moby_stats(rows, lo, hi, level):
    per = collections.defaultdict(lambda: collections.Counter())
    last = {}
    for r in rows:
        v = int(r["vblank"])
        if v < lo or v > hi or (level is not None and int(r["level"]) != level):
            continue
        slot, cls = int(r["slot"]), int(r["class"])
        cur = (v, cls, int(r["x"]), int(r["y"]), int(r["z"]), int(r["rz"]), int(r["state"]),
               int(r["sub"]), int(r["anim"]), int(r["frame"]))
        p = last.get(slot)
        last[slot] = cur
        c = per[cls]
        if not p or p[1] != cls or p[0] != v - 1:
            continue
        c["n"] += 1
        c["xy"] += math.hypot(cur[2] - p[2], cur[3] - p[3])
        c["z"] += abs(cur[4] - p[4])
        d = (cur[5] - p[5]) & 0xFF
        c["rz"] += min(d, 256 - d)
        c["state"] += cur[6] != p[6]
        c["sub"] += cur[7] != p[7]
        c["anim"] += cur[8] != p[8] or cur[9] != p[9]
    return per


def mean(a):
    return sum(a) / len(a) if a else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run30")
    ap.add_argument("run60")
    ap.add_argument("--level", type=int)
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=1 << 30)
    ap.add_argument("--min-samples", type=int, default=120)
    ap.add_argument("--spyro-paths", action="store_true",
                    help="Spyro's two paths side by side, vblank for vblank (an open-loop pad)")
    a = ap.parse_args()
    out = []
    if a.spyro_paths:
        t30 = {int(r["vblank"]): r for r in read_tsv(os.path.join(a.run30, "trace.tsv"))}
        t60 = {int(r["vblank"]): r for r in read_tsv(os.path.join(a.run60, "trace.tsv"))}
        vs = [v for v in sorted(t30) if a.lo <= v <= a.hi and v in t60 and t30[v]["gamestate"] == "0"
              and t60[v]["gamestate"] == "0"]
        ds, worst = [], (0, None)
        out.append("| vblank | 30 Hz x y z state | 60 Hz x y z state | apart |")
        out.append("|---:|---|---|---:|")
        for v in vs:
            p, q = t30[v], t60[v]
            d = math.dist([int(p[k]) for k in "xyz"], [int(q[k]) for k in "xyz"])
            ds.append(d)
            if d > worst[0]:
                worst = (d, v)
            if v % 60 == 0:
                out.append(f"| {v} | {p['x']} {p['y']} {p['z']} {p['state']} | {q['x']} {q['y']} {q['z']} "
                           f"{q['state']} | {d:.0f} |")
        out.append(f"\n{len(vs)} vblanks in play in both: apart by {mean(ds):.0f} on average, at most "
                   f"{worst[0]:.0f} (vblank {worst[1]}); the 30 Hz run's own step is up to 2 vblanks' move")
        print("\n".join(out))
        return
    sp = {}
    for k, run in (("30", a.run30), ("60", a.run60)):
        sp[k] = spyro_stats(read_tsv(os.path.join(run, "trace.tsv")), a.lo, a.hi)
    out.append("## Spyro\n")
    out.append("| | 30 Hz | 60 Hz | 60/30 |")
    out.append("|---|---:|---:|---:|")

    def row(name, f30, f60, fmt="{:.1f}"):
        ratio = f60 / f30 if f30 else float("nan")
        out.append(f"| {name} | {fmt.format(f30)} | {fmt.format(f60)} | {ratio:.2f} |")

    j30, j60 = sp["30"][0], sp["60"][0]
    row("jumps", len(j30), len(j60), "{:.0f}")
    row("height gained (mean)", mean([j["h"] for j in j30]), mean([j["h"] for j in j60]))
    row("vblanks in the air (mean)", mean([j["air"] for j in j30]), mean([j["air"] for j in j60]))
    row("distance (mean)", mean([j["dist"] for j in j30]), mean([j["dist"] for j in j60]))
    row("speed walking/running, per vblank", mean(sp["30"][1]), mean(sp["60"][1]))
    row("gems at the end", sp["30"][2], sp["60"][2], "{:.0f}")
    m = {k: moby_stats(read_tsv(os.path.join(run, "mobys.tsv")), a.lo, a.hi, a.level)
         for k, run in (("30", a.run30), ("60", a.run60))}
    out.append("\n## Mobys, per class (per 60 vblanks of a moby seen; ratio 60/30)\n")
    out.append("| class | samples 30/60 | moved | z | turn | state | substate | anim frames |")
    out.append("|---:|---:|---|---|---|---|---|---|")
    for cls in sorted(set(m["30"]) | set(m["60"])):
        c30, c60 = m["30"].get(cls, collections.Counter()), m["60"].get(cls, collections.Counter())
        if c30["n"] < a.min_samples or c60["n"] < a.min_samples:
            continue
        cells = []
        for key in ("xy", "z", "rz", "state", "sub", "anim"):
            r30 = c30[key] * 60 / c30["n"]
            r60 = c60[key] * 60 / c60["n"]
            if r30 == 0 and r60 == 0:
                cells.append("–")
            else:
                cells.append(f"{r30:.1f} / {r60:.1f} ({r60 / r30:.2f})" if r30 else f"0 / {r60:.1f}")
        out.append(f"| {cls} | {c30['n']}/{c60['n']} | " + " | ".join(cells) + " |")
    print("\n".join(out))


if __name__ == "__main__":
    main()

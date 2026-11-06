#!/usr/bin/env python3
"""Does 60 fps hold? Per level, from a Vita spyro.log's 300-vblank windows
(decisions D81). For each window:
  late     the "vblank N:" line's vblanks late (of 300)
  pics     pictures shown in the window (300 at a clean 60)
  falls    the "present N:" line's falls to a held 30 fps (D75's safety net)
  rend     the render thread's ms per picture ("render N:", D63; includes its
           waits for the vblank and the GPU, so ~16.7 at a clean 60)
  gpu      D75's meter (gpu_meter=1): the GPU's ms per picture for the game's
           scenes and texture decodes, without the present's quad (or, in a
           log from before the meter's own line, the "dynres N:" line's)
  pres     the meter's present (with its wait for the display)
  busy     the game thread's busy % per vblank ("time N:", waits left out)
  wfr      ms the game thread waited for the render thread's previous present
           ("render N:": "previous frame not shown yet"; 0 with present_due=1)
  wpool    ms it waited for the ring or a vertex slab ("ring full", "vertex slabs")
  skip     frames whose draws the render thread skipped (present_due=1, D112:
           the "present N:" line's 7c part, in-between and real frames)
Windows are named by the level the last "where:" line before their end
names. A window with a "where:" line inside it (a level load, the title's
start, a menu warp) is a load window: it's listed but kept out of the level's
means, and its late vblanks and falls are counted apart, as "loads".

  fps60_summary.py spyro.log [--all] [--from V] [--to V]
"""
import argparse
import re
from collections import OrderedDict


def windows(path):
    w = OrderedDict()
    where = []  # (vblank, level name)
    for line in open(path, errors="replace"):
        m = re.match(r"\[\s*(\d+)\] vita (.*)", line)
        if not m:
            continue
        v, rest = int(m.group(1)), m.group(2)
        m = re.match(r"where: (.*?)(?: \(level \d+\))?, gamestate \d+ (.*)", rest)
        if m:
            name = m.group(1)
            if name.startswith("level "):
                name = m.group(2)  # level 0: the title, the demos' start
            where.append((v, name))
            continue
        m = re.match(r"(\w+(?: meter)?) (\d+): (.*)", rest)
        if not m:
            continue
        kind, n, body = m.group(1), int(m.group(2)), m.group(3)
        d = w.setdefault(n, {})
        if kind == "vblank":
            mm = re.search(r"(\d+) pictures, (\d+) vblanks late", body)
            if mm:
                d["pics"], d["late"] = int(mm.group(1)), int(mm.group(2))
        elif kind == "time":
            mm = re.search(r"busy (\d+)%", body)
            if mm:
                d["busy"] = int(mm.group(1))
        elif kind == "render":
            mm = re.search(r"([\d.]+) ms per picture", body)
            if mm:
                d["rend"] = float(mm.group(1))
            waits = dict((k, float(x)) for k, x in re.findall(
                r"(ring full|previous frame not shown yet|vertex slabs) ([\d.]+) ms", body))
            if waits:
                d["wfr"] = waits.get("previous frame not shown yet", 0.0)
                d["wpool"] = waits.get("ring full", 0.0) + waits.get("vertex slabs", 0.0)
        elif kind == "present":
            mm = re.search(r"\((\d+) falls to a held 30 fps\)", body)
            if mm:
                d["falls"] = int(mm.group(1))
            mm = re.search(r"draws skipped for (\d+) in-between and (\d+) real frames", body)
            if mm:
                d["skip"] = int(mm.group(1)) + int(mm.group(2))
        elif kind == "dynres" and "gpu" not in d:
            # D75 before the meter's own line (J6): pictures and ms at each scale
            ps = [(int(p), float(x)) for p, x in re.findall(r"(\d+) pictures, ([\d.]+) ms", body)]
            if ps and sum(p for p, _ in ps):
                d["gpu"] = sum(p * x for p, x in ps) / sum(p for p, _ in ps)
        elif kind == "gpu meter":
            parts = dict((k, float(x)) for k, x in re.findall(
                r"(the game's frames|texture decodes|other|the present) ([\d.]+) ms", body))
            if parts:
                d["gpu"] = parts.get("the game's frames", 0) + parts.get("texture decodes", 0) + parts.get("other", 0)
                d["pres"] = parts.get("the present", 0)
    out = []
    for n, d in w.items():
        if "late" not in d:
            continue
        before = [name for v, name in where if v <= n]
        inside = [name for v, name in where if n - 300 < v <= n]
        d["level"] = before[-1] if before else "start"
        d["load"] = bool(inside)
        d["n"] = n
        out.append(d)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=1 << 30)
    a = ap.parse_args()
    ws = [d for d in windows(a.log) if a.lo <= d["n"] <= a.hi]
    cols = ["late", "pics", "falls", "rend", "gpu", "pres", "busy", "wfr", "wpool", "skip"]

    def fmt(d, k):
        x = d.get(k)
        if x is None:
            return "-"
        return f"{x:.2f}" if isinstance(x, float) else str(x)

    if a.all:
        print("window level                 " + " ".join(f"{c:>6}" for c in cols))
        for d in ws:
            print(f"{d['n']:6d} {d['level'][:20]:20s} {'L' if d['load'] else ' '} "
                  + " ".join(f"{fmt(d, c):>6}" for c in cols))
        print()
    levels = OrderedDict()
    loads = {"windows": 0, "late": 0, "falls": 0}
    for d in ws:
        if d["load"]:
            loads["windows"] += 1
            loads["late"] += d.get("late", 0)
            loads["falls"] += d.get("falls", 0)
            continue
        levels.setdefault(d["level"], []).append(d)
    print(f"{'level':20s} {'win':>4} {'late/300':>9} {'worst':>6} {'clean':>6} {'falls':>6} "
          f"{'rend ms':>8} {'gpu ms':>7} {'gpu max':>8} {'busy %':>7} {'busy max':>9} "
          f"{'wfr ms':>7} {'wpool ms':>9} {'skips':>6}")
    for name, ds in levels.items():
        def mean(k):
            xs = [d[k] for d in ds if k in d]
            return sum(xs) / len(xs) if xs else float("nan")

        def top(k):
            xs = [d[k] for d in ds if k in d]
            return max(xs) if xs else float("nan")
        clean = sum(1 for d in ds if d.get("late", 0) <= 5)
        print(f"{name[:20]:20s} {len(ds):4d} {mean('late'):9.1f} {top('late'):6d} {clean:6d} "
              f"{sum(d.get('falls', 0) for d in ds):6d} {mean('rend'):8.2f} {mean('gpu'):7.2f} "
              f"{top('gpu'):8.2f} {mean('busy'):7.1f} {top('busy'):9d} "
              f"{mean('wfr'):7.2f} {mean('wpool'):9.2f} {sum(d.get('skip', 0) for d in ds):6d}")
    print(f"loads: {loads['windows']} windows, {loads['late']} vblanks late, {loads['falls']} falls to 30")


if __name__ == "__main__":
    main()

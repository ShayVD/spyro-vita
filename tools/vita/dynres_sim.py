#!/usr/bin/env python3
"""What dynres=1 (D75, src/platform/vita/dynres.c) would choose in each
300-vblank window of a hardware spyro.log, today and with the GPU cheaper at
full resolution (D74's picture-identical savings).

  tools/vita/dynres_sim.py LOG [--savings 0,10,20,30] [--interp 1]

The GPU's time per picture at 960x544 comes from, in this order:
  - a `dynres N:` line (gpu_meter=1 or dynres on: runs J1-J5): the measured
    scenes at 100% (or the lowest step measured, scaled up as the pixels),
    plus PRESENT_MS for the present's quad;
  - else the window's picture interval, wall ms / pictures, where pictures
    were late (more than LATE late vblanks): the GPU behind. Vsync rounds a
    late picture up to two vblanks, so this errs high (more dropping);
  - else the window fitted: <= 16.7 ms, native.
Windows over 5.1 s of wall time (the options menu, loads, pauses) are left
out, and with --interp 1 those that weren't at 60 fps (under 270 pictures
without late vblanks: the budget held 30, or the in-between frame was off). The choice is dynres.c's down rule for a sustained miss: native if the
picture fits a vblank, else the smallest step predicted (fixed part FIXED_MS
plus the pixels) to fit in FIT_PCT. Savings take that share off the
measured part (not the present's quad). It's per window only: dynres's
holds and its 0.4 s before going down are below a window's resolution.
"""
import re
import sys

STEPS = [1000, 875, 750, 667]
VBLANK_MS = 16.683
PRESENT_MS = 1.5
FIXED_MS = 1.0
FIT_PCT = 95
LATE = 10


def predict(cost, frm, to, fixed):
    f = min(fixed, cost)
    return f + (cost - f) * (STEPS[to] / STEPS[frm]) ** 2


def choose(native_total, frame):
    """native_total: GPU ms per picture at 100%, present included"""
    if native_total <= frame:
        return 0
    cost = native_total - PRESENT_MS
    for i in range(1, len(STEPS)):
        if predict(cost, 0, i, FIXED_MS) + PRESENT_MS <= frame * FIT_PCT / 100:
            return i
    return len(STEPS) - 1


def windows(path):
    cur = {}
    for line in open(path, errors="replace"):
        m = re.search(r"vblank (\d+): 300 vblanks in (\d+) ms .*?, (\d+) pictures, (\d+) vblanks late", line)
        if m:
            cur = {"v": int(m[1]), "wall": int(m[2]), "pics": int(m[3]), "late": int(m[4])}
            yield cur
            continue
        m = re.search(r"dynres (\d+): GPU time per picture.*?\) (.*?); \d+ down", line)
        if m and cur and abs(int(m[1]) - cur["v"]) < 30:
            per = {}
            for part in m[2].split("; "):
                p = re.match(r"(\d+)%: (\d+) pictures, ([\d.]+) ms", part)
                if p:
                    per[int(p[1])] = (int(p[2]), float(p[3]))
            cur["dyn"] = per


def native_cost(w):
    if w.get("dyn"):
        best = max(w["dyn"].items(), key=lambda kv: kv[1][0])  # most pictures
        pct, (_, ms) = best
        i = min(range(len(STEPS)), key=lambda k: abs(STEPS[k] / 10 - pct))
        return predict(ms, i, 0, 0) + PRESENT_MS, "measured at %d%%" % pct
    if w["late"] > LATE:
        return w["wall"] / w["pics"], "interval (late)"
    return None, "fits"


def main():
    args = sys.argv[1:]
    savings = [0, 10, 20, 30]
    interp = 1
    if "--savings" in args:
        savings = [int(x) for x in args[args.index("--savings") + 1].split(",")]
    if "--interp" in args:
        interp = int(args[args.index("--interp") + 1])
    frame = VBLANK_MS * (1 if interp else 2)
    log = [a for a in args if not a.startswith("--") and not re.fullmatch(r"[\d,]+", a)][0]
    names = ["%d%%" % ((s + 5) // 10) for s in STEPS]
    print("| Window | Pictures, late | GPU at 100% (ms) | Source | " +
          " | ".join("−%d%%" % s for s in savings) + " |")
    print("|---|---|---|---|" + "---|" * len(savings))
    count = {s: [0] * len(STEPS) for s in savings}
    for w in windows(log):
        if w["wall"] > 5100 or (interp and not w.get("dyn") and w["pics"] < 270 and w["late"] <= LATE):
            continue
        c, src = native_cost(w)
        picks = []
        for s in savings:
            i = 0 if c is None else choose(PRESENT_MS + (c - PRESENT_MS) * (1 - s / 100), frame)
            count[s][i] += 1
            picks.append(names[i])
        print("| %d | %d, %d | %s | %s | %s |" % (w["v"], w["pics"], w["late"],
              "≤ 16.7" if c is None else "%.1f" % c, src, " | ".join(picks)))
    print()
    for s in savings:
        n = sum(count[s])
        print("GPU %d%% cheaper: " % s + ", ".join("%s %d of %d windows" % (names[i], count[s][i], n)
                                              for i in range(len(STEPS)) if count[s][i]))


if __name__ == "__main__":
    main()

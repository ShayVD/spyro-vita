#!/usr/bin/env python3
"""Sum a gpu_fused=2 run's "gpu fused ab" lines (decisions D92).

Usage: tools/vita/fused_ab_summary.py spyro.log [FROM_VBLANK]

gpu_fused=2 makes the render thread take the way before D92 (f0) and
gpu_fused=1's (f1) in turn, one vertex buffer each (gpu_gl.c), so every
300-vblank window has both in nearly the same scenes. Per way: the vertex
buffers, then per buffer the render thread's ms in gl_vertices (the copy,
the collapse, expand, the upload), in issuing the draws (GL calls
included) and both, and the GL draws. Then f1 against f0: the mean of the
windows' per-buffer differences and its standard error.
"""
import math
import re
import sys

LINE = re.compile(r"gpu fused ab (\d+):(.*)")
WAY = re.compile(r"f(\d) ([\d/]+);")


def main():
    path = sys.argv[1]
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 900
    wins = {}
    for line in open(path, errors="replace"):
        m = LINE.search(line)
        if m and int(m.group(1)) >= first:
            wins[int(m.group(1))] = {int(f): [int(x) for x in v.split("/")]
                                     for f, v in WAY.findall(m.group(2))}
    keys = sorted(wins)
    if not keys:
        sys.exit("no gpu fused ab lines from vblank %d" % first)
    cols = ["vertices", "issue", "both", "GL draws"]

    def per_buf(w, f):
        d = wins[w].get(f)
        if not d or not d[0]:
            return None
        n = d[0]
        return [d[1] / n / 1000, d[2] / n / 1000, (d[1] + d[2]) / n / 1000, d[3] / n]

    print("%d windows, vblanks %d-%d" % (len(keys), keys[0], keys[-1]))
    print("%-4s %7s " % ("way", "buffers") + " ".join("%9s" % c for c in cols))
    for f in (0, 1):
        tot = [0] * 4
        for w in keys:
            d = wins[w].get(f, [0, 0, 0, 0])
            for i in range(4):
                tot[i] += d[i]
        n = tot[0] or 1
        row = [tot[1] / n / 1000, tot[2] / n / 1000, (tot[1] + tot[2]) / n / 1000, tot[3] / n]
        print("f%-3d %7d " % (f, tot[0]) + " ".join("%9.3f" % x for x in row))
    diffs = []
    for w in keys:
        a, b = per_buf(w, 1), per_buf(w, 0)
        if a and b:
            diffs.append([x - y for x, y in zip(a, b)])
    if len(diffs) < 2:
        return
    out = []
    for i in range(4):
        xs = [d[i] for d in diffs]
        mean = sum(xs) / len(xs)
        sd = math.sqrt(sum((x - mean) ** 2 for x in xs) / (len(xs) - 1))
        out.append("%+.3f+-%.3f" % (mean, sd / math.sqrt(len(xs))))
    print("\nf1 against f0: mean of the windows' per-buffer differences, ms (+- standard error)")
    print("     " + " ".join("%14s" % c for c in cols))
    print("     " + " ".join("%14s" % o for o in out))


if __name__ == "__main__":
    main()

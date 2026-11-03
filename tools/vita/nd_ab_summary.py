#!/usr/bin/env python3
"""Sum an nd_ab=1 run's "draw ab" and "parse ab" lines (decisions D87).

Usage: tools/vita/nd_ab_summary.py spyro.log [FROM_VBLANK]

nd_ab=1 makes the record path's variants take turns by blocks of 8 lists
(native_draw.c), so every 300-vblank window has all of them in nearly the
same scenes. Per variant: the counted lists, then per list the renderers'
ms (env, mobys, Spyro, sky and their sum), the parse thread's walk and the
game thread's wait for it. Then each variant against v0 (native_draw=0)
and v1 (D86's path): the mean of the windows' per-list differences and its
standard error, so the noise is in the table.
"""
import math
import re
import sys

NAMES = ["v0 off", "v1 D86", "v2 side", "v3 pf", "v4 side+pf", "v5 probe", "v6 probe+pf"]
PARTS = ["env", "mobys", "spyro", "fx", "particles", "sky"]
DRAW = re.compile(r"draw ab (\d+):(.*)")
PARSE = re.compile(r"parse ab (\d+):(.*)")
VAR = re.compile(r"v(\d) ([\d/]+);")


def main():
    path = sys.argv[1]
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 900
    draw, parse = {}, {}
    for line in open(path, errors="replace"):
        m = DRAW.search(line)
        if m and int(m.group(1)) >= first:
            draw[int(m.group(1))] = {int(v): [int(x) for x in f.split("/")]
                                     for v, f in VAR.findall(m.group(2))}
        m = PARSE.search(line)
        if m and int(m.group(1)) >= first:
            parse[int(m.group(1))] = {int(v): [int(x) for x in f.split("/")]
                                      for v, f in VAR.findall(m.group(2))}
    wins = sorted(set(draw) & set(parse))
    if not wins:
        sys.exit("no windows with both lines from vblank %d" % first)

    def per_list(w, v):
        """ms per list: env, mobys, spyro, sky, renderers, walk, wait"""
        d, p = draw[w].get(v), parse[w].get(v)
        if not d or not d[0] or not p or not p[0]:
            return None
        n = d[0]
        e, mo, sp, sk = d[1] / n, d[2] / n, d[3] / n, d[6] / n
        return [x / 1000 for x in (e, mo, sp, sk, e + mo + sp + sk, p[1] / p[0], p[2] / p[0])]

    cols = ["env", "mobys", "spyro", "sky", "renderers", "walk", "wait"]
    print("%d windows, vblanks %d-%d" % (len(wins), wins[0], wins[-1]))
    print("%-11s %6s " % ("variant", "lists") + " ".join("%9s" % c for c in cols))
    for v in range(len(NAMES)):
        lists = sum(draw[w].get(v, [0])[0] for w in wins)
        if not lists:
            continue
        tot = [0.0] * 7
        for w in wins:
            d, p = draw[w].get(v), parse[w].get(v)
            if not d or not p:
                continue
            for i, k in enumerate((1, 2, 3, 6)):
                tot[i] += d[k]
            tot[5] += p[1]
            tot[6] += p[2]
        tot[4] = sum(tot[:4])
        walks = sum(parse[w].get(v, [0])[0] for w in wins) or 1
        row = [t / lists / 1000 for t in tot[:5]] + [t / walks / 1000 for t in tot[5:]]
        print("%-11s %6d " % (NAMES[v], lists) + " ".join("%9.3f" % x for x in row))
    for base in (0, 1):
        print("\nagainst %s: mean of the windows' per-list differences, ms (+- standard error)"
              % NAMES[base])
        for v in range(len(NAMES)):
            if v == base:
                continue
            diffs = []
            for w in wins:
                a, b = per_list(w, v), per_list(w, base)
                if a and b:
                    diffs.append([x - y for x, y in zip(a, b)])
            if len(diffs) < 2:
                continue
            out = []
            for i in range(7):
                xs = [d[i] for d in diffs]
                mean = sum(xs) / len(xs)
                sd = math.sqrt(sum((x - mean) ** 2 for x in xs) / (len(xs) - 1))
                out.append("%+.3f+-%.3f" % (mean, sd / math.sqrt(len(xs))))
            print("%-11s " % NAMES[v] + " ".join("%14s" % o for o in out[:1]) + " " +
                  " ".join("%14s" % o for o in out[1:]))
        print("%-11s " % "" + " ".join("%14s" % c for c in cols))


if __name__ == "__main__":
    main()

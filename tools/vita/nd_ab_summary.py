#!/usr/bin/env python3
"""Sum an nd_ab=1 run's "draw ab" and "parse ab" lines (decisions D91, D92).

Usage: tools/vita/nd_ab_summary.py [--t1|--t2] spyro.log [FROM_VBLANK]

nd_ab=1 makes native_draw 0, 1 and 2 take turns by blocks of 8 lists
(native_draw.c; runs T1 and T2 rotated seven variants of the record path,
T3 two),
so every 300-vblank window has all of them in nearly the same scenes. Per variant: the counted lists, then per list the renderers'
ms (env, mobys, Spyro, sky and their sum), the parse thread's walk and the
game thread's wait for it, and "game": the renderers and the wait, what the
game thread spends on the list. Then each variant against v0 (native_draw=0)
and v1: the mean of the windows' per-list differences and its
standard error, so the noise is in the table.
"""
import math
import re
import sys

# native_draw 0, 1 (D91's lean records) and 2 (D92: the walk parses the PS1
# primitives with the records' converter); --t1 and --t2 name the variants of those runs
NAMES = ["v0 off", "v1 records", "v2 parse"]
for flag, names in (("--t1", ["v0 off", "v1 D86", "v2 side", "v3 pf", "v4 side+pf", "v5 probe",
                              "v6 probe+pf"]),
                    ("--t2", ["v0 off", "v1 side+pf", "v2 side+nc", "v3 side+ncw", "v4 probe",
                              "v5 probe+nc", "v6 D86"])):
    if flag in sys.argv:
        sys.argv.remove(flag)
        NAMES = names
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
        """ms per list: env, mobys, spyro, sky, renderers, walk, wait, game"""
        d, p = draw[w].get(v), parse[w].get(v)
        if not d or not d[0] or not p or not p[0]:
            return None
        n = d[0]
        e, mo, sp, sk = d[1] / n, d[2] / n, d[3] / n, d[6] / n
        r, wt = e + mo + sp + sk, p[2] / p[0]
        return [x / 1000 for x in (e, mo, sp, sk, r, p[1] / p[0], wt, r + wt)]

    cols = ["env", "mobys", "spyro", "sky", "renderers", "walk", "wait", "game"]
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
        row.append(row[4] + row[6])
        print("%-11s %6d " % (NAMES[v], lists) + " ".join("%9.3f" % x for x in row))
    for base in (0, 1, 2):
        print("\nagainst %s: mean of the windows' per-list differences, ms (+- standard error)"
              % NAMES[base])
        print("%-11s " % "" + " ".join("%14s" % c for c in cols))
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
            for i in range(8):
                xs = [d[i] for d in diffs]
                mean = sum(xs) / len(xs)
                sd = math.sqrt(sum((x - mean) ** 2 for x in xs) / (len(xs) - 1))
                out.append("%+.3f+-%.3f" % (mean, sd / math.sqrt(len(xs))))
            print("%-11s " % NAMES[v] + " ".join("%14s" % o for o in out))


if __name__ == "__main__":
    main()

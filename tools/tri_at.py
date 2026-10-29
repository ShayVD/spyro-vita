#!/usr/bin/env python3
"""List the triangles of a `spyro --tri-dump V` log that cover screen point(s).
   tools/tri_at.py LOG X,Y [X,Y ...]   (screen = vertex coordinates on screen)"""
import re, sys

def tris(path):
    for line in open(path):
        if " tri " not in line:
            continue
        head, *verts = line.split(" | ")
        vs = []
        for v in verts:
            m = re.match(r"\s*(-?\d+),(-?\d+) uv (\d+),(\d+) rgb (\d+),(\d+),(\d+)", v)
            vs.append(tuple(int(g) for g in m.groups()))
        yield head.split(" host ")[1], vs

def inside(p, a, b, c):
    def s(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
    d1, d2, d3 = s(p, a, b), s(p, b, c), s(p, c, a)
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)

log = sys.argv[1]
for pt in sys.argv[2:]:
    x, y = map(int, pt.split(","))
    print(f"== {x},{y}")
    for i, (head, vs) in enumerate(tris(log)):
        if inside((x, y), *[v[:2] for v in vs]):
            print(f"#{i} {head} " + " | ".join(f"{v[0]},{v[1]} uv{v[2]},{v[3]} c{v[4]},{v[5]},{v[6]}" for v in vs))

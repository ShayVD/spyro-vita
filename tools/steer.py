#!/usr/bin/env python3
"""Steer Spyro to a point in a `spyro --step DIR --moby-dump` session
(src/platform/linux32/step.c): each step reads Spyro's position from
DIR/now.tsv, holds UP (plus LEFT or RIGHT when the target is off to one
side) for a few vblanks, and repeats until Spyro is within --radius of the
target. Which of LEFT/RIGHT turns which way is learned from the first turns.

    tools/steer.py DIR X Y [--radius R] [--step N] [--max-steps M] [--extra BUTTONS]
"""
import argparse
import csv
import math
import subprocess
import sys

ap = argparse.ArgumentParser()
ap.add_argument("dir")
ap.add_argument("x", type=int)
ap.add_argument("y", type=int)
ap.add_argument("--radius", type=int, default=400)
ap.add_argument("--step", type=int, default=4, help="vblanks per command")
ap.add_argument("--max-steps", type=int, default=400)
ap.add_argument("--extra", default="", help="buttons added to every command, e.g. square to charge")
a = ap.parse_args()


def state():
    with open(f"{a.dir}/now.tsv") as f:
        r = next(csv.DictReader(f, delimiter="\t"))
    return int(r["spyro_x"]), int(r["spyro_y"]), int(r["gems"])


def send(buttons):
    out = subprocess.run(["tools/step.sh", a.dir, f"{buttons} {a.step}"], capture_output=True, text=True)
    if out.returncode:
        sys.exit(out.stderr)
    return int(out.stdout.split()[0])


def angle(v):
    return math.atan2(v[1], v[0])


prev = state()
left_sign = None  # +1: LEFT turns counter-clockwise (x right, y up)
for i in range(a.max_steps):
    x, y, gems = state()
    d = (a.x - x, a.y - y)
    dist = math.hypot(*d)
    if dist <= a.radius:
        print(f"arrived: ({x},{y}) dist {dist:.0f} gems {gems}")
        sys.exit(0)
    v = (x - prev[0], y - prev[1])
    btn = "up"
    if math.hypot(*v) > 8:
        err = (angle(d) - angle(v) + math.pi) % (2 * math.pi) - math.pi
        if abs(err) > math.radians(12):
            ccw = err > 0
            side = "left" if (left_sign is None and ccw) or (left_sign == 1 and ccw) or (left_sign == -1 and not ccw) else "right"
            btn = "up+" + side
            last_turn, last_err = side, err
    if a.extra:
        btn += "+" + a.extra
    prev = (x, y, gems)
    v_at = send(btn)
    if left_sign is None and "+" in btn and btn.split("+")[1] in ("left", "right"):
        nx, ny, _ = state()
        nv = (nx - x, ny - y)
        if math.hypot(*nv) > 8 and math.hypot(*v) > 8:
            turned = (angle(nv) - angle(v) + math.pi) % (2 * math.pi) - math.pi
            if abs(turned) > math.radians(2):
                was_left = btn.split("+")[1] == "left"
                left_sign = (1 if turned > 0 else -1) * (1 if was_left else -1)
                print(f"learned: LEFT turns {'ccw' if left_sign == 1 else 'cw'}")
    if i % 20 == 0:
        print(f"step {i} vblank {v_at} ({x},{y}) dist {dist:.0f} gems {gems}")
print("gave up")
sys.exit(1)

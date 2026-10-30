#!/usr/bin/env python3
"""Turn a host pad script with absolute vblanks (a `--step` session's
DIR/pad.txt) into state-anchored lines (`@gs=G,level=L +N buttons`) that
replay the same way on the host and in the emulator (capture.lua
ORACLE_PAD / ORACLE_PAD_AFTER), using the host's --trace of that session
for the gamestate and level at each vblank.

Each line is anchored to the (gamestate, level) the game was in when it was
pressed. Every state change between two lines also gets a line of its own
(the buttons already held, +0), so a run of lines with the same condition
never spans a visit to another state: the anchor then follows each
transition, as capture.lua's and pad.c's matching does.

    tools/anchor_pad.py TRACE PAD [--from V] [--to V] > anchored.txt
"""
import argparse
import csv

ap = argparse.ArgumentParser()
ap.add_argument("trace")
ap.add_argument("pad")
ap.add_argument("--from", dest="start", type=int, default=0,
                help="drop pad lines before this vblank")
ap.add_argument("--to", type=int, default=1 << 30,
                help="stop at this vblank (a final 'none' line is added)")
a = ap.parse_args()

state = {}
with open(a.trace) as f:
    for r in csv.DictReader(f, delimiter="\t"):
        state[int(r["vblank"])] = (int(r["gamestate"]), int(r["level"]))

events = []
for line in open(a.pad):
    line = line.split("#")[0].split()
    if len(line) == 2 and line[0].isdigit():
        v = int(line[0])
        if a.start <= v < a.to:
            events.append((v, line[1]))
if a.to < (1 << 30):
    events.append((a.to, "none"))
events.sort()


def cond(s):
    return f"@gs={s[0]},level={s[1]}"


out = []
anchor_cond, anchor_v = None, None
held = "none"
prev_v = a.start - 1 if a.start else (events[0][0] if events else 0)
for v, btn in events:
    # State changes since the previous line: re-anchor on each.
    for t in range(prev_v + 1, v + 1):
        s, ps = state.get(t), state.get(t - 1)
        if s is None or s == ps or t == v:
            continue
        if cond(s) != anchor_cond:
            anchor_cond, anchor_v = cond(s), t
            out.append(f"{anchor_cond} +0 {held}")
    s = state.get(v) or state.get(v - 1)
    c = cond(s)
    if c != anchor_cond:
        # first vblank of this state run, going back from v
        t = v
        while t - 1 > prev_v and state.get(t - 1) == s:
            t -= 1
        anchor_cond, anchor_v = c, t
    out.append(f"{c} +{v - anchor_v} {btn}")
    if not btn.startswith("poke:"):  # pokes don't change the buttons held
        held = btn
    prev_v = v

print(f"# anchored from {a.pad} with {a.trace} (tools/anchor_pad.py)")
print("\n".join(out))

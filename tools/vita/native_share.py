#!/usr/bin/env python3
"""CPU time per vblank on the Vita, by layer, from spyro.log's 300-vblank
windows (docs/native-plan.md "Run time", decisions D76).

Every thread's work in a window, in ms per vblank:
  logic      the game's update (the "draw" line's update: decomp C, the hand
             C of pete and camera, mips2c collision, math and gamestates)
  renderers  env, mobys, Spyro, fx, particles, sky (hand C over the machine
             model with the GTE inline, and mips2c for the rest)
  draw/other the rest of the game thread's "game" part (draw.c building
             primitives, the HUD, PsyCross calls other than DrawOTag)
  ot walk    PsyCross's OT walk: the parse thread's busy time when it runs
             (D71), else the game thread's "ot" part
  render thr the render thread (D63): PS1 GPU commands turned into vitaGL
             calls, the atlas decode of PS1 VRAM, vitaGL itself. Its busy
             time includes vglSwapBuffers and vglWaitVblankStart, so it is
             an upper bound on CPU. With render_thread=0 (run K1) it is the
             game thread's "raster" part instead: CPU only
  spu+mixer  the SPU model on the game thread and the mixer thread (D37)
  xa/cd      the XA read-ahead's reads (D64)
  present    the game thread's "present" part
Waits are left out: the game thread's join for the parse thread is "ot"
minus the copy, and isn't work.

  native_share.py spyro.log [--from V] [--to V] [--fps 30|60] [--all]

--fps keeps windows at a steady 30 (no in-between frames, 140-155 pictures)
or 60 (at least 250 pictures). --all prints every window, not only the mean.
"""
import argparse
import re
from collections import OrderedDict

COLS = ["logic", "renderers", "draw/other", "ot walk", "render thr", "spu+mixer", "xa/cd", "present"]


def kv(s):
    return {k: float(v) for k, v in re.findall(r"(\w+) ([\d.]+)", s)}


def windows(path):
    w = OrderedDict()
    for line in open(path, errors="replace"):
        m = re.search(r"vita (\w+) (\d+): (.*)$", line)
        if not m:
            continue
        kind, v, rest = m.group(1), int(m.group(2)), m.group(3)
        d = w.setdefault(v, {})
        if kind == "time":
            d["time"] = kv(rest.split(";")[0].replace("per vblank ms", ""))
        elif kind == "draw":
            d["draw"] = kv(rest.split("(")[0].replace("per vblank ms", ""))
        elif kind == "vblank":
            m2 = re.search(r"(\d+) pictures", rest)
            d["pictures"] = int(m2.group(1)) if m2 else 0
        elif kind == "render":
            m2 = re.search(r"busy ([\d.]+) ms per vblank", rest)
            if m2:
                d["render"] = float(m2.group(1))
        elif kind == "parse":
            m2 = re.search(r"busy ([\d.]+) ms per vblank.*?copied \d+ KB in ([\d.]+) ms", rest)
            if m2:
                d["parse"] = (float(m2.group(1)), float(m2.group(2)))
        elif kind == "mixer":
            m2 = re.search(r"([\d.]+) ms each", rest)
            if m2:
                d["mixer"] = float(m2.group(1))
        elif kind == "xa":
            m2 = re.search(r"KB in ([\d.]+) ms", rest)
            if m2:
                d["xa"] = float(m2.group(1)) / 300
        elif kind == "atlas":
            m2 = re.search(r"expand ([\d.]+) ms", rest)
            if m2:
                d["expand"] = float(m2.group(1)) / 300
        elif kind == "present":
            m2 = re.search(r"(\d+) in-between frames", rest)
            if m2:
                d["between"] = int(m2.group(1))
    return w


def row(d):
    t, dr = d.get("time"), d.get("draw")
    if not t or not dr:
        return None
    rend = sum(dr.get(k, 0) for k in ("env", "mobys", "spyro", "fx", "particles", "sky"))
    parse = d.get("parse")
    ot = parse[0] + parse[1] if parse else t.get("ot", 0)
    return OrderedDict([
        ("logic", dr.get("update", 0)),
        ("renderers", rend),
        ("draw/other", max(0.0, t.get("game", 0) - dr.get("update", 0) - rend)),
        ("ot walk", ot),
        ("render thr", d["render"] if "render" in d else t.get("raster", 0)),
        ("spu+mixer", t.get("spu", 0) + d.get("mixer", 0)),
        ("xa/cd", d.get("xa", 0)),
        ("present", t.get("present", 0)),
    ])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--from", dest="lo", type=int, default=600)
    ap.add_argument("--to", dest="hi", type=int, default=10 ** 9)
    ap.add_argument("--fps", type=int, choices=(30, 60))
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args()
    rows = []
    for v, d in windows(a.log).items():
        if not a.lo <= v <= a.hi:
            continue
        pics, between = d.get("pictures", 0), d.get("between")
        if a.fps == 30 and not (140 <= pics <= 155 and not between):
            continue
        if a.fps == 60 and pics < 250:
            continue
        r = row(d)
        if r:
            rows.append((v, r, d.get("expand", 0)))
    if not rows:
        print(f"{a.log}: no windows with time and draw lines" + (f" at {a.fps} fps" if a.fps else ""))
        return
    print("| vblank | " + " | ".join(COLS) + " | sum | of it: atlas expand |")
    print("|---" * (len(COLS) + 3) + "|")
    if a.all:
        for v, r, e in rows:
            print(f"| {v} | " + " | ".join(f"{x:.2f}" for x in r.values()) + f" | {sum(r.values()):.2f} | {e:.2f} |")
    mean = OrderedDict((k, sum(r[k] for _, r, _ in rows) / len(rows)) for k in COLS)
    e = sum(x for _, _, x in rows) / len(rows)
    tot = sum(mean.values())
    print(f"| mean of {len(rows)} | " + " | ".join(f"{x:.2f}" for x in mean.values()) + f" | {tot:.2f} | {e:.2f} |")
    print("| % | " + " | ".join(f"{100 * x / tot:.0f}%" for x in mean.values()) + f" | 100% | {100 * e / tot:.0f}% |")


if __name__ == "__main__":
    main()

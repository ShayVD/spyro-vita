#!/usr/bin/env python3
"""Differential sweeps of func_80041670's hand C (Spyro's state transitions,
src/game/pete_transition_hand.c) against its mips2c unit (decisions D33;
src/game/oracle_replay.c SPYRO_REPLAY_SWEEP/POKE), for the states and
branches no capture reaches.

For each Spyro state 0-44 and each input word it reads (STATE_FIELDS), every sample of CAPTURE
is replayed with m_State set and that word swept over its values (the branch
boundaries in the asm); round 0 sets nothing else, later rounds also set a
few other words at random (seeded) so that the sweeps meet each other's
paths. Needs a
-DSPYRO_M2C_REF=ON build.

    sweep_transition.py CAPTURE OUTDIR [--build DIR] [--rounds N] [--first-round R] [--seed S]
                        [--states 0-44] [--pokes K]

Writes OUTDIR/runs.sh (one replay per line), runs it in one tools/tc call
and prints the totals (PASS / FAIL / CRASH; CRASH-REF runs aren't compared)."""
import argparse
import os
import random
import re
import subprocess

S = 0x80078A58  # g_Spyro
PAD = 0x80077378  # g_Pad
FRAMES = [PAD + 0x44 + 24 * i for i in range(4)]  # m_BufferedInputs

# name: (address or list of addresses, values)
FIELDS = {
    "idle": (S + 0x80, [0, 3, 4, 8, 9, 12, 13, 15, 16, 17, 18, 20, 21, 24, 30, 31, 36, 37, 47,
                        48, 60, 61, 120, 121, 300, 301]),
    "walking": (S + 0x7C, [0, 1, 2, 7, 0x40, 0x41, 0x80, 0xC0]),
    "air": (S + 0x9C, [0, 1, 3, 4, 7, 8]),
    "slope": (S + 0xB0, [0, 11, 12, 22, 23, 40]),
    "down": (PAD + 0x0, [0, 0x40, 0x10, 0x4, 0x8, 0x80, 0x44, 0x50, 0xFFFF]),
    "released": (PAD + 0x4, [0, 0x40]),
    "held": (PAD + 0x8, [0, 0x40, 0x80, 0xC0, 0x4, 0x8, 0x44, 0xFFFF]),
    "stickmoved": (PAD + 0x10, [0, 1]),
    "nomove": (PAD + 0x1C, [0, 1]),
    "frameheld": (FRAMES[0] + 4, [0, 4, 8, 0xC]),
    "stick": (FRAMES[0] + 0x14, [0x7F7F7F7F, 0x4F7F7F7F, 0x4E7F7F7F, 0xAF7F7F7F, 0xB07F7F7F, 0]),
    "speed": (S + 0x118, [0, 0x15, 0x63F, 0x640, 0x77F, 0x780, 0x781, 0xC7F, 0xC80, 0xC81,
                          0xF00, 0xF01, 0x1DFF, 0x1E00, 0x1F00, 0x1F01, 0x3000, 0x3001]),
    "target": (S + 0xC8, [-0x100, 0, 1, 0x800, 0x2000]),
    "rotz": (S + 0xCC, [0, 0x400, 0x500, 0x501, 0x800, 0xB00]),  # target heading (current: sample's)
    "truespeed": (S + 0x128, [0, 0xFF, 0x100, 0x3FF, 0x400, 0x401, 0x8FF, 0x900, 0x2000]),
    "tvz": (S + 0x114, [-0x1901, -0x1900, -0x1001, -0x1000, -0xC00, -0xBFF, -1, 0, 0x5FF,
                        0x600, 0x800, 0x801]),
    "accz": (S + 0xFC, [-0x100, -1, 0, 1, 0x100]),
    "accx": (S + 0xF4, [-0x800, 0, 0x800]),
    "floorx": (S + 0xA4, [-0x1000, 0, 0x1000]),
    "health": (S + 0x164, [-1, 0, 3]),
    "damage": (S + 0x2C, [0, 0x4, 0x6, 0x10, 0x20, 0x400, 0x800, 0x2000, 0x8000]),
    "invuln": (S + 0x160, [0, 1, 90]),
    "onslope": (S + 0x150, [0, 1]),
    "gliding": (S + 0x154, [0, 1]),
    "flooridle": (S + 0x98, [0, 1]),
    "u15c": (S + 0x15C, [0, 1]),
    "flying": (S + 0x24C, [0, 1]),
    "supercharge": (S + 0x250, [0, 1]),
    "touchmoby": (S + 0x88, [0, 1]),
    "onedge": (S + 0xC4, [0, 1]),
    "nopad": (S + 0x1F0, [0, 1, 7, 8, 11, 12]),
    "control": (S + 0x1F4, [0, 1]),
    "u198": (S + 0x198, [0, 1]),
    "flight": (0x80075690, [0, 1]),
    "transition": (0x800756D0, [0, 1]),
    "camera": (0x80076E90, [0, 0x80000009]),
    "d700": (0x80075700, [0, 9, 10]),
    "idlewait": (0x80075788, [0, 20, 0x2710]),  # D_80075788 (short) and its neighbour
    "posz": (S + 0x8, [-0x100000, 0, 0x400, 0x4000, 0x40000]),
    "highest": (S + 0x158, [0x3DFF, 0x3E00, 0x3FE7, 0x3FE8, 0x4000, 0x5000]),  # posz 0x4000
    "anim": (S + 0x18, [n << 8 for n in (0, 19, 20, 37, 38, 44)]),  # m_nextBodyAnimation
    "frame": (S + 0x1C, [f << 24 for f in range(0, 64)]),  # m_nextBodyAnimationFrame
    "level64": (0x8007596C, [64]),  # with g_NextLevelId 64 (poked below)
}


# The words each state's code reads (pete_transition_hand.c), "damage"
# standing for HandleSpyroDamage / func_80041270's result.
GROUND = ["down", "held", "air", "slope", "damage"]
FLIGHT = ["air", "slope", "speed", "target", "flooridle", "truespeed", "tvz", "down", "idle",
          "walking", "highest", "frameheld", "flight", "held", "damage"]
HURT = ["damage", "idle", "health", "air", "nopad"]
IDLE_ANIM = GROUND + ["truespeed", "camera", "nomove", "onedge", "nopad", "anim", "u198"]
STATE_FIELDS = {
    0: GROUND + ["truespeed", "camera", "nomove", "nopad", "onedge", "idle", "idlewait", "u198"],
    13: GROUND + ["camera", "nomove", "nopad", "onedge", "idle"],
    1: GROUND + ["speed", "target", "idle"],
    2: GROUND + ["target", "rotz", "stickmoved", "stick", "speed"],
    3: GROUND + ["speed", "target", "truespeed", "idle", "rotz"],
    4: ["air", "slope", "idle", "nomove", "damage"],
    5: ["onslope", "down", "tvz", "walking", "held", "air", "idle", "slope", "accx", "floorx",
        "gliding", "flight", "level64", "damage"],
    6: ["down", "u15c", "idle", "walking", "flight", "level64", "onslope", "posz", "air", "slope",
        "speed", "target", "damage"],
    8: ["damage", "idle", "nomove"],
    9: ["damage", "air", "slope", "held", "idle"],
    11: ["damage", "held", "slope", "air", "speed", "truespeed", "transition", "supercharge", "walking"],
    12: ["idle", "air", "slope", "damage"],
    15: ["air", "slope", "speed", "target", "flooridle", "truespeed", "held", "idle", "down", "flying",
         "walking", "highest", "frameheld", "flight", "level64", "damage"],
    16: ["posz", "air", "idle", "slope", "nopad", "down", "onslope", "flight", "damage"],
    17: ["damage"],
    18: IDLE_ANIM + ["frame"],
    19: ["held", "idle", "speed", "air", "slope", "frame", "truespeed", "target", "damage"],
    20: ["onslope", "down", "walking", "damage", "slope", "transition", "accx", "floorx", "touchmoby",
         "speed", "truespeed", "air", "accz", "gliding", "flight"],
    21: GROUND + ["speed"],
    24: ["idle", "held", "u15c", "down", "onslope", "slope", "transition", "accx", "floorx",
         "touchmoby", "truespeed", "air", "accz", "gliding", "flight"],
    26: ["down", "air", "slope", "speed", "target", "idle", "damage"],
    29: ["damage", "health", "idle", "down"],
    32: FLIGHT, 33: FLIGHT, 34: FLIGHT,
    35: ["control", "damage"],
    44: ["held", "d700", "air", "released", "slope", "idle", "touchmoby", "speed", "truespeed",
         "transition", "damage"],
    30: ["idle"], 31: ["idle"],
}
STATE_FIELDS[10] = STATE_FIELDS[9]
STATE_FIELDS[23] = STATE_FIELDS[15]
for st in (7, 14, 22, 25, 27, 28):
    STATE_FIELDS[st] = HURT
for st in range(36, 44):
    STATE_FIELDS[st] = IDLE_ANIM


def parse_states(spec):
    out = []
    for part in spec.split(","):
        lo, _, hi = part.partition("-")
        out += range(int(lo), int(hi or lo) + 1)
    return out


ap = argparse.ArgumentParser()
ap.add_argument("capture")
ap.add_argument("outdir")
ap.add_argument("--build", default="build/linux32-ref")
ap.add_argument("--rounds", type=int, default=1)
ap.add_argument("--first-round", type=int, default=0)
ap.add_argument("--seed", type=int, default=1)
ap.add_argument("--states", default="0-44")
ap.add_argument("--pokes", type=int, default=4)
ap.add_argument("--fields", default="state", help="state (the words the state reads) or all")
a = ap.parse_args()

rng = random.Random(a.seed)
os.makedirs(a.outdir, exist_ok=True)
binary = f"{a.build}/src/platform/linux32/spyro"
fnmap = f"{a.build}/oracle/fnmap.txt"
lines = ["#!/bin/sh"]  # runs in tools/tc, in the repo
if not os.path.exists(fnmap) or os.path.getmtime(fnmap) < os.path.getmtime(binary):
    os.makedirs(os.path.dirname(fnmap), exist_ok=True)
    subprocess.run(["tools/tc", "sh", "-c", f"i686-linux-gnu-nm {binary} > {a.build}/oracle/nm.txt"], check=True)
    with open(fnmap, "w") as f:
        subprocess.run(["python3", "tools/oracle/fnmap.py", "build/s1-matching/build/psx.map",
                        f"{a.build}/oracle/nm.txt"], stdout=f, check=True)

n = 0
for rnd in range(a.first_round, a.rounds):
    for state in parse_states(a.states):
        names = STATE_FIELDS[state] if a.fields == "state" else sorted(FIELDS)
        for name in names:
            addr, vals = FIELDS[name]
            pokes = {S + 0x78: state}
            others = [x for x in names if x != name]
            if rnd > 0:  # round 0: the sweep alone; then with random others
                for o in rng.sample(others, min(a.pokes, len(others))):
                    oa, ov = FIELDS[o]
                    pokes[oa] = rng.choice(ov)
            if name == "highest" or S + 0x158 in pokes:
                pokes[S + 0x8] = 0x4000
            if name == "level64" or pokes.get(0x8007596C) == 64:
                pokes[0x800758B4] = 64  # g_NextLevelId, or HandleSpyroDamage does nothing
            if FRAMES[0] + 4 in pokes or name == "frameheld":
                for fr in FRAMES[1:]:
                    pokes[fr + 4] = pokes.get(FRAMES[0] + 4, 0)
            poke = ",".join(f"{k:08X}={v & 0xFFFFFFFF:#x}" for k, v in pokes.items() if k != addr)
            sweep = f"{addr:08X}=" + ",".join(str(v if v < 0x80000000 else v - 0x100000000) for v in vals)
            tag = f"r{rnd}-s{state}-{name}"
            lines.append(f"SPYRO_REPLAY_POKE='{poke}' SPYRO_REPLAY_SWEEP='{sweep}' {binary} --replay {a.capture}"
                         f" --only func_80041670 --ignore tools/oracle/ignore_ranges.txt --fnmap {fnmap}"
                         f" --disc disc/files > {a.outdir}/{tag}.txt 2>&1")
            n += 1
script = f"{a.outdir}/runs.sh"
open(script, "w").write("\n".join(lines) + "\n")
print(f"{n} replays: {script}")
subprocess.run(["tools/tc", "sh", script])

tot = {}
fails = []
for fn in sorted(os.listdir(a.outdir)):
    if not fn.endswith(".txt"):
        continue
    for line in open(os.path.join(a.outdir, fn)):
        m = re.match(r"func_80041670 \d+ \d+ (PASS|FAIL|CRASH-REF|CRASH)\b", line)
        if m:
            tot[m.group(1)] = tot.get(m.group(1), 0) + 1
            if m.group(1) in ("FAIL", "CRASH"):
                fails.append(f"{fn}: {line.strip()}")
print(" ".join(f"{k} {v}" for k, v in sorted(tot.items())))
for f in fails[:40]:
    print(f)

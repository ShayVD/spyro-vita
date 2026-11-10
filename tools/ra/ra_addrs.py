#!/usr/bin/env python3
"""Where each PS1 address a RetroAchievements set reads lives in the port
(docs/retroachievements-plan.md §3).

    tools/ra/ra_addrs.py [--map psx.map] [--all] FILE.json

FILE.json is either
  - the public code notes, dorequest.php?r=codenotes2&g=GAME
    ({"CodeNotes": [{"Address": "0x075860", "Note": ...}, ...]}), or
  - a set's patch data as rc_client receives it (r=achievementsets; it needs
    a login, so the owner fetches it). Every MemAddr / Mem / Definition
    string is scanned for memory operands (0xH075860, 0xX..., fF..., b0x...).
    Operands that follow an AddAddress (I:) in the same group are offsets
    from a pointer, not addresses, and are reported as such.

Neither file goes in the repo: both are RetroAchievements' data. Keep them
under logs/ (gitignored).

Classes (the native memory build, the Vita's default, decisions D106):
  kernel       0x000000-0x00FFFF   g_psx_kernel_ram, but the BIOS's boot path
                                   (0xB8B0, 20 bytes) has to be supplied
  exe          0x010000-0x06BBDF   the executable's code and rodata: host code
                                   in the port, served from the player's exe
  labels       0x06BBE0-0x07AA37   s1_labels + (a - 0x8006BBE0)
  HOLE:<obj>                       a C-defined or PsyQ object the image leaves
                                   out (zeros in s1_labels): needs its own map
  block        0x07AA38-0x1FFFFF   g_psx_arena_base + (a - 0x8007A000)
  scratch      0x200000-0x2003FF   g_psx_scratch
The symbol column is the nearest psx.map symbol at or below the address.
"""
import argparse
import bisect
import collections
import json
import re
import sys

LABELS_LO, BSS_END = 0x8006BBE0, 0x8007AA38
# The labels image's holes (cmake/game.cmake S1_DATA_FILES; psx.map sections
# not in it), with the two PsyQ labels the image keeps.
HOLES = [
    (0x8006F694, 0x8006FA74, "strings.data"),
    (0x80072E94, 0x80073094, "cheats.data"),
    (0x800730BC, 0x80075264, "psyq.data"),
    (0x800752E8, 0x800755A4, "strings.sdata"),
    (0x800755AC, 0x80075640, "draw.sdata"),
    (0x80075640, 0x80075668, "psyq.sbss"),
    (0x80075978, 0x80075EF8, "psyq.bss"),
]
KEPT = [(0x80074B84, 0x80074B84 + 0x400), (0x80075AC0, 0x80075AC4)]  # sqrt table, rand seed
# PsyQ data the port keeps elsewhere, by address
SPECIAL = {0x800749E0: "libetc Vcount -> psx_clock_count()"}

OPERAND = re.compile(r"(?<![0-9A-Za-z])(?:0x([HLMNOPQRSTUVWXYZGIJK ]?)|f([FBHIM]))([0-9a-fA-F]+)")
SIZES = {"H": "8", "": "16", " ": "16", "W": "24", "X": "32", "I": "16be", "J": "24be",
         "G": "32be", "K": "bits", "L": "lo4", "U": "hi4", "M": "b0", "N": "b1",
         "O": "b2", "P": "b3", "Q": "b4", "R": "b5", "S": "b6", "T": "b7"}


def load_syms(path):
    syms = []
    for line in open(path):
        m = re.match(r"^\s+0x([0-9a-f]{8})\s+([A-Za-z_][\w.$]*)$", line)
        if m:
            a = int(m.group(1), 16)
            if 0x80000000 <= a < 0x80200000:
                syms.append((a, m.group(2)))
    syms.sort()
    return syms


def classify(a):
    if a >= 0x200000:
        return "scratch" if a < 0x200400 else "outside"
    if a < 0x10000:
        return "kernel"
    pa = 0x80000000 | a
    if pa < LABELS_LO:
        return "exe"
    if pa in SPECIAL:
        return "special"
    for lo, hi, name in HOLES:
        if lo <= pa < hi and not any(k0 <= pa < k1 for k0, k1 in KEPT):
            return "HOLE:" + name
    return "labels" if pa < BSS_END else "block"


def patch_operands(doc):
    """(address, size, relative) for every operand in every logic string."""
    out = []

    def strings(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if k in ("MemAddr", "Mem", "Definition", "RichPresencePatch") and isinstance(v, str):
                    yield v
                else:
                    yield from strings(v)
        elif isinstance(o, list):
            for v in o:
                yield from strings(v)

    for s in strings(doc):
        for line in s.splitlines():
            for group in re.split(r"(?<!0x)S|::", line):  # core/alts, leaderboard parts
                rel = False
                for cond in group.split("_"):
                    for m in OPERAND.finditer(cond):
                        size = SIZES.get(m.group(1), "f" + (m.group(2) or ""))
                        out.append((int(m.group(3), 16), size, rel))
                    rel = cond.startswith("I:")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="build/s1-matching/build/psx.map")
    ap.add_argument("--all", action="store_true", help="print the labels/block rows too")
    ap.add_argument("file")
    a = ap.parse_args()
    doc = json.load(open(a.file))
    syms = load_syms(a.map)
    keys = [s[0] for s in syms]

    rows = {}
    if isinstance(doc, dict) and "CodeNotes" in doc:
        for n in doc["CodeNotes"]:
            addr = int(n["Address"], 16)
            ptr = "pointer" in n["Note"].lower()
            rows[addr] = ("note", ptr, n["Note"].splitlines()[0][:60] if n["Note"] else "")
    else:
        for addr, size, rel in patch_operands(doc):
            if rel:
                key = ("offset", addr)
                rows.setdefault(key, ("offset", False, "after AddAddress, size " + size))
            else:
                rows.setdefault(addr, ("read", False, "size " + size))

    counts = collections.Counter()
    for key in sorted(rows, key=lambda k: (isinstance(k, tuple), k if isinstance(k, int) else k[1])):
        kind, ptr, text = rows[key]
        if isinstance(key, tuple):
            counts["offset"] += 1
            if a.all:
                print("+%06x %-18s %s" % (key[1], "offset", text))
            continue
        cls = classify(key)
        counts[cls] += 1
        i = bisect.bisect_right(keys, 0x80000000 | key) - 1
        sym = "%s+0x%x" % (syms[i][1], (0x80000000 | key) - syms[i][0]) if i >= 0 else ""
        if a.all or cls not in ("labels", "block") or ptr:
            print("%06x %-18s %-3s %-36s %s" % (key, cls, "PTR" if ptr else "", sym, text))
    print("#", ", ".join("%s %d" % kv for kv in sorted(counts.items())), file=sys.stderr)


if __name__ == "__main__":
    main()

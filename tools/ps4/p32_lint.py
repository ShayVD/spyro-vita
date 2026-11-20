#!/usr/bin/env python3
"""Pointer data labels declared without P32 (the low-image model, D174).

    tools/ps4/p32_lint.py MAP DIR...

Every file-scope declaration of a pointer (or an array of pointers) whose
name is a label in MAP (psx.map: the game's data, 4-byte pointers in the
image of PS1 RAM) must carry P32, or on linux64 it reads 8 bytes from a
4-byte slot. Scans the .c and .h files under each DIR (the port's src/game
and the patched decomp). Function pointers are listed apart: they must be
linux64's own globals (cmake/game.cmake --host-label, src/game/p32_fnptrs.c).
Exit status 1 when anything is found.
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(__file__))
from p32_patch import rewrite  # noqa: E402

FNPTR = re.compile(r"^\s*(?:extern\s+)?[\w\s*]+\(\s*\*\s*(\w+)\s*(?:\[[^\]]*\])?\s*\)\s*\(", re.M)
HOST_FN = {"g_SpawnMoby", "g_UpdateMoby", "g_FlightResultsUpdate", "D_8007567C", "g_SpawnParticle",
           "g_UpdateParticle", "D_800757A0", "D_800758A8", "D_8007574C", "D_800758D8", "D_80075694",
           "D_800757A8", "D_800758C4"}


def main(argv):
    labels = set()
    for line in open(argv[1]):
        m = re.match(r"^\s+0x[0-9a-fA-F]{8}\s+([A-Za-z_][\w.]*)\s*$", line)
        if m:
            labels.add(m.group(1))
    found = 0
    for d in argv[2:]:
        for dp, _, fs in os.walk(d):
            for f in sorted(fs):
                if not f.endswith((".c", ".h")):
                    continue
                p = os.path.join(dp, f)
                t = open(p, encoding="latin-1").read()
                _, ch = rewrite(t)
                for old, new, kind in ch:
                    name = re.findall(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*;$", old)
                    # Terminator_*: gen/ovl_table.c's own (overlay_pointers.h)
                    if kind == "global" and name and name[0] in labels \
                            and not name[0].startswith("Terminator_"):
                        print(f"{p}: no P32: {old}")
                        found += 1
                for m in FNPTR.finditer(t):
                    if m.group(1) in labels and m.group(1) not in HOST_FN:
                        print(f"{p}: function pointer label not linux64's own: {m.group(0).strip()}")
                        found += 1
    print(f"p32_lint: {found} found")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

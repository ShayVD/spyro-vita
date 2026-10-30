#!/usr/bin/env python3
"""List the GTE command instructions in the game's main executable (for the
oracle's GTE capture, docs/porting-plan.md §9 step 8): one hex address per
line, skipping commands in branch delay slots (their next instruction is not
addr+4).  tools/oracle/gte_sites.py EXE OUT.txt"""
import collections
import struct
import sys

# GTE command numbers (low 6 bits of the cop2 imm25 word), per psx-spx.
NAMES = {0x01: "RTPS", 0x06: "NCLIP", 0x0C: "OP", 0x10: "DPCS", 0x11: "INTPL", 0x12: "MVMVA",
         0x13: "NCDS", 0x14: "CDP", 0x16: "NCDT", 0x1B: "NCCS", 0x1C: "CC", 0x1E: "NCS", 0x20: "NCT",
         0x28: "SQR", 0x29: "DCPL", 0x2A: "DPCT", 0x2D: "AVSZ3", 0x2E: "AVSZ4", 0x30: "RTPT",
         0x3D: "GPF", 0x3E: "GPL", 0x3F: "NCCT"}


def is_branch(w):
    op = w >> 26
    if op in (2, 3) or 4 <= op <= 7 or op == 1:  # j jal beq bne blez bgtz, regimm
        return True
    return op == 0 and (w & 0x3F) in (8, 9)  # jr jalr


def main(exe, out):
    d = open(exe, "rb").read()
    taddr, tsize = struct.unpack("<II", d[0x18:0x20])
    words = struct.unpack(f"<{tsize // 4}I", d[0x800:0x800 + tsize])
    sites, count = [], collections.Counter()
    for i, w in enumerate(words):
        if w & 0xFE000000 != 0x4A000000:
            continue
        if i and is_branch(words[i - 1]):
            continue
        name = NAMES.get(w & 0x3F)
        if name is None:
            continue
        sites.append(taddr + 4 * i)
        count[name] += 1
    with open(out, "w") as f:
        f.writelines(f"{a:08X}\n" for a in sites)
    print(f"{len(sites)} GTE sites:", ", ".join(f"{k} {v}" for k, v in sorted(count.items())))


if __name__ == "__main__":
    main(*sys.argv[1:3])

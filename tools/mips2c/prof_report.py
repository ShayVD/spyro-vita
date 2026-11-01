#!/usr/bin/env python3
"""Rank translated code by cost from a profiling build's counts.

A build configured with -DSPYRO_M2C_PROF=ON counts every translated
instruction's executions and writes them out at exit (m2c_prof.bin, see
src/game/mips2c_rt.h M2C_PROF). This reads that table back against the
decomp's asm and prints, per function (glabel) and per hot loop (a run of
instructions between branch targets), the executed instructions, memory
accesses (each one a MIPS_KUSEG mapping in the translated C), GTE register
moves and GTE commands, plus a rough cost in ARM instructions.

usage: prof_report.py --root third_party/spyro-1 PROF.bin [PROF.bin...]
       [--units asm/renderers/r_environment.s ...] [--loops N] [--func NAME]
"""
import argparse
import glob
import os
import re
import struct
import sys

LO = 0x80010000
WORDS = 0x20000

LINE = re.compile(r"^\s*/\*\s*[0-9A-F]+\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s*\*/\s+(\S+)\s*(.*)$")
GLABEL = re.compile(r"^\s*glabel\s+(\S+)")
LABEL = re.compile(r"^\s*(\.L[0-9A-F]+|D_[0-9A-F]+|func_[0-9A-F]+)\s*:")

LOADS = {"lb", "lbu", "lh", "lhu", "lw", "lwl", "lwr", "lwc2"}
STORES = {"sb", "sh", "sw", "swl", "swr", "swc2"}
GTE_MOVES = {"mtc2", "mfc2", "ctc2", "cfc2", "lwc2", "swc2"}
# A rough cost per GTE command in ARM instructions through PsyCross's
# GTE_operator (decode, switch, out-of-line helpers): for ranking only.
GTE_COST = {"RTPS": 120, "RTPT": 340, "NCLIP": 40, "AVSZ3": 30, "AVSZ4": 34, "MVMVA": 70,
            "DPCS": 90, "INTPL": 90, "GPF": 60, "GPL": 66, "SQR": 50, "OP": 50, "CC": 110,
            "NCDS": 220, "NCCS": 200, "NCS": 150, "NCT": 420, "NCDT": 640, "NCCT": 600,
            "CDP": 180, "DCPL": 100, "DPCT": 260}
KUSEG_COST = 6   # MIPS_KUSEG on the Vita: two masks, two compares, an or
ALU_COST = 1.3   # registers as locals: mostly one ARM instruction, some two


def read_asm(root, units):
    insns = {}  # addr -> (op, args, func, is_label)
    for rel in units:
        func = None
        label_next = False
        with open(os.path.join(root, rel)) as f:
            for line in f:
                m = GLABEL.match(line)
                if m:
                    func = m.group(1)
                    label_next = True
                    continue
                if LABEL.match(line):
                    label_next = True
                    continue
                m = LINE.match(line)
                if not m:
                    continue
                addr = int(m.group(1), 16)
                insns[addr] = (m.group(3), m.group(4), func, label_next)
                label_next = False
    return insns


def cost_of(op, n):
    if op in GTE_COST or op.isupper():
        return n * GTE_COST.get(op, 80)
    if op in LOADS or op in STORES:
        return n * (1 + KUSEG_COST)
    if op in GTE_MOVES:
        return n * 2
    if op in ("mult", "multu", "div", "divu"):
        return n * 4
    return n * ALU_COST


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--units", nargs="*")
    ap.add_argument("--loops", type=int, default=25)
    ap.add_argument("--func", help="only this function's loops")
    ap.add_argument("prof", nargs="+")
    a = ap.parse_args()
    units = a.units or sorted(
        os.path.relpath(p, a.root) for p in glob.glob(os.path.join(a.root, "asm", "**", "*.s"), recursive=True))
    insns = read_asm(a.root, units)
    counts = [0] * WORDS
    for p in a.prof:
        data = open(p, "rb").read()
        for i, c in enumerate(struct.unpack(f"<{WORDS}I", data[:WORDS * 4])):
            counts[i] += c

    funcs = {}
    for addr, (op, args, func, _) in insns.items():
        n = counts[(addr - LO) >> 2] if LO <= addr < LO + WORDS * 4 else 0
        if not n:
            continue
        d = funcs.setdefault(func, {"insns": 0, "mem": 0, "moves": 0, "gte": {}, "cost": 0.0})
        d["insns"] += n
        if op in LOADS or op in STORES:
            d["mem"] += n
        if op in GTE_MOVES:
            d["moves"] += n
        if op.isupper():
            d["gte"][op] = d["gte"].get(op, 0) + n
        d["cost"] += cost_of(op, n)
    total = sum(d["cost"] for d in funcs.values()) or 1
    print(f"{'function':<16} {'cost%':>6} {'cost(M)':>9} {'insns(M)':>9} {'mem(M)':>8} {'gtemov(M)':>9}  GTE commands (K)")
    for name, d in sorted(funcs.items(), key=lambda kv: -kv[1]["cost"])[:40]:
        g = " ".join(f"{k}:{v // 1000}" for k, v in sorted(d["gte"].items(), key=lambda kv: -kv[1]))
        print(f"{name or '?':<16} {100 * d['cost'] / total:6.2f} {d['cost'] / 1e6:9.1f} {d['insns'] / 1e6:9.2f} "
              f"{d['mem'] / 1e6:8.2f} {d['moves'] / 1e6:9.2f}  {g}")

    # hot straight-line runs: from a label to the next label
    runs = []
    cur = None
    for addr in sorted(insns):
        op, args, func, is_label = insns[addr]
        if a.func and func != a.func:
            cur = None
            continue
        n = counts[(addr - LO) >> 2] if LO <= addr < LO + WORDS * 4 else 0
        if cur is None or is_label or func != cur["func"]:
            cur = {"func": func, "start": addr, "end": addr, "cost": 0.0, "max": 0, "gte": {}}
            runs.append(cur)
        cur["end"] = addr
        cur["cost"] += cost_of(op, n)
        cur["max"] = max(cur["max"], n)
        if op.isupper() and n:
            cur["gte"][op] = cur["gte"].get(op, 0) + n
    print()
    print(f"hottest runs (label to label){' in ' + a.func if a.func else ''}:")
    for r in sorted(runs, key=lambda r: -r["cost"])[:a.loops]:
        g = " ".join(f"{k}:{v // 1000}K" for k, v in r["gte"].items())
        print(f"  {r['func']:<14} {r['start']:08X}-{r['end'] + 4:08X} ({(r['end'] - r['start']) // 4 + 1:3} insns) "
              f"cost {100 * r['cost'] / total:5.2f}%  max count {r['max']}  {g}")


if __name__ == "__main__":
    sys.exit(main())

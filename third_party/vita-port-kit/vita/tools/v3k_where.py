#!/usr/bin/env python3
"""Where a Vita3K run went wrong: each invalid memory access or guest exception in Vita3K's output, with the PC and
LR of the CPU dump that follows it named by the nearest function in the build's unstripped ELF.

  targets/vita/tools/v3k_where.py OUT/vita3k.txt --elf build/game.elf [--nm NM] [--top 8]

Vita3K follows an "Invalid read of uint32_t at address: 0x10" (or a guest exception) with "PC: 0x810001de, SP: ...,
LR: 0x81000273" and, for an access, "Executing: ldr r0, [r0]". Those lines are logged at Vita3K's default ERROR level.
The eboot's load address comes from Vita3K's "Loaded module segment 0 @ [0xA - 0xB / 0xL] ... of module
app0:eboot.bin" line (INFO level, VITA3K_LOG=2): the slide is A - L. Without that line the slide is 0: Vita3K loads
the eboot at its link address (seen with Vita3K on macOS, 2026-11; a stub's main logged at its nm address), and the
report says it assumed so.

Prints one line per distinct (PC, LR, what) site, most frequent first; exit 0, or 2 for a usage error or an ELF nm
cannot read. --nm: default $VITASDK/bin/arm-vita-eabi-nm, else nm. Python 3, stdlib.
"""
import argparse
import bisect
import collections
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fnprof import symbols  # noqa: E402  (the same nm reading: text symbols, Thumb bit cleared)

INVALID = re.compile(r"Invalid (read|write) of uint\d+_t at addr(?:ess)?: *(0x[0-9a-fA-F]+)")
DUMP = re.compile(r"PC: *(0x[0-9a-fA-F]+),\s*SP: *0x[0-9a-fA-F]+,\s*LR: *(0x[0-9a-fA-F]+)")
EXEC = re.compile(r"Executing: *(.+?)\s*$")
SEGMENT = re.compile(r"Loaded module segment 0 @ \[(0x[0-9a-fA-F]+) - 0x[0-9a-fA-F]+ / (0x[0-9a-fA-F]+)\].*"
                     r"of module app0:eboot\.bin")


def default_nm():
    sdk = os.path.join(os.environ.get("VITASDK", "/usr/local/vitasdk"), "bin/arm-vita-eabi-nm")
    return sdk if os.path.exists(sdk) else (shutil.which("nm") or "nm")


def sites(text):
    """[(what, pc, lr, insn)]: each PC dump with the invalid access just before it (or 'exception') and the
    instruction Vita3K disassembled after it"""
    out, what, lines = [], None, text.splitlines()
    for i, l in enumerate(lines):
        m = INVALID.search(l)
        if m:
            what = f"invalid {m[1]} at {m[2]}"
            continue
        m = DUMP.search(l)
        if m:
            insn = next((EXEC.search(x)[1] for x in lines[i + 1:i + 16] if EXEC.search(x)), "")
            out.append((what or "guest exception", int(m[1], 16), int(m[2], 16), insn))
            what = None
    return out


def slide_of(text):
    m = SEGMENT.search(text)
    return (int(m[1], 16) - int(m[2], 16), "from Vita3K's segment line") if m else \
        (0, "assumed: Vita3K loads the eboot at its link address (VITA3K_LOG=2 logs it)")


def namer(addrs, names, slide):
    def name(x):
        a = (x & ~1) - slide
        i = bisect.bisect_right(addrs, a) - 1
        if i < 0:
            return "?"
        return names[i] if a == addrs[i] else f"{names[i]}+0x{a - addrs[i]:x}"
    return name


def report(text, addrs, names, top=8):
    ss = sites(text)
    if not ss:
        return []
    slide, how = slide_of(text)
    name = namer(addrs, names, slide)
    n = collections.Counter(ss)
    out = [f"slide 0x{slide:x} ({how})"]
    for (what, pc, lr, insn), k in n.most_common(top):
        out.append(f"{k:4d}  PC 0x{pc:08x} = {name(pc)}  LR 0x{lr:08x} = {name(lr)}  {what}"
                   + (f"  ({insn})" if insn else ""))
    if len(n) > top:
        out.append(f"      (+{len(n) - top} more sites)")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("log", help="Vita3K's output (v3k_run.sh's OUT/vita3k.txt)")
    ap.add_argument("--elf", required=True, help="the build's unstripped ELF")
    ap.add_argument("--nm", default=default_nm())
    ap.add_argument("--top", type=int, default=8)
    a = ap.parse_args()
    try:
        addrs, names = symbols(a.elf, a.nm)
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"v3k_where: cannot read {a.elf} with {a.nm}: {e}", file=sys.stderr)
        return 2
    with open(a.log, errors="replace") as f:
        for line in report(f.read(), addrs, names, a.top):
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())

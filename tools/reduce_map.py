#!/usr/bin/env python3
"""Cut the matching build's psx.map down to what the port's build reads, for
builds without the matching build (CI; decisions D57). The matching build
needs the user's own PsyQ headers, so CI can't make psx.map itself.

Kept: output and input section lines, symbol lines and `x = .` assignments,
from the memory map onward (no discarded sections, no LOAD lines, no linker
script text). tools/data2s.py, tools/psxmap.py and
tools/overlays/port_overlays.py read the result exactly as the full map.

  reduce_map.py build/s1-matching/build/psx.map tools/layout/psx.map
  reduce_map.py --check FULL REDUCED     exit 1 if REDUCED is stale
"""
import re
import sys

KEEP = [
    re.compile(r"^\.[\w.]+(\s+0x[0-9a-f]+\s+0x[0-9a-f]+.*)?$"),  # output section
    re.compile(r"^ \.[\w.]+(\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+\S+)?$"),  # input section
    re.compile(r"^\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+\S+$"),  # its continuation line
    re.compile(r"^\s+0x[0-9a-fA-F]{8}\s+[A-Za-z_.$][\w.$]*\s*$"),  # symbol
    re.compile(r"^\s+0x[0-9a-f]+\s+[A-Za-z_]\w* = \.$"),  # assignment
]


def reduce(path):
    out = ["# Reduced from the matching build's psx.map by tools/reduce_map.py;",
           "# do not edit (decisions D57)."]
    started = False
    kept = []
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("Linker script and memory map"):
            started = True
            continue
        if started:
            for i, k in enumerate(KEEP):
                if k.match(line):
                    kept.append((i, line))
                    break
    # An input section no symbol follows tells the readers nothing: drop it.
    for n, (i, line) in enumerate(kept):
        if i in (1, 2):
            rest = (j for j, _ in kept[n + 1:] if not (i == 1 and j == 2))
            if next(rest, 0) in (0, 1):
                continue
        out.append(line)
    return "\n".join(out) + "\n"


def main(argv):
    if argv[1] == "--check":
        if reduce(argv[2]) != open(argv[3]).read():
            sys.stderr.write(f"{argv[3]} is stale: rerun tools/reduce_map.py {argv[2]} {argv[3]}\n")
            return 1
        return 0
    with open(argv[2], "w") as f:
        f.write(reduce(argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

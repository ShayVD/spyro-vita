#!/usr/bin/env python3
"""Read the matching build's linker map (build/s1-matching/build/psx.map).

    from psxmap import load_map
    syms = load_map(path)    # {name: Sym(addr, obj, section)}

Command line:
    tools/psxmap.py MAP [SYMBOL...]       print addr, object, section
    tools/psxmap.py MAP --classify FILE   one line per symbol in FILE (one name
                                          per line): name addr object section
"""
import re
import sys
from collections import namedtuple

Sym = namedtuple("Sym", "name addr obj section")

_SECTION = re.compile(r"^ (\.[\w.]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$")
_SECTION_NAME_ONLY = re.compile(r"^ (\.[\w.]+)$")
_SECTION_CONT = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$")
_SYMBOL = re.compile(r"^\s+0x([0-9a-f]+)\s+([A-Za-z_.$][\w.$]*)$")


def load_map(path):
    """Symbols defined in input sections, keyed by name. Linker-script
    assignments (`x = .`) are skipped; they have no object."""
    syms = {}
    obj = section = None
    pending = None
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            m = _SECTION.match(line)
            if m:
                section, obj = m.group(1), m.group(4)
                continue
            m = _SECTION_NAME_ONLY.match(line)
            if m:
                pending = m.group(1)
                continue
            if pending:
                m = _SECTION_CONT.match(line)
                if m:
                    section, obj = pending, m.group(3)
                pending = None
                if m:
                    continue
            m = _SYMBOL.match(line)
            if m and obj and "=" not in line:
                name = m.group(2)
                if name not in syms:
                    syms[name] = Sym(name, int(m.group(1), 16), obj, section)
            elif line and not line.startswith(" "):
                obj = section = None  # top-level output section header
    return syms


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    syms = load_map(argv[1])
    names = argv[2:]
    if names[:1] == ["--classify"]:
        names = [n.strip() for n in open(names[1]) if n.strip()]
    for n in names:
        s = syms.get(n)
        if s:
            print(f"{n} 0x{s.addr:08x} {s.obj} {s.section}")
        else:
            print(f"{n} ? ? ?")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Byte differences between two save states (decisions D51), per chunk; for
the arena, as PS1 address ranges with the nearest psx.map symbol.

  savestate_diff.py A.bin B.bin [psx.map]
"""
import bisect
import re
import struct
import sys

HEADER = 8 + 4 + 4 + 64 + 6 * 4 + 4 * 4


def chunks(path):
    b = open(path, "rb").read()
    size = struct.unpack_from("<I", b, 12)[0]
    o, out = size, {}
    while o + 8 <= len(b):
        tag, n = b[o:o + 4].decode(), struct.unpack_from("<I", b, o + 4)[0]
        out[tag] = b[o + 8:o + 8 + n]
        o += 8 + n
    return out


def symbols(path):
    syms = []
    for line in open(path):
        m = re.match(r"\s+0x(800[0-9a-f]{5})\s+([A-Za-z_]\w*)\s*$", line)
        if m:
            syms.append((int(m.group(1), 16), m.group(2)))
    syms.sort()
    return syms


def main():
    a, b = chunks(sys.argv[1]), chunks(sys.argv[2])
    syms = symbols(sys.argv[3]) if len(sys.argv) > 3 else []
    keys = [s[0] for s in syms]
    for tag in a:
        x, y = a[tag], b.get(tag, b"")
        if x == y:
            continue
        print(f"{tag}: differs" + (f" (lengths {len(x)} {len(y)})" if len(x) != len(y) else ""))
        ranges, i, n = [], 0, min(len(x), len(y))
        while i < n:
            if x[i] != y[i]:
                j = i
                while j < n and (x[j] != y[j] or (j + 16 < n and x[j:j + 16] != y[j:j + 16])):
                    j += 1
                ranges.append((i, j))
                i = j
            i += 1
        for lo, hi in ranges[:40]:
            where = f"+{lo:#x}"
            if tag == "ARNA":
                addr = 0x80000000 + lo
                k = bisect.bisect_right(keys, addr) - 1
                sym = f" {syms[k][1]}+{addr - syms[k][0]:#x}" if k >= 0 else ""
                where = f"{addr:08x}-{0x80000000 + hi:08x}{sym}"
            print(f"  {where} ({hi - lo} bytes)")
        if len(ranges) > 40:
            print(f"  ... {len(ranges)} ranges")


if __name__ == "__main__":
    main()

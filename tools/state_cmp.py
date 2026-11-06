#!/usr/bin/env python3
"""Compare two save states (src/game/savestate.c's format) from two builds.

  tools/state_cmp.py A.bin B.bin

No build loads another build's state (D51: the header's build ID and layout
words), rightly, since a state holds code and data addresses. What a change
that keeps the format can show instead (D115) is two states saved at the same
vblank, one by each build, that differ only in those addresses. This prints
the header fields, then each chunk's tag and length and, where its bytes
differ, the differing 32-bit words grouped by the shift between A's and B's
value (a code shift, a data shift). Exit 0 if the format matches (the same
chunks with the same lengths) and every differing word is an address: each
value inside its own build's image (from the 16 MB below the lowest of the
header's two function addresses up to its bss end) and the two less than
1 MB apart; 1 otherwise. Code moves unevenly, so two code pointers can shift
by different amounts."""
import collections
import struct
import sys

HDR = struct.Struct("<8sII64s6I3iI")  # savestate.c's Header


def read(path):
    d = open(path, "rb").read()
    h = HDR.unpack_from(d, 0)
    hdr = dict(magic=h[0], format=h[1], header_size=h[2], build=h[3].rstrip(b"\0").decode(),
               layout=h[4:10], vblank=h[10], gamestate=h[11], level=h[12], nchunks=h[13])
    o = hdr["header_size"]
    chunks = []
    for _ in range(hdr["nchunks"]):
        tag, n = struct.unpack_from("<4sI", d, o)
        o += 8
        chunks.append((tag.decode(), d[o:o + n]))
        o += n
    return hdr, chunks


def main():
    ha, ca = read(sys.argv[1])
    hb, cb = read(sys.argv[2])
    ok = True
    for k in ha:
        same = ha[k] == hb[k]
        if k == "layout":
            print("header layout:", " ".join(f"{a:#x}/{b:#x}" for a, b in zip(ha[k], hb[k])))
        else:
            print(f"header {k}: {'same' if same else 'differs'}: {ha[k]!r}" + ("" if same else f" / {hb[k]!r}"))
        if k in ("magic", "format", "header_size", "nchunks") and not same:
            ok = False
    def image(h):  # layout: format 2 [4], [5] functions, [3] bss end (format 1 the same)
        return min(h["layout"][4], h["layout"][5]) & ~0xFFFFFF, h["layout"][3]
    ia, ib = image(ha), image(hb)
    if len(ca) != len(cb):
        ok = False
    identical = 0
    for (ta, xa), (tb, xb) in zip(ca, cb):
        if ta != tb or len(xa) != len(xb):
            print(f"chunk {ta}/{tb}: length {len(xa)}/{len(xb)}: the format differs")
            ok = False
            continue
        if xa == xb:
            identical += 1
            print(f"chunk {ta}: {len(xa)} bytes, identical")
            continue
        words = sorted({i & ~3 for i in range(len(xa)) if xa[i] != xb[i]})
        kinds = collections.Counter()
        for w in words:
            if w + 4 > len(xa):
                kinds[("tail", None)] += 1
                ok = False
                continue
            a, b = struct.unpack_from("<I", xa, w)[0], struct.unpack_from("<I", xb, w)[0]
            d = (a - b) & 0xFFFFFFFF
            kinds[(f"{a >> 20 << 20:#x}", d)] += 1
            if not (ia[0] <= a < ia[1] and ib[0] <= b < ib[1] and min(d, (1 << 32) - d) < 1 << 20):
                print(f"  {ta}+{w:#x}: {a:#010x} / {b:#010x} is not an address of either build")
                ok = False
        desc = ", ".join(f"{n} near {r} moved by {d if d is None or d < 0x80000000 else d - (1 << 32):+d}"
                         for (r, d), n in sorted(kinds.items(), key=lambda x: -x[1]))
        print(f"chunk {ta}: {len(xa)} bytes, {len(words)} words differ: {desc}")
    print(f"{identical} of {len(ca)} chunks identical; images {ia[0]:#x}-{ia[1]:#x} / {ib[0]:#x}-{ib[1]:#x}")
    print("OK: the same format, only addresses moved" if ok else "DIFFERENT: see above")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

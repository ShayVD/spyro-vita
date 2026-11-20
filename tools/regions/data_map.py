#!/usr/bin/env python3
"""Where the decomp's NTSC-U data labels are in another release (D133,
docs/regions-plan.md §6.2).

For every function compare_code.py finds "same" (the instructions equal with
relocations masked), the data addresses the US code builds (lui + addiu, ori,
or a load or store on the same register) are paired, instruction by
instruction, with the ones the other release's code builds. A pair whose US
address is a label of the matching build's psx.map maps that label. A label
two functions place differently is reported and left out.

It prints and writes addresses, sizes and names only: no game data.

Usage (from the repo root):
  python3 -I tools/regions/data_map.py disc/files-pal/SCES_014.38 [--tsv OUT] [--labels A,B,...]
US code: the matching build (build/s1-matching/build/PSX.EXE, psx.elf, psx.map).
"""
import argparse
import collections
import difflib
import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = 0x80010000
LO_OPS = {0x09, 0x0D, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B}


def load_compare():
    spec = importlib.util.spec_from_file_location("compare_code", os.path.join(HERE, "compare_code.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def exe_words(path):
    b = open(path, "rb").read()[0x800:]
    n = len(b) // 4
    return struct.unpack("<%dI" % n, b[: n * 4])


def refs(W, start, size):
    """(word index in the function, address) of each lui-built address."""
    out = []
    i0 = (start - BASE) // 4
    end = min(i0 + size // 4, len(W))
    for i in range(i0, end):
        w = W[i]
        if w >> 26 != 0x0F:
            continue
        rt, hi = (w >> 16) & 31, w & 0xFFFF
        for j in range(i + 1, end):
            x = W[j]
            op = x >> 26
            if op in LO_OPS and ((x >> 21) & 31) == rt:
                imm = x & 0xFFFF
                if op != 0x0D and imm & 0x8000:
                    imm -= 0x10000
                out.append((j - i0, ((hi << 16) + imm) & 0xFFFFFFFF))
            if op == 0x0F and ((x >> 16) & 31) == rt:
                break
    return out


def map_labels(path):
    """psx.map's labels: address -> name, and sorted addresses (for sizes)."""
    labels = {}
    for line in open(path):
        m = re.match(r"^\s+0x([0-9a-f]{8})\s+([A-Za-z_]\w*)\s*$", line)
        if m:
            a = int(m.group(1), 16)
            if 0x80010000 <= a < 0x80200000:
                labels.setdefault(a, m.group(2))
    return labels, sorted(labels)


def aligned(cc, res, US, OT, labels, code):
    """--aligned (D143): the functions that aren't "same", paired inside the
    blocks difflib aligns between their placed neighbours (as compare_code's
    coverage does). Returns {us address: Counter(other address)}."""
    T = [cc.norm(w) for w in OT]
    seen = collections.defaultdict(collections.Counter)
    for i, r in enumerate(res):
        if r["hits"]:
            continue
        prev = next((res[j] for j in range(i - 1, -1, -1) if len(res[j]["hits"]) == 1), None)
        nxt = next((res[j] for j in range(i + 1, len(res)) if len(res[j]["hits"]) == 1), None)
        lo = prev["hits"][0] + prev["size"] // 4 if prev else 0
        hi = nxt["hits"][0] if nxt else len(T)
        if hi <= lo or hi - lo > 40000:
            continue
        i0 = (r["addr"] - BASE) // 4
        U = [cc.norm(w) for w in US[i0: i0 + r["size"] // 4]]
        u = dict(refs(US, r["addr"], r["size"]))
        o = dict(refs(OT, BASE + lo * 4, (hi - lo) * 4))
        sm = difflib.SequenceMatcher(None, U, T[lo:hi], autojunk=False)
        for blk in sm.get_matching_blocks():
            for k in range(blk.size):
                a, b = u.get(blk.a + k), o.get(blk.b + k)
                if a in labels and a not in code and b is not None:
                    seen[a][b] += 1
    return seen


def build(other_exe, us_exe, elf, psxmap, loose=False):
    cc = load_compare()
    d, secs, names, funcs = cc.elf_sections_and_funcs(elf)
    i = [k for k, v in names.items() if v == ".main"][0]
    addr, body = secs[i][3], d[secs[i][4]: secs[i][4] + secs[i][5]]
    target = open(other_exe, "rb").read()[0x800:]
    res = cc.compare(body, addr, funcs[".main"], target)
    US, OT = exe_words(us_exe), exe_words(other_exe)
    labels, order = map_labels(psxmap)
    code = {r["addr"] for r in res}
    seen = collections.defaultdict(collections.Counter)
    for r in res:
        if len(r["hits"]) != 1:
            continue
        ot = BASE + r["hits"][0] * 4
        u = dict(refs(US, r["addr"], r["size"]))
        for k, b in refs(OT, ot, r["size"]):
            a = u.get(k)
            if a in labels and a not in code:
                seen[a][b] += 1
    if loose:
        for a, c in aligned(cc, res, US, OT, labels, code).items():
            if a not in seen:
                seen[a] = c
    out, conflicts = [], []
    for a in sorted(seen):
        (b, n), *rest = seen[a].most_common()
        nxt = next((x for x in order if x > a), a)
        if rest:
            conflicts.append((labels[a], a, dict(seen[a])))
            continue
        out.append((labels[a], a, b, nxt - a, n))
    return out, conflicts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("other_exe")
    ap.add_argument("--us-exe", default="build/s1-matching/build/PSX.EXE")
    ap.add_argument("--elf", default="build/s1-matching/build/psx.elf")
    ap.add_argument("--map", default="build/s1-matching/build/psx.map")
    ap.add_argument("--tsv")
    ap.add_argument("--labels", help="print only these labels")
    ap.add_argument("--aligned", action="store_true",
                    help="also place labels only functions that aren't the same use, through "
                         "their difflib-aligned blocks (less sure: check by reading the code)")
    a = ap.parse_args()
    out, conflicts = build(a.other_exe, a.us_exe, a.elf, a.map, a.aligned)
    want = set(a.labels.split(",")) if a.labels else None
    lines = [f"{n}\t{u:08x}\t{o:08x}\t{s}\t{c}" for n, u, o, s, c in out if not want or n in want]
    print(f"{len(out)} labels mapped, {len(conflicts)} placed differently by two functions (left out)")
    for n, u, seen in conflicts:
        print(f"  conflict {n} {u:08x}: " + ", ".join(f"{o:08x}x{c}" for o, c in seen.items()))
    if a.tsv:
        with open(a.tsv, "w") as f:
            f.write("label\tus\tother\tus_size\trefs\n" + "\n".join(lines) + "\n")
    else:
        print("label\tus\tother\tus_size\trefs")
        print("\n".join(lines))


if __name__ == "__main__":
    sys.exit(main())

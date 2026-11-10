#!/usr/bin/env python3
"""Compare the decomp's NTSC-U functions with another release's code (D131,
docs/regions-plan.md §2).

For every function symbol of the matching build's ELF (build/s1-matching/
build/psx.elf, tools/build_matching.sh) it looks for the same instructions in
the other release's executable or WAD overlay, with relocations masked: the
targets of j/jal, lui immediates, and the immediates of addiu/ori/loads/stores
whose base isn't $sp. A function found once is "same"; for the others the
coverage is the share of its masked instructions that difflib finds, in order,
between the neighbouring functions that were found (an estimate: a changed
function's counterpart is looked for in that gap only).

--exact (D133) also compares the constants the mask hides but no relocation
uses: slti/sltiu/andi/xori immediates, and addiu/ori from $zero (li). A
region edit (RV/RT in the decomp's C) is proved by its function being "same"
with --exact in that region's matching build (tools/build_matching.sh
REGION=pal). --only NAME,... prints those functions' results.

It prints names, addresses, sizes and ratios: no game data. Inputs come from the
player's own disc (disc/, gitignored).

Usage (from the repo root):
  python3 -I tools/regions/compare_code.py exe  disc/files-pal/SCES_014.38 [--json OUT]
  python3 -I tools/regions/compare_code.py wad  disc/files-pal/WAD.WAD pal [--json OUT]
The WAD form compares every level overlay, the title screen's and the credits'
at that region's WAD index (WAD_LAYOUT below).
"""
import argparse
import difflib
import json
import struct
import sys

ELF = "build/s1-matching/build/psx.elf"
EXE_BASE = 0x80010000

# WAD index of: the first level's overlay (levels every 2 entries), the credits
# overlay and the title screen's overlay (docs/regions-plan.md §2.3).
WAD_LAYOUT = {"us": (9, 81, 2), "pal": (21, 93, 2), "jp": (22, 95, 3)}
LEVELS = [f"LEVEL_{w}{l}" for w in range(1, 7) for l in range(6)][:35]

# I-type opcodes whose immediate is (usually) an address part: masked unless
# the base register is $sp.
MASK_IMM = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x20, 0x21, 0x22, 0x23,
            0x24, 0x25, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x32, 0x3A}
K = 4  # words in the index key
EXACT = False  # --exact: keep the immediates no relocation uses
NEVER_RELOC = {0x0A, 0x0B, 0x0C, 0x0E}  # slti, sltiu, andi, xori


def norm(w):
    op = w >> 26
    if op in (2, 3):
        return w & 0xFC000000
    if op == 0x0F:
        return w & 0xFFFF0000
    if EXACT and (op in NEVER_RELOC or (op in (0x09, 0x0D) and (w >> 21) & 31 == 0)):
        return w
    if op in MASK_IMM and ((w >> 21) & 31) != 29:
        return w & 0xFFFF0000
    return w


def words(b):
    n = len(b) // 4
    return list(struct.unpack("<%dI" % n, b[: n * 4]))


def elf_sections_and_funcs(path):
    d = open(path, "rb").read()
    shoff, = struct.unpack_from("<I", d, 0x20)
    es, n, si = struct.unpack_from("<HHH", d, 0x2E)
    secs = [struct.unpack_from("<10I", d, shoff + i * es) for i in range(n)]
    st = secs[si]

    def cstr(base, off):
        return d[base + off: d.index(b"\0", base + off)].decode()

    names = {i: cstr(st[4], s[0]) for i, s in enumerate(secs)}
    funcs = {}
    for s in secs:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 16):
            nm, val, size, info, _, shn = struct.unpack_from("<IIIBBH", d, s[4] + i * 16)
            if info & 15 == 2 and size > 0 and shn < len(secs):
                name = cstr(strtab[4], nm)
                if not name.endswith(".NON_MATCHING"):
                    funcs.setdefault(names[shn], set()).add((val, size, name))
    return d, secs, names, funcs


def compare(us_body, us_addr, funcs, target):
    T = [norm(w) for w in words(target)]
    index = {}
    for i in range(len(T) - K):
        index.setdefault(tuple(T[i:i + K]), []).append(i)
    res = []
    for v, sz, name in sorted(funcs):
        U = [norm(w) for w in words(us_body[v - us_addr: v - us_addr + sz])]
        if len(U) >= K:
            hits = [c for c in index.get(tuple(U[:K]), []) if T[c:c + len(U)] == U]
        else:
            hits = [i for i in range(len(T) - len(U)) if T[i:i + len(U)] == U][:1]
        res.append({"name": name, "addr": v, "size": sz, "hits": hits, "U": U})
    for i, r in enumerate(res):
        if r["hits"]:
            r["coverage"] = 1.0
            continue
        prev = next((res[j] for j in range(i - 1, -1, -1) if len(res[j]["hits"]) == 1), None)
        nxt = next((res[j] for j in range(i + 1, len(res)) if len(res[j]["hits"]) == 1), None)
        lo = prev["hits"][0] + prev["size"] // 4 if prev else 0
        hi = nxt["hits"][0] if nxt else len(T)
        if hi <= lo or hi - lo > 40000:
            r["coverage"] = None  # not measured
            continue
        sm = difflib.SequenceMatcher(None, r["U"], T[lo:hi], autojunk=False)
        r["coverage"] = round(sum(b.size for b in sm.get_matching_blocks()) / len(r["U"]), 3)
        r["gap_words"] = hi - lo
    for r in res:
        del r["U"]
    return res


def summary(label, res):
    tot = sum(r["size"] for r in res)
    same = sum(r["size"] for r in res if r["hits"])
    cov = sum(r["size"] * (r["coverage"] or 0) for r in res)
    print(f"{label}: {len(res)} functions, {tot} bytes; same {sum(1 for r in res if r['hits'])} "
          f"({100 * same / max(tot, 1):.1f}% of bytes); weighted coverage {100 * cov / max(tot, 1):.1f}%")
    return tot, same, cov


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=("exe", "wad"))
    ap.add_argument("path")
    ap.add_argument("region", nargs="?")
    ap.add_argument("--elf", default=ELF)
    ap.add_argument("--json")
    ap.add_argument("--list", action="store_true", help="list the functions that differ")
    ap.add_argument("--exact", action="store_true", help="compare li/slti/andi constants too")
    ap.add_argument("--only", help="print these functions' results (comma-separated)")
    a = ap.parse_args()
    global EXACT
    EXACT = a.exact
    d, secs, names, funcs = elf_sections_and_funcs(a.elf)

    def sec(name):
        i = [k for k, v in names.items() if v == name][0]
        return secs[i][3], d[secs[i][4]: secs[i][4] + secs[i][5]]

    out = {}
    if a.kind == "exe":
        addr, body = sec(".main")
        target = open(a.path, "rb").read()[0x800:]
        res = compare(body, addr, funcs[".main"], target)
        summary(".main", res)
        out[".main"] = res
    else:
        lv, cr, ti = WAD_LAYOUT[a.region]
        f = open(a.path, "rb")
        hdr = f.read(0x800)
        jobs = [(n, lv + 2 * k) for k, n in enumerate(LEVELS)] + [("credits", cr), ("titlescreen_code", ti)]
        T = S = C = 0
        for n, i in jobs:
            off, ln = struct.unpack_from("<II", hdr, i * 8)
            f.seek(off)
            addr, body = sec("." + n)
            res = compare(body, addr, funcs["." + n], f.read(ln))
            t, s, c = summary(f"{n} (WAD {i})", res)
            T, S, C = T + t, S + s, C + c
            out["." + n] = res
        print(f"all overlays: {T} bytes; same {100 * S / T:.1f}%; weighted coverage {100 * C / T:.1f}%")
    if a.only:
        want = set(a.only.split(","))
        for sname, res in out.items():
            for r in res:
                if r["name"] in want:
                    print(f"  {sname} {r['name']} {r['addr']:08x} {r['size']}: "
                          + (f"same at {0x80010000 + 4 * r['hits'][0]:08x}" if len(r["hits"]) == 1
                             else f"differs, coverage={r['coverage']}"))
    if a.list:
        for sname, res in out.items():
            for r in res:
                if not r["hits"]:
                    print(f"  {sname} {r['name']} {r['addr']:08x} {r['size']} coverage={r['coverage']}")
    if a.json:
        json.dump(out, open(a.json, "w"), indent=0)


if __name__ == "__main__":
    sys.exit(main())

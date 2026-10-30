#!/usr/bin/env python3
"""Compare struct/union layouts between two `readelf --debug-dump=info`
dumps (docs/porting-plan.md §4.4 "Struct layout", §9 step 4).

    tools/layoutcheck.py MIPS_DUMP HOST_DUMP      (tools/layoutcheck.sh makes both)

MIPS_DUMP comes from the decomp's C compiled for mipsel against Sony's PsyQ
headers, HOST_DUMP from the port's build (i686, PsyCross headers). Every
struct or union known by the same name in both (tag or typedef name) must
have the same size and the same member offsets: tools/mips2c output reads
fields at raw offsets, so a difference is a bug. Exit status 1 on a mismatch.
"""
import re
import sys

DIE = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+ \((DW_TAG_\w+)\)")
ATTR = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$")


def parse(path):
    """{name: (size, {member: offset})} for named structs/unions. The dump may
    hold many objects; DIE offsets are only unique within one object."""
    out = {}
    dies, stack = {}, []

    def name_of(d):
        n = d["attrs"].get("DW_AT_name", "")
        return n.split(":")[-1].strip() if n else None

    def layout(d):
        size = d["attrs"].get("DW_AT_byte_size")
        if size is None:
            return None
        mem = {}
        for c in d["children"]:
            if c["tag"] != "DW_TAG_member":
                continue
            loc = c["attrs"].get("DW_AT_data_member_location", "0")
            mo = re.search(r"(-?\d+)\s*$", loc) or re.search(r"DW_OP_plus_uconst:\s*(\d+)", loc)
            mn = name_of(c) or f"<anon@{loc}>"
            if "DW_AT_bit_size" in c["attrs"] or "DW_AT_data_bit_offset" in c["attrs"]:
                mn += ":bits"
            mem[mn] = int(mo.group(1)) if mo else loc
        return int(size, 0), mem

    def flush():
        for d in dies.values():
            if d["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type"):
                n, lay = name_of(d), layout(d)
                if n and lay:
                    out.setdefault(n, lay)
            elif d["tag"] == "DW_TAG_typedef":
                n = name_of(d)
                m = re.search(r"<0x([0-9a-f]+)>", d["attrs"].get("DW_AT_type", ""))
                if n and m and int(m.group(1), 16) in dies:
                    td = dies[int(m.group(1), 16)]
                    if td["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type"):
                        lay = layout(td)
                        if lay:
                            out.setdefault(n, lay)
        dies.clear()
        stack.clear()

    for line in open(path, errors="replace"):
        if line.startswith("Contents of the .debug_info section"):
            flush()
            continue
        m = DIE.match(line)
        if m:
            depth, off, tag = int(m.group(1)), int(m.group(2), 16), m.group(3)
            d = {"tag": tag, "attrs": {}, "children": []}
            dies[off] = d
            while stack and stack[-1][0] >= depth:
                stack.pop()
            if stack:
                stack[-1][1]["children"].append(d)
            stack.append((depth, d))
            continue
        m = ATTR.match(line)
        if m and stack:
            stack[-1][1]["attrs"][m.group(1)] = m.group(2).strip()
    flush()
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    mips, host = parse(argv[1]), parse(argv[2])
    common = sorted(set(mips) & set(host))
    bad = 0
    for n in common:
        (ms, mm), (hs, hm) = mips[n], host[n]
        diffs = []
        if ms != hs:
            diffs.append(f"size {ms} vs {hs}")
        for f, o in mm.items():
            if f in hm and hm[f] != o:
                diffs.append(f"{f} @{o} vs @{hm[f]}")
        if diffs:
            bad += 1
            print(f"MISMATCH {n}: " + "; ".join(diffs))
    only_mips = sorted(set(mips) - set(host))
    print(f"layoutcheck: {len(common)} types compared, {bad} mismatched; "
          f"{len(only_mips)} only in the MIPS build")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

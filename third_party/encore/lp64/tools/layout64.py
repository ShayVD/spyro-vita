#!/usr/bin/env python3
"""Struct layouts, the 32-bit build against the 64-bit one (the low-image model, lp64/docs/low-image.md).

    python3 lp64/tools/layout64.py DUMP32 DUMP64 [--host-only FILE]

DUMP32 and DUMP64 are `readelf --debug-dump=info` of the two builds' executables (i686-linux-gnu-readelf,
x86_64-linux-gnu-readelf; build both with -g). Every struct or union known by the same name (tag or typedef) in both
must have the same size and member offsets: the game's memory is laid out as on the original, P32 keeps its pointer
fields 4 bytes. Host-only types, which may hold 8-byte pointers, are listed in FILE, one a line: the name, then why
(`#` comments). A listed type that no longer differs, or isn't in both builds, is reported as stale. Exit status 1
on a difference not listed or a stale entry.

From spyro-vita tools/ps4/layout64.py and the parser of tools/layoutcheck.py @ c25a3ea (MIT; D174). Spyro's own list
(tools/ps4/layout64_host_only.txt, 65 types) stays there; lp64/tools/README.md says what belongs in one.
"""
import re, sys

DIE = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+ \((DW_TAG_\w+)\)")
ATTR = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$")


def parse(path):
    """{name: (size, {member: offset})} for named structs/unions and typedefs of them. The dump may hold many
    compilation units; DIE offsets are only unique within one."""
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
    args = [a for a in argv[1:] if not a.startswith("--")]
    host_only = {}
    if "--host-only" in argv:
        path = argv[argv.index("--host-only") + 1]
        args.remove(path)
        for line in open(path):
            line = line.split("#")[0].strip()
            if line:
                host_only[line.split()[0]] = line
    if len(args) != 2:
        print(__doc__)
        return 2
    a, b = parse(args[0]), parse(args[1])
    common = sorted(set(a) & set(b))
    bad, allowed = [], []
    for n in common:
        (s32, m32), (s64, m64) = a[n], b[n]
        diffs = []
        if s32 != s64:
            diffs.append(f"size {s32} vs {s64}")
        for f, o in m32.items():
            if f in m64 and m64[f] != o:
                diffs.append(f"{f} @{o} vs @{m64[f]}")
        if diffs:
            (allowed if n in host_only else bad).append(f"{n}: " + "; ".join(diffs))
    for x in bad:
        print("MISMATCH " + x)
    for x in allowed:
        print("host-only " + x)
    stale = sorted(set(host_only) - {x.split(":")[0] for x in allowed})
    for n in stale:
        print(f"STALE host-only entry (no difference, or not in both builds): {n}")
    print(f"layout64: {len(common)} types compared, {len(bad)} mismatched, "
          f"{len(allowed)} host-only differences, {len(stale)} stale entries")
    return 1 if bad or stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Struct layouts, linux32 against linux64 (the low-image model, D174).

    tools/ps4/layout64.py DUMP32 DUMP64 [--host-only FILE]

DUMP32 and DUMP64 are `readelf --debug-dump=info` of the two builds' spyro
executables (i686-linux-gnu-readelf, x86_64-linux-gnu-readelf). Every
struct or union known by the same name in both must have the same size and
member offsets: the game's memory is laid out as on the PS1, P32 keeps its
pointer fields 4 bytes. Host-only types, which may hold 8-byte pointers,
are listed with a reason in FILE (tools/ps4/layout64_host_only.txt); a type
listed there that holds no difference any more is reported too. Exit status
1 on a difference not listed.
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from layoutcheck import parse  # noqa: E402


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

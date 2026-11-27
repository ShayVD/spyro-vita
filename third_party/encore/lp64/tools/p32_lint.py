#!/usr/bin/env python3
"""Pointer data labels declared without P32 (the low-image model, lp64/docs/low-image.md).

    python3 lp64/tools/p32_lint.py LABELS DIR... [--host-fn NAME]... [--host-fn-file FILE] [--ignore-prefix P]...

LABELS names the game's data labels: a GNU ld map (lines `  0x8007582c  g_Name`, as a decomp's splat/ld map has them)
or a plain list, one name a line (`#` comments). Every file-scope declaration of a pointer (or an array of pointers)
whose name is a label must carry P32, or on the 64-bit build it reads 8 bytes from a 4-byte slot. Scans the .c and .h
files under each DIR (the port's own C and the patched decomp). A function pointer under a label's name is reported
too, unless it is one of the port's own 8-byte globals named by --host-fn (repeatable) or --host-fn-file (one a line):
a call through a __ptr32 function pointer miscompiles (lp64/docs/clang-bugs.md). Names starting with an
--ignore-prefix are skipped (spyro-vita: `Terminator_`, its overlay table's own). Exit status 1 when anything is found.

From spyro-vita tools/ps4/p32_lint.py @ c25a3ea (MIT; D174). Its 13 overlay function pointers (HOST_FN) are
--host-fn-file, its `Terminator_` rule --ignore-prefix.
"""
import argparse, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from p32_patch import rewrite  # noqa: E402

FNPTR = re.compile(r"^\s*(?:extern\s+)?[\w\s*]+\(\s*\*\s*(\w+)\s*(?:\[[^\]]*\])?\s*\)\s*\(", re.M)
MAP_LINE = re.compile(r"^\s+0x[0-9a-fA-F]{8,16}\s+([A-Za-z_][\w.]*)\s*$")


def read_labels(path):
    labels, plain = set(), set()
    for line in open(path, errors="replace"):
        m = MAP_LINE.match(line)
        if m:
            labels.add(m.group(1))
            continue
        s = line.split("#")[0].strip()
        if re.fullmatch(r"[A-Za-z_]\w*", s):
            plain.add(s)
    return labels or plain


def read_names(path):
    return {l.split("#")[0].strip() for l in open(path) if l.split("#")[0].strip()}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("labels")
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--host-fn", action="append", default=[])
    ap.add_argument("--host-fn-file")
    ap.add_argument("--ignore-prefix", action="append", default=[])
    a = ap.parse_args(argv)
    labels = read_labels(a.labels)
    host_fn = set(a.host_fn) | (read_names(a.host_fn_file) if a.host_fn_file else set())
    ignored = tuple(a.ignore_prefix)
    found = 0
    for d in a.dirs:
        for dp, dns, fs in os.walk(d):
            dns.sort()
            for f in sorted(fs):
                if not f.endswith((".c", ".h")):
                    continue
                p = os.path.join(dp, f)
                t = open(p, encoding="latin-1").read()
                _, ch = rewrite(t)
                for old, new, kind in ch:
                    name = re.findall(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*;$", old)
                    if kind == "global" and name and name[0] in labels \
                            and not (ignored and name[0].startswith(ignored)):
                        print(f"{p}: no P32: {old}")
                        found += 1
                for m in FNPTR.finditer(t):
                    if m.group(1) in labels and m.group(1) not in host_fn:
                        print(f"{p}: function pointer label not the port's own: {m.group(0).strip()}")
                        found += 1
    print(f"p32_lint: {len(labels)} labels, {found} found")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())

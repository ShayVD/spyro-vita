#!/usr/bin/env python3
"""O2's completeness check (D250, D258): compile every linux64 unit with the
offset model (-DSPYRO_P32_OFFSET=1, src/compat/p32.h), where a P32 slot is a
union. A read or write of a slot that doesn't go through an accessor, and an
accessor on something that isn't a slot, are compile errors there.

    tools/tc python3 tools/ps4/p32_offset_check.py BUILD [--jobs N] [--out FILE]

BUILD as for p32_offset_rewrite.py (the units are the same: the overlays from
their own sources). Syntax only. Prints the errors and the
-Wincompatible-pointer-types warnings that matter, each counted once per
file:line, and exits 1 if there is any. A warning between two slot unions
(`P32F(Moby *)` spelled twice is two unnamed unions of one layout) is
harmless and not counted; one between a slot and anything else (a slot's
address passed as a `Moby **`) is a bug on ARM64 and is.
"""
import argparse, collections, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from p32_offset_rewrite import units  # noqa: E402

DIAG = re.compile(r"^(?P<file>[^\s:]+):(?P<line>\d+):(?P<col>\d+): (?P<sev>error|warning): (?P<msg>.*?)(?: \[(?P<flag>-W[^\]]+)\])?$", re.M)
SLOT = "p32_ty"


def run(u):
    cmd = u.cmd + ["-fsyntax-only", "-DSPYRO_P32_OFFSET=1", "-fno-color-diagnostics", "-ferror-limit=0",
                   "-Wno-everything", "-Wincompatible-pointer-types"]
    p = subprocess.run(cmd, cwd=u.d, capture_output=True, text=True, errors="replace")
    return u.src, p.returncode, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--out")
    a = ap.parse_args()
    us = units(os.path.abspath(a.build))
    seen, kinds, failed = set(), collections.Counter(), []
    out = open(a.out, "w") if a.out else None
    with ThreadPoolExecutor(a.jobs) as ex:
        for src, rc, err in ex.map(run, us):
            if rc:
                failed.append(src)
            for m in DIAG.finditer(err):
                msg = m.group("msg")
                if m.group("sev") == "warning":
                    if m.group("flag") != "-Wincompatible-pointer-types":
                        continue
                    # only a slot against something else counts: two slot
                    # unions are two spellings of one layout, and two
                    # non-slots (`long *` and `int *`) are the decomp's own
                    n = len(re.findall(r"unnamed(?: union)? at|p32_ty|\b\w+Slot \*'", msg))
                    if n != 1:
                        continue
                key = (m.group("file"), m.group("line"), msg)
                if key in seen:
                    continue
                seen.add(key)
                kinds[m.group("sev") + ": " + re.sub(r"'[^']*'", "X", msg)[:80]] += 1
                if out:
                    out.write(f"{m.group('file')}:{m.group('line')}:{m.group('col')}: {m.group('sev')}: {msg}\n")
    print(f"units: {len(us)}, failed: {len(failed)}, diagnostics: {len(seen)}")
    for k, n in kinds.most_common(25):
        print(f"  {n:6d}  {k}")
    sys.exit(1 if seen or failed else 0)


if __name__ == "__main__":
    main()

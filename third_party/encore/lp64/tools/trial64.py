#!/usr/bin/env python3
"""A 64-bit trial compile of a 32-bit port: what LP64 breaks, measured before choosing (lp64/docs/low-image.md).

    python3 lp64/tools/trial64.py --db BUILD32/compile_commands.json [--db MORE.json]... --out DIR
        [--ptr32 INCLUDE_DIR]... [--sysroot SYSROOT] [--clang CLANG] [--only REGEX] [--exclude REGEX]
        [--path-map FROM=TO]... [--jobs N]

Replays a 32-bit build's compile commands (configure it with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON) with clang for
x86_64-unknown-linux-gnu (LP64, the 64-bit consoles' data model), writing ELF objects with DWARF under DIR/obj, and
counts the diagnostics that matter for pointer size: failed units, failed `_Static_assert`s (layout checks), other
errors, and pointer/integer cast warnings. Nothing is linked. Results: DIR/summary.txt (also printed) and
DIR/diag.txt (every unit's stderr).

--ptr32 INCLUDE_DIR (repeatable): a copy of that header folder, with every pointer field and pointer data label
marked P32 by p32_patch.py's rule, replaces it in every command, and lp64_p32.h is forced in: the trial of the
low-image model with no hand edits. Compare the two runs' summaries (spyro-vita: 22 failed units and 52 layout
asserts plain, 17 and 8 with --ptr32). It doesn't touch the SDK's 4-byte `long`s or the .c files' `T **`s (both
p32_patch.py's): a struct with a `long` after its pointers still fails its asserts here.

--sysroot SYSROOT: a folder whose include/ holds a glibc's headers (with -nostdinc and clang's own include folder).
Without it clang finds the x86-64 headers itself (Debian's libc6-dev-amd64-cross puts them where it looks).
--path-map FROM=TO rewrites paths in the commands (a build made in a container at /work, replayed elsewhere).
Units whose source is generated 32-bit output (preprocessed with -m32, layouts baked in) can't be measured as they
are: give the trial their real sources through another --db file of hand-written entries.

From spyro-vita tools/ps4/trial64.py @ c25a3ea (MIT; D173). Spyro's fixed paths (build/linux32, its overlays'
generated units and their flags) became --db, --out, --ptr32 and --path-map; its own field-only header rewrite is
p32_patch.py's (fields and data labels).
"""
import argparse, collections, json, os, re, shlex, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32_patch import rewrite  # noqa: E402

P32_H = os.path.normpath(os.path.join(HERE, "..", "runtime", "p32", "lp64_p32.h"))
WARN = ["-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wvoid-pointer-to-int-cast",
        "-Wshorten-64-to-32", "-Wint-conversion", "-Wincompatible-pointer-types"]


def clang_include(clang):
    res = subprocess.check_output([clang, "-print-resource-dir"], text=True).strip()
    return os.path.join(res, "include")


def make_ptr32_includes(src, dst):
    """Copy a header folder with every pointer field and pointer label P32."""
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    n = 0
    for dirpath, _, files in os.walk(dst):
        for f in files:
            if not f.endswith(".h"):
                continue
            p = os.path.join(dirpath, f)
            t = open(p, encoding="latin-1").read()
            new, ch = rewrite(t)
            n += len(ch)
            open(p, "w", encoding="latin-1").write(new)
    return n


def translate(entry, a, out, subs):
    args = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    if args and os.path.basename(args[0]) == "timeout":
        args = args[2:]
    cxx = args[0].endswith("++")
    clang = a.clang + ("++" if cxx and not a.clang.endswith("++") else "")
    new = [clang, "-target", "x86_64-unknown-linux-gnu", "-fms-extensions", "-ferror-limit=0", "-c", "-g", "-O0",
           "-fno-eliminate-unused-debug-types"] + WARN
    if a.sysroot:
        new += ["-nostdinc", "-isystem", clang_include(a.clang), "-isystem", os.path.join(a.sysroot, "include")]
    if a.ptr32:
        new += ["-include", P32_H]
    skip = 0
    for x in args[1:]:
        if skip:
            skip -= 1
            continue
        if x in ("-o", "-MF", "-MT"):
            skip = 1
            continue
        if x in ("-c", "-m32", "-MD", "-MMD", "-w") or x.startswith(("-O", "-march", "-mtune", "-Werror")) \
                or x == entry["file"]:
            continue
        for f, t in subs:
            x = x.replace(f, t)
        new.append(x)
    src = entry["file"]
    for f, t in subs:
        src = src.replace(f, t)
    return new + ["-o", out, src]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--db", action="append", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--ptr32", action="append", default=[], metavar="INCLUDE_DIR")
    ap.add_argument("--sysroot")
    ap.add_argument("--clang", default="clang")
    ap.add_argument("--only")
    ap.add_argument("--exclude")
    ap.add_argument("--path-map", action="append", default=[], metavar="FROM=TO")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    a = ap.parse_args(argv)
    pmap = [tuple(m.split("=", 1)) for m in a.path_map]
    out = os.path.abspath(a.out)
    os.makedirs(os.path.join(out, "obj"), exist_ok=True)
    subs = list(pmap)
    for i, inc in enumerate(a.ptr32):
        src = inc
        for f, t in pmap:
            src = src.replace(f, t)
        dst = os.path.join(out, f"p32inc{i}")
        print(f"ptr32: {make_ptr32_includes(src, dst)} pointer fields and labels marked in {inc}")
        subs.insert(0, (src, dst))
    todo = []
    for dbp in a.db:
        for e in json.load(open(dbp)):
            f = e["file"]
            if not f.endswith((".c", ".cpp", ".cc")):
                continue
            if a.only and not re.search(a.only, f) or a.exclude and re.search(a.exclude, f):
                continue
            d = e["directory"]
            for fr, t in pmap:
                d = d.replace(fr, t)
            rel = f
            for fr, t in pmap:
                rel = rel.replace(fr, t)
            o = os.path.join(out, "obj", re.sub(r"[^\w.]+", "_", os.path.relpath(rel, d)) + ".o")
            todo.append((os.path.relpath(rel, d), d, translate(e, a, o, subs)))

    def run(t):
        rel, d, cmd = t
        p = subprocess.run(cmd, cwd=d, capture_output=True, text=True)
        return rel, p.returncode, p.stderr

    res = list(ThreadPoolExecutor(a.jobs).map(run, todo))
    kinds, perfile, sites = collections.Counter(), collections.Counter(), set()
    with open(os.path.join(out, "diag.txt"), "w") as d:
        for rel, rc, err in res:
            d.write(f"### {rel} rc={rc}\n{err}\n")
            for m in re.finditer(r"^(\S+?):(\d+):\d+: (warning|error): (.*?)(?: \[(-W[^\]]+)\])?$", err, re.M):
                path, line, sev, msg, flag = m.groups()
                if sev == "error" and "static assert" in msg:
                    k = "error: static assertion"
                elif sev == "error":
                    k = "error: " + re.sub(r"'[^']*'", "'X'", msg)[:70]
                else:
                    k = "warning: " + (flag or msg[:60])
                key = (path, line, k)
                if key in sites:
                    continue
                sites.add(key)
                kinds[k] += 1
                perfile[path.replace(out + "/", "")] += 1
    failed = sorted(r for r, rc, _ in res if rc)
    with open(os.path.join(out, "summary.txt"), "w") as s:
        s.write(f"units: {len(res)}, failed to compile: {len(failed)}\n")
        for f in failed:
            s.write(f"  failed: {f}\n")
        s.write("\ndiagnostics by kind (unique file:line):\n")
        for k, n in kinds.most_common():
            s.write(f"  {n:6d}  {k}\n")
        s.write("\nfiles with most diagnostics:\n")
        for k, n in perfile.most_common(40):
            s.write(f"  {n:6d}  {k}\n")
    print(open(os.path.join(out, "summary.txt")).read(), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())

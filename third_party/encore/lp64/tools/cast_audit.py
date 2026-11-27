#!/usr/bin/env python3
"""Pointer/integer casts in the 64-bit build (the low-image model, lp64/docs/low-image.md).

    python3 lp64/tools/cast_audit.py BUILD [--clang CLANG] [--only REGEX] > cast_audit.txt

Replays BUILD's compile_commands.json (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON) with -fsyntax-only and
clang's -Wpointer-to-int-cast, -Wint-to-pointer-cast and -Wvoid-pointer-to-int-cast (every other warning and -w
dropped), and lists each site once, by file, with its source line. Run it where the build's compiler and paths are
(the host container). Under the low-image rule such a cast is exact when the pointer is the game's (the image, the
arena, the game thread's stack: below 2 GB); it is wrong only when the pointer is the host's (malloc'd, mapped, the
main thread's stack), which lies above 4 GB and crashes when truncated (lp64/runtime/low_image). Read the list for
host pointers. A `timeout N` compiler launcher is dropped; --clang replaces the compiler (default: the command's own);
--only keeps the units whose path matches.

From spyro-vita tools/ps4/cast_audit.py @ c25a3ea (MIT; D174); the build folder is an argument (spyro's default was
build/linux64).
"""
import argparse, collections, json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

WARN = ["-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wvoid-pointer-to-int-cast"]
SITE = re.compile(r"^(\S+?):(\d+):(\d+): warning: (.*?) \[(-W[\w-]+)\]$", re.M)


def command(e, clang=None):
    args = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
    if args and os.path.basename(args[0]) == "timeout":
        args = args[2:]
    out, skip = [], 0
    for a in args:
        if skip:
            skip -= 1
            continue
        if a in ("-o", "-MF", "-MT"):
            skip = 1
            continue
        if a in ("-w", "-MD", "-MMD", "-c") or a.startswith("-W") and not a.startswith("-Wno-error") \
                and not a.startswith("-Wl,") and not a.startswith("-Wa,") and not a.startswith("-Wp,"):
            continue
        out.append(a)
    if clang and out:
        out[0] = clang
    return out + ["-fsyntax-only", "-Wno-everything"] + WARN


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("build")
    ap.add_argument("--clang")
    ap.add_argument("--only")
    a = ap.parse_args(argv)
    db = json.load(open(os.path.join(a.build, "compile_commands.json")))
    db = [e for e in db if e["file"].endswith((".c", ".cpp", ".C")) and (not a.only or re.search(a.only, e["file"]))]

    def run(e):
        p = subprocess.run(command(e, a.clang), cwd=e["directory"], capture_output=True, text=True)
        return e["directory"], p.stderr

    sites = {}
    with ThreadPoolExecutor(os.cpu_count()) as ex:
        for d, err in ex.map(run, db):
            for m in SITE.finditer(err):
                path, line, col, msg, flag = m.groups()
                path = os.path.normpath(os.path.join(d, path))
                path = re.sub(r"^.*/patched/", "patched/", path).replace(os.getcwd() + "/", "")
                sites.setdefault((path, int(line)), (flag, msg))
    byfile = collections.defaultdict(list)
    for (path, line), (flag, msg) in sorted(sites.items()):
        byfile[path].append((line, flag, msg))
    kinds = collections.Counter(f for f, _ in sites.values())
    print(f"cast_audit: {len(sites)} sites in {len(byfile)} files: "
          + ", ".join(f"{k} {n}" for k, n in kinds.most_common()))
    for path in sorted(byfile, key=lambda p: (-len(byfile[p]), p)):
        print(f"\n## {path} ({len(byfile[path])})")
        src = None
        for cand in (path, os.path.join(a.build, path)):
            if os.path.exists(cand):
                src = open(cand, errors="replace").read().split("\n")
                break
        for line, flag, msg in byfile[path]:
            text = src[line - 1].strip() if src and line <= len(src) else ""
            print(f"  {line}: {flag[2:]}: {text[:150]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

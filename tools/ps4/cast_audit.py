#!/usr/bin/env python3
"""Pointer/integer casts in the linux64 build (the low-image model, D174).

    tools/tc python3 tools/ps4/cast_audit.py [BUILD] > build/linux64/cast_audit.txt

Replays BUILD's compile_commands.json (default build/linux64, configured with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON) with -fsyntax-only and clang's
-Wpointer-to-int-cast, -Wint-to-pointer-cast and -Wvoid-pointer-to-int-cast
(the overlays' -w dropped), and lists each site once, by file, with its
source line. Under the low-image rule such a cast is exact when the pointer
is one of the game's (the image, the arena, the game thread's stack: below
2 GB); it is wrong only when the pointer is the host's (malloc'd, mapped,
the main thread's stack), which on linux64 lies above 4 GB and crashes when
truncated (src/platform/linux32/low_image.c).
"""
import collections, json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

WARN = ["-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wvoid-pointer-to-int-cast"]
SITE = re.compile(r"^(\S+?):(\d+):(\d+): warning: (.*?) \[(-W[\w-]+)\]$", re.M)


def run(e):
    args = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
    out, skip = [], 0
    for a in args:
        if skip:
            skip -= 1
            continue
        if a in ("-o", "-MF", "-MT"):
            skip = 1
            continue
        if a in ("-w", "-MD", "-c") or a.startswith("-W") and not a.startswith("-Wno-error"):
            continue
        out.append(a)
    if out and out[0] == "timeout":
        out = out[2:]
    out += ["-fsyntax-only", "-Wno-everything"] + WARN
    p = subprocess.run(out, cwd=e["directory"], capture_output=True, text=True)
    return p.stderr


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build/linux64"
    db = json.load(open(os.path.join(build, "compile_commands.json")))
    db = [e for e in db if e["file"].endswith((".c", ".cpp", ".C"))]
    sites = {}
    with ThreadPoolExecutor(os.cpu_count()) as ex:
        for err in ex.map(run, db):
            for m in SITE.finditer(err):
                path, line, col, msg, flag = m.groups()
                path = os.path.normpath(path)
                path = re.sub(r"^.*/patched/", "patched/", path).replace(os.getcwd() + "/", "")
                sites.setdefault((path, int(line)), (flag, msg))
    byfile = collections.defaultdict(list)
    for (path, line), (flag, msg) in sorted(sites.items()):
        byfile[path].append((line, flag, msg))
    kinds = collections.Counter(f for f, _ in sites.values())
    print(f"cast_audit: {len(sites)} sites in {len(byfile)} files: "
          + ", ".join(f"{k} {n}" for k, n in kinds.most_common()))
    for path in sorted(byfile, key=lambda p: -len(byfile[p])):
        print(f"\n## {path} ({len(byfile[path])})")
        src = None
        for cand in (path, os.path.join(build, path)):
            if os.path.exists(cand):
                src = open(cand, errors="replace").read().split("\n")
                break
        for line, flag, msg in byfile[path]:
            text = src[line - 1].strip() if src and line <= len(src) else ""
            print(f"  {line}: {flag[2:]}: {text[:150]}")


if __name__ == "__main__":
    main()

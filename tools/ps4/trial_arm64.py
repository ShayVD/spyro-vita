#!/usr/bin/env python3
"""ARM64 trial compile (D250): the linux64 build's units, with the Mac's own
clang for arm64-apple-macos, syntax only. Shows what an Apple Silicon build
meets before any change: P32 is empty there (src/compat/p32.h; `__ptr32`
does nothing on AArch64), so every pointer field is 8 bytes and the port's
layout asserts fail.

    python3 tools/ps4/trial_arm64.py build/linux64 [--out FILE] [--jobs N]

Run on a Mac (xcrun gives the SDK), from the repo root; BUILD is a linux64
tree configured with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON (its /work paths are
mapped to this checkout). The overlays' gen/ sources are skipped: they are
linux64's preprocessed text. Prints failed units and diagnostics by kind,
each counted once per file:line.
"""
import argparse, collections, json, os, re, shlex, subprocess
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WARN = ["-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wshorten-64-to-32"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--out", help="every unit's diagnostics")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    a = ap.parse_args()
    sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
    todo = []
    for e in json.load(open(os.path.join(a.build, "compile_commands.json"))):
        f = e["file"]
        if "/gen/ovl" in f or "/tests/" in f or "/tools/" in f or not f.endswith((".c", ".cpp")):
            continue
        args = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
        cmd = ["clang++" if args[0].endswith("++") else "clang", "-target", "arm64-apple-macos13",
               "-isysroot", sdk, "-fsyntax-only", "-ferror-limit=0"] + WARN
        skip = 0
        for x in args[1:]:
            if skip:
                skip -= 1
                continue
            if x in ("-o", "-MF", "-MT"):
                skip = 1
                continue
            if x == "-c" or x == f or x.startswith("--target") or x.startswith("-g") \
                    or x in ("-fno-pic", "-fms-extensions"):
                continue
            cmd.append(x.replace("/work", ROOT))
        cmd.append(f.replace("/work", ROOT))
        todo.append((f.replace("/work/", ""), cmd, e["directory"].replace("/work", ROOT)))

    def run(t):
        rel, cmd, d = t
        p = subprocess.run(cmd, cwd=d, capture_output=True, text=True)
        return rel, p.returncode, p.stderr

    res = list(ThreadPoolExecutor(a.jobs).map(run, todo))
    kinds, seen, failed = collections.Counter(), set(), []
    out = open(a.out, "w") if a.out else None
    for rel, rc, err in res:
        if out:
            out.write(f"### {rel} rc={rc}\n{err}\n")
        if rc:
            failed.append(rel)
        for m in re.finditer(r"^(\S+?):(\d+):\d+: (warning|error): (.*?)(?: \[(-W[^\]]+)\])?$", err, re.M):
            path, line, sev, msg, flag = m.groups()
            if sev == "error":
                k = "error: static assertion" if "static assert" in msg else \
                    "error: " + re.sub(r"'[^']*'", "X", msg)[:70]
            else:
                k = "warning: " + (flag or msg[:50])
            if (path, line, k) not in seen:
                seen.add((path, line, k))
                kinds[k] += 1
    print(f"units: {len(res)}, failed: {len(set(failed))}")
    for k, n in kinds.most_common():
        print(f"  {n:6d}  {k}")
    for f in sorted(set(failed)):
        print("  failed:", f)


if __name__ == "__main__":
    main()

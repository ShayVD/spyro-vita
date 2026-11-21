#!/usr/bin/env python3
"""64-bit trial compile of the port's game code (the PS4 question, D173).

Replays the linux32 build's compile commands (build/linux32/compile_commands.json,
configured with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON) with clang for
x86_64-unknown-linux-gnu (LP64, the PS4's data model), writes ELF objects with
DWARF under build/trial64/, and counts the diagnostics that matter for
pointer size.

    python3 tools/ps4/trial64.py --sysroot DIR [--only REGEX] [--jobs N]

DIR holds a glibc `include/` (the container's /usr/i686-linux-gnu/include, plus an
empty gnu/stubs-64.h): only the declarations are used, nothing is linked.
Output: build/trial64/summary.txt and build/trial64/diag.txt.
"""
import argparse, collections, json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WARN = ["-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wvoid-pointer-to-int-cast",
        "-Wshorten-64-to-32", "-Wint-conversion", "-Wincompatible-pointer-types"]


def translate(entry, sysroot, out):
    args = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    src = entry["file"].replace("/work", ROOT, 1)
    cxx = args[0].endswith("++")
    new = ["clang++" if cxx else "clang", "-target", "x86_64-unknown-linux-gnu",
           "-nostdinc", "-isystem", clang_include(), "-isystem", os.path.join(sysroot, "include"),
           "-fms-extensions", "-ferror-limit=0", "-c", "-g", "-O0",
           "-fno-eliminate-unused-debug-types"] + WARN
    skip = 0
    for i, a in enumerate(args[1:], 1):
        if skip:
            skip -= 1
            continue
        if a in ("-o", "-MF", "-MT", "-c"):
            skip = 1 if a != "-c" else 0
            continue
        if a in ("-m32", "-MD", "-MMD", "-w") or a.startswith("-O") or a.startswith("-march") \
                or a.startswith("-mtune") or a == entry["file"] or a.startswith("-Werror"):
            continue
        a = a.replace("/work", ROOT)
        if INC_SUB:
            a = a.replace(INC_SUB[0], INC_SUB[1])
        new.append(a)
    new += ["-o", out, src]
    return new


INC_SUB = None
# A struct field declared as a pointer: `T *name;`, `T **name;`, `T *name[N];`.
FIELD = re.compile(r"^(\s+(?:const\s+|volatile\s+|struct\s+|union\s+|unsigned\s+|signed\s+)*"
                   r"[A-Za-z_]\w*\s*)(\*+)(\s*[A-Za-z_]\w*\s*(?:\[[^\]]*\]\s*)*;)", re.M)


def make_ptr32_includes(src, dst):
    """Copy the decomp's headers with every pointer field 32 bits wide
    (clang's `__ptr32 __uptr`, zero-extended): the low-memory trial."""
    import shutil
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    n = 0
    for dirpath, _, files in os.walk(dst):
        for f in files:
            if not f.endswith(".h"):
                continue
            p = os.path.join(dirpath, f)
            t = open(p).read()
            # only inside struct/union bodies: crude, a brace depth > 0 test
            out, depth, pos = [], 0, 0
            for m in FIELD.finditer(t):
                seg = t[pos:m.start()]
                depth += seg.count("{") - seg.count("}")
                out.append(seg)
                if depth > 0:
                    stars = m.group(2)
                    out.append(m.group(1) + stars[:-1] + "* __ptr32 __uptr " + m.group(3).lstrip())
                    n += 1
                else:
                    out.append(m.group(0))
                pos = m.end()
            out.append(t[pos:])
            open(p, "w").write("".join(out))
    return n


_ci = None


def clang_include():
    global _ci
    if _ci is None:
        res = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
        _ci = os.path.join(res, "include")
    return _ci


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sysroot", required=True)
    ap.add_argument("--only", default=r"src/(game|platform/common)/|/gen/|third_party/spyro-1|patched")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--ptr32", action="store_true",
                    help="pointer fields in the decomp's structs as __ptr32 (build/trial64-ptr32)")
    a = ap.parse_args()
    global INC_SUB
    db = json.load(open(os.path.join(ROOT, "build/linux32/compile_commands.json")))
    outdir = os.path.join(ROOT, "build/trial64" + ("-ptr32" if a.ptr32 else ""))
    if a.ptr32:
        inc = os.path.join(ROOT, "build/linux32/patched/spyro-1/include")
        INC_SUB = (inc, os.path.join(outdir, "include"))
        print("ptr32 fields:", make_ptr32_includes(inc, INC_SUB[1]))
    os.makedirs(outdir, exist_ok=True)
    todo = []
    # The overlays' gen/ sources are `gcc -m32 -E` output (cmake/overlays.cmake),
    # 32-bit layouts baked in: compile each overlay source once from the
    # decomp instead, with the overlay flags, every class included.
    s1 = os.path.join(ROOT, "build/linux32/patched/spyro-1")
    pc = os.path.join(ROOT, "build/linux32/patched/PsyCross")
    seen = set()
    for e in db:
        m = re.search(r"/gen/ovl/(\w+)/(\w+)\.c$", e["file"])
        if not m or m.group(2) in seen:
            continue
        ovl, stem = m.groups()
        seen.add(stem)
        src = f"/work/build/linux32/patched/spyro-1/src/overlays/{stem}.c"
        if not os.path.exists(src.replace("/work", ROOT)):
            continue
        cmd = ("/usr/bin/i686-linux-gnu-gcc -std=gnu11 -funsigned-char -fno-strict-aliasing "
               "-DMODERN_COMPILER -DNDEBUG -DNNOCD -DSPYRO_PORT -DSPYRO_NATIVE_MEM=1 "
               "-DUSE_EXTENDED_PRIM_POINTERS=0 -DUSE_PGXP=0 "
               f"-I/work/src/compat -I/work/build/linux32/patched/spyro-1/include "
               f"-I/work/build/linux32/patched/PsyCross/include "
               f"-I/work/build/linux32/patched/PsyCross/include/psx "
               f"-include /work/src/compat/port_prefix.h -DOVERLAY_NAME={ovl} -DIS_{ovl} -c {src}")
        out = os.path.join(outdir, "obj", "ovl_" + stem + ".o")
        todo.append((f"overlays/{stem}.c ({ovl})", translate({"file": src, "command": cmd}, a.sysroot, out)))
    for e in db:
        f = e["file"]
        if "/gen/ovl" in f:
            continue
        if not re.search(a.only, f) or "/tests/" in f or "/tools/" in f:
            continue
        if not f.endswith((".c", ".cpp")):
            continue
        rel = f.replace("/work/", "")
        out = os.path.join(outdir, "obj", rel.replace("/", "_") + ".o")
        todo.append((rel, translate(e, a.sysroot, out)))
    os.makedirs(os.path.join(outdir, "obj"), exist_ok=True)

    def run(t):
        rel, cmd = t
        p = subprocess.run(cmd, cwd=os.path.join(ROOT, "build/linux32"), capture_output=True, text=True)
        return rel, p.returncode, p.stderr

    res = list(ThreadPoolExecutor(a.jobs).map(run, todo))
    kinds = collections.Counter()
    perfile = collections.Counter()
    sites = set()
    with open(os.path.join(outdir, "diag.txt"), "w") as d:
        for rel, rc, err in res:
            d.write(f"### {rel} rc={rc}\n{err}\n")
            for m in re.finditer(r"^(\S+?):(\d+):\d+: (warning|error): (.*?)(?: \[(-W[^\]]+)\])?$", err, re.M):
                path, line, sev, msg, flag = m.groups()
                if sev == "error" and "static assertion" in msg:
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
                perfile[path.replace(ROOT + "/", "").replace("../../", "")] += 1
    failed = [r for r, rc, _ in res if rc]
    with open(os.path.join(outdir, "summary.txt"), "w") as s:
        s.write(f"units: {len(res)}, failed to compile: {len(failed)}\n")
        for f in failed:
            s.write(f"  failed: {f}\n")
        s.write("\ndiagnostics by kind (unique file:line):\n")
        for k, n in kinds.most_common():
            s.write(f"  {n:6d}  {k}\n")
        s.write("\nfiles with most diagnostics:\n")
        for k, n in perfile.most_common(40):
            s.write(f"  {n:6d}  {k}\n")
    print(open(os.path.join(outdir, "summary.txt")).read())


if __name__ == "__main__":
    main()

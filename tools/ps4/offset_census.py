#!/usr/bin/env python3
"""Count what the offset model would rewrite (D250): every source site where
a P32 pointer is read, written or decoded, from clang's AST of the linux64
build, where P32 is `__ptr32 __uptr` and so visible in every type.

    tools/tc python3 tools/ps4/offset_census.py build/linux64 [--jobs N] [--sites FILE]

BUILD is a linux64 tree configured with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON and
built (the generated sources must exist). Each compile command is replayed
with -fsyntax-only -Xclang -ast-dump; a site is counted once by its spelling
location (file:line:col), however many units include it. Kinds:

  load      a P32 lvalue read (LValueToRValue of a `__ptr32` type): the offset
            model decodes it (base + offset)
  store     `=` into a P32 lvalue: the model encodes the value
            (pointer - base)
  arith     `+=`, `-=`, `++`, `--` on a P32 lvalue: decode, step, encode
  init      a P32 variable with an initialiser
  as_cast   any other conversion into or out of a P32 type
            (AddressSpaceConversion): already counted by load/store mostly,
            reported apart
  p2i, i2p  pointer <-> integer casts ((int)p, (Moby *)i): in the offset
            model an address kept in an integer is an offset too, so each is
            an encode or a decode

Sites are grouped by origin: decomp (the patched spyro-1 tree), hand (src/),
gen (the build's generated files), other.
"""
import argparse, collections, json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

LOC = re.compile(r"(?<![\w/.])(?:(?P<file><scratch space>|<built-in>|[^\s:<>,']+\.(?:c|h|cc|cpp|hpp|inc)):(?P<fl>\d+):(?P<fc>\d+)|line:(?P<l>\d+):(?P<lc>\d+)|col:(?P<c>\d+))")
NODE = re.compile(r"^(?P<ind>[ |`-]*)(?P<kind>[A-Za-z]+) 0x[0-9a-f]+")


def units(build):
    with open(os.path.join(build, "compile_commands.json")) as f:
        cc = json.load(f)
    seen, out = set(), []
    for e in cc:
        args = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
        if not e["file"].endswith((".c", ".cpp", ".cc")):
            continue
        key = (e["file"], tuple(a for a in args if a.startswith("-D")))
        if key in seen:
            continue
        seen.add(key)
        new, skip = [], 0
        for a in args:
            if skip:
                skip -= 1
                continue
            if a in ("-o", "-MF", "-MT", "-MQ"):
                skip = 1
                continue
            if a in ("-c", "-MD", "-MMD") or a.startswith("-g"):
                continue
            new.append(a)
        if new[0] in ("timeout",):
            new = new[2:]
        new += ["-fsyntax-only", "-Xclang", "-ast-dump", "-fno-color-diagnostics", "-w"]
        out.append((e["directory"], e["file"], new))
    return out


def scan(unit):
    d, src, cmd = unit
    p = subprocess.run(cmd, cwd=d, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors="replace")
    sites = []
    cur_file, cur_line = None, 0
    stack = []  # (indent, kind, type, loc)
    for line in p.stdout.splitlines():
        m = NODE.match(line)
        if not m:
            continue
        ind = len(m.group("ind"))
        kind = m.group("kind")
        # clang prints each location relative to the last one printed, the
        # range ends included: walk them all; the node's is the first
        loc = None
        # (not inside a quoted type: `(unnamed at moby.h:175:9)` is the type
        # printer's, and doesn't move clang's last location)
        for lm in LOC.finditer(re.sub(r"'[^']*'", "''", line)):
            if lm.group("file"):
                cur_file, cur_line = lm.group("file"), int(lm.group("fl"))
                here = (cur_file, cur_line, int(lm.group("fc")))
            elif lm.group("l"):
                cur_line = int(lm.group("l"))
                here = (cur_file, cur_line, int(lm.group("lc")))
            else:
                here = (cur_file, cur_line, int(lm.group("c")))
            if loc is None:
                loc = here
        # the node's own type: the first quoted string after the range
        tm = re.search(r"> '([^']*)'", line)
        ty = tm.group(1) if tm else ""
        while stack and stack[-1][0] >= ind:
            stack.pop()
        parent = stack[-1] if stack else None
        stack.append((ind, kind, ty, loc, line))
        if loc is None or loc[0] is None:
            continue
        p32 = "__ptr32" in ty
        k = None
        if kind == "ImplicitCastExpr":
            if "<LValueToRValue>" in line and p32:
                k = "load"
            elif "<AddressSpaceConversion>" in line:
                k = "as_cast"
            elif "<PointerToIntegral>" in line:
                k = "p2i"
            elif "<IntegralToPointer>" in line:
                k = "i2p"
        elif kind == "CStyleCastExpr":
            if "<PointerToIntegral>" in line:
                k = "p2i"
            elif "<IntegralToPointer>" in line:
                k = "i2p"
            elif "<AddressSpaceConversion>" in line:
                k = "as_cast"
        elif kind in ("BinaryOperator", "CompoundAssignOperator") and p32 and \
                re.search(r"'(=|\+=|-=)'", line.split("'" + ty + "'", 1)[-1]):
            k = "store" if kind == "BinaryOperator" else "arith"
        elif kind == "UnaryOperator" and p32 and re.search(r"'(\+\+|--)'", line):
            k = "arith"
        elif kind == "VarDecl" and p32 and line.rstrip().endswith(("cinit", "callinit")):
            k = "init"
        if k:
            sites.append((k, loc))
    return src, sites


def origin(path, build):
    if "/patched/spyro-1/" in path:
        return "decomp"
    if "/src/" in path and "/build" not in path:
        return "hand"
    if path.startswith(build) or "/gen/" in path:
        return "gen"
    return "other"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--sites")
    a = ap.parse_args()
    build = os.path.abspath(a.build)
    us = units(build)
    allsites = {}
    with ThreadPoolExecutor(a.jobs) as ex:
        for src, sites in ex.map(scan, us):
            for k, loc in sites:
                allsites[(k,) + loc] = 1
    tab = collections.Counter()
    files = collections.defaultdict(collections.Counter)
    for k, f, l, c in allsites:
        o = origin(f, build)
        tab[(o, k)] += 1
        files[f][k] += 1
    kinds = ["load", "store", "arith", "init", "as_cast", "p2i", "i2p"]
    print(f"{len(us)} units")
    print("%-8s" % "origin" + "".join("%9s" % k for k in kinds))
    for o in ["decomp", "hand", "gen", "other"]:
        print("%-8s" % o + "".join("%9d" % tab[(o, k)] for k in kinds))
    print("%-8s" % "total" + "".join("%9d" % sum(tab[(o, k)] for o in ["decomp", "hand", "gen", "other"]) for k in kinds))
    if a.sites:
        with open(a.sites, "w") as f:
            for k, fn, l, c in sorted(allsites, key=lambda s: (s[1], s[2], s[3], s[0])):
                f.write(f"{fn}:{l}:{c}\t{k}\n")
        with open(a.sites + ".files", "w") as f:
            for fn in sorted(files, key=lambda x: -sum(files[x].values())):
                f.write(f"{sum(files[fn].values()):6d} {fn} " + " ".join(f"{k}={files[fn][k]}" for k in kinds) + "\n")


if __name__ == "__main__":
    main()

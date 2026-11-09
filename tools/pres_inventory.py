#!/usr/bin/env python3
"""The presenter's call graph, from the game's objects (stage 7e, D125).

Reads `objdump -dr` of the game's objects (the decomp's C, our src/game,
the platform's common code) and lists every function reachable from
GamestateDraw's play branch, with the globals each one names:

  python3 tools/pres_inventory.py BUILD_DIR [--root FN ...] [--stop FN ...]

For each function: its object, the functions it calls, the data symbols it
names (a TU's statics as OBJECT:SECTION). A function that names no
writable data is "shared": the presenter may call the game's own copy. The
rest need the presenter build (src/game/pres/). Run inside tools/tc (it
needs the container's objdump for i386 objects).

--roots defaults to GamestateDraw; --stop names functions not followed:
the renderers (their presenter builds are 7d's), the other gamestates'
draws (7g's), and the output calls (libgpu's DrawOTag and kin)."""
import argparse
import collections
import glob
import os
import re
import subprocess
import sys

# Not followed: the renderers' entries (7d's NAME_pres), GamestateDraw's
# other branches (7g), libgpu's output and libetc's VSync (the presenter's
# stubs or output, pres_names.h).
RENDERERS = """func_800258F0 func_8001F798 func_80022A2C func_80020F34 func_8004EBA8
func_80023AC4 func_80059A48 func_80059F8C func_80058D64 func_800573C8
func_80058BA8 func_80058864""".split()
OTHER_GAMESTATES = """func_8001A050 func_8001A40C func_8001CA38 func_8001CFDC
func_8001C694 func_8001D718 func_8001E24C func_8001E6B8 TitlescreenDraw
func_8001E9C8 func_credits_8007BFD0 func_8001EB80""".split()
OUTPUT = """DrawOTag PutDrawEnv PutDispEnv DrawSync VSync LoadImage StoreImage
MoveImage ClearImage""".split()

FN_RE = re.compile(r"^([0-9a-f]+) <(.+)>:$")
REL_RE = re.compile(r"^\s+[0-9a-f]+: (R_386_\w+)\s+(\S+)")
CALL_RE = re.compile(r"\bcall\s+[0-9a-f]+ <([^>+]+)>")


def objdump(path):
    out = subprocess.run([os.environ.get("OBJDUMP", "i686-linux-gnu-objdump"), "-dr", "--no-show-raw-insn", path],
                         capture_output=True, text=True, check=True).stdout
    funcs = {}
    cur = None
    for line in out.splitlines():
        m = FN_RE.match(line)
        if m:
            cur = m.group(2)
            funcs.setdefault(cur, {"calls": set(), "data": set(), "rels": []})
            continue
        if cur is None:
            continue
        m = REL_RE.match(line)
        if m:
            funcs[cur]["rels"].append((m.group(1), m.group(2)))
            continue
        m = CALL_RE.search(line)
        if m and "R_386" not in line:
            funcs[cur]["calls"].add(m.group(1))
    return funcs


def symbols(path):
    """defined symbols: name -> nm type; undefined set"""
    out = subprocess.run([os.environ.get("NM", "i686-linux-gnu-nm"), path], capture_output=True, text=True, check=True).stdout
    d, u = {}, set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "U":
            u.add(parts[1])
        elif len(parts) == 3:
            d[parts[2]] = parts[1]
    return d, u


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--root", action="append", default=None)
    ap.add_argument("--stop", action="append", default=[])
    ap.add_argument("--tsv", help="write the inventory as TSV here")
    a = ap.parse_args()
    roots = a.root or ["GamestateDraw"]
    objs = []
    for pat in ("CMakeFiles/spyro_game_main.dir/**/*.o", "CMakeFiles/spyro_game_port.dir/**/*.o",
                "CMakeFiles/spyro_platform_common.dir/**/*.o"):
        objs += glob.glob(os.path.join(a.build, pat), recursive=True)
    if not objs:
        sys.exit("no objects under %s" % a.build)
    where = {}      # function -> object
    body = {}       # function -> calls/data
    symtype = {}    # global symbol -> nm type (first definition)
    for o in sorted(objs):
        short = os.path.relpath(o, a.build)
        d, _ = symbols(o)
        for s, t in d.items():
            if t.isupper():
                symtype.setdefault(s, t)
        for fn, b in objdump(o).items():
            key = fn if d.get(fn, "T").isupper() else short + ":" + fn
            # a TU's static function: calls to it from its own TU name it bare
            where.setdefault(key, short)
            body[key] = (short, b)
    stop = set(RENDERERS) | set(OTHER_GAMESTATES) | set(OUTPUT) | set(a.stop)

    def resolve(name, obj):
        local = obj + ":" + name
        return local if local in body else name

    seen, order = set(), []
    work = collections.deque(roots)
    while work:
        fn = work.popleft()
        if fn in seen:
            continue
        seen.add(fn)
        order.append(fn)
        if fn in stop or fn not in body:
            continue
        obj, b = body[fn]
        for c in sorted(b["calls"]):
            work.append(resolve(c, obj))
        for kind, s in b["rels"]:
            if kind == "R_386_PC32" and (symtype.get(s) == "T" or s not in symtype):
                work.append(resolve(s, obj))
    rows = []
    for fn in order:
        if fn not in body:
            rows.append((fn, "-", "extern" if fn not in stop else "stop", "", ""))
            continue
        obj, b = body[fn]
        data = set()
        calls = set()
        for kind, s in b["rels"]:
            t = symtype.get(s)
            if kind == "R_386_PC32" and (t == "T" or t is None and not s.startswith(".")):
                calls.add(s)
            elif s.startswith("."):
                data.add(obj + ":" + s)
            else:
                data.add(s)
        calls |= b["calls"]
        writable = sorted(x for x in data if not (x.endswith(".rodata") or ".rodata." in x
                                                   or symtype.get(x) == "R"))
        kind = "stop" if fn in stop else ("shared" if not writable else "pres")
        rows.append((fn, obj, kind, " ".join(sorted(calls)), " ".join(writable)))
    for r in rows:
        print("%-28s %-6s %-60s calls[%s] data[%s]" % (r[0], r[2], r[1], r[3], r[4]))
    if a.tsv:
        with open(a.tsv, "w") as f:
            f.write("function\tkind\tobject\tcalls\tdata\n")
            for r in rows:
                f.write("\t".join((r[0], r[2], r[1], r[3], r[4])) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""The offset model's integer narrowings (D250 O4, D265): every place a
64-bit integer becomes a 32-bit one, in the units of a build of the offset
model (linuxa64 or macos, configured with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON).

    p32_narrow_check.py BUILD [--all] [--widen | --raw]

O3's cast pass (p32_offset_rewrite.py --casts) found every cast between a
pointer and a 32-bit integer. A host address can also reach a 32-bit word
through an integer: `uintptr_t at = ...; return (u32)at;`. In the offset
model that keeps the address's low bits instead of its offset; the 32-bit
targets lose nothing, and the low image's addresses are their own offsets,
so only an offset-model run shows it (the presenter's sector copies, D265).

Each such cast or conversion is listed with its line, but those whose
operand is a 32-bit value widened (`(u32)(uintptr_t)P32_ENC(p)`) and those
in the build's own generated or third-party files. A listed site is either
right (an integer, a size, a difference of two addresses) or a missed
P32_ENC: read each one. --all lists the decomp's units too.

--widen lists the other way: a 32-bit value cast to a 64-bit integer that
is then used as an address. --raw (#281) lists a 32-bit word read through a
pointer cast (`*(u32 *)p`, `*(int *)&slot`) and widened to a 64-bit integer,
explicitly or not: an initialiser, an assignment, a call's uintptr_t
argument. If the word is a P32 slot, the offset model's value is its offset
from spyro_p32_base, not an address, and the read wants P32_DEC (render_hand.h
HAND_CX handed the vertex cache D_800757B0's offset as the renderer's range).
"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

WIDE = {"unsigned long", "long"}  # uintptr_t, intptr_t, size_t on LP64 (uint64_t is long long on macOS)
NARROW = {"unsigned int", "int", "unsigned short", "short", "unsigned char", "char", "signed char"}
WIDEN = [False]
RAW = [False]
LEAVES = ("DeclRefExpr", "MemberExpr", "CallExpr", "ArraySubscriptExpr")


def desugar(t):
    return (t or {}).get("desugaredQualType") or (t or {}).get("qualType", "")


def strip(n):
    """through parentheses and the value casts that keep it (an lvalue's load)"""
    while n.get("kind") == "ParenExpr" or (n.get("kind") == "ImplicitCastExpr" and
                                          n.get("castKind") in ("LValueToRValue", "NoOp")):
        n = (n.get("inner") or [{}])[0]
    return n


def raw_read(n):
    """a word read through a cast pointer: `*(u32 *)p`, `((int *)p)[i]`"""
    n = strip(n)
    k = n.get("kind")
    if k == "UnaryOperator" and n.get("opcode") == "*":
        base = strip((n.get("inner") or [{}])[0])
    elif k == "ArraySubscriptExpr":
        base = strip((n.get("inner") or [{}])[0])
        if base.get("kind") == "ImplicitCastExpr":  # an array's decay
            return False
    else:
        return False
    # (the word's own type, 32 bits, the caller tests: `u32 *` has no desugared name)
    return base.get("kind") == "CStyleCastExpr" and desugar(base.get("type")).endswith("*")


def addr_like(n):
    """the expression's value can be a host address: a 64-bit variable, field,
    call or array element, or a pointer cast to an integer, reached through
    arithmetic (not through sizeof, a pointer difference or a 32-bit value)"""
    k = n.get("kind")
    t = desugar(n.get("type"))
    if k in ("ParenExpr", "ImplicitCastExpr", "CStyleCastExpr"):
        if n.get("castKind") == "PointerToIntegral":
            return True
        if t not in WIDE:
            return False
        return any(addr_like(c) for c in n.get("inner") or ())
    if k in LEAVES:
        return t in WIDE
    if k == "BinaryOperator":
        kids = n.get("inner") or ()
        if n.get("opcode") == "-" and all("*" in desugar(c.get("type")) for c in kids):
            return False  # a difference of two pointers
        if n.get("opcode") == "-" and len(kids) == 2 and all(addr_like(c) for c in kids):
            return False  # of two addresses (an offset in the arena, MEM_ARENA_U32_)
        return t in WIDE and any(addr_like(c) for c in kids)
    if k in ("ConditionalOperator", "UnaryOperator", "CompoundAssignOperator"):
        return t in WIDE and any(addr_like(c) for c in n.get("inner") or ())
    return False


class Locs:
    """clang's JSON omits a location's file and line when they are the last
    printed's: replay them in print order (loc, then range's begin and end;
    each as spelling, then expansion)"""

    def __init__(self):
        self.file, self.line = "", 0

    def bare(self, b):
        if "file" in b:
            self.file = b["file"]
        if "line" in b:
            self.line = b["line"]
        return self.file, self.line

    def loc(self, l):
        if not l:
            return None
        if "spellingLoc" in l:
            sp = self.bare(l["spellingLoc"])
            return sp, self.bare(l["expansionLoc"])
        b = self.bare(l)
        return b, b


ADDR_TYPES = ("uintptr_t", "intptr_t", "const uintptr_t", "const intptr_t")


def address_use(parent, node):
    """the widened value goes where an address goes: a pointer cast, a
    uintptr_t variable or assignment, arithmetic or a comparison with an
    address"""
    if not parent:
        return False
    k = parent.get("kind")
    if k in ("CStyleCastExpr", "ImplicitCastExpr") and parent.get("castKind") == "IntegralToPointer":
        return True
    if k == "VarDecl" and (parent.get("type") or {}).get("qualType") in ADDR_TYPES:
        return True
    if k in ("BinaryOperator", "CompoundAssignOperator"):
        kids = parent.get("inner") or ()
        if parent.get("opcode") == "=" and kids and (kids[0].get("type") or {}).get("qualType") in ADDR_TYPES:
            return True
        return any(c is not node and addr_like(c) for c in kids)
    return False


def scan(node, root, out, locs, inside=False, parent=None):
    locs.loc(node.get("loc"))
    rng = node.get("range") or {}
    begin = locs.loc(rng.get("begin"))
    locs.loc(rng.get("end"))
    # inside an accessor of p32.h (P32_DEC's __builtin_choose_expr, P32_ENC's
    # statement expression): what it widens it encodes or decodes
    if begin and node.get("kind") in ("ChooseExpr", "StmtExpr") and begin[0][0].endswith("/src/compat/p32.h"):
        inside = True
    if node.get("kind") in ("CStyleCastExpr", "ImplicitCastExpr") and node.get("castKind") == "IntegralCast" \
            and begin:
        inner = (node.get("inner") or [{}])[0]
        (sfile, _), (efile, eline) = begin
        narrowing = desugar(inner.get("type")) in WIDE and desugar(node.get("type")) in NARROW and \
            addr_like(inner)
        # --widen: a 32-bit value made a 64-bit one, which code may use as a host address
        widening = WIDEN[0] and node["kind"] == "CStyleCastExpr" and \
            desugar(inner.get("type")) in ("unsigned int", "int") and desugar(node.get("type")) in WIDE and \
            address_use(parent, node)
        # --raw: a word read through a cast pointer made a 64-bit integer (#281)
        raw = RAW[0] and not inside and desugar(inner.get("type")) in ("unsigned int", "int") and \
            desugar(node.get("type")) in WIDE and raw_read(inner)
        hit = raw if RAW[0] else (widening and not inside if WIDEN[0] else narrowing)
        # (a raw read in a macro by the macro's line: render_hand.h's, once)
        if raw and sfile.startswith(root + "/src/"):
            efile, eline = begin[0]
        if hit \
                and not sfile.endswith("/src/compat/p32.h") and efile.startswith(root) \
                and "/build/" not in efile and "/third_party/" not in efile:
            out.add((os.path.relpath(efile, root), eline, node["kind"][:5], desugar(inner.get("type")),
                     desugar(node.get("type"))))
    # a parenthesised widening counts as its parentheses' use
    up = parent if node.get("kind") == "ParenExpr" else node
    for c in node.get("inner") or ():
        scan(c, root, out, locs, inside, up)


def unit(entry, root):
    args = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    keep, skip = [], False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-MD", "-MMD") or a == entry["file"]:
            continue
        keep.append(a)
    cmd = [args[0]] + keep + ["-fsyntax-only", "-Wno-everything", "-Xclang", "-ast-dump=json", entry["file"]]
    r = subprocess.run(cmd, cwd=entry["directory"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if r.returncode:
        return entry["file"], None
    out = set()
    scan(json.loads(r.stdout), root, out, Locs())
    return entry["file"], out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--all", action="store_true", help="the decomp's units too")
    ap.add_argument("--widen", action="store_true",
                    help="list the casts of a 32-bit value to a 64-bit integer instead")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("--raw", action="store_true",
                    help="list the 32-bit words read through a cast pointer and widened to 64 bits instead")
    a = ap.parse_args()
    WIDEN[0] = a.widen
    RAW[0] = a.raw
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    cc = json.load(open(os.path.join(a.build, "compile_commands.json")))
    cc = [e for e in cc if e["file"].endswith(".c") and (a.all or e["file"].startswith(root + "/src/"))]
    sites, failed = set(), []
    with ThreadPoolExecutor(a.j) as ex:
        for f, out in ex.map(lambda e: unit(e, root), cc):
            if out is None:
                failed.append(f)
            else:
                sites |= out
    lines = {}
    for f, line, kind, src, dst in sorted(sites):
        if f not in lines:
            lines[f] = open(os.path.join(root, f), errors="replace").read().split("\n")
        text = lines[f][line - 1].strip() if 0 < line <= len(lines[f]) else ""
        print(f"{f}:{line}: {kind} {src} -> {dst}: {text[:140]}")
    print(f"{len(sites)} sites in {len(cc) - len(failed)} units", file=sys.stderr)
    for f in failed:
        print(f"not parsed: {f}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

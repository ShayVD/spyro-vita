#!/usr/bin/env python3
"""The offset model's cross-unit check (lp64/docs/low-image.md, "The offset model"; spyro-vita's O3, D262): a
variable that one unit declares as a P32 slot and another as a plain pointer (or the other way) compiles in each unit
and links, but the two disagree on its size and contents: an 8-byte pointer stored over a 4-byte slot holding an
offset (spyro-vita's generated `extern void *g_OverlaySpacePointer` was one). Likewise a function whose parameter or
return is a pointer in one declaration and a 32-bit integer in another: the caller's pointer arrives cut to its low
32 bits, not as an offset (spyro-vita's hand C's entries, which became P32_ARG_T).

    p32_decl_check.py BUILD [--jobs N] [--root DIR] [--skip REGEX]... [--overlay-preprocess REGEX]

BUILD is an offset-model build (aarch64, compile_commands.json), where a slot is a union. Every unit is parsed
(p32_offset_rewrite.py's scan, `decls` mode; generated units included, unlike the rewriter: --skip leaves some out)
and each file-scope variable's type classed by its element: a slot (an anonymous union, or a typedef named by
--slot-typedef, default `\\w+Slot`), a pointer, or other. A name
with both a slot and a pointer among its declarations is printed with them, and so is a function with a pointer and
a 32-bit integer in one place (PsyQ's u_long counts as 32 bits, as the game's own headers mean it). The exit status
is 1 if there is any. glibc's two strerror_r declarations are the one expected hit.

From spyro-vita tools/ps4/p32_decl_check.py @ 36d9e07 (MIT; D262).
"""
import argparse, collections, json, os, re, shlex, sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import p32_offset_rewrite as r  # noqa: E402


SLOT_TYPEDEF = [re.compile(r"\w+Slot")]  # --slot-typedef: a slot behind a typedef shows as its name


def element(t):
    t = re.sub(r"\b(const|volatile|extern|static)\b", "", t).strip()
    t = re.sub(r"(\s*\[\d*\])+$", "", t).strip()  # arrays: their element
    # clang 19 prints a slot's type `union (unnamed union at F:L:C)`, Apple clang 16 `union (unnamed at F:L:C)`
    if (re.search(r"\bunion \((unnamed|anonymous)", t) or SLOT_TYPEDEF[0].fullmatch(t)) and not t.endswith("*"):
        return "slot"
    if t.endswith("*") or "(*)" in t:
        return "pointer"
    return "other"


def split_sig(t):
    """'u32 (u32, Moby *, ...)' -> ('u32', ['u32', 'Moby *', '...'])"""
    t = t.strip()
    depth, i = 0, len(t)
    # the parameter list is the last parenthesised group at depth 0
    for j in range(len(t) - 1, -1, -1):
        c = t[j]
        depth += c == ")"
        depth -= c == "("
        if depth == 0:
            i = j
            break
    ret, inner = t[:i].strip(), t[i + 1:-1]
    params, depth, cur = [], 0, ""
    for c in inner:
        if c == "," and depth == 0:
            params.append(cur.strip())
            cur = ""
            continue
        depth += c in "(["
        depth -= c in ")]"
        cur += c
    if cur.strip():
        params.append(cur.strip())
    return ret, params


def word(t):
    """a parameter's or return's class: a pointer, a 32-bit integer, other"""
    t = re.sub(r"\b(const|volatile)\b", "", t).strip()
    if t.endswith("*") or "(*)" in t:
        return "pointer"
    if r.small_int(t) or t in ("u32", "s32", "u_int", "uint32_t", "int32_t", "u_long"):
        return "int32"
    return "other"


def all_units(build, skip):
    """the rewriter's units plus the generated ones it leaves out, but those --skip names"""
    us = r.units(build)
    have = {u.src for u in us}
    with open(os.path.join(build, "compile_commands.json")) as f:
        for e in json.load(f):
            fn = e["file"]
            if not fn.endswith(".c") or fn in have or any(x.search(fn) for x in skip):
                continue
            args = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
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
            if new[0] == "timeout":
                new = new[2:]
            us.append(r.Unit(e["directory"], fn, new))
            have.add(fn)
    return us


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--root", default=os.getcwd(), help="where paths are reported from")
    ap.add_argument("--skip", action="append", default=[], help="units to leave out (a regex; repeatable)")
    ap.add_argument("--overlay-preprocess")
    ap.add_argument("--slot-typedef", default=r"\w+Slot", help="the game's typedefs of slots (a regex)")
    a = ap.parse_args()
    SLOT_TYPEDEF[0] = re.compile(a.slot_typedef)
    r.MODE[0] = "decls"
    r.ROOT = os.path.realpath(a.root)
    if a.overlay_preprocess:
        r.OVERLAY_RE[0] = re.compile(a.overlay_preprocess)
    us = all_units(os.path.abspath(a.build), [re.compile(x) for x in a.skip])
    decls = collections.defaultdict(set)
    funcs = collections.defaultdict(set)
    failed = []
    with ThreadPoolExecutor(a.jobs) as ex:
        for src, rc, sites in ex.map(r.scan, us):
            if rc:
                failed.append(src)
            for what, name, ty, sc, loc in sites:
                if sc == "static":
                    continue
                where = f"{os.path.relpath(loc[0], r.ROOT)}" if isinstance(loc, tuple) and loc[0] else src
                if what == "func":
                    funcs[name].add((ty, where))
                else:
                    decls[name].add((element(ty), ty, where))
    bad = 0
    for name in sorted(decls):
        kinds = {k for k, _, _ in decls[name]}
        if "slot" in kinds and "pointer" in kinds:
            bad += 1
            print(name)
            for k, ty, where in sorted(decls[name]):
                print(f"  {k:7s} {where}: {ty[:120]}")
    # functions: a parameter (or the return) a pointer in one declaration and
    # a 32-bit integer in another; the caller's 64-bit pointer reaches the
    # callee cut to its low 32 bits, not as an offset
    fbad = 0
    for name in sorted(funcs):
        sigs = [(split_sig(ty), ty, where) for ty, where in funcs[name]]
        n = max(len(p) for (_, p), _, _ in sigs)
        hit = []
        for i in range(-1, n):
            kinds = set()
            for (ret, ps), _, _ in sigs:
                t = ret if i < 0 else (ps[i] if i < len(ps) else None)
                if t and t != "...":
                    kinds.add(word(t))
            if {"pointer", "int32"} <= kinds:
                hit.append("return" if i < 0 else f"arg {i}")
        if hit:
            fbad += 1
            print(f"{name}: {', '.join(hit)}")
            for _, ty, where in sorted(sigs, key=lambda x: x[2]):
                print(f"  {where}: {ty[:140]}")
    print(f"units: {len(us)}, failed: {len(failed)}, names: {len(decls)}, slot/pointer mismatches: {bad}, "
          f"functions: {len(funcs)}, pointer/int32 mismatches: {fbad}")
    for f in failed:
        print("  failed:", f)
    sys.exit(1 if bad or fbad or failed else 0)


if __name__ == "__main__":
    main()

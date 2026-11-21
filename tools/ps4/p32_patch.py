#!/usr/bin/env python3
"""Mark the decomp's pointer fields and pointer globals `P32` (D173, D174).

    python3 tools/ps4/p32_patch.py TREE [--out PATCH] [--list]

TREE is a patched copy of the decomp (build/<dir>/patched/spyro-1, made with
every patch folder before patches/spyro-1-ptr32). The script rewrites, in
include/*.h and src/**/*.c:
  - every pointer field in a struct or union body: `T *name;`, `T **name;`,
    `T *name[N];` get `P32` on every level of indirection (`T *P32 *P32 name`),
    since the memory each level points at is the game's, laid out as on the
    PS1;
  - every pointer variable declared at file scope that is `extern` or a
    tentative definition (no initialiser): the data labels the port lays out
    as PS1 RAM (tools/data2s.py), 4 bytes each.
  - every other `T **` (locals, parameters, casts such as
    `*(Vector3D **)&particle->m_Data.m_Raw[20]`): in the decomp's code a
    pointer to a pointer always points into the game's memory, whose
    pointers are 4 bytes, so it becomes `T *P32 *` (T a type: a builtin, or
    a name that starts with a capital letter, or `struct X`).
  - `long` and `u_long` as the type of a declaration (a field, a label, a
    local) or of a store through a cast (`((u_long *)p)[1] = ...`): PsyQ's
    are 32 bits, LP64's 64; they become `int` and `u_int` (the same type on
    the 32-bit targets). A `(u_long *)` that only passes a pointer to the
    PsyQ API (LoadImage) stays.
Function pointers (`T (*name)(...)`) are never touched: a call through a
`__ptr32` function pointer miscompiles (D174); those go through
P32_FNPTR/P32_FNCALL by hand. Lines are never added or removed, so the
32-bit builds, where P32 is empty, keep their debug info byte for byte.

With --out, writes a unified diff (a/ b/ paths) that patch -p1 applies to
TREE's state; --list prints each rewritten declaration for review.
"""
import argparse, difflib, os, re, sys

QUAL = r"(?:const\s+|volatile\s+|struct\s+|union\s+|enum\s+|unsigned\s+|signed\s+|static\s+|extern\s+)*"
# `T *name;` / `T **name[N];` / several declarators are left to the reviewer
DECL = re.compile(r"^(?P<lead>[ \t]*" + QUAL + r"[A-Za-z_]\w*(?:\s+(?:int|char|short|long))?\s*)"
                  r"(?P<stars>\*(?:\s*\*)*)"
                  r"(?P<rest>\s*[A-Za-z_]\w*\s*(?:\[[^\]]*\]\s*)*;)", re.M)
SKIP_TYPES = {"return", "goto", "case", "else", "typedef", "sizeof"}


def brace_kinds(text):
    """For each `{` offset: True when it opens a struct or union body."""
    kinds = {}
    i, n = 0, len(text)
    # strip comments and strings for the scan, keep offsets
    clean = list(text)
    j = 0
    while j < n:
        if text.startswith("//", j):
            k = text.find("\n", j)
            k = n if k < 0 else k
            for x in range(j, k):
                clean[x] = " "
            j = k
        elif text.startswith("/*", j):
            k = text.find("*/", j + 2)
            k = n if k < 0 else k + 2
            for x in range(j, k):
                if clean[x] != "\n":
                    clean[x] = " "
            j = k
        elif text[j] in "\"'":
            q = text[j]
            k = j + 1
            while k < n and text[k] != q:
                k += 2 if text[k] == "\\" else 1
            for x in range(j + 1, min(k, n)):
                clean[x] = " "
            j = k + 1
        else:
            j += 1
    clean = "".join(clean)
    last = 0
    for m in re.finditer(r"[{};]", clean):
        if m.group() == "{":
            head = clean[last:m.start()]
            kinds[m.start()] = bool(re.search(r"\b(struct|union)\b[\s\w]*$", head.strip() + " ")
                                    or re.search(r"\b(struct|union)\s*(\w+\s*)?$", head.rstrip()))
        last = m.end()
    return clean, kinds


def rewrite(text):
    clean, kinds = brace_kinds(text)
    # context at each offset: stack of brace kinds
    ctx = []
    stack = []
    events = sorted([(o, "{") for o in kinds] + [(m.start(), "}") for m in re.finditer(r"}", clean)])
    out, pos, changes = [], 0, []
    ev = 0
    for m in DECL.finditer(clean):
        while ev < len(events) and events[ev][0] < m.start():
            o, c = events[ev]
            if c == "{":
                stack.append(kinds[o])
            elif stack:
                stack.pop()
            ev += 1
        lead, stars, rest = m.group("lead"), m.group("stars"), m.group("rest")
        first = lead.split()[0] if lead.split() else ""
        if first in SKIP_TYPES or "(" in m.group(0):
            continue
        in_struct = bool(stack) and stack[-1]
        at_file = not stack
        orig = text[m.start():m.end()]
        if orig != m.group(0):  # inside a comment or string
            continue
        if in_struct:
            pass
        elif at_file and ("extern" in lead.split() or "=" not in orig):
            if "static" in lead.split() or "typedef" in lead.split():
                continue
        else:
            continue
        if "P32" in orig:
            continue
        nstars = stars.count("*")
        new = lead + " ".join(["*P32"] * nstars) + " " + rest.lstrip()
        out.append(text[pos:m.start()])
        out.append(new)
        pos = m.end()
        changes.append((orig.strip(), new.strip(), "field" if in_struct else "global"))
    out.append(text[pos:])
    return "".join(out), changes


BUILTIN = r"(?:void|char|short|int|long|u_char|u_short|u_int|u_long|uint8_t|uint16_t|uint32_t|int8_t|int16_t|int32_t|u8|u16|u32|s8|s16|s32)"
PTRPTR = re.compile(r"\b((?:struct\s+\w+|" + BUILTIN + r"|[A-Z]\w*))(\s*)\*(\s*)\*(?!\s*P32)")


# a file-scope pointer definition with an initializer: `T *name[N] = ...`
INITDEF = re.compile(r"^(?P<type>[A-Za-z_]\w*)\s*(?P<stars>\*)(?P<name>[A-Za-z_]\w*)(?P<dims>(?:\[[^\]]*\])*)"
                     r"(?P<eq>\s*=\s*)(?P<init>[^\n]*)$", re.M)
ZERO_INIT = re.compile(r"^\{*\s*0\s*\}*\s*;\s*$|^0\s*;\s*$|^NULL\s*;\s*$")


def rewrite_initdefs(text):
    """`T *name[N] = {0};` -> `T *P32 name[N] = {0};`; an initializer with
    addresses (clang can't take them for __ptr32) -> `T *P32_INIT(name)[N] =`
    and P32_INIT_ARRAY/P32_INIT_SCALAR(T, name) appended at the file's end."""
    clean, kinds = brace_kinds(text)
    depth_at = {}
    out, pos, changes, fills = [], 0, [], []
    for m in INITDEF.finditer(clean):
        if text[m.start():m.end()] != m.group(0):
            continue
        if clean[:m.start()].count("{") != clean[:m.start()].count("}"):
            continue  # not at file scope
        t_, name, dims = m.group("type"), m.group("name"), m.group("dims")
        if t_ in ("static", "typedef", "return", "extern"):
            continue
        # the #else of an #ifdef SPYRO_PORT: the port never compiles it
        before = text[:m.start()].rstrip().split("\n")
        if before[-1].strip() == "#else":
            opener = next((l for l in reversed(before[:-1]) if l.lstrip().startswith("#if")), "")
            if opener.split() == ["#ifdef", "SPYRO_PORT"]:
                continue
        if ZERO_INIT.match(m.group("init")):
            new = f"{t_} *P32 {name}{dims}{m.group('eq')}{m.group('init')}"
            kind = "global"
        else:
            new = f"{t_} *P32_INIT({name}){dims}{m.group('eq')}{m.group('init')}"
            fills.append(f"P32_INIT_{'ARRAY' if dims else 'SCALAR'}({t_}, {name})")
            kind = "init"
        out.append(text[pos:m.start()])
        out.append(new)
        pos = m.end()
        changes.append((m.group(0).strip(), new.strip(), kind))
    out.append(text[pos:])
    new = "".join(out)
    if fills:
        new = new.rstrip("\n") + "\n" + "\n".join(fills) + "\n"
    return new, changes


LONG_DECL = re.compile(r"^(?P<lead>[ \t]*(?:extern\s+|static\s+|const\s+|volatile\s+)*)"
                       r"(?P<t>u_long|unsigned\s+long|long)(?P<rest>\s+\**\s*[A-Za-z_]\w*\s*(?:\[[^\]]*\]\s*)*[;=,])",
                       re.M)
LONG_STORE = re.compile(r"(?P<pre>\(\s*\(\s*|\*\s*\(\s*)(?P<t>u_long|long)(?P<post>\s*\*\s*\))")


def rewrite_long(text):
    """32-bit PsyQ longs: declarations and dereferenced casts -> int/u_int."""
    clean, _ = brace_kinds(text)
    changes = []
    for rx, kind in ((LONG_DECL, "decl"), (LONG_STORE, "store")):
        out, pos = [], 0
        for m in rx.finditer(clean):
            if text[m.start():m.end()] != m.group(0):
                continue
            if kind == "decl" and m.group("lead").strip() == "" and "long long" in m.group(0):
                continue
            new_t = "int" if m.group("t") == "long" else "u_int"
            if kind == "decl":
                new = m.group("lead") + new_t + m.group("rest")
            else:
                new = m.group("pre") + new_t + m.group("post")
            out.append(text[pos:m.start()])
            out.append(new)
            pos = m.end()
            line = text.count("\n", 0, m.start()) + 1
            changes.append((f"line {line}: {m.group(0).strip()}", new.strip(), "long"))
        out.append(text[pos:])
        text = "".join(out)
        clean, _ = brace_kinds(text)
    return text, changes


def rewrite_ptrptr(text):
    """`T **` -> `T *P32 *` outside comments and strings."""
    clean, _ = brace_kinds(text)
    out, pos, changes = [], 0, []
    for m in PTRPTR.finditer(clean):
        if text[m.start():m.end()] != m.group(0):
            continue
        out.append(text[pos:m.start()])
        new = m.group(1) + m.group(2) + "*P32 *"
        out.append(new)
        pos = m.end()
        line = text.count("\n", 0, m.start()) + 1
        changes.append((f"line {line}: {m.group(0)}", new, "ptrptr"))
    out.append(text[pos:])
    return "".join(out), changes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tree")
    ap.add_argument("--out")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    files = []
    for d in ("include", "src"):
        for dp, _, fs in os.walk(os.path.join(a.tree, d)):
            for f in sorted(fs):
                if f.endswith((".h", ".c")):
                    files.append(os.path.join(dp, f))
    diff, total = [], {"field": 0, "global": 0, "ptrptr": 0, "init": 0, "long": 0}
    for p in sorted(files):
        rel = os.path.relpath(p, a.tree)
        old = open(p, encoding="latin-1").read()
        new, ch = rewrite(old)
        new, ch2 = rewrite_ptrptr(new)
        ch += ch2
        new, ch4 = rewrite_long(new)
        ch += ch4
        if rel.endswith(".c"):
            new, ch3 = rewrite_initdefs(new)
            ch += ch3
        if not ch:
            continue
        # lines only ever added at a file's end (P32_INIT_*)
        assert new.startswith(old[:0]) and old.count("\n") <= new.count("\n"), rel
        for o, n, k in ch:
            total[k] += 1
            if a.list:
                print(f"{rel}: [{k}] {o}  ->  {n}")
        diff += difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                     "a/" + rel, "b/" + rel, n=1)
    print(f"P32: {total['field']} fields, {total['global']} globals, {total['ptrptr']} other T **, "
          f"{total['init']} address-initialised (P32_INIT), {total['long']} PsyQ longs",
          file=sys.stderr)
    if a.out:
        open(a.out, "w", encoding="latin-1").writelines(diff)


if __name__ == "__main__":
    main()

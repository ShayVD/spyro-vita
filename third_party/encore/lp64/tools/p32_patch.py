#!/usr/bin/env python3
"""Mark a decomp's pointer fields and pointer data labels `P32` (the low-image model, lp64/docs/low-image.md).

    python3 lp64/tools/p32_patch.py TREE [--out PATCH] [--list] [--dirs include,src]
        [--skip-else-of MACRO]... [--type-regex REGEX] [--long-names]

TREE is a patched copy of the decomp (every patch the port applies, but this one). The script rewrites, in the .h
and .c files under each of --dirs (default include and src):
  - every pointer field in a struct or union body: `T *name;`, `T **name;`, `T *name[N];` get P32 on every level of
    indirection (`T *P32 *P32 name`), since the memory each level points at is the game's, laid out as on the
    original;
  - every pointer variable declared at file scope that is `extern` or a tentative definition (no initialiser): the
    data labels, 4 bytes each;
  - every other `T **` (locals, parameters, casts such as `*(Vector3D **)&p->m_Raw[20]`): in a decomp's code a
    pointer to a pointer points into the game's memory, whose pointers are 4 bytes, so it becomes `T *P32 *`. T is a
    builtin or common decomp integer type, `struct X`, or a name matching --type-regex (default `[A-Z]\\w*`: decomps
    capitalise their types);
  - `long` and `u_long` as the type of a declaration (a field, a label, a local) or of a store through a cast
    (`((u_long *)p)[1] = ...`): the console SDKs' are 32 bits (PsyQ's on the PS1), LP64's 64; they become `int` and
    `u_int` (the same type on the 32-bit targets; the decomp must have `u_int`). A `(u_long *)` that only passes a
    pointer to the SDK stays;
  - in .c files, a file-scope pointer definition with an initialiser: `= {0}`, `= 0` or `= NULL` gets P32; one that
    holds addresses becomes `T *P32_INIT(name)[N] = ...` with `P32_INIT_ARRAY(T, name)` (or P32_INIT_SCALAR) appended
    at the file's end, since clang 19 can't emit a __ptr32 static initializer with an address. A definition right
    after the `#else` of `#ifdef MACRO` for a --skip-else-of MACRO is left alone (the port never compiles it;
    spyro-vita passes SPYRO_PORT).
Function pointers (`T (*name)(...)`) are never touched: a call through a __ptr32 function pointer miscompiles
(lp64/docs/clang-bugs.md); keep those as the port's own 8-byte globals, by hand. Lines are never added or removed
before a file's end, so the 32-bit builds, where P32 is empty, keep their line numbers and debug info.

With --out, writes a unified diff (a/ b/ paths) that `patch -p1` applies to TREE's state; --list prints each
rewritten declaration for review. The counts go to stderr. --long-names writes LP64_P32, LP64_P32_INIT, ... instead
of the short aliases.

From spyro-vita tools/ps4/p32_patch.py @ c25a3ea (MIT; D174). Its SPYRO_PORT #else rule is --skip-else-of, its
fixed include/src walk is --dirs, its capitalised-type rule is --type-regex. Changed: an initialiser holding strings
(`= {"A", "BB"}`) is taken; spyro's compared it with the string-blanked text and skipped it.
"""
import argparse, difflib, os, re, sys

P32, P32_INIT, P32_INIT_ARRAY, P32_INIT_SCALAR = "P32", "P32_INIT", "P32_INIT_ARRAY", "P32_INIT_SCALAR"

QUAL = r"(?:const\s+|volatile\s+|struct\s+|union\s+|enum\s+|unsigned\s+|signed\s+|static\s+|extern\s+)*"
# `T *name;` / `T **name[N];` / several declarators are left to the reviewer
DECL = re.compile(r"^(?P<lead>[ \t]*" + QUAL + r"[A-Za-z_]\w*(?:\s+(?:int|char|short|long))?\s*)"
                  r"(?P<stars>\*(?:\s*\*)*)"
                  r"(?P<rest>\s*[A-Za-z_]\w*\s*(?:\[[^\]]*\]\s*)*;)", re.M)
SKIP_TYPES = {"return", "goto", "case", "else", "typedef", "sizeof"}


def brace_kinds(text):
    """The text with comments and strings blanked (offsets kept), and for each `{` offset: True when it opens a
    struct or union body."""
    kinds = {}
    n = len(text)
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
    """Pointer fields and file-scope pointer labels: (new text, [(old, new, 'field'|'global')])."""
    clean, kinds = brace_kinds(text)
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
        if re.search(r"\b(LP64_)?P32\b", orig):
            continue
        nstars = stars.count("*")
        new = lead + " ".join(["*" + P32] * nstars) + " " + rest.lstrip()
        out.append(text[pos:m.start()])
        out.append(new)
        pos = m.end()
        changes.append((orig.strip(), new.strip(), "field" if in_struct else "global"))
    out.append(text[pos:])
    return "".join(out), changes


BUILTIN = (r"(?:void|char|short|int|long|u_char|u_short|u_int|u_long|uint8_t|uint16_t|uint32_t|int8_t|int16_t|"
           r"int32_t|u8|u16|u32|s8|s16|s32)")
PTRPTR = None


def set_type_regex(rx):
    global PTRPTR
    PTRPTR = re.compile(r"\b((?:struct\s+\w+|" + BUILTIN + r"|" + rx + r"))(\s*)\*(\s*)\*(?!\s*(?:LP64_)?P32)")


set_type_regex(r"[A-Z]\w*")

# a file-scope pointer definition with an initializer: `T *name[N] = ...`
INITDEF = re.compile(r"^(?P<type>[A-Za-z_]\w*)\s*(?P<stars>\*)(?P<name>[A-Za-z_]\w*)(?P<dims>(?:\[[^\]]*\])*)"
                     r"(?P<eq>\s*=\s*)(?P<init>[^\n]*)$", re.M)
ZERO_INIT = re.compile(r"^\{*\s*0\s*\}*\s*;\s*$|^0\s*;\s*$|^NULL\s*;\s*$")


def rewrite_initdefs(text, skip_else_of=()):
    """`T *name[N] = {0};` -> `T *P32 name[N] = {0};`; an initializer with addresses -> `T *P32_INIT(name)[N] =`
    and P32_INIT_ARRAY/P32_INIT_SCALAR(T, name) appended at the file's end."""
    clean, _ = brace_kinds(text)
    out, pos, changes, fills = [], 0, [], []
    for m in INITDEF.finditer(clean):
        # the declarator must be code; the initialiser may hold strings (blanked in `clean`), taken from `text`
        if text[m.start():m.end("eq")] != clean[m.start():m.end("eq")]:
            continue
        init = text[m.start("init"):m.end()]
        if clean[:m.start()].count("{") != clean[:m.start()].count("}"):
            continue  # not at file scope
        t_, name, dims = m.group("type"), m.group("name"), m.group("dims")
        if t_ in ("static", "typedef", "return", "extern"):
            continue
        # the #else of an #ifdef MACRO the port defines: never compiled
        before = text[:m.start()].rstrip().split("\n")
        if skip_else_of and before[-1].strip() == "#else":
            opener = next((l for l in reversed(before[:-1]) if l.lstrip().startswith("#if")), "")
            if len(opener.split()) == 2 and opener.split()[0] == "#ifdef" and opener.split()[1] in skip_else_of:
                continue
        if ZERO_INIT.match(init):
            new = f"{t_} *{P32} {name}{dims}{m.group('eq')}{init}"
            kind = "global"
        else:
            new = f"{t_} *{P32_INIT}({name}){dims}{m.group('eq')}{init}"
            fills.append(f"{P32_INIT_ARRAY if dims else P32_INIT_SCALAR}({t_}, {name})")
            kind = "init"
        out.append(text[pos:m.start()])
        out.append(new)
        pos = m.end()
        changes.append((text[m.start():m.end()].strip(), new.strip(), kind))
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
    """32-bit SDK longs: declarations and dereferenced casts -> int/u_int."""
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
        new = m.group(1) + m.group(2) + "*" + P32 + " *"
        out.append(new)
        pos = m.end()
        line = text.count("\n", 0, m.start()) + 1
        changes.append((f"line {line}: {m.group(0)}", new, "ptrptr"))
    out.append(text[pos:])
    return "".join(out), changes


def rewrite_file(rel, old, skip_else_of=()):
    new, ch = rewrite(old)
    new, ch2 = rewrite_ptrptr(new)
    new, ch4 = rewrite_long(new)
    ch += ch2 + ch4
    if rel.endswith(".c"):
        new, ch3 = rewrite_initdefs(new, skip_else_of)
        ch += ch3
    return new, ch


def main(argv=None):
    global P32, P32_INIT, P32_INIT_ARRAY, P32_INIT_SCALAR
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("tree")
    ap.add_argument("--out")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--dirs", default="include,src", help="comma-separated, relative to TREE")
    ap.add_argument("--skip-else-of", action="append", default=[], metavar="MACRO")
    ap.add_argument("--type-regex", default=r"[A-Z]\w*", help="type names whose T ** becomes T *P32 *")
    ap.add_argument("--long-names", action="store_true")
    a = ap.parse_args(argv)
    if a.long_names:
        P32, P32_INIT, P32_INIT_ARRAY, P32_INIT_SCALAR = ("LP64_P32", "LP64_P32_INIT", "LP64_P32_INIT_ARRAY",
                                                          "LP64_P32_INIT_SCALAR")
    set_type_regex(a.type_regex)
    files = []
    for d in a.dirs.split(","):
        for dp, dns, fs in os.walk(os.path.join(a.tree, d)):
            dns.sort()
            for f in sorted(fs):
                if f.endswith((".h", ".c")):
                    files.append(os.path.join(dp, f))
    diff, total = [], {"field": 0, "global": 0, "ptrptr": 0, "init": 0, "long": 0}
    for p in sorted(files):
        rel = os.path.relpath(p, a.tree)
        old = open(p, encoding="latin-1").read()
        new, ch = rewrite_file(rel, old, set(a.skip_else_of))
        if not ch:
            continue
        # lines are only ever added at a file's end (P32_INIT_*)
        ol, nl = old.splitlines(), new.splitlines()
        assert len(nl) >= len(ol) and all(x.startswith(P32_INIT) for x in nl[len(ol):]), rel
        for o, n, k in ch:
            total[k] += 1
            if a.list:
                print(f"{rel}: [{k}] {o}  ->  {n}")
        diff += difflib.unified_diff(old.splitlines(True), new.splitlines(True), "a/" + rel, "b/" + rel, n=1)
    print(f"P32: {total['field']} fields, {total['global']} globals, {total['ptrptr']} other T **, "
          f"{total['init']} address-initialised (P32_INIT), {total['long']} SDK longs", file=sys.stderr)
    if a.out:
        open(a.out, "w", encoding="latin-1").writelines(diff)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Route every use of a P32 slot through src/compat/p32.h's accessors (D250 O2, D258).

    tools/tc python3 tools/ps4/p32_offset_rewrite.py BUILD [--jobs N] [--apply] [--list FILE]

BUILD is a linux64 tree (-DCMAKE_EXPORT_COMPILE_COMMANDS=ON, configured and
built far enough that the patched decomp and gen/ exist), whose P32 slots are
`__ptr32 __uptr` and so visible in clang's AST. Every unit is parsed again
(-fsyntax-only -Xclang -ast-dump=json, streamed); the overlays from their own sources in
BUILD/patched/spyro-1/src/overlays, not the stripped gen/ovl text, with the
flags of their preprocess step in build.ninja. Each site is found by its
spelling location and counted once:

  load    a read of a slot (LValueToRValue of a type whose outermost pointer
          is __ptr32): `x` becomes `P32_GET(x)`
  store   `x = v` into a slot: `P32_SET(x, v)`
  add     `x += n`, `x -= n`: `P32_ADD(x, n)`, `P32_SUB(x, n)`
  step    `++x`, `--x`, `x++`, `x--`: `P32_PREINC(x)` ... `P32_POSTDEC(x)`

Where a site is written: in ordinary text (a macro's argument too), it is
rewritten there; a strict part of a #define's body is rewritten in the body,
once; the whole of a macro, or a site that ends in one (`x = NULL`), is
wrapped where the macro is used. Anything else (a token paste, an operator
inside a macro) is listed (--list), to be done by hand. Text is only inserted or replaced within a line, so no line
moves: on the 32-bit and x86-64 builds the accessors expand to the text that
was there (`(x)`, `((x) = (v))`), and only debug-info columns change.

Without --apply the tool reports what it would do. With --apply it edits the
files under src/ in place and writes the decomp's edits as patches,
patches/spyro-1-p32off/0001-p32-accessors-N.patch (a/ b/ paths, whole files
in parts under 500 KB; against
BUILD/patched/spyro-1, which has every patch folder before this one).
"""
import argparse, collections, difflib, json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
def is_slot(ty):
    """A slot's type is printed with its outermost pointer's qualifiers last:
    'Moby * __ptr32 __uptr'. A pointer to a slot ('Moby * __ptr32 __uptr *')
    is a normal pointer."""
    return ty.rstrip().endswith(("__ptr32 __uptr", "__uptr __ptr32", "__ptr32"))


class Unit:
    def __init__(self, d, src, cmd):
        self.d, self.src, self.cmd = d, src, cmd


def units(build):
    out, seen = [], set()
    with open(os.path.join(build, "compile_commands.json")) as f:
        cc = json.load(f)
    for e in cc:
        fn = e["file"]
        if "/gen/" in fn or not fn.endswith((".c", ".cpp")):
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
        key = (fn, tuple(a for a in new if a.startswith("-D")))
        if key in seen:
            continue
        seen.add(key)
        out.append(Unit(e["directory"], fn, new))
    # the overlays: their own sources, once each, with the preprocess step's flags
    pre = re.compile(r"port_overlays\.py preprocess .*? -- (.*?\.c)(?:\s|$)")
    done = set()
    with open(os.path.join(build, "build.ninja")) as f:
        for line in f:
            m = pre.search(line)
            if not m:
                continue
            args = shlex.split(m.group(1))
            src = args[-1]
            if src in done:
                continue
            done.add(src)
            args = [a for a in args if a != "-E"]
            out.append(Unit(build, src, args))
    return out


def scan(u):
    """Stream clang's JSON AST (moby_update.c's is 1 GB): keep only the
    open objects, finish each node when it closes. A location is
    (file, offset, tokLen); a macro location has a spelling and an expansion
    (and isMacroArgExpansion). Like the text dump, a location names its file
    and line only when they change, in document order."""
    cmd = u.cmd + ["-fsyntax-only", "-w", "-fno-color-diagnostics", "-Xclang", "-ast-dump=json"]
    p = subprocess.Popen(cmd, cwd=u.d, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                         errors="replace", bufsize=1 << 20)
    sites = []
    last = [None]
    stack = []  # [key, dict or None (a list)]
    KEEP = {"kind", "castKind", "opcode", "isPostfix", "offset", "file", "tokLen", "qualType",
            "isMacroArgExpansion"}
    LOCS = ("begin", "end", "loc", "spellingLoc", "expansionLoc")

    def bare(d):
        if "file" in d:
            last[0] = d["file"]
        return (last[0], d["offset"], d.get("tokLen", 0))

    def close(key, d):
        """a dict is done: a location gets its file; a node becomes a site"""
        if "offset" in d and key != "includedFrom":
            if d.get("isMacroArgExpansion") and stack and stack[-1][1] is not None:
                stack[-1][1]["isMacroArgExpansion"] = True
            return bare(d)
        if "spellingLoc" in d or "expansionLoc" in d:
            sp, ex = d.get("spellingLoc"), d.get("expansionLoc")
            return ("M", sp, ex, bool(d.get("isMacroArgExpansion")))
        if "kind" in d and "range" in d:
            node(d)
            # the first child's range, for a load through a macro parameter
            if len(stack) >= 2 and stack[-1][0] == "inner" and stack[-1][1] is None:
                parent = stack[-2][1]
                if parent is not None and "_child" not in parent:
                    r = d["range"]
                    if isinstance(r, dict) and "begin" in r and "end" in r:
                        parent["_child"] = (d["kind"], norm(r["begin"]), norm(r["end"]), d.get("_child"))
        return d

    def node(d):
        kind = d["kind"]
        ty = d.get("type", {}).get("qualType", "") if isinstance(d.get("type"), dict) else ""
        k = None
        if kind == "ImplicitCastExpr" and d.get("castKind") == "LValueToRValue" and is_slot(ty):
            k = "load"
        elif kind == "BinaryOperator" and d.get("opcode") == "=" and is_slot(ty):
            k = "store"
        elif kind == "CompoundAssignOperator" and is_slot(ty) and d.get("opcode") in ("+=", "-="):
            k = "add" if d["opcode"] == "+=" else "sub"
        elif kind == "UnaryOperator" and is_slot(ty) and d.get("opcode") in ("++", "--"):
            k = ("post" if d.get("isPostfix") else "pre") + ("inc" if d["opcode"] == "++" else "dec")
        if k:
            r = d["range"]
            if isinstance(r, dict) and "begin" in r and "end" in r:
                sites.append((k, norm(r["begin"]), norm(r["end"]), d.get("_child") if k == "load" else None))

    for raw in p.stdout:
        line = raw.strip()
        if not line:
            continue
        if line[0] == '"':
            q = line.index('"', 1)
            if line[q + 1:q + 2] != ":":
                continue  # a string in an array
            key = line[1:q]
            val = line[q + 2:].strip()
            if val.endswith(","):
                val = val[:-1]
            if val == "{":
                stack.append([key, {}])
                continue
            if val == "[":
                stack.append([key, None])
                continue
            if key in KEEP and stack and stack[-1][1] is not None:
                stack[-1][1][key] = json.loads(val)
            continue
        c = line[0]
        if c == "{":
            stack.append([None, {}])
        elif c == "[":
            stack.append([None, None])
        elif c in "}]":
            key, d = stack.pop()
            if d is None:
                continue
            v = close(key, d)
            if stack and stack[-1][1] is not None and key is not None and (key in LOCS or key in ("range", "type")):
                stack[-1][1][key] = v
    p.wait()
    return u.src, p.returncode, sites


def norm(loc):
    """(spelling, expansion, from a macro argument): each (file, offset, tokLen)"""
    if isinstance(loc, tuple) and loc and loc[0] == "M":
        _, sp, ex, arg = loc
        return (sp, ex, arg)
    return (loc, loc, False)


class Source:
    def __init__(self, path):
        self.path = path
        with open(path, encoding="latin-1") as f:
            self.text = f.read()
        self.starts = [0]
        for i, ch in enumerate(self.text):
            if ch == "\n":
                self.starts.append(i + 1)
        # #define directives, continuations included: (start, end, body start, function-like)
        self.defines = []
        for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(\w+)(\()?", self.text, re.M):
            e = m.end()
            while True:
                nl = self.text.find("\n", e)
                nl = len(self.text) if nl < 0 else nl
                if self.text[:nl].rstrip(" \t\r").endswith("\\"):
                    e = nl + 1
                    continue
                break
            body, params = m.end(), ()
            if m.group(2):
                body = self.text.index(")", m.end()) + 1
                params = tuple(x.strip() for x in self.text[m.end():body - 1].split(","))
            self.defines.append((m.start(), nl, body, params))

    def define_at(self, o):
        for d in self.defines:
            if d[0] <= o < d[1]:
                return d
        return None

    def strip(self, a, b):
        """text[a:b] without whitespace, continuations or enclosing parens"""
        t = re.sub(r"\\\n|\s+", "", self.text[a:b])
        while t.startswith("(") and t.endswith(")"):
            depth = 0
            for i, ch in enumerate(t):
                depth += ch == "("
                depth -= ch == ")"
                if depth == 0 and i < len(t) - 1:
                    return t
            t = t[1:-1]
        return t

    def off(self, line, col):
        return self.starts[line - 1] + col - 1

    def tok_end(self, o):
        """the end of the token that starts at o"""
        t = self.text
        m = re.compile(r"[A-Za-z_]\w*|\.?\d(?:[\w.]|[eEpP][+-])*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'"
                       r"|->|\+\+|--|<<=|>>=|[-+*/%&|^<>=!]=|&&|\|\||<<|>>|\S").match(t, o)
        return o + len(m.group(0))

    def skip_ws(self, o):
        t = self.text
        while True:
            while o < len(t) and t[o] in " \t\r\n\\":
                o += 1
            if t.startswith("/*", o):
                o = t.index("*/", o) + 2
            elif t.startswith("//", o):
                o = t.index("\n", o)
            else:
                return o


PREFIX = {"store": "P32_SET(", "add": "P32_ADD(", "sub": "P32_SUB(", "preinc": "P32_PREINC(",
          "predec": "P32_PREDEC(", "postinc": "P32_POSTINC(", "postdec": "P32_POSTDEC("}


def locate(k, b, e, sources):
    """where a site's text is: (file, start, end, how) or (None, why)"""
    (sb, xb, _), (se, xe, _) = b, e
    if not (isinstance(sb, tuple) and isinstance(se, tuple)):
        return None, "no location"
    real = lambda loc: loc[0] in sources
    # 1. both ends spelled in ordinary text (macro arguments included)
    if real(sb) and sb[0] == se[0]:
        src = sources[sb[0]]
        db, de = src.define_at(sb[1]), src.define_at(se[1])
        if db is None and de is None and sb[1] <= se[1]:
            return (sb[0], sb[1], se[1] + se[2], "text"), None
        # 2. a strict part of one #define's body: rewrite the body once
        if db is not None and db == de and sb[1] <= se[1] and sb[1] >= db[2]:
            t = src.strip(sb[1], se[1] + se[2])
            if t in db[3]:
                # only a parameter: a slot or not by each use's argument
                return None, "a macro parameter"
            if t != src.strip(db[2], db[1]):
                return (sb[0], sb[1], se[1] + se[2], "body"), None
    # 3. the whole of a macro, or ends in one: wrap it where it is used
    if real(xb) and real(xe) and xb[0] == xe[0]:
        src = sources[xb[0]]
        if src.define_at(xb[1]) is None and src.define_at(xe[1]) is None and xb[1] <= xe[1]:
            end = xe[1] + xe[2]
            if se != xe:
                # the end is a macro's expansion point: take its arguments too
                o = src.skip_ws(end)
                if src.text.startswith("(", o):
                    depth = 0
                    while o < len(src.text):
                        depth += src.text[o] == "("
                        depth -= src.text[o] == ")"
                        o += 1
                        if depth == 0:
                            break
                    end = o
            return (xb[0], xb[1], end, "use"), None
    if sb[0] == "<scratch space>" or se[0] == "<scratch space>":
        return None, "token paste"
    return None, "macro"


def plan(sites, sources):
    """edits per file: (offset, end, order, text) replacing text[offset:end];
    and the sites that are listed instead"""
    edits = collections.defaultdict(list)
    listed, how = [], collections.Counter()
    done = set()
    for (k, b, e, child) in sorted(sites, key=repr):
        where, why = locate(k, b, e, sources)
        if why == "a macro parameter" and k == "load":
            # wrap the argument where the macro is used: the first descendant
            # spelled in ordinary text. PATCH_POINTER's dest is written too:
            # those become P32_PATCH by hand.
            src = sources[b[0][0]]
            name = re.match(r"[ \t]*#[ \t]*define[ \t]+(\w+)", src.text[src.define_at(b[0][1])[0]:]).group(1)
            c = child
            while c is not None and name not in ("PATCH_POINTER", "PATCH_POINTER2"):
                w, _ = locate(k, c[1], c[2], sources)
                if w is not None and w[3] == "text":
                    where, why = (w[0], w[1], w[2], "arg"), None
                    break
                c = c[3]
        if where is None:
            listed.append((k, b, e, why))
            continue
        fb, ob, oe, h = where
        if (k, fb, ob, oe) in done:
            continue
        done.add((k, fb, ob, oe))
        s = sources[fb]
        span = oe - ob
        if k == "load":
            edits[fb] += [(ob, ob, (1, -span), "P32_GET("), (oe, oe, (0, span), ")")]
        elif k in ("store", "add", "sub"):
            # the operator: the first `=`/`+=`/`-=` at depth 0 after the LHS
            op = {"store": "=", "add": "+=", "sub": "-="}[k]
            o, depth, found = ob, 0, None
            while o < oe:
                o = s.skip_ws(o)
                if o >= oe:
                    break
                t = s.text[o:s.tok_end(o)]
                if t in ("(", "[", "{"):
                    depth += 1
                elif t in (")", "]", "}"):
                    depth -= 1
                elif t == op and depth == 0:
                    found = o
                    break
                o += len(t)
            if found is None:
                listed.append((k, b, e, "operator not found"))
                continue
            if "{" in s.text[found:oe] or "#" in s.text[ob:oe].replace("##", ""):
                listed.append((k, b, e, "braces or a directive in the operands"))
                continue
            edits[fb] += [(ob, ob, (1, -span), PREFIX[k]), (found, found + len(op), (1, 0), ","),
                          (oe, oe, (0, span), ")")]
        elif k in ("preinc", "predec"):
            if s.text[ob:ob + 2] not in ("++", "--"):
                listed.append((k, b, e, "operator not found"))
                continue
            edits[fb] += [(ob, ob + 2, (1, -span), PREFIX[k]), (oe, oe, (0, span), ")")]
        else:
            if s.text[oe - 2:oe] not in ("++", "--"):
                listed.append((k, b, e, "operator not found"))
                continue
            edits[fb] += [(ob, ob, (1, -span), PREFIX[k]), (oe - 2, oe, (0, span), ")")]
        how[h] += 1
    return edits, listed, how


def apply(text, eds):
    """apply edits (offset, end, order, text): at one offset, closing text
    (order 0, the inner site's first) comes before opening text (order 1, the
    outer site's first)"""
    eds = sorted(set(eds), key=lambda x: (x[0], x[2]))
    out, pos = [], 0
    for o, e, _, t in eds:
        if o < pos:
            raise ValueError(f"overlapping edit at {o}")
        out.append(text[pos:o])
        out.append(t)
        pos = e
    out.append(text[pos:])
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--list", help="every site, and why a listed one isn't rewritten")
    a = ap.parse_args()
    build = os.path.abspath(a.build)
    s1 = os.path.join(build, "patched", "spyro-1")
    us = units(build)
    print(f"{len(us)} units", file=sys.stderr)
    allsites, failed = set(), []
    with ThreadPoolExecutor(a.jobs) as ex:
        for src, rc, sites in ex.map(scan, us):
            if rc:
                failed.append(src)
            allsites.update(sites)
    if failed:
        print("units that didn't parse:", *failed, sep="\n  ", file=sys.stderr)
        sys.exit(1)

    def mine(f):
        f = os.path.normpath(f)
        return f.startswith(s1 + "/") or (f.startswith(ROOT + "/src/") and "/build" not in f
                                           and not f.endswith("/src/compat/p32.h"))

    files = {loc[0] for _, b, e, _ in allsites for x in (b, e) for loc in x[:2]
             if isinstance(loc, tuple) and loc[0] and mine(loc[0])}
    sources = {f: Source(f) for f in files}
    # a site counts when any of its locations is in our text
    sites = [x for x in allsites if any(isinstance(l, tuple) and l[0] in sources for y in x[1:3] for l in y[:2])]
    edits, listed, how = plan(sites, sources)

    def org(x):
        f = x[1][1][0] if isinstance(x[1][1], tuple) else ""
        return "decomp" if f and f.startswith(s1) else "src"
    by = collections.Counter((org(x), x[0]) for x in sites)
    kinds = ["load", "store", "add", "sub", "preinc", "predec", "postinc", "postdec"]
    for o in ("decomp", "src"):
        print(f"{o:7s}", " ".join(f"{k}={by[(o, k)]}" for k in kinds if by[(o, k)]))
    print(f"rewritten: {sum(how.values())} sites in {len(edits)} files ({dict(how)}); listed: {len(listed)}")
    for why, n in collections.Counter(w for *_, w in listed).most_common():
        print(f"  listed, {why}: {n}")

    if a.list:
        with open(a.list, "w") as f:
            for k, b, e, why in sorted(listed, key=repr):
                for loc in (b[0], b[1], e[0]):
                    if isinstance(loc, tuple) and loc[0] in sources:
                        src = sources[loc[0]]
                        ln = src.text.count("\n", 0, loc[1]) + 1
                        f.write(f"{os.path.relpath(loc[0], ROOT)}:{ln}\t{k}\t{why}\n")
                        break
                else:
                    f.write(f"{b}\t{k}\t{why}\n")

    patch = collections.OrderedDict()
    for fn in sorted(edits):
        s = sources[fn]
        new = apply(s.text, edits[fn])
        assert new.count("\n") == s.text.count("\n"), fn
        if not a.apply:
            continue
        if fn.startswith(s1 + "/"):
            rel = os.path.relpath(fn, s1)
            patch[rel] = list(difflib.unified_diff(s.text.splitlines(True), new.splitlines(True), "a/" + rel, "b/" + rel))
        else:
            with open(fn, "w", encoding="latin-1") as f:
                f.write(new)
    if a.apply:
        # whole files packed into parts under tools/check_no_assets.sh's
        # 512 KiB limit (moby_update.c's alone is ~490 KB)
        d = os.path.join(ROOT, "patches", "spyro-1-p32off")
        os.makedirs(d, exist_ok=True)
        for old_part in os.listdir(d):
            if old_part.startswith("0001-p32-accessors"):
                os.remove(os.path.join(d, old_part))
        parts, cur, size = [], [], 0
        for rel, lines in sorted(patch.items(), key=lambda kv: -sum(map(len, kv[1]))):
            n = sum(map(len, lines))
            if cur and size + n > 500000:
                parts.append(cur)
                cur, size = [], 0
            cur.append(rel)
            size += n
        if cur:
            parts.append(cur)
        for i, files in enumerate(parts, 1):
            with open(os.path.join(d, f"0001-p32-accessors-{i}.patch"), "w", encoding="latin-1") as f:
                for rel in sorted(files):
                    f.writelines(patch[rel])


if __name__ == "__main__":
    main()

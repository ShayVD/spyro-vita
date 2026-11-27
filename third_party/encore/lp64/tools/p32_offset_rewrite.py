#!/usr/bin/env python3
"""Route every use of a P32 slot through lp64_p32.h's accessors: the offset model's rewriter
(lp64/docs/low-image.md, "The offset model"; spyro-vita's O2 and O3, D258 and D262).

    p32_offset_rewrite.py BUILD [--src DIR]... [--decomp DIR --patch-dir DIR] [--apply] [--list FILE] [--casts]

BUILD has compile_commands.json (-DCMAKE_EXPORT_COMPILE_COMMANDS=ON). Every unit is parsed again (-fsyntax-only
-Xclang -ast-dump=json, streamed: a decomp's biggest unit gave 1 GB). Each site is found by its spelling location and
counted once. Two passes, each on its own build:

Slots (the default; O2), on a low-image build (x86-64 clang, -fms-extensions), whose slots are `__ptr32 __uptr` and
so visible in clang's AST:
  load    a read of a slot (LValueToRValue of a type whose outermost pointer is __ptr32): `x` -> `P32_GET(x)`
  store   `x = v` into a slot: `P32_SET(x, v)`
  add     `x += n`, `x -= n`: `P32_ADD(x, n)`, `P32_SUB(x, n)`
  step    `++x`, `--x`, `x++`, `x--`: `P32_PREINC(x)` ... `P32_POSTDEC(x)`

Casts (--casts; O3), on an offset-model build (aarch64, or LP64_P32_OFFSET=1), where a slot is a union: a cast from
a pointer to a 32-bit integer, directly or through uintptr_t, gets P32_ENC on its operand; one from a 32-bit integer
to a pointer, P32_DEC. Then a textual pass wraps `(int)P32_GET(x)` (clang names the outer accessor's place for one
nested in another's argument) as `(int)P32_ENC(P32_GET(x))`.

Where a site is written: in ordinary text (a macro's argument too), it is rewritten there; a strict part of a
#define's body is rewritten in the body, once; the whole of a macro, or a site that ends in one (`x = NULL`), is
wrapped where the macro is used. Anything else (a token paste, an operator inside a macro) is listed (--list), to be
done by hand. Text is only inserted or replaced within a line, so no line moves: on the 32-bit and x86-64 builds the
accessors expand to the text that was there (`(x)`, `((x) = (v))`), and only debug-info columns change.

Without --apply the tool reports what it would do. With --apply it edits the files under each --src in place and
writes the edits to files under --decomp as patches (--patch-dir, `<stem>-N.patch`, a/ b/ paths, whole files in
parts under --part-size bytes; against the --decomp tree, which should have every patch folder before this one).
Files elsewhere (system headers, the build's generated files, the accessors' own header) are never edited.

  --src DIR            the port's own sources, edited in place (default: ROOT/src)
  --root DIR           where paths are reported from (default: the current directory)
  --decomp DIR         the patched decomp tree (BUILD/patched/<decomp>, say), whose edits become patches
  --patch-dir DIR      where those patches go; --patch-stem (default 0001-p32-accessors, 0002-p32-casts with --casts)
  --header NAME        the accessors' header, never a site (default: lp64_p32.h and p32.h; repeatable)
  --host-side PATH     with --casts: files that deal in the host's own addresses, left alone (the host's allocator,
                       the arena's set-up; a path suffix; repeatable)
  --skip REGEX         compile_commands entries to leave out (default: /gen/, the build's generated text)
  --overlay-preprocess REGEX   a game's units built from preprocessed text: a regex over build.ninja whose group 1 is
                       the preprocess step's compiler arguments ending in the source (spyro-vita's overlays:
                       `port_overlays\\.py preprocess .*? -- (.*?\\.c)(?:\\s|$)`); each source is parsed once, or once
                       per argument set with --all-overlays

From spyro-vita tools/ps4/p32_offset_rewrite.py @ 36d9e07 (MIT; D258, D262). Its fixed paths (patched/spyro-1,
src/, src/compat/p32.h, the overlays' rule, the host-side files) are arguments here.
"""
import argparse, collections, difflib, json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.getcwd()
MODE = ["slots"]  # or "casts" (--casts), or "decls" (p32_decl_check.py)
CASTS = ("CStyleCastExpr", "ImplicitCastExpr")
HEADERS = ["lp64_p32.h", "p32.h"]  # the accessors' header(s): --header
HOST_SIDE = ()  # --casts leaves these alone: --host-side
SKIP = [re.compile(r"/gen/")]  # --skip
OVERLAY_RE = [None]  # --overlay-preprocess


def is_header(f):
    """f is the accessors' own header"""
    return bool(f) and os.path.basename(f) in HEADERS
SMALL = {"int", "unsigned int", "short", "unsigned short", "char", "signed char", "unsigned char",
         "_Bool"}


def small_int(t):
    """a 32-bit or narrower integer type (LP64: long and its kin are 64)"""
    t = re.sub(r"\b(const|volatile)\b", "", t or "").strip()
    return t.startswith("enum ") or t in SMALL


def ty_of(d):
    t = d.get("type")
    if not isinstance(t, dict):
        return ""
    return t.get("desugaredQualType") or t.get("qualType", "")


def is_slot(ty):
    """A slot's type is printed with its outermost pointer's qualifiers last:
    'Moby * __ptr32 __uptr'. A pointer to a slot ('Moby * __ptr32 __uptr *')
    is a normal pointer. A slot's desugared type (one behind a typedef,
    `typedef P32F(Moby *) MobySlot;`) has them first: '__uptr __ptr32 Moby *'."""
    ty = ty.strip()
    return ty.endswith(("__ptr32 __uptr", "__uptr __ptr32", "__ptr32")) or \
        ty.startswith(("__uptr __ptr32 ", "__ptr32 __uptr ", "__ptr32 "))


class Unit:
    def __init__(self, d, src, cmd):
        self.d, self.src, self.cmd = d, src, cmd


def units(build, all_overlays=False):
    """the units of compile_commands.json (but --skip's), and with --overlay-preprocess the sources of
    preprocessed units; all_overlays: each such source once per argument set (a game's per-overlay branches)"""
    out, seen = [], set()
    with open(os.path.join(build, "compile_commands.json")) as f:
        cc = json.load(f)
    for e in cc:
        fn = e["file"]
        if any(r.search(fn) for r in SKIP) or not fn.endswith((".c", ".cpp")):
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
    # preprocessed units: their own sources, once each, with the preprocess step's flags
    pre = OVERLAY_RE[0]
    ninja = os.path.join(build, "build.ninja")
    if pre is None or not os.path.exists(ninja):
        return out
    done = set()
    with open(ninja) as f:
        for line in f:
            m = pre.search(line)
            if not m:
                continue
            args = shlex.split(m.group(1))
            src = args[-1]
            key = (src, tuple(args)) if all_overlays else src
            if key in done:
                continue
            done.add(key)
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
            "desugaredQualType", "isMacroArgExpansion", "name", "storageClass"}
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
                        parent["_child"] = (d["kind"], norm(r["begin"]), norm(r["end"]), d.get("_child"),
                                            d.get("castKind"), ty_of(d))
        return d

    def cast_site(d):
        """--casts: a cast between a pointer and a 32-bit integer; the site
        is the operand that P32_ENC or P32_DEC wraps"""
        ck, c = d.get("castKind"), d.get("_child")
        if d["kind"] not in CASTS or c is None:
            return None
        sp = norm(d["range"]["begin"])[0] if isinstance(d.get("range"), dict) and "begin" in d["range"] else None
        if isinstance(sp, tuple) and sp[0] and is_header(sp[0]):
            return None  # P32_ENC/P32_DEC's own casts
        if ck == "PointerToIntegral" and small_int(ty_of(d)):
            return ("enc", c[1], c[2], c[3])  # (int)p
        if (ck == "IntegralCast" and small_int(ty_of(d)) and c[0] in CASTS and c[4] == "PointerToIntegral"
                and c[3] is not None):
            g = c[3]
            return ("enc", g[1], g[2], g[3])  # (u32)(uintptr_t)p
        if ck == "IntegralToPointer":
            if small_int(c[5]):
                return ("dec", c[1], c[2], c[3])  # (T *)i
            if c[0] in CASTS and c[4] == "IntegralCast" and c[3] is not None and small_int(c[3][5]):
                return ("dec", c[1], c[2], c[3])  # (T *)(uintptr_t)i
        return None

    def node(d):
        kind = d["kind"]
        if MODE[0] == "decls":
            # p32_decl_check.py: a variable declared at file scope
            if kind == "VarDecl" and d.get("name") and not any(
                    x[1] is not None and x[1].get("kind") in ("FunctionDecl", "BlockDecl") for x in stack):
                b = norm(d["range"]["begin"])[1] if isinstance(d.get("range"), dict) and "begin" in d["range"] else None
                sites.append(("decl", d["name"], ty_of(d), d.get("storageClass", ""), b))
            elif kind == "FunctionDecl" and d.get("name"):
                b = norm(d["range"]["begin"])[1] if isinstance(d.get("range"), dict) and "begin" in d["range"] else None
                sites.append(("func", d["name"], ty_of(d), d.get("storageClass", ""), b))
            return
        if MODE[0] == "casts":
            site = cast_site(d)
            if site:
                sites.append(site)
            return
        t = d.get("type") if isinstance(d.get("type"), dict) else {}
        # a slot behind a typedef (`typedef P32F(Moby *) MobySlot;`) shows as its name: its desugared type says
        ty = t.get("qualType", "") if is_slot(t.get("qualType", "")) else t.get("desugaredQualType", "")
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


WRAP = {"load": "P32_GET(", "enc": "P32_ENC(", "dec": "P32_DEC("}
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


def whole(loc, sources, start):
    """--casts: a "use" placement wraps a whole macro use, so the operand must
    begin (start) or end there too: in ordinary text, in one of p32.h's
    accessors, or at the start or end of a #define's body"""
    if not isinstance(loc, tuple) or not loc[0]:
        return False
    if is_header(loc[0]):
        return True
    src = sources.get(loc[0])
    if src is None:
        return False
    d = src.define_at(loc[1])
    if d is None:
        return True
    if start:
        return src.skip_ws(d[2]) == loc[1]
    return src.text[loc[1] + loc[2]:d[1]].strip(" \t\r\n\\") == ""


def macro_body(name, sources):
    """a #define's body, whitespace and continuations folded, from any of our files (None if not found)"""
    for src in sources.values():
        for d in src.defines:
            m = re.match(r"[ \t]*#[ \t]*define[ \t]+(\w+)", src.text[d[0]:])
            if m and m.group(1) == name:
                return re.sub(r"\\\n|\s+", " ", src.text[d[2]:d[1]]).strip()
    return None


def whole_use(b, e, where, sources):
    """a "use" placement wraps the whole macro use, so the site must be the whole of the macro's expansion: it begins
    and ends where the body does (whole()), or it is a token paste whose body is one pasted name (`g_##n`), whose
    spelling clang puts in <scratch space>. A site that is a strict part of a macro's expansion (`(g_##n->m_X)`'s
    g_n) is not, and is listed instead (spyro-vita's rewriter checked this for casts only)."""
    for loc, start in ((b[0], True), (e[0], False)):
        if isinstance(loc, tuple) and loc[0] == "<scratch space>":
            m = re.match(r"\w+", sources[where[0]].text[where[1]:])
            body = macro_body(m.group(0), sources) if m else None
            if body is None or not re.fullmatch(r"\w*(?:\s*##\s*\w+)+", body):
                return False
        elif not whole(loc, sources, start):
            return False
    return True


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
        if where is not None and where[3] == "use" and not whole_use(b, e, where, sources):
            where, why = None, "token paste" if "<scratch space>" in (b[0][0], e[0][0]) else "part of a macro"
        if (where is not None and MODE[0] == "casts" and where[3] == "use" and isinstance(b[0], tuple)
                and b[0][0] and is_header(b[0][0])
                and not sources[where[0]].text.startswith("P32_GET(", where[1])):
            # an accessor nested in another's argument: clang names the outer
            # one's place; the textual pass (cast_text) takes `(int)P32_GET(`
            where, why = None, "nested accessor"
        if where is None:
            listed.append((k, b, e, why))
            continue
        fb, ob, oe, h = where
        if (k, fb, ob, oe) in done:
            continue
        done.add((k, fb, ob, oe))
        s = sources[fb]
        span = oe - ob
        if k in WRAP:
            edits[fb] += [(ob, ob, (1, -span, k), WRAP[k]), (oe, oe, (0, span, k), ")")]
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


CAST_GET = re.compile(r"\((?:int|u_int|u32|s32|unsigned int|unsigned|uint32_t|int32_t|short|u_short|u16|s16)\)"
                      r"\s*(?:\(uintptr_t\)\s*)?(?=P32_GET\()")


def cast_text(text):
    """--casts: `(int)P32_GET(x)` is always a pointer cut to 32 bits; wrap the
    accessor: `(int)P32_ENC(P32_GET(x))`. Only text is inserted, so no line
    moves."""
    out, pos, n = [], 0, 0
    for m in CAST_GET.finditer(text):
        o = m.end()
        depth, e = 0, text.index("(", o)  # P32_GET's own paren
        while e < len(text):
            c = text[e]
            depth += c == "("
            depth -= c == ")"
            e += 1
            if depth == 0:
                break
        nxt = text[e:e + 2].lstrip()
        if nxt[:1] in (".", "[", "(") or text[e:].lstrip().startswith("->"):
            continue  # the cast is on a member or element of it, not the pointer
        out += [text[pos:o], "P32_ENC(", text[o:e], ")"]
        pos = e
        n += 1
    out.append(text[pos:])
    return "".join(out), n


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
    global ROOT, HOST_SIDE
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("build")
    ap.add_argument("--root", default=os.getcwd())
    ap.add_argument("--src", action="append", default=[])
    ap.add_argument("--decomp")
    ap.add_argument("--patch-dir")
    ap.add_argument("--patch-stem")
    ap.add_argument("--part-size", type=int, default=500000)
    ap.add_argument("--header", action="append", default=[])
    ap.add_argument("--host-side", action="append", default=[])
    ap.add_argument("--skip", action="append", default=[])
    ap.add_argument("--overlay-preprocess")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--list", help="every site, and why a listed one isn't rewritten")
    ap.add_argument("--casts", action="store_true",
                    help="O3: the casts between pointers and 32-bit integers, on a linuxa64 BUILD")
    ap.add_argument("--all-overlays", action="store_true",
                    help="parse each overlay source once per overlay (every IS_<ovl> branch)")
    a = ap.parse_args()
    if a.casts:
        MODE[0] = "casts"
    ROOT = os.path.realpath(a.root)
    build = os.path.realpath(a.build)
    srcs = [os.path.realpath(d) for d in (a.src or [os.path.join(ROOT, "src")])]
    s1 = os.path.realpath(a.decomp) if a.decomp else None
    if a.header:
        HEADERS[:] = a.header
    HOST_SIDE = tuple(a.host_side)
    if a.skip:
        SKIP[:] = [re.compile(x) for x in a.skip]
    if a.overlay_preprocess:
        OVERLAY_RE[0] = re.compile(a.overlay_preprocess)
    if a.apply and s1 and not a.patch_dir:
        ap.error("--decomp with --apply needs --patch-dir")
    us = units(build, a.all_overlays)
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
        f = os.path.realpath(f)
        if MODE[0] == "casts" and HOST_SIDE and f.endswith(HOST_SIDE):
            return False  # the host's own memory: never offsets
        if is_header(f):
            return False
        if s1 and f.startswith(s1 + "/"):
            return True  # the decomp's tree may lie inside BUILD (BUILD/patched/<decomp>)
        return not f.startswith(build + "/") and any(f.startswith(d + "/") for d in srcs)

    files = {loc[0] for _, b, e, _ in allsites for x in (b, e) for loc in x[:2]
             if isinstance(loc, tuple) and loc[0] and mine(loc[0])}
    sources = {f: Source(f) for f in files}
    # a site counts when any of its locations is in our text
    sites = [x for x in allsites if any(isinstance(l, tuple) and l[0] in sources for y in x[1:3] for l in y[:2])]
    edits, listed, how = plan(sites, sources)

    def org(x):
        f = x[1][1][0] if isinstance(x[1][1], tuple) else ""
        return "decomp" if f and s1 and os.path.realpath(f).startswith(s1 + "/") else "src"
    by = collections.Counter((org(x), x[0]) for x in sites)
    kinds = (["enc", "dec"] if a.casts else
             ["load", "store", "add", "sub", "preinc", "predec", "postinc", "postdec"])
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
    todo = set(edits)
    if a.casts:
        # the textual pass: every file of ours with `(int)P32_GET(`
        for top in [t for t in [s1] + srcs if t]:
            for d, _, fs in os.walk(top):
                for f in fs:
                    fn = os.path.join(d, f)
                    if f.endswith((".c", ".h")) and mine(fn):
                        try:
                            t = open(fn, encoding="latin-1").read()
                        except OSError:
                            continue
                        if CAST_GET.search(t):
                            todo.add(fn)
                            sources.setdefault(fn, Source(fn))
    ntext = 0
    for fn in sorted(todo):
        s = sources[fn]
        new = apply(s.text, edits.get(fn, []))
        if a.casts:
            new, k = cast_text(new)
            ntext += k
        assert new.count("\n") == s.text.count("\n"), fn
        if not a.apply:
            continue
        if s1 and os.path.realpath(fn).startswith(s1 + "/"):
            rel = os.path.relpath(os.path.realpath(fn), s1)
            patch[rel] = list(difflib.unified_diff(s.text.splitlines(True), new.splitlines(True), "a/" + rel, "b/" + rel))
        else:
            with open(fn, "w", encoding="latin-1") as f:
                f.write(new)
    if a.casts:
        print(f"textual pass: {ntext} `(int)P32_GET(` casts wrapped")
    if a.apply and patch:
        # whole files packed into parts under --part-size (a game's no-assets check may cap a file's size: spyro-vita's
        # is 512 KiB, and its moby_update.c's patch alone is ~490 KB)
        d = a.patch_dir
        stem = a.patch_stem or ("0002-p32-casts" if a.casts else "0001-p32-accessors")
        os.makedirs(d, exist_ok=True)
        for old_part in os.listdir(d):
            if old_part.startswith(stem):
                os.remove(os.path.join(d, old_part))
        parts, cur, size = [], [], 0
        for rel, lines in sorted(patch.items(), key=lambda kv: -sum(map(len, kv[1]))):
            n = sum(map(len, lines))
            if cur and size + n > a.part_size:
                parts.append(cur)
                cur, size = [], 0
            cur.append(rel)
            size += n
        if cur:
            parts.append(cur)
        for i, files in enumerate(parts, 1):
            with open(os.path.join(d, f"{stem}-{i}.patch"), "w", encoding="latin-1") as f:
                for rel in sorted(files):
                    f.writelines(patch[rel])
        print(f"patches: {len(parts)} in {d}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""PAL's 50 Hz retune, site by site (regions plan §9 R3, D135).

Aligns each NTSC-U function with its counterpart in another release and lists
the constants that differ, then maps those constants to the decomp's source
tokens by recompiling, and writes the RT(us, pal) edits.

  sites   align and list.  For every function of the US matching build's ELF
          (exe: .main; wad: every level overlay, the title's and the credits'):
          find the release's counterpart (compare_code's matcher), align the
          two with relocations and constants masked, and inside the aligned
          blocks compare once more with only relocations masked ("strict",
          below). Each differing immediate is a site: US address, release
          address, opcode, both values and its ratio class (x1.2, x1.44, x5/6,
          other). The instructions difflib couldn't pair are listed as ranges
          (the "inserted sequences": not constants).
  probe   write a copy of the decomp's sources (after the patches) in which
          every integer literal whose value is a site's US value (or one off,
          or negated) inside a function body is replaced by a sentinel,
          0x1000 + 3*id, numbered per source file (a function's tokens are
          read back through the file that defines it). Build that copy (tools/build_matching.sh PROBE=DIR).
  map     align the probe build with the US build function by function and
          read back, for each source token, every instruction that holds its
          sentinel: the token's US instructions, and through `sites` whether
          each is a site and what the release has there.
  apply   rewrite the tokens whose every instruction agrees (all changed by
          the same rule) as RT(us, pal), in a source tree; list the others.

Strict comparison: only immediates that are (or may be) addresses are masked:
j/jal targets, lui (unless its register's next use is an ori: a 32-bit
constant), and addiu/ori/loads/stores whose base register holds an address
part ($gp, or a register last written by lui or by addu from such a register).
So addiu v0,v0,192 (a speed added) is compared, which compare_code's --exact
doesn't do.  `check` reports, per function, "same" in this strict sense.

It prints names, addresses, sizes and numbers only: no game data.
"""
import argparse
import difflib
import json
import os
import re
import struct
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_code as cc  # noqa: E402

GP, SP, AT = 28, 29, 1
LOADSTORE = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x32, 0x3A}
CONST_OPS = {0x08: "addi", 0x09: "addiu", 0x0A: "slti", 0x0B: "sltiu", 0x0C: "andi",
             0x0D: "ori", 0x0E: "xori", 0x0F: "lui"}
SENT_BASE, SENT_STEP, SENT_MAX = 0x1000, 3, 0x7FF0


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


def imm_of(w):
    op = w >> 26
    return (w & 0xFFFF) if op in (0x0C, 0x0D, 0x0E, 0x0F) else s16(w & 0xFFFF)


def strict(ws):
    """Mask only what may be relocated (see the module doc). Stream-wise."""
    out = []
    hi = {GP}
    n = len(ws)
    for i, w in enumerate(ws):
        op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        rd = (w >> 11) & 31
        m = w
        dst = None
        if op in (2, 3):
            m = w & 0xFC000000
        elif op == 0x0F:  # lui rt
            # a 32-bit constant if rt's next use is ori rt, rt
            const = False
            for j in range(i + 1, min(n, i + 6)):
                v = ws[j]
                if v >> 26 == 0x0D and (v >> 21) & 31 == rt and (v >> 16) & 31 == rt:
                    const = True
                    break
                if ((v >> 21) & 31) == rt or ((v >> 16) & 31) == rt:
                    break
            if not const:
                m = w & 0xFFFF0000
            dst = (rt, not const)
        elif op in (0x09, 0x0D) or op in LOADSTORE:
            if rs in hi:
                m = w & 0xFFFF0000
            if op in (0x09, 0x0D):
                dst = (rt, False)
            elif op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x22, 0x26):
                dst = (rt, False)
        elif op == 0 and (w & 63) in (0x21, 0x20):  # addu/add rd
            dst = (rd, rs in hi or rt in hi)
        elif op == 0:
            if (w & 63) not in (0x08, 0x09, 0x0C, 0x0D, 0x18, 0x19, 0x1A, 0x1B, 0x11, 0x13):
                dst = (rd, False)
        elif op in (0x08, 0x0A, 0x0B, 0x0C, 0x0E):
            dst = (rt, False)
        if dst:
            r, h = dst
            if r != GP and r != 0:
                (hi.add if h else hi.discard)(r)
        out.append(m)
    return out


def ratio_class(a, b):
    if a == 0 or b == 0:
        return "other"
    r = Fraction(b, a)
    for name, f, tol in (("x1.2", Fraction(6, 5), 0.02), ("x1.44", Fraction(36, 25), 0.02),
                         ("x5/6", Fraction(5, 6), 0.03), ("x0.75", Fraction(3, 4), 0.0)):
        if abs(float(r) - float(f)) <= tol * float(f) + (1.0 / abs(a) if abs(a) < 64 else 0):
            return name
    return "other"


def counterpart_spans(res, tlen):
    """compare_code's gap rule: (lo, hi) word span in the target per function."""
    out = []
    for i, r in enumerate(res):
        prev = next((res[j] for j in range(i - 1, -1, -1) if len(res[j]["hits"]) == 1), None)
        nxt = next((res[j] for j in range(i + 1, len(res)) if len(res[j]["hits"]) == 1), None)
        lo = prev["hits"][0] + prev["size"] // 4 if prev else 0
        hi = nxt["hits"][0] if nxt else tlen
        if r["hits"]:
            # one hit, or the duplicate inside the neighbours' gap
            want = lo + ((r["addr"] - prev["addr"] - prev["size"]) // 4 if prev else 0)
            h = min(r["hits"], key=lambda h: abs(h - want))
            out.append((h, h + r["size"] // 4))
            continue
        out.append((lo, hi) if 0 < hi - lo <= 40000 else None)
    return out


def align_fn(U, T, u_addr, t_addr):
    """Sites and unpaired ranges between US words U (at u_addr) and target T."""
    cu = [cc.norm(w) for w in U]
    ct = [cc.norm(w) for w in T]
    su, st = strict(U), strict(T)
    sm = difflib.SequenceMatcher(None, cu, ct, autojunk=False)
    sites, ranges = [], []
    ops = sm.get_opcodes()
    # the gap may hold padding or a neighbour's code before and after
    while ops and ops[0][0] == "insert":
        ops = ops[1:]
    while ops and ops[-1][0] == "insert":
        ops = ops[:-1]
    for tag, i1, i2, j1, j2 in ops:
        if tag == "equal":
            for k in range(i2 - i1):
                a, b = su[i1 + k], st[j1 + k]
                if a == b:
                    continue
                w, v = U[i1 + k], T[j1 + k]
                op = w >> 26
                kind = CONST_OPS.get(op, "load/store" if op in LOADSTORE else
                                     "branch" if op in (1, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) else "op%02x" % op)
                ua, pa = imm_of(w), imm_of(v)
                sites.append({"us": u_addr + 4 * (i1 + k), "pal": t_addr + 4 * (j1 + k), "op": kind,
                              "rs": (w >> 21) & 31, "a": ua, "b": pa,
                              "cls": ratio_class(ua, pa) if kind in CONST_OPS.values() else kind})
        else:
            ranges.append({"tag": tag, "us": [u_addr + 4 * i1, u_addr + 4 * i2],
                           "pal": [t_addr + 4 * j1, t_addr + 4 * j2], "nu": i2 - i1, "nt": j2 - j1})
    return sites, ranges


def load_units(elf, kind, target, region):
    """[(section, us_addr, us_bytes, funcs, target_bytes, target_addr)]"""
    d, secs, names, funcs = cc.elf_sections_and_funcs(elf)

    def sec(name):
        i = [k for k, v in names.items() if v == name][0]
        return secs[i][3], d[secs[i][4]: secs[i][4] + secs[i][5]]

    units = []
    if kind == "exe":
        a, b = sec(".main")
        units.append((".main", a, b, funcs[".main"], open(target, "rb").read()[0x800:], cc.EXE_BASE))
    else:
        lv, cr, ti = cc.WAD_LAYOUT[region]
        base = {"us": 0x8007AA38, "pal": 0x80081428}.get(region, 0)
        f = open(target, "rb")
        hdr = f.read(0x800)
        jobs = [(n, lv + 2 * k) for k, n in enumerate(cc.LEVELS)] + [("credits", cr), ("titlescreen_code", ti)]
        for n, i in jobs:
            off, ln = struct.unpack_from("<II", hdr, i * 8)
            f.seek(off)
            a, b = sec("." + n)
            units.append(("." + n, a, b, funcs["." + n], f.read(ln), base))
    return units


def cmd_sites(a):
    units = load_units(a.elf, a.kind, a.target, a.region)
    out = {}
    tot = {"same": 0, "fns": 0, "sites": 0, "unpaired_us": 0, "unpaired_t": 0}
    for sname, ua, ub, fns, tb, tbase in units:
        T = cc.words(tb)
        res = cc.compare(ub, ua, fns, tb)
        spans = counterpart_spans(res, len(T))
        rows = []
        for r, sp in zip(res, spans):
            U = cc.words(ub[r["addr"] - ua: r["addr"] - ua + r["size"]])
            tot["fns"] += 1
            if sp is None:
                rows.append({"name": r["name"], "addr": r["addr"], "size": r["size"], "unmeasured": True})
                continue
            lo, hi = sp
            if len(r["hits"]) > 1:  # a duplicate: prefer one that is strictly the same
                su = strict(U)
                for h in sorted(r["hits"], key=lambda h: abs(h - lo)):
                    if strict(T[h:h + len(U)]) == su:
                        lo, hi = h, h + len(U)
                        break
            sites, ranges = align_fn(U, T[lo:hi], r["addr"], tbase + 4 * lo)
            same = not sites and not ranges
            tot["same"] += same
            tot["sites"] += len(sites)
            tot["unpaired_us"] += sum(x["nu"] for x in ranges)
            tot["unpaired_t"] += sum(x["nt"] for x in ranges)
            rows.append({"name": r["name"], "addr": r["addr"], "size": r["size"], "t_addr": tbase + 4 * lo,
                         "same": same, "sites": sites, "ranges": ranges})
        out[sname] = rows
    print(f"{a.kind}: {tot['fns']} functions, strictly same {tot['same']}; {tot['sites']} constant sites; "
          f"unpaired instructions US {tot['unpaired_us']}, {a.region} {tot['unpaired_t']}")
    if a.list:
        for sname, rows in out.items():
            for r in rows:
                if r.get("unmeasured"):
                    print(f"  {sname} {r['name']} not measured")
                elif not r["same"]:
                    cls = {}
                    for s in r["sites"]:
                        cls[s["cls"]] = cls.get(s["cls"], 0) + 1
                    print(f"  {sname} {r['name']} {r['addr']:08x}: {len(r['sites'])} sites "
                          + " ".join(f"{k}={v}" for k, v in sorted(cls.items()))
                          + f"; unpaired {sum(x['nu'] for x in r['ranges'])}/{sum(x['nt'] for x in r['ranges'])}")
    if a.only:
        want = set(a.only.split(","))
        for sname, rows in out.items():
            for r in rows:
                if r["name"] in want and not r.get("unmeasured"):
                    print(f"{sname} {r['name']} {r['addr']:08x} -> {r['t_addr']:08x}")
                    for s in r["sites"]:
                        print(f"    {s['us']:08x} {s['pal']:08x} {s['op']:6} {s['a']} -> {s['b']} ({s['cls']})")
                    for x in r["ranges"]:
                        print(f"    {x['tag']} US {x['us'][0]:08x}+{x['nu']} {a.region} {x['pal'][0]:08x}+{x['nt']}")
    if a.json:
        json.dump({"kind": a.kind, "region": a.region, "units": out}, open(a.json, "w"))


# ---- source mapping: the compiler's own line table ------------------------

LIT = re.compile(r"(?<![\w.])(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)(?![\w.])")


def body_mask(text):
    """Per character: True inside a function body (brace depth >= 1 of a {
    whose previous significant char at depth 0 is ')'), outside comments,
    strings and preprocessor lines."""
    n = len(text)
    inbody = bytearray(n)
    fns = []  # (start, end, name)
    depth, fdepth = 0, None
    i, last_sig = 0, ""
    bol = True
    hdr_from = 0
    while i < n:
        c = text[i]
        if bol and c == "#":
            while i < n and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n:
                    i += 1
                i += 1
            continue
        if c == "\n":
            bol = True
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        bol = False
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            last_sig = c
            continue
        if c == "{":
            if depth == 0 and last_sig == ")":
                fdepth = 1
                hdr = text[hdr_from:i]
                m = re.search(r"NAME_OVERLAY_FUNCTION\((\w+)\)", hdr) or re.search(r"(\w+)\s*\(", hdr)
                fns.append([i, None, m.group(1) if m else "?"])
            depth += 1
        elif c == "}":
            depth -= 1
            if fdepth is not None and depth < fdepth:
                fdepth = None
                fns[-1][1] = i
            if depth == 0:
                hdr_from = i + 1
        elif c == ";" and depth == 0:
            hdr_from = i + 1
        if fdepth is not None and depth >= fdepth:
            inbody[i] = 1
        last_sig = c
        i += 1
    return inbody, fns


def literal_tokens(text):
    """[(start, end, value, suffix)] for literals in function bodies we may touch."""
    mask, fns = body_mask(text)
    toks = []
    for m in LIT.finditer(text):
        s = m.start()
        if not mask[s]:
            continue
        before = text[max(0, s - 12):s].rstrip()
        if before.endswith(("case", "...")) or before.endswith("[") or before.endswith("<<") or before.endswith(">>"):
            continue
        # skip a literal directly followed by ']' (an index) or ':' (a bit-field/label)
        after = text[m.end():m.end() + 3].lstrip()
        if after.startswith("]"):
            continue
        if re.search(r"RT\([^()]*$", text[max(0, s - 40):s]):  # already a region site
            continue
        tok = m.group(1)
        v = int(tok, 16) if tok[:2] in ("0x", "0X") else int(tok, 10) if not (len(tok) > 1 and tok[0] == "0") else int(tok, 8)
        fn = next((f[2] for f in fns if f[0] <= s <= (f[1] or len(text))), "?")
        toks.append((s, m.end(), v, m.group(2), fn))
    return toks



FN_HDR = re.compile(r"^([0-9a-f]+) <(\S+)>:$")
LINE = re.compile(r"^/s1/(\S+):(\d+)")
INSN = re.compile(r"^\s*([0-9a-f]+):\t")


def cmd_lines(a):
    """address -> source line, from `objdump -dl` of the build's objects
    (the decomp compiles with -g3; tools/regions/objlines.sh writes them)."""
    fns = {}
    for root, _, files in os.walk(a.dis):
        for f in files:
            if not f.endswith(".dis"):
                continue
            cur, start, src = None, 0, None
            for l in open(os.path.join(root, f)):
                m = FN_HDR.match(l)
                if m:
                    cur, start, src = m.group(2), int(m.group(1), 16), None
                    fns[cur] = {}
                    continue
                m = LINE.match(l)
                if m:
                    src = (m.group(1), int(m.group(2)))
                    continue
                m = INSN.match(l)
                if m and cur and src:
                    fns[cur][int(m.group(1), 16) - start] = src
    d, secs, names, funcs = cc.elf_sections_and_funcs(a.elf)
    out = {}
    for sname, fl in funcs.items():
        for v, sz, nm in fl:
            if nm in fns:
                for off, src in fns[nm].items():
                    out.setdefault(sname, {})["%x" % (v + off)] = src
    json.dump(out, open(a.out, "w"))
    print(f"lines: {sum(len(x) for x in out.values())} instructions in {len(out)} sections")


def transforms(v):
    """(immediate, inverse) pairs: how a source literal v can show in an
    instruction (x < v: slti v; x <= v: slti v+1; x -= v: addiu -v; ...)."""
    return [(v, lambda p: p), (-v, lambda p: -p), (v + 1, lambda p: p - 1),
            (-(v + 1), lambda p: -p - 1), (v - 1, lambda p: p + 1), (-(v - 1), lambda p: 1 - p)]


def cmd_propose(a):
    sites = [json.load(open(p)) for p in a.sites]
    lines = json.load(open(a.lines))
    scope = set(a.functions.split(",")) if a.functions else None
    sec_ok = (lambda s: True) if not a.sections else (lambda s, S=set(a.sections.split(",")): s in S)
    d, secs, names, funcs = cc.elf_sections_and_funcs(a.elf)

    def base_name(fn):
        return fn.rsplit("_", 2)[0] if "_LEVEL_" in fn else fn

    # every constant-class instruction by source line, and its state
    site_at, unpaired = {}, {}
    for doc in sites:
        for sname, rows in doc["units"].items():
            for r in rows:
                if r.get("unmeasured"):
                    unpaired[(sname, r["name"])] = None
                    continue
                unpaired[(sname, r["name"])] = [(x["us"][0], x["us"][1]) for x in r["ranges"]]
                for s_ in r["sites"]:
                    site_at[(sname, s_["us"])] = s_
    by_line = {}
    for sname in lines:
        if not sec_ok(sname):
            continue
        i = [k for k, v in names.items() if v == sname][0]
        base, off = secs[i][3], secs[i][4]
        for v, sz, fn in funcs[sname]:
            if scope and base_name(fn) not in scope:
                continue
            bad = unpaired.get((sname, fn), [])
            for k, w in enumerate(cc.words(d[off + v - base: off + v - base + sz])):
                addr = v + 4 * k
                src = lines[sname].get("%x" % addr)
                op = w >> 26
                if not src or op not in CONST_OPS or op == 0x0F:
                    continue
                if (op in (0x09, 0x0D) or op in LOADSTORE) and (w >> 21) & 31 not in (0,):
                    # addiu/ori from a register: an addend; keep unless strict() masks it
                    if strict([w])[0] != w:
                        continue
                st = site_at.get((sname, addr))
                state = "site" if st else ("unaligned" if bad is None else
                                           "unpaired" if any(lo <= addr < hi for lo, hi in bad) else "same")
                by_line.setdefault((src[0], src[1]), []).append(
                    {"sec": sname, "fn": fn, "us": addr, "imm": imm_of(w), "state": state,
                     "pal": st["b"] if st else None})
    # tokens per line
    files = sorted({f for f, _ in by_line})
    toks = {}
    for rel in files:
        path = os.path.join(a.tree, rel)
        if not os.path.exists(path):
            continue
        text = open(path).read()
        src_lines = text.split("\n")
        for s_, e, v, suf, fn in literal_tokens(text):
            ln = text.count("\n", 0, s_) + 1
            col = s_ - (text.rfind("\n", 0, s_) + 1)
            toks.setdefault((rel, ln), []).append({"file": rel, "line": ln, "start": s_, "end": e,
                                                   "col": col, "line_text": src_lines[ln - 1],
                                                   "value": v, "text": text[s_:e], "fn": fn})
    edits, report = [], []
    cands_all, claim = {}, {}
    n = {"edit": 0, "keep": 0, "conflict": 0, "unmapped": 0}
    for key, ins in sorted(by_line.items()):
        if not any(x["state"] == "site" for x in ins):
            continue
        cands = toks.get(key, [])
        # attach each instruction to the tokens it can come from: its own
        # line's, else the nearest line's within two (a constant the compiler
        # put on the condition's line, a call's arguments on the next lines)
        for x in ins:
            hits = []
            for dl in (0, 1, -1, 2, -2):
                for t in toks.get((key[0], key[1] + dl), []) if dl else cands:
                    if t["fn"] != (cands[0]["fn"] if cands else t["fn"]):
                        continue
                    for imm, inv in transforms(t["value"]):
                        if imm == x["imm"]:
                            hits.append((t["start"], inv, t))
                            break
                if hits:
                    break
            if len(hits) == 1:
                k_ = (key[0], hits[0][0])
                cands_all.setdefault(k_, hits[0][2])
                claim.setdefault(k_, []).append((x, hits[0][1]))
            elif x["state"] == "site":
                n["unmapped"] += 1
                report.append(f"{key[0]}:{key[1]} {x['sec']} {x['fn']} {x['us']:08x} {x['imm']}->{x['pal']}: "
                              + ("no literal on the line" if not hits else f"{len(hits)} literals could be it"))
    # one decision a literal, over every instruction any line gave it
    if True:
        for start, xs in sorted(claim.items()):
            t = cands_all[start]
            states = {x["state"] for x, _ in xs}
            if "site" not in states:
                continue
            pals = {inv(x["pal"] if x["state"] == "site" else x["imm"]) for x, inv in xs
                    if x["state"] in ("site", "same")}
            if states - {"site", "same"} or len(pals) != 1:
                n["conflict"] += 1
                report.append(f"{t['file']}:{t['line']} {t['text']}: conflict "
                              + "; ".join(f"{x['sec']}:{x['fn']}@{x['us']:08x} {x['state']} {x['imm']}->{x['pal']}"
                                          for x, _ in xs[:10]))
                continue
            (p,) = pals
            if p == t["value"]:
                n["keep"] += 1
                continue
            n["edit"] += 1
            edits.append({"file": t["file"], "line": t["line"], "col": t["col"], "line_text": t["line_text"],
                          "text": t["text"], "pal": p, "fn": t["fn"], "n": len(xs),
                          "secs": sorted({x["sec"] for x, _ in xs})})
    json.dump(edits, open(a.out, "w"), indent=0)
    open(a.report, "w").write("\n".join(report) + "\n")
    print("propose: " + ", ".join(f"{k} {v}" for k, v in n.items()) + f" ({len(edits)} tokens to edit)")


def cmd_apply(a):
    """Write the edits into a tree: each literal found by its line's text
    (the nearest line with the same text: the tree may have lines added
    above it) and its column."""
    edits = json.load(open(a.edits))
    byfile = {}
    for e in edits:
        byfile.setdefault(e["file"], []).append(e)
    for rel, es in byfile.items():
        path = os.path.join(a.tree, rel)
        lines = open(path).read().split("\n")
        done = {}
        for e in es:
            cand = [i for i, l in enumerate(lines) if l == e["line_text"]]
            if not cand:
                raise SystemExit(f"{rel}:{e['line']}: line not found: {e['line_text']!r}")
            i = min(cand, key=lambda i: abs(i + 1 - e["line"]))
            done.setdefault(i, []).append(e)
        for i, es_ in done.items():
            l = lines[i]
            for e in sorted(es_, key=lambda e: -e["col"]):
                c = e["col"]
                if l[c:c + len(e["text"])] != e["text"]:
                    raise SystemExit(f"{rel}:{i + 1}: {l[c:c + len(e['text'])]!r} isn't {e['text']!r}")
                pal = e["pal"]
                if e["text"][:2] in ("0x", "0X") and pal >= 0:
                    pal = "0x%X" % pal
                l = l[:c] + f"RT({e['text']}, {pal})" + l[c + len(e["text"]):]
            lines[i] = l
        text = "\n".join(lines)
        if '#include "region.h"' not in text:
            idx = [m.end() for m in re.finditer(r'^#include .*$', text, re.M)]
            at = idx[-1] if idx else 0
            text = text[:at] + '\n#include "region.h"' + text[at:]
        open(path, "w").write(text)
    print(f"apply: {len(edits)} literals in {len(byfile)} files")


def _hunk(path, a_lines, i1, i2, b_new, ctx):
    """One unified hunk replacing a_lines[i1:i2] with b_new, ctx lines around."""
    lo, hi = max(0, i1 - ctx), min(len(a_lines), i2 + ctx)
    old = a_lines[lo:hi]
    new = a_lines[lo:i1] + b_new + a_lines[i2:hi]
    h = [f"@@ -{lo + 1},{len(old)} +{lo + 1},{len(new)} @@"]
    h += [" " + l for l in a_lines[lo:i1]] + ["-" + l for l in a_lines[i1:i2]]
    h += ["+" + l for l in b_new] + [" " + l for l in a_lines[i2:hi]]
    return h


def _patch_text(files_hunks):
    out = []
    for rel, hunks in files_hunks:
        if hunks:
            out += [f"--- a/{rel}", f"+++ b/{rel}"]
            for h in hunks:
                out += h
    return "\n".join(out) + "\n" if out else ""


def _applies(tree, text, dry=True):
    import subprocess
    args = ["patch", "-p1", "--forward", "-s", "-F0", "-d", tree] + (["--dry-run"] if dry else [])
    return subprocess.run(args, input=text, text=True, capture_output=True).returncode == 0


RT_PAIR = re.compile(r"RT\(([^,()]+), (-?[0-9A-Fa-fx]+)\)")


def _stmt(line):
    """A line's statement without the 60 Hz pilot's wrappers and comments."""
    code = line.split("//")[0].strip()
    return re.sub(r"T60_(?:SHARE8?|TICK_DO|TICK_END_DO|TICK_AND)\((.*)\)(?=;?$)", r"\1", code) \
        if code.startswith("T60_") else re.sub(r"T60_SHARE8?\((-?[\w.]+)\)", r"\1", code)


def _tokens(line, tok):
    """The places of literal tok in line's code (not its comment)."""
    code = line.split("//")[0]
    return [(m.start(), m.end()) for m in re.finditer(r"(?<![\w.])" + re.escape(tok) + r"(?![\w.])", code)]


def _core(old, new):
    """The part of old that new changes, and what replaces it."""
    p = 0
    while p < min(len(old), len(new)) and old[p] == new[p]:
        p += 1
    q = 0
    while q < min(len(old), len(new)) - p and old[-1 - q] == new[-1 - q]:
        q += 1
    co, cn = old[p:len(old) - q], new[p:len(new) - q]
    # whole tokens: widen to the literal (RT(8, 10) from 8 -> "8" and "RT(8, 10)")
    m = re.search(r"[\w.]*$", old[:p]).group(0)
    return m + co, m + cn


def _fragment(old, new, P):
    """The widest piece of old around its change that some port line holds,
    and new's piece in its place (the port may have wrapped the line)."""
    p = 0
    while p < min(len(old), len(new)) and old[p] == new[p]:
        p += 1
    q = 0
    while q < min(len(old), len(new)) - p and old[-1 - q] == new[-1 - q]:
        q += 1
    core_o, core_n = old[p:len(old) - q], new[p:len(new) - q]
    left, right = old[:p], old[len(old) - q:]
    for n in range(len(left), -1, -1):
        for m in range(len(right), -1, -1):
            frag = left[len(left) - n:] + core_o + right[:m]
            if (n + m >= 6 or not left and not right) and any(frag in l for l in P):
                return frag, left[len(left) - n:] + core_n + right[:m]
    return old, new


def _udiff(rel, a, b):
    lines = list(difflib.unified_diff(a, b, f"a/{rel}", f"b/{rel}", n=3, lineterm=""))
    return "\n".join(lines) + "\n" if lines else ""


def _apply(tree, rel, text, lines):
    """lines patched with text (in a scratch tree), or None if it fails."""
    import subprocess
    path = os.path.join(tree, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write("\n".join(lines))
    if text:
        r = subprocess.run(["patch", "-p1", "--forward", "-s", "-F0", "-d", tree], input=text,
                           text=True, capture_output=True)
        if r.returncode:
            print((r.stdout + r.stderr)[:600])
            return None
    return open(path).read().split("\n")


def cmd_patch(a):
    """Write a region edit as patches both builds apply (D135). A change
    whose lines, with their context, are the same in the port's tree (after
    the port's own patches, D133's order) goes to patches/spyro-1-region/
    NAME.patch. One the port's patches rewrote (the 60 Hz pilot's T60_*
    sites, D130; the skipped logos) is written twice: patches/
    spyro-1-region-matching/ (build_matching.sh only) and patches/
    spyro-1-region-port/ (the port only), the port's on the port's own line.
    Placement is checked: base -> port lines are paired by difflib, and the
    patches applied to a copy must give exactly the expected files."""
    import shutil
    import tempfile
    shared, monly, ponly, manual = [], [], [], []
    tmp = tempfile.mkdtemp()
    for rel in a.files:
        A = open(os.path.join(a.base, rel)).read().split("\n")
        B = open(os.path.join(a.edit, rel)).read().split("\n")
        P = open(os.path.join(a.port, rel)).read().split("\n")
        amap = {}
        for blk in difflib.SequenceMatcher(None, A, P, autojunk=False).get_matching_blocks():
            for k in range(blk.size):
                amap[blk.a + k] = blk.b + k

        A_txt, P_txt = "\n".join(A), "\n".join(P)

        def window_ok(lo, hi):
            idx = [amap.get(i) for i in range(lo, hi)]
            if None in idx or idx != list(range(idx[0], idx[0] + len(idx))):
                return False
            # unique in both trees, or patch may put it on a copy (moby_update.c repeats itself)
            w = "\n" + "\n".join(A[lo:hi]) + "\n"
            return ("\n" + A_txt + "\n").count(w) == 1 and ("\n" + P_txt + "\n").count(w) == 1

        blocks = [(i1, i2, j1, j2) for tag, i1, i2, j1, j2 in
                  difflib.SequenceMatcher(None, A, B, autojunk=False).get_opcodes() if tag != "equal"]
        # group blocks closer than 2*10 lines, then share a group if its window
        # (3 to 10 lines of context) is the same in the port and unique in both
        groups = []
        for b in blocks:
            if groups and b[0] - groups[-1][-1][1] <= 20:
                groups[-1].append(b)
            else:
                groups.append([b])
        sh_hunks, sh_port, conflict, anchors = [], [], [], []
        for g in groups:
            i1, i2 = g[0][0], g[-1][1]
            ctx = next((c for c in range(3, 11) if window_ok(max(0, i1 - c), min(len(A), i2 + c))), None)
            if ctx is None:
                conflict.extend(g)
                continue
            lo, hi = max(0, i1 - ctx), min(len(A), i2 + ctx)
            new, at = [], lo
            for b1, b2, c1, c2 in g:
                new += A[at:b1] + B[c1:c2]
                at = b2
            new += A[at:hi]
            h = [f"@@ -{lo + 1},{hi - lo} +{lo + 1},{len(new)} @@"]
            h += [" " + l for l in A[lo:i1]]
            at = i1
            for b1, b2, c1, c2 in g:
                h += [" " + l for l in A[at:b1]] + ["-" + l for l in A[b1:b2]] + ["+" + l for l in B[c1:c2]]
                at = b2
            h += [" " + l for l in A[at:hi]]
            sh_hunks.append(h)
            sh_port.append((amap[lo], amap[hi - 1] + 1, new))
            anchors.append((lo, amap[lo]))  # a window unique in both trees: a sure offset
        st = _patch_text([(rel, sh_hunks)])
        # the port: the shared groups where the pairing puts them, then each
        # conflicting change on the port's own line, near where it should be
        expect, at = [], 0
        for plo, phi, new in sh_port:
            expect += P[at:plo] + new
            at = phi
        expect += P[at:]
        # positions in `expect`: shift the pairing by the shared groups' growth
        def to_expect(n):
            d = 0
            for plo, phi, new in sh_port:
                if phi <= n:
                    d += len(new) - (phi - plo)
            return n + d
        last = -1  # the conflicts in order: each on a port line after the one before
        prev_b = -1
        for b1, b2, c1, c2 in conflict:
            if b1 == b2:  # an inserted line (#include "region.h"): after the line before it
                before = max((i for i in amap if i < b1), default=None)
                at = to_expect(amap[before]) + 1 if before is not None else 0
                expect[at:at] = B[c1:c2]
                last = max(last, at + c2 - c1 - 1)
                continue
            if b2 - b1 != c2 - c1:
                manual.append(f"{rel}:{b1 + 1}: not a line-for-line change")
                continue
            anc = min(anchors, key=lambda x: abs(x[0] - b1), default=None)
            est = to_expect(b1 + anc[1] - anc[0]) if anc else b1
            for k in range(b2 - b1):
                ao, bo = A[b1 + k].strip(), B[c1 + k].strip()
                co, cn = _core(ao, bo)
                # the base's unchanged copies of this statement since the last
                # conflict: the port has them too, in order, and they're skipped
                skip = sum(1 for i in range(prev_b + 1, b1 + k) if A[i].strip() == ao and A[i] == B[i - b1 - k + c1 + k])
                found = []
                for n in range(last + 1, min(len(expect), est + 80 + k)):
                    if _stmt(expect[n]) == ao:
                        found.append(n)
                        if len(found) > skip:
                            break
                n = found[skip] if len(found) > skip else None
                if n is None:  # not the same statement (the logos' `LOGOS && `): the nearest alike
                    near = [m for m in range(max(last + 1, est - 40), min(len(expect), est + 40 + k))
                            if difflib.SequenceMatcher(None, ao, _stmt(expect[m])).ratio() >= 0.75
                            and len(_tokens(expect[m], co)) == 1]
                    n = min(near, key=lambda m: abs(m - est - k)) if near else None
                if n is None or len(_tokens(expect[n], co)) != 1:
                    manual.append(f"{rel}:{b1 + k + 1}: no port line near {est + 1} is {ao!r}")
                    continue
                if os.environ.get("RETUNE_DEBUG"):
                    print("  place", rel, b1 + k + 1, "->", n + 1, repr(expect[n].strip()[:50]))
                t0, t1 = _tokens(expect[n], co)[0]
                expect[n] = expect[n][:t0] + cn + expect[n][t1:]
                last = n
                prev_b = b1 + k
        d = os.path.join(tmp, "t")
        P2 = _apply(d, rel, st, P)
        if P2 is None:
            raise SystemExit(f"{rel}: the shared patch doesn't apply to the port's tree")
        pt = _udiff(rel, P2, expect)
        if _apply(d, rel, pt, P2) != expect:
            raise SystemExit(f"{rel}: the port-only patch doesn't give the expected file")
        A2 = _apply(d, rel, st, A)
        mt = _udiff(rel, A2, B)
        if A2 is None or _apply(d, rel, mt, A2) != B:
            raise SystemExit(f"{rel}: the matching patches don't give the edited file")
        shared.append(st)
        monly.append(mt)
        ponly.append(pt)
        print(f"{rel}: {len(sh_hunks)} shared hunks; {len(conflict)} changes on lines the port rewrote, "
              f"written for each tree; placement checked")
    for folder, texts in (("spyro-1-region", shared), ("spyro-1-region-matching", monly),
                          ("spyro-1-region-port", ponly)):
        t = "".join(texts)
        path = os.path.join(a.patches, folder, a.name + ".patch")
        if t:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "w").write(t)
        elif os.path.exists(path):
            os.remove(path)
    shutil.rmtree(tmp)
    for m in manual:
        print("manual:", m)
    return 1 if manual else 0


def cmd_rtcheck(a):
    """The RT(us, pal) pairs, file by file, in two trees: the same, or a list."""
    bad = 0
    for rel in a.files:
        x = sorted(RT_PAIR.findall(open(os.path.join(a.tree_a, rel)).read()))
        y = sorted(RT_PAIR.findall(open(os.path.join(a.tree_b, rel)).read()))
        if x != y:
            bad += 1
            from collections import Counter
            cx, cy = Counter(x), Counter(y)
            print(f"{rel}: {len(x)} vs {len(y)} RT pairs; only in the first {list((cx - cy).elements())[:8]}, "
                  f"only in the second {list((cy - cx).elements())[:8]}")
        else:
            print(f"{rel}: {len(x)} RT pairs, the same")
    return 1 if bad else 0


def cmd_context(a):
    """A function's sites and unpaired ranges with the asm around each: the
    decomp's asm/nonmatchings .s for US, objdump of the player's executable
    for the release (--dis: `mipsel-linux-gnu-objdump -D -b binary -m mips:3000
    -EL --adjust-vma=0x80010000` of the executable less its 0x800-byte
    header). For mapping sites into hand C by reading (D135). The output
    holds disassembly of the player's disc: keep it out of the repo."""
    import glob
    d = json.load(open(a.sites))
    row = next(r for rows in d["units"].values() for r in rows if r["name"] == a.function)
    s_path = glob.glob(os.path.join(a.asm, "*", a.function + ".s"))[0]
    us = open(s_path).read().splitlines()
    idx = {}
    for i, l in enumerate(us):
        m = re.match(r"/\* \w+ ([0-9A-F]{8}) ", l)
        if m:
            idx[int(m.group(1), 16)] = i
    pal = {}
    for l in open(a.dis):
        m = re.match(r"([0-9a-f]{8}):\t", l)
        if m:
            pal[int(m.group(1), 16)] = l.rstrip()

    def usctx(addr, before=6, after=2):
        i = idx[addr]
        return "\n".join((">> " if j == i else "   ") + us[j]
                         for j in range(max(0, i - before), min(len(us), i + after + 1)))
    w = open(a.out, "w")
    w.write(f"# {a.function}: US {row['addr']:08x} -> {d['region']} {row['t_addr']:08x}; asm {s_path}\n\n"
            f"## Constant sites ({len(row['sites'])})\n")
    for k, s_ in enumerate(row["sites"]):
        w.write(f"\n### site {k}: US {s_['us']:08x} {s_['op']} {s_['a']} -> {s_['pal']:08x} {s_['b']} "
                f"({s_['cls']})\n```\n{usctx(s_['us'])}\nrelease: {pal.get(s_['pal'], '?')}\n```\n")
    w.write(f"\n## Unpaired ranges ({len(row['ranges'])})\n")
    for x in row["ranges"]:
        w.write(f"\n### {x['tag']}: US {x['us'][0]:08x}+{x['nu']} / {x['pal'][0]:08x}+{x['nt']}\n```\nUS:\n")
        w.write("\n".join(us[idx[q]] for q in range(x["us"][0] - 12, x["us"][1] + 8, 4) if q in idx))
        w.write("\nrelease:\n" + "\n".join(pal.get(q, "?") for q in range(x["pal"][0] - 12, x["pal"][1] + 8, 4))
                + "\n```\n")


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("sites")
    s.add_argument("kind", choices=("exe", "wad"))
    s.add_argument("target")
    s.add_argument("region", nargs="?", default="pal")
    s.add_argument("--elf", default=cc.ELF)
    s.add_argument("--json")
    s.add_argument("--list", action="store_true")
    s.add_argument("--only")
    l_ = sp.add_parser("lines")
    l_.add_argument("--dis", required=True, help="objdump -dl outputs of the build's objects")
    l_.add_argument("--elf", default=cc.ELF)
    l_.add_argument("--out", required=True)
    p = sp.add_parser("propose")
    p.add_argument("--sites", nargs="+", required=True)
    p.add_argument("--lines", required=True)
    p.add_argument("--elf", default=cc.ELF)
    p.add_argument("--tree", required=True, help="the patched decomp tree the build compiled")
    p.add_argument("--functions", help="only these (overlay functions by their base name)")
    p.add_argument("--sections", help="only these sections (.main, .LEVEL_10, ...)")
    p.add_argument("--out", required=True)
    p.add_argument("--report", required=True)
    y = sp.add_parser("apply")
    y.add_argument("--edits", required=True)
    y.add_argument("--tree", required=True)
    q = sp.add_parser("patch")
    q.add_argument("--base", required=True, help="the matching tree before the edit")
    q.add_argument("--edit", required=True, help="the matching tree with it")
    q.add_argument("--port", required=True, help="the port's tree (its patches and the earlier region ones in)")
    q.add_argument("--patches", default="patches")
    q.add_argument("--name", required=True)
    q.add_argument("files", nargs="+")
    x = sp.add_parser("context")
    x.add_argument("function")
    x.add_argument("--sites", required=True)
    x.add_argument("--asm", default="third_party/spyro-1/asm/nonmatchings")
    x.add_argument("--dis", required=True)
    x.add_argument("--out", required=True)
    c = sp.add_parser("rtcheck")
    c.add_argument("tree_a")
    c.add_argument("tree_b")
    c.add_argument("files", nargs="+")
    a = ap.parse_args()
    return {"sites": cmd_sites, "lines": cmd_lines, "propose": cmd_propose, "apply": cmd_apply,
            "patch": cmd_patch, "rtcheck": cmd_rtcheck, "context": cmd_context}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())

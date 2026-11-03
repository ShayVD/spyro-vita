#!/usr/bin/env python3
"""Is v1 (or v0) live after calls to a function? For each `jal NAME` in the
decomp's asm, follow the caller's code from after the delay slot, both ways
at each branch, to the first write or read of the register on every path:
"dead" if every path writes it before reading it, "READ" if a path reads it
first, "returns" if a path reaches the caller's `jr $ra` with it unwritten
(live only if the caller's callers read it), "unknown" if a path leaves by
a computed jump, "call" if a path reaches another call first (its callee
decides; the delay slot is checked). C callers can't see v1. Used to justify "v1 NAME" and "ret
NAME" lines in tools/oracle/ignore_ranges.txt.

    v1_live.py NAME [--reg v0] [--asm third_party/spyro-1/asm] [-v]"""
import argparse
import glob
import re

ap = argparse.ArgumentParser()
ap.add_argument("name")
ap.add_argument("--reg", default="v1")
ap.add_argument("--asm", default="third_party/spyro-1/asm")
ap.add_argument("-v", action="store_true", help="print dead call sites too")
a = ap.parse_args()
REG = "$" + a.reg

INS = re.compile(r"\*/\s+([a-z0-9.]+)\s*(.*)$")
LABEL = re.compile(r"^(?:glabel\s+)?(\.?\w+):?\s*$")
BRANCHES = re.compile(r"^b\w*$")
# Only read their operands: stores, branches, jumps, moves to coprocessors,
# multiply and divide.
READ_ONLY = ("sb", "sh", "sw", "swl", "swr", "swc2", "mult", "multu", "div", "divu", "mthi", "mtlo")


def functions(path):
    """Each function's instructions as (op, args) and its labels' indices."""
    funcs = []
    cur = None
    for l in open(path, errors="ignore"):
        if l.startswith("glabel"):
            name = l.split()[1]
            if cur is None or cur["ins"]:
                cur = {"name": name, "ins": [], "labels": {}}
                funcs.append(cur)
            cur["labels"][name] = len(cur["ins"])
            continue
        if cur is None:
            continue
        m = INS.search(l)
        if m:
            cur["ins"].append((m.group(1), [x.strip() for x in m.group(2).split(",") if x.strip()]))
            continue
        m = LABEL.match(l.strip())
        if m:
            cur["labels"][m.group(1)] = len(cur["ins"])
    return funcs


def verdict(f, start):
    """The register's fate on every path from instruction start."""
    seen, todo, out = set(), [start], set()
    while todo:
        i = todo.pop()
        while True:
            if i in seen or i >= len(f["ins"]):
                break
            seen.add(i)
            op, args = f["ins"][i]
            reads = args if op in READ_ONLY or op.startswith(("b", "j", "mt", "ct")) else args[1:]
            if any(REG in s for s in reads):
                out.add("READ")
                break
            if args and args[0] == REG and op not in READ_ONLY and not op.startswith(("b", "j", "mt", "ct")):
                out.add("dead")
                break
            if op in ("jal", "jalr"):
                d_op, d_args = f["ins"][i + 1]
                d_reads = d_args if d_op in READ_ONLY or d_op.startswith(("b", "j", "mt", "ct")) else d_args[1:]
                out.add("READ" if any(REG in s for s in d_reads) else "call")
                break
            if op == "jr":
                # The delay slot runs first.
                d_op, d_args = f["ins"][i + 1] if i + 1 < len(f["ins"]) else ("nop", [])
                d_reads = d_args if d_op in READ_ONLY or d_op.startswith(("b", "j", "mt", "ct")) else d_args[1:]
                if any(REG in s for s in d_reads):
                    out.add("READ")
                elif d_args and d_args[0] == REG and d_op not in READ_ONLY:
                    out.add("dead")
                else:
                    out.add("returns" if args[0] == "$ra" else "unknown")
                break
            if op == "j" or BRANCHES.match(op):
                t = f["labels"].get(args[-1])
                if t is None:
                    out.add("unknown")
                    break
                # The delay slot, then the target (and, for a conditional
                # branch, the fall-through).
                todo.append(t)
                if op in ("j", "b"):
                    i += 1
                    d_op, d_args = f["ins"][i]
                    d_reads = d_args if d_op in READ_ONLY or d_op.startswith(("b", "j", "mt", "ct")) else d_args[1:]
                    if any(REG in s for s in d_reads):
                        out.add("READ")
                        todo.pop()
                    elif d_args and d_args[0] == REG and d_op not in READ_ONLY:
                        out.add("dead")
                        todo.pop()
                    break
                i += 1
                continue
            i += 1
    if not out:
        return "dead"  # every path loops back to code already seen
    for v in ("READ", "unknown", "returns", "call"):
        if v in out:
            return v
    return "dead"


res = {"dead": 0, "READ": 0, "returns": 0, "call": 0, "unknown": 0}
for path in sorted(glob.glob(f"{a.asm}/**/*.s", recursive=True)):
    for f in functions(path):
        for i, (op, args) in enumerate(f["ins"]):
            if op != "jal" or not args or args[0] != a.name:
                continue
            d_op, d_args = f["ins"][i + 1]
            if d_args and d_args[0] == REG and d_op not in READ_ONLY and not d_op.startswith(("b", "j")):
                v = "READ"  # the delay slot writes it before the callee runs: the caller's own value
            else:
                v = verdict(f, i + 2)
            res[v] += 1
            if v != "dead" or a.v:
                print(f"{v}: {path} {f['name']} +{i * 4:#x}")
print(f"{a.name} ({a.reg}): {res['dead']} call sites write it first, {res['READ']} read it, "
      f"{res['returns']} return with it, {res['call']} reach a call, {res['unknown']} unknown")

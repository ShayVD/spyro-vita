#!/usr/bin/env python3
"""The 60 Hz rules' object (decisions D130; cmake/tick60.cmake).

  tick60_link.py --nm NM --objcopy OBJCOPY --entry OLD=NEW [--report TXT] IN.o OUT.o

IN.o is the update path built again with SPYRO_TICK60 (`cc -r`). OUT.o is
IN.o with:
  - every function it defines made local, but the entry, renamed NEW: a
    call inside the set goes to the set's copy, a call from the game's
    30 Hz code never reaches it;
  - every global data definition weakened: the rules write the game's own
    memory, so the game's definition wins (a common symbol, the decomp's
    -fcommon globals, is the game's already);
  - no writable data of its own (local .data/.bss) but what ALLOWED lists:
    a function-static the 30 Hz code also has would split the game's state
    in two. Read-only data (tables, strings) may be copied.
The report lists the counts and every name in each group.
"""
import argparse
import re
import subprocess
import sys

# Writable data the set may keep for itself, each because it holds nothing
# between updates:
#   s_t60_*: the rules' own scratch, which t60_rules.c sets every update
#            (this update's delta, the moby list split by rate);
#   s_tilt:  camera_hand.c's free-camera tilt, set by func_80035FB4 for the
#            springs it runs and cleared before it returns.
ALLOWED = r"""
^s_t60_
^s_tilt$
"""


def nm(tool, obj):
    out = subprocess.run([tool, "-P", obj], check=True, stdout=subprocess.PIPE,
                         universal_newlines=True).stdout
    syms = []
    for line in out.splitlines():
        f = line.split()
        if len(f) < 2:
            continue
        syms.append((f[0], f[1]))
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--objcopy", required=True)
    ap.add_argument("--entry", action="append", required=True)
    ap.add_argument("--report")
    ap.add_argument("inp")
    ap.add_argument("out")
    a = ap.parse_args()
    entries = dict(e.split("=", 1) for e in a.entry)
    allowed = [re.compile(p) for p in ALLOWED.split()]
    syms = nm(a.nm, a.inp)
    funcs, weak, own, ro, common = [], [], [], [], []
    for name, t in syms:
        if t in "T":
            if name not in entries:
                funcs.append(name)
        elif t in "DBGS":
            weak.append(name)
        elif t in "dbgs":
            own.append(name)
        elif t in "Rr":
            ro.append(name)
        elif t == "C":
            common.append(name)
    for e in entries:
        if (e, "T") not in syms:
            sys.exit(f"tick60_link: the entry {e} isn't defined")
    bad = [n for n in own if not any(p.search(n) for p in allowed)]
    # local labels the compiler makes for its own constants are read-only
    bad = [n for n in bad if not re.match(r"^\.L|^\$", n)]
    if bad:
        sys.exit("tick60_link: writable data of the 60 Hz rules' own, which would split the "
                 "game's state: " + ", ".join(sorted(bad)))
    loc = a.out + ".localize"
    with open(loc, "w") as f:
        f.write("\n".join(funcs) + "\n")
    wk = a.out + ".weaken"
    with open(wk, "w") as f:
        f.write("\n".join(weak) + "\n")
    cmd = [a.objcopy, "--localize-symbols=" + loc, "--weaken-symbols=" + wk]
    for old, new in entries.items():
        cmd.append(f"--redefine-sym={old}={new}")
    subprocess.run(cmd + [a.inp, a.out], check=True)
    if a.report:
        with open(a.report, "w") as f:
            f.write(f"entries: {', '.join(f'{o} as {n}' for o, n in entries.items())}\n")
            for title, group in (("functions made local", funcs), ("data weakened (the game's)", weak),
                                 ("the rules' own writable data", own), ("common (the game's)", common),
                                 ("read-only", ro)):
                f.write(f"{title}: {len(group)}\n")
                for n in sorted(group):
                    f.write(f"  {n}\n")


if __name__ == "__main__":
    main()

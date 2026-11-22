#!/usr/bin/env python3
"""Rank a thread's functions from a Vita log's fnprof lines (vita/runtime/fnprof: vpk_fnprof_init / _dump in a build
whose game files have -finstrument-functions).

  vita/tools/fnprof.py RUN/game.log --elf build/game.elf [--top 40] [--callers 3] [--units] [--prefix vpk-fnprof]

A sample is (where, site): `where` is the instrumented function just entered (then `site` is its call site) or the
address the last instrumented call returned to. Time in callees that are not instrumented counts for `where`'s
function. Shares are of the samples taken while the profiled thread was running. The Vita loads the eboot at another
address each run: the init line's `base` (vpk_fnprof_init's own address) gives the slide.

--alias PREFIX: of two names at one address, prefer the one without PREFIX (an override table's aliases).
--units sums by source file (addr2line next to --nm); paths under the current directory are shown relative to it.
From kh1-vita tools/vita/fnprof.py @ 75afd241. Python 3, stdlib.
"""
import argparse
import bisect
import collections
import os
import re
import subprocess


def symbols(elf, nm, alias=None):
    out = subprocess.run([nm, "-n", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    addrs, names = [], []
    for l in out.splitlines():
        f = l.split()
        if len(f) == 3 and f[1] in "tTwW" and not f[2].startswith("$"):
            a = int(f[0], 16) & ~1
            if addrs and addrs[-1] == a:
                if alias and names[-1].startswith(alias) and not f[2].startswith(alias):
                    names[-1] = f[2]
                continue
            addrs.append(a)
            names.append(f[2])
    return addrs, names


def units(elf, addr2line, addrs):
    out = subprocess.run([addr2line, "-e", elf] + ["%x" % a for a in addrs], capture_output=True, text=True).stdout
    cwd = os.getcwd() + os.sep

    def rel(p):
        p = os.path.normpath(p)
        return p[len(cwd):] if p.startswith(cwd) else p
    return [rel(l.split(":")[0]) for l in out.splitlines()]


def rank(text, addrs, names, prefix="vpk-fnprof"):
    """{'total', 'end', 'self': Counter(name -> samples), 'callers': name -> Counter, 'first': name -> address}"""
    p = re.escape(prefix)
    base = re.search(p + r": on, .* base ([0-9a-f]+)", text)
    if not base or "vpk_fnprof_init" not in names:
        raise SystemExit("fnprof: no `%s: on` line in the log, or no vpk_fnprof_init in the ELF" % prefix)
    slide = (int(base[1], 16) & ~1) - addrs[names.index("vpk_fnprof_init")]

    def name(x):
        i = bisect.bisect_right(addrs, (x & ~1) - slide) - 1
        return names[i] if i >= 0 else "?"

    self_n, callers, first, total = collections.Counter(), collections.defaultdict(collections.Counter), {}, 0
    for fn, site, n in re.findall(p + r": s ([0-9a-f]+) ([0-9a-f]+) (\d+)", text):
        fn, site, n = int(fn, 16), int(site, 16), int(n)
        f = name(fn) if fn else "(before the first call)"
        self_n[f] += n
        first.setdefault(f, (fn & ~1) - slide)
        total += n
        if site:
            callers[f][name(site)] += n
    m = re.search(p + r": end .*", text)
    return {"total": total, "end": m[0] if m else None, "self": self_n, "callers": callers, "first": first}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--elf", required=True)
    ap.add_argument("--nm", default=os.path.join(os.environ.get("VITASDK", "/usr/local/vitasdk"), "bin/arm-vita-eabi-nm"))
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--callers", type=int, default=3)
    ap.add_argument("--prefix", default="vpk-fnprof")
    ap.add_argument("--alias", help="of two names at one address, prefer the one without this prefix")
    ap.add_argument("--units", action="store_true", help="also sum by source file (addr2line)")
    a = ap.parse_args()
    addrs, names = symbols(a.elf, a.nm, a.alias)
    r = rank(open(a.log, errors="replace").read(), addrs, names, a.prefix)
    total, self_n, callers = r["total"], r["self"], r["callers"]
    if not total:
        raise SystemExit("fnprof: no samples in the log")
    print("%d samples; %s" % (total, r["end"] or "no end line (the run did not stop cleanly)"))
    print("| function | samples | share | entered from (samples taken before its first instrumented call returned) |")
    print("| --- | --- | --- | --- |")
    for f, n in self_n.most_common(a.top):
        c = ", ".join("%s %d" % (k, v) for k, v in callers[f].most_common(a.callers))
        print("| %s | %d | %.2f %% | %s |" % (f, n, 100.0 * n / total, c))
    if a.units:
        fs = list(self_n)
        us = units(a.elf, a.nm.replace("-nm", "-addr2line"), [r["first"][f] for f in fs])
        by = collections.Counter()
        for f, u in zip(fs, us):
            by[u] += self_n[f]
        print("\n| source file | samples | share |\n| --- | --- | --- |")
        for u, n in by.most_common(25):
            print("| %s | %d | %.2f %% |" % (u, n, 100.0 * n / total))


if __name__ == "__main__":
    main()

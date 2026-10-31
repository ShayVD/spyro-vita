#!/usr/bin/env python3
"""Look for code copied from a reference-only checkout (decisions D56): every
line of 40+ characters (whitespace normalised, comments and #lines skipped)
that the reference's own code and this repo's tracked files outside
third_party/ share. Hits need reading: CMake idioms and GL calls are shared
by everyone.

  tools/check_reference_overlap.py ../psxrecomp-ref .
"""
import os, re, subprocess, sys
ref, repo = sys.argv[1], sys.argv[2]
EXT = (".c", ".cpp", ".h", ".hpp", ".py", ".sh", ".cmake", ".txt", ".lua", ".s", ".S", ".swift", ".rs")
norm = lambda l: re.sub(r"\s+", " ", l.strip())
refl = {}
for d, ds, fs in os.walk(ref):
    ds[:] = [x for x in ds if x not in (".git", "third_party", "build", "generated")]
    for f in fs:
        if f.endswith(EXT) or f == "CMakeLists.txt":
            p = os.path.join(d, f)
            try:
                for l in open(p, errors="ignore"):
                    n = norm(l)
                    if len(n) >= 40 and not n.startswith(("#include", "//", "/*", "*", "#")):
                        refl.setdefault(n, p)
            except Exception: pass
files = subprocess.run(["git", "-C", repo, "ls-files"], capture_output=True, text=True).stdout.split()
hits = []
for f in files:
    if f.startswith("third_party/"): continue
    p = os.path.join(repo, f)
    try:
        for i, l in enumerate(open(p, errors="ignore")):
            n = norm(l)
            if n in refl: hits.append((f, i + 1, n[:100], refl[n].replace(ref, "")))
    except Exception: pass
print(len(refl), "ref lines;", len(hits), "shared")
for h in hits[:80]: print(*h, sep=" | ")

#!/usr/bin/env python3
"""List an object's common symbols, one per line, for objcopy's
--keep-global-symbols (the PS4 build, D193).

llvm-objcopy's --keep-global-symbol localizes every other global, common
symbols included, and a local common symbol is an absolute one at its
alignment (4, 8): the presenter's pres_draw.o would read the decomp's
-fcommon globals at address 4. GNU objcopy (linux32, linux64, the Vita)
leaves commons global, so they stay the game's own.

    keep_commons.py NM OBJ OUT
"""
import subprocess
import sys

nm, obj, out = sys.argv[1:4]
lines = subprocess.run([nm, "-P", obj], check=True, stdout=subprocess.PIPE,
                       universal_newlines=True).stdout.splitlines()
names = sorted({f[0] for f in (l.split() for l in lines) if len(f) >= 2 and f[1] == "C"})
with open(out, "w") as f:
    f.write("".join(n + "\n" for n in names))

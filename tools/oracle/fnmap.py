#!/usr/bin/env python3
"""Map PS1 code addresses to the host's functions, for the oracle replay
(spyro --replay --fnmap): the emulator's RAM holds function pointers as PS1
addresses (moby update tables, overlay pointers), the host build holds its
own. Names come from the matching build's psx.map (.text symbols only), host
addresses from `nm` of the host binary (run inside tools/tc).

Output lines: PSX_ADDR HOST_ADDR NAME. Overlay variants share addresses;
their names end in the variant (UpdateMoby_LEVEL_10), and the replay keeps
the one for the snapshot's level. Also "TERM PSX_ADDR NAME" for each
overlay's end (Terminator_*) and main_BSS_END: what lies above the loaded
overlay is the level's data from disc, where the replay leaves words that
merely look like pointers alone.

    fnmap.py psx.map NM_OUTPUT > fnmap.txt"""
import re
import sys

mapfile, nmfile = sys.argv[1], sys.argv[2]
host = {}
for line in open(nmfile):
    p = line.split()
    if len(p) == 3 and p[1] in "Tt":
        host[p[2]] = int(p[0], 16)

text = False
seen = set()
for line in open(mapfile):
    m = re.match(r"^\s+0x(80[0-9a-f]{6})\s+(Terminator_\w+|main_BSS_END)\b", line)
    if m:
        print(f"TERM {m.group(1).upper()} {m.group(2)}")
    m = re.match(r"^ (\.\w+)\s", line)
    if m:
        text = m.group(1) == ".text"
        continue
    m = re.match(r"^\s+0x(80[0-9a-f]{6})\s+([A-Za-z_]\w*)\s*$", line)
    if m and text and m.group(2) in host and m.group(2) not in seen:
        seen.add(m.group(2))
        print(f"{m.group(1).upper()} {host[m.group(2)]:08X} {m.group(2)}")

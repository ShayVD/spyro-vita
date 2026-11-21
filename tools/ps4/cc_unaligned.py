#!/usr/bin/env python3
"""The PS4 build's compiler launcher (CMAKE_C_COMPILER_LAUNCHER, D193): no
instruction that faults on a misaligned address.

On x86-64, clang takes every global array of 16 bytes or more as 16-byte
aligned (the SysV ABI's large-array rule; it does so for `extern` declarations
and -fcommon tentative definitions too) and writes aligned SSE moves for it.
The decomp's data labels aren't: they sit at their PS1 addresses in the data
image (tools/data2s.py --image), many of them 4- or 8-aligned, and the C can't
be told otherwise without an attribute on every declaration. Hardware run 2
(logs/p3/hw-run2/klog.txt): starting a game crashed in func_80012604 on
`movaps %xmm0, g_FlightCourseRecords`, 8-aligned (a general protection fault;
qemu-x86_64, linux64's checker, doesn't enforce the alignment).

So every C unit is compiled with -march=btver2 (the PS4's Jaguar: SSE as VEX
instructions, whose memory operands have no alignment requirement), to
assembly; the moves that still demand alignment (vmovaps, vmovapd, vmovdqa:
reg-memory forms of the same operation as vmovups, vmovupd, vmovdqu, which
don't) are rewritten to their unaligned twins; a non-temporal store (no
unaligned form) or a non-VEX SSE move fails the build. Then the assembly is
assembled with the same flags.

    cc_unaligned.py CC ARGS...     (as CMake runs a launcher)
"""
import os
import re
import subprocess
import sys

ALIGNED = re.compile(r"\b(vmova)(ps|pd)\b|\b(vmovdqa)\b(?!\d)")
FORBIDDEN = re.compile(r"^\s+(v?movnt\w*|mova(ps|pd)|movdqa|movntdqa)\b", re.M)


def unaligned(m):
    if m.group(1):
        return "vmovu" + m.group(2)
    return "vmovdqu"


def main():
    cmd = sys.argv[1:]
    srcs = [a for a in cmd if not a.startswith("-") and a.endswith((".c", ".C", ".cpp", ".cc"))]
    if "-c" not in cmd or "-o" not in cmd or len(srcs) != 1:
        os.execvp(cmd[0], cmd)
    out = cmd[cmd.index("-o") + 1]
    asm = out + ".ua.s"
    to_asm = []
    i = 0
    while i < len(cmd):
        a = cmd[i]
        if a == "-c":
            to_asm.append("-S")
        elif a == "-o":
            to_asm += ["-o", asm]
            i += 1
        else:
            to_asm.append(a)
        i += 1
    r = subprocess.run(to_asm)
    if r.returncode:
        return r.returncode
    with open(asm, encoding="latin-1") as f:
        text = f.read()
    bad = FORBIDDEN.findall(text)
    if bad:
        sys.stderr.write(f"cc_unaligned: {srcs[0]}: instructions that need alignment and have no "
                         f"unaligned form: {sorted(set(b[0] for b in bad))}\n")
        return 1
    text, n = ALIGNED.subn(unaligned, text)
    with open(asm, "w", encoding="latin-1") as f:
        f.write(text)
    # assemble with the same flags, less the source, the output and the
    # dependency file the -S step already wrote
    skip_next = {"-o", "-MF", "-MT", "-MQ"}
    to_obj = []
    i = 0
    while i < len(cmd):
        a = cmd[i]
        if a in skip_next:
            i += 2
            continue
        if a in ("-MD", "-MMD") or a == srcs[0]:
            i += 1
            continue
        to_obj.append(a)
        i += 1
    to_obj += ["-Wno-unused-command-line-argument", asm, "-o", out]
    r = subprocess.run(to_obj)
    if r.returncode == 0:
        os.remove(asm)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())

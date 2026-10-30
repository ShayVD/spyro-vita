#!/usr/bin/env python3
"""Convert the decomp's splat data files (asm/data/*.s, MIPS GNU as) into
host GNU as (i386 or ARM) with the same bytes, labels and alignment
(docs/porting-plan.md §4.4, decisions D9 and D15).

    tools/data2s.py --root third_party/spyro-1 -o OUT.S asm/data/game.data.s ...
    (FILE.s:LABEL,LABEL takes only those labels from a file)

MIPS `.word` (4 bytes) becomes `.long`, `.short`/`.half` stays 2 bytes,
`.align N` (2^N bytes on MIPS) becomes `.balign 2^N`, `dlabel X` becomes a
global object label, `.incbin` paths are made absolute. Symbolic words
(`.word D_800752C8`) stay symbolic, so the host linker relocates them.

Raw words holding scratchpad addresses (0x1F800000-0x1F8003FF) are listed in
`s1_scratch_relocs` (a null-terminated table of their addresses).

PS1 layout (--map psx.map, decisions D23): every label becomes an absolute
symbol at its PS1 address (the port's RAM arena), and its bytes become an
image that start-up copies there (`s1_psx_image`: {PS1 address, image,
bytes}, zero-terminated; .bss labels need no copy, the arena starts zeroed).
Game code then sees the same addresses as on a PS1, bit 31 included.

abscheck: a raw `.word` whose value lies in PS1 main RAM (0x80000000-0x801FFFFF)
may be an unrelocated pointer. Each one must be listed in --allow (one
`SYMBOL+0xOFFSET` per line, # comments) or the conversion fails.
"""
import argparse
import itertools
import os
import re
import sys

SECTIONS = {".data": ".data", ".sdata": ".data", ".rodata": ".section .rodata",
            ".bss": ".bss", ".sbss": ".bss"}
DIRECTIVE = re.compile(r"^(\.\w+)\s*(.*?)\s*$")


scratch_relocs = []  # words holding scratchpad addresses (0x1F800000-0x1F8003FF)


psx_addr = {}    # --map: symbol -> PS1 address
psx_images = []  # (PS1 address, image label, end label, PS1 span)
psx_labels = itertools.count()


def convert(path, root, allow, problems, only=None):
    """only: set of labels to keep (FILE.s:LABEL,LABEL); their data is
    emitted in place, everything else in the file is dropped."""
    out = [f"/* {os.path.relpath(path, root)} */"]
    label, offset = None, 0
    full = out
    section = ".data"
    open_img = []  # the image label whose end label is still to be placed

    def close_image():
        if open_img:
            full.append(f"{open_img.pop()}:")

    if only:
        out = []
    for raw in open(path):
        line = re.sub(r"^/\*.*?\*/", "", raw.strip()).strip()
        if not line:
            continue
        if line.startswith(".include") or line.startswith(".set"):
            continue
        m = re.match(r"^(?:dlabel|glabel|jlabel)\s+(\S+)", line)
        if m:
            label, offset = m.group(1), 0
            close_image()
            if only:
                out = full if label in only else []
                if label in only:
                    out.append(".balign 4")
            if psx_addr and (not only or label in only):
                if label not in psx_addr:
                    raise SystemExit(f"{path}: {label} is not in the map")
                n = next(psx_labels)
                out += [f".globl {label}", f".set {label}, 0x{psx_addr[label]:08X}",
                        f".Lpsx_img{n}:"]
                if section != ".bss":
                    psx_images.append((psx_addr[label], f".Lpsx_img{n}", f".Lpsx_end{n}", label))
                    open_img.append(f".Lpsx_end{n}")
                continue
            out += [f".globl {label}", f".type {label}, %object", f"{label}:"]
            continue
        m = DIRECTIVE.match(line)
        if not m:
            raise SystemExit(f"{path}: can't parse: {line}")
        op, args = m.group(1), m.group(2)
        if op == ".section":
            name = args.split(",")[0].strip()
            if name not in SECTIONS:
                raise SystemExit(f"{path}: unknown section {name}")
            close_image()
            section = SECTIONS[name]
            full.append(SECTIONS[name])
        elif op in (".data", ".bss", ".rodata", ".sdata", ".sbss"):
            close_image()
            section = SECTIONS[op]
            full.append(SECTIONS[op])
        elif op == ".size":
            if not psx_addr:  # absolute symbols have no size
                out.append(f".size {args}")
        elif op == ".align":
            out.append(f".balign {1 << int(args, 0)}")
        elif op == ".word":
            for w in [a.strip() for a in args.split(",")]:
                if re.match(r"^-?(0x[0-9A-Fa-f]+|\d+)$", w):
                    v = int(w, 0) & 0xFFFFFFFF
                    if 0x1F800000 <= v < 0x1F800400 and out is full:
                        scratch_relocs.append(f"{label}+0x{offset:X}")
                    if 0x80000000 <= v < 0x80200000:
                        key = f"{label}+0x{offset:X}"
                        if key not in allow and out is full:
                            problems.append(f"{os.path.relpath(path, root)}: {key} = 0x{v:08X}")
                out.append(f".long {w}")
                offset += 4
        elif op in (".short", ".half"):
            n = len(args.split(","))
            out.append(f".short {args}")
            offset += 2 * n
        elif op == ".byte":
            n = len(args.split(","))
            out.append(f".byte {args}")
            offset += n
        elif op == ".space":
            out.append(f".space {args}")
            offset += int(args.split(",")[0], 0)
        elif op in (".asciz", ".ascii"):
            out.append(f"{op} {args}")
            s = bytes(eval(args), "latin-1").decode("unicode_escape")  # noqa: S307
            offset += len(s) + (op == ".asciz")
        elif op == ".incbin":
            f = args.strip('"')
            out.append(f'.incbin "{os.path.join(root, f)}"')
            offset += os.path.getsize(os.path.join(root, f))
        else:
            raise SystemExit(f"{path}: unhandled directive {op}")
    close_image()
    return full


def read_map(path):
    """Symbol definitions in a GNU ld map: '   0x8006fcf4   D_8006FCF4'."""
    syms = {}
    for line in open(path):
        m = re.match(r"^\s+0x([0-9a-fA-F]{8})\s+([A-Za-z_][\w.]*)\s*$", line)
        if m:
            syms.setdefault(m.group(2), int(m.group(1), 16))
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, help="decomp checkout (paths are relative to it)")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--allow", help="abscheck allowlist")
    ap.add_argument("--map", help="psx.map: place every label at its PS1 address (D23)")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    allow = set()
    if a.allow:
        for line in open(a.allow):
            line = line.split("#")[0].strip()
            if line:
                allow.add(line)
    if a.map:
        psx_addr.update(read_map(a.map))
    problems = []
    lines = ["/* Generated by tools/data2s.py; do not edit. */"]
    for f in a.files:
        only = None
        if ":" in f:
            f, labels = f.split(":", 1)
            only = set(labels.split(","))
        lines += convert(os.path.join(a.root, f), a.root, allow, problems, only)
    if problems:
        sys.stderr.write("abscheck: raw words that look like PS1 RAM pointers:\n  "
                         + "\n  ".join(problems) + "\n")
        return 1
    # Start-up code (src/game/port.c) rebases these onto the port's scratchpad.
    lines += [".data", ".balign 4", ".globl s1_scratch_relocs", "s1_scratch_relocs:"]
    lines += [f".long {r}" for r in scratch_relocs] + [".long 0"]
    if a.map:
        # Bytes to copy: the image's own span, but never past the next label's
        # PS1 address (host and PS1 alignment padding can differ).
        addrs = sorted(set(psx_addr[lbl] for lbl in psx_addr))
        lines += [".data", ".balign 4", ".globl s1_psx_image", "s1_psx_image:"]
        for addr, start, end, label in psx_images:
            nxt = next((x for x in addrs if x > addr), addr + 0x100000)
            lines.append(f".long 0x{addr:08X}, {start}, {end} - {start}, 0x{nxt - addr:X}")
        lines.append(".long 0, 0, 0, 0")
    lines.append('.section .note.GNU-stack,"",%progbits')
    with open(a.out, "w") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

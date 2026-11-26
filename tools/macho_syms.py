#!/usr/bin/env python3
"""objcopy's symbol edits for a Mach-O relocatable object (macOS, D250 O4).

Apple's tools have no objcopy, and llvm-objcopy can't localize Mach-O
symbols. The presenter's and the 60 Hz rules' objects (cmake/presenter.cmake,
tools/tick60_link.py) need three edits, done here on the symbol table:

  macho_syms.py [--localize-symbols FILE] [--keep-global-symbol NAME]...
                [--redefine-sym OLD=NEW]... --ld LD IN.o OUT.o

  --localize-symbols FILE  every defined global named in FILE becomes local
  --keep-global-symbol N   every defined global but the N's becomes local
                           (commons and undefined symbols stay as they are,
                           as GNU objcopy leaves them)
  --redefine-sym OLD=NEW   a symbol, defined or not, renamed

Names are C's (no leading underscore). A symbol to localize is marked
private extern (N_PEXT) and the object linked again with `ld -r`, which turns
private externs into local symbols; renames append the new name to the
string table, which the relink then rewrites in order.
"""
import argparse
import os
import struct
import subprocess
import sys

MH_MAGIC_64 = 0xFEEDFACF
LC_SYMTAB = 0x2
N_STAB, N_PEXT, N_TYPE, N_EXT = 0xE0, 0x10, 0x0E, 0x01
N_SECT = 0x0E


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--localize-symbols")
    ap.add_argument("--keep-global-symbol", action="append", default=[])
    ap.add_argument("--redefine-sym", action="append", default=[])
    ap.add_argument("--ld", default="ld")
    ap.add_argument("--arch", default="arm64")
    ap.add_argument("inp")
    ap.add_argument("out")
    a = ap.parse_args()
    localize = set()
    if a.localize_symbols:
        localize = {"_" + l.strip() for l in open(a.localize_symbols) if l.strip()}
    keep = {"_" + n for n in a.keep_global_symbol}
    renames = {"_" + o: "_" + n for o, n in (r.split("=", 1) for r in a.redefine_sym)}

    data = bytearray(open(a.inp, "rb").read())
    magic, _cpu, _sub, _ftype, ncmds, _size, _flags, _res = struct.unpack_from("<8I", data, 0)
    if magic != MH_MAGIC_64:
        sys.exit(f"macho_syms: {a.inp}: not a 64-bit Mach-O object")
    off, symtab = 32, None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<2I", data, off)
        if cmd == LC_SYMTAB:
            symtab = off
        off += cmdsize
    if symtab is None:
        sys.exit(f"macho_syms: {a.inp}: no symbol table")
    symoff, nsyms, stroff, strsize = struct.unpack_from("<4I", data, symtab + 8)
    if stroff + strsize != len(data):
        sys.exit(f"macho_syms: {a.inp}: the string table isn't at the file's end")

    def name_at(strx):
        end = data.index(b"\0", stroff + strx)
        return data[stroff + strx:end].decode()

    extra = bytearray()
    marked = renamed = 0
    for i in range(nsyms):
        e = symoff + 16 * i
        strx, ntype = struct.unpack_from("<IB", data, e)
        if ntype & N_STAB:
            continue
        name = name_at(strx)
        defined_global = (ntype & N_TYPE) == N_SECT and (ntype & N_EXT)
        if defined_global and (name in localize or (keep and name not in keep)):
            data[e + 4] = ntype | N_PEXT
            marked += 1
        if name in renames:
            struct.pack_into("<I", data, e, strsize + len(extra))
            extra += renames[name].encode() + b"\0"
            renamed += 1
    while (strsize + len(extra)) % 8:
        extra += b"\0"
    struct.pack_into("<I", data, symtab + 20, strsize + len(extra))
    tmp = a.out + ".pext"
    with open(tmp, "wb") as f:
        f.write(data + extra)
    # ld -r: private externs become local; the symbol table comes out sorted
    subprocess.run([a.ld, "-r", "-arch", a.arch, "-o", a.out, tmp], check=True)
    os.remove(tmp)
    if renames and not renamed:
        sys.exit("macho_syms: no symbol to rename: " + ", ".join(o[1:] for o in renames))
    return 0


if __name__ == "__main__":
    sys.exit(main())

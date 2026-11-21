"""Check that create-fself will map an ELF's whole .bss (D180).

create-fself (OpenOrbis, v1.3) sizes the eboot's read-write PT_LOAD as
  (last .data* section's end - first .data* section's address) + align(.bss size, 16)
counting from the first .data* section (".data.rel.ro" excluded), and the PS4
maps that rounded up to 16 KB. It assumes .bss starts right where the data
sections end. When .bss is aligned further (a 16 KB-aligned array, say), the
gap is lost: the end of .bss lies outside the mapping, and a relocation that
points there makes the PS4's dynamic linker refuse the eboot
("reloc_non_plt() failed ... rv=8", PRX_INVALID_IMAGE, CE-34878-0; the P2
spike's first hardware run, logs/p2/hw-run2/klog.txt).

    python3 tools/ps4/oelf_check.py ELF      exit 1 if .bss would be cut short
"""
import struct
import sys

PAGE = 0x4000


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def sections(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        sys.exit(f"{path}: not a little-endian ELF64")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    raw = []
    for i in range(shnum):
        name, _type, _flags, addr, off, size = struct.unpack_from(
            "<IIQQQQ", data, shoff + i * shentsize)
        raw.append((name, addr, off, size))
    stroff = raw[shstrndx][2]

    def name_of(n):
        end = data.index(b"\0", stroff + n)
        return data[stroff + n:end].decode()

    return [(name_of(n), addr, size) for n, addr, _off, size in raw]


def main():
    path = sys.argv[1]
    secs = sections(path)
    dat = [s for s in secs if s[0].startswith(".data") and s[0] != ".data.rel.ro"]
    bss = next((s for s in secs if s[0] == ".bss"), None)
    if not dat or not bss:
        print(f"{path}: no .data* or no .bss section; nothing to check")
        return
    first, last = dat[0], dat[-1]
    memsz = (last[1] - first[1]) + last[2] + align(bss[2], 16)
    mapped_end = align(first[1] + memsz, PAGE)
    bss_end = bss[1] + bss[2]
    gap = bss[1] - (last[1] + last[2])
    print(f"oelf_check: data from 0x{first[1]:x}, .bss 0x{bss[1]:x}..0x{bss_end:x} "
          f"(gap after data 0x{gap:x}), create-fself maps to 0x{mapped_end:x}: "
          f"{'ok' if bss_end <= mapped_end else 'CUT SHORT'}")
    if bss_end > mapped_end:
        sys.exit(f"{path}: create-fself would map .bss only to 0x{mapped_end:x}, "
                 f"short of 0x{bss_end:x}; keep .bss alignment small (see this script)")


main()

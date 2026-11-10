#!/usr/bin/env python3
"""Pack a linked Vita ELF into a VPK, refusing a link that lost its process parameters. Standard library only.
From kh1-decomp tools/vita/build.py @ 85befa4a (package(), png_icon() and the link guard); vita/cmake/VpkApp.cmake
runs the same commands for CMake games (spyro-vita).

    vpk_pack.py pack --elf APP.elf --vpk OUT.vpk --title-id ABCD00001 --version 01.00 --name "Title"
                     [--file SRC=DEST]... [--placeholder-art] [--work DIR]
    vpk_pack.py check APP.elf        the process-parameter check alone, and the room after the text segment
    vpk_pack.py show OUT.vpk         param.sfo's values and the file list (name, size, crc32): diff two builds' output

The steps are vita-elf-create, vita-make-fself -s -c, vita-mksfoex with APP_VER, TITLE_ID and ATTRIBUTE2=12 (the
extended memory budget), and vita-pack-vpk with the sce_sys files. Before them the ELF must still define
sceUserMainThreadStackSize and _newlib_heap_size_user: the SDK reads both by name, nothing in the app refers to
them, so LTO drops them unless they are marked used (vita/runtime/procparam's VPK_PROCESS_PARAMS), and the app then
starts with a 256 KiB main stack and stops at launch without a log.

vita-elf-create puts its SCE data (module info, import tables: a few KB) after the text segment, and the data segment
starts at a 64 KiB boundary after it; when text ends closer to it than that, it fails "segment 1 overlaps" (gotchas
17). This script packs a linked ELF, so it can't add the room: link with -Wl,--defsym=__sce_headroom=0x4000
(VpkApp.cmake's SCE_HEADROOM). It says so when vita-elf-create fails that way, and warns before packing when the gap
is under 16 KiB, since the next change that grows or shrinks the text can hit it.

--placeholder-art writes a plain one-colour icon0.png, LiveArea bg.png, startup.png and template.xml into the work
folder and packs them, for a game with no art of its own. --file SRC=DEST adds a file (DEST is the path in the VPK,
sce_sys/icon0.png ...). vitasdk comes from $VITASDK (default /usr/local/vitasdk).

As a module: check_procparams(), text_gap(), sce_headroom(), pack(), placeholder_livearea(), read_sfo(), show().
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import zipfile
import zlib

PROCPARAMS = ("sceUserMainThreadStackSize", "_newlib_heap_size_user")
ATTRIBUTE2 = 12
HEADROOM = 0x4000  # what gotchas 17 links with (spyro-vita D32)
HEADROOM_FIX = ("link with -Wl,--defsym=__sce_headroom=0x%x (vitasdk's linker script keeps that much room after the "
                "text segment; VpkApp.cmake: SCE_HEADROOM 0x%x; vita/docs/gotchas.md 17)" % (HEADROOM, HEADROOM))


class PackError(Exception):
    pass


def sdk_bin(vitasdk=None):
    sdk = vitasdk or os.environ.get("VITASDK", "/usr/local/vitasdk")
    b = os.path.join(sdk, "bin")
    if not os.path.exists(os.path.join(b, "vita-elf-create")):
        raise PackError("no vitasdk at %s (set VITASDK; vita/tools/get_vitasdk.sh fetches one)" % sdk)
    return b + os.sep


def run(args, what):
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode:
        raise PackError("%s failed:\n%s%s" % (what, p.stdout[-3000:], p.stderr[-3000:]))
    return p


def check_procparams(elf, b=None):
    """The process-parameter symbols the linked ELF does not define (empty: both there). Only a line with an
    address counts: without the heap size newlib still leaves its weak reference, "         w _newlib_heap_size_user",
    which a check of the names alone takes for the symbol."""
    b = b or sdk_bin()
    out = run([b + "arm-vita-eabi-nm", elf], "arm-vita-eabi-nm").stdout
    defined = set(f[2] for f in (l.split() for l in out.splitlines()) if len(f) == 3)
    return [s for s in PROCPARAMS if s not in defined]


def text_gap(elf):
    """Bytes between the end of the ELF's first load segment (the text) and the start of the next (the data), where
    vita-elf-create puts its SCE data; None with fewer than two load segments."""
    with open(elf, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise PackError("%s is not a 32-bit little-endian ELF" % elf)
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    loads = []
    for i in range(phnum):
        p_type, _off, vaddr, _paddr, _filesz, memsz = struct.unpack_from("<6I", data, phoff + i * phentsize)
        if p_type == 1:  # PT_LOAD
            loads.append((vaddr, memsz))
    if len(loads) < 2:
        return None
    loads.sort()
    return loads[1][0] - (loads[0][0] + loads[0][1])


def sce_headroom(elf, b=None):
    """__sce_headroom's value when the link defined it (-Wl,--defsym=__sce_headroom=...), else None."""
    b = b or sdk_bin()
    for line in run([b + "arm-vita-eabi-nm", elf], "arm-vita-eabi-nm").stdout.splitlines():
        f = line.split()
        if len(f) == 3 and f[2] == "__sce_headroom":
            return int(f[0], 16)
    return None


def png_plain(path, w, h, rgb):
    """A plain one-colour 8-bit paletted PNG (the LiveArea wants indexed colour)."""
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(w) for _ in range(h))
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)) \
        + chunk(b"PLTE", bytes(rgb)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def placeholder_livearea(sysdir, icon=(0x20, 0x30, 0x80), bg=(0x10, 0x18, 0x40), gate=(0x20, 0x30, 0x80)):
    """Plain sce_sys art in sysdir (made here, nothing committed); the (src, dest) pairs to pack."""
    contents = os.path.join(sysdir, "livearea", "contents")
    os.makedirs(contents, exist_ok=True)
    png_plain(os.path.join(sysdir, "icon0.png"), 128, 128, icon)
    png_plain(os.path.join(contents, "bg.png"), 840, 500, bg)
    png_plain(os.path.join(contents, "startup.png"), 280, 158, gate)
    with open(os.path.join(contents, "template.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<livearea style="a1" format-ver="01.00" content-rev="1">\n'
                '  <livearea-background><image>bg.png</image></livearea-background>\n'
                '  <gate><startup-image>startup.png</startup-image></gate>\n</livearea>\n')
    return [(os.path.join(sysdir, f), "sce_sys/" + f) for f in
            ("icon0.png", "livearea/contents/bg.png", "livearea/contents/startup.png",
             "livearea/contents/template.xml")]


def pack(elf, vpk, title_id, version, name, files=(), work=None, b=None):
    """elf -> work/<elf>.velf, work/eboot.bin, work/param.sfo -> vpk (files: (src, dest) pairs). Raises PackError."""
    if not re.fullmatch(r"[A-Z]{4}[0-9]{5}", title_id):
        raise PackError("title id %r is not four capitals and five digits (ABCD00001)" % title_id)
    if not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", version):
        raise PackError("version %r is not ##.##" % version)
    b = b or sdk_bin()
    lost = check_procparams(elf, b)
    if lost:
        raise PackError("%s missing from the link (dropped by LTO? define them with VPK_PROCESS_PARAMS, "
                        "vita/runtime/procparam, which marks them used)" % " ".join(lost))
    gap = text_gap(elf)
    if gap is not None and gap < HEADROOM:
        sys.stderr.write("vpk: warning: %s's text segment ends %d bytes before its data segment; vita-elf-create "
                         "needs a few KB there, and a change to the text can take them away: %s\n"
                         % (elf, gap, HEADROOM_FIX))
    work = work or os.path.dirname(os.path.abspath(vpk))
    os.makedirs(work, exist_ok=True)
    velf = os.path.join(work, os.path.splitext(os.path.basename(elf))[0] + ".velf")
    try:
        run([b + "vita-elf-create", elf, velf], "vita-elf-create")
    except PackError as e:
        if "overlaps" in str(e):
            raise PackError("%s\nNo room for the SCE data after the text segment: %s" % (e, HEADROOM_FIX))
        raise
    eboot = os.path.join(work, "eboot.bin")
    run([b + "vita-make-fself", "-s", "-c", velf, eboot], "vita-make-fself")
    sfo = os.path.join(work, "param.sfo")
    run([b + "vita-mksfoex", "-s", "APP_VER=" + version, "-s", "TITLE_ID=" + title_id,
         "-d", "ATTRIBUTE2=%d" % ATTRIBUTE2, name, sfo], "vita-mksfoex")
    if os.path.exists(vpk):
        os.remove(vpk)
    args = [b + "vita-pack-vpk", "-s", sfo, "-b", eboot]
    for src, dest in files:
        if not os.path.isfile(src):
            raise PackError("no %s to pack as %s" % (src, dest))
        args += ["-a", "%s=%s" % (src, dest)]
    run(args + [vpk], "vita-pack-vpk")
    return vpk


def read_sfo(data):
    """param.sfo's entries as {key: str or int}."""
    magic, _ver, keys, vals, n = struct.unpack_from("<4sIIII", data, 0)
    if magic != b"\0PSF":
        raise PackError("not a param.sfo")
    out = {}
    for i in range(n):
        koff, fmt, length, _maxlen, voff = struct.unpack_from("<HHIII", data, 20 + 16 * i)
        key = data[keys + koff:data.index(b"\0", keys + koff)].decode()
        raw = data[vals + voff:vals + voff + length]
        out[key] = struct.unpack("<I", raw)[0] if fmt == 0x0404 else raw.rstrip(b"\0").decode("utf-8", "replace")
    return out


def show(vpk):
    """Lines describing a VPK: its param.sfo values, then each file's name, size and crc32."""
    lines = []
    with zipfile.ZipFile(vpk) as z:
        infos = sorted(z.infolist(), key=lambda i: i.filename)
        sfo = read_sfo(z.read("sce_sys/param.sfo"))
        for k in sorted(sfo):
            lines.append("sfo %s=%s" % (k, sfo[k]))
        for i in infos:
            lines.append("file %s %d %08x" % (i.filename, i.file_size, i.CRC))
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--elf", required=True)
    p.add_argument("--vpk", required=True)
    p.add_argument("--title-id", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--file", action="append", default=[], metavar="SRC=DEST")
    p.add_argument("--placeholder-art", action="store_true")
    p.add_argument("--work", help="folder for the .velf, eboot.bin, param.sfo (default: the VPK's)")
    sub.add_parser("check").add_argument("elf")
    sub.add_parser("show").add_argument("vpk")
    a = ap.parse_args()
    try:
        if a.cmd == "check":
            lost = check_procparams(a.elf)
            if lost:
                print("vpk: %s missing from %s" % (" ".join(lost), a.elf))
                return 1
            print("vpk: %s has %s" % (a.elf, " and ".join(PROCPARAMS)))
            gap, room = text_gap(a.elf), sce_headroom(a.elf)
            if gap is not None:
                print("vpk: %d bytes between the text and data segments, __sce_headroom %s%s"
                      % (gap, "0x%x" % room if room is not None else "not set",
                         "" if gap >= HEADROOM else " (under 16 KiB: %s)" % HEADROOM_FIX))
        elif a.cmd == "show":
            print("\n".join(show(a.vpk)))
        else:
            files = []
            work = a.work or os.path.dirname(os.path.abspath(a.vpk))
            if a.placeholder_art:
                files += placeholder_livearea(os.path.join(work, "sce_sys"))
            for f in a.file:
                src, sep, dest = f.partition("=")
                if not sep:
                    ap.error("--file wants SRC=DEST: %s" % f)
                files.append((src, dest))
            pack(a.elf, a.vpk, a.title_id, a.version, a.name, files, work)
            print("vpk: %s" % a.vpk)
    except PackError as e:
        print("vpk: %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

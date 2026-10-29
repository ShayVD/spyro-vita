#!/usr/bin/env python3
"""Extract the files from a PS1 MODE2/2352 disc image (single data track).

Form 1 files are written as plain 2048-byte-per-sector data. XA / Form 2 files
(streamed audio or video) are written as 2336-byte sectors (subheader + data),
the same layout dumpsxiso uses. A manifest.json records each file's LBA, size
and type.

Usage: python3 -I tools/extract_disc.py <image.bin> <out_dir>
"""
import json
import os
import struct
import sys

RAW = 2352
SYNC = b"\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00"

XA_FORM2 = 0x1000
XA_INTERLEAVED = 0x2000
XA_CDDA = 0x4000


def read_raw(f, lba):
    f.seek(lba * RAW)
    sector = f.read(RAW)
    if len(sector) != RAW or sector[:12] != SYNC:
        raise ValueError(f"bad sector at LBA {lba}")
    return sector


def read_form1(f, lba, count=1):
    return b"".join(read_raw(f, lba + i)[24:24 + 2048] for i in range(count))


def parse_dir(f, lba, size):
    data = read_form1(f, lba, (size + 2047) // 2048)[:size]
    entries, pos = [], 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:
            pos = (pos // 2048 + 1) * 2048
            continue
        rec = data[pos:pos + rec_len]
        name_len = rec[32]
        name = rec[33:33 + name_len]
        sys_use = rec[33 + name_len + (1 - name_len % 2):]
        xa_attr = 0
        if len(sys_use) >= 14 and sys_use[6:8] == b"XA":
            xa_attr = struct.unpack(">H", sys_use[4:6])[0]
        if name not in (b"\x00", b"\x01"):
            entries.append({
                "name": name.decode("ascii").split(";")[0],
                "lba": struct.unpack("<I", rec[2:6])[0],
                "size": struct.unpack("<I", rec[10:14])[0],
                "is_dir": bool(rec[25] & 0x02),
                "xa_attr": xa_attr,
            })
        pos += rec_len
    return entries


def sector_is_form2(f, lba):
    return bool(read_raw(f, lba)[18] & 0x20)


def extract(image, out_dir):
    manifest = []
    with open(image, "rb") as f:
        pvd = read_form1(f, 16)
        if pvd[1:6] != b"CD001":
            raise ValueError("no ISO9660 primary volume descriptor at LBA 16")
        root = pvd[156:156 + 34]
        stack = [("", struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0])]
        while stack:
            rel, lba, size = stack.pop()
            os.makedirs(os.path.join(out_dir, rel), exist_ok=True)
            for e in parse_dir(f, lba, size):
                path = os.path.join(rel, e["name"])
                if e["is_dir"]:
                    stack.append((path, e["lba"], e["size"]))
                    continue
                if e["xa_attr"] & XA_CDDA:
                    raise ValueError(f"{path}: CD-DA files are not supported")
                count = (e["size"] + 2047) // 2048
                xa = bool(e["xa_attr"] & (XA_FORM2 | XA_INTERLEAVED)) or (
                    count > 0 and sector_is_form2(f, e["lba"]))
                with open(os.path.join(out_dir, path), "wb") as out:
                    if xa:
                        for i in range(count):
                            out.write(read_raw(f, e["lba"] + i)[16:RAW])
                    else:
                        out.write(read_form1(f, e["lba"], count)[:e["size"]])
                manifest.append({"path": path, "lba": e["lba"], "size": e["size"],
                                 "type": "xa" if xa else "data"})
    manifest.sort(key=lambda m: m["lba"])
    with open(os.path.join(out_dir, "manifest.json"), "w") as m:
        json.dump(manifest, m, indent=2)
    return manifest


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    for m in extract(sys.argv[1], sys.argv[2]):
        print(f'{m["lba"]:>7}  {m["type"]:<4}  {m["size"]:>11}  {m["path"]}')

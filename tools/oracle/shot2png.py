#!/usr/bin/env python3
"""Convert the oracle's display screenshots (capture.lua shot-V.raw: width,
height, bpp as u32, then pixels: 2 bytes a pixel is PS1 15-bit BGR, 3 is RGB) to
PNG. Usage: shot2png.py FILE.raw... (writes FILE.png next to each)."""
import struct
import sys
import zlib


def png(path, w, h, rows):
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + r for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


for p in sys.argv[1:]:
    d = open(p, "rb").read()
    w, h, bpp = struct.unpack_from("<III", d)
    px = d[12:]
    rows = []
    if len(px) == w * h * 2:  # bpp is an enum here (0: 16-bit), so go by size
        for y in range(h):
            r = bytearray()
            for (v,) in struct.iter_unpack("<H", px[y * w * 2:(y + 1) * w * 2]):
                r += bytes(((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3))
            rows.append(bytes(r))
    else:
        stride = len(px) // h
        rows = [px[y * stride:y * stride + w * 3] for y in range(h)]
    png(p[:-4] + ".png", w, h, rows)
    print(p[:-4] + ".png", w, h, bpp)

#!/usr/bin/env python3
"""Tile a run's screenshots (shot-<vblank>.png, as written by the linux32
host) into one PNG, in vblank order.
   tools/contact_sheet.py DIR [OUT.png] [--from V] [--to V]"""
import glob
import re
import struct
import sys
import zlib


def read_png(path):
    d = open(path, "rb").read()
    i, w, h, idat = 8, 0, 0, b""
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        t, c = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        i += 12 + n
        if t == b"IHDR":
            w, h = struct.unpack(">II", c[:8])
        elif t == b"IDAT":
            idat += c
    raw, st = zlib.decompress(idat), w * 3
    return w, h, [raw[y * (st + 1) + 1:(y + 1) * (st + 1)] for y in range(h)]


def write_png(path, w, h, rgb):
    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main(d, out=None, lo=0, hi=1 << 30, tw=256, th=120, cols=6):
    vb = lambda p: int(re.findall(r"(\d+)", p)[-1])
    files = sorted((f for f in glob.glob(f"{d}/shot-*.png") if lo <= vb(f) <= hi), key=vb)
    rows = (len(files) + cols - 1) // cols
    W, H = cols * tw, rows * th
    img = bytearray(W * H * 3)
    for k, f in enumerate(files):
        w, h, px = read_png(f)
        ox, oy = (k % cols) * tw, (k // cols) * th
        for y in range(th):
            src = px[y * h // th]
            for x in range(tw):
                o, s = ((oy + y) * W + ox + x) * 3, (x * w // tw) * 3
                img[o:o + 3] = src[s:s + 3]
    write_png(out or f"{d}/contact.png", W, H, img)
    print(f"{len(files)} shots -> {out or d + '/contact.png'}")


if __name__ == "__main__":
    args, lo, hi = sys.argv[1:], 0, 1 << 30
    for flag in ("--from", "--to"):
        if flag in args:
            i = args.index(flag)
            v = int(args[i + 1])
            del args[i:i + 2]
            lo, hi = (v, hi) if flag == "--from" else (lo, v)
    main(args[0], args[1] if len(args) > 1 else None, lo, hi)

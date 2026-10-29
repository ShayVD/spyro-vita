#!/usr/bin/env python3
"""Decode a texture from a raw VRAM dump (`spyro --vram-shot` writes vram-N.bin).
   tools/vram.py VRAM.bin PAGE CLUT U0,V0 W,H OUT.png [--scale N]
PAGE and CLUT are the hex tpage/clut words of a primitive (GP0 layout)."""
import struct, sys, zlib

def png(path, w, h, rgb):
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

args = sys.argv[1:]
scale = 1
if "--scale" in args:
    i = args.index("--scale"); scale = int(args[i + 1]); del args[i:i + 2]
vram = struct.unpack("<524288H", open(args[0], "rb").read())
page, clut = int(args[1], 16), int(args[2], 16)
u0, v0 = map(int, args[3].split(",")); w, h = map(int, args[4].split(","))
fmt, bx, by = (page >> 7) & 3, (page & 15) * 64, ((page >> 4) & 1) * 256
cx, cy = (clut & 0x3F) * 16, (clut >> 6) & 511
def texel(u, v):
    row = ((by + v) & 511) * 1024
    if fmt == 0:
        wd = vram[row + ((bx + (u >> 2)) & 1023)]; return vram[cy * 1024 + cx + ((wd >> ((u & 3) * 4)) & 15)]
    if fmt == 1:
        wd = vram[row + ((bx + (u >> 1)) & 1023)]; return vram[cy * 1024 + cx + ((wd >> ((u & 1) * 8)) & 255)]
    return vram[row + ((bx + u) & 1023)]
rgb = []
for y in range(h * scale):
    for x in range(w * scale):
        t = texel((u0 + x // scale) & 255, (v0 + y // scale) & 255)
        rgb += [(t & 31) << 3, ((t >> 5) & 31) << 3, ((t >> 10) & 31) << 3]
png(args[5], w * scale, h * scale, rgb)

#!/usr/bin/env python3
"""Placeholder LiveArea icon for the Vita build: 128x128, 8-bit indexed PNG
(the Vita wants indexed PNGs, decisions D7), a purple gradient with "SPYRO
VITA" in a blocky pixel font. Original art; no game assets. The real LiveArea
is M5.

  make_icon.py out.png
"""
import struct
import sys
import zlib

FONT = {  # 5x7
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
}
W = H = 128
GRAD = 32  # palette 0..31: gradient; 32: text; 33: text shadow


def main():
    pal = []
    for i in range(GRAD):
        t = i / (GRAD - 1)
        pal.append((int(40 + 60 * t), int(10 + 20 * t), int(90 + 90 * t)))
    pal += [(255, 200, 40), (30, 0, 40)]
    px = [[y * GRAD // H for _ in range(W)] for y in range(H)]

    def text(s, y0, scale):
        w = len(s) * 6 * scale - scale
        x0 = (W - w) // 2
        for k, ch in enumerate(s):
            for r, row in enumerate(FONT[ch]):
                for c, bit in enumerate(row):
                    if bit == "1":
                        for dy in range(scale):
                            for dx in range(scale):
                                x = x0 + (k * 6 + c) * scale + dx
                                y = y0 + r * scale + dy
                                if y + 2 < H and x + 2 < W:
                                    px[y + 2][x + 2] = GRAD + 1
                                px[y][x] = GRAD

    text("SPYRO", 30, 4)
    text("VITA", 76, 4)
    raw = b"".join(b"\0" + bytes(row) for row in px)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", b"".join(bytes(c) for c in pal))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(sys.argv[1], "wb").write(png)


if __name__ == "__main__":
    main()

"""A plain 512x512 grey PNG for the spike's icon0.png (no art; D180).
   python3 icon.py OUT.png"""
import struct
import sys
import zlib


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def main():
    w = h = 512
    row = b"\x00" + bytes((0x40, 0x48, 0x50)) * w
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(row * h, 9))
    png += chunk(b"IEND", b"")
    with open(sys.argv[1], "wb") as f:
        f.write(png)


main()

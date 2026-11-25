#!/usr/bin/env python3
"""The release fpkg's icon from the Vita's LiveArea icon (PS4 plan P5, D246).

    tools/ps4/art.py SRC_DIR OUT_DIR

SRC_DIR/icon0.png (square) becomes OUT_DIR/icon0.png at 512x512, an 8-bit RGB PNG, the same bytes on every run.
No background (pic1.png): on the owner's PS4 it stayed over the game's GPU picture (release run 1, D246). The
input is 8-bit RGB, RGBA or palette, not interlaced: assets/livearea/ (committed, indexed) or the gitignored
disc/livearea/ (RGB, the better source), as the Vita build picks it (src/platform/vita/CMakeLists.txt). Which
images may be used is the owner's decision (assets/livearea/README.md, D59, D246); this only rescales them.
Plain Python (no Pillow), so the Mac and the Docker image give the same files.
"""
import os
import struct
import sys
import zlib

SIG = b"\x89PNG\r\n\x1a\n"


def read_png(path):
    """(w, h, rows): rows of RGB bytes from an 8-bit, non-interlaced RGB, RGBA or palette PNG."""
    d = open(path, "rb").read()
    if d[:8] != SIG:
        raise SystemExit(f"{path}: not a PNG")
    pos, idat, w, h, bpp, ctype, pal = 8, b"", 0, 0, 0, 0, None
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype, _, _, lace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 3, 6) or lace:
                raise SystemExit(f"{path}: wants 8-bit RGB, RGBA or palette, not interlaced")
            bpp = {2: 3, 3: 1, 6: 4}[ctype]
        elif t == b"PLTE":
            pal = body
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    rows = []
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if ctype == 2:
            rows.append(bytes(line))
        elif ctype == 6:  # alpha onto black
            rows.append(bytes(line[i] * line[i - i % 4 + 3] // 255 for i in range(stride) if i % 4 != 3))
        else:
            rows.append(b"".join(pal[3 * p:3 * p + 3] for p in line))
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    raw = b"".join(b"\x00" + r for r in rows)
    png = SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path + ".tmp", "wb") as f:
        f.write(png)
    os.replace(path + ".tmp", path)


def cubic(t):
    """Catmull-Rom weights for the four taps around a sample, t in [0, 1)."""
    t2, t3 = t * t, t * t * t
    return (-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1, -1.5 * t3 + 2 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2)


def taps(n_in, n_out):
    """For each output index, four (input index, weight) pairs (pixel centres, edges clamped)."""
    out = []
    for o in range(n_out):
        x = (o + 0.5) * n_in / n_out - 0.5
        i = int(x // 1)
        ws = cubic(x - i)
        out.append([(min(max(i - 1 + k, 0), n_in - 1), ws[k]) for k in range(4)])
    return out


def resize(w, h, rows, nw, nh):
    """Separable Catmull-Rom (an enlargement: the sources are smaller than the PS4's sizes)."""
    if nw < w or nh < h:
        raise SystemExit("art.py only enlarges")
    tx = taps(w, nw)
    mid = []
    for r in rows:
        line = bytearray(nw * 3)
        for o, tp in enumerate(tx):
            for c in range(3):
                v = sum(r[i * 3 + c] * k for i, k in tp)
                line[o * 3 + c] = min(max(int(v + 0.5), 0), 255)
        mid.append(line)
    ty = taps(h, nh)
    out = []
    for tp in ty:
        line = bytearray(nw * 3)
        (i0, k0), (i1, k1), (i2, k2), (i3, k3) = tp
        r0, r1, r2, r3 = mid[i0], mid[i1], mid[i2], mid[i3]
        for x in range(nw * 3):
            v = r0[x] * k0 + r1[x] * k1 + r2[x] * k2 + r3[x] * k3
            line[x] = min(max(int(v + 0.5), 0), 255)
        out.append(bytes(line))
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    w, h, rows = read_png(os.path.join(src, "icon0.png"))
    if w != h:
        raise SystemExit(f"{src}/icon0.png: {w}x{h}, not square")
    write_png(os.path.join(out, "icon0.png"), 512, 512, resize(w, h, rows, 512, 512))
    print(f"art: icon0.png 512x512 from {src}")

if __name__ == "__main__":
    main()

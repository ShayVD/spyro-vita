#!/usr/bin/env python3
"""Compare a candidate run's pictures with a baseline run's, by file name: the port autopilot's picture A/B.
  shots_cmp.py BASE_DIR NEW_DIR [--glob '*.png'] [--max-diff 0.0] [--tol 0] [--edge 0]
A picture passes when its bytes are identical, or when the fraction of pixels whose R, G or B differs by more than
TOL (inside a border of EDGE pixels left out) is at most MAX_DIFF. Vita3K window captures differ run to run at the
edges (targets/vita/docs/gotchas.md 7): give --edge, and set --max-diff from two runs of the same build. Reads 8-bit RGB and
RGBA PNGs, not interlaced (what Vita3K captures and the ports' shots write). Standard library only.
Exit 0 when every BASE picture passes; 1 otherwise (a picture over the limit, a size change, a missing picture,
a picture that cannot be decoded, an edge that leaves no pixels, or no BASE picture matching the glob).
  shots_cmp.py --one-colour R,G,B PNG [--tol 0]
Exit 0 when every pixel of PNG is R,G,B (each channel within TOL); 1 otherwise or when it cannot be decoded.
v3k_run.sh CAPTURE=1 uses it to tell a one-colour window grab that is the game's frame (a black fade, as its VRAM
shot) from one that is not.
"""
import argparse
import fnmatch
import hashlib
import os
import struct
import sys
import zlib


def read_png(path):
    """(width, height, bytes per pixel, rows of unfiltered bytes)"""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w, h, bpp = 8, b"", 0, 0, 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: only 8-bit RGB or RGBA, not interlaced")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if not w or not h or not bpp:
        raise ValueError(f"{path}: no IHDR chunk")
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif f != 0:
                raise ValueError(f"{path}: filter type {f}")
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows


def diff_fraction(a, b, tol, edge):
    """(differing pixels, pixels compared); None when the sizes differ"""
    aw, ah, abpp, arows = a
    bw, bh, bbpp, brows = b
    if (aw, ah) != (bw, bh):
        return None
    diff = total = 0
    for y in range(edge, ah - edge):
        ra, rb = arows[y], brows[y]
        for x in range(edge, aw - edge):
            total += 1
            pa, pb = x * abpp, x * bbpp
            if any(abs(ra[pa + k] - rb[pb + k]) > tol for k in range(3)):
                diff += 1
    return diff, total


def sha1(path):
    with open(path, "rb") as fh:
        return hashlib.sha1(fh.read()).hexdigest()


def one_colour(argv):
    ap = argparse.ArgumentParser(prog="shots_cmp.py --one-colour")
    ap.add_argument("--one-colour", required=True)
    ap.add_argument("png")
    ap.add_argument("--tol", type=int, default=0)
    a = ap.parse_args(argv)
    try:
        want = [int(x) for x in a.one_colour.split(",")]
        if len(want) != 3:
            raise ValueError
    except ValueError:
        ap.error("--one-colour takes R,G,B")
    try:
        w, h, bpp, rows = read_png(a.png)
    except (OSError, ValueError, zlib.error, struct.error, IndexError) as e:
        print(f"bad {a.png}: {e}")
        return 1
    for y, row in enumerate(rows):
        for x in range(w):
            p = row[x * bpp:x * bpp + 3]
            if any(abs(p[k] - want[k]) > a.tol for k in range(3)):
                print(f"not one colour: ({x},{y}) is {tuple(p)}, not {tuple(want)}")
                return 1
    print(f"one colour {tuple(want)}: {w}x{h}")
    return 0


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if "--one-colour" in argv:
        return one_colour(argv)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base")
    ap.add_argument("new")
    ap.add_argument("--glob", default="*.png")
    ap.add_argument("--max-diff", type=float, default=0.0)
    ap.add_argument("--tol", type=int, default=0)
    ap.add_argument("--edge", type=int, default=0)
    a = ap.parse_args(argv)
    if a.edge < 0:
        ap.error("--edge must be 0 or more")
    names = sorted(n for n in os.listdir(a.base) if fnmatch.fnmatch(n, a.glob))
    if not names:
        print(f"nothing to compare: no {a.glob} pictures in {a.base}")
        return 1
    same = within = over = missing = 0
    for n in names:
        bp, np_ = os.path.join(a.base, n), os.path.join(a.new, n)
        if not os.path.exists(np_):
            print(f"missing {n}")
            missing += 1
            continue
        if sha1(bp) == sha1(np_):
            same += 1
            continue
        try:
            r = diff_fraction(read_png(bp), read_png(np_), a.tol, a.edge)
        except (ValueError, zlib.error, struct.error, IndexError) as e:
            print(f"bad {n}: {e}")
            over += 1
            continue
        if r is None:
            print(f"size {n}")
            over += 1
            continue
        d, t = r
        if not t:
            print(f"over {n} edge leaves no pixels")
            over += 1
            continue
        frac = d / t
        if frac <= a.max_diff:
            within += 1
            print(f"within {n} {frac:.5f} ({d} px)")
        else:
            over += 1
            print(f"over {n} {frac:.5f} ({d} px)")
    for n in sorted(set(os.listdir(a.new)) - set(names)):
        if fnmatch.fnmatch(n, a.glob):
            print(f"extra {n}")
    print(f"shots_cmp: {same} same, {within} within, {over} over, {missing} missing")
    return 1 if over or missing else 0


if __name__ == "__main__":
    sys.exit(main())

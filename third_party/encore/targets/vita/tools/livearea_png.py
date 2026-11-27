#!/usr/bin/env python3
"""The Vita's rules for a VPK's sce_sys art, checked, and a player's own art converted to fit them.

The Vita wants sce_sys PNGs 8-bit indexed (palette, colour type 3) at fixed sizes: the bubble icon
sce_sys/icon0.png 128x128, the LiveArea background sce_sys/livearea/contents/bg.png 840x500 and the gate
sce_sys/livearea/contents/startup.png 280x158. Plain Python (no Pillow); the output is the same on every run.

  livearea_png.py check PNG WxH [PNG WxH ...]
      fail unless each PNG is WxH and 8-bit indexed
  livearea_png.py from DIR OUTDIR
      DIR/icon0.png, DIR/bg.png and DIR/startup.png (8-bit RGB, RGBA or palette, not interlaced, already at the
      sizes above) converted to 8-bit indexed PNGs in OUTDIR (median cut to 256 colours, 4x4 ordered dither)
  livearea_png.py vpk VPK [--livearea]
      check a finished VPK: eboot.bin and sce_sys/param.sfo present and not empty, sce_sys/icon0.png present, and
      every sce_sys PNG above that is present at its size and 8-bit indexed; --livearea also requires bg.png,
      startup.png and livearea/contents/template.xml

Exit status 0 when everything holds; the problems go to stderr. Drawing the default art is the game's (it must be
plainly original: no logos, fonts or box art traced); vpk_pack.py --placeholder-art writes plain one-colour art.
From spyro-vita tools/vita/make_livearea.py @ bb62f38 (its PNG half: quantize, write_png, read_png, check_png;
decisions D55, D59) and the VPK check in its tools/ci/build_vpk.sh (D57).
"""
import os
import struct
import sys
import zipfile
import zlib

SIZES = {"icon0.png": (128, 128), "bg.png": (840, 500), "startup.png": (280, 158)}
IN_VPK = {"icon0.png": "sce_sys/icon0.png", "bg.png": "sce_sys/livearea/contents/bg.png",
          "startup.png": "sce_sys/livearea/contents/startup.png"}
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
SIG = b"\x89PNG\r\n\x1a\n"


def check_png_bytes(d, size, name):
    """The rule for sce_sys art: 8-bit indexed at size. None, or what is wrong."""
    if d[:8] != SIG or d[12:16] != b"IHDR":
        return f"{name}: not a PNG"
    w, h, depth, ctype = struct.unpack(">IIBB", d[16:26])
    if (w, h) != tuple(size):
        return f"{name}: {w}x{h}, wants {size[0]}x{size[1]}"
    if depth != 8 or ctype != 3:
        return f"{name}: bit depth {depth}, colour type {ctype}; wants 8-bit indexed (colour type 3)"
    return None


def read_png(path):
    """(w, h, pixels) from an 8-bit, non-interlaced RGB, RGBA or palette PNG; alpha is dropped onto black."""
    d = open(path, "rb").read()
    if d[:8] != SIG:
        raise SystemExit(f"{path}: not a PNG")
    pos, idat, w, h, bpp, pal = 8, b"", 0, 0, 0, None
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype, _, _, lace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 3, 6) or lace:
                raise SystemExit(f"{path}: wants 8-bit RGB, RGBA or palette, not interlaced "
                                 f"(depth {depth}, colour type {ctype}, interlace {lace})")
            bpp = {2: 3, 3: 1, 6: 4}[ctype]
        elif t == b"PLTE":
            pal = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    px = []
    prev = bytearray(stride)
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
        for x in range(w):
            p = pal[line[x]] if bpp == 1 else line[x * bpp:x * bpp + bpp]
            k = p[3] / 255 if bpp == 4 else 1.0
            px.append(tuple(max(0, min(255, int(round(p[j] * k)))) for j in range(3)))
        prev = line
    return w, h, px


def quantize(w, px, n=256):
    """Median cut on a 5-bit-per-channel histogram; returns (palette, indices)."""
    hist = {}
    for c in px:
        hist[c] = hist.get(c, 0) + 1
    if len(hist) <= n:
        pal = sorted(hist)
        idx = {c: i for i, c in enumerate(pal)}
        return pal, [idx[c] for c in px]
    keys = {}
    for c, k in hist.items():
        keys.setdefault((c[0] >> 3, c[1] >> 3, c[2] >> 3), []).append((c, k))
    boxes = [sorted(keys)]
    while len(boxes) < n:
        def spread(b):
            return max(max(q[i] for q in b) - min(q[i] for q in b) for i in range(3))
        cand = [b for b in boxes if len(b) > 1]
        if not cand:
            break
        b = max(cand, key=lambda b: (spread(b), len(b)))
        boxes.remove(b)
        axis = max(range(3), key=lambda i: max(q[i] for q in b) - min(q[i] for q in b))
        b.sort(key=lambda q: (q[axis], q))
        total = sum(k for q in b for _, k in keys[q])
        acc, cut = 0, 1
        for j, q in enumerate(b[:-1]):
            acc += sum(k for _, k in keys[q])
            cut = j + 1
            if acc * 2 >= total:
                break
        boxes += [b[:cut], b[cut:]]
    boxes.sort()
    pal, qidx = [], {}
    for i, b in enumerate(boxes):
        tot, cnt = [0, 0, 0], 0
        for q in b:
            qidx[q] = i
            for c, k in keys[q]:
                cnt += k
                for j in range(3):
                    tot[j] += c[j] * k
        pal.append(tuple(int(round(t / cnt)) for t in tot))
    # a 4x4 ordered dither, so gradients don't band
    out = []
    for i, c in enumerate(px):
        d = BAYER[(i // w) % 4][i % w % 4] - 8
        q = tuple(min(31, max(0, (v + d) >> 3)) for v in c)
        out.append(qidx[q] if q in qidx else qidx[(c[0] >> 3, c[1] >> 3, c[2] >> 3)])
    return pal, out


def write_png(w, h, px, path):
    pal, idx = quantize(w, px)
    raw = b"".join(b"\0" + bytes(idx[y * w:(y + 1) * w]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

    png = SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", b"".join(bytes(c) for c in pal))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def report(errs):
    for e in errs:
        print(e, file=sys.stderr)
    return 1 if errs else 0


def main(argv):
    if len(argv) >= 4 and argv[1] == "check" and len(argv) % 2 == 0:
        errs = []
        for path, size in zip(argv[2::2], argv[3::2]):
            e = check_png_bytes(open(path, "rb").read(), tuple(int(v) for v in size.split("x")), path)
            if e:
                errs.append(e)
        return report(errs)
    if len(argv) == 4 and argv[1] == "from":
        os.makedirs(argv[3], exist_ok=True)
        for name, size in SIZES.items():
            w, h, px = read_png(os.path.join(argv[2], name))
            if (w, h) != size:
                raise SystemExit(f"{argv[2]}/{name}: {w}x{h}, wants {size[0]}x{size[1]}")
            write_png(w, h, px, os.path.join(argv[3], name))
        return 0
    if len(argv) in (3, 4) and argv[1] == "vpk" and (len(argv) == 3 or argv[3] == "--livearea"):
        errs = []
        with zipfile.ZipFile(argv[2]) as z:
            names = set(z.namelist())
            need = ["eboot.bin", "sce_sys/param.sfo", IN_VPK["icon0.png"]]
            if len(argv) == 4:
                need += [IN_VPK["bg.png"], IN_VPK["startup.png"], "sce_sys/livearea/contents/template.xml"]
            for f in need:
                if f not in names or z.getinfo(f).file_size == 0:
                    errs.append(f"{argv[2]}: {f} missing or empty")
            for name, size in SIZES.items():
                if IN_VPK[name] in names and z.getinfo(IN_VPK[name]).file_size:
                    e = check_png_bytes(z.read(IN_VPK[name]), size, f"{argv[2]}: {IN_VPK[name]}")
                    if e:
                        errs.append(e)
        return report(errs)
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))

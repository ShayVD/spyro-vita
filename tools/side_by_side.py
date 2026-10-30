#!/usr/bin/env python3
"""Put screenshots side by side (or in a grid) in one PNG, for comparing
presentation modes (docs/presentation.md). Images may differ in size: each
cell is the largest one, smaller images are centred on black.
   tools/side_by_side.py OUT.png A.png B.png ... [--cols N] [--scale F]
--cols N   images per row (default: all in one row)
--scale F  shrink every cell by F (e.g. 0.5), nearest neighbour"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from contact_sheet import read_png, write_png  # noqa: E402

GAP = 8


def main(out, files, cols=None, scale=1.0):
    imgs = [read_png(f) for f in files]
    cols = cols or len(imgs)
    rows = (len(imgs) + cols - 1) // cols
    cw = int(max(w for w, _, _ in imgs) * scale)
    ch = int(max(h for _, h, _ in imgs) * scale)
    W, H = cols * cw + (cols - 1) * GAP, rows * ch + (rows - 1) * GAP
    img = bytearray(b"\x40" * (W * H * 3))  # dark gray gaps
    for k, (w, h, px) in enumerate(imgs):
        sw, sh = int(w * scale), int(h * scale)
        cx = (k % cols) * (cw + GAP)
        cy = (k // cols) * (ch + GAP)
        for y in range(ch):
            row = (cy + y) * W
            img[(row + cx) * 3:(row + cx + cw) * 3] = bytes(cw * 3)
        ox, oy = cx + (cw - sw) // 2, cy + (ch - sh) // 2
        for y in range(sh):
            src = px[int(y / scale)]
            o = ((oy + y) * W + ox) * 3
            if scale == 1.0:
                img[o:o + sw * 3] = src[:sw * 3]
            else:
                for x in range(sw):
                    s = int(x / scale) * 3
                    img[o + x * 3:o + x * 3 + 3] = src[s:s + 3]
    write_png(out, W, H, img)
    print(f"{len(imgs)} images -> {out} ({W}x{H})")


if __name__ == "__main__":
    args, cols, scale = sys.argv[1:], None, 1.0
    for flag in ("--cols", "--scale"):
        if flag in args:
            i = args.index(flag)
            v = args[i + 1]
            del args[i:i + 2]
            if flag == "--cols":
                cols = int(v)
            else:
                scale = float(v)
    if len(args) < 2:
        sys.exit(__doc__)
    main(args[0], args[1:], cols, scale)

#!/usr/bin/env python3
"""The VPK's bubble icon and LiveArea art (decisions D55), drawn from
scratch: a faceted gem between two dragon wings on a night-purple sky, and
"SPYRO VITA" in a stroke font defined below. Nothing is traced from the game,
its logos or its box art; no font files are read. Plain Python (no Pillow),
so the build needs nothing new. The output is the same on every run.

  make_livearea.py OUTDIR      write OUTDIR/icon0.png (128x128),
                               OUTDIR/bg.png (840x500), OUTDIR/startup.png (280x158)
  make_livearea.py --check PNG WxH ...
                               fail unless each PNG is WxH, 8-bit indexed

The Vita wants sce_sys PNGs 8-bit indexed (palette) at these sizes: the
bubble icon 128x128, the LiveArea background 840x500, the gate 280x158.
"""
import math
import os
import random
import struct
import sys
import zlib

# ---------------------------------------------------------------- canvas


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0.0, 0.0, 0.0] for _ in range(w * h)]

    def shade(self, fn):
        """Every pixel from fn(x, y) -> (r, g, b), 0..255 floats."""
        for y in range(self.h):
            for x in range(self.w):
                self.px[y * self.w + x] = list(fn(x + 0.5, y + 0.5))

    def blend(self, cov, color):
        """cov: {index: coverage 0..1}; color: (r,g,b) or fn(x, y) -> (r,g,b)."""
        for i, a in cov.items():
            if a <= 0:
                continue
            a = min(a, 1.0)
            c = color(i % self.w + 0.5, i // self.w + 0.5) if callable(color) else color
            p = self.px[i]
            for k in range(3):
                p[k] += (c[k] - p[k]) * a

    def poly_cov(self, pts, ss=4):
        """Coverage of a polygon (any shape, even-odd), ss sub-rows per pixel
        and exact coverage along each sub-row."""
        cov = {}
        ys = [p[1] for p in pts]
        y0 = max(0, int(math.floor(min(ys))))
        y1 = min(self.h - 1, int(math.ceil(max(ys))))
        n = len(pts)
        w = 1.0 / ss
        for y in range(y0, y1 + 1):
            for s in range(ss):
                sy = y + (s + 0.5) / ss
                xs = []
                for i in range(n):
                    ax, ay = pts[i]
                    bx, by = pts[(i + 1) % n]
                    if (ay <= sy) != (by <= sy):
                        xs.append(ax + (sy - ay) * (bx - ax) / (by - ay))
                xs.sort()
                for k in range(0, len(xs) - 1, 2):
                    xa = max(0.0, xs[k])
                    xb = min(float(self.w), xs[k + 1])
                    if xb <= xa:
                        continue
                    ia, ib = int(xa), int(xb)
                    row = y * self.w
                    if ia == ib:
                        cov[row + ia] = cov.get(row + ia, 0) + (xb - xa) * w
                        continue
                    cov[row + ia] = cov.get(row + ia, 0) + (ia + 1 - xa) * w
                    for x in range(ia + 1, ib):
                        cov[row + x] = cov.get(row + x, 0) + w
                    if ib < self.w:
                        cov[row + ib] = cov.get(row + ib, 0) + (xb - ib) * w
        return cov

    def stroke_cov(self, lines, r):
        """Coverage of round-capped strokes of radius r along polylines."""
        dist = {}
        for line in lines:
            for (ax, ay), (bx, by) in zip(line, line[1:]):
                x0 = max(0, int(min(ax, bx) - r - 1))
                x1 = min(self.w - 1, int(max(ax, bx) + r + 1))
                y0 = max(0, int(min(ay, by) - r - 1))
                y1 = min(self.h - 1, int(max(ay, by) + r + 1))
                dx, dy = bx - ax, by - ay
                ll = dx * dx + dy * dy or 1e-9
                for y in range(y0, y1 + 1):
                    py = y + 0.5
                    for x in range(x0, x1 + 1):
                        px = x + 0.5
                        t = ((px - ax) * dx + (py - ay) * dy) / ll
                        t = 0.0 if t < 0 else 1.0 if t > 1 else t
                        ex, ey = px - (ax + t * dx), py - (ay + t * dy)
                        d = math.sqrt(ex * ex + ey * ey)
                        i = y * self.w + x
                        if d < dist.get(i, 1e9):
                            dist[i] = d
        return {i: max(0.0, min(1.0, r - d + 0.5)) for i, d in dist.items()}

    def fill(self, pts, color, alpha=1.0):
        cov = self.poly_cov(pts)
        if alpha != 1.0:
            cov = {i: a * alpha for i, a in cov.items()}
        self.blend(cov, color)

    def stroke(self, lines, r, color, alpha=1.0):
        cov = self.stroke_cov(lines, r)
        if alpha != 1.0:
            cov = {i: a * alpha for i, a in cov.items()}
        self.blend(cov, color)


# ---------------------------------------------------------------- shapes


def lerp(a, b, t):
    return tuple(a[k] + (b[k] - a[k]) * t for k in range(len(a)))


def hexc(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


def arc(cx, cy, rx, ry, a0, a1, n=14):
    """Points on an ellipse, degrees, y down (angles turn clockwise on screen)."""
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def bezier(a, c, b, n=10):
    return [lerp(lerp(a, c, i / n), lerp(c, b, i / n), i / n) for i in range(n + 1)]


def xform(pts, s, ox, oy, flip=False):
    return [((-x if flip else x) * s + ox, y * s + oy) for x, y in pts]


# Glyphs: polylines in a box 1 high (cap height), x from 0 to the advance
# width. Geometric strokes, our own.
GLYPHS = {
    "S": (0.56, [arc(0.28, 0.25, 0.27, 0.25, -25, -270) + arc(0.28, 0.75, 0.27, 0.25, -90, 155)[1:]]),
    "P": (0.55, [[(0, 1), (0, 0), (0.28, 0)] + arc(0.28, 0.26, 0.26, 0.26, -90, 90)[1:] + [(0, 0.52)]]),
    "R": (0.58, [[(0, 1), (0, 0), (0.28, 0)] + arc(0.28, 0.26, 0.26, 0.26, -90, 90)[1:] + [(0, 0.52)],
                 [(0.26, 0.52), (0.58, 1)]]),
    "Y": (0.62, [[(0, 0), (0.31, 0.5), (0.62, 0)], [(0.31, 0.5), (0.31, 1)]]),
    "O": (0.68, [arc(0.34, 0.5, 0.34, 0.5, 0, 360, 32)]),
    "V": (0.64, [[(0, 0), (0.32, 1), (0.64, 0)]]),
    "I": (0.0, [[(0, 0), (0, 1)]]),
    "T": (0.6, [[(0, 0), (0.6, 0)], [(0.3, 0), (0.3, 1)]]),
    "A": (0.66, [[(0, 1), (0.33, 0), (0.66, 1)], [(0.13, 0.62), (0.53, 0.62)]]),
    " ": (0.3, []),
}
GAP = 0.2  # between glyphs, plus the stroke width


def text_lines(s, size, weight):
    """Polylines for s at cap height size, its width, origin at the top left."""
    lines, x = [], 0.0
    for ch in s:
        adv, g = GLYPHS[ch]
        lines += [[(x + px * size, py * size) for px, py in pl] for pl in g]
        x += adv * size + GAP * size + 2 * weight
    return lines, x - GAP * size - 2 * weight


def draw_text(cv, s, cx, top, size, weight):
    """Gold strokes, a dark outline and a soft drop shadow, centred on cx."""
    lines, w = text_lines(s, size, weight)
    x0 = cx - w / 2

    def at(dx, dy):
        return [[(x + x0 + dx, y + top + dy) for x, y in pl] for pl in lines]

    sh = max(1.0, size * 0.06)
    cv.stroke(at(sh, sh * 1.3), weight + size * 0.09, (20, 4, 34), 0.55)
    cv.stroke(at(0, 0), weight + max(1.0, size * 0.07), hexc("#2a0a3d"))
    hi, lo = hexc("#fff1a8"), hexc("#f08a1c")
    cv.stroke(at(0, 0), weight, lambda x, y: lerp(hi, lo, max(0, min(1, (y - top) / size))))


# A brilliant-cut gem seen from the front: girdle at y=0, 2 wide.
G = [(-1, 0), (-0.34, 0), (0.34, 0), (1, 0)]
T = [(-0.48, -0.42), (0, -0.42), (0.48, -0.42)]
CULET = (0, 1.05)
GEM_FACETS = [  # (points, shade 0 dark .. 1 light)
    ([G[0], T[0], G[1]], 0.85),
    ([T[0], T[1], G[1]], 1.0),
    ([T[1], G[2], G[1]], 0.7),
    ([T[1], T[2], G[2]], 0.9),
    ([T[2], G[3], G[2]], 0.55),
    ([G[0], G[1], CULET], 0.5),
    ([G[1], G[2], CULET], 0.35),
    ([G[2], G[3], CULET], 0.18),
]
GEM_OUTLINE = [G[0], T[0], T[2], G[3], CULET]


def draw_gem(cv, cx, cy, s, ramp):
    out = xform(GEM_OUTLINE, s, cx, cy)
    cv.stroke([out + out[:1]], max(1.0, s * 0.07), hexc("#14062a"))
    cv.fill(out, ramp[0])
    for pts, t in GEM_FACETS:
        k = t * (len(ramp) - 1)
        i = min(int(k), len(ramp) - 2)
        cv.fill(xform(pts, s * 0.985, cx, cy), lerp(ramp[i], ramp[i + 1], k - i))
    # a glint on the table
    gx, gy, g = cx - s * 0.2, cy - s * 0.3, s * 0.16
    cv.fill([(gx, gy - g), (gx + g * 0.22, gy - g * 0.22), (gx + g, gy), (gx + g * 0.22, gy + g * 0.22),
             (gx, gy + g), (gx - g * 0.22, gy + g * 0.22), (gx - g, gy), (gx - g * 0.22, gy - g * 0.22)],
            (255, 255, 255), 0.9)


# A dragon wing, attached at (0, 0) and spreading to +x, up is -y.
ELBOW = (0.3, -0.55)
FINGERS = [(1.0, -0.95), (1.04, -0.42), (0.86, 0.02), (0.55, 0.3)]
ROOT = (0.04, 0.24)


def wing_outline():
    pts = bezier((0, 0), (0.05, -0.4), ELBOW) + bezier(ELBOW, (0.62, -0.88), FINGERS[0])[1:]
    tips = FINGERS + [ROOT]
    for a, b in zip(tips, tips[1:]):
        m = lerp(a, b, 0.5)
        pts += bezier(a, lerp(m, ELBOW, 0.32), b)[1:]
    return pts


def draw_wing(cv, ox, oy, s, flip, membrane, rib, alpha=1.0):
    cv.fill(xform(wing_outline(), s, ox, oy, flip), membrane, alpha)
    ribs = [xform([ELBOW, f], s, ox, oy, flip) for f in FINGERS[1:]]
    arm = xform(bezier((0, 0), (0.05, -0.4), ELBOW) + bezier(ELBOW, (0.62, -0.88), FINGERS[0])[1:],
                s, ox, oy, flip)
    cv.stroke(ribs, max(0.6, s * 0.018), rib, alpha)
    cv.stroke([arm], max(0.8, s * 0.032), rib, alpha)


def sparkle(cv, x, y, r, alpha):
    t = r * 0.18
    cv.fill([(x, y - r), (x + t, y - t), (x + r, y), (x + t, y + t),
             (x, y + r), (x - t, y + t), (x - r, y), (x - t, y - t)], (255, 236, 255), alpha)


GREEN = [hexc(c) for c in ("#0e5a36", "#1f9a5c", "#3fd68a", "#9ff7c3", "#e6fff1")]
GOLD = [hexc(c) for c in ("#8a4a08", "#d0821a", "#f6b733", "#ffe27a", "#fff8d8")]
MEMBRANE = hexc("#9a4fd6")
RIB = hexc("#e3b8ff")


def emblem(cv, cx, cy, s, gem=GREEN, wing_alpha=1.0):
    """The winged gem: s is the gem's half width."""
    ws = s * 1.75
    for flip in (False, True):
        draw_wing(cv, cx + (-s * 0.82 if flip else s * 0.82), cy - s * 0.05, ws, flip,
                  MEMBRANE, RIB, wing_alpha)
    draw_gem(cv, cx, cy, s, gem)


def night(cv, top, bottom, glow, gx, gy, gr):
    h = cv.h

    def fn(x, y):
        c = lerp(top, bottom, y / h)
        d = math.hypot(x - gx, y - gy) / gr
        return lerp(c, glow, max(0.0, 1 - d) ** 2 * 0.85)
    cv.shade(fn)


# ---------------------------------------------------------------- the images

SKY_TOP, SKY_BOTTOM, SKY_GLOW = hexc("#120630"), hexc("#3d1766"), hexc("#8e4cc9")


def make_icon():
    cv = Canvas(128, 128)
    night(cv, SKY_TOP, SKY_BOTTOM, SKY_GLOW, 64, 50, 70)
    emblem(cv, 64, 44, 17)
    draw_text(cv, "SPYRO", 64, 77, 15, 1.9)
    draw_text(cv, "VITA", 64, 98, 11, 1.5)
    return cv


def make_startup():
    cv = Canvas(280, 158)
    night(cv, SKY_TOP, SKY_BOTTOM, SKY_GLOW, 140, 58, 150)
    rnd = random.Random(2)
    for _ in range(18):
        sparkle(cv, rnd.uniform(4, 276), rnd.uniform(4, 150), rnd.uniform(1.2, 3), rnd.uniform(0.3, 0.8))
    emblem(cv, 140, 50, 26)
    draw_text(cv, "SPYRO VITA", 140, 104, 28, 3.2)
    return cv


def make_bg():
    cv = Canvas(840, 500)
    night(cv, SKY_TOP, SKY_BOTTOM, SKY_GLOW, 420, 230, 520)
    rnd = random.Random(1998)
    for _ in range(90):
        sparkle(cv, rnd.uniform(0, 840), rnd.uniform(0, 500), rnd.uniform(1.0, 4.2), rnd.uniform(0.2, 0.75))
    # big faint wings at the sides, a few gems below the gate
    draw_wing(cv, 300, 330, 300, True, MEMBRANE, RIB, 0.28)
    draw_wing(cv, 540, 330, 300, False, MEMBRANE, RIB, 0.28)
    for x, y, s, ramp in ((120, 420, 22, GREEN), (250, 452, 15, GOLD), (590, 452, 15, GREEN),
                          (720, 420, 22, GOLD), (420, 440, 18, GREEN)):
        draw_gem(cv, x, y, s, ramp)
    return cv


# ---------------------------------------------------------------- PNG


def quantize(cv, n=256):
    """Median cut on a 5-bit-per-channel histogram; returns (palette, indices)."""
    rgb = [tuple(max(0, min(255, int(round(c)))) for c in p) for p in cv.px]
    hist = {}
    for c in rgb:
        hist[c] = hist.get(c, 0) + 1
    if len(hist) <= n:
        pal = sorted(hist)
        idx = {c: i for i, c in enumerate(pal)}
        return pal, [idx[c] for c in rgb]
    keys = {}
    for c, k in hist.items():
        q = (c[0] >> 3, c[1] >> 3, c[2] >> 3)
        keys.setdefault(q, []).append((c, k))
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
        tot = [0, 0, 0]
        cnt = 0
        for q in b:
            qidx[q] = i
            for c, k in keys[q]:
                cnt += k
                for j in range(3):
                    tot[j] += c[j] * k
        pal.append(tuple(int(round(t / cnt)) for t in tot))
    # Map through a 4x4 ordered dither so the sky's gradients don't band.
    out = []
    for i, c in enumerate(rgb):
        d = BAYER[(i // cv.w) % 4][i % cv.w % 4] - 8
        q = tuple(min(31, max(0, (v + d) >> 3)) for v in c)
        out.append(qidx[q] if q in qidx else qidx[(c[0] >> 3, c[1] >> 3, c[2] >> 3)])
    return pal, out


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def write_png(cv, path):
    pal, idx = quantize(cv)
    w = cv.w
    raw = b"".join(b"\0" + bytes(idx[y * w:(y + 1) * w]) for y in range(cv.h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, cv.h, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", b"".join(bytes(c) for c in pal))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def check_png(path, size):
    """The Vita's rule for sce_sys art: 8-bit indexed (colour type 3) at size."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n" or d[12:16] != b"IHDR":
        return f"{path}: not a PNG"
    w, h, depth, ctype = struct.unpack(">IIBB", d[16:26])
    if (w, h) != size:
        return f"{path}: {w}x{h}, wants {size[0]}x{size[1]}"
    if depth != 8 or ctype != 3:
        return f"{path}: bit depth {depth}, colour type {ctype}; wants 8-bit indexed"
    return None


def main(argv):
    if len(argv) > 1 and argv[1] == "--check":
        errs = []
        for path, size in zip(argv[2::2], argv[3::2]):
            e = check_png(path, tuple(int(v) for v in size.split("x")))
            if e:
                errs.append(e)
        for e in errs:
            print(e, file=sys.stderr)
        return 1 if errs else 0
    if len(argv) != 2:
        sys.exit(__doc__)
    os.makedirs(argv[1], exist_ok=True)
    for name, make in (("icon0.png", make_icon), ("startup.png", make_startup), ("bg.png", make_bg)):
        write_png(make(), os.path.join(argv[1], name))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

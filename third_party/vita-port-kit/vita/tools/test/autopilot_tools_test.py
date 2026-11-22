#!/usr/bin/env python3
"""perf_report.py and shots_cmp.py on synthetic logs and pictures. Standard library only.
  python3 vita/tools/test/autopilot_tools_test.py
"""
import contextlib
import importlib.util
import io
import json
import shutil
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / (name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


perf_report = load("perf_report")
shots_cmp = load("shots_cmp")

BASE_LOG = """boot
[12.3] vpk-perfcount: w 1 300 300 draws=3000 uploads=600
vpk-perfcount: w 301 600 300 draws=3300 uploads=600
other: w 1 2 3 x=1
frames=600 reached, exiting
"""
NEW_LOG = """vpk-perfcount: w 1 300 300 draws=2700 uploads=606 allocs=30
vpk-perfcount: w 301 600 300 draws=2970 uploads=606 allocs=30
"""


class PerfReport(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.d)
        (self.d / "base.log").write_text(BASE_LOG)
        (self.d / "new.log").write_text(NEW_LOG)

    def test_means(self):
        frames, means = perf_report.read(self.d / "base.log", "vpk-perfcount")
        self.assertEqual(frames, 600)
        self.assertEqual(means, {"draws": 10.5, "uploads": 2.0})

    def test_frame_range(self):
        frames, means = perf_report.read(self.d / "base.log", "vpk-perfcount", lo=301)
        self.assertEqual((frames, means["draws"]), (300, 11.0))

    def test_compare(self):
        _, _, rows = perf_report.compare(self.d / "base.log", self.d / "new.log", "vpk-perfcount", noise=0.02)
        verdicts = {r["name"]: r["verdict"] for r in rows}
        self.assertEqual(verdicts, {"draws": "better", "uploads": "same", "allocs": "new"})

    def test_worse_exits_1(self):
        _, _, rows = perf_report.compare(self.d / "new.log", self.d / "base.log", "vpk-perfcount", noise=0.02)
        self.assertIn("worse", {r["verdict"] for r in rows})
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(perf_report.main([str(self.d / "new.log"), str(self.d / "base.log")]), 1)
            self.assertEqual(perf_report.main([str(self.d / "base.log"), str(self.d / "new.log")]), 0)

    def test_window_without_counters(self):
        (self.d / "none.log").write_text("vpk-perfcount: w 1 300 300\nvpk-perfcount: w 301 600 300 draws=3000\n")
        frames, means = perf_report.read(self.d / "none.log", "vpk-perfcount")
        self.assertEqual((frames, means), (600, {"draws": 5.0}))
        frames, means = perf_report.read(self.d / "none.log", "vpk-perfcount", hi=300)
        self.assertEqual((frames, means), (300, {}))

    def test_no_lines_exits_2(self):
        (self.d / "empty.log").write_text("boot\n")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(perf_report.main([str(self.d / "empty.log")]), 2)

    def one(self, name, draws, frames=100):
        p = self.d / name
        p.write_text(f"vpk-perfcount: w 1 {frames} {frames} draws={draws}\n")
        return str(p)

    def verdict(self, base, new):
        _, _, rows = perf_report.compare(base, new, "vpk-perfcount", noise=0.02)
        return rows[0]["verdict"]

    def test_noise_boundary_is_exact(self):
        # 1 -> 1.02 is 0.020000000000000018 in floats; 10 -> 10.2 is exactly 0.02
        self.assertEqual(self.verdict(self.one("a.log", 100), self.one("b.log", 102)), "same")
        self.assertEqual(self.verdict(self.one("c.log", 1000), self.one("d.log", 1020)), "same")
        self.assertEqual(self.verdict(self.one("e.log", 100), self.one("f.log", 98)), "same")
        self.assertEqual(self.verdict(self.one("g.log", 100), self.one("h.log", 103)), "worse")
        self.assertEqual(self.verdict(self.one("i.log", 100), self.one("j.log", 97)), "better")

    def test_unreadable_log_exits_2(self):
        err = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
            self.assertEqual(perf_report.main([str(self.d / "nope.log")]), 2)
            self.assertEqual(perf_report.main([str(self.d / "base.log"), str(self.d / "nope.log")]), 2)
        self.assertIn("perf_report:", err.getvalue())
        self.assertIn("nope.log", err.getvalue())

    def test_zero_baseline_is_worse_and_valid_json(self):
        base, new = self.one("z.log", 0), self.one("y.log", 5)
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = perf_report.main([base, new, "--json"])
        self.assertEqual(code, 1)
        doc = json.loads(out.getvalue())  # no Infinity
        row = doc["rows"][0]
        self.assertEqual((row["verdict"], row["change"]), ("worse", None))
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(perf_report.main([base, new]), 1)
        self.assertIn("new>0", out.getvalue())
        self.assertNotIn("inf", out.getvalue())

    def test_frame_counts_reported(self):
        base, new = self.one("a.log", 100, 100), self.one("b.log", 100, 250)
        with contextlib.redirect_stdout(io.StringIO()) as out:
            perf_report.main([base, new, "--json"])
        doc = json.loads(out.getvalue())
        self.assertEqual((doc["base_frames"], doc["new_frames"]), (100, 250))
        with contextlib.redirect_stdout(io.StringIO()) as out:
            perf_report.main([base, new])
        self.assertIn("base 100 frames", out.getvalue())
        self.assertIn("new 250 frames", out.getvalue())


def png(path, w, h, pixel, filt=0, ctype=2):
    """An 8-bit PNG, colour type 2 (RGB) or 6 (RGBA, alpha (x + y) * 7); pixel(x, y) -> (r, g, b).
    Every row has filter type filt (0 none, 1 sub, 2 up, 3 average, 4 paeth). Returns the unfiltered rows."""
    bpp = 3 if ctype == 2 else 4
    rows, out = b"", []
    prev = bytes(w * bpp)
    for y in range(h):
        line = bytes(c for x in range(w) for c in (pixel(x, y) + (((x + y) * 7) & 255,))[:bpp])
        enc = bytearray()
        for i in range(len(line)):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if filt == 1:
                pred = a
            elif filt == 2:
                pred = b
            elif filt == 3:
                pred = (a + b) // 2
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
            else:
                pred = 0
            enc.append((line[i] - pred) & 255)
        rows += bytes([filt]) + bytes(enc)
        out.append(line)
        prev = line
    write_png(path, w, h, ctype, zlib.compress(rows))
    return out


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def write_png(path, w, h, ctype, idat):
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0)) +
                           chunk(b"IDAT", idat) + chunk(b"IEND", b""))


class ShotsCmp(unittest.TestCase):
    def setUp(self):
        self.base = Path(tempfile.mkdtemp())
        self.new = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.base)
        self.addCleanup(shutil.rmtree, self.new)
        grey = lambda x, y: (x * 10, y * 10, 50)
        png(self.base / "600.png", 10, 10, grey)
        png(self.base / "1200.png", 10, 10, grey)

    def run_cmp(self, *args):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = shots_cmp.main([str(self.base), str(self.new), *args])
        return code, out.getvalue()

    def test_decode_filters_agree(self):
        png(self.new / "600.png", 10, 10, lambda x, y: (x * 10, y * 10, 50), filt=1)
        png(self.new / "1200.png", 10, 10, lambda x, y: (x * 10, y * 10, 50), filt=2)
        a = shots_cmp.read_png(self.base / "600.png")
        self.assertEqual(a, shots_cmp.read_png(self.new / "600.png"))
        self.assertEqual(a, shots_cmp.read_png(self.new / "1200.png"))
        self.assertEqual(self.run_cmp()[0], 0)

    def test_identical(self):
        for n in ("600.png", "1200.png"):
            (self.new / n).write_bytes((self.base / n).read_bytes())
        code, out = self.run_cmp()
        self.assertEqual(code, 0)
        self.assertIn("2 same", out)

    def test_one_pixel_off(self):
        (self.new / "600.png").write_bytes((self.base / "600.png").read_bytes())
        png(self.new / "1200.png", 10, 10, lambda x, y: (255, 0, 0) if (x, y) == (5, 5) else (x * 10, y * 10, 50))
        self.assertEqual(self.run_cmp()[0], 1)
        self.assertEqual(self.run_cmp("--max-diff", "0.01")[0], 0)  # 1 of 100 pixels

    def test_edge_ignored(self):
        (self.new / "600.png").write_bytes((self.base / "600.png").read_bytes())
        png(self.new / "1200.png", 10, 10, lambda x, y: (255, 0, 0) if x == 0 else (x * 10, y * 10, 50))
        self.assertEqual(self.run_cmp()[0], 1)
        self.assertEqual(self.run_cmp("--edge", "1")[0], 0)

    def test_tolerance(self):
        (self.new / "600.png").write_bytes((self.base / "600.png").read_bytes())
        png(self.new / "1200.png", 10, 10, lambda x, y: (x * 10 + 2, y * 10, 50))
        self.assertEqual(self.run_cmp()[0], 1)
        self.assertEqual(self.run_cmp("--tol", "2")[0], 0)

    def test_missing_and_size(self):
        png(self.new / "600.png", 8, 10, lambda x, y: (0, 0, 0))
        code, out = self.run_cmp()
        self.assertEqual(code, 1)
        self.assertIn("missing 1200.png", out)
        self.assertIn("size 600.png", out)

    def test_decode_round_trip_all_filters_and_colour_types(self):
        pixel = lambda x, y: ((x * 37 + y * 11) & 255, (x * y * 5) & 255, (255 - x * 13 + y * 29) & 255)
        for ctype in (2, 6):
            for filt in range(5):
                with self.subTest(ctype=ctype, filt=filt):
                    path = self.new / f"rt{ctype}{filt}.png"
                    expect = png(path, 9, 7, pixel, filt=filt, ctype=ctype)
                    w, h, bpp, rows = shots_cmp.read_png(path)
                    self.assertEqual((w, h, bpp), (9, 7, 3 if ctype == 2 else 4))
                    self.assertEqual(rows, expect)

    def test_glob(self):
        for n in ("600.png", "1200.png"):
            (self.new / n).write_bytes((self.base / n).read_bytes())
        (self.base / "600.jpg").write_bytes(b"not a png")  # only compared when the glob matches it
        code, out = self.run_cmp("--glob", "600.*g")
        self.assertEqual(code, 1)  # the jpg is missing from NEW
        self.assertIn("missing 600.jpg", out)
        self.assertIn("1 same", out)
        code, out = self.run_cmp("--glob", "1200*")
        self.assertEqual(code, 0)
        self.assertIn("1 same", out)
        self.assertNotIn("600", out)

    def test_nothing_to_compare_fails(self):
        code, out = self.run_cmp("--glob", "*.bmp")
        self.assertEqual(code, 1)
        self.assertIn("nothing to compare", out)

    def test_negative_edge_rejected(self):
        with contextlib.redirect_stderr(io.StringIO()) as err:
            with self.assertRaises(SystemExit) as cm:
                self.run_cmp("--edge", "-1")
        self.assertEqual(cm.exception.code, 2)
        self.assertIn("--edge", err.getvalue())

    def test_edge_leaving_no_pixels_is_over(self):
        (self.new / "600.png").write_bytes((self.base / "600.png").read_bytes())
        png(self.new / "1200.png", 10, 10, lambda x, y: (255, 0, 0))
        code, out = self.run_cmp("--edge", "5")  # 10 - 2 * 5 = 0 pixels left
        self.assertEqual(code, 1)
        self.assertIn("over 1200.png", out)
        self.assertIn("edge leaves no pixels", out)

    def test_bad_pictures_are_over_and_the_rest_carry_on(self):
        (self.new / "600.png").write_bytes((self.base / "600.png").read_bytes())
        good = (self.base / "1200.png").read_bytes()
        (self.new / "1200.png").write_bytes(good[:-30])  # truncated: zlib / chunk errors
        png(self.base / "a.png", 10, 10, lambda x, y: (1, 2, 3))
        write_png(self.new / "a.png", 10, 10, 2, zlib.compress(b"\x00"))  # too little data: IndexError
        (self.base / "b.png").write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IEND", b""))  # no IHDR
        (self.new / "b.png").write_bytes(b"garbage")
        code, out = self.run_cmp()
        self.assertEqual(code, 1)
        for n in ("1200.png", "a.png", "b.png"):
            self.assertIn(f"bad {n}:", out)
        self.assertIn("1 same, 0 within, 3 over", out)

    def test_palette_png_is_bad(self):
        write_png(self.base / "pal.png", 4, 4, 3, zlib.compress(bytes(4 * 5)))
        (self.base / "600.png").unlink()
        (self.base / "1200.png").unlink()
        png(self.new / "pal.png", 4, 4, lambda x, y: (0, 0, 0))
        code, out = self.run_cmp()
        self.assertEqual(code, 1)
        self.assertIn("bad pal.png:", out)
        self.assertIn("only 8-bit RGB or RGBA", out)

    def test_read_png_without_ihdr_raises(self):
        p = self.new / "x.png"
        p.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IEND", b""))
        with self.assertRaises(ValueError):
            shots_cmp.read_png(p)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""perf_report.py, hw_log.py and shots_cmp.py on synthetic logs and pictures. Standard library only.
  python3 targets/vita/tools/test/autopilot_tools_test.py
"""
import contextlib
import importlib.util
import io
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
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

    def test_counters_judges_only_named(self):
        base, new = self.d / "new.log", self.d / "base.log"  # draws worse, uploads same, allocs gone
        _, _, rows = perf_report.compare(base, new, "vpk-perfcount", noise=0.02, counters={"uploads", "allocs"})
        self.assertEqual({r["name"]: r["verdict"] for r in rows}, {"draws": "info", "uploads": "same", "allocs": "gone"})
        _, _, rows = perf_report.compare(base, new, "vpk-perfcount", noise=0.02, ignore={"draws", "allocs"})
        self.assertEqual({r["name"]: r["verdict"] for r in rows}, {"draws": "info", "uploads": "same", "allocs": "info"})
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(perf_report.main([str(base), str(new), "--counters", "uploads, allocs"]), 0)
            self.assertEqual(perf_report.main([str(base), str(new), "--ignore", "draws"]), 0)
            self.assertEqual(perf_report.main([str(base), str(new), "--counters", "draws"]), 1)
        self.assertIn("info", out.getvalue())

    def test_globs(self):
        self.assertTrue(perf_report.matches("vc.rec.hits", {"vc.rec.*"}))
        self.assertTrue(perf_report.matches("draws", {"draws"}))
        self.assertFalse(perf_report.matches("vc.recx", {"vc.rec.*"}))
        base, new = self.d / "new.log", self.d / "base.log"  # draws worse
        _, _, rows = perf_report.compare(base, new, "vpk-perfcount", noise=0.02, ignore={"dr*"})
        self.assertEqual({r["name"]: r["verdict"] for r in rows}["draws"], "info")
        _, _, rows = perf_report.compare(base, new, "vpk-perfcount", noise=0.02, counters={"up*"})
        self.assertEqual({r["name"]: r["verdict"] for r in rows}, {"draws": "info", "uploads": "same", "allocs": "info"})
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(perf_report.main([str(base), str(new), "--ignore", "d*"]), 0)

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



class PerfRerun(unittest.TestCase):
    """same-VPK noise: a counter is worse only when worse than every run of the base VPK"""

    def setUp(self):
        self.d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.d)
        for name, vk in (("base", 100), ("base2", 104), ("new", 103)):
            (self.d / f"{name}.log").write_text(f"vpk-perfcount: w 1 100 100 vert.kib={vk * 100} draws={1000}\n")

    def test_noise_is_not_worse(self):
        _, _, rows = perf_report.compare(self.d / "base.log", self.d / "new.log", "vpk-perfcount")
        self.assertEqual({r["name"]: r["verdict"] for r in rows}["vert.kib"], "worse")
        _, _, rows = perf_report.compare(self.d / "base.log", self.d / "new.log", "vpk-perfcount",
                                         reruns=[self.d / "base2.log"])
        r = {r["name"]: r for r in rows}
        self.assertEqual((r["vert.kib"]["verdict"], r["draws"]["verdict"]), ("same", "same"))
        self.assertAlmostEqual(r["vert.kib"]["spread"], 0.04)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(perf_report.main([str(self.d / "base.log"), str(self.d / "new.log"),
                                               "--rerun", str(self.d / "base2.log")]), 0)

    def test_worse_than_every_base_stays_worse(self):
        (self.d / "new.log").write_text("vpk-perfcount: w 1 100 100 vert.kib=11000 draws=1000\n")
        _, _, rows = perf_report.compare(self.d / "base.log", self.d / "new.log", "vpk-perfcount",
                                         reruns=[self.d / "base2.log"])
        self.assertEqual({r["name"]: r["verdict"] for r in rows}["vert.kib"], "worse")


class PerfSpread(unittest.TestCase):
    """--spread: run-to-run spread of every counter over runs of one build, before it gates"""

    def setUp(self):
        self.d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.d)

    def run_dir(self, name, *windows):
        """a run folder (the runner contract's run.log) with one perfcount line per (first, last, counters)"""
        d = self.d / name
        d.mkdir()
        (d / "run.log").write_text("boot\n" + "".join(
            f"vpk-perfcount: w {a} {b} {b - a + 1}" + "".join(f" {k}={v}" for k, v in c.items()) + "\n"
            for a, b, c in windows) + "frames reached\n")
        return str(d)

    def main(self, *args):
        with contextlib.redirect_stdout(io.StringIO()) as out, contextlib.redirect_stderr(io.StringIO()) as err:
            code = perf_report.main(["--spread", *args])
        return code, out.getvalue(), err.getvalue()

    def test_same_runs_are_steady(self):
        a = self.run_dir("a", (1, 100, {"draws": 500, "verts": 9000}), (101, 200, {"draws": 600, "verts": 9000}))
        b = self.run_dir("b", (1, 100, {"draws": 500, "verts": 9000}), (101, 200, {"draws": 600, "verts": 9000}))
        doc = perf_report.spread([a, b], "vpk-perfcount")
        self.assertEqual((doc["frames"], doc["windows"]), ([200, 200], [[1, 100], [101, 200]]))
        self.assertEqual({r["name"]: (r["verdict"], r["spread"]) for r in doc["rows"]},
                         {"draws": ("steady", 0.0), "verts": ("steady", 0.0)})
        code, out, _ = self.main(a, b)
        self.assertEqual(code, 0)
        self.assertIn("none", out)
        self.assertEqual(self.main(a, b, "--ignore-list")[:2], (0, '"ignore": []\n'))

    def test_moving_counter_and_ignore_list(self):
        # draws 5.0 vs 5.15 per frame over the range: +3% moves; verts +1% stays steady
        a = self.run_dir("a", (1, 100, {"draws": 500, "verts": 10000}))
        b = self.run_dir("b", (1, 100, {"draws": 515, "verts": 10100}))
        c = self.run_dir("c", (1, 100, {"draws": 505, "verts": 10000}))
        doc = perf_report.spread([a, b, c], "vpk-perfcount", noise=0.02)
        r = {r["name"]: r for r in doc["rows"]}
        self.assertEqual((r["draws"]["verdict"], r["verts"]["verdict"]), ("moves", "steady"))
        self.assertAlmostEqual(r["draws"]["spread"], 0.03)
        self.assertEqual((r["draws"]["min"], r["draws"]["max"]), (5.0, 5.15))
        self.assertEqual(doc["moved"], [["draws"]])
        code, out, _ = self.main(a, b, c)
        self.assertEqual(code, 1)
        self.assertRegex(out, r"draws .* 3\.0% .* moves")
        code, out, _ = self.main(a, b, c, "--ignore-list")
        self.assertEqual((code, json.loads("{" + out + "}")), (1, {"ignore": ["draws"]}))
        self.assertEqual(self.main(a, b, c, "--noise", "0.05")[0], 0)

    def test_judging_options(self):
        a = self.run_dir("a", (1, 100, {"draws": 500, "vc.rec.hits": 100}))
        b = self.run_dir("b", (1, 100, {"draws": 600, "vc.rec.hits": 130}))
        code, out, _ = self.main(a, b, "--ignore", "vc.rec.*", "--ignore-list")
        self.assertEqual((code, out), (1, '"ignore": ["draws"]\n'))
        code, out, _ = self.main(a, b, "--counters", "vc.*", "--ignore-list")
        self.assertEqual((code, out), (1, '"ignore": ["vc.rec.hits"]\n'))
        code, out, _ = self.main(a, b, "--ignore", "draws,vc.*")
        self.assertEqual(code, 0)
        self.assertRegex(out, r"draws .* info")

    def test_window_moves_but_range_does_not(self):
        # each run's work shifts between windows; the range's mean is the same
        a = self.run_dir("a", (1, 100, {"draws": 400}), (101, 200, {"draws": 600}))
        b = self.run_dir("b", (1, 100, {"draws": 600}), (101, 200, {"draws": 400}))
        doc = perf_report.spread([a, b], "vpk-perfcount")
        row = doc["rows"][0]
        self.assertEqual((row["verdict"], row["spread"]), ("steady", 0.0))
        self.assertEqual(doc["moved"], [["draws"], ["draws"]])
        self.assertAlmostEqual(row["worst"]["spread"], 0.5)
        self.assertEqual(row["worst"]["window"], [1, 100])
        code, out, _ = self.main(a, b)
        self.assertEqual(code, 0)
        self.assertIn("1-100: draws 50.0%", out)
        # the scene's frame range alone sees it
        self.assertEqual(self.main(a, b, "--max-frame", "100")[0], 1)

    def test_zero_in_one_run_moves(self):
        a = self.run_dir("a", (1, 100, {"stalls": 0}))
        b = self.run_dir("b", (1, 100, {"stalls": 3}))
        row = perf_report.spread([a, b], "vpk-perfcount")["rows"][0]
        self.assertEqual((row["verdict"], row["spread"]), ("moves", None))
        code, out, _ = self.main(a, b, "--json")
        self.assertEqual((code, json.loads(out)["rows"][0]["spread"]), (1, None))  # valid JSON, no Infinity
        self.assertIn("0>0", self.main(a, b)[1])

    def test_partial_counter_and_only_common_windows(self):
        a = self.run_dir("a", (1, 100, {"draws": 500, "extra": 7}), (101, 200, {"draws": 500}))
        b = self.run_dir("b", (1, 100, {"draws": 500}))  # stopped early
        doc = perf_report.spread([a, b], "vpk-perfcount")
        self.assertEqual((doc["frames"], doc["windows"]), ([200, 100], [[1, 100]]))
        r = {r["name"]: r for r in doc["rows"]}
        self.assertEqual((r["extra"]["verdict"], r["extra"]["runs"], r["extra"]["windows"]), ("partial", 1, []))
        code, out, _ = self.main(a, b)
        self.assertEqual(code, 0)
        self.assertIn("different frame counts", out)
        self.assertIn("1/2", out)

    def test_logs_and_noise_boundary(self):
        # a plain log works as well as a run folder; exactly NOISE apart is steady, as in the gate
        a = self.d / "a.log"
        a.write_text("vpk-perfcount: w 1 100 100 draws=1000\n")
        b = self.run_dir("b", (1, 100, {"draws": 1020}))
        self.assertEqual(self.main(str(a), b)[0], 0)
        c = self.run_dir("c", (1, 100, {"draws": 1021}))
        self.assertEqual(self.main(str(a), c)[0], 1)

    def test_errors(self):
        a = self.run_dir("a", (1, 100, {"draws": 500}))
        empty = self.d / "empty"
        empty.mkdir()
        (empty / "run.log").write_text("boot\n")
        code, _, err = self.main(a, str(empty))
        self.assertEqual(code, 2)
        self.assertIn("run.log", err)
        self.assertEqual(self.main(a, str(self.d / "nope"))[0], 2)
        self.assertEqual(self.main(a, a, "--min-frame", "500")[0], 2)
        with self.assertRaises(SystemExit):
            self.main(a)  # one run: nothing to spread
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            perf_report.main([a, a, "--ignore-list"])  # --ignore-list needs --spread


hw_log = load("hw_log")

HW_LOG = """boot
game: room=town
vpk-watchdog: threads game 900 ms (core 0, mask 10000, prio a0, 3 preempted), render 400 ms (core 1, mask 20000, prio a1, 0 preempted), audio 200 ms (core 1, mask 20000, prio a2, 0 preempted)
kh1: [CPU-bound] game 90% of core 0; 40.0 ms/picture (target 33.3); gpu 40%
vpk-watchdog: threads game 500 ms (core 0, mask 10000, prio a0, 0 preempted), render 300 ms (core 1, mask 20000, prio a1, 0 preempted)
kh1: [on-target] 33.3 ms/picture (target 33.3)
game: fight start
game: room=field
vpk-watchdog: threads game 950 ms (core 0, mask 10000, prio a0, 0 preempted)
kh1: [GPU-bound] gpu 95% busy; 50.0 ms/picture (target 33.3)
game: fight end
vpk-watchdog: threads game 100 ms (core 0, mask 10000, prio a0, 0 preempted)
"""


class HwLog(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.d)
        self.log = self.d / "hw.log"
        self.log.write_text(HW_LOG)

    def test_windows_and_loads(self):
        ws = hw_log.parse(self.log)
        self.assertEqual(len(ws), 4)
        self.assertEqual([w.get("label") for w in ws], ["CPU-bound", "on-target", "GPU-bound", None])
        th, co = hw_log.loads(ws[0], 1000)
        self.assertEqual(th, {"game": 90.0, "render": 40.0, "audio": 20.0})
        self.assertEqual(co, {0: 90.0, 1: 60.0})

    def test_pics_per_scene(self):
        rows = {r["scene"]: r for r in hw_log.pics(hw_log.parse(self.log))}
        self.assertEqual(rows["town"]["windows"], 2)
        self.assertAlmostEqual(rows["town"]["mean"], (25.0 + 1000 / 33.3) / 2)
        self.assertEqual((rows["field"]["min"], rows["field"]["max"]), (20.0, 20.0))

    def test_fight_span(self):
        ws = hw_log.span(hw_log.parse(self.log), "fight start", "fight end", self.log)
        self.assertEqual([w["scene"] for w in ws], ["field"])
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(hw_log.main(["windows", str(self.log), "--start-re", "fight start", "--end-re",
                                          "fight end"]), 0)
        self.assertIn("GPU-bound", out.getvalue())
        self.assertIn("95%", out.getvalue())

    def test_custom_fps_line_and_empty(self):
        self.log.write_text("scene=a fps=29.5\nscene=b fps=20\n")
        rows = hw_log.pics(hw_log.parse(self.log, pics_re=r"fps=(?P<fps>[\d.]+)"))
        self.assertEqual([(r["scene"], r["mean"]) for r in rows], [("a", 29.5), ("b", 20.0)])
        self.log.write_text("nothing\n")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(hw_log.main(["pics", str(self.log)]), 2)

    def test_sym(self):
        fake = self.d / "addr2line"
        fake.write_text("#!/bin/sh\nshift 5\nfor a; do echo \"fn_$a at x.c:1\"; done\n")
        fake.chmod(0o755)
        rows = hw_log.sym(["0x98001235"], "game.elf", 0x98000000, addr2line=str(fake))
        self.assertEqual(rows, [("0x98001235", "0x81001234", "fn_0x81001234 at x.c:1")])

    # kh1-vita's watchdog shape: a running 'K shown' count against a ms clock, then that window's threads line
    # (names with spaces); before this, pics returned nothing on these logs and windows split 'gs worker'
    SHOWN_LOG = """kh1-vita: watchdog 1000 ms: frame 60, 3 disc reads, 10 sends (last 0x0), 0 shown; 9 ms vblank wait
kh1-vita: threads: game 900 ms (core 0, mask 10000, prio a0, 2 preempted), gs worker 400 ms (core 1, mask 20000, prio a0, 0 preempted)
kh1-vita: watchdog 3000 ms: frame 180, 3 disc reads, 10 sends (last 0x0), 60 shown; 9 ms vblank wait
kh1-vita: threads: game 1800 ms (core 0, mask 10000, prio a0, 2 preempted), gs worker 1000 ms (core 1, mask 20000, prio a0, 0 preempted)
kh1-vita: game threads: 1 waits on sema 2 (count 0)
kh1-vita: watchdog 5000 ms: frame 300, 3 disc reads, 10 sends (last 0x0), 90 shown; 9 ms vblank wait
kh1-vita: threads: game 1200 ms (core 0, mask 10000, prio a0, 2 preempted), gs worker 600 ms (core 1, mask 20000, prio a0, 0 preempted)
kh1-vita: watchdog 9000 ms: frame 540, 3 disc reads, 10 sends (last 0x0), 210 shown; 9 ms vblank wait
"""

    def test_shown_count_threads_with_spaces_and_frames(self):
        self.log.write_text(self.SHOWN_LOG)
        ws = hw_log.parse(self.log)
        self.assertEqual([round(w.get("pics", -1), 1) for w in ws], [-1, 30.0, 15.0, 30.0])
        self.assertEqual(ws[1]["threads"], {"game": (1800, 0), "gs worker": (1000, 1)})
        self.assertEqual([w.get("frame") for w in ws], [None, 180, 300, 540])
        th, _ = hw_log.loads(ws[1], 1000)  # the window's own 2000 ms, not --window-ms
        self.assertEqual(th, {"game": 90.0, "gs worker": 50.0})
        r = hw_log.summary(hw_log.frames(ws, (100, 600)))
        self.assertAlmostEqual(r["pics"], (60 + 30 + 120) / 8.0)  # shown over time, not the windows' mean
        self.assertEqual((r["worst"], r["frames"], r["windows"]), (15.0, [180, 540], 3))
        self.assertEqual(r["threads"], {"game": 1500.0, "gs worker": 800.0})
        self.assertEqual([w["frame"] for w in hw_log.frames(ws, (200, 400))], [300])
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(hw_log.main(["summary", str(self.log), "--frames", "100-600"]), 0)
        self.assertIn("26.2 pictures/s, worst window 15.0", out.getvalue())
        self.assertIn("gs worker", out.getvalue())
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(hw_log.main(["pics", str(self.log), "--json"]), 0)
        self.assertEqual(json.loads(out.getvalue())[0]["windows"], 3)
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(hw_log.main(["pics", str(self.log), "--frames", "1000-2000"]), 2)
        self.assertIn("in frames 1000-2000", err.getvalue())
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                hw_log.main(["pics", str(self.log), "--frames", "9-1"])

    def test_format_file_and_flag_precedence(self):
        self.log.write_text("stage=a t=0 n=0\nstage=a t=1000 n=25\nstage=b t=2000 n=45\n")
        fmt = self.d / "hw_log.json"
        fmt.write_text(json.dumps({"scene_re": r"stage=(\w+)",
                                   "shown_re": r"t=(?P<ms>\d+) n=(?P<shown>\d+)"}))
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(hw_log.main(["pics", str(self.log), "--format", str(fmt), "--json"]), 0)
        self.assertEqual([(r["scene"], r["mean"]) for r in json.loads(out.getvalue())], [("a", 25.0), ("b", 20.0)])
        with contextlib.redirect_stderr(io.StringIO()):  # --shown-re '' on the command line wins: no windows
            self.assertEqual(hw_log.main(["pics", str(self.log), "--format", str(fmt), "--shown-re", ""]), 2)
        fmt.write_text("[]")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(hw_log.main(["pics", str(self.log), "--format", str(fmt)]), 2)


# spyro-vita's shape (hw session 9): a 'vblank N:' line closes each 300-vblank window, its metrics follow under the
# same N, and 'where:' lines name the level at the vblank they were logged at. Synthetic, no game log.
WINDOW_FMT = {
    "scene_re": r"\[\s*(?P<at>\d+)\] vita where: (?P<scene>level \d+|.+?)(?: \(level \d+\))?, gamestate",
    "shown_re": "",
    "window_re": r"vita vblank (?P<frame>\d+): \d+ vblanks in (?P<ms>\d+) ms \([\d.]+ per second\), "
                 r"(?P<pictures>\d+) pictures, (?P<late>\d+) vblanks late",
    "metrics": {"busy": r"vita time (?P<frame>\d+): .*; busy (?P<value>\d+)%",
                "join_ms": r"vita parse (?P<frame>\d+): .*waited for the walk (?P<value>[\d.]+) ms per vblank"},
}


def window_log(changes, late=0, busy=50, end=3000):
    """a spyro-shaped log: changes [(vblank, 'NAME (level N)')], a window every 300 vblanks to end"""
    out, cs = [], sorted(changes)
    for v in range(300, end + 1, 300):
        while cs and cs[0][0] <= v:
            at, name = cs.pop(0)
            out.append(f"[{at:6d}] vita where: {name}, gamestate 0 playing")
        out.append(f"[{v:6d}] vita vblank {v}: 300 vblanks in 5000 ms (60.0 per second), {300 - late} pictures, "
                   f"{late} vblanks late")
        out.append(f"[{v:6d}] vita time {v}: per vblank ms game 8.00 ot 1.00; busy {busy}%, worst vblank 20.0 ms")
        out.append(f"[{v:6d}] vita parse {v}: 3 lists walked on core 1, busy 7.00 ms per vblank (40%); it waited "
                   f"for the walk {busy / 100:.2f} ms per vblank in 150 joins")
    return "\n".join(out) + "\n"


class HwLogWindows(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.d)
        self.fmt = self.d / "hw_log.json"
        self.fmt.write_text(json.dumps(WINDOW_FMT))

    def write(self, name, *a, **k):
        p = self.d / name / "game.log"
        p.parent.mkdir()
        p.write_text(window_log(*a, **k))
        return str(p)

    def run_main(self, *argv):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = hw_log.main(list(argv) + ["--format", str(self.fmt)])
        return code, out.getvalue()

    def test_window_line_metrics_scenes_and_loads(self):
        log = self.write("r", [(1, "level 0"), (700, "ART (level 10)"), (1810, "TOAST (level 14)")], late=3, busy=60)
        ch = []
        ws = hw_log.parse(log, WINDOW_FMT["scene_re"], hw_log.PICS_RE, "", WINDOW_FMT["window_re"],
                          WINDOW_FMT["metrics"], changes=ch)
        self.assertEqual([w["frame"] for w in ws], list(range(300, 3001, 300)))
        w = ws[3]  # vblank 1200: by hand, 297 pictures in 5000 ms, 3 late, busy 60, join 0.60
        self.assertAlmostEqual(w["pics"], 59.4)
        self.assertEqual(w["metrics"], {"pictures": 297, "late": 3, "busy": 60, "join_ms": 0.6})
        self.assertEqual([(w["frame"], w["scene"], w["load"]) for w in ws[1:3] + ws[5:7]],
                         [(600, "level 0", False), (900, "ART", True), (1800, "ART", False), (2100, "TOAST", True)])
        self.assertEqual(ch, [(1, "level 0"), (700, "ART"), (1810, "TOAST")])
        code, out = self.run_main("summary", log, "--frames", "900-1800", "--drop-loads")
        self.assertEqual(code, 0)
        self.assertIn("frames 1200-1800, 3 windows: 59.4 pictures/s", out)
        self.assertIn("late                3.00 a window (max 3)", out)
        self.assertIn("timing: level 0 1, ART 700, TOAST 1810", out)
        code, out = self.run_main("windows", log, "--frames", "900-900")
        self.assertIn("ART (load)", out)

    def test_runs_grouped_by_route_timing(self):
        # one pad route, two timings (spyro-vita session 9's t1 t3 t5 t6 / t2 t4 t7): the first ends in a game over
        # back to ART, the second stays in TOAST; scene changes a few vblanks apart are one timing
        over = [(1, "level 0"), (700, "ART (level 10)"), (1415, "TOAST (level 14)"), (2428, "ART (level 10)")]
        stay = [(1, "level 0"), (701, "ART (level 10)"), (1416, "TOAST (level 14)")]
        runs = {"t1": (over, 0), "t2": (stay, 2), "t3": (over, 1), "t4": (stay, 2), "t5": (over, 0),
                "t6": [(a + 6, n) for a, n in over], "t7": (stay, 3)}
        logs = {}
        for name, r in runs.items():
            ch, late = r if isinstance(r, tuple) else (r, 0)
            logs[name] = self.write("hw-" + name, ch, late=late)
        code, out = self.run_main("summary", *logs.values(), "--json")
        self.assertEqual(code, 0)
        r = json.loads(out)
        self.assertEqual([sorted(g["runs"]) for g in r["timings"]],
                         [["hw-t1", "hw-t3", "hw-t5", "hw-t6"], ["hw-t2", "hw-t4", "hw-t7"]])
        self.assertIn("span 2 timings", r["warnings"][0])
        code, out = self.run_main("summary", *logs.values())
        self.assertIn("timing 1 (hw-t1 hw-t3 hw-t5 hw-t6): level 0 1, ART 700, TOAST 1415, ART 2428", out)
        self.assertIn("warning: the runs span 2 timings", out)
        # an A/B within one timing: means per side, no warning
        code, out = self.run_main("summary", logs["t1"], logs["t5"], "--vs", logs["t3"])
        self.assertNotIn("warning", out)
        self.assertIn("timing 1: A (hw-t1 hw-t5) against B (hw-t3)", out)
        self.assertRegex(out, r"late +A +0\.00 +B +1\.00 +B-A \+1\.00")
        # an A/B across timings: warned, and nothing compared across
        code, out = self.run_main("summary", logs["t1"], "--vs", logs["t2"])
        self.assertIn("warning: the A/B spans timings: no timing holds both sides", out)
        self.assertNotIn(") against B (", out)
        code, out = self.run_main("summary", logs["t1"], logs["t2"], "--vs", logs["t3"])
        self.assertIn("timing 2 holds only A (hw-t2)", out)
        # a tolerance tighter than the 6-vblank shift splits t6 off
        code, out = self.run_main("summary", logs["t1"], logs["t6"], "--timing-tol", "5", "--json")
        self.assertEqual(len(json.loads(out)["timings"]), 2)


vpk_pack = load("vpk_pack")


class VpkRequire(unittest.TestCase):
    def test_missing_empty_and_folders(self):
        d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, d)
        vpk = d / "game.vpk"
        with zipfile.ZipFile(vpk, "w") as z:
            z.writestr("eboot.bin", b"x")
            z.writestr("data/vu1/wm.x", b"prog")
            z.writestr("data/empty.bin", b"")
        self.assertEqual(vpk_pack.missing(vpk, ["eboot.bin", "data/vu1/wm.x", "data/vu1/"]), [])
        self.assertEqual(vpk_pack.missing(vpk, ["data/vu1/gb.x", "data/empty.bin", "data/none/"]),
                         ["data/vu1/gb.x", "data/empty.bin", "data/none/"])
        run = lambda *a: subprocess.run([sys.executable, str(TOOLS / "vpk_pack.py"), "require", *a],
                                        capture_output=True, text=True)
        self.assertEqual(run(str(vpk), "data/vu1/wm.x").returncode, 0)
        p = run(str(vpk), "data/vu1/gb.x")
        self.assertEqual(p.returncode, 1)
        self.assertIn("gb.x", p.stderr)
        self.assertEqual(run(str(d / "none.vpk"), "eboot.bin").returncode, 1)


if __name__ == "__main__":
    unittest.main()

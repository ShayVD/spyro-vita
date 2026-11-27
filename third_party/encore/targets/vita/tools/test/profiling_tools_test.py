#!/usr/bin/env python3
"""fnprof.py, present_trace.py, launch_token.py and embed_pad.py on synthetic logs, symbol tables and scripts. No
game bytes, no ARM toolchain (a fake nm stands in; embed_pad's header is compiled with the host cc when there is one).
  python3 targets/vita/tools/test/profiling_tools_test.py
"""
import contextlib
import importlib.util
import io
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / (name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


fnprof, present_trace, launch_token, embed_pad = (load(n) for n in ("fnprof", "present_trace", "launch_token",
                                                                    "embed_pad"))

# ---- fnprof.py ----
NM = """81000000 T _start
81000101 T vpk_fnprof_init
81000200 t $t
81000201 T game_update
81000201 T ovr_game_update
81000401 T draw_world
81000801 T leaf_math
81001000 D g_data
"""
SLIDE = 0x00100000  # this run loaded the eboot 1 MiB higher
FNLOG = """boot
test-fnprof: on, frames 0-0, sampler thread 40010003, base 81100101
test-fnprof: s 81100201 81100410 50
test-fnprof: s 81100801 81100230 30
test-fnprof: s 81100801 81100420 10
test-fnprof: s 81100440 00000000 20
test-fnprof: s 00000000 00000000 5
test-fnprof: end 115 samples in 5 pairs, 7 with the thread not running, 0 lost
"""


class Fnprof(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.nm = os.path.join(self.d, "fake-nm")
        with open(self.nm, "w") as f:
            f.write("#!/bin/sh\ncat <<'X'\n" + NM + "X\n")
        os.chmod(self.nm, os.stat(self.nm).st_mode | stat.S_IEXEC)

    def tearDown(self):
        shutil.rmtree(self.d)

    def test_aliases(self):
        _, names = fnprof.symbols("elf", self.nm, alias="ovr_")
        self.assertIn("game_update", names)
        self.assertNotIn("ovr_game_update", names)
        self.assertNotIn("$t", names)
        self.assertNotIn("g_data", names)
        _, names = fnprof.symbols("elf", self.nm)  # no alias rule: the first name at an address stays
        self.assertIn("game_update", names)

    def test_rank_with_slide(self):
        addrs, names = fnprof.symbols("elf", self.nm)
        r = fnprof.rank(FNLOG, addrs, names, "test-fnprof")
        self.assertEqual(r["total"], 115)
        self.assertEqual(r["self"]["game_update"], 50)
        self.assertEqual(r["self"]["leaf_math"], 40)
        self.assertEqual(r["self"]["draw_world"], 20)  # returned to inside draw_world
        self.assertEqual(r["self"]["(before the first call)"], 5)
        self.assertEqual(r["callers"]["leaf_math"], {"game_update": 30, "draw_world": 10})
        self.assertEqual(r["callers"]["game_update"], {"draw_world": 50})
        self.assertTrue(r["end"].startswith("test-fnprof: end 115 samples"))
        self.assertEqual(r["first"]["leaf_math"], 0x81000800)
        # without the slide every name would be wrong: the base line is what makes it right
        with self.assertRaises(SystemExit):
            fnprof.rank(FNLOG.replace("test-fnprof: on", "x"), addrs, names, "test-fnprof")

    def test_cli(self):
        log = os.path.join(self.d, "run.log")
        Path(log).write_text(FNLOG)
        out = subprocess.run([sys.executable, str(TOOLS / "fnprof.py"), log, "--elf", "elf", "--nm", self.nm,
                              "--prefix", "test-fnprof", "--top", "2"], capture_output=True, text=True, check=True)
        rows = [l for l in out.stdout.splitlines() if l.startswith("| ") and "---" not in l][1:]
        self.assertEqual(len(rows), 2)
        self.assertTrue(rows[0].startswith("| game_update | 50 | 43.48 % | draw_world 50"), rows[0])


# ---- present_trace.py, launch_token.py ----
RAW = b"""# pid=1234abcd nonce=99 capture=1 samples=4 capacity_stop=0 clock=sceKernelGetProcessTimeWide units=us event=cpu-swap-return
picture,observed_game_frame,swap_return_us,interval_us,present_cpu_us
1,10,1000,0,100
2,12,11000,10000,200
3,14,41000,30000,100
4,16,61000,20000,200
# end samples=4
"""
PTLOG = """[12.0] vpk-presenttrace: armed for manual requests; token 1234abcd 98; capacity 4096; CPU swap-return cadence only
[13.0] vpk-presenttrace: armed for manual requests; token 1234abcd 99; capacity 4096; CPU swap-return cadence only
[20.0] vpk-presenttrace: start accepted 1234abcd 99 capture=1
[30.0] vpk-presenttrace: export complete 4 samples; capacity_stop=0; pid=1234abcd nonce=99 capture=1
"""


class PresentTrace(unittest.TestCase):
    def test_exact_intervals_and_percentiles(self):
        r = present_trace.analyze(RAW, PTLOG)
        self.assertEqual(r["interval_ms"], dict(p50=20, p95=30, p99=30, max=30))
        self.assertEqual(r["picture_fps"], 50)

    def test_both_endpoints_must_be_in_range(self):
        self.assertEqual(present_trace.analyze(RAW, PTLOG, 12, 16)["selected_intervals"], 2)
        self.assertEqual(present_trace.analyze(RAW, PTLOG, 14, 14)["selected_intervals"], 0)

    def test_truncation_and_wrong_launch(self):
        with self.assertRaises(ValueError):
            present_trace.analyze(RAW.rsplit(b"# end", 1)[0], PTLOG)
        with self.assertRaises(ValueError):
            present_trace.analyze(RAW, PTLOG.replace("nonce=99", "nonce=100"))
        with self.assertRaises(ValueError):
            present_trace.analyze(RAW, PTLOG.replace("capture=1\n", "capture=10\n"))
        with self.assertRaises(ValueError):
            present_trace.analyze(RAW, PTLOG, prefix="other")

    def test_inconsistent_timestamps_are_rejected(self):
        with self.assertRaises(ValueError):
            present_trace.analyze(RAW.replace(b"11000,10000", b"11000,9999"), PTLOG)

    def test_token_is_the_last_launch(self):
        self.assertEqual(launch_token.token(PTLOG), "1234abcd 99")
        self.assertIsNone(launch_token.token(PTLOG, "other"))
        self.assertIsNone(launch_token.token("vpk-presenttrace: start accepted 1234abcd 99 capture=1\n"))


# ---- embed_pad.py ----
class EmbedPad(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp())
        (self.d / "a.txt").write_text('# a pad script\n\n10 CROSS\n20 "quoted" back\\slash\n')
        (self.d / "b.txt").write_text("# only a comment\n")
        self.out = self.d / "pads.h"
        self.args = [str(self.out), "pad_a=%s" % (self.d / "a.txt"), "pad_b=%s" % (self.d / "b.txt")]

    def tearDown(self):
        shutil.rmtree(self.d)

    def run_tool(self, args):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return embed_pad.main(args)

    def test_write_check_and_stale(self):
        self.assertEqual(self.run_tool(["--check"] + self.args), 1)  # no header yet
        self.assertEqual(self.run_tool(self.args), 0)
        self.assertEqual(self.run_tool(["--check"] + self.args), 0)
        h = self.out.read_text()
        self.assertIn('    "10 CROSS\\n"\n', h)
        self.assertIn('static const char pad_b[] =\n    "";', h)
        (self.d / "a.txt").write_text("10 CIRCLE\n")
        self.assertEqual(self.run_tool(["--check"] + self.args), 1)
        self.assertEqual(self.run_tool([str(self.out), "1bad=x"]), 2)

    def test_header_compiles_to_the_script(self):
        cc = shutil.which("cc") or shutil.which("gcc")
        if not cc:
            self.skipTest("no host C compiler")
        self.run_tool(self.args)
        (self.d / "t.c").write_text('#include <stdio.h>\n#include "pads.h"\n'
                                    'int main(void) { fputs(pad_a, stdout); return pad_b[0]; }\n')
        subprocess.run([cc, "-Wall", "-Werror", "-Wno-unused-const-variable", str(self.d / "t.c"),
                        "-o", str(self.d / "t")], check=True)
        out = subprocess.run([str(self.d / "t")], capture_output=True, text=True, check=True).stdout
        self.assertEqual(out, '10 CROSS\n20 "quoted" back\\slash\n')


if __name__ == "__main__":
    unittest.main()

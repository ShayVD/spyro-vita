#!/usr/bin/env python3
"""relink.py on a fake game: a git repo whose "compiler" hashes a source with its headers and whose "link" writes an
ARM ELF (one Thumb function per object), an eboot.bin and a VPK. A batch checkout builds it in full; a worker clone
changes files and relinks against the batch's build. Python 3, git and bash; no vitasdk.
  python3 targets/vita/tools/test/relink_test.py
"""
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
RELINK = TOOLS / "relink.py"
spec = importlib.util.spec_from_file_location("relink", RELINK)
relink = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relink)

FAKECC = r'''
import hashlib, os, re, sys
a = sys.argv[1:]
src, obj = a[a.index("-c") + 1], a[a.index("-o") + 1]
incs = [x[2:] for x in a if x.startswith("-I")]
def text(p, seen):
    if p in seen:
        return ""
    seen.add(p)
    t = open(p).read()
    for m in re.findall(r'#include "([^"]+)"', t):
        for d in [os.path.dirname(p)] + incs:
            q = os.path.join(d, m)
            if os.path.exists(q):
                t += text(q, seen)
                break
    return t
body = text(src, set())
size = int(re.search(r"SIZE=(\d+)", body).group(1)) if "SIZE=" in body else 16
open(obj, "w").write("%d %s\n" % (size, hashlib.sha256(body.encode()).hexdigest()))
if os.environ.get("FAKE_LOG"):
    open(os.environ["FAKE_LOG"], "a").write(src + "\n")
'''

FAKEBUILD = r'''
import json, os, struct, subprocess, sys, zipfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def sources():
    out = []
    for d in ("src", "lib"):
        if os.path.isdir(os.path.join(ROOT, d)):
            out += sorted(d + "/" + f for f in os.listdir(os.path.join(ROOT, d)) if f.endswith(".c"))
    return out
def obj_of(out, src):
    return os.path.join(out, "obj", src.replace("/", "_")[:-2] + ".o")
def cmd(out, src):
    return [sys.executable, os.path.join(ROOT, "tools", "fakecc.py"), "-Iinclude", "-c", src, "-o", obj_of(out, src)]
def compile_(out, srcs):
    os.makedirs(os.path.join(out, "obj"), exist_ok=True)
    for s in srcs:
        subprocess.run(cmd(out, s), cwd=ROOT, check=True)
def elf(objs):
    text, syms = b"", []
    for name, (size, digest) in objs:
        syms.append((name, 0x81000000 + len(text), size))
        text += (bytes.fromhex(digest) * 4)[:size]
    strtab = b"\0" + b"".join(n.encode() + b"\0" for n, _, _ in syms)
    symtab = b"\0" * 16
    o = 1
    for n, addr, size in syms:
        symtab += struct.pack("<IIIBBH", o, addr | 1, size, 0x12, 0, 1)
        o += len(n) + 1
    shstr = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
    off_text = 84; off_sym = off_text + len(text); off_str = off_sym + len(symtab); off_sh = off_str + len(strtab)
    off_shs = off_sh; off_sh = off_shs + len(shstr)
    off_sh = (off_sh + 3) & ~3
    hdr = b"\x7fELF\x01\x01\x01" + b"\0" * 9 + struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, 0x81000001, 52, off_sh, 0x5000000,
                                                            52, 32, 1, 40, 5, 4)
    hdr += struct.pack("<8I", 1, off_text, 0x81000000, 0x81000000, len(text), len(text), 5, 4)
    sh = b"\0" * 40
    sh += struct.pack("<IIIIIIIIII", 1, 1, 6, 0x81000000, off_text, len(text), 0, 0, 4, 0)
    sh += struct.pack("<IIIIIIIIII", 7, 2, 0, 0, off_sym, len(symtab), 3, 1, 4, 16)
    sh += struct.pack("<IIIIIIIIII", 15, 3, 0, 0, off_str, len(strtab), 0, 0, 1, 0)
    sh += struct.pack("<IIIIIIIIII", 23, 3, 0, 0, off_shs, len(shstr), 0, 0, 1, 0)
    body = hdr + text + symtab + strtab + shstr
    return body + b"\0" * (off_sh - len(body)) + sh
def link(out):
    objs = []
    for s in sources():
        p = obj_of(out, s)
        size, digest = open(p).read().split()
        objs.append(("fn_" + os.path.basename(s)[:-2], (int(size), digest)))
    e = elf(objs)
    open(os.path.join(out, "game.elf"), "wb").write(e)
    open(os.path.join(out, "eboot.bin"), "wb").write(b"SCE\0" + e)
    with zipfile.ZipFile(os.path.join(out, "game.vpk.tmp"), "w") as z:
        z.writestr(zipfile.ZipInfo("eboot.bin", (2020, 1, 1, 0, 0, 0)), b"SCE\0" + e)
        z.writestr(zipfile.ZipInfo("sce_sys/param.sfo", (2020, 1, 1, 0, 0, 0)), b"SFO")
    os.replace(os.path.join(out, "game.vpk.tmp"), os.path.join(out, "game.vpk"))
out = os.path.abspath(sys.argv[2])
if sys.argv[1] == "full":
    compile_(out, sources())
    json.dump([{"directory": ROOT, "file": s, "arguments": cmd(out, s)} for s in sources()],
              open(os.path.join(out, "compile_commands.json"), "w"))
elif sys.argv[1] == "build":  # the reuse build: listed files and missing objects
    listed = [l.strip() for l in open(sys.argv[3]) if l.strip()]
    compile_(out, [s for s in sources() if s in listed or not os.path.exists(obj_of(out, s))])
link(out)
'''

GAME_DB = r'''RELINK_OUT=out
RELINK_ELF=game.elf
RELINK_EBOOT=eboot.bin
RELINK_VPK=game.vpk
RELINK_COMPILE_DB=compile_commands.json
RELINK_IGNORE='\.md$|^docs/'
RELINK_FULL='^tools/'
RELINK_SHARED='^big\.bin$'
relink_link() { python3 tools/fakebuild.py link "$1"; }
'''

GAME_HOOK = r'''RELINK_OUT=out
RELINK_ELF=game.elf
RELINK_EBOOT=eboot.bin
RELINK_VPK=game.vpk
RELINK_IGNORE='\.md$'
relink_build() { python3 tools/fakebuild.py build "$1" "$2"; }
'''


def sh(cwd, *args, env=None):
    p = subprocess.run(list(args), cwd=cwd, capture_output=True, text=True, env=env)
    if p.returncode:
        raise AssertionError("%s: %s%s" % (" ".join(args), p.stdout, p.stderr))
    return p.stdout


def tree_hash(d):
    h = {}
    for dp, _dn, fn in os.walk(d):
        for f in fn:
            p = os.path.join(dp, f)
            h[os.path.relpath(p, d)] = hashlib.sha1(open(p, "rb").read()).hexdigest()
    return h


GIT_ENV = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@t", GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_ALLOW_PROTOCOL="file")


class Relink(unittest.TestCase):
    game = GAME_DB

    def setUp(self):
        self.d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.d)
        os.environ.update({k: GIT_ENV[k] for k in GIT_ENV if k.startswith("GIT_")})
        self.log = self.d / "compiled.txt"
        os.environ["FAKE_LOG"] = str(self.log)
        b = self.batch = self.d / "batch"
        for p, t in {
            "src/a.c": "int a; /* one */\n",
            "src/b.c": '#include "h.h"\nint b;\n',
            "src/c.c": '#include "g.h"\nint c;\n',
            "src/d.c": "int d;\n",
            "include/h.h": "#define H 1\n",
            "include/g.h": '#include "h.h"\n#define G 1\n',
            "tools/fakecc.py": FAKECC,
            "tools/fakebuild.py": FAKEBUILD,
            "tools/vita/relink.game": self.game,
            "docs/notes.md": "notes\n",
            "data.tsv": "x\t1\n",
            ".gitignore": "out/\n",
        }.items():
            (b / p).parent.mkdir(parents=True, exist_ok=True)
            (b / p).write_text(t)
        sh(b, "git", "init", "-q", "-b", "main")
        sh(b, "git", "add", "-A")
        sh(b, "git", "commit", "-qm", "game")
        self.build_full(b)
        self.worker = self.d / "worker"
        sh(self.d, "git", "clone", "-q", str(b), str(self.worker))
        self.before = tree_hash(b / "out")
        self.log.write_text("")

    def build_full(self, b):
        (b / "out").mkdir(exist_ok=True)
        (b / "out" / "big.bin").write_bytes(b"disc" * 1024)
        sh(b, sys.executable, "tools/fakebuild.py", "full", "out")

    def run_relink(self, *args, code=0):
        p = subprocess.run([sys.executable, str(RELINK), "run", "--from", str(self.batch / "out"), "--mode", "copy"]
                           + list(args), cwd=self.worker, capture_output=True, text=True)
        self.assertEqual(p.returncode, code, p.stdout + p.stderr)
        return p.stdout + p.stderr

    def compiled(self):
        return sorted(set(self.log.read_text().split()))

    def tearDown(self):
        # the batch's build is never written through
        if hasattr(self, "before"):
            self.assertEqual(tree_hash(self.batch / "out"), self.before)

    def edit(self, path, text, commit=True):
        (self.worker / path).parent.mkdir(parents=True, exist_ok=True)
        (self.worker / path).write_text(text)
        if commit:
            sh(self.worker, "git", "add", "-A")
            sh(self.worker, "git", "commit", "-qm", "change " + path)

    def test_no_change_is_identical(self):
        out = self.run_relink()
        self.assertIn("to compile (0): none", out)
        self.assertIn("elf: identical", out)
        self.assertIn("vpk: entries that differ: none", out)
        self.assertEqual(self.compiled(), [])

    def test_one_file(self):
        self.edit("src/a.c", "int a; /* two */\n")
        out = self.run_relink()
        self.assertIn("to compile (1): src/a.c", out)
        self.assertEqual(self.compiled(), ["src/a.c"])
        self.assertIn("elf: loaded segments: differ", out)
        self.assertIn("elf: loaded bytes that differ outside the symbols below: 0", out)
        self.assertIn("elf: layout: same", out)
        self.assertIn("elf: 1 symbol(s) bytes differ: fn_a", out)
        self.assertIn("vpk: entries that differ: eboot.bin", out)
        new = self.worker / "out"
        self.assertTrue((new / "relink.txt").read_text().startswith("worker "))
        rc = relink.main(["diff", str(self.batch / "out/game.elf"), str(new / "game.elf"), "--expect", "fn_a"])
        self.assertEqual(rc, 0)
        rc = relink.main(["diff", str(self.batch / "out/game.elf"), str(new / "game.elf"), "--expect", "fn_b"])
        self.assertEqual(rc, 1)

    def test_uncommitted_and_ignored(self):
        self.edit("src/d.c", "int d = 2;\n", commit=False)
        self.edit("docs/notes.md", "more\n", commit=False)
        out = self.run_relink()
        self.assertIn("ignored (RELINK_IGNORE): docs/notes.md", out)
        self.assertEqual(self.compiled(), ["src/d.c"])

    def test_header_dependents(self):
        self.edit("include/h.h", "#define H 2\n")
        out = self.run_relink()
        self.assertIn("headers include/h.h: 2 source(s) include them", out)
        self.assertEqual(self.compiled(), ["src/b.c", "src/c.c"])  # c.c through g.h
        r = relink.elf_diff(str(self.batch / "out/game.elf"), str(self.worker / "out/game.elf"))
        self.assertEqual(sorted(r["changed"]), ["fn_b", "fn_c"])

    def test_resized_function(self):
        self.edit("src/b.c", '#include "h.h"\nint b; /* SIZE=32 */\n')
        out = self.run_relink()
        self.assertIn("symbol(s) resized: fn_b 16 -> 32", out)
        self.assertIn("2 symbol(s) moved", out)  # fn_c and fn_d after it
        self.assertIn("section .text", out)

    def test_refusals(self):
        self.edit("tools/fakecc.py", FAKECC + "\n")
        out = self.run_relink(code=3)
        self.assertIn("needs the full build (RELINK_FULL): tools/fakecc.py", out)
        sh(self.worker, "git", "reset", "-q", "--hard", "HEAD~1")
        self.edit("data.tsv", "x\t2\n")
        out = self.run_relink(code=3)
        self.assertIn("not a source or header and not in RELINK_IGNORE: data.tsv", out)
        out = self.run_relink("--force")
        self.assertIn("--force: not rebuilt from these: data.tsv", out)
        out = self.run_relink("--force", code=2)
        self.assertIn("exists: --replace", out)
        self.run_relink("--force", "--replace")

    def test_new_source_needs_full_build(self):
        self.edit("src/e.c", "int e;\n", commit=False)
        out = self.run_relink(code=3)
        self.assertIn("no compile command in compile_commands.json for: src/e.c", out)

    def test_unfinished_build(self):
        o = self.batch / "out" / "obj" / "src_a.o"
        t = os.path.getmtime(self.batch / "out" / "game.vpk") + 60
        os.utime(o, (t, t))
        self.before = tree_hash(self.batch / "out")
        out = self.run_relink(code=2)
        self.assertIn("is running or failed", out)

    def test_dry_run(self):
        self.edit("src/a.c", "int a = 3;\n")
        out = self.run_relink("--dry-run")
        self.assertIn("to compile (1): src/a.c", out)
        self.assertFalse((self.worker / "out").exists())

    def test_hardlink_mode(self):
        self.edit("src/a.c", "int a = 4;\n")
        p = subprocess.run([sys.executable, str(RELINK), "run", "--from", str(self.batch / "out"), "--mode",
                            "hardlink"], cwd=self.worker, capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        same = lambda f: os.stat(self.batch / "out" / f).st_ino == os.stat(self.worker / "out" / f).st_ino
        self.assertTrue(same("big.bin"))
        self.assertFalse(same("obj/src_a.o"))
        self.assertFalse(same("obj/src_b.o"))

    def test_clone_mode(self):
        self.edit("src/a.c", "int a = 5;\n")
        p = subprocess.run([sys.executable, str(RELINK), "run", "--from", str(self.batch / "out")],
                           cwd=self.worker, capture_output=True, text=True)
        if p.returncode == 2 and "could not clone" in p.stderr:
            self.skipTest("no copy-on-write clones on this file system")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertEqual(self.compiled(), ["src/a.c"])

    def test_submodule(self):
        lib = self.d / "lib"
        lib.mkdir()
        (lib / "l.c").write_text("int l;\n")
        sh(lib, "git", "init", "-q", "-b", "main")
        sh(lib, "git", "add", "-A")
        sh(lib, "git", "commit", "-qm", "lib")
        b = self.batch
        sh(b, "git", "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(lib), "lib")
        sh(b, "git", "commit", "-qm", "lib")
        shutil.rmtree(b / "out")
        self.build_full(b)
        shutil.rmtree(self.worker)
        sh(self.d, "git", "clone", "-q", str(b), str(self.worker))
        sh(self.worker, "git", "-c", "protocol.file.allow=always", "submodule", "update", "-q", "--init")
        self.before = tree_hash(b / "out")
        (self.worker / "lib" / "l.c").write_text("int l = 1;\n")
        sh(self.worker / "lib", "git", "commit", "-qam", "l")
        sh(self.worker, "git", "commit", "-qam", "bump lib")
        out = self.run_relink()
        self.assertIn("to compile (1): lib/l.c", out)
        self.assertIn("bytes differ: fn_l", out)


class RelinkHook(Relink):
    """The same game without a compile_commands.json: its relink_build hook does the reuse build."""
    game = GAME_HOOK

    def test_new_source_needs_full_build(self):
        # the hook's build compiles a new file (its object is missing): nothing to refuse
        self.edit("src/e.c", "int e;\n", commit=False)
        out = self.run_relink()
        self.assertEqual(self.compiled(), ["src/e.c"])
        self.assertIn("symbol(s) added: fn_e", out)

    def test_refusals(self):
        self.edit("tools/fakecc.py", FAKECC + "\n")
        out = self.run_relink(code=3)  # no RELINK_FULL here: not a source, header or ignored
        self.assertIn("not a source or header and not in RELINK_IGNORE: tools/fakecc.py", out)

    def test_uncommitted_and_ignored(self):
        self.edit("src/d.c", "int d = 2;\n", commit=False)
        self.run_relink()
        self.assertEqual(self.compiled(), ["src/d.c"])

    def test_hardlink_mode(self):
        self.skipTest("RELINK_SHARED is the db game's")


class Dependents(unittest.TestCase):
    def test_suffix_match_and_cycles(self):
        d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, d)
        files = {"host/include/platform/vu.h": '#include "x.h"\n', "host/include/x.h": '#include "platform/vu.h"\n',
                 "src/a.c": "#include <platform/vu.h>\n", "src/b.c": '#include "../host/include/x.h"\n',
                 "src/c.c": '#include "vu.h.bak"\n'}
        for p, t in files.items():
            (d / p).parent.mkdir(parents=True, exist_ok=True)
            (d / p).write_text(t)
        import re
        got = relink.dependents(str(d), ["host/include/platform/vu.h"], list(files),
                                re.compile(relink.DEFAULTS["RELINK_SOURCES"]),
                                re.compile(relink.DEFAULTS["RELINK_HEADERS"]))
        self.assertEqual(sorted(got), ["src/a.c", "src/b.c"])


if __name__ == "__main__":
    unittest.main()

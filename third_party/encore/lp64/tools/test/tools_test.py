#!/usr/bin/env python3
"""lp64/tools on a synthetic decomp (fixture/, no game's code), end to end:

  1. p32_patch.py marks it: the expected counts, the comment and the string untouched, the #else of
     --skip-else-of PORT left alone (and taken without the flag), lines only appended at a file's end, and the patch
     it writes applies with `patch -p1` to the same text;
  2. the patched tree compiles for i686 to the same assembly as the original (the 32-bit build doesn't change);
  3. harness.c over the patched tree, built for x86-64 with clang and lp64_p32.h, runs (under RUN64) and finds the
     32-bit layout; over the original tree its layout asserts fail;
  4. layout64.py on the two builds' DWARF: 0 mismatched with HostState listed host-only; HostState reported when it
     isn't; a stale entry reported; the original tree's 64-bit build mismatched;
  5. p32_lint.py: the original tree's labels and function pointer found, the patched tree clean with --host-fn;
  6. cast_audit.py lists code.c's `(int)m`;
  7. trial64.py on a 32-bit compile_commands.json: the layout assert fails plain and passes with --ptr32, where the
     `Moby **` local walking a P32 table is the error left (the one p32_patch.py's T ** rule fixes).

    CLANG=clang-19 CC32=i686-linux-gnu-gcc RUN64=qemu-x86_64 READELF32=i686-linux-gnu-readelf \\
      READELF64=x86_64-linux-gnu-readelf python3 lp64/tools/test/tools_test.py
Defaults: clang, `gcc -m32`, no runner, readelf. Exit 1 on any failure.
Written for the kit; the tools are from spyro-vita tools/ps4/ @ c25a3ea (MIT; D173, D174).
"""
import json, os, re, shlex, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
P32_DIR = os.path.normpath(os.path.join(TOOLS, "..", "runtime", "p32"))
CLANG = shlex.split(os.environ.get("CLANG", "clang"))
CC32 = shlex.split(os.environ.get("CC32", "gcc -m32"))
RUN64 = shlex.split(os.environ.get("RUN64", ""))
READELF32 = os.environ.get("READELF32", "readelf")
READELF64 = os.environ.get("READELF64", "readelf")
CC64 = CLANG + ["--target=x86_64-linux-gnu", "-fms-extensions", "-include", os.path.join(P32_DIR, "lp64_p32.h")]
fails = []


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        fails.append(what)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def tool(name, *args, **kw):
    return run([sys.executable, os.path.join(TOOLS, name)] + list(args), **kw)


def main():
    t = tempfile.mkdtemp(prefix="lp64_tools.")
    try:
        orig, tree, applied = (os.path.join(t, n) for n in ("orig", "tree", "applied"))
        for d in (orig, tree, applied):
            shutil.copytree(os.path.join(HERE, "fixture"), d)

        # 1. p32_patch.py
        patch = os.path.join(t, "p32.patch")
        r = tool("p32_patch.py", tree, "--out", patch, "--list", "--skip-else-of", "PORT")
        print(r.stdout, end="")
        check(r.returncode == 0, "p32_patch.py runs")
        check("P32: 3 fields, 5 globals, 2 other T **, 1 address-initialised (P32_INIT), 2 SDK longs" in r.stderr,
              "p32_patch.py counts: " + r.stderr.strip())
        check(run(["patch", "-p1", "-s", "-i", patch], cwd=applied).returncode == 0, "the patch applies with patch -p1")
        r2 = tool("p32_patch.py", applied, "--skip-else-of", "PORT")
        check("P32: 0 fields, 0 globals, 0 other T **, 0 address-initialised (P32_INIT), 0 SDK longs" in r2.stderr,
              "the patched tree needs nothing more: " + r2.stderr.strip())
        h = open(os.path.join(applied, "include", "game.h")).read()
        c = open(os.path.join(applied, "src", "code.c")).read()
        d = open(os.path.join(applied, "src", "data.c")).read()
        check("Moby *P32 m_Next;" in h and "int *P32 *P32 m_Table;" in h and "unsigned char *P32 m_Bytes[2];" in h,
              "fields marked on every level")
        check("extern Moby *P32 g_List[4];" in h and "u_int m_Flags;" in h, "a label marked, an SDK long made u_int")
        check("void (*g_Update)(Moby *);" in h and "Moby *g_Fake;" in h, "the function pointer and the comment untouched")
        check("Moby *P32 *w;" in c and "*(Moby *P32 *)p" in c and "((u_int *)p)[1]" in c, "T ** and an SDK long store")
        check("char *P32_INIT(g_Names)[3] = " in d and d.rstrip().endswith("P32_INIT_ARRAY(char, g_Names)"),
              "an address-initialised table as P32_INIT, filled at the file's end")
        check('char *g_Other[2] = {"x", "y"};' in d and '"Moby *g_InString;"' in d,
              "the #else of #ifdef PORT and the string untouched")
        for f in ("include/game.h", "src/code.c"):
            check(open(os.path.join(orig, f)).read().count("\n") == open(os.path.join(applied, f)).read().count("\n"),
                  f"{f}: no line added or removed")
        r3 = tool("p32_patch.py", tree, "--long-names")
        check("2 address-initialised" in r3.stderr, "without --skip-else-of the #else's table is taken too")
        r4 = tool("p32_patch.py", tree, "--long-names", "--list", "--skip-else-of", "PORT")
        check("Moby *LP64_P32 m_Next;" in r4.stdout and "LP64_P32_INIT(g_Names)" in r4.stdout, "--long-names")

        # 2. 32-bit: the same code with the patch as without
        same = True
        for f in ("src/code.c", "src/data.c"):
            asm = []
            for root in (orig, applied):
                o = os.path.join(t, "a.s")
                r = run(CC32 + ["-O1", "-I", os.path.join(root, "include"), "-I", P32_DIR, "-include", "lp64_p32.h",
                                "-S", os.path.join(root, f), "-o", o])
                check(r.returncode == 0, f"i686 {os.path.basename(root)}/{f} compiles" + (": " + r.stderr if r.returncode else ""))
                asm.append([l for l in open(o) if not re.match(r"\s*\.(file|ident)\b", l)] if r.returncode == 0 else [])
            same = same and asm[0] == asm[1] and asm[0]
        check(same, "i686: the patched tree gives the original's assembly")

        # 3. x86-64: the patched tree runs; the original's layout asserts fail
        exe = os.path.join(t, "harness64")
        srcs = lambda root: [os.path.join(root, "src", "code.c"), os.path.join(root, "src", "data.c"),
                             os.path.join(HERE, "harness.c")]
        r = run(CC64 + ["-O1", "-g", "-w", "-static", "-fno-pic", "-I", os.path.join(applied, "include")]
                + srcs(applied) + ["-o", exe])
        check(r.returncode == 0, "x86-64 build of the patched tree" + (": " + r.stderr[:2000] if r.returncode else ""))
        if r.returncode == 0:
            r = run(RUN64 + [exe])
            print(r.stdout, end="")
            check(r.returncode == 0, "the patched tree runs on x86-64")
        r = run(CLANG + ["--target=x86_64-linux-gnu", "-fsyntax-only", "-w", "-I", os.path.join(orig, "include"),
                         os.path.join(HERE, "harness.c")])
        check(r.returncode != 0 and "Moby's 32-bit" in r.stderr, "the original tree's layout fails on x86-64")

        # 4. layout64.py
        exe32 = os.path.join(t, "harness32")
        r = run(CC32 + ["-O1", "-g", "-w", "-I", P32_DIR, "-include", "lp64_p32.h", "-I",
                        os.path.join(applied, "include")] + srcs(applied) + ["-o", exe32])
        check(r.returncode == 0, "i686 build of the patched tree" + (": " + r.stderr if r.returncode else ""))
        dumps = {}
        for name, re_, e in (("32", READELF32, exe32), ("64", READELF64, exe)):
            dumps[name] = os.path.join(t, f"l{name}.txt")
            with open(dumps[name], "w") as fh:
                subprocess.run([re_, "--debug-dump=info", e], stdout=fh, check=False)
        host_only = os.path.join(t, "host_only.txt")
        open(host_only, "w").write("# the host's own\nHostState   harness.c: a host pointer\n")
        r = tool("layout64.py", dumps["32"], dumps["64"], "--host-only", host_only)
        print(r.stdout, end="")
        check(r.returncode == 0 and re.search(r"layout64: \d+ types compared, 0 mismatched, 1 host-only", r.stdout),
              "layout64: Moby matches, HostState host-only")
        r = tool("layout64.py", dumps["32"], dumps["64"])
        check(r.returncode == 1 and "MISMATCH HostState" in r.stdout, "layout64: HostState reported when not listed")
        open(host_only, "a").write("Moby  wrongly listed\n")
        r = tool("layout64.py", dumps["32"], dumps["64"], "--host-only", host_only)
        check(r.returncode == 1 and "STALE host-only entry (no difference, or not in both builds): Moby" in r.stdout,
              "layout64: a stale entry reported")
        bad64 = os.path.join(t, "bad64.o")
        r = run(CLANG + ["--target=x86_64-linux-gnu", "-g", "-w", "-c", "-DGAME_LAYOUT_UNCHECKED", "-I",
                         os.path.join(orig, "include"), "-x", "c", "-", "-o", bad64],
                input='#include "game.h"\nMoby g_M;\n')
        with open(os.path.join(t, "bad64.txt"), "w") as fh:
            subprocess.run([READELF64, "--debug-dump=info", bad64], stdout=fh, check=False)
        r = tool("layout64.py", dumps["32"], os.path.join(t, "bad64.txt"))
        check(r.returncode == 1 and "MISMATCH Moby: size 24 vs 48" in r.stdout,
              "layout64: the unpatched 64-bit Moby mismatched: " + r.stdout.strip().split("\n")[0])

        # 5. p32_lint.py
        labels = os.path.join(t, "labels.map")
        open(labels, "w").write("".join(f"                0x8007{i:04x}                {n}\n" for i, n in
                                        enumerate(["g_Player", "g_List", "g_Names", "g_Update", "g_Text"])))
        r = tool("p32_lint.py", labels, orig)
        print(r.stdout, end="")
        check(r.returncode == 1 and "p32_lint: 5 labels, 5 found" in r.stdout,
              "p32_lint: the original's labels and function pointer found")
        r = tool("p32_lint.py", labels, applied)
        check(r.returncode == 1 and "function pointer label not the port's own" in r.stdout and "1 found" in r.stdout,
              "p32_lint: the patched tree's function pointer still reported")
        r = tool("p32_lint.py", labels, applied, "--host-fn", "g_Update")
        check(r.returncode == 0 and "p32_lint: 5 labels, 0 found" in r.stdout, "p32_lint: clean with --host-fn")

        # 6. cast_audit.py over a 64-bit compile_commands.json
        b64 = os.path.join(t, "build64")
        os.makedirs(b64)
        cmd = " ".join(["timeout", "300"] + CC64 + ["-I", os.path.join(applied, "include"), "-w", "-c"])
        json.dump([{"directory": b64, "file": os.path.join(applied, "src", f),
                    "command": f"{cmd} -o {f}.o {os.path.join(applied, 'src', f)}"} for f in ("code.c", "data.c")],
                  open(os.path.join(b64, "compile_commands.json"), "w"))
        r = tool("cast_audit.py", b64)
        print(r.stdout, end="")
        check(r.returncode == 0 and re.search(r"cast_audit: 1 sites in 1 files", r.stdout)
              and "return (int)m;" in r.stdout, "cast_audit: code.c's (int)m listed")

        # 7. trial64.py over a 32-bit compile_commands.json
        b32 = os.path.join(t, "build32")
        os.makedirs(b32)
        open(os.path.join(orig, "src", "layout.c"), "w").write(
            '#include "game.h"\n#include <stddef.h>\n'
            '_Static_assert(offsetof(Moby, m_Bytes) == 12, "Moby\'s pointers are 4 bytes");\n')
        json.dump([{"directory": b32, "file": os.path.join(orig, "src", f),
                    "command": f"/usr/bin/i686-linux-gnu-gcc -m32 -O1 -Wall -I{os.path.join(orig, 'include')} "
                               f"-o {f}.o -c {os.path.join(orig, 'src', f)}"} for f in ("code.c", "layout.c")],
                  open(os.path.join(b32, "compile_commands.json"), "w"))
        db = os.path.join(b32, "compile_commands.json")
        r = tool("trial64.py", "--db", db, "--out", os.path.join(t, "trial"), "--clang", CLANG[0])
        print(r.stdout, end="")
        check("failed to compile: 1" in r.stdout and "error: static assertion" in r.stdout
              and "-Wpointer-to-int-cast" in r.stdout, "trial64 plain: the layout assert fails, the cast counted")
        r = tool("trial64.py", "--db", db, "--out", os.path.join(t, "trial-p32"), "--clang", CLANG[0],
                 "--ptr32", os.path.join(orig, "include"))
        print(r.stdout, end="")
        check("failed to compile: 1" in r.stdout and "failed: ../orig/src/code.c" in r.stdout
              and "static assertion" not in r.stdout and "changes address space" in r.stdout
              and "6 pointer fields and labels marked" in r.stdout,
              "trial64 --ptr32: the layout holds; code.c's Moby ** local over a P32 table is what's left")
    finally:
        shutil.rmtree(t, ignore_errors=True)
    if fails:
        print(f"lp64 tools tests: {len(fails)} failed")
        return 1
    print("lp64 tools tests: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

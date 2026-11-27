#!/usr/bin/env python3
"""The offset model's tools on a stand-in decomp and port (offset_fixture/, no game's code), end to end, in the order
spyro-vita took (its O2 to O4, D258, D262, D265, D267):

  1. p32_offset_check.py on the low-image build (x86-64) before the rewrite: the uses that skip the accessors fail;
  2. p32_offset_rewrite.py --apply: every load, store, compound and step rewritten (in ordinary text, a #define's
     body, a whole macro's use), the token paste listed; the decomp's edits as a patch that applies with patch -p1, the
     port's in place, no line moved;
  3. after the listed site is done by hand, p32_offset_check.py passes, and its mutant (an accessor removed) fails;
  4. the rewritten tree compiles for i686 to the same assembly as the original (the 32-bit builds don't change);
  5. p32_offset_rewrite.py --casts on the offset-model build (aarch64): `(int)m` gets P32_ENC, `(Moby *)i` P32_DEC,
     `(int)P32_GET(x)` P32_ENC by the textual pass; the result compiles there;
  6. p32_narrow_check.py lists the port's host address cut to 32 bits; --raw lists the slot read as a word, not the
     decoded one;
  7. p32_decl_check.py: clean, and with a unit declaring a slot as a plain pointer, that name;
  8. on macOS only: macho_syms.py's three edits on a Mach-O object (nm shows them, and a link proves the localised
     symbol is gone); asm_macho.py's output assembles and links, its data read back by C.

    CLANG=clang python3 lp64/tools/test/offset_tools_test.py
CLANG (default: clang-19 if found, else clang) must target x86_64-linux-gnu, i686-linux-gnu and aarch64-linux-gnu
(-ffreestanding: no system headers). Exit 1 on any failure.
Written for the kit; the tools are from spyro-vita tools/ps4/ and tools/ @ 36d9e07 (MIT).
"""
import json, os, platform, re, shlex, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
P32_DIR = os.path.normpath(os.path.join(TOOLS, "..", "runtime", "p32"))
CLANG = shlex.split(os.environ.get("CLANG") or (shutil.which("clang-19") or "clang"))
X64 = ["--target=x86_64-linux-gnu", "-fms-extensions"]
A64 = ["--target=aarch64-linux-gnu"]
fails = []


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        fails.append(what)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def tool(name, *args, **kw):
    return run([sys.executable, os.path.join(TOOLS, name)] + list(args), **kw)


def ccdb(root, target, files, extra=()):
    """compile_commands.json for the stand-in game's build, absolute paths, as CMake writes it"""
    b = os.path.join(root, "build-" + target[0].split("=")[1].split("-")[0])
    os.makedirs(b, exist_ok=True)
    flags = CLANG + target + ["-ffreestanding", "-std=gnu11", "-O1", "-include", os.path.join(P32_DIR, "lp64_p32.h"),
                              "-I", os.path.join(root, "decomp", "include")] + list(extra)
    db = [{"directory": b, "file": f, "command": shlex.join(flags + ["-c", f, "-o", os.path.basename(f) + ".o"])}
          for f in files]
    with open(os.path.join(b, "compile_commands.json"), "w") as fh:
        json.dump(db, fh, indent=1)
    return b, flags


def asm(flags, f):
    r = run(flags + ["-S", "-o", "-", f])
    return r.returncode, "\n".join(l for l in r.stdout.split("\n") if not re.match(r"\s*\.(file|ident)\b", l))


def main():
    t = os.path.realpath(tempfile.mkdtemp(prefix="lp64_offset_tools."))
    try:
        root, orig = os.path.join(t, "game"), os.path.join(t, "orig")
        for d in (root, orig):
            shutil.copytree(os.path.join(HERE, "offset_fixture"), d)
        game_c, port_c = os.path.join(root, "decomp", "src", "game.c"), os.path.join(root, "src", "port.c")
        units = [game_c, port_c]
        b64, f64 = ccdb(root, X64, units)

        # 1. before the rewrite, the completeness check fails
        r = tool("p32_offset_check.py", b64)
        print(r.stdout, end="")
        check(r.returncode == 1 and "failed: 2" in r.stdout, "p32_offset_check.py fails before the rewrite")

        # 2. the rewrite
        patches = os.path.join(t, "patches")
        lst = os.path.join(t, "listed.txt")
        before = {f: open(f).read() for f in (game_c, port_c, os.path.join(root, "decomp", "include", "game.h"))}
        r = tool("p32_offset_rewrite.py", b64, "--root", root, "--decomp", os.path.join(root, "decomp"),
                 "--patch-dir", patches, "--src", os.path.join(root, "src"), "--apply", "--list", lst)
        print(r.stdout, end="")
        print(r.stderr, end="")
        check(r.returncode == 0, "p32_offset_rewrite.py runs")
        m = re.search(r"rewritten: (\d+) sites in (\d+) files .*; listed: (\d+)", r.stdout)
        check(m is not None and m.group(2) == "3" and m.group(3) == "1",
              "rewritten in 3 files (game.c, game.h, port.c), 1 listed: " + (m.group(0) if m else r.stdout))
        check("listed, token paste: 1" in r.stdout, "the token paste listed")
        check(open(port_c).read() != before[port_c], "the port's own C edited in place")
        check(open(game_c).read() == before[game_c], "the decomp's tree left as it was (its edits are a patch)")
        pf = sorted(os.listdir(patches)) if os.path.isdir(patches) else []
        check(pf == ["0001-p32-accessors-1.patch"], f"one patch part: {pf}")
        p = run(["patch", "-p1", "-s", "-i", os.path.join(patches, pf[0])], cwd=os.path.join(root, "decomp")) \
            if pf else None
        check(p is not None and p.returncode == 0, "the patch applies with patch -p1")
        g, h, pc = (open(f).read() for f in (game_c, os.path.join(root, "decomp", "include", "game.h"), port_c))
        for f, old in before.items():
            check(open(f).read().count("\n") == old.count("\n"), f"no line moved in {os.path.basename(f)}")
        for want in ("for (p = P32_GET(g_Head); p; p = P32_GET(p->m_Next))", "P32_SET(m->m_Next , P32_GET(g_Head));",
                     "P32_SET(g_Head , m);", "P32_ADD(m->m_Data , 4);", "P32_SUB(m->m_Data , 2);",
                     "*P32_POSTINC(m->m_Data)", "*P32_PREDEC(m->m_Data)", "P32_SET(m->m_Next , 0);",
                     "P32_GET(FIRST_MOBY)->m_Type", "P32_GET(SLOT_OF(Spare))->m_Flags", "P32_SET(m->m_List , g_Table);",
                     "(*P32_GET(m->m_List))", "(int)P32_GET(g_Head)"):
            check(want in g, "game.c: " + want)
        check("((m)->P32_GET(m_Next)->m_Type)" in h or "(P32_GET((m)->m_Next)->m_Type)" in h,
              "game.h: NEXT_TYPE's body rewritten once")
        check("for (m = P32_GET(g_Head); m; m = P32_GET(m->m_Next))" in pc, "port.c: the walk")
        check("game.c" in open(lst).read() and "token paste" in open(lst).read(), "--list names the token paste")

        # 3. the listed site by hand; then the check passes, and an accessor removed fails it
        game_h = os.path.join(root, "decomp", "include", "game.h")
        open(game_h, "w").write(h.replace("#define SPARE_FLAGS(n) (g_##n->m_Flags)",
                                          "#define SPARE_FLAGS(n) (P32_GET(g_##n)->m_Flags)"))
        r = tool("p32_offset_check.py", b64)
        print(r.stdout, end="")
        check(r.returncode == 0 and "diagnostics: 0" in r.stdout, "p32_offset_check.py passes after the rewrite")
        open(game_c, "w").write(g.replace("P32_SET(g_Head , m);", "g_Head = m;"))
        r = tool("p32_offset_check.py", b64)
        check(r.returncode == 1, "p32_offset_check.py's mutant (one store without its accessor) fails")
        open(game_c, "w").write(g)

        # 4. the 32-bit build doesn't change
        f32 = CLANG + ["--target=i686-linux-gnu", "-ffreestanding", "-std=gnu11", "-O1", "-include",
                       os.path.join(P32_DIR, "lp64_p32.h")]
        for f in ("decomp/src/game.c", "src/port.c"):
            ra = asm(f32 + ["-I", os.path.join(root, "decomp", "include")], os.path.join(root, f))
            rb = asm(f32 + ["-I", os.path.join(orig, "decomp", "include")], os.path.join(orig, f))
            check(ra[0] == 0 and rb[0] == 0 and ra[1] == rb[1], f"i686: {f} the same assembly before and after")

        # 5. the casts, on the offset-model build
        ba64, fa64 = ccdb(root, A64, units)
        r = tool("p32_offset_rewrite.py", ba64, "--casts", "--root", root, "--decomp", os.path.join(root, "decomp"),
                 "--patch-dir", patches, "--src", os.path.join(root, "src"), "--apply")
        print(r.stdout, end="")
        check(r.returncode == 0 and "textual pass:" in r.stdout, "p32_offset_rewrite.py --casts runs")
        cp = os.path.join(patches, "0002-p32-casts-1.patch")
        p = run(["patch", "-p1", "-s", "-i", cp], cwd=os.path.join(root, "decomp")) if os.path.exists(cp) else None
        check(p is not None and p.returncode == 0, "the casts' patch applies")
        g = open(game_c).read()
        check("(int)P32_ENC(m) + (int)P32_ENC(P32_GET(g_Head))" in g, "(int)m and (int)P32_GET(x) encoded")
        check("(Moby *)P32_DEC(i)" in g, "(Moby *)i decoded")
        print("\n".join(l for l in g.split("\n") if "cast_nested" in l))
        check("(int)P32_ENC(P32_GET(*P32_GET(m->m_List)))" in g, "an accessor nested in another's argument encoded")
        for f in units:
            check(run(fa64 + ["-fsyntax-only", "-Werror=int-conversion", f]).returncode == 0,
                  f"aarch64: {os.path.basename(f)} compiles in the offset model")

        # 6. narrowings and raw reads, on the offset-model build
        r = tool("p32_narrow_check.py", ba64, "--root", root)
        print(r.stdout, end="")
        check(re.search(r"^src/port\.c:20: CStyl unsigned long -> unsigned int: return \(u32\)at;", r.stdout, re.M)
              is not None and r.stdout.count("\n") == 1, "p32_narrow_check.py lists the host address cut to 32 bits")
        r = tool("p32_narrow_check.py", ba64, "--root", root, "--raw")
        print(r.stdout, end="")
        check(re.search(r"^src/port\.c:24: Impli unsigned int -> unsigned long:", r.stdout, re.M) is not None
              and r.stdout.count("\n") == 1, "--raw lists the slot read as a word, not the decoded one")

        # 7. declarations across units
        r = tool("p32_decl_check.py", ba64, "--root", root)
        print(r.stdout, end="")
        check(r.returncode == 0 and "slot/pointer mismatches: 0" in r.stdout, "p32_decl_check.py: clean")
        bbad, _ = ccdb(root, A64, units + [os.path.join(root, "src", "decl_bad.c")])
        r = tool("p32_decl_check.py", bbad, "--root", root)
        check(r.returncode == 1 and re.search(r"^g_Spare$", r.stdout, re.M) is not None,
              "p32_decl_check.py names a slot declared as a pointer elsewhere")

        # 8. Mach-O
        if platform.system() == "Darwin":
            macho(t)
        else:
            print("skip  macho_syms.py, asm_macho.py (macOS only)")
    finally:
        shutil.rmtree(t, ignore_errors=True)
    if fails:
        print(f"offset tools: {len(fails)} failed")
        return 1
    print("offset tools: ok")
    return 0


def macho(t):
    lib = os.path.join(t, "lib.c")
    open(lib, "w").write("int keep(void);\nint helper(void) { return 2; }\nint other(void) { return 3; }\n"
                         "int keep(void) { return helper() + other(); }\nint ext_call(void);\n"
                         "int calls(void) { return ext_call(); }\n")
    o, out = os.path.join(t, "lib.o"), os.path.join(t, "lib2.o")
    check(run(["cc", "-c", "-O0", lib, "-o", o]).returncode == 0, "macOS: a relocatable object")
    r = tool("macho_syms.py", "--keep-global-symbol", "keep", "--keep-global-symbol", "calls",
             "--redefine-sym", "ext_call=renamed_call", o, out)
    check(r.returncode == 0, "macho_syms.py runs: " + r.stderr.strip())
    nm = run(["nm", "-m", out]).stdout
    check(re.search(r"non-external.* _helper$", nm, re.M) is not None and
          re.search(r"non-external.* _other$", nm, re.M) is not None, "helper and other made local")
    check(re.search(r"\) external _keep$", nm, re.M) is not None, "keep stays global")
    check(re.search(r"external _renamed_call", nm) is not None and "_ext_call" not in nm, "ext_call renamed")
    main_c = os.path.join(t, "main.c")
    open(main_c, "w").write("int keep(void);\nint renamed_call(void) { return 4; }\nint calls(void);\n"
                            "int main(void) { return keep() + calls() == 9 ? 0 : 1; }\n")
    exe = os.path.join(t, "m")
    check(run(["cc", main_c, out, "-o", exe]).returncode == 0 and run([exe]).returncode == 0,
          "the edited object links and runs")
    open(main_c, "a").write("int helper(void);\nint use_helper(void) { return helper(); }\n")
    check(run(["cc", main_c, out, "-o", exe]).returncode != 0, "a localised symbol can't be linked to")
    # asm_macho.py
    s = os.path.join(t, "data.s")
    open(s, "w").write("\n".join([
        "/* ELF-flavoured data, as a port generates it */", ".section .rodata", ".balign 4",
        ".globl g_Words", ".type g_Words, @object", "g_Words: .long 0x11, 0x22 /* two */",
        ".size g_Words, 8", ".data", ".balign 8", ".globl g_Ptr", "g_Ptr:", ".quad g_Words + 4",
        ".globl g_Len", "g_Len: .long .Lend - .Lstart", ".Lstart:", ".asciz \"lp64\"", ".Lend:",
        ".set g_Alias, g_Words", ".globl g_Alias", '.section .note.GNU-stack,"",%progbits', ""]))
    r = tool("asm_macho.py", s, os.path.join(t, "data_m.s"))
    conv = open(os.path.join(t, "data_m.s")).read() if r.returncode == 0 else ""
    check("_g_Words:" in conv and "Lstart:" in conv and ".type" not in conv and "GNU-stack" not in conv
          and ".section __TEXT,__const" in conv, "asm_macho.py: underscores, local labels, sections")
    open(main_c, "w").write("extern const unsigned g_Words[2], g_Alias[2]; extern const unsigned *g_Ptr;\n"
                            "extern unsigned g_Len;\nint main(void) { return g_Words[1] == 0x22 && *g_Ptr == 0x22"
                            " && g_Len == 5 && g_Alias[1] == 0x22 ? 0 : 1; }\n")
    r = run(["cc", main_c, os.path.join(t, "data_m.s"), "-o", exe])
    check(r.returncode == 0 and run([exe]).returncode == 0, "asm_macho.py's output assembles, links and reads back: "
          + r.stderr.strip()[-600:])


if __name__ == "__main__":
    sys.exit(main())

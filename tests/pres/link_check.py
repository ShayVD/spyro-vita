#!/usr/bin/env python3
"""The presenter's link check (stage 7e, D124; ctest pres_link).

  link_check.py OBJDUMP NM PRES_OBJ GAME_OBJ...

PRES_OBJ is pres_draw.o, the draw path built again (cmake/presenter.cmake).
From its entry, GamestateDraw_pres, every function it reaches is followed
(the other gamestates' draws, 7g's, are not: the presenter draws play
only, and the names the check stops at are listed below). What those
functions name from outside must be one of:
  - the presenter's own state and functions (g_pres_*, pres_*): its
    context (render_ctx.h), its snapshot (pres_snap_names.h), its
    renderers (NAME_pres, 7d);
  - a read-only name (READ_ONLY below): the game's constant tables, the
    level's data and the pointers set when a level loads;
  - its output on the game thread (OUTPUT below), which 7f routes;
  - a function of the game's that names no writable data at all, itself
    or through what it calls ("shared"), found in GAME_OBJ.
What the presenter's object defines itself must be functions, constant
data, or a local the list below names (PRIVATE). A common symbol (the
decomp's uninitialised globals, -fcommon) is the game's, so an outside
name.
"""
import re
import subprocess
import sys

STOP = set("""func_8001A050 func_8001A40C func_8001CA38 func_8001CFDC func_8001C694
func_8001D718 func_8001E24C func_8001E6B8 TitlescreenDraw func_8001E9C8
func_credits_8007BFD0 func_8001EB80 D_8007567C""".split())

# read only during play (each checked: the draw path built again stores
# through none of them, by name; the --pres-check frames would differ if a
# copy were needed)
READ_ONLY = r"""
^(memcpy|memset|sprintf|snprintf|mips2c_div|__divdi3|__udivdi3|__moddi3|__umoddi3)$
^g_psx_arena_base$
^port_ws_(hud_ofx|hud_r11|r1112)$
^port_poly_start$
^g_present$
^(g_Environment|g_EnvironmentAnimations|g_Models|g_SkipLowPolyWorld|g_Buffers|g_LevelId)$
^D_8006[0-9A-F]{4}$
^D_80074B84$
"""

# the presenter's output, on the game thread in 7e (7f gives it its own)
OUTPUT = r"""
^(DrawOTag|PutDrawEnv|PutDispEnv|DrawSync)$
^port_present_note_2d$
^g_psx_walk_nd$
"""

PRIVATE = set()

FN_RE = re.compile(r"^([0-9a-f]+) <(.+)>:$")
REL_RE = re.compile(r"^\s+[0-9a-f]+: (R_\w+)\s+(\S+?)(?:[-+]0x[0-9a-f]+)?$")
CALL_RE = re.compile(r"\b(?:call|bl|blx|b)\s+[0-9a-f]+ <([^>+]+)>")


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def functions(objdump, path):
    out = run(objdump, "-dr", "--no-show-raw-insn", path)
    funcs, cur = {}, None
    for line in out.splitlines():
        m = FN_RE.match(line)
        if m:
            cur = m.group(2)
            funcs.setdefault(cur, set())
            continue
        if cur is None:
            continue
        m = REL_RE.match(line)
        if m:
            funcs[cur].add(m.group(2))
            continue
        m = CALL_RE.search(line)
        if m:
            funcs[cur].add(m.group(1))
    return funcs


def symtab(nm, path):
    """name -> type for every symbol nm lists (U for undefined)"""
    t = {}
    for line in run(nm, path).splitlines():
        p = line.split()
        if len(p) == 2:
            t.setdefault(p[1], p[0])
        elif len(p) == 3:
            if t.get(p[2], "U") == "U":
                t[p[2]] = p[1]
    return t


def main():
    objdump, nm, pres = sys.argv[1:4]
    game_objs = [o for a in sys.argv[4:] for o in a.split(";") if o]
    ro = re.compile("|".join(x for x in READ_ONLY.split("\n") if x))
    out = re.compile("|".join(x for x in OUTPUT.split("\n") if x))
    pf = functions(objdump, pres)
    pt = symtab(nm, pres)
    if "GamestateDraw_pres" not in pf:
        sys.exit("pres_link: no GamestateDraw_pres in %s" % pres)
    # the game's functions and what they name, for the shared ones
    gf, gt = {}, {}
    for o in game_objs:
        t = symtab(nm, o)
        for fn, names in functions(objdump, o).items():
            if t.get(fn, "t").isupper():
                gf.setdefault(fn, (names, t, o))
        for s, ty in t.items():
            if ty != "U":
                gt.setdefault(s, ty)

    def shared(fn, seen):
        """'' if fn (a game function) names no writable data, else why not"""
        if fn in seen:
            return ""
        seen.add(fn)
        if fn not in gf:
            return "" if ro.match(fn) else "not found"
        names, t, o = gf[fn]
        for s in names:
            ty = t.get(s, gt.get(s, "U"))
            if s.startswith(".rodata") or ty in "rR":
                continue
            if s.startswith(".text") or ty in "tT":
                why = shared(s, seen) if not s.startswith(".") else ""
                if why:
                    return "%s -> %s" % (s, why)
                continue
            if ty == "U" and (s in gf or s in ("memcpy", "memset", "memmove", "memcmp", "strlen")):
                why = shared(s, seen)
                if why:
                    return "%s -> %s" % (s, why)
                continue
            if ro.match(s):
                continue
            return "names %s" % s
        return ""

    bad, reached, outside, private = [], set(), set(), set()
    work = ["GamestateDraw_pres"]
    while work:
        fn = work.pop()
        if fn in reached:
            continue
        reached.add(fn)
        for s in sorted(pf.get(fn, ())):
            ty = pt.get(s)
            if s in STOP:
                continue
            if s in pf:
                work.append(s)
                continue
            if s.startswith(".rodata") or ty in ("r", "R"):
                continue
            if s.startswith((".bss", ".data", ".sbss", ".sdata")) or ty in ("b", "d", "B", "D"):
                private.add("%s (in %s)" % (s, fn))
                continue
            outside.add(s)
    for s in sorted(outside):
        if s.startswith(("g_pres_", "pres_")) or s.endswith("_pres") or ro.match(s) or out.match(s):
            continue
        why = shared(s, set())
        if why:
            bad.append("  %s (%s)" % (s, why))
    for s in sorted(private - PRIVATE):
        bad.append("  %s (state of the presenter's object's own)" % s)
    if bad:
        print("pres_link: the presenter's draw names what isn't its own, read only, its output or shared:")
        print("\n".join(bad))
        sys.exit(1)
    print("pres_link: %d functions reached from GamestateDraw_pres; %d outside names, each the "
          "presenter's own, read only, its output or shared" % (len(reached), len(outside)))


if __name__ == "__main__":
    main()

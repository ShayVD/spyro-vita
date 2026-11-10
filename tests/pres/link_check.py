#!/usr/bin/env python3
"""The presenter's link check (stage 7e, D125; ctest pres_link).

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
  - its output (OUTPUT below): empty since stage 7f (D126), which maps
    DrawOTag, PutDrawEnv, PutDispEnv, DrawSync and port_present_note_2d
    onto present.c's presenter versions (pres_*), so it runs beside the
    logic draw on a core of its own;
  - a function of the game's that names no writable data at all, itself
    or through what it calls ("shared"), found in GAME_OBJ.
What the presenter's object defines itself must be functions, constant
data, or a local the list below names (PRIVATE). A common symbol (the
decomp's uninitialised globals, -fcommon) is the game's, so an outside
name.
"""
import os
import re
import subprocess
import sys

# Since stage 7g (D128) every gamestate's draw is followed; 7e and 7f
# stopped at the other gamestates' (func_8001A050 and the rest).
STOP = set()

# read only (each checked: the draw path built again stores through none
# of them, by name; the --pres-check frames would differ if a copy were
# needed). Stage 7g (D128) adds the decomp's strings.c tables (constant C
# data, outside the data region the snapshot mirrors) and the kernel RAM
# base the port's small addresses go to, and the native build's
# translation of a PS1 address (psx_kuseg_host, port.c: PSX_KUSEG), which
# only PlaySound reaches, through the sound reference it is given, NULL in
# every call the draws make (the flight results'); --pres-write-check holds
# that the presenter writes none of the game's memory.
READ_ONLY = r"""
^(memcpy|memset|sprintf|snprintf|strcpy|strlen|mips2c_div|__divdi3|__udivdi3|__moddi3|__umoddi3)$
^__aeabi_(idiv|uidiv|idivmod|uidivmod|ldivmod|uldivmod)$
^g_psx_arena_base$
^g_psx_kernel_ram$
^psx_kuseg_host$
^(g_DragonNames|g_HomeworldNames|g_HomeworldNamesLength|g_LevelNames|g_BalloonistDialogue|g_BalloonistDialogueIndices|g_BalloonistNames)$
^port_ws_(hud_ofx|hud_r11|r1112)$
^port_poly_start$
^g_present$
^(g_Environment|g_EnvironmentAnimations|g_Models|g_SkipLowPolyWorld|g_Buffers|g_LevelId)$
^D_8006[0-9A-F]{4}$
^D_80074B84$
"""

# the presenter's output of the game's: none since 7f (D126); 7e's were
# DrawOTag, PutDrawEnv, PutDispEnv, DrawSync, port_present_note_2d and
# g_psx_walk_nd
OUTPUT = r"""
"""

PRIVATE = set()

FN_RE = re.compile(r"^([0-9a-f]+) <(.+)>:$")
REL_RE = re.compile(r"^\s+[0-9a-f]+: (R_\w+)\s+(\S+?)(?:[-+]0x[0-9a-f]+)?$")
CALL_RE = re.compile(r"\b(?:call|bl|blx|b)\s+[0-9a-f]+ <([^>+]+)>")
# an indirect call (x86 call *..., ARM blx through a register): a function
# pointer the by-name check can't follow
INDIRECT_RE = re.compile(r"\bcall\s+\*|\bblx\s+(?:r\d+|ip|sl|fp|lr)\b")
# Stage 7g (D128): the only indirect calls allowed, each through a pointer
# pres_ovl.c returns (pres_names.h maps the game's overlay pointers onto
# those): draw.c's flight results (D_8007567C), the balloonist's talk
# (D_800758D8) and the flight results' time (D_800758C4)
OVL_ROOTS = re.compile(r"^(Flight5_LEVEL_|Flight4_LEVEL_|Balloonist4_LEVEL_)\d+$")
INDIRECT_OK = re.compile(r"^(GamestateDraw_pres|func_8001E24C|Flight5_LEVEL_\d+)$")


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
        if INDIRECT_RE.search(line):
            funcs[cur].add("*indirect*")
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
    out_lines = [x for x in OUTPUT.split("\n") if x]
    out = re.compile("|".join(out_lines)) if out_lines else None
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
    # the entry, and the overlay functions pres_ovl.c's tables hold (reached
    # through the pointers it returns, which the walk can't follow)
    work = ["GamestateDraw_pres"] + sorted(f for f in pf if OVL_ROOTS.match(f))
    while work:
        fn = work.pop()
        if fn in reached:
            continue
        reached.add(fn)
        for s in sorted(pf.get(fn, ())):
            ty = pt.get(s)
            if s in STOP:
                continue
            if s == "*indirect*":
                if not INDIRECT_OK.match(fn):
                    bad.append("  %s calls through a pointer" % fn)
                continue
            if s in pf:
                work.append(s)
                continue
            if s.startswith(".rodata") or ty in ("r", "R"):
                continue
            if s.startswith(".text"):
                continue  # its own code: an ARM switch's jump table (x86 puts it in .rodata)
            if s.startswith((".bss", ".data", ".sbss", ".sdata")) or ty in ("b", "d", "B", "D"):
                private.add("%s (in %s)" % (s, fn))
                continue
            outside.add(s)
    for s in sorted(outside):
        if s.startswith(("g_pres_", "pres_")) or s.endswith("_pres") or ro.match(s) or (out and out.match(s)):
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
    if os.environ.get("PRES_LINK_LIST"):
        own = sorted(x for x in outside if x.startswith(("g_pres_", "pres_")) or x.endswith("_pres"))
        rest = sorted(x for x in outside if x not in own)
        print("reached (the presenter's copies): " + " ".join(sorted(reached)))
        print("its own: " + " ".join(own))
        print("read only: " + " ".join(x for x in rest if ro.match(x)))
        print("output: " + " ".join(x for x in rest if out and out.match(x) and not ro.match(x)))
        print("shared: " + " ".join(x for x in rest if not ro.match(x) and not (out and out.match(x))))
    print("pres_link: %d functions reached from GamestateDraw_pres; %d outside names, each the "
          "presenter's own, read only, its output or shared" % (len(reached), len(outside)))


if __name__ == "__main__":
    main()

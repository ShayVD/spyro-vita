#!/usr/bin/env python3
"""How native the port is, by category (docs/native-plan.md, decisions D76).

Three static measures, each split into the categories of docs/native-plan.md:
  1. the original game's MIPS instructions (SCUS_942.28 and every overlay, from
     tools/layout/psx.map and the decomp's asm/), by what runs them on the Vita
     today;
  2. the port's source lines (code lines: comments and blank lines left out);
  3. the Vita ELF's code bytes, from a link map (tools/native_audit_map.sh).

  tools/native_audit.py [--build build/vita-audit] [--json FILE] [--detail]
  tools/native_audit.py --prof GPROF_FLAT BUILD      a host profile, by layer

--prof classifies a gprof flat profile (gprof -b -p) of BUILD's binary by the
object each function comes from (BUILD/spyro.map, tools/native_audit_map.sh
BUILD linux32). The host's software rasterizer is shown apart: the Vita
doesn't run it at gpu=1.

--build is a Vita build directory: its gen/mips2c (the generated C) and, if
present, spyro_vita.map. Without it, tables 2 and 3 leave out what needs it.
Nothing here builds or runs the game.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
S1 = os.path.join(ROOT, "third_party/spyro-1")
PSYCROSS = os.path.join(ROOT, "third_party/PsyCross")

# Categories (docs/native-plan.md "Categories"). Order is the table order.
CATS = [
    ("A", "decomp C, compiled natively (matched source)"),
    ("B1", "hand-written C, readable (no machine model)"),
    ("B2", "hand-written C over the PS1 machine model (GTE regs, scratchpad, KUSEG)"),
    ("C", "machine-translated C (mips2c: MIPS registers and memory)"),
    ("Cf", "machine-translated C linked only as a fallback (fast_render=0, A/B)"),
    ("D", "PsyQ libraries emulated by PsyCross"),
    ("E", "PS1 hardware and libraries emulated in our own code"),
    ("F", "native Vita platform code"),
    ("G", "port features over the game (platform-neutral)"),
    ("X", "third-party native libraries (vitaGL, SDL2, newlib, libstdc++, SCE stubs)"),
    ("T", "host and test only (not in the Vita ELF)"),
]
CAT_NAMES = dict(CATS)

INSN = re.compile(r"^\s*/\* [0-9A-F]+ [0-9A-F]{8} [0-9A-F]{8} \*/\s+([a-z][\w.]*)")
GLABEL = re.compile(r"^glabel\s+(\S+)")


# ---------------------------------------------------------------- the asm

def asm_functions(path):
    """[(function, insns)] in file order. A glabel that isn't D_... starts a
    function; D_... glabels are labels inside it entered by computed jumps."""
    out = []
    for line in open(path, errors="replace"):
        m = GLABEL.match(line)
        if m:
            name = m.group(1)
            if not name.startswith("D_") or not out:
                out.append([name, 0])
            continue
        if INSN.match(line) and out:
            out[-1][1] += 1
    return [tuple(x) for x in out]


def asm_insns(path):
    return sum(n for _, n in asm_functions(path))


def cmake_list(text, name):
    m = re.search(r"set\(%s\b(.*?)\)" % re.escape(name), text, re.S)
    if not m:
        sys.exit(f"native_audit: no set({name} ...) in cmake/mips2c.cmake")
    return [w for w in re.sub(r"#.*", "", m.group(1)).split() if not w.startswith("$")]


def build_lists():
    text = open(os.path.join(ROOT, "cmake/mips2c.cmake")).read()
    units = cmake_list(text, "M2C_UNITS")
    # M2C_HAND_MULTI: units whose entries are all hand C (collision.s, D83;
    # math.s and 42CC4.s, D87)
    hand = cmake_list(text, "M2C_HAND") + cmake_list(text, "M2C_HAND_MULTI")
    render = [e.split(":") for e in cmake_list(text, "M2C_RENDER_HAND")]
    nonmatch = sorted(os.path.relpath(p, S1) for p in
                      glob.glob(os.path.join(S1, "asm/nonmatchings/**/*.s"), recursive=True))
    return units, hand, render, nonmatch


# Hand-written asm turned into readable C by hand (cmake/mips2c.cmake's
# "Hand-written in src/game instead").
HAND_ASM_UNITS = ["asm/memory.s", "asm/order_table.s", "asm/system_time.s"]

# psyq.s: each instruction belongs to the library of the named entry point
# before it (internal func_/D_ labels follow their library's entry points).
PSYQ_LIBS = [
    (r"^Spu", "libspu"), (r"^Cd", "libcd"), (r"^MemCard", "libmcrd"), (r"^Pad", "libpad"),
    (r"^(VSync|VSyncCallback|StopCallback|ResetCallback)$", "libetc"),
    (r"^(LoadExec|_96_init|_96_remove)$", "libapi"),
    (r"^(InitGeom|MulMatrix0?|SetTransMatrix|SetGeomOffset|SetGeomScreen)$", "libgte"),
    (r"^(rand|srand)$", "libc rand"),
    (r"^(memcpy|strlen|printf|sprintf|exit)$", "libc"),
    (r"^__main$", "crt"),
    (r".", "libgpu"),  # SetDefDrawEnv ... DrawOTag, PutDrawEnv, SetDrawMode
]
# Who runs each library on the Vita: our own libcd/libspu/libmcrd
# (src/platform/common), our rand_psyq.c, PsyCross for the rest.
PSYQ_CAT = {"libspu": "E", "libcd": "E", "libmcrd": "E", "libc rand": "B1"}


def psyq_split(path):
    lib = "crt"
    out = collections.Counter()
    for line in open(path, errors="replace"):
        m = GLABEL.match(line)
        if m and not re.match(r"^(func_|D_)", m.group(1)):
            lib = next(l for pat, l in PSYQ_LIBS if re.search(pat, m.group(1)))
        elif INSN.match(line):
            out[lib] += 1
    return out


def map_text(path):
    """[(output section, object, bytes)] for every .text input section."""
    out, sect = [], None
    for line in open(path, errors="replace"):
        m = re.match(r"^\.(\S+)\s", line)
        if m:
            sect = m.group(1)
        m = re.match(r"^ \.text\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)", line)
        if m:
            out.append((sect, m.group(2), int(m.group(1), 16)))
    return out


def original_code():
    """Rows (where, category, insns, functions) for the original game."""
    units, hand, render, nonmatch = build_lists()
    render_funcs = {f for _, f in render}
    rows = []
    nm_by_obj = collections.defaultdict(list)
    for p in nonmatch:
        # asm/nonmatchings/pete/func_X.s -> build/src/pete.o;
        # .../gamestates/update/func_X.s -> build/src/gamestates/update.o;
        # .../overlays/credits/func_X.s -> build/src/overlays/credits/credits.o
        d = os.path.dirname(p)[len("asm/nonmatchings/"):]
        obj = f"build/src/{d}/credits.o" if d == "overlays/credits" else f"build/src/{d}.o"
        nm_by_obj[obj].append(p)
    for sect, obj, size in map_text(os.path.join(ROOT, "tools/layout/psx.map")):
        insns = size // 4
        where = "exe" if sect in ("main",) else "overlays"
        if obj.startswith("build/src/"):
            rest = insns
            for p in nm_by_obj.get(obj, []):
                n = asm_insns(os.path.join(S1, p))
                cat = "B1" if p in hand else "C"
                rows.append((where, cat, n, 1, p))
                rest -= n
            rows.append((where, "A", rest, None, obj))
        elif obj == "build/asm/psyq.o":
            for lib, n in psyq_split(os.path.join(S1, "asm/psyq.s")).items():
                rows.append((where, PSYQ_CAT.get(lib, "D"), n, None, "psyq " + lib))
        elif obj.startswith("build/asm/"):
            src = "asm/" + obj[len("build/asm/"):-2] + ".s"
            for f, n in asm_functions(os.path.join(S1, src)):
                if src in HAND_ASM_UNITS or src in hand:
                    cat = "B1"
                elif f in render_funcs:
                    cat = "B2"
                else:
                    cat = "C"
                rows.append((where, cat, n, 1, f"{src}:{f}"))
        else:
            rows.append((where, "?", insns, None, obj))
    return rows


def c_function_count():
    """Functions the decomp has as C, from psx.map's symbols in build/src
    objects (globals only: the map lists no static functions)."""
    n_exe = n_ovl = 0
    sect = obj = None
    for line in open(os.path.join(ROOT, "tools/layout/psx.map"), errors="replace"):
        m = re.match(r"^\.(\S+)\s", line)
        if m:
            sect = m.group(1)
        m = re.match(r"^ (\.\S+)\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+(\S+)", line)
        if m:
            obj = m.group(2) if m.group(1) == ".text" else None
            continue
        m = re.match(r"^\s+0x[0-9a-f]+\s+(\w+)$", line)
        if m and obj and obj.startswith("build/src/") and not m.group(1).startswith(("Terminator_", "jtbl_")):
            if sect == "main":
                n_exe += 1
            else:
                n_ovl += 1
    return n_exe, n_ovl


# ---------------------------------------------------------------- our sources

def code_lines(path):
    text = open(path, errors="replace").read()
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return sum(1 for l in text.split("\n") if l.strip())


# src/game: the rest is G (port features).
GAME_FILES = {
    "B1": ["camera_hand.c", "collision_hand.c", "math_hand.c", "42CC4_hand.c", "gamestates_hand.c",
           "gte_int.h", "math_hand.h", "pete_hand.c", "pete_dispatch_hand.c", "pete_state_hand.c",
           "pete_transition_hand.c", "memory.c", "order_table.c", "rand_psyq.c",
           "system_time.c", "hand_structs.h"],
    "B2": ["env_hand.c", "moby_hand.c", "pete_render_hand.c", "sky_hand.c", "render_hand.h"],
    "C": ["mips2c_rt.c", "mips2c_rt.h"],
    "E": ["gte_inline.h", "gte_ops.c"],
    "T": ["oracle_replay.c", "trace.c", "hand_index.c"],
}
# src/platform/common and src/platform/vita: the rest is F (native).
PLATFORM_E = {
    "common": ["arena.c", "psx_arena.h", "cdimage.c", "psx_cdimage.h", "libcd.c", "psx_cd.h",
               "libmcrd.c", "libspu.c", "psx_spu.h", "xa.c", "psx_xa.h", "vcache.c",
               "psx_vcache.h", "walk.c", "psx_walk.h", "psx_hooks.h", "psx_state.h"],
    # gpu_gl.c: the PS1 GPU (primitives, VRAM, texture pages, CLUTs, blend
    # modes) drawn with vitaGL; gpu.c/video.c: the GR_* switch and gpu=0's
    # software picture; vblank.c: the PS1's vblank clock over sceDisplay;
    # xa_ahead.c: the CD model's XA stream read ahead.
    "vita": ["gpu_gl.c", "gpu.c", "video.c", "vblank.c", "xa_ahead.c", "gpu_rt.h",
             "gpu_soft_names.h", "linux32_shared.c"],
}


def source_rows(build):
    rows = []

    def add(cat, path, what=None):
        rows.append((cat, code_lines(path), what or os.path.relpath(path, ROOT)))

    for p in sorted(glob.glob(os.path.join(S1, "src/*.c")) + glob.glob(os.path.join(S1, "src/gamestates/*.c"))
                    + glob.glob(os.path.join(S1, "src/overlays/*.c"))):
        add("A", p)
    for p in sorted(glob.glob(os.path.join(ROOT, "src/game/*.[ch]"))):
        b = os.path.basename(p)
        cat = next((c for c, fs in GAME_FILES.items() if b in fs), "G")
        add(cat, p)
    for sub, e in PLATFORM_E.items():
        for p in sorted(glob.glob(os.path.join(ROOT, f"src/platform/{sub}/*.[ch]"))):
            add("E" if os.path.basename(p) in e else "F", p)
    # linux32: gpu_soft.c and png.c are linked into the Vita too (gpu=0)
    for p in sorted(glob.glob(os.path.join(ROOT, "src/platform/linux32/*.[ch]"))):
        b = os.path.basename(p)
        add("E" if b == "gpu_soft.c" else ("F" if b == "png.c" else "T"), p)
    for p in sorted(glob.glob(os.path.join(ROOT, "src/compat/*.[ch]"))):
        add("A", p, os.path.relpath(p, ROOT) + " (decomp build shims)")
    ps = sorted(glob.glob(os.path.join(PSYCROSS, "src/psx/*.C")) + glob.glob(os.path.join(PSYCROSS, "src/gte/*.cpp"))
                + glob.glob(os.path.join(PSYCROSS, "src/gpu/*.cpp")))
    for p in ps:
        if not re.search(r"/(LIBCD|LIBSPU|LIBMCRD)\.C$", p):
            add("D", p)
    if build:
        gen = os.path.join(build, "gen/mips2c")
        for p in sorted(glob.glob(os.path.join(gen, "m2c_*.c"))):
            add("C", p, "gen/mips2c/" + os.path.basename(p) + " (generated)")
    return rows


# ---------------------------------------------------------------- the ELF

def map_sections(path):
    """[(start, size, file)] for every .text* input section of a GNU ld map."""
    lines = open(path, errors="replace").read().split("\n")
    i = next(k for k, l in enumerate(lines) if re.match(r"^\.text\s", l))
    out = []
    i += 1
    while i < len(lines) and not re.match(r"^\.\S", lines[i]):
        m = re.match(r"^ \.text\S*(\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*))?$", lines[i])
        if m:
            if m.group(1):
                out.append((int(m.group(2), 16), int(m.group(3), 16), m.group(4)))
            else:
                m2 = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", lines[i + 1])
                if m2:
                    out.append((int(m2.group(1), 16), int(m2.group(2), 16), m2.group(3)))
                    i += 1
        i += 1
    return out


GAME = {os.path.basename(p): c for c, fs in GAME_FILES.items() for p in fs}
PLAT = {b: "E" for fs in PLATFORM_E.values() for b in fs}
DECOMP = {os.path.basename(p) for p in glob.glob(os.path.join(S1, "src/**/*.c"), recursive=True)}


def obj_category(f):
    """Category of a linked object, from the map's file column."""
    m = re.search(r"\(([^)]+)\)$", f)
    member = m.group(1) if m else os.path.basename(f)
    base = re.sub(r"\.(obj|o)$", "", member)
    if "libpsycross.a" in f:
        return "D", member
    if "libspyro_game.a" in f:
        if base.startswith("m2c_"):
            return "C", member
        if base in GAME:
            return GAME[base], member
        if base in DECOMP or base == "ovl_table.c" or base.startswith("s1_data"):
            return "A", member
        return "G", member
    if "libspyro_platform_common.a" in f or "CMakeFiles/spyro_vita.dir" in f:
        b = os.path.basename(base)
        return PLAT.get(b, "E" if b == "gpu_soft.c" else "F"), member
    if "linux32" in f:  # the host's platform layer: not in the Vita ELF
        return "T", member
    return "X", os.path.basename(f.split("(")[0])


def render_unit_split(orig):
    """A render unit's object holds both the live code and the fallback-only
    hand-written functions (mips2c emits one C function per unit, so symbol
    sizes don't separate them): its share of each, by original instructions."""
    split = collections.defaultdict(collections.Counter)
    for where, cat, n, _, what in orig:
        if what.startswith("asm/renderers/"):
            unit = "m2c_" + re.sub(r"[^A-Za-z0-9_]", "_", what.split(":")[0][4:-2]) + ".c"
            split[unit]["Cf" if cat == "B2" else "C"] += n
    return split


def elf_rows(build, orig):
    path = os.path.join(build, "spyro_vita.map")
    if not os.path.exists(path):
        return None
    split = render_unit_split(orig)
    rows = []
    for _, size, f in map_sections(path):
        cat, member = obj_category(f)
        sp = split.get(re.sub(r"\.(obj|o)$", "", member))
        if cat == "C" and sp:
            tot = sum(sp.values())
            rows.extend((c, size * n // tot, member) for c, n in sp.items())
        else:
            rows.append((cat, size, member))
    return rows


# ---------------------------------------------------------------- a profile

# Groups for run time, by what the time is spent on. First match wins:
# (category, object or function pattern, group).
PROF_GROUPS = [
    (None, r"^m2c_gte_|^gte_|GTE_operator|^(MTC2|MFC2|CTC2|CFC2|LWC2|SWC2)|PsyX_GTE|LIBGTE|INLINE_C", "GTE in software"),
    ("T", r"gpu_soft|^GR_DrawTriangles$|^gradients$", "host picture (software rasterizer; not on the Vita at gpu=1)"),
    ("D", r"PsyX_GPU|LIBGPU", "PS1 primitives: PsyCross OT walk and translation"),
    ("E", r"walk\.c|vcache\.c", "PS1 primitives: PsyCross OT walk and translation"),
    ("E", r"libspu|xa\.c|psx_spu|psx_xa", "SPU and XA in software"),
    ("E", r"libcd|cdimage", "CD in software"),
    ("T", r"audio\.c", "SPU and XA in software"),
    ("C", r".", "mips2c (register-model C)"),
    ("B2", r".", "hand C over the machine model (renderers)"),
    ("B1", r".", "hand C, readable"),
    ("A", r".", "decomp C"),
    ("G", r".", "port features"),
    ("D", r".", "PsyCross, other"),
    ("E", r".", "our emulation, other"),
]


def profile(gprof_flat, build):
    """Self time of a gprof flat profile, by category and group."""
    mp = next((os.path.join(build, n) for n in ("spyro.map", "spyro_vita.map")
               if os.path.exists(os.path.join(build, n))), None)
    if not mp:
        sys.exit("native_audit: no spyro.map in " + build + " (tools/native_audit_map.sh)")
    secs = sorted(map_sections(mp))
    addr = {}
    for line in open(mp[:-4] + ".syms", errors="replace"):
        m = re.match(r"^([0-9a-f]+) [0-9a-f]+ [tTwW] (.+)$", line.rstrip("\n"))
        if m:
            addr.setdefault(m.group(2), int(m.group(1), 16))
            addr.setdefault(re.sub(r"\(.*$", "", m.group(2)), int(m.group(1), 16))
    starts = [s for s, _, _ in secs]
    import bisect
    rows = []
    for line in open(gprof_flat, errors="replace"):
        m = re.match(r"^\s*[\d.]+\s+[\d.]+\s+([\d.]+)\s+(?:\d+\s+[\d.]+\s+[\d.]+\s+)?(\S.*)$", line)
        if not m:
            continue
        t, name = float(m.group(1)), m.group(2).strip()
        a = addr.get(name, addr.get(re.sub(r"\(.*$", "", name)))
        cat, obj = "?", "?"
        if a is not None:
            k = bisect.bisect_right(starts, a) - 1
            if k >= 0 and a < secs[k][0] + secs[k][1]:
                cat, obj = obj_category(secs[k][2])
        group = next((g for c, pat, g in PROF_GROUPS
                      if (c is None or c == cat) and (re.search(pat, name) or re.search(pat, obj))),
                     "other (libc, startup, unknown)")
        rows.append((group, cat, t, name, obj))
    return rows


# ---------------------------------------------------------------- output

def table(title, unit, rows, total_label="total"):
    tot = sum(v for _, v in rows) or 1
    print(f"\n{title}")
    print(f"| Category | {unit} | % |")
    print("|---|---:|---:|")
    for c, v in rows:
        print(f"| {c} {CAT_NAMES.get(c, '?')} | {v:,} | {100.0 * v / tot:.1f}% |")
    print(f"| {total_label} | {tot:,} | 100% |")


def by_cat(rows, idx_cat=0, idx_val=1):
    acc = collections.Counter()
    for r in rows:
        acc[r[idx_cat]] += r[idx_val]
    return [(c, acc[c]) for c, _ in CATS if acc[c]] + [(c, v) for c, v in acc.items() if c not in CAT_NAMES]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", help="Vita build directory (gen/mips2c, spyro_vita.map)")
    ap.add_argument("--json", help="also write every row as JSON")
    ap.add_argument("--detail", action="store_true", help="also list the rows of each table")
    ap.add_argument("--prof", nargs=2, metavar=("GPROF_FLAT", "BUILD"),
                    help="only classify a gprof flat profile (gprof -b -p) of BUILD's binary")
    a = ap.parse_args()
    build = os.path.abspath(a.build) if a.build else None
    if a.prof:
        rows = profile(a.prof[0], a.prof[1])
        tot = sum(r[2] for r in rows) or 1
        host = sum(r[2] for r in rows if r[0].startswith("host picture"))
        acc = collections.Counter()
        for g, _, t, _, _ in rows:
            acc[g] += t
        print(f"# Profile {a.prof[0]}: self time by group ({tot:.2f} s sampled)")
        print("| Group | s | % of all | % without the host picture |")
        print("|---|---:|---:|---:|")
        for g, t in acc.most_common():
            rest = "" if g.startswith("host picture") else f"{100 * t / (tot - host):.1f}%"
            print(f"| {g} | {t:.2f} | {100 * t / tot:.1f}% | {rest} |")
        cat = collections.Counter()
        for g, c, t, _, _ in rows:
            if not g.startswith("host picture"):
                cat[c] += t
        print("\nBy category, without the host picture:")
        for c, t in cat.most_common():
            print(f"- {c} {CAT_NAMES.get(c, 'unresolved')}: {100 * t / (tot - host):.1f}%")
        if a.detail:
            for r in sorted(rows, key=lambda r: -r[2]):
                print("prof", *r, sep="\t")
        return

    orig = original_code()
    exe = [(c, n) for w, c, n, _, _ in orig if w == "exe"]
    allg = [(c, n) for _, c, n, _, _ in orig]
    print("# Native audit (tools/native_audit.py, decisions D76)")
    table("## 1a. The original executable (SCUS_942.28 .text), MIPS instructions, by what runs them on the Vita",
          "instructions", by_cat(exe))
    table("## 1b. The whole original game (executable + all 37 overlays as on the disc), MIPS instructions",
          "instructions", by_cat(allg))
    n_exe, n_ovl = c_function_count()
    fn = collections.Counter(c for _, c, _, k, _ in orig if k == 1)
    print(f"\nFunctions: {n_exe} decomp C functions in the executable and {n_ovl} in the overlays (global symbols "
          f"in psx.map); asm functions by category: " + ", ".join(f"{c} {fn[c]}" for c, _ in CATS if fn[c]))

    src = source_rows(build)
    table("## 2. Source code lines (no comments or blank lines) that build the Vita game, by category",
          "code lines", by_cat([r for r in src if r[0] != "T"]), "total (Vita)")
    t = sum(n for c, n, _ in src if c == "T")
    print(f"\nHost and test only (category T, not in the Vita ELF): {t:,} code lines.")
    if not build:
        print("(No --build: the generated mips2c C isn't counted.)")

    elf = elf_rows(build, orig) if build else None
    if elf is None:
        print("\n## 3. Vita ELF code bytes: no link map (tools/native_audit_map.sh BUILD_DIR, then --build)")
    else:
        table("## 3. Vita ELF .text bytes (Thumb-2), by category", "bytes", by_cat(elf), "total .text")
        own = [(c, v) for c, v in by_cat(elf) if c != "X"]
        table("## 3b. The same without the third-party libraries (X)", "bytes", own, "total, ours + decomp + PsyCross")

    if a.detail:
        print("\n## Rows")
        for r in orig:
            print("orig", *r, sep="\t")
        for r in src:
            print("src", *r, sep="\t")
        for r in elf or []:
            print("elf", *r, sep="\t")
    if a.json:
        json.dump({"original": orig, "source": src, "elf": elf}, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()

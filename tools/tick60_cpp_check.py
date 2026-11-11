#!/usr/bin/env python3
"""60 Hz logic's proof that the 30 Hz code is unchanged (decisions D130).

  tools/tick60_cpp_check.py [--base REF] [--build build/linux32]

Every 30 Hz file the 60 Hz pilot touches (the decomp's, through
patches/spyro-1-port, patches/spyro-1-region and patches/spyro-1-region-port, and our hand C) is preprocessed twice with the flags
the build compiles it with (build.ninja: its 30 Hz target's DEFINES, FLAGS
and INCLUDES; the overlays' preprocessor flags for moby_update.c): as this
tree has it, and as REF (default origin/main) has it. The two token streams
must be the same, file for file. The dispatch differs by design and is
reported, not compared: main.c (0012), present.c (no in-between frame after
a 60 Hz update), savestate.c (the T60 chunk), tick60.c (new). Run from the Mac: it needs git, and
runs the preprocessor in the spyro-host container (tools/tc).
"""
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP = ["src/gamestates/update.c", "src/pete.c", "src/camera.c", "src/moby_helpers.c",
          "src/overlays/moby_update.c"]
HAND = ["src/game/pete_hand.c", "src/game/pete_dispatch_hand.c", "src/game/pete_transition_hand.c",
        "src/game/pete_state_hand.c", "src/game/camera_hand.c", "src/game/moby_lists_hand.c"]
TOK = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|0[xX][0-9a-fA-F]+[uUlL]*|'
                 r'\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[uUlLfF]*|->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||'
                 r'[-+*/%&|^]=|\.\.\.|##|\S')


def git(*a):
    return subprocess.run(["git", "-C", ROOT] + list(a), check=True, stdout=subprocess.PIPE).stdout


def ninja_flags(build, obj_re):
    """DEFINES FLAGS INCLUDES of the first build.ninja object matching obj_re."""
    txt = open(os.path.join(build, "build.ninja")).read()
    m = re.search(r"^build (" + obj_re + r"): .*\n((?:  .*\n)+)", txt, re.M)
    if not m:
        sys.exit(f"tick60_cpp_check: no object {obj_re} in build.ninja")
    vals = dict(re.findall(r"^  (\w+) = (.*)$", m.group(2), re.M))
    return " ".join(vals.get(k, "") for k in ("DEFINES", "FLAGS", "INCLUDES"))


def ovl_flags(build):
    txt = open(os.path.join(build, "build.ninja")).read()
    m = re.search(r"port_overlays\.py preprocess --classes \S+ --out \S+/ovl/LEVEL_10/moby_update\.c \S+ -- (.*?) "
                  r"\S+/src/overlays/moby_update\.c", txt)
    if not m:
        sys.exit("tick60_cpp_check: no LEVEL_10 moby_update.c preprocess command in build.ninja")
    flags = m.group(1).split()
    if flags and flags[0].endswith("gcc"):  # the compiler itself
        flags = flags[1:]
    return " ".join(f for f in flags if f != "-E")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--build", default="build/linux32")
    a = ap.parse_args()
    build = os.path.join(ROOT, a.build)
    work = os.path.join(build, "t60cpp")
    shutil.rmtree(work, ignore_errors=True)
    base_s1 = os.path.join(work, "base/spyro-1")
    for d in ("src", "include"):
        shutil.copytree(os.path.join(ROOT, "third_party/spyro-1", d), os.path.join(base_s1, d))
    # the port's patches, then the region switches (D133) and the port's region edits (D135),
    # as CMakeLists.txt's spyro_prepare applies them
    patches = []
    for d in ("patches/spyro-1-port/", "patches/spyro-1-region/", "patches/spyro-1-region-port/"):
        patches += sorted(p for p in git("ls-tree", "--name-only", a.base, d).decode().split()
                          if p.endswith(".patch"))
    for p in patches:
        subprocess.run(["patch", "-p1", "--forward", "--quiet"], input=git("show", f"{a.base}:{p}"),
                       cwd=base_s1, check=True)
    # our files as REF has them; the compat headers too (port_prefix.h)
    for d in ("src/game", "src/compat"):
        for f in git("ls-tree", "--name-only", a.base, d + "/").decode().split():
            if f.endswith((".c", ".h")):
                dst = os.path.join(work, "base", f)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, "wb").write(git("show", f"{a.base}:{f}"))
    main_flags = ninja_flags(build, r"CMakeFiles/spyro_game_main\.dir/patched/spyro-1/src/pete\.c\.o")
    port_flags = ninja_flags(build, r"CMakeFiles/spyro_game_port\.dir/src/game/pete_hand\.c\.o")
    oflags = ovl_flags(build)
    cc = "i686-linux-gnu-gcc"
    jobs = []  # (name, head cmd, base cmd)
    w = "/work/" + os.path.relpath(work, ROOT)
    pb = f"/work/{a.build}/patched/spyro-1"

    def rebase(flags, s1_dir, compat_dir, game_dir):
        f = flags.replace(f"{pb}/include", f"{s1_dir}/include")
        f = f.replace("/work/src/compat", compat_dir).replace("/work/src/game", game_dir)
        return f

    for f in DECOMP:
        fl = oflags + " -DOVERLAY_NAME=LEVEL_10 -DIS_LEVEL_10" if "overlays" in f else main_flags
        jobs.append((f, f"{cc} -E -P {fl} {pb}/{f}",
                     f"{cc} -E -P {rebase(fl, w + '/base/spyro-1', w + '/base/src/compat', w + '/base/src/game')} "
                     f"{w}/base/spyro-1/{f}"))
    for f in HAND:
        jobs.append((f, f"{cc} -E -P {port_flags} /work/{f}",
                     f"{cc} -E -P {rebase(port_flags, pb, w + '/base/src/compat', w + '/base/src/game')} "
                     f"-I{w}/base/src/game {w}/base/{f}"))
    script = ["set -e"]
    for k, (n, h, b) in enumerate(jobs):
        script.append(f"{h} > {w}/head_{k}.i")
        script.append(f"{b} > {w}/base_{k}.i")
    open(os.path.join(work, "run.sh"), "w").write("\n".join(script) + "\n")
    r = subprocess.run([os.path.join(ROOT, "tools/tc"), "sh", f"{w}/run.sh"], cwd=ROOT)
    if r.returncode:
        sys.exit("tick60_cpp_check: the preprocessor failed")
    bad = 0
    for k, (n, _, _) in enumerate(jobs):
        th = TOK.findall(open(os.path.join(work, f"head_{k}.i")).read())
        tb = TOK.findall(open(os.path.join(work, f"base_{k}.i")).read())
        if th == tb:
            print(f"same  {n}: {len(th)} tokens")
        else:
            bad += 1
            i = next((i for i, (x, y) in enumerate(zip(th, tb)) if x != y), min(len(th), len(tb)))
            print(f"DIFF  {n}: at token {i}: this tree {' '.join(th[max(0, i - 8):i + 8])!r}\n"
                  f"      {a.base}: {' '.join(tb[max(0, i - 8):i + 8])!r}")
    print("not compared, the dispatch (by design): main.c (0012: PortGamestateUpdate, PortTickDelta), "
          "present.c (no in-between frame after a 60 Hz update), savestate.c (the T60 chunk), tick60.c (new)")
    print(f"{len(jobs) - bad} of {len(jobs)} files preprocess to {a.base}'s tokens")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()

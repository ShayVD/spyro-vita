#!/usr/bin/env python3
"""A release fpkg's shader binaries match the renderer's GLSL as built (PS4 plan P5, D246).

    tools/ps4/shader_check.py GPU_GLES_C SHADER_DIR [--cc CC]

The release fpkg ships pre-compiled shaders (D245, the owner's option C); gpu_gles.c looks each up by name, kind and
the FNV-1a of its GLSL source, and with one missing the firmware's Piglet path is off and players without the Piglet
pair get the software renderer. This takes the shader block of gpu_gles.c (from its "---- shaders" comment to
gl_check), runs it through the C preprocessor (the sources are built from macros), evaluates each k_vs_* / k_fs_*
string, hashes it as src_hash() does, reads which name each is linked under from the link() calls, and fails unless
SHADER_DIR holds NAME-vs|fs-HASH.bin for every one. Prints what it found; exit 0 only when all are there.
"""
import argparse
import os
import re
import subprocess
import sys

LINK = re.compile(r'link\(&s_prog\[\w+\],\s*(k_\w+),\s*"(\w+)",\s*(k_\w+),\s*\d+,\s*"(\w+)"\)')
ARRAY = re.compile(r"static const char (k_(?:vs|fs)_\w+)\[\]\s*=\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+);")
ESC = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'", "0": "\0", "r": "\r"}


def c_string(lits):
    out = []
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', lits):
        i = 0
        while i < len(lit):
            ch = lit[i]
            if ch == "\\":
                nxt = lit[i + 1]
                if nxt not in ESC:
                    raise SystemExit(f"shader_check: escape \\{nxt} not handled")
                out.append(ESC[nxt])
                i += 2
            else:
                out.append(ch)
                i += 1
    return "".join(out)


def fnv1a(s):
    h = 2166136261
    for b in s.encode("latin-1"):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gpu_gles_c")
    ap.add_argument("shader_dir")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    a = ap.parse_args()
    text = open(a.gpu_gles_c).read()
    start = text.find("/* ---- shaders")
    end = text.find("static void gl_check")
    if start < 0 or end < start:
        raise SystemExit("shader_check: no shader block in " + a.gpu_gles_c)
    pre = subprocess.run([a.cc, "-E", "-P", "-x", "c", "-"], input=text[start:end], text=True,
                         capture_output=True, check=True).stdout
    sources = {m.group(1): c_string(m.group(2)) for m in ARRAY.finditer(pre)}
    want = set()
    for vs, vname, fs, fname in LINK.findall(text):
        for arr, name, kind in ((vs, vname, "vs"), (fs, fname, "fs")):
            if arr not in sources:
                raise SystemExit(f"shader_check: {arr} not found in the shader block")
            want.add((name, kind, "%08x" % fnv1a(sources[arr])))
    if not want:
        raise SystemExit("shader_check: no link() calls found")
    have = set(os.listdir(a.shader_dir)) if os.path.isdir(a.shader_dir) else set()
    missing = 0
    for name, kind, h in sorted(want):
        f = f"{name}-{kind}-{h}.bin"
        ok = f in have
        missing += not ok
        print(f"shader_check: {f} {'ok' if ok else 'MISSING'}")
    if missing:
        print(f"shader_check: {missing} of {len(want)} shaders have no binary for this source in {a.shader_dir}: "
              "capture them on a PS4 with shader_compile=1 (docs/ps4-build.md)", file=sys.stderr)
        return 1
    print(f"shader_check: all {len(want)} shaders have a binary")
    return 0


if __name__ == "__main__":
    sys.exit(main())

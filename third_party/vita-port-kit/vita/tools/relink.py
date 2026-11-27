#!/usr/bin/env python3
"""A worker's change on the Vita without a full build: the last finished build's objects, the worker's files compiled
again, then the build's own map / link / package steps. From kh1-vita worker #342, which relinked its change against
batch 73's objects in minutes, against about 2 h for `make vpk` after a header change (ShayVD/encore#90).

  relink.py run --from BUILD_DIR [--repo ROOT] [--game FILE] [--base REV] [--mode clone|copy|hardlink]
                [--files PATH ...] [--eboot-only] [--replace] [--force] [--dry-run]
  relink.py diff OLD.elf NEW.elf [--expect NAME ...]
  relink.py vpk OLD.vpk NEW.vpk

`run`, in the worker's checkout (ROOT, default the git top level of the current directory):
 1. Checks that BUILD_DIR (the batch's build output: kh1-vita's ../kh1-vita-batch/build/work) is a finished build:
    its ELF and VPK exist, and nothing beside the ELF is newer than the VPK (a build still running, or one that failed
    after compiling, is refused).
 2. Lists what the worker touched: `git diff --name-only BASE` (the working tree, so uncommitted edits count),
    untracked files, and inside each submodule whose pointer moved. BASE defaults to the merge base of HEAD and the
    commit BUILD_DIR was built from (the game's `relink_built_rev` hook, else the HEAD of BUILD_DIR's git checkout).
    Each path is a source (recompiled), a header (every source that includes it, directly or through other headers,
    is recompiled: a textual #include scan of the tracked files, so it errs on the side of more), ignored
    (RELINK_IGNORE), or one a relink cannot cover (RELINK_FULL, or anything else: refused, exit 3, unless --force).
 3. Copies BUILD_DIR to ROOT/RELINK_OUT: `clone` (default) is a copy-on-write clone (APFS `cp -c`, Linux
    `cp --reflink=always`): seconds for gigabytes, and nothing written later reaches the batch's files. `copy` is a
    plain copy. `hardlink` hard-links the files matching RELINK_SHARED (inputs the build only reads: a disc image, an
    SDK copy) and copies the rest, so a compiler writing into an object never writes through to the batch's.
 4. Builds: with RELINK_COMPILE_DB (a compile_commands.json the build left in BUILD_DIR) the kit recompiles each
    listed file with the build's own command (the batch's paths rewritten to ROOT's) and then calls the game's
    `relink_link OUT`; without one it calls the game's `relink_build OUT LIST` (LIST: a file of the paths to compile,
    one per line, relative to ROOT; the hook reuses every other object and compiles any object that is missing).
 5. Reports: the new ELF, eboot.bin and VPK with their SHA-1s against BUILD_DIR's, whether the loaded segments are
    identical, which symbols' bytes differ and how many loaded bytes differ outside them (`diff`), and which VPK
    entries differ (`vpk`); the same text goes to OUT/relink.txt.

The game's settings: tools/vita/relink.game in ROOT (--game or RELINK_GAME names another), KEY=VALUE lines plus bash
functions for the hooks, as hw.game (relink.game.example has every key). Exit 0 built; 1 the build or a check
failed; 2 usage or setup; 3 the change needs the full build.

`diff` reads two ELF files (32- or 64-bit, little-endian; ARM's Thumb bit handled) and names the symbols whose bytes
differ, those resized, added or removed, the sections that moved, and the sections not loaded that differ (debug
info); with the same layout, the loaded bytes that differ outside every named symbol. With --expect it exits 1 when
anything but the names given changed, or a loaded byte changed outside them (a relink of a one-function change:
`--expect that_function`). Standard library only.
"""
import argparse
import concurrent.futures
import fnmatch
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import zipfile

DEFAULTS = {
    "RELINK_SOURCES": r"\.(c|cc|cpp|cxx|m|s|S)$",
    "RELINK_HEADERS": r"\.(h|hh|hpp|hxx|inc|inl)$",
    "RELINK_IGNORE": r"\.md$",
    "RELINK_FULL": "",
    "RELINK_SHARED": "",
}
ARG_PATH = re.compile(r"^(-I|-L|-F|-o|-MF|-MT|-MQ|-isystem|-iquote|-idirafter|-include|--sysroot=)(/.*)$")
INCLUDE_RE = re.compile(r'^\s*#\s*(?:include|import)\s*[<"]([^>"]+)[>"]', re.M)


class Refuse(Exception):
    """A check failed: message, exit code."""

    def __init__(self, msg, code=2):
        super().__init__(msg)
        self.code = code


def say(msg, out=None):
    print("relink: " + msg, flush=True)
    if out is not None:
        out.append(msg)


# ---------------------------------------------------------------- the game's settings


def read_game(path):
    """KEY=VALUE lines (quotes stripped, no expansion) over DEFAULTS; hooks stay in the file for bash."""
    cfg = dict(DEFAULTS)
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            m = re.match(r"^([A-Z_][A-Z0-9_]*)=(.*)$", line)
            if not m:
                continue
            val = m.group(2).strip()
            if len(val) >= 2 and val[0] == val[-1] and val[0] in "'\"":
                val = val[1:-1]
            cfg[m.group(1)] = val
    for key in ("RELINK_OUT", "RELINK_ELF"):
        if not cfg.get(key):
            raise Refuse("%s: %s is not set" % (path, key))
    return cfg


def hooks(game):
    p = subprocess.run(["bash", "-c", 'source "$1" >/dev/null && declare -F | sed "s/^declare -f //"', "_", game],
                       capture_output=True, text=True)
    if p.returncode:
        raise Refuse("%s does not source in bash: %s" % (game, p.stderr.strip()))
    return set(p.stdout.split())


def call_hook(game, name, args, cwd, env):
    """Run hook NAME from the game file in bash, in cwd; its output goes straight through."""
    return subprocess.run(["bash", "-c", 'source "$1" && shift && "$@"', "_", game, name] + list(args),
                          cwd=cwd, env=env).returncode


def hook_output(game, name, args, cwd, env):
    p = subprocess.run(["bash", "-c", 'source "$1" && shift && "$@"', "_", game, name] + list(args),
                       cwd=cwd, env=env, capture_output=True, text=True)
    if p.returncode:
        raise Refuse("hook %s failed (%d): %s" % (name, p.returncode, p.stderr.strip()[-500:]))
    return p.stdout.strip()


# ---------------------------------------------------------------- git


def git(root, *args, check=True):
    p = subprocess.run(["git", "-C", root] + list(args), capture_output=True, text=True)
    if check and p.returncode:
        raise Refuse("git %s in %s: %s" % (" ".join(args), root, p.stderr.strip()))
    return p.stdout


def lines(text):
    return [l for l in text.splitlines() if l]


def submodule_paths(root):
    return [l.split("\t", 1)[1] for l in lines(git(root, "ls-files", "-s")) if l.startswith("160000 ")]


def changed_paths(root, base):
    """Paths (relative to root) the working tree changed since base, inside submodules too, plus untracked files."""
    subs = set(submodule_paths(root))
    out = set()
    for p in lines(git(root, "diff", "--name-only", "--no-renames", "--ignore-submodules=dirty", base)):
        if p not in subs:
            out.add(p)
            continue
        old = git(root, "rev-parse", "--verify", "-q", "%s:%s" % (base, p), check=False).strip()
        sub = os.path.join(root, p)
        if not old or not os.path.exists(os.path.join(sub, ".git")):
            out.add(p)  # added, removed or not checked out: the caller refuses it
            continue
        for q in lines(git(sub, "diff", "--name-only", "--no-renames", old)):
            out.add(p + "/" + q)
        for q in lines(git(sub, "ls-files", "--others", "--exclude-standard")):
            out.add(p + "/" + q)
    for p in lines(git(root, "ls-files", "--others", "--exclude-standard")):
        out.add(p)
    return sorted(out)


def tracked_files(root):
    return lines(git(root, "ls-files", "--recurse-submodules"))


# ---------------------------------------------------------------- what to compile


def norm_include(s):
    parts = [x for x in s.replace("\\", "/").split("/") if x not in ("", ".", "..")]
    return "/".join(parts)


def includes_of(root, path):
    try:
        with open(os.path.join(root, path), errors="replace") as fh:
            return [norm_include(m) for m in INCLUDE_RE.findall(fh.read())]
    except OSError:
        return []


def dependents(root, headers, files, src_re, hdr_re):
    """Sources that include any of `headers`, directly or through other headers (by path suffix: an
    `#include "platform/vu.h"` matches host/include/platform/vu.h). Over-approximates, never misses a textual one."""
    scan = [f for f in files if src_re.search(f) or hdr_re.search(f)]
    incs = {f: includes_of(root, f) for f in scan}

    def hits(f, h):
        return any(h == i or h.endswith("/" + i) for i in incs[f])

    seen, todo, srcs = set(headers), list(headers), set()
    while todo:
        h = todo.pop()
        for f in scan:
            if f in seen or not hits(f, h):
                continue
            if hdr_re.search(f):
                seen.add(f)
                todo.append(f)
            else:
                srcs.add(f)
    return srcs


def classify(cfg, paths):
    """(sources, headers, ignored, full, unknown)"""
    src_re, hdr_re = re.compile(cfg["RELINK_SOURCES"]), re.compile(cfg["RELINK_HEADERS"])
    ign = re.compile(cfg["RELINK_IGNORE"]) if cfg["RELINK_IGNORE"] else None
    full = re.compile(cfg["RELINK_FULL"]) if cfg["RELINK_FULL"] else None
    res = ([], [], [], [], [])
    for p in paths:
        if full and full.search(p):
            res[3].append(p)
        elif ign and ign.search(p):
            res[2].append(p)
        elif src_re.search(p):
            res[0].append(p)
        elif hdr_re.search(p):
            res[1].append(p)
        else:
            res[4].append(p)
    return res


# ---------------------------------------------------------------- the copy


def check_finished(src_dir, cfg, eboot_only):
    elf = os.path.join(src_dir, cfg["RELINK_ELF"])
    last = cfg.get("RELINK_EBOOT") if eboot_only and cfg.get("RELINK_EBOOT") else cfg.get("RELINK_VPK")
    if not os.path.isfile(elf):
        raise Refuse("%s has no %s: not a finished build" % (src_dir, cfg["RELINK_ELF"]))
    if not last:
        return
    last_p = os.path.join(src_dir, last)
    if not os.path.isfile(last_p):
        raise Refuse("%s has no %s: not a finished build (or it failed before packaging)" % (src_dir, last))
    t = os.path.getmtime(last_p)
    d = os.path.dirname(elf)
    for dp, _dn, fn in os.walk(d):
        for f in fn:
            q = os.path.join(dp, f)
            if not os.path.islink(q) and os.path.getmtime(q) > t + 2:
                raise Refuse("%s is newer than %s: the build in %s is running or failed; relink against a finished "
                             "one" % (os.path.relpath(q, src_dir), last, src_dir))


def copy_tree(src, dst, mode, shared_re):
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    if mode == "clone":
        flag = ["-c"] if sys.platform == "darwin" else ["--reflink=always"]
        p = subprocess.run(["cp", "-pR"] + flag + [src, dst], capture_output=True, text=True)
        if p.returncode:
            shutil.rmtree(dst, ignore_errors=True)
            raise Refuse("cp %s could not clone %s (%s): another file system? --mode copy or hardlink"
                         % (" ".join(flag), src, p.stderr.strip()))
        return
    if mode == "copy":
        shutil.copytree(src, dst, symlinks=True)
        return
    rx = re.compile(shared_re) if shared_re else None
    for dp, dn, fn in os.walk(src):
        rel = os.path.relpath(dp, src)
        out = os.path.normpath(os.path.join(dst, rel))
        os.makedirs(out, exist_ok=True)
        for n in list(dn):
            if os.path.islink(os.path.join(dp, n)):
                os.symlink(os.readlink(os.path.join(dp, n)), os.path.join(out, n))
                dn.remove(n)
        for n in fn:
            s, t = os.path.join(dp, n), os.path.join(out, n)
            r = os.path.normpath(os.path.join(rel, n))
            if os.path.islink(s):
                os.symlink(os.readlink(s), t)
            elif rx and rx.search(r):
                os.link(s, t)
            else:
                shutil.copy2(s, t)


# ---------------------------------------------------------------- compile_commands.json


def db_compile(db_path, todo, root, batch_root, src_dir, out_dir, jobs):
    """Each of todo with the build's own command: paths under the batch's root and build dir moved to ours."""
    with open(db_path) as fh:
        db = json.load(fh)
    subst = sorted({(os.path.realpath(src_dir), out_dir), (src_dir.rstrip("/"), out_dir),
                    (os.path.realpath(batch_root), root), (batch_root.rstrip("/"), root)}, key=lambda x: -len(x[0]))

    def move(s):
        for a, b in subst:
            if s == a or s.startswith(a + "/"):
                return b + s[len(a):]
        return s

    by_file = {}
    for e in db:
        d = e.get("directory", batch_root)
        f = os.path.normpath(os.path.join(d, e["file"]))
        for base in (os.path.realpath(batch_root), batch_root):
            if f.startswith(base.rstrip("/") + "/"):
                by_file[os.path.relpath(f, base)] = e
    cmds, missing = [], []
    for p in todo:
        e = by_file.get(p)
        if e is None:
            missing.append(p)
            continue
        args = e["arguments"] if "arguments" in e else shlex.split(e["command"])
        args = [move(x) if x.startswith("/") else ARG_PATH.sub(lambda m: m.group(1) + move(m.group(2)), x)
                for x in args]
        cmds.append((p, move(e.get("directory", batch_root)), args))
    if missing:
        raise Refuse("no compile command in %s for: %s (a new file needs the full build)"
                     % (os.path.basename(db_path), " ".join(missing)), 3)

    def one(c):
        p, d, args = c
        r = subprocess.run(args, cwd=d, capture_output=True, text=True)
        return p, r.returncode, (r.stdout + r.stderr).strip()

    bad = []
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for p, rc, msg in ex.map(one, cmds):
            say("compiled %s%s" % (p, "" if rc == 0 else " FAILED (%d)" % rc))
            if rc:
                bad.append(p)
                print(msg[-2000:])
    if bad:
        raise Refuse("%d file(s) do not compile: %s" % (len(bad), " ".join(bad)), 1)


# ---------------------------------------------------------------- ELF and VPK comparison


class Elf:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.data = fh.read()
        d = self.data
        if d[:4] != b"\x7fELF":
            raise Refuse("%s: not an ELF file" % path)
        self.is64 = d[4] == 2
        if d[5] != 1:
            raise Refuse("%s: big-endian ELF files are not read" % path)
        if self.is64:
            (self.machine,) = struct.unpack_from("<H", d, 18)
            shoff, = struct.unpack_from("<Q", d, 40)
            shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 58)
        else:
            (self.machine,) = struct.unpack_from("<H", d, 18)
            shoff, = struct.unpack_from("<I", d, 32)
            shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 46)
        self.sections = []
        for i in range(shnum):
            o = shoff + i * shentsize
            if self.is64:
                name, typ, flags, addr, off, size, link, info, _al, entsize = struct.unpack_from("<IIQQQQIIQQ", d, o)
            else:
                name, typ, flags, addr, off, size, link, info, _al, entsize = struct.unpack_from("<IIIIIIIIII", d, o)
            self.sections.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link,
                                      entsize=entsize))
        if shnum:
            strs = self.sections[shstrndx]
            for s in self.sections:
                s["name"] = self.cstr(strs["off"] + s["name"])
        self.symbols = self.read_symbols()
        if self.is64:
            phoff, = struct.unpack_from("<Q", d, 32)
            phentsize, phnum = struct.unpack_from("<HH", d, 54)
        else:
            phoff, = struct.unpack_from("<I", d, 28)
            phentsize, phnum = struct.unpack_from("<HH", d, 42)
        self.segments = []  # (type, vaddr, memsz, file bytes): what a loader reads
        for i in range(phnum):
            o = phoff + i * phentsize
            if self.is64:
                typ, _fl, off, vaddr, _pa, filesz, memsz, _al = struct.unpack_from("<IIQQQQQQ", d, o)
            else:
                typ, off, vaddr, _pa, filesz, memsz, _fl, _al = struct.unpack_from("<IIIIIIII", d, o)
            if filesz or memsz:
                self.segments.append((typ, vaddr, memsz, d[off:off + filesz]))

    def cstr(self, o):
        e = self.data.index(b"\0", o)
        return self.data[o:e].decode("latin-1")

    def read_symbols(self):
        """{(name, n): (section name, addr, size, type)}: functions and objects with a size, in allocated sections;
        n counts earlier symbols of the same name (static functions of the same name in two files)."""
        out, seen = {}, {}
        for s in self.sections:
            if s["type"] != 2:  # SHT_SYMTAB
                continue
            strtab = self.sections[s["link"]]
            ent = s["entsize"] or (24 if self.is64 else 16)
            for i in range(1, s["size"] // ent):
                o = s["off"] + i * ent
                if self.is64:
                    name, info, _oth, shndx, value, size = struct.unpack_from("<IBBHQQ", self.data, o)
                else:
                    name, value, size, info, _oth, shndx = struct.unpack_from("<IIIBBH", self.data, o)
                typ = info & 0xf
                if typ not in (1, 2) or size == 0 or shndx == 0 or shndx >= len(self.sections):
                    continue
                sec = self.sections[shndx]
                if not sec["flags"] & 2:  # SHF_ALLOC
                    continue
                if typ == 2 and self.machine == 40:
                    value &= ~1  # the Thumb bit
                n = self.cstr(strtab["off"] + name)
                k = seen.get(n, 0)
                seen[n] = k + 1
                out[(n, k)] = (sec["name"], value, size, "func" if typ == 2 else "object", shndx)
        return out

    def sym_bytes(self, key):
        secname, addr, size, _t, shndx = self.symbols[key]
        sec = self.sections[shndx]
        if sec["type"] == 8:  # NOBITS
            return None
        o = sec["off"] + addr - sec["addr"]
        return self.data[o:o + size]

    def alloc_sections(self):
        return {s["name"]: s for s in self.sections if s["flags"] & 2 and s["name"]}


def elf_diff(old_path, new_path):
    """A dict: sections moved / resized, symbols changed (same place and size, other bytes), resized, moved,
    added, removed; `same` when the files are identical."""
    a_raw = open(old_path, "rb").read()
    b_raw = open(new_path, "rb").read()
    r = dict(same=a_raw == b_raw, sections=[], changed=[], resized=[], moved=0, added=[], removed=[],
             loaded=None, unloaded=[], stray=None)
    if r["same"]:
        return r
    a, b = Elf(old_path), Elf(new_path)
    if a.segments or b.segments:
        r["loaded"] = a.segments == b.segments

    def body(e, sec):
        return b"" if sec["type"] == 8 else e.data[sec["off"]:sec["off"] + sec["size"]]
    ua = {x["name"]: x for x in a.sections if x["name"] and not x["flags"] & 2}
    ub = {x["name"]: x for x in b.sections if x["name"] and not x["flags"] & 2}
    r["unloaded"] = [n for n in sorted(set(ua) | set(ub)) if n not in ua or n not in ub
                     or body(a, ua[n]) != body(b, ub[n])]
    sa, sb = a.alloc_sections(), b.alloc_sections()
    for n in sorted(set(sa) | set(sb)):
        x, y = sa.get(n), sb.get(n)
        if x is None or y is None:
            r["sections"].append("%s %s" % (n, "added" if x is None else "removed"))
        elif (x["addr"], x["size"]) != (y["addr"], y["size"]):
            r["sections"].append("%s 0x%x+0x%x -> 0x%x+0x%x" % (n, x["addr"], x["size"], y["addr"], y["size"]))
    for k in sorted(set(a.symbols) | set(b.symbols)):
        name = k[0] if k[1] == 0 else "%s#%d" % k
        if k not in b.symbols:
            r["removed"].append(name)
        elif k not in a.symbols:
            r["added"].append(name)
        else:
            x, y = a.symbols[k], b.symbols[k]
            if x[2] != y[2]:
                r["resized"].append("%s %d -> %d" % (name, x[2], y[2]))
            elif x[1] != y[1]:
                r["moved"] += 1
            elif a.sym_bytes(k) != b.sym_bytes(k):
                r["changed"].append(name)
    # same layout: every loaded byte that differs should be inside a symbol named above
    if r["loaded"] is False and not r["sections"] and not r["moved"] and len(a.segments) == len(b.segments):
        spans = []
        for n in r["changed"] + [x.split(" ")[0] for x in r["resized"]]:
            name, _, k = n.partition("#")
            _s, addr, size, _t, _i = b.symbols[(name, int(k or 0))]
            spans.append((addr, addr + ((max(size, a.symbols[(name, int(k or 0))][2]) + 3) & ~3)))
        stray = 0
        for (_t, va, _m, x), (_t2, vb, _m2, y) in zip(a.segments, b.segments):
            if va != vb:
                stray = None
                break
            for i in range(min(len(x), len(y))):
                if x[i] != y[i] and not any(lo <= va + i < hi for lo, hi in spans):
                    stray += 1
            stray += abs(len(x) - len(y))
        r["stray"] = stray
    return r


def diff_text(r):
    if r["same"]:
        return ["identical"]
    out = []
    if r["loaded"] is not None:
        out.append("loaded segments: %s" % ("identical (what the Vita runs is the same)" if r["loaded"] else "differ"))
    if r["stray"] is not None:
        out.append("loaded bytes that differ outside the symbols below: %d" % r["stray"])
    out.append("layout: %s" % ("same" if not r["sections"] and not r["moved"] else
                               "%d section(s) moved or resized, %d symbol(s) moved" % (len(r["sections"]), r["moved"])))
    for s in r["sections"]:
        out.append("  section " + s)
    for key, what in (("changed", "bytes differ"), ("resized", "resized"), ("added", "added"), ("removed", "removed")):
        if r[key]:
            out.append("%d symbol(s) %s: %s" % (len(r[key]), what, " ".join(r[key][:40]) +
                                                 (" ..." if len(r[key]) > 40 else "")))
    if not any(r[k] for k in ("changed", "resized", "added", "removed")):
        out.append("no symbol's bytes differ")
    if r["unloaded"]:
        out.append("sections not loaded that differ: %s" % " ".join(r["unloaded"][:20]))
    return out


def unexpected(r, expect):
    def ok(name):
        base = name.split(" ")[0].split("#")[0]
        return any(fnmatch.fnmatchcase(base, p) for p in expect)
    bad = [n for k in ("changed", "resized", "added", "removed") for n in r[k] if not ok(n)]
    if r.get("stray"):
        bad.append("%d loaded bytes outside every symbol" % r["stray"])
    return bad


def vpk_diff(old, new):
    with zipfile.ZipFile(old) as za, zipfile.ZipFile(new) as zb:
        ea = {i.filename: (i.CRC, i.file_size) for i in za.infolist()}
        eb = {i.filename: (i.CRC, i.file_size) for i in zb.infolist()}
    return dict(changed=sorted(n for n in set(ea) & set(eb) if ea[n] != eb[n]),
                added=sorted(set(eb) - set(ea)), removed=sorted(set(ea) - set(eb)))


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# ---------------------------------------------------------------- run


def cmd_run(a):
    root = os.path.realpath(a.repo or git(os.getcwd(), "rev-parse", "--show-toplevel").strip())
    game = a.game or os.environ.get("RELINK_GAME") or os.path.join(root, "tools", "vita", "relink.game")
    if not os.path.isfile(game):
        raise Refuse("no game settings at %s (--game, RELINK_GAME; relink.game.example)" % game)
    game = os.path.realpath(game)
    cfg = read_game(game)
    have = hooks(game)
    src_dir = os.path.realpath(a.src)
    if not os.path.isdir(src_dir):
        raise Refuse("--from %s is not a directory" % a.src)
    out_dir = os.path.join(root, cfg["RELINK_OUT"])
    out_real = os.path.realpath(out_dir)
    if out_real == src_dir or src_dir.startswith(out_real + "/") or out_real.startswith(src_dir + "/"):
        raise Refuse("--from %s and %s overlap" % (src_dir, out_real))
    db = cfg.get("RELINK_COMPILE_DB")
    need = ["relink_link"] if db else ["relink_build"]
    lost = [h for h in need if h not in have]
    if lost:
        raise Refuse("%s defines no %s" % (game, " ".join(lost)))
    env = dict(os.environ, RELINK_FROM=src_dir, RELINK_REPO=root, RELINK_GAME=game,
               RELINK_EBOOT_ONLY="1" if a.eboot_only else "0")

    check_finished(src_dir, cfg, a.eboot_only)
    batch_root = git(src_dir, "rev-parse", "--show-toplevel", check=False).strip()
    built = None
    if "relink_built_rev" in have:
        built = hook_output(game, "relink_built_rev", [src_dir], root, env) or None
    if built is None and batch_root:
        built = git(batch_root, "rev-parse", "HEAD").strip()
    base = a.base
    if base is None:
        if not built:
            raise Refuse("%s is not in a git checkout and the game has no relink_built_rev: give --base" % src_dir)
        if subprocess.run(["git", "-C", root, "cat-file", "-e", built + "^{commit}"],
                          capture_output=True).returncode:
            raise Refuse("the build's commit %s is not in %s: fetch it, or give --base" % (built, root))
        base = git(root, "merge-base", "HEAD", built).strip()
    base = git(root, "rev-parse", "--verify", base + "^{commit}").strip()

    report = []
    say("worker %s at %s; build %s (built from %s); base %s" % (root, git(root, "rev-parse", "--short", "HEAD").strip(),
                                                                 src_dir, (built or "?")[:12], base[:12]), report)
    paths = sorted(set(changed_paths(root, base)) | set(a.files or []))
    srcs, hdrs, ign, full, unknown = classify(cfg, paths)
    subs = [p for p in unknown if p in submodule_paths(root)]
    if ign:
        say("ignored (RELINK_IGNORE): %s" % " ".join(ign), report)
    if full or (unknown and not a.force):
        why = (["needs the full build (RELINK_FULL): " + " ".join(full)] if full else []) + \
              (["not a source or header and not in RELINK_IGNORE: " + " ".join(unknown) +
                (" (a submodule added, removed or not checked out)" if subs else "")] if unknown and not a.force else [])
        raise Refuse("; ".join(why), 3)
    if unknown:
        say("--force: not rebuilt from these: %s" % " ".join(unknown), report)
    todo = set(srcs)
    deleted = [p for p in todo if not os.path.exists(os.path.join(root, p))]
    if deleted:
        raise Refuse("deleted source(s) %s: the link would keep their objects; needs the full build"
                     % " ".join(deleted), 3)
    if hdrs:
        src_re, hdr_re = re.compile(cfg["RELINK_SOURCES"]), re.compile(cfg["RELINK_HEADERS"])
        deps = dependents(root, hdrs, tracked_files(root), src_re, hdr_re)
        say("headers %s: %d source(s) include them" % (" ".join(hdrs), len(deps)), report)
        todo |= deps
    todo = sorted(todo)
    if built:
        behind = [p for p in lines(git(root, "diff", "--name-only", "--ignore-submodules=all", base, built,
                                       check=False))]
        if behind:
            hb = [p for p in behind if re.search(cfg["RELINK_HEADERS"], p)]
            say("the build has %d file(s) this tree does not (other branches of its batch)%s"
                % (len(behind), "; headers among them: " + " ".join(hb[:10]) if hb else ""), report)
    say("to compile (%d): %s" % (len(todo), " ".join(todo) or "none"), report)
    if a.dry_run:
        return 0

    if os.path.lexists(out_dir) and not (os.path.isdir(out_real) and not os.listdir(out_real)):
        if not a.replace:
            raise Refuse("%s exists: --replace to remove it first" % out_real)
        shutil.rmtree(out_real)
    elif os.path.isdir(out_real):
        os.rmdir(out_real)
    copy_tree(src_dir, out_real, a.mode, cfg["RELINK_SHARED"])
    say("%s: %s of %s" % (out_real, a.mode, src_dir), report)

    if db:
        db_compile(os.path.join(out_real, db), todo, root, batch_root or src_dir, src_dir, out_real,
                   a.jobs or os.cpu_count() or 4)
        rc = call_hook(game, "relink_link", [out_real], root, env)
        if rc:
            raise Refuse("relink_link failed (%d)" % rc, 1)
    else:
        lst = os.path.join(out_real, "relink-files.txt")
        with open(lst, "w") as fh:
            fh.write("".join(p + "\n" for p in todo))
        rc = call_hook(game, "relink_build", [out_real, lst], root, dict(env, RELINK_LIST=lst))
        if rc:
            raise Refuse("relink_build failed (%d)" % rc, 1)

    for key in ("RELINK_ELF", "RELINK_EBOOT", "RELINK_VPK"):
        rel = cfg.get(key)
        if not rel or (key == "RELINK_VPK" and a.eboot_only):
            continue
        old, new = os.path.join(src_dir, rel), os.path.join(out_real, rel)
        if not os.path.isfile(new):
            raise Refuse("the build made no %s" % rel, 1)
        say("%s %s (the build's %s)%s" % (rel, sha1(new)[:12], sha1(old)[:12] if os.path.isfile(old) else "none",
                                         ": identical" if os.path.isfile(old) and sha1(old) == sha1(new) else ""),
            report)
    r = elf_diff(os.path.join(src_dir, cfg["RELINK_ELF"]), os.path.join(out_real, cfg["RELINK_ELF"]))
    for l in diff_text(r):
        say("elf: " + l, report)
    if cfg.get("RELINK_VPK") and not a.eboot_only and os.path.isfile(os.path.join(src_dir, cfg["RELINK_VPK"])):
        v = vpk_diff(os.path.join(src_dir, cfg["RELINK_VPK"]), os.path.join(out_real, cfg["RELINK_VPK"]))
        say("vpk: entries that differ: %s" % (" ".join(v["changed"] + ["+" + x for x in v["added"]] +
                                                      ["-" + x for x in v["removed"]]) or "none"), report)
    with open(os.path.join(out_real, "relink.txt"), "w") as fh:
        fh.write("".join(l + "\n" for l in report))
    say("done: %s" % os.path.join(out_real, cfg.get("RELINK_VPK") if cfg.get("RELINK_VPK") and not a.eboot_only
                                  else cfg.get("RELINK_EBOOT") or cfg["RELINK_ELF"]))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="relink this checkout's change against a finished build's objects")
    r.add_argument("--from", dest="src", required=True, help="the finished build's directory (the batch's)")
    r.add_argument("--repo", help="the worker's checkout (default: the git top level here)")
    r.add_argument("--game", help="the game's relink settings (default ROOT/tools/vita/relink.game, or RELINK_GAME)")
    r.add_argument("--base", help="the commit the worker's change is measured from (default: merge base with the "
                                  "build's commit)")
    r.add_argument("--mode", choices=("clone", "copy", "hardlink"), default="clone")
    r.add_argument("--files", nargs="*", help="also compile these (paths relative to ROOT)")
    r.add_argument("--eboot-only", action="store_true", help="no VPK: the ELF and eboot.bin only (the hook decides)")
    r.add_argument("--replace", action="store_true", help="remove ROOT/RELINK_OUT first when it is there")
    r.add_argument("--force", action="store_true", help="relink although some changed files are not sources, "
                                                        "headers or ignored")
    r.add_argument("--dry-run", action="store_true", help="list what would be compiled and stop")
    r.add_argument("--jobs", type=int, help="parallel compiles with RELINK_COMPILE_DB (default: the CPUs)")
    d = sub.add_parser("diff", help="which symbols differ between two ELF files")
    d.add_argument("old")
    d.add_argument("new")
    d.add_argument("--expect", nargs="*", default=None, help="names (globs) allowed to differ; anything else: exit 1")
    v = sub.add_parser("vpk", help="which entries differ between two VPKs")
    v.add_argument("old")
    v.add_argument("new")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "run":
            return cmd_run(a)
        if a.cmd == "diff":
            r = elf_diff(a.old, a.new)
            for l in diff_text(r):
                print(l)
            if a.expect is not None:
                bad = unexpected(r, a.expect)
                if bad:
                    print("unexpected: %s" % " ".join(bad))
                    return 1
            return 0
        v = vpk_diff(a.old, a.new)
        for k in ("changed", "added", "removed"):
            print("%s: %s" % (k, " ".join(v[k]) or "none"))
        return 0
    except Refuse as e:
        print("relink: %s" % e, file=sys.stderr)
        return e.code
    except (OSError, ValueError, struct.error) as e:
        print("relink: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())

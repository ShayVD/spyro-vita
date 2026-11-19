#!/usr/bin/env python3
"""Read a PS Vita crash dump (ux0:data/psp2core-*.psp2dmp): modules, threads, registers, PC/LR against the ELF.

  psp2dmp.py DUMP [--elf FILE|DIR ...] [--module NAME] [--addr2line PATH] [--stack THREAD] [--depth BYTES] [--title]

  DUMP              the .psp2dmp (gzip or already unpacked)
  --elf FILE|DIR    the unstripped ELF of the same build (vpk_pack.py's input, not the .velf or eboot.bin). Several,
                    or directories, are allowed: the first whose sizes fit a module is used (the dump doesn't name
                    the build). Fit: same data size, and the module's text (the .velf's) is the ELF's plus
                    vita-elf-create's tables, under 64 KiB more (seen +0x1028..+0x1050)
  --module NAME     the app's module (default: the one the ELF fits, else the one holding the main thread's entry)
  --addr2line PATH  also give file:line (default arm-vita-eabi-addr2line from PATH if there; "none" to skip)
  --stack THREAD    the words on that thread's stack (uid in hex, or its name) that point into the app's text with
                    the Thumb bit set: the return addresses, roughly a backtrace. --depth caps the bytes read (0x2000)
  --title           title ids ([A-Z]{4}[0-9]{5}) in the dump, most frequent first: the dump names no app, and the
                    Vita may run other people's games

Prints each module (uid, name, segments: base+size and r/w/x) and each thread (uid, name, status, stop reason
when it stopped on a fault: 0x30002 undefined instruction, 0x30003 prefetch abort, 0x30004 data abort, with the
faulting address for the aborts; what it waits on; r0-r15 and cpsr). PC and LR are given as module, segment and offset; with --elf, the app's addresses are
moved to the ELF's (address - the module's text base + the ELF's text address: e.g. 0x8107b000 vs 0x81000000)
and named from the ELF's .symtab, plus addr2line's file:line (LR minus 1, the call). The dump has the stacks but not
.data/.bss: read globals from registers against `objdump -d`; the data segment is moved on its own (module seg1
vs the ELF's data address).

Format: an ELF32 core (ET_CORE) whose PT_NOTE segments hold notes named after tables (MODULE_INFO, THREAD_INFO,
THREAD_REG_INFO, ...: u32 version, u32 count, entries) and whose PT_LOAD segments hold the stacks. The table
offsets are facts learned from spyro-vita sessions' small readers (logs/s8e/dumpthreads.py, dumpstack.py),
vita-parse-core and psp2_core_parse (MIT; the fault registers), checked on spyro-vita's dumps; no code was taken. Written for
vita-port-kit (spyro-vita's lessons, item P9: kit-lessons E §3.6, §8.5). Python 3, stdlib only.
"""
import bisect, collections, gzip, os, re, shutil, struct, subprocess, sys

STOP = {0x30002: "undefined instruction", 0x30003: "prefetch abort", 0x30004: "data abort", 0x10006: "stack overflow",
        0x20004: "software breakpoint", 0x40001: "VFP exception", 0x60080: "integer division by zero"}
STATUS = {1: "running", 2: "ready", 4: "standby", 8: "waiting", 0x10: "dormant", 0x20: "deleted", 0x40: "dead",
          0x80: "stagnant", 0x100: "suspended"}  # SceThreadStatus
u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
cstr = lambda b, o, n: b[o:o + n].split(b"\0")[0].decode("latin-1")


def die(msg):
    sys.exit("psp2dmp: " + msg)


def phdrs(d):
    if d[:4] != b"\x7fELF" or d[4] != 1:
        die("not an ELF32 file")
    off, = struct.unpack_from("<I", d, 28); esz, num = struct.unpack_from("<HH", d, 42)
    return [struct.unpack_from("<8I", d, off + i * esz) for i in range(num)]  # type off vaddr paddr filesz memsz flags align


class Dump:
    def __init__(self, path):
        d = open(path, "rb").read()
        self.d = d = gzip.decompress(d) if d[:2] == b"\x1f\x8b" else d
        if struct.unpack_from("<H", d, 16)[0] != 4:
            die(f"{path}: not a core file (ET_CORE)")
        self.notes, self.mem = {}, []
        for typ, off, va, _, fsz, *_ in phdrs(d):
            if typ == 1:
                self.mem.append((va, off, fsz))
            elif typ == 4:
                o = off
                while o + 12 <= off + fsz:
                    nsz, dsz, _ = struct.unpack_from("<3I", d, o); q = o + 12 + ((nsz + 3) & ~3)
                    self.notes[cstr(d, o + 12, nsz)] = d[q:q + dsz]; o = q + ((dsz + 3) & ~3)
        self.modules = []  # (uid, name, [(attr, base, size)])
        m = self.notes.get("MODULE_INFO", b""); o = 8
        for _ in range(u32(m, 4) if m else 0):  # head 0x50, segments 0x14 each, foot 0x10
            n = u32(m, o + 0x4c)
            segs = [(u32(m, s + 4), u32(m, s + 8), u32(m, s + 12)) for s in range(o + 0x50, o + 0x50 + n * 0x14, 0x14)]
            self.modules.append((u32(m, o + 4), cstr(m, o + 0x24, 28), segs)); o += 0x60 + n * 0x14
        self.threads = []  # dicts
        t = self.notes.get("THREAD_INFO", b""); o = 8
        for _ in range(u32(t, 4) if t else 0):
            g = lambda k: u32(t, o + k)
            self.threads.append(dict(uid=g(4), name=cstr(t, o + 8, 32), status=g(0x30) & 0xffff, entry=g(0x34),
                                     stack=(g(0x38), g(0x3c)), wait=(g(0x60), g(0x64)), stop=g(0x74), pc=g(0x9c)))
            o += g(0)
        regs, fault = {}, {}; r = self.notes.get("THREAD_REG_INFO", b""); o = 8
        for _ in range(u32(r, 4) if r else 0):  # r0-r15, cpsr; ...; ifsr ifar dfsr dfar at 0x168 (entries of 0x178)
            regs[u32(r, o + 4)] = struct.unpack_from("<17I", r, o + 8)
            if u32(r, o) >= 0x178:
                fault[u32(r, o + 4)] = struct.unpack_from("<4I", r, o + 0x168)
            o += u32(r, o)
        for th in self.threads:
            th["regs"] = regs.get(th["uid"]); th["fault"] = fault.get(th["uid"])

    def read(self, va, n):
        for base, off, size in self.mem:
            if base <= va < base + size:
                return self.d[off + va - base:off + min(va - base + n, size)]
        return b""

    def where(self, a):
        for uid, name, segs in self.modules:
            for i, (_, base, size) in enumerate(segs):
                if base <= a < base + size:
                    return name, i, a - base
        return None


class Elf:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            h = f.read(52)
            if len(h) < 52 or h[:4] != b"\x7fELF" or h[4] != 1 or struct.unpack_from("<H", h, 18)[0] != 40:
                raise ValueError("not an ARM ELF32")
            d = h + f.read()
        self.exe = struct.unpack_from("<H", h, 16)[0] == 2  # ET_EXEC; a .velf is 0xfe04 and has no debug info
        load = [(va, msz, fl) for typ, _, va, _, _, msz, fl, _ in phdrs(d) if typ == 1]
        text = [(va, msz) for va, msz, fl in load if fl & 1]
        if not text:
            raise ValueError("no executable segment")
        self.text, self.size = text[0]
        self.dsize = next((msz for va, msz, fl in load if fl & 2), None)
        shoff, = struct.unpack_from("<I", d, 32); shsz, shnum = struct.unpack_from("<HH", d, 46)
        sh = [struct.unpack_from("<10I", d, shoff + i * shsz) for i in range(shnum)]
        syms = {}
        for s in sh:
            if s[1] != 2:  # SHT_SYMTAB
                continue
            strtab = sh[s[6]]; so = strtab[4]
            for o in range(s[4], s[4] + s[5], 16):
                nm, val, sz, info = struct.unpack_from("<IIIB", d, o)
                name = cstr(d, so + nm, 256) if nm else ""
                if info & 0xf in (0, 2) and name and not name.startswith("$") and val:
                    a = val & ~1
                    if a not in syms or (info & 0xf) == 2:
                        syms[a] = (name, sz)
        if not syms:
            raise ValueError("no symbols (stripped?)")
        self.addrs = sorted(syms); self.syms = syms

    def name(self, a):
        if not self.text <= a < self.text + self.size:
            return "?"
        i = bisect.bisect_right(self.addrs, a) - 1
        if i < 0:
            return "?"
        s = self.addrs[i]; nm, sz = self.syms[s]
        return f"{nm}+{a - s:#x}" + ("" if not sz or a - s < sz else " (past its end)")


def main(argv):
    args = argv[1:]; opt = collections.defaultdict(list); pos = []
    while args:
        a = args.pop(0)
        if a in ("--elf", "--module", "--addr2line", "--stack", "--depth"):
            if not args:
                die(f"{a} needs a value")
            opt[a].append(args.pop(0))
        elif a == "--title":
            opt[a].append(True)
        elif a in ("-h", "--help"):
            print(__doc__); return 0
        elif a.startswith("-"):
            die(f"unknown option {a} (see --help)")
        else:
            pos.append(a)
    if len(pos) != 1:
        die("usage: psp2dmp.py DUMP [--elf FILE|DIR ...] [--module NAME] [--addr2line PATH] [--stack THREAD] "
            "[--depth BYTES] [--title]")
    dmp = Dump(pos[0])
    for want in opt["--stack"]:
        if not any(t["name"] == want or f"{t['uid']:x}" == want.lower().removeprefix("0x") for t in dmp.threads):
            die(f"no thread {want}")
    textseg = lambda segs: next((s for s in segs if s[0] & 1), segs[0] if segs else None)

    elves = []
    for p in opt["--elf"]:
        files = [os.path.join(r, f) for r, _, fs in os.walk(p) for f in fs] if os.path.isdir(p) else [p]
        for f in files:
            try:
                elves.append(Elf(f))
            except (ValueError, OSError, struct.error, IndexError) as e:
                if not os.path.isdir(p):
                    die(f"{f}: {e}")
    app = elf = None
    if opt["--module"]:
        app = next((m for m in dmp.modules if m[1] == opt["--module"][0]), None) or die("no module " + opt["--module"][0])
    # vita-elf-create appends its import/export tables to the text (seen +0x1028..+0x1050): the module's text is the
    # .velf's size, a little more than the ELF's; the data size is the same in all three.
    def fits(e, m):
        t = textseg(m[2]); d = next((s for s in m[2] if s[0] & 2), None)
        return t and 0 <= t[2] - e.size < 0x10000 and (d is None or e.dsize is None or d[2] == e.dsize)
    elves.sort(key=lambda e: not e.exe)
    for e in elves:
        hit = [m for m in ([app] if app else dmp.modules) if fits(e, m)]
        if hit and not elf:
            elf, app = e, hit[0]
    if elves and not elf:
        if len(elves) == 1 and app is None:
            print(f"warning: {elves[0].path}: text {elves[0].size:#x} bytes fits no module's text; using it anyway")
            elf = elves[0]
        else:
            print("warning: no ELF's text size fits " + ("the app's" if app else "any module's") + " text: " +
                  ", ".join(f"{e.path} {e.size:#x}" for e in elves))
            elf = elves[0] if len(elves) == 1 else None
    if app is None and dmp.threads:
        main_entry = dmp.threads[0]["entry"] & ~1
        app = next((m for m in dmp.modules if any(b <= main_entry < b + s for _, b, s in m[2])), None)
    a2l = (opt["--addr2line"] or [shutil.which("arm-vita-eabi-addr2line") or "none"])[0]
    a2l = None if a2l == "none" or not elf else a2l

    print(f"dump {pos[0]}: {len(dmp.modules)} modules, {len(dmp.threads)} threads"
          + (f"; app module {app[1]}" if app else ""))
    if elf:
        print(f"elf {elf.path}: text {elf.text:#x}+{elf.size:#x}" + ("" if not app else
              f" (app text at {textseg(app[2])[1]:#x}+{textseg(app[2])[2]:#x})"))
        for e in elves:
            if e is not elf and app and fits(e, app):
                print(f"  also matches: {e.path}")

    def to_elf(a):
        if os.environ.get("PSP2DMP_MUTANT") == "1":  # the test's mutant: forget the load base
            return a
        return a - textseg(app[2])[1] + elf.text

    def where(a, call=False):
        w = dmp.where(a & ~1)
        if not w:
            return f"{a:#010x}"
        s = f"{a:#010x} {w[0]} seg{w[1]}+{w[2]:#x}"
        if elf and app and w[0] == app[1] and w[1] == app[2].index(textseg(app[2])):
            ea = to_elf(a & ~1); s += f" = elf {ea:#x} {elf.name(ea)}"
            if a2l:
                try:
                    out = subprocess.run([a2l, "-e", elf.path, f"{ea - 1 if call else ea:#x}"], capture_output=True,
                                         text=True, timeout=30).stdout.strip()
                    if out and not out.startswith("??"):
                        s += f" ({out})"
                except (OSError, subprocess.SubprocessError):
                    pass
        return s

    print("\nmodules:")
    for uid, name, segs in dmp.modules:
        print(f"  {uid:#010x} {name:28s} " + "  ".join(
            f"seg{i} {b:#010x}+{s:#x} " + "".join(c if attr & f else "-" for c, f in (("r", 4), ("w", 2), ("x", 1)))
            for i, (attr, b, s) in enumerate(segs)))
    print("\nthreads:")
    for th in dmp.threads:
        st = STATUS.get(th["status"], f"status {th['status']:#x}")
        stop = f", stopped: {STOP.get(th['stop'], 'reason')} ({th['stop']:#x})" if th["stop"] else ""
        wt = f", wait type {th['wait'][0]:#x} on {th['wait'][1]:#x}" if th["status"] == 8 else ""
        f = th["fault"]
        if f and th["stop"] == 0x30004:  # DFSR bit 11: the access was a write
            stop += f" at {f[3]:#x} ({'write' if f[2] & 0x800 else 'read'}, dfsr {f[2]:#x})"
        elif f and th["stop"] == 0x30003:
            stop += f" at {f[1]:#x} (ifsr {f[0]:#x})"
        print(f"  {th['uid']:#010x} {th['name']}: {st}{stop}{wt}")
        r = th["regs"]
        if not r:
            print(f"    pc {where(th['pc'])} (no registers)"); continue
        print(f"    pc {where(r[15])}\n    lr {where(r[14], call=True)}")
        names = [f"r{i}" for i in range(13)] + ["sp", "lr", "pc", "cpsr"]
        for i in range(0, 17, 6):
            print("    " + " ".join(f"{n:>4} {v:08x}" for n, v in zip(names[i:i + 6], r[i:i + 6])))

    for want in opt["--stack"]:
        th = next(t for t in dmp.threads if t["name"] == want or f"{t['uid']:x}" == want.lower().removeprefix("0x"))
        if not (th["regs"] and app):
            die(f"{want}: no registers or no app module")
        sp = th["regs"][13]; lo, sz = th["stack"]
        n = int(opt["--depth"][0], 0) if opt["--depth"] else 0x2000
        n = min(n, lo + sz - sp) if lo <= sp < lo + sz else n
        mem = dmp.read(sp, n); _, tb, ts = textseg(app[2])
        print(f"\nstack of {th['name']} ({th['uid']:#x}) from sp {sp:#x}, {len(mem):#x} bytes:")
        if not mem:
            print("  (the stack isn't in the dump)")
        for o in range(0, len(mem) - 3, 4):
            w = u32(mem, o)
            if w & 1 and tb <= w < tb + ts:
                print(f"  sp+{o:#06x} {where(w, call=True)}")

    if opt["--title"]:
        c = collections.Counter(re.findall(rb"(?<![A-Z])[A-Z]{4}[0-9]{5}(?![0-9])", dmp.d))
        print("\ntitle ids: " + (", ".join(f"{k.decode()} ({v})" for k, v in c.most_common(8)) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

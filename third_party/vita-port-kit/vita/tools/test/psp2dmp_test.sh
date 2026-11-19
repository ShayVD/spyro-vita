#!/usr/bin/env bash
# psp2dmp.py on a synthetic crash dump (gzip ELF core: MODULE_INFO with two modules, THREAD_INFO and THREAD_REG_INFO
# with three threads, one stopped by a data abort, a stack in a PT_LOAD) and two synthetic ARM ELFs with a .symtab,
# one of another build: the right ELF is picked by size, the crashed thread's PC and LR are named with the load base
# subtracted, --stack finds the return addresses, --title the title id. All written here; no game bytes, no ARM
# toolchain. The mutant (PSP2DMP_MUTANT=1: the load base not subtracted) must fail. Needs python3 only.
#   vita/tools/test/psp2dmp_test.sh
set -euo pipefail
tools=$(cd "$(dirname "$0")/.." && pwd)
w=$(mktemp -d "${TMPDIR:-/tmp}/psp2dmp_test.XXXXXX")
trap 'rc=$?; [ "${KEEP:-0}" = 1 ] || rm -rf "$w"; exit $rc' EXIT
fails=0
check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi; }

python3 -I - "$w" <<'PY'
import gzip, os, struct, sys
out = sys.argv[1]; os.makedirs(f"{out}/elves")
TEXT, BASE, STACK = 0x81000000, 0x8107b000, 0x82000000

def elf(path, data_size):  # ET_EXEC, ARM: text + data PT_LOADs, .symtab/.strtab/.shstrtab
    syms = [("", 0, 0, 0), ("main_loop", TEXT + 0x101, 0x40, 0x12), ("$t", TEXT + 0x200, 0, 0),
            ("crash_here", TEXT + 0x201, 0x80, 0x12), ("caller_fn", TEXT + 0x301, 0x60, 0x12),
            ("last_fn", TEXT + 0x1001, 0x20, 0x12), ("g_state", 0x81380010, 4, 0x11)]
    strtab = b"\0"; symtab = b""
    for name, val, size, info in syms:
        symtab += struct.pack("<IIIBBH", len(strtab) if name else 0, val, size, info, 0, 1); strtab += name.encode() + b"\0" if name else b""
    shstr = b"\0.symtab\0.strtab\0.shstrtab\0"
    body = bytes(0x100) + symtab + strtab + shstr
    o_sym = 52 + 2 * 32 + 0x100; o_str = o_sym + len(symtab); o_shs = o_str + len(strtab); shoff = (o_shs + len(shstr) + 3) & ~3
    sh = [bytes(40), struct.pack("<10I", 1, 2, 0, 0, o_sym, len(symtab), 2, 1, 4, 16),
          struct.pack("<10I", 9, 3, 0, 0, o_str, len(strtab), 0, 0, 1, 0),
          struct.pack("<10I", 17, 3, 0, 0, o_shs, len(shstr), 0, 0, 1, 0)]
    eh = b"\x7fELF\1\1\1" + bytes(9) + struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, TEXT + 0x101, 52, shoff, 0x5000000,
                                                    52, 32, 2, 40, len(sh), 3)
    ph = struct.pack("<8I", 1, 0, TEXT, TEXT, 0, 0x2000, 5, 0x1000) + struct.pack("<8I", 1, 0, 0x81380000, 0x81380000, 0, data_size, 6, 0x1000)
    img = eh + ph + body
    open(path, "wb").write(img + bytes(shoff - len(img)) + b"".join(sh))
elf(f"{out}/elves/kit_test", 0x1000)
elf(f"{out}/elves/kit_test_other_build", 0x1800)  # same text, different data: not this dump's build

def table(entries, ver=1):
    return struct.pack("<II", ver, len(entries)) + b"".join(entries)
def module(uid, name, segs):
    head = bytearray(0x50); struct.pack_into("<I", head, 4, uid); head[0x24:0x24 + len(name)] = name.encode()
    struct.pack_into("<I", head, 0x4c, len(segs))
    return bytes(head) + b"".join(struct.pack("<5I", 0, a, b, s, 0x10) for a, b, s in segs) + bytes(0x10)
def thread(uid, name, status, entry, stack, wait, stop, pc):
    e = bytearray(0xc8); struct.pack_into("<II", e, 0, 0xc8, uid); e[8:8 + len(name)] = name.encode()
    struct.pack_into("<4I", e, 0x30, 0x80000000 | status, entry, *stack); struct.pack_into("<2I", e, 0x60, *wait)
    struct.pack_into("<I", e, 0x74, stop); struct.pack_into("<I", e, 0x9c, pc); return bytes(e)
def regs(uid, sp, lr, pc, dfsr=0, dfar=0):
    e = bytearray(0x178); struct.pack_into("<II", e, 0, 0x178, uid)
    struct.pack_into("<16I", e, 8, *[0x1000 + i for i in range(13)], sp, lr, pc); struct.pack_into("<I", e, 0x48, 0x60000030)
    struct.pack_into("<2I", e, 0x170, dfsr, dfar); return bytes(e)
notes = {
    "MODULE_INFO": table([module(0x4001000b, "SceLibKernel", [(5, 0xe0000000, 0x10000), (6, 0xe0020000, 0x100)]),
                          module(0x40010001, "kit_test", [(5, BASE, 0x2050), (6, 0x81400000, 0x1000)])]),
    "THREAD_INFO": table([thread(0x40010003, "KITT00001", 8, BASE + 0x101, (0x81f00000, 0x10000), (0x20, 0x40010099), 0, 0xe0001234),
                          thread(0x40010005, "kit_render", 1, BASE + 0x301, (STACK - 0xf000, 0x10000), (0, 0), 0x30004, BASE + 0x210),
                          thread(0x40010007, "kit_audio", 8, BASE + 0x301, (0x81f20000, 0x4000), (0x10, 0xffffffff), 0, 0xe0001234)], 0x12),
    "THREAD_REG_INFO": table([regs(0x40010003, 0x81f0ff00, BASE + 0x121, 0xe0001234),
                              regs(0x40010005, STACK + 0xf00, BASE + 0x331, BASE + 0x210, 0x807, 0x10),
                              regs(0x40010007, 0x81f23f00, 0xe0001001, 0xe0001234)], 0x11),
    "APP_INFO": struct.pack("<II", 6, 0) + b"KITT00001".ljust(16, b"\0"),
}
stack = bytearray(0x1000)  # sp = STACK + 0xf00: two return addresses, an even text pointer and an odd one elsewhere
for off, word in ((0xf10, BASE + 0x335), (0xf20, BASE + 0x200), (0xf30, 0x09000001), (0xf40, BASE + 0x10b)):
    struct.pack_into("<I", stack, off, word)
blobs = []
for name, desc in notes.items():
    n = name.encode() + b"\0"; n += bytes(-len(n) % 4)
    blobs.append((4, struct.pack("<3I", len(name) + 1, len(desc), 0x1000) + n + desc + bytes(-len(desc) % 4), 0))
blobs.append((1, bytes(stack), STACK))
phoff = 52; off = phoff + 32 * len(blobs); ph = b""; data = b""
for typ, blob, va in blobs:
    ph += struct.pack("<8I", typ, off + len(data), va, 0, len(blob), len(blob), 4 if typ == 1 else 0, 4); data += blob
eh = b"\x7fELF\1\1\1" + bytes(9) + struct.pack("<HHIIIIIHHHHHH", 4, 40, 1, 0, phoff, 0, 0, 52, 32, len(blobs), 40, 0, 0)
open(f"{out}/psp2core-0-0x0000000000-eboot.bin.psp2dmp", "wb").write(gzip.compress(eh + ph + data))
PY

dump="$w/psp2core-0-0x0000000000-eboot.bin.psp2dmp"
run() { python3 -I "$tools/psp2dmp.py" "$dump" --elf "$w/elves" --addr2line none --stack kit_render --title "$@"; }
run > "$w/out.txt"
cat "$w/out.txt"
o="$w/out.txt"
check "the app module named, with the ELF of this build picked by size" \
  "grep -q 'app module kit_test' '$o' && grep -q '^elf .*/kit_test: text 0x81000000+0x2000 (app text at 0x8107b000+0x2050)' '$o'"
check "modules: both, with segments" \
  "grep -q 'SceLibKernel .*seg0 0xe0000000+0x10000 r-x' '$o' && grep -q 'kit_test .*seg0 0x8107b000+0x2050 r-x  seg1 0x81400000+0x1000 rw-' '$o'"
check "threads: names, states, the data abort and its address" \
  "grep -q '0x40010003 KITT00001: waiting, wait type 0x20 on 0x40010099' '$o' && grep -q '0x40010005 kit_render: running, stopped: data abort (0x30004) at 0x10 (write, dfsr 0x807)$' '$o' && grep -q '0x40010007 kit_audio: waiting' '$o'"
check "the crashed thread's PC named with the load base subtracted" \
  "grep -A1 'kit_render: running' '$o' | grep -q 'pc 0x8107b210 kit_test seg0+0x210 = elf 0x81000210 crash_here+0x10$'"
check "its LR named too" "grep -A2 'kit_render: running' '$o' | grep -q 'lr 0x8107b331 kit_test seg0+0x330 = elf 0x81000330 caller_fn+0x30$'"
check "registers" "grep -A5 'kit_render: running' '$o' | grep -q 'sp 82000f00   lr 8107b331   pc 8107b210 cpsr 60000030'"
check "a PC outside the app: module and offset only" "grep -A1 'KITT00001: waiting' '$o' | grep -q 'pc 0xe0001234 SceLibKernel seg0+0x1234$'"
check "--stack: the two return addresses, nothing else" \
  "grep -q 'sp+0x0010 .*caller_fn+0x34' '$o' && grep -q 'sp+0x0040 .*main_loop+0xa' '$o' && [ \$(grep -c '^  sp+' '$o') = 2 ]"
check "--title: the title id" "grep -q '^title ids: KITT00001 ' '$o'"
check "only the other build's ELF: a warning" \
  "python3 -I '$tools/psp2dmp.py' '$dump' --elf '$w/elves/kit_test_other_build' --addr2line none | grep -q 'warning: .*fits no module'"
check "not a core file: refused" "! python3 -I '$tools/psp2dmp.py' '$w/elves/kit_test' 2>/dev/null"

echo "mutant (PSP2DMP_MUTANT=1: the load base not subtracted) must fail:"
PSP2DMP_MUTANT=1 run > "$w/mutant.txt"
if grep -A1 'kit_render: running' "$w/mutant.txt" | grep -q 'crash_here+0x10$'; then
  echo "  FAIL the mutant still names crash_here"; fails=$((fails + 1))
else
  echo "  ok   the mutant does not name crash_here"
fi

[ "$fails" = 0 ] && echo "psp2dmp_test: all passed" || { echo "psp2dmp_test: $fails failed"; exit 1; }

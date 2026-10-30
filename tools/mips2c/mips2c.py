#!/usr/bin/env python3
"""mips2c: translate the decomp's splat MIPS I assembly into C, symbolically
(docs/porting-plan.md §7, decisions D1, D16).

    mips2c.py --root S1_DIR --out-dir GEN [--hints HINTS] FILE.s ...

Each input .s file is one *unit* and becomes one C file with one static
function holding all of the file's code. Every glabel is an entry point:
  NAME(a0..a7)      C ABI, for C callers (args 5-8 go to the emulated stack)
  NAME__m2c(a0..a3) for translated callers (stack args already in place)
Inside a unit, `j`/`jal`/branches are gotos, so hand-written code that falls
through or jumps between glabels, shares registers across local calls, or
uses `jr $t4` local subroutines keeps working. Computed jumps (`jr reg`) go
through one dispatch switch over PS1 code addresses: labels, jump-table
entries, return points of local calls and every instruction after an
address-taken label. Registers are C locals; $sp points into the emulated
stack (g_mips_sp, in the arena). Memory is accessed at host addresses:
%hi/%lo(sym) use the host address of sym, 0x1F80xxxx (scratchpad) maps to
the arena's scratchpad. GTE ops call PsyCross's GTE (MTC2/CTC2/GTE_operator).

Every instruction is either translated or the run fails (self-check).
"""
import argparse
import os
import re
import sys

REGS = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
REGNO = {n: i for i, n in enumerate(REGS)}
REGNO["s8"] = 30
C2DATA = ["VXY0", "VZ0", "VXY1", "VZ1", "VXY2", "VZ2", "RGB", "OTZ", "IR0", "IR1", "IR2", "IR3", "SXY0", "SXY1",
          "SXY2", "SXYP", "SZ0", "SZ1", "SZ2", "SZ3", "RGB0", "RGB1", "RGB2", "RES1", "MAC0", "MAC1", "MAC2",
          "MAC3", "IRGB", "ORGB", "LZCS", "LZCR"]
C2CTRL = ["R11R12", "R13R21", "R22R23", "R31R32", "R33", "TRX", "TRY", "TRZ", "L11L12", "L13L21", "L22L23",
          "L31L32", "L33", "RBK", "GBK", "BBK", "LR1LR2", "LR3LG1", "LG2LG3", "LB1LB2", "LB3", "RFC", "GFC",
          "BFC", "OFX", "OFY", "H", "DQA", "DQB", "ZSF3", "ZSF4", "FLAG"]
GTE_OPS = {"RTPS", "RTPT", "NCLIP", "OP", "DPCS", "INTPL", "MVMVA", "NCDS", "CDP", "NCDT", "NCCS", "CC", "NCS",
           "NCT", "SQR", "DCPL", "DPCT", "AVSZ3", "AVSZ4", "GPF", "GPL", "NCCT", "DPCL"}
BRANCHES = {"beq", "bne", "beqz", "bnez", "blez", "bgtz", "bltz", "bgez", "b", "bal", "bltzal", "bgezal"}
JUMPS = {"j", "jal", "jr", "jalr"}
LOADS = {"lb": "S8", "lbu": "U8", "lh": "S16", "lhu": "U16", "lw": "U32"}
STORES = {"sb": "U8", "sh": "U16", "sw": "U32"}

INSN = re.compile(r"^/\*\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s*\*/\s*(\S+)\s*(.*?)\s*(/\*.*\*/)?\s*$")
WORD = re.compile(r"^/\*[^*]*\*/\s*\.word\s+(\S+)")


class Fail(Exception):
    pass


class Insn:
    __slots__ = ("addr", "raw", "op", "args", "file", "line")

    def __init__(self, addr, raw, op, args, file, line):
        self.addr, self.raw, self.op, self.args, self.file, self.line = addr, raw, op, args, file, line

    def where(self):
        return f"{self.file}:{self.line} ({self.addr:08X} {self.op} {', '.join(self.args)})"


def split_args(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


class Unit:
    """One .s file."""

    def __init__(self, path, rel):
        self.path, self.rel = path, rel
        self.insns = []          # in address order
        self.labels = {}         # name -> addr (local .L and glabels in .text)
        self.glabels = []        # [(name, addr)]
        self.jtbls = {}          # name -> [label names]
        self.rostrs = {}         # name -> bytes (.rodata strings)
        self.parse()
        self.by_addr = {i.addr: k for k, i in enumerate(self.insns)}
        self.addr_labels = {}
        for n, a in self.labels.items():
            self.addr_labels.setdefault(a, []).append(n)
        self.hand = "nonmatchings" not in rel
        self.name = "m2c_" + re.sub(r"\W", "_", os.path.splitext(rel)[0].replace("asm/", ""))

    def parse(self):
        section = ".text"
        pending, cur_jtbl = [], None
        for n, raw in enumerate(open(self.path), 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith(".section"):
                section = line.split()[1].rstrip(",")
                continue
            m = re.match(r"^(glabel|jlabel|dlabel)\s+(\S+)", line)
            if m:
                kind, name = m.groups()
                if section.startswith(".rodata"):
                    cur_jtbl = name
                    self.jtbls[name] = []
                else:
                    pending.append(name)
                    if kind == "glabel":
                        self.glabels.append([name, None])
                continue
            m = re.match(r"^(\.L\w+|\w+):$", line)
            if m:
                pending.append(m.group(1))
                continue
            if section.startswith(".rodata"):
                m = WORD.match(line)
                if m and cur_jtbl:
                    self.jtbls[cur_jtbl].append(m.group(1))
                    continue
                m = re.match(r'^/\*[^*]*\*/\s*\.asciz\s+(".*")$', line)
                if m and cur_jtbl:
                    if cur_jtbl in self.jtbls:
                        if self.jtbls[cur_jtbl]:
                            raise Fail(f"{self.rel}:{n}: string inside a jump table")
                        del self.jtbls[cur_jtbl]
                        self.rostrs[cur_jtbl] = b""
                    lit = eval(m.group(1)).encode("latin-1") + b"\0"  # noqa: S307 - a string literal
                    self.rostrs[cur_jtbl] += lit
                    continue
                m = re.match(r"^\.align\s+(\d+)$", line)
                if m and cur_jtbl in self.rostrs:
                    al = 1 << int(m.group(1))
                    while len(self.rostrs[cur_jtbl]) % al:
                        self.rostrs[cur_jtbl] += b"\0"
                    continue
                if line.startswith("."):
                    continue
                raise Fail(f"{self.rel}:{n}: unexpected rodata line: {line}")
            m = INSN.match(line)
            if m:
                addr = int(m.group(2), 16)
                b = bytes.fromhex(m.group(3))
                rawword = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24
                ins = Insn(addr, rawword, m.group(4), split_args(m.group(5)), self.rel, n)
                for p in pending:
                    self.labels[p] = addr
                    for g in self.glabels:
                        if g[0] == p:
                            g[1] = addr
                pending = []
                self.insns.append(ins)
                continue
            if line.startswith(".") or line.startswith("/*"):
                continue
            raise Fail(f"{self.rel}:{n}: can't parse: {line}")
        for g in self.glabels:
            if g[1] is None:
                raise Fail(f"{self.rel}: glabel {g[0]} has no code")


def cnum(v):
    v &= 0xFFFFFFFF
    return f"0x{v:X}u"


SAFE_EXPR = re.compile(r"^[0-9a-fA-FxX\s()+\-*/&|<>~]+$")


def eval_num(s):
    if not SAFE_EXPR.match(s):
        return None
    try:
        return int(eval(s, {"__builtins__": {}}, {}))  # noqa: S307 - digits and operators only
    except Exception:
        return None


class Translator:
    def __init__(self, units, hints):
        self.units = units
        self.hints = hints
        self.global_funcs = {}  # glabel -> unit
        for u in units:
            for name, _ in u.glabels:
                self.global_funcs[name] = u

    # ---------- operand helpers ----------
    def reg(self, s):
        if not s.startswith("$"):
            raise Fail(f"expected register, got {s}")
        n = s[1:]
        if n.isdigit():
            n = REGS[int(n)]
        if n == "s8":
            n = "fp"
        if n not in REGNO:
            raise Fail(f"unknown register {s}")
        return "0u" if n == "zero" else n

    def dst(self, s):
        r = self.reg(s)
        return None if r == "0u" else r

    def c2(self, s, ctrl):
        if s.startswith("C2_"):
            names = C2CTRL if ctrl else C2DATA
            if s[3:] not in names:
                raise Fail(f"unknown GTE register {s}")
            return names.index(s[3:])
        if s.startswith("$") and s[1:].isdigit():
            return int(s[1:])
        raise Fail(f"bad GTE register {s}")

    def symaddr(self, expr):
        """C expression (u32) for the address `sym [+-] off`."""
        v = eval_num(expr)
        if v is not None:
            return None, v & 0xFFFFFFFF
        m = re.match(r"^([A-Za-z_.$][\w.$]*)\s*(?:([+-])\s*(.+))?$", expr.strip())
        if not m:
            raise Fail(f"bad symbol expression {expr}")
        sym, sign, off = m.groups()
        o = 0
        if off:
            o = eval_num(off)
            if o is None:
                raise Fail(f"bad offset in {expr}")
            if sign == "-":
                o = -o
        u = self.u
        if sym in u.labels:
            return None, (u.labels[sym] + o) & 0xFFFFFFFF  # PS1 code address
        if sym in u.jtbls or sym in u.rostrs:
            base = f"(u32)(uintptr_t){sym}"
        else:
            self.data_refs.add(sym)
            base = f"(u32)(uintptr_t){sym}"
        return (f"({base} + {cnum(o)})" if o else base), None

    def imm(self, s, kind="s"):
        """Immediate operand -> C expression. kind: s (sign-extend 16), z (zero-extend 16), raw."""
        s = s.strip()
        m = re.match(r"^%(hi|lo|gp_rel)\((.*)\)$", s)
        if m:
            fn, inner = m.groups()
            c, k = self.symaddr(inner)
            if fn == "hi":
                return cnum(((k + 0x8000) >> 16) << 16) if c is None else f"MIPS_HI({c})"
            if fn == "gp_rel":
                raise Fail("%gp_rel outside a memory operand")
            if c is None:
                lo = k & 0xFFFF
                return cnum(lo if kind == "z" else (lo - 0x10000 if lo & 0x8000 else lo))
            return f"MIPS_LOZ({c})" if kind == "z" else f"MIPS_LO({c})"
        v = eval_num(s)
        if v is None:
            raise Fail(f"bad immediate {s}")
        return cnum(v)

    def mem(self, s):
        """`off($base)` -> C address expression."""
        m = re.match(r"^(.*)\((\$\w+)\)$", s.strip())
        if not m:
            raise Fail(f"bad memory operand {s}")
        off, base = m.group(1).strip(), self.reg(m.group(2))
        g = re.match(r"^%gp_rel\((.*)\)$", off)
        if g:
            c, k = self.symaddr(g.group(1))
            return c if c is not None else cnum(k)
        ea = f"{base} + {self.imm(off, 's')}" if off else base
        # The game dereferences KUSEG addresses, in hand-written and compiled
        # code alike (bit-31-tagged model pointers with the tag cleared, packed
        # 21-bit pointers, speculative loads through null): on the PS1
        # 0x00000000-0x001FFFFF mirrors RAM, so every access maps that range.
        return f"MIPS_KUSEG({ea})"

    # ---------- emission ----------
    def lab(self, addr):
        return f"L_{addr:08X}"

    def target(self, ins, s):
        """Branch/jump target in this unit -> address, else None."""
        if s in self.u.labels:
            return self.u.labels[s]
        return None

    def simple(self, ins):
        """C statements for a non-control-flow instruction."""
        op, a = ins.op, ins.args
        if op == "nop":
            return ""
        if op in GTE_OPS or (op.isupper() and op not in ("RA",)):
            if op not in GTE_OPS:
                raise Fail(f"unknown GTE op {op}")
            return f"GTE_operator(0x{ins.raw & 0x1FFFFFF:X});"
        if op in ("addu", "add", "subu", "sub", "and", "or", "xor", "nor", "slt", "sltu", "sllv", "srlv", "srav"):
            d, s1, s2 = self.dst(a[0]), self.reg(a[1]), self.reg(a[2])
            if d is None:
                return ""
            e = {"addu": f"{s1} + {s2}", "add": f"{s1} + {s2}", "subu": f"{s1} - {s2}", "sub": f"{s1} - {s2}",
                 "and": f"{s1} & {s2}", "or": f"{s1} | {s2}", "xor": f"{s1} ^ {s2}", "nor": f"~({s1} | {s2})",
                 "slt": f"(s32){s1} < (s32){s2}", "sltu": f"{s1} < {s2}",
                 "sllv": f"{s1} << ({s2} & 31)", "srlv": f"{s1} >> ({s2} & 31)",
                 "srav": f"(u32)((s32){s1} >> ({s2} & 31))"}[op]
            return f"{d} = {e};"
        if op in ("neg", "negu"):
            d, s1 = self.dst(a[0]), self.reg(a[1])
            return f"{d} = 0u - {s1};" if d else ""
        if op in ("move",):
            d, s1 = self.dst(a[0]), self.reg(a[1])
            return f"{d} = {s1};" if d else ""
        if op == "not":
            d, s1 = self.dst(a[0]), self.reg(a[1])
            return f"{d} = ~{s1};" if d else ""
        if op in ("sll", "srl", "sra"):
            d, s1, sa = self.dst(a[0]), self.reg(a[1]), eval_num(a[2])
            if d is None:
                return ""
            if op == "sll":
                return f"{d} = {s1} << {sa};"
            if op == "srl":
                return f"{d} = {s1} >> {sa};"
            return f"{d} = (u32)((s32){s1} >> {sa});"
        if op in ("addiu", "addi", "slti", "sltiu"):
            d, s1, i = self.dst(a[0]), self.reg(a[1]), self.imm(a[2], "s")
            if d is None:
                return ""
            if op in ("addiu", "addi"):
                return f"{d} = {s1} + {i};"
            if op == "slti":
                return f"{d} = (s32){s1} < (s32){i};"
            return f"{d} = {s1} < {i};"
        if op in ("andi", "ori", "xori"):
            d, s1, i = self.dst(a[0]), self.reg(a[1]), self.imm(a[2], "z")
            if d is None:
                return ""
            o = {"andi": "&", "ori": "|", "xori": "^"}[op]
            return f"{d} = {s1} {o} {i};"
        if op == "lui":
            d = self.dst(a[0])
            if d is None:
                return ""
            s = a[1].strip()
            m = re.match(r"^%hi\((.*)\)$", s)
            if m:
                return f"{d} = {self.imm(s)};"
            v = eval_num(s)
            if v is None:
                raise Fail(f"bad lui operand {s}")
            v &= 0xFFFF
            if v == 0x1F80:
                return f"{d} = MIPS_SCRATCH_HI; /* scratchpad */"
            return f"{d} = {cnum(v << 16)};"
        if op == "li":
            d = self.dst(a[0])
            return f"{d} = {self.imm(a[1], 'raw')};" if d else ""
        if op in LOADS:
            d = self.dst(a[0])
            ea = self.mem(a[1])
            if d is None:
                return ""
            return f"{d} = (u32)M_{LOADS[op]}({ea});"
        if op in STORES:
            s1 = self.reg(a[0])
            return f"M_{STORES[op]}({self.mem(a[1])}) = ({'u8' if op == 'sb' else 'u16' if op == 'sh' else 'u32'}){s1};"
        if op in ("lwl", "lwr"):
            d = self.dst(a[0])
            return f"{d} = MIPS_{op.upper()}({d}, {self.mem(a[1])});" if d else ""
        if op in ("swl", "swr"):
            return f"MIPS_{op.upper()}({self.reg(a[0])}, {self.mem(a[1])});"
        if op in ("mult", "multu", "div", "divu"):
            s1, s2 = self.reg(a[-2]), self.reg(a[-1])
            if len(a) == 3 and self.reg(a[0]) != "0u":
                raise Fail("3-operand div")
            return f"MIPS_{op.upper()}({s1}, {s2});"
        if op in ("mfhi", "mflo"):
            d = self.dst(a[0])
            return f"{d} = {op[2:]};" if d else ""
        if op in ("mthi", "mtlo"):
            return f"{op[2:]} = {self.reg(a[0])};"
        if op == "mtc2":
            return f"MTC2({self.reg(a[0])}, {self.c2(a[1], False)});"
        if op == "ctc2":
            return f"CTC2({self.reg(a[0])}, {self.c2(a[1], True)});"
        if op == "mfc2":
            d = self.dst(a[0])
            r = self.c2(a[1], False)
            return f"{d} = MFC2({r});" if d else f"(void)MFC2({r});"
        if op == "cfc2":
            d = self.dst(a[0])
            r = self.c2(a[1], True)
            return f"{d} = CFC2({r});" if d else f"(void)CFC2({r});"
        if op == "lwc2":
            return f"MTC2(M_U32({self.mem(a[1])}), {self.c2(a[0], False)});"
        if op == "swc2":
            return f"M_U32({self.mem(a[1])}) = MFC2({self.c2(a[0], False)});"
        if op == "break":
            code = eval_num(a[0]) if a else 0
            return f"mips2c_break({code or 0}, 0x{ins.addr:08X}u);"
        raise Fail(f"unhandled instruction {ins.where()}")

    def writes(self, ins):
        """Registers an instruction writes (names)."""
        op, a = ins.op, ins.args
        if op in STORES or op in ("swl", "swr", "swc2", "mtc2", "ctc2", "lwc2", "nop", "break", "mult", "multu",
                                  "div", "divu", "mthi", "mtlo") or op in BRANCHES or op in ("j", "jr"):
            return set()
        if op in GTE_OPS:
            return set()
        if op == "jal":
            return {"ra"}
        if op == "jalr":
            return {self.reg(a[0])} if len(a) == 2 else {"ra"}
        if a and a[0].startswith("$"):
            r = self.reg(a[0])
            return set() if r == "0u" else {r}
        return set()

    def reads(self, ins):
        regs = set()
        op, a = ins.op, ins.args
        start = 1 if self.writes(ins) and op not in ("jal",) else 0
        if op in STORES or op in ("swl", "swr", "mtc2", "ctc2") or op in BRANCHES or op in ("jr", "mult", "multu",
                                                                                              "div", "divu",
                                                                                              "mthi", "mtlo"):
            start = 0
        if op in ("lwl", "lwr"):
            start = 0  # merges into the old value
        for x in a[start:]:
            for r in re.findall(r"\$(\w+)", x):
                if r in REGNO and r != "zero":
                    regs.add("fp" if r == "s8" else r)
                elif r.isdigit() and int(r) and op not in ("mtc2", "mfc2", "ctc2", "cfc2", "lwc2", "swc2"):
                    regs.add(REGS[int(r)])
        return regs

    def is_load(self, ins):
        return ins.op in LOADS or ins.op in ("lwl", "lwr", "mfc2", "cfc2")

    def call_ext(self, name):
        """C statements calling an external function by name (not in this unit)."""
        if name in self.global_funcs:
            self.ext_m2c.add(name)
            return (f"g_mips_sp = sp; v0 = {name}__m2c(a0, a1, a2, a3); v1 = g_mips_v1;")
        self.ext_funcs.add(name)
        return (f"g_mips_sp = sp; v0 = (u32){name}(a0, a1, a2, a3, M_U32(sp + 16u), M_U32(sp + 20u), "
                f"M_U32(sp + 24u), M_U32(sp + 28u)); v1 = g_mips_v1;")

    def control(self, ins, delay):
        """C for a branch/jump with its delay slot statement (already translated)."""
        op, a = ins.op, ins.args
        nxt = ins.addr + 8
        if op in ("b",):
            t = self.target(ins, a[0])
            return f"{delay} goto {self.lab(t)};"
        if op in BRANCHES and op not in ("bal", "bltzal", "bgezal"):
            if op in ("beq", "bne"):
                s1, s2 = self.reg(a[0]), self.reg(a[1])
                cond = f"{s1} {'==' if op == 'beq' else '!='} {s2}"
                lab = a[2]
            else:
                s1 = self.reg(a[0])
                cond = {"beqz": f"{s1} == 0", "bnez": f"{s1} != 0", "blez": f"(s32){s1} <= 0",
                        "bgtz": f"(s32){s1} > 0", "bltz": f"(s32){s1} < 0", "bgez": f"(s32){s1} >= 0"}[op]
                lab = a[1]
            t = self.target(ins, lab)
            if t is None:
                raise Fail(f"branch out of unit {ins.where()}")
            if not delay:
                return f"if ({cond}) goto {self.lab(t)};"
            return f"{{ int c_ = {cond}; {delay} if (c_) goto {self.lab(t)}; }}"
        if op == "bal":
            t = self.target(ins, a[0])
            self.ret_points.add(nxt)
            return f"{delay} ra = 0x{nxt:08X}u; goto {self.lab(t)};"
        if op == "j":
            t = self.target(ins, a[0])
            if t is not None:
                return f"{delay} goto {self.lab(t)};"
            return f"{delay} {self.call_ext(a[0])} jt_ = ra; goto dispatch_; /* tail call */"
        if op == "jal":
            t = self.target(ins, a[0])
            self.ret_points.add(nxt)
            if t is not None:
                return f"{delay} ra = 0x{nxt:08X}u; goto {self.lab(t)};"
            return f"{delay} ra = 0x{nxt:08X}u; {self.call_ext(a[0])}"
        if op == "jr":
            r = self.reg(a[0])
            return f"jt_ = {r}; {delay} goto dispatch_;"
        if op == "jalr":
            rd, rs = ("ra", self.reg(a[0])) if len(a) == 1 else (self.dst(a[0]), self.reg(a[1]))
            self.ret_points.add(nxt)
            self.has_jalr = True
            link = f"{rd} = 0x{nxt:08X}u;" if rd else ""
            return (f"jt_ = {rs}; {delay} {link} "
                    f"if (MIPS_IN_UNIT(jt_)) goto dispatch_; "
                    f"g_mips_sp = sp; v0 = ((m2c_fn)(uintptr_t)jt_)(a0, a1, a2, a3, M_U32(sp + 16u), "
                    f"M_U32(sp + 20u), M_U32(sp + 24u), M_U32(sp + 28u)); v1 = g_mips_v1;")
        raise Fail(f"unhandled control {ins.where()}")

    def translate(self, u):
        self.u = u
        self.data_refs, self.ext_funcs, self.ext_m2c = set(), set(), set()
        self.ret_points, self.has_jalr = set(), False
        self.hazards = []
        self.kuseg = False
        body = []
        insns = u.insns
        dispatch = set(a for _, a in u.glabels)
        for tbl, ents in u.jtbls.items():
            for e in ents:
                if eval_num(e) is not None:
                    continue  # numeric filler entry (never taken)
                if e not in u.labels:
                    raise Fail(f"{u.rel}: jump table {tbl} entry {e} not in unit")
                dispatch.add(u.labels[e])
        # address-taken labels: every instruction from the label to the next label
        taken = set()
        for ins in insns:
            for x in ins.args:
                for m in re.finditer(r"%(?:hi|lo)\(\s*([A-Za-z_.][\w.]*)", x):
                    if m.group(1) in u.labels:
                        taken.add(u.labels[m.group(1)])
        for a in sorted(taken):
            k = u.by_addr[a]
            dispatch.add(a)
            k += 1
            while k < len(insns) and insns[k].addr not in u.addr_labels:
                dispatch.add(insns[k].addr)
                k += 1
        dispatch |= set(self.hints.get(u.rel, {}).get("dispatch", []))
        branch_targets = set()
        for ins in insns:
            if ins.op in BRANCHES or ins.op in ("j", "jal"):
                t = self.target(ins, ins.args[-1])
                if t is not None:
                    branch_targets.add(t)

        k = 0
        n = len(insns)
        while k < n:
            ins = insns[k]
            label_here = (ins.addr in branch_targets or ins.addr in dispatch or ins.addr in u.addr_labels
                          or ins.addr in self.ret_points)
            if label_here:
                body.append(f"{self.lab(ins.addr)}:;")
            if ins.op in BRANCHES or ins.op in JUMPS:
                if k + 1 >= n:
                    raise Fail(f"branch without delay slot {ins.where()}")
                d = insns[k + 1]
                if d.op in BRANCHES or d.op in JUMPS:
                    raise Fail(f"branch in delay slot {d.where()}")
                # Hand-written code loads speculatively in delay slots, also
                # through null pointers: on the PS1 that reads the RAM mirror
                # at 0x00000000 (KUSEG), so map low addresses to the arena.
                self.kuseg = u.hand and (d.op in LOADS or d.op == "lwc2")
                dstmt = self.simple(d)
                self.kuseg = False
                body.append(f"  {self.control(ins, dstmt)}")
                d_is_target = d.addr in branch_targets or d.addr in dispatch
                if d_is_target:
                    after = insns[k + 2].addr if k + 2 < n else None
                    body.append(f"  goto {self.lab(after) if after else 'end_'};")
                    body.append(f"{self.lab(d.addr)}:;")
                    body.append(f"  {dstmt}")
                    if after is not None:
                        branch_targets.add(after)
                k += 2
                continue
            stmt = self.simple(ins)
            # Packed RAM pointers (porting-plan §4.4): hand-written code keeps the
            # low 21 bits (`sll 11; srl 11`, or `srl 10` for a halved field) and
            # dereferences the result as a KUSEG address. Rebase it on the arena.
            if (u.hand and ins.op == "srl" and k > 0 and insns[k - 1].op == "sll"
                    and eval_num(insns[k - 1].args[2]) == 11 and eval_num(ins.args[2]) in (10, 11)
                    and self.reg(insns[k - 1].args[0]) == self.reg(ins.args[0]) == self.reg(ins.args[1])):
                r = self.reg(ins.args[0])
                stmt += f" {r} += (u32)g_psx_arena_base; /* KUSEG pointer -> arena */"
            # MIPS I load delay: the next instruction still sees the old value
            if self.is_load(ins) and k + 1 < n and ins.op not in ("lwl", "lwr"):
                nx = insns[k + 1]
                w = self.writes(ins)
                if w and (w & self.reads(nx)) and not (nx.addr in branch_targets or nx.addr in dispatch
                                                        or nx.addr in u.addr_labels):
                    r = next(iter(w))
                    if nx.op in BRANCHES or nx.op in JUMPS:
                        self.hazards.append(f"load-delay into branch at {nx.where()}")
                    elif r in self.writes(nx):
                        body.append(f"  /* load delay: {ins.op} result discarded by next insn */")
                        body.append(f"  {self.simple(nx)}")
                        k += 2
                        continue
                    else:
                        tmp = stmt.replace(f"{r} = ", "ld_ = ", 1).replace(f"{r} = MFC2", "ld_ = MFC2")
                        body.append(f"  {{ u32 ld_; {tmp} {self.simple(nx)} {r} = ld_; }} /* load delay */")
                        k += 2
                        continue
            if stmt:
                body.append(f"  {stmt}")
            k += 1
        if self.hazards:
            raise Fail(f"{u.rel}: unhandled load-delay hazards:\n  " + "\n  ".join(self.hazards))
        dispatch |= self.ret_points
        return self.emit(u, body, dispatch, branch_targets)

    def emit(self, u, body, dispatch, branch_targets):
        lo = min(i.addr for i in u.insns)
        hi = max(i.addr for i in u.insns) + 4
        out = [f"/* Generated by tools/mips2c from {u.rel}; do not edit. */",
               '#include "mips2c_rt.h"', ""]
        own = {g for g, _ in u.glabels}
        for s in sorted(self.data_refs - own - set(u.jtbls) - set(u.rostrs) - self.ext_funcs):
            out.append(f"extern char {s}[];")
        for f in sorted(self.ext_funcs - own):
            out.append(f"extern u32 {f}();")
        for f in sorted(self.ext_m2c - own):
            out.append(f"u32 {f}__m2c(u32, u32, u32, u32);")
        for name, blob in u.rostrs.items():
            vals = ", ".join(str(b) for b in blob)
            out.append(f"static const u8 {name}[] __attribute__((aligned(8))) = {{{vals}}};")
        for tbl, ents in u.jtbls.items():
            vals = ", ".join(cnum(eval_num(e)) if eval_num(e) is not None else f"0x{u.labels[e]:08X}u"
                             for e in ents)
            out.append(f"static const u32 {tbl}[] = {{{vals}}};")
        out.append("")
        out.append(f"#undef MIPS_IN_UNIT\n#define MIPS_IN_UNIT(a) ((a) >= 0x{lo:08X}u && (a) < 0x{hi:08X}u)")
        out.append(f"static u32 {u.name}(u32 entry_, u32 a0, u32 a1, u32 a2, u32 a3) {{")
        out.append("  u32 at = 0, v0 = 0, v1 = 0, t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0, t5 = 0, t6 = 0, t7 = 0;")
        out.append("  u32 s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0, t8 = 0, t9 = 0;")
        out.append("  u32 k0 = 0, k1 = 0, gp = 0, fp = 0, ra = MIPS_RA_SENTINEL, hi = 0, lo = 0;")
        out.append("  u32 sp = g_mips_sp, jt_ = entry_;")
        out.append("  const u32 sp_entry_ = sp;")
        out.append("  (void)at; (void)k0; (void)k1; (void)gp; (void)hi; (void)lo;")
        # Oracle replay (src/game/oracle_replay.c): the outermost call takes the
        # emulator's registers, as hand-written entry points pass inputs in
        # any register. Host debug build only.
        out.append("#if MIPS2C_REPLAY")
        out.append("  if (g_m2c_seed) {")
        out.append("    const u32 *r_ = g_m2c_seed;")
        out.append("    g_m2c_seed = 0;")
        out.append("    at = r_[1]; v0 = r_[2]; v1 = r_[3]; t0 = r_[8]; t1 = r_[9]; t2 = r_[10]; t3 = r_[11];")
        out.append("    t4 = r_[12]; t5 = r_[13]; t6 = r_[14]; t7 = r_[15]; s0 = r_[16]; s1 = r_[17]; s2 = r_[18];")
        out.append("    s3 = r_[19]; s4 = r_[20]; s5 = r_[21]; s6 = r_[22]; s7 = r_[23]; t8 = r_[24]; t9 = r_[25];")
        out.append("    gp = r_[28]; fp = r_[30]; lo = r_[32]; hi = r_[33];")
        out.append("  }")
        out.append("#endif")
        out.append("  goto dispatch_;")
        out += body
        out.append("end_:")
        out.append(f'  mips2c_fell_off("{u.rel}");')
        out.append("dispatch_:")
        out.append("  if (jt_ == MIPS_RA_SENTINEL) goto return_;")
        out.append("  switch (jt_) {")
        for a in sorted(dispatch):
            if a in u.by_addr:
                out.append(f"  case 0x{a:08X}u: goto {self.lab(a)};")
        out.append("  default: break;")
        out.append("  }")
        out.append(f'  mips2c_bad_jump("{u.rel}", jt_);')
        out.append("return_:")
        out.append("  g_mips_sp = sp_entry_;")
        out.append("  g_mips_v1 = v1;")
        out.append("  return v0;")
        out.append("}")
        out.append("")
        for g, a in u.glabels:
            out.append(f"u32 {g}__m2c(u32 a0, u32 a1, u32 a2, u32 a3) {{ "
                       f"return {u.name}(0x{a:08X}u, a0, a1, a2, a3); }}")
            out.append(f"u32 {g}(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {{")
            out.append("  u32 r_, sp_ = g_mips_sp;")
            out.append("  g_mips_sp = sp_ - 32u;")
            out.append("  M_U32(sp_ - 16u) = a4; M_U32(sp_ - 12u) = a5; M_U32(sp_ - 8u) = a6; M_U32(sp_ - 4u) = a7;")
            out.append(f"  r_ = {u.name}(0x{a:08X}u, a0, a1, a2, a3);")
            out.append("  g_mips_sp = sp_;")
            out.append("  return r_;")
            out.append("}")
        return "\n".join(out) + "\n"


def index_source(entries):
    """m2c_index.c: PS1 address, name and NAME__m2c of every entry point, for
    spyro --replay (tools/oracle/capture.lua)."""
    out = ['#include "mips2c_rt.h"', ""]
    for a, g in sorted(entries):
        out.append(f"u32 {g}__m2c(u32, u32, u32, u32);")
    out += ["", "const M2cEntry m2c_index[] = {"]
    for a, g in sorted(entries):
        out.append(f'  {{0x{a:08X}u, "{g}", {g}__m2c}},')
    out += ["  {0, 0, 0},", "};", ""]
    return "\n".join(out)


def write_if_changed(path, src):
    old = open(path).read() if os.path.exists(path) else None
    if old != src:
        with open(path, "w") as f:
            f.write(src)


def load_hints(path):
    if not path:
        return {}
    import yaml
    with open(path) as f:
        return yaml.safe_load(f) or {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--hints")
    ap.add_argument("--list", help="write the generated file names here")
    ap.add_argument("--index", help="write a table of every entry point here (oracle replay)")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    try:
        units = [Unit(os.path.join(a.root, f), f) for f in a.files]
        tr = Translator(units, load_hints(a.hints))
        os.makedirs(a.out_dir, exist_ok=True)
        names, entries = [], []
        for u in units:
            src = tr.translate(u)
            out = os.path.join(a.out_dir, u.name + ".c")
            names.append(out)
            old = open(out).read() if os.path.exists(out) else None
            if old != src:
                with open(out, "w") as f:
                    f.write(src)
            entries += [(a, g) for g, a in u.glabels]
            print(f"mips2c: {u.rel}: {len(u.insns)} insns, {len(u.glabels)} entries", file=sys.stderr)
        if a.index:
            write_if_changed(a.index, index_source(entries))
    except Fail as e:
        print(f"mips2c: error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

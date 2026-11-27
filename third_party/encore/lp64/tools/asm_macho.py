#!/usr/bin/env python3
"""ELF-flavoured GNU assembly (the data a port generates from a decomp: labels, words, strings) in Mach-O's dialect,
for macOS arm64 (lp64/docs/low-image.md, "macOS"):

    asm_macho.py IN.s OUT.s

  - C's names take Mach-O's leading underscore (`g_Table` -> `_g_Table`); ELF's local labels (`.Lname`) lose the
    dot (`Lname`) and take none;
  - `.type`, `.size` and the `.note.GNU-stack` section go;
  - `.section .rodata` is `__TEXT,__const`; any other `.section NAME[,flags]` is `__DATA,NAME` (without the dot);
  - the operands of `.globl`, `.set`, `.long`, `.quad`, `.word`, `.short` and `.byte` and every label are renamed, a
    label before a directive on its line too; C comments in data operands go.
Everything else (`.balign`, `.space`, `.asciz`, `.incbin`, `.data`, `.text`) passes as it is. A 32-bit word can't
hold an address in a macOS image (a PIE above 4 GB): emit address words as 0 with a {slot, target} table instead
(lp64_p32_image_start's `words`, spyro-vita's data2s.py --p32-offset).

From spyro-vita tools/data2s.py's macho() @ 36d9e07 (MIT; D265), there fixed to its own local-label prefix (.Ls1).
"""
import re
import sys

IDENT = re.compile(r"(?<![\w.$])(\.?[A-Za-z_][\w.$]*)")


def sym(name):
    if name.startswith(".L"):
        return name[1:]
    if name.startswith("."):
        return name  # a directive or the location counter's kin: not a symbol
    return "_" + name


def syms(expr):
    return IDENT.sub(lambda m: sym(m.group(1)), expr)


def convert(lines):
    out = []
    for line in lines:
        t = line.strip()
        if t.startswith((".type ", ".size ")) or "GNU-stack" in t:
            continue
        if t.startswith(".section "):
            name = t.split()[1].split(",")[0]
            line = ".section __TEXT,__const" if name == ".rodata" else f".section __DATA,{name.lstrip('.')}"
        elif t.startswith((".globl ", ".global ", ".set ")):
            d, rest = t.split(None, 1)
            line = f"{d} {syms(rest)}"
        elif t.startswith((".long ", ".quad ", ".word ", ".short ", ".byte ", ".4byte ", ".8byte ")):
            d, rest = t.split(None, 1)
            line = f"{d} {syms(re.sub(r'/[*].*?[*]/', '', rest))}"
        else:
            m = re.match(r"^(\.?[A-Za-z_][\w.$]*):\s*(.*)$", t)
            if m:  # a label, alone or before a directive
                out.append(sym(m.group(1)) + ":")
                if m.group(2):
                    out += convert([m.group(2)])
                continue
        out.append(line)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.split("\n\n")[1])
    with open(sys.argv[1]) as f:
        lines = f.read().split("\n")
    with open(sys.argv[2], "w") as f:
        f.write("\n".join(convert(lines)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

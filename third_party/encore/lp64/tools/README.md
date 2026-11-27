# lp64 tools

Host scripts for the low-image model ([`../docs/low-image.md`](../docs/low-image.md)), in the order the playbook
([`../../playbooks/lp64-port`](../../playbooks/lp64-port/SKILL.md)) uses them. Python 3, no packages; the ones that
compile run where the build's compilers are (the host container). Every one prints its usage with `--help` or in its
docstring.

| Tool | What | spyro-vita's run (D173, D174) |
|---|---|---|
| `trial64.py --db BUILD32/compile_commands.json --out DIR [--ptr32 INCLUDE_DIR]` | replays the 32-bit build's commands with clang for x86-64 and counts what breaks; `--ptr32` marks the headers' pointer fields and labels first | 22 failed units, 52 layout asserts plain; 17 and 8 with `--ptr32` |
| `p32_patch.py TREE --out PATCH [--list] [--skip-else-of MACRO]` | the pointer patch over the patched decomp: fields, labels, `T **`, SDK `long`s, `P32_INIT` tables; never adds a line before a file's end | 231 fields, 223 labels and tables, 54 `T **`, 5 `long`s, 5 `P32_INIT` |
| `p32_lint.py LABELS DIR... [--host-fn NAME]` | file-scope pointer declarations under a label's name without `P32`, and function-pointer labels that aren't the port's own | 0 found |
| `layout64.py DUMP32 DUMP64 [--host-only FILE]` | every struct of both builds' DWARF the same size and offsets, but the host's own, listed with a reason | 447 types, 0 mismatched, 65 host-only |
| `cast_audit.py BUILD64` | every pointer/integer cast of the 64-bit build, by file, to read for host pointers | 260 sites |
| `clang_bugs/check.sh [CLANG]` | which of clang's four `__ptr32` bugs a clang has; the workarounds compile | clang 19.1.7, Apple clang 16 and LLVM 18.1.8: all present |

## The offset model's tools (ARM64)

For the offset model ([`../docs/low-image.md`](../docs/low-image.md#the-offset-model-arm64)), in the order the
playbook's step 7 uses them. Each reads clang's JSON AST of a build's units (`compile_commands.json`, absolute paths as
CMake writes them) and takes the game's paths as arguments.

| Tool | What | spyro-vita's run (D258-D267) |
|---|---|---|
| `p32_offset_rewrite.py BUILD --src DIR --decomp TREE --patch-dir DIR [--apply] [--list FILE]` | on a low-image build: wraps every load, store, compound and step of a slot in its accessor; the decomp's edits as patches, the port's in place, never a line moved; token pastes and parts of macros listed | 4,575 sites (3,970 text, 299 macro arguments, 297 whole macro uses, 9 `#define` bodies) |
| `p32_offset_rewrite.py BUILD --casts ...` | on an offset-model build: `P32_ENC` on every pointer→32-bit cast's operand, `P32_DEC` on every 32-bit→pointer cast's; `(int)P32_GET(x)` by a textual pass | the decomp's casts patch and ~30 port files |
| `p32_offset_check.py BUILD` | the completeness check: every low-image unit compiled with `-DLP64_P32_OFFSET=1`; a missed or extra accessor is an error, a slot/non-slot pointer mismatch a warning that counts | 166 and 174 units, 0 diagnostics |
| `p32_decl_check.py BUILD` | a name a slot in one unit and a pointer in another; a function with a pointer in one declaration and a 32-bit integer in another | 695 names, 3,781 functions: 0 (but glibc's `strerror_r`) |
| `p32_narrow_check.py BUILD [--widen \| --raw]` | 64→32-bit integer narrowings of values that can be addresses; `--widen` the other way; `--raw` a 32-bit word read through a cast pointer and widened (a slot read raw) | 19 and 14 sites read; `--raw` 2 sites before the #281 fix, 0 after |
| `macho_syms.py [--keep-global-symbol N] [--localize-symbols F] [--redefine-sym O=N] IN.o OUT.o` | objcopy's symbol edits on a Mach-O object: private extern, then `ld -r` | the presenter's and the 60 Hz rules' objects |
| `asm_macho.py IN.s OUT.s` | ELF-flavoured GNU assembly in Mach-O's dialect: underscores, `.L` labels, sections | the data image's and the presenter's assembly |

Changed from spyro-vita's (`tools/ps4/p32_offset_*.py`, `p32_decl_check.py`, `p32_narrow_check.py`,
`tools/macho_syms.py`, `tools/data2s.py`'s `macho()` @ `36d9e07`):
- the fixed paths are arguments: `--src`, `--decomp`, `--patch-dir`, `--patch-stem`, `--root`, `--header` (spyro's
  `src/compat/p32.h`), `--host-side`, `--skip` (its `/gen/`), `--overlay-preprocess` (its overlays' `build.ninja`
  rule), `--define` (its `SPYRO_P32_OFFSET`), `--slot-typedef` (its `\w+Slot`);
- a slot behind a typedef (`typedef P32F(Moby *) MobySlot;`) is found: clang prints its desugared type with the
  qualifiers first (`__uptr __ptr32 Moby *`), which spyro's suffix test missed (the kit's test found it);
- a slot read *inside* a token-paste macro's expression (`#define F(n) (g_##n->m_X)`) is listed instead of the whole
  macro use being wrapped (`P32_GET(F(a))`, a compile error): the whole-use check spyro ran for casts only now runs
  for slots too, and a pasted whole name (`#define S(n) g_##n`) is still wrapped (the kit's test found it);
- `p32_decl_check.py` takes Apple clang's `union (unnamed at ...)` as well as clang 19's `unnamed union at`;
- `asm_macho.py`: any `.L` local label (spyro's was `.Ls1` only).

**Test:** `test/offset_tools_test.py` on a stand-in decomp and port (`test/offset_fixture/`): the check fails before,
the rewrite (a patch that applies, no line moved, the token paste listed), the check passes after and fails its
mutant, i686 assembly unchanged, the casts pass on aarch64, the narrowing and raw reads listed, the declaration
mismatch found; on macOS `macho_syms.py`'s edits (nm and a link) and `asm_macho.py`'s output assembled and read back.
`CLANG=cc` on a Mac, clang 19 in the lp64 image.

## A host-only list

`layout64.py --host-only FILE` takes the types allowed to differ between the builds: the host's own, which hold host
pointers and never lie in the game's memory. One a line, the name and why (`#` comments). spyro-vita's
(`tools/ps4/layout64_host_only.txt`, kept there) is in three groups: the hand-written renderers' and collision code's
working state (stream cursors, face-loop contexts), its checks' and presenter's bookkeeping, and platform state. A
game struct never belongs in it: a mismatch there means a missed `P32` or `long`. An entry that no longer differs is
reported as stale, so the list can't hide a type that later moves into the game's memory.

## What stays in the game repo

The game's patch files, its host-only list, its function-pointer globals (spyro-vita's `p32_fnptrs.c`) and its label
renames, its region tables. The tools take the game's names as arguments (`--host-fn-file`, `--skip-else-of`,
`--ignore-prefix`, `--type-regex`).

## Changed from spyro-vita's

- `p32_patch.py`: `--skip-else-of MACRO` (spyro's fixed `SPYRO_PORT` rule), `--dirs` (its fixed `include`/`src`),
  `--type-regex` (its capitalised-type rule), `--long-names`. An initialiser holding strings (`= {"A", "BB"}`) is now
  taken: spyro's compared the line with its string-blanked copy and skipped it (the kit's test found it).
- `p32_lint.py`: `LABELS` may be a plain name list; spyro's 13 overlay function pointers are `--host-fn` /
  `--host-fn-file`, its `Terminator_` rule `--ignore-prefix`.
- `layout64.py`: carries the DWARF parser of spyro's `tools/layoutcheck.py`.
- `cast_audit.py`: the build folder is an argument; `--clang`, `--only`.
- `trial64.py`: `--db`, `--out`, `--ptr32 DIR`, `--path-map`, `--sysroot` optional; spyro's overlay units (generated
  32-bit output) are left to a hand-written `--db`; the header rewrite is `p32_patch.py`'s (fields and labels; spyro's
  trial marked fields only).
- `clang_bugs/`: `fnptr_call.c` split in two (the wrong code and the crash), each file with its workaround under
  `-DWORKAROUND`, and `check.sh` to run them.

**From:** spyro-vita `tools/ps4/` @ `c25a3ea` (MIT). **Test:** `test/tools_test.py` on a synthetic decomp
(`test/fixture/`, `test/harness.c`), run by `../test/run_tests.sh`.

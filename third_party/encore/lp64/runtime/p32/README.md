# p32

`LP64_P32` (short alias `P32`): a pointer field or data label 32 bits wide on x86-64 with clang (`__ptr32 __uptr`,
needs `-fms-extensions`), nothing on every other target. With it on every pointer field of a decomp's structs and on
every pointer-typed data label, the game's memory keeps its original layout on a 64-bit build, and the 32-bit builds
(the Vita, an i686 host) compile the same text to the same code. Header only; force it into every unit
(`-include lp64_p32.h`, see [`../../cmake/`](../../cmake/)).

- `T *P32 field;`, `T *P32 *P32 list;`: a pointer the game's memory holds. `__uptr`: zero-extended when read, so an
  address up to 4 GB survives; the low image keeps them all below 2 GB anyway ([`../../docs/low-image.md`](../../docs/low-image.md)).
- `T *P32_INIT(name)[N] = {...};` plus `P32_INIT_ARRAY(T, name)` at the file's end (`P32_INIT_SCALAR` for one
  pointer): a table initialised with addresses, which clang 19 can't emit as a `__ptr32` static initializer. A
  constructor fills it before `main()`. Declare it in headers as usual (`extern T *P32 name[N];`).
- `LP64_P32_ON`: 1 when `P32` means something. `LP64_P32_NO_SHORT_NAMES`: leave the short names out.
- A 64-bit target with neither `__ptr32` nor the offset model (gcc on x86-64, a non-x86 clang with
  `LP64_P32_OFFSET=0`) is an `#error`, since the fields would silently become 8 bytes; `LP64_P32_WIDE_OK` allows it
  where nothing reads the game's memory (a 64-bit analysis tool), and keeps AArch64's default off.

## The offset model (ARM64): `P32F` and the accessors

On AArch64 `__ptr32` does nothing, so there a slot is a 32-bit offset from `lp64_p32_base`
([`../../docs/low-image.md`](../../docs/low-image.md#the-offset-model-arm64)). The same header carries it, on by default
for AArch64 LP64 (`LP64_P32_OFFSET`, 1 to force it anywhere):

- **`P32F(T *)`** spells a slot in every model: `T *` on 32-bit, `T *__ptr32 __uptr` on x86-64, a 4-byte union on
  ARM64. `P32F(Moby *) m_Next;`, two levels `P32F(P32F(Moby *) *) m_List;`, a pointer to slots `P32F(Moby *) *w`.
  The bare `T *P32` still works in the low image; in the offset model it is a compile error.
- **Every use through an accessor:** `P32_GET(x)`, `P32_SET(x, v)` (gives `v`), `P32_ADD`/`P32_SUB(x, n)`,
  `P32_PREINC`/`PREDEC`/`POSTINC`/`POSTDEC(x)`, `P32_PATCH(x, base)` (a loader's fix-up; `P32_PATCH2` adds the other
  way round, for byte identity with code written `x + base`). Elsewhere each is the code's own text (`(x)`,
  `((x) = (v))`), so the 32-bit and x86-64 binaries don't change.
- **Casts:** `(int)P32_ENC(p)` and `(T *)P32_DEC(i)`: the offset and back. A non-pointer passes `P32_ENC` and a
  pointer passes `P32_DEC` (chosen by type), so a macro that takes either can have them in its body. NULL, small
  numbers and sentinels (0xffffffff) keep their low 32 bits. `P32_ARG_T`/`P32_ARG(a)`: an entry point that takes the
  caller's pointers as 32-bit words takes a 64-bit register and encodes it.
- **Base:** `lp64_p32_base`: on macOS `__mh_execute_header` (`__asm__` name); on Linux link
  `--defsym=lp64_p32_base=__executable_start` ([`../../cmake/linuxa64.toolchain.cmake`](../../cmake/linuxa64.toolchain.cmake)).
- **`LP64_P32_CHECK=1`**: the checking build; a host pointer encoded stops in `lp64_p32_check_fail`
  ([`../p32_image`](../p32_image/README.md)).
- `LP64_ASM_LIT("name")`: a C name as an `__asm__` label (Mach-O's underscore).
- **Two traps.** Two `P32F(T *)` spellings are two distinct unions: a redeclaration borrows the first's type
  (`extern __typeof__(g_X) g_X;`) or both use one typedef (`typedef P32F(Moby *) MobySlot;`); an
  incompatible-pointer warning between two slot unions of one layout is harmless, one between a slot and anything else
  is a bug. And `P32F(T *) *a, *b;` makes `b` a plain `T *`: the macro's `*` belongs to the first declarator only.

**Tests:** `test/p32_same.c` also spells slots with `P32F` and uses every accessor, against the plain code they
replace (the same assembly on i686, ARM and Thumb); `test/p32_complete.c` is the completeness check on x86-64 (a missed
accessor, an extra one and the bare qualifier fail with `-DLP64_P32_OFFSET=1`); the offset model's run on AArch64 is
[`../p32_image`](../p32_image/README.md)'s. Run by `../test/run_offset_tests.sh`.

**From:** spyro-vita `src/compat/p32.h` @ `36d9e07` (MIT; D249, D258, D262, D265). Its `SPYRO_P32_OFFSET`,
`SPYRO_P32_CHECK`, `spyro_p32_base`, `SPYRO_ASM_LIT` are `LP64_P32_OFFSET`, `LP64_P32_CHECK`, `lp64_p32_base`,
`LP64_ASM_LIT`; the accessors keep their names. Changed: `uintptr_t` is `__UINTPTR_TYPE__` (no `<stdint.h>` needed),
`P32_PATCH` casts to `unsigned int` (spyro's PsyQ `u_int`, the same type).

**Naming.** The kit's prefix is the canonical spelling (`LP64_P32`, `LP64_P32_INIT`, ...), as `vpk_` / `ps1_` are
elsewhere. The short `P32` names stay as aliases on purpose: they appear on hundreds of lines of a generated decomp
patch (231 fields and 223 labels in spyro-vita's), where a 9-character token would push lines past the decomp's
format and make the patch harder to review, and spyro-vita's existing patch already uses them.
[`../../tools/p32_patch.py`](../../tools/p32_patch.py) writes the short names (`--long-names` for the long ones).

**What it cannot do** (clang 19, [`../../docs/clang-bugs.md`](../../docs/clang-bugs.md)): calls through a `P32`
function pointer, a `?:` mixing a `P32` and a normal pointer, a `P32` static initializer holding an address.
Also: `(uintptr_t)p32_field` is a 32-bit integer read and never extends; `(T *)p32_field` converts as the qualifier
says (`__sptr` would sign-extend, `__uptr` doesn't).

**From:** spyro-vita `src/compat/p32.h` @ `c25a3ea` (MIT; D173, D174).

**Tests** (run by `../test/run_host_tests.sh`):
- `test/p32_test.c` on x86-64 clang, static, under `qemu-x86_64` on another machine: a 40-byte struct with `P32`
  fields keeps its 32-bit offsets; a raw word 0xF0000000 reads back as a pointer zero-extended; a `T *P32 *` walks a
  table of `P32` pointers 4 bytes a step; `P32_INIT` tables are filled before `main()`. The mutant
  (`LP64_P32_MUTANT`, `__sptr`) must fail.
- `test/p32_same.c` on each 32-bit compiler (i686 gcc, i686 clang with `-fms-extensions`, `arm-vita-eabi-gcc` in
  ARM and Thumb): the same assembly through the header as with every `P32` spelled as nothing by hand.
- the same unit on a 64-bit gcc: refused without `LP64_P32_WIDE_OK`, accepted with it.

## Windows (mingw)

x86_64-w64-mingw32 clang has `__ptr32` too, but mingw's `_mingw.h` defines `__ptr32` and `__ptr64` as empty macros
(MSVC's keywords, for other compilers): once any system header is in, every P32 field silently becomes 8 bytes.
`lp64_p32.h` includes `_mingw.h` and undefines the two (it is guarded, so it never defines them again), and nothing
else: a header forced into units that are preprocessed and compiled again with it forced in (a decomp's overlays)
must not bring structs. Build with `-mno-ms-bitfields` too, so the structs keep their GNU layout.
**Test:** `test/p32_mingw.c` (compile-only static asserts after `<stdio.h>`; its mutant, `LP64_P32_MUTANT_MINGW`,
must fail), in `run_host_tests.sh` (`CCWIN`) and `targets/pc/test/desktop_runtime_tests.sh`. From spyro-vita's
Windows build (its D241).

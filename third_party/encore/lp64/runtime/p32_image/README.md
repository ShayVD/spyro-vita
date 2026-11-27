# p32_image

The offset model's start-up and checks (lp64/docs/low-image.md,
["The offset model"](../../docs/low-image.md#the-offset-model-arm64)), for a game whose `P32F` slots are 32-bit
offsets from `lp64_p32_base` ([`../p32`](../p32/README.md)). Linux (ELF, static or dynamic) and macOS (Mach-O); empty
unless `LP64_P32_OFFSET`.

- **`lp64_p32_image_start(words, why, n)`**: call it first in `main`, before anything reads the game's data. It checks
  that the image lies above 4 GB (so a pointer cut to 32 bits instead of encoded points at nothing) and within 4 GB of
  its start (so an offset reaches all of it), then stores the data image's pointer words: `words` is `{slot, target}`
  pairs of 8 bytes ending `{0, 0}` (an assembler's `.quad slot, target` table, NULL for none), and each 32-bit slot
  becomes `target`'s offset, as `P32_SET` stores it. A word outside the image is refused. Returns the number of words,
  or -1 with why.
- `lp64_p32_image_end()`: Linux's `_end`; on macOS the end of the last segment but `__LINKEDIT`, slid.
- `lp64_p32_within(p, len)`: 1 if `[p, p+len)` is within the 4 GB above the base.
- `lp64_p32_check_fail`: the checking build's stop (`LP64_P32_CHECK=1`): the file, line and value of a host pointer
  encoded, then `abort()`.

**The game provides:** the base (macOS: nothing, it is the Mach-O header; Linux: `--defsym=lp64_p32_base=
__executable_start` and a link above 4 GB, not 4 GB-aligned:
[`../../cmake/linuxa64.toolchain.cmake`](../../cmake/linuxa64.toolchain.cmake)), its data image's words table, and on
macOS any ELF linker symbols its own code reads (`etext`, `_edata`, `_end`: marks in the image, as spyro-vita's save
state stamp needs). A dynamic Linux executable also links
[`../low_image/lp64_low_image_dyn.c`](../low_image/README.md) for the reservation below 4 GB.

**From:** spyro-vita `src/platform/common/p32_image.c` @ `36d9e07` (MIT; D262, D265). Changed: the words table is an
argument (spyro's `s1_pie_words`); the Mach-O `etext`/`_edata`/`_end` marks stay the game's.

**Test:** [`test/p32_offset_test.c`](test/p32_offset_test.c), natively on AArch64, run by
[`../test/run_offset_tests.sh`](../test/run_offset_tests.sh) on a Mac (a PIE above `__PAGEZERO`) and on aarch64 Linux
(static, and dynamic with the reservation, both at 0x2_0123_0000): a 40-byte struct of `P32F` slots keeps its 32-bit
offsets; every accessor stores an offset and reads the pointer; the casts round-trip, a sentinel and integers pass; a
loader's fix-up; the `P32_INIT` tables; the data word stored; a pointer cut to 32 bits crashes in a child; a word
outside the image refused. Mutants: `CUT` (an encode that keeps the low 32 bits: the miss spyro-vita's D262 and D265
were about; it crashes) and `WORDS` (the data word left unstored) must fail. The checking build must stop at a host
pointer with its file and line. On Linux, without the reservation (`LP64_LOW_IMAGE_MUTANT=1`) the dynamic run fails.
Note for a test's own "host" memory: on macOS an unhinted `mmap` lands right after the image, inside the window.

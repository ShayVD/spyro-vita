# low_image

The low image of a 4-byte-pointer game on 64-bit Linux (x86-64): everything the game can address below 2 GB, nothing
of the host's below 4 GB. Link `lp64_low_image.c` into a static, non-PIE executable (base 0x400000, the x86-64
default; [`../../cmake/linux64.toolchain.cmake`](../../cmake/linux64.toolchain.cmake)) and it provides:

- **`main()`**: checks that the image, the game thread's stack and `lp64_game_memory_end()` lie below 2 GB and exits
  1 with a `lp64-low-image:` line otherwise; then runs `lp64_game_main(argc, argv)` on a thread whose stack is an
  8 MB `.bss` array (`LP64_GAME_STACK`), since game code takes the addresses of locals and some reach 32-bit fields.
- **the malloc family** (`malloc`, `calloc`, `realloc`, `free`, `memalign`, `aligned_alloc`, `posix_memalign`,
  `valloc`, `pvalloc`, `malloc_usable_size`): one `mmap` per block, unmapped by `free`, so above 4 GB and a use after
  free crashes. glibc's own can't be used: static glibc's `sbrk` heap starts right after the image, before any
  constructor runs ([`../../docs/low-image.md`](../../docs/low-image.md#static-glibcs-sbrk-heap)).
- **a constructor** that reserves every free page from 64 KiB to 4 GB `PROT_NONE`: later mappings (blocks, thread
  stacks) land above, and a host pointer cut to 32 bits points at nothing and crashes instead of aliasing low memory.
- `lp64_is_low(p, n)`, `lp64_on_game_stack(p)`, `lp64_reserved_pages()`; `LP64_LOW_IMAGE_REPORT=1` in the
  environment prints where everything lies.

**The game provides** (the split rule of `docs/layout.md`):
- `int lp64_game_main(int, char **)`, required: its own `main`, e.g. its `main.c` compiled with
  `-Dmain=lp64_game_main` for the 64-bit build only (spyro-vita: `set_source_files_properties(main.c PROPERTIES
  COMPILE_DEFINITIONS main=spyro_host_main)`).
- `const void *lp64_game_memory_end(void)`, optional (weak, default `NULL`): the end of any game memory outside the
  image. A game whose arena is a `.bss` array (the model's way) needs none; spyro-vita's `psx_low_arena_end` returned
  its `.bss` arena's end.

**Changed from spyro-vita's:** a reservation that `mmap` places elsewhere is unmapped and its range split until each
piece lands where asked. Under `qemu-x86_64` 7.2, `MAP_FIXED_NOREPLACE` is only a hint for some large ranges, and
spyro-vita's copy left 0x10000-0x400000 (below the image) unreserved while counting it (this module's test found it).
Spyro's `SPYRO_LOW_IMAGE_REPORT` is `LP64_LOW_IMAGE_REPORT`; its log prefix `spyro:` is `lp64-low-image:`.

x86-64 Linux only (`/proc/self/maps`, `MAP_FIXED_NOREPLACE`). A console's version waits until its loader's addresses
have been logged on hardware (spyro-vita's PS4 plan P2).

**From:** spyro-vita `src/platform/linux32/low_image.c` @ `c25a3ea` (MIT; D173, D174).

**Test:** `test/low_image_test.c`, static x86-64, under `qemu-x86_64` on another machine (run by
`../test/run_host_tests.sh`): the game's main on the low stack; a `.bss` arena and the code below 2 GB; every
malloc-family block, a fresh `mmap` and a new thread's stack above 4 GB; `free` unmaps; no page from 64 KiB to 4 GB
that a mapping could take (a gap in the list that can't be mapped is the emulator's own memory: a newer
`qemu-x86_64` running a whole amd64 container hides a few pages of its own there, and a guest read of them crashes
too); a malloc'd pointer cut to 32 bits lands in the reservation and a forked child reading it dies of `SIGSEGV`.
The mutant (`LP64_LOW_IMAGE_MUTANT=1`, no reservation) must fail. Built with `LP64_TEST_REFUSE` (the game reports
memory up to 0x90000000), `main()` must exit 1 without running the game's main.

## The desktop variants

Two more files, for the PC target (`targets/pc/`); link one of the three:

- **`lp64_low_image_dyn.c`**: a *dynamically linked*, non-PIE x86-64 Linux executable (SDL, GL drivers and sound
  servers are loaded at run time, which a static one can't do). Every free page from 64 KiB to 4 GB is reserved from
  `.preinit_array`, which the dynamic loader runs before any shared library's constructor, so glibc's own malloc, the
  libraries and every thread stack land above 4 GB. No `main()` and no malloc family: the game keeps its own and runs
  on a `.bss` stack itself (`pthread_attr_setstack`). `LP64_LOW_TRAP=0` skips the reservation.
  **Test:** `test/low_image_dyn_test.c` (`targets/pc/test/desktop_runtime_tests.sh` runs it, and its mutant).
- **`lp64_low_image_win.c`**: 64-bit Windows, linked with a fixed base of 0x400000, no ASLR and not
  large-address-aware (the flags are in its comment): Windows keeps the whole process below 2 GB, so nothing needs
  reserving. `lp64_win_low_probe` says where a heap block, a fresh mapping and the stack land (`GetSystemInfo`'s
  maximum address is the system's, 128 TB, whatever the flag). Wine doesn't apply the limit to 64-bit programs.
  **Test:** `test/low_image_win_test.c`, run on Windows (CI's runner).

From spyro-vita `src/platform/pc/low_linux.c` and `low_win.c` (its D240, D241).

**Known:** `test/low_image_test.c` (the static variant) fails its "free unmaps the block" check when run natively
on x86-64 (it passes under qemu-x86_64, where it has always run); not looked into yet.

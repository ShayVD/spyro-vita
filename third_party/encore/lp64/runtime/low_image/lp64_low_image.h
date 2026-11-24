/* lp64_low_image: the low image of a 4-byte-pointer game built for 64-bit Linux (x86-64; lp64/docs/low-image.md).
 *
 * Linking lp64_low_image.c gives the executable its main(), its malloc family and a constructor:
 *  - main() checks that the image (static, non-PIE at 0x400000), the game's stack and anything the game reports
 *    (lp64_game_memory_end) lie below 2 GB, refuses to run otherwise (exit 1), and runs lp64_game_main on a thread
 *    whose stack is a .bss array, so the game's locals are low too;
 *  - malloc, calloc, realloc, free, memalign, aligned_alloc, posix_memalign, valloc, pvalloc and
 *    malloc_usable_size map one block each with mmap, which the reservation puts above 4 GB; free unmaps it;
 *  - a constructor reserves every free page below 4 GB (PROT_NONE), so no later mapping lands there and a host
 *    pointer cut to 32 bits points at nothing and crashes, instead of aliasing the game's memory.
 *
 * LP64_LOW_IMAGE_REPORT=1 in the environment prints where each piece lies. Log lines start "lp64-low-image: ".
 *
 * From spyro-vita src/platform/linux32/low_image.c @ c25a3ea (MIT; its decisions D173, D174). */
#ifndef LP64_LOW_IMAGE_H
#define LP64_LOW_IMAGE_H

#include <stddef.h>

#define LP64_LOW_LIMIT 0x80000000ull   /* the game's addresses: below 2 GB ((int) casts sign-extend) */
#define LP64_TRAP_LIMIT 0x100000000ull /* nothing of the host's below 4 GB */
#ifndef LP64_GAME_STACK
#define LP64_GAME_STACK (8u << 20) /* the game thread's stack in .bss: the main thread's usual 8 MB */
#endif

/* The game's own main (the game defines it; e.g. its main.c built with -Dmain=lp64_game_main). Runs on the game
 * thread, on the low stack; its return value is the process's. */
int lp64_game_main(int argc, char **argv);

/* Optional (a weak default returns NULL): the end of the highest memory the game addresses that is not in the
 * executable's image, if any. main() refuses to run when it is above 2 GB. A game whose arena is a .bss array needs
 * none. */
const void *lp64_game_memory_end(void);

/* 1 when [p, p + n) lies below 2 GB */
int lp64_is_low(const void *p, size_t n);
/* 1 when p is on the game thread's stack */
int lp64_on_game_stack(const void *p);
/* pages the constructor reserved below 4 GB (0 on Windows) */
int lp64_reserved_pages(void);

/* The variants (each its own .c; link one): lp64_low_image.c (static Linux: main, malloc, the reservation; all of the
 * above), lp64_low_image_dyn.c (dynamic Linux: the reservation from .preinit_array; lp64_is_low and
 * lp64_reserved_pages only: the game keeps its main and glibc's malloc), lp64_low_image_win.c (64-bit Windows, not
 * large-address-aware: lp64_is_low, lp64_reserved_pages and the probe below). */
#if defined(_WIN32)
/* 1 when a heap block, a fresh mapping and the caller's stack all lie below 2 GB; report (may be NULL) gets a line
 * naming them */
int lp64_win_low_probe(char *report, size_t cap);
#endif

#endif

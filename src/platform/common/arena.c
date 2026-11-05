/* The PS1 RAM arena (docs/porting-plan.md §4.3): one 2 MB block standing in
 * for PS1 addresses 0x80000000-0x801FFFFF, at a 16 MB-aligned host address
 * with bit 31 set so 24-bit OT links and 21-bit packed pointers rebuild as
 * base | low bits. On linux32 it sits at 0x80000000 itself, or with
 * SPYRO_ARENA_AT at another such address, as on the Vita (decisions D97).
 * The native build (SPYRO_NATIVE_MEM, D100) decodes them relative to the
 * base instead (src/game/psx_mem.h), so its arena may be anywhere; since
 * D103 it is only overlay space (psx_arena.h), from the heap, or on linux32
 * with SPYRO_ARENA_AT at that page, for tests. */
#include "psx_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uintptr_t g_psx_arena_base;
#if SPYRO_NATIVE_MEM
/* the game's scratchpad (the arena's first KB elsewhere), aligned as the
 * PS1's (D103) */
unsigned char g_psx_scratch[1024] __attribute__((aligned(1024)));
#else
unsigned char g_psx_scratch[1024];
#endif
/* PS1 kernel RAM (0x00000000-0x0000FFFF) for the game's stray stores through
 * small integers (PSX_KUSEG, src/compat/port_prefix.h). The arena's own first
 * KB is the scratchpad, so they can't go there. */
unsigned char g_psx_kernel_ram[0x10000];

#if defined(__linux__)
#include <sys/mman.h>
#if !defined(PSX_ARENA_AT) && !SPYRO_NATIVE_MEM
#define PSX_ARENA_AT PSX_RAM_BASE
#endif
#endif
#if !defined(__linux__) || SPYRO_NATIVE_MEM
#include <malloc.h>
#endif

#if defined(__linux__)
static void *map_at(uint32_t at, int prot) {
  void *p = mmap((void *)(uintptr_t)at, PSX_MEM_SIZE, prot,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == MAP_FAILED || (uintptr_t)p != at) {
    fprintf(stderr, "psx_arena_init: mmap at 0x%08x failed\n", (unsigned)at);
    exit(1);
  }
  return p;
}
#endif

void psx_arena_init(void) {
  void *p;
#if SPYRO_NATIVE_MEM && !(defined(__linux__) && defined(PSX_ARENA_AT))
  p = memalign(0x1000, PSX_MEM_SIZE);
#elif defined(__linux__)
  p = map_at(PSX_ARENA_AT, PROT_READ | PROT_WRITE);
#if SPYRO_ARENA_MOVED && !SPYRO_NATIVE_MEM
  /* The moved arena's aliases of PS1 RAM (mips2c_rt.h) must not reach other
   * memory: the Vita has none there (D25); here nothing may be mapped there.
   * Below 2 MB nothing can be (mmap_min_addr, and the binary is above). */
  map_at(PSX_RAM_BASE, PROT_NONE);
  map_at(PSX_ARENA_AT & 0x7FFFFFFFu, PROT_NONE);
#endif
#else
  p = memalign(0x1000000, PSX_RAM_SIZE);
#endif
#if SPYRO_NATIVE_MEM
  if (!p) {
    fprintf(stderr, "psx_arena_init: no memory for the arena\n");
    exit(1);
  }
#else
  if (((uintptr_t)p & 0xFFFFFF) != 0 || !((uintptr_t)p & 0x80000000u)) {
    fprintf(stderr, "psx_arena_init: arena %p not 16 MB aligned above 2 GB\n", p);
    exit(1);
  }
#endif
  memset(p, 0, PSX_MEM_SIZE);
  g_psx_arena_base = (uintptr_t)p;
}


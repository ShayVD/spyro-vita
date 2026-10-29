/* The PS1 RAM arena (docs/porting-plan.md §4.3): one 2 MB block standing in
 * for PS1 addresses 0x80000000-0x801FFFFF, at a 16 MB-aligned host address
 * with bit 31 set so 24-bit OT links and 21-bit packed pointers rebuild as
 * base | low bits. On linux32 it sits at 0x80000000 itself. */
#include "psx_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uintptr_t g_psx_arena_base;
unsigned char g_psx_scratch[1024];

#if defined(__linux__)
#include <sys/mman.h>
#else
#include <malloc.h>
#endif

void psx_arena_init(void) {
  void *p;
#if defined(__linux__)
  p = mmap((void *)PSX_RAM_BASE, PSX_RAM_SIZE, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == MAP_FAILED || (uintptr_t)p != PSX_RAM_BASE) {
    fprintf(stderr, "psx_arena_init: mmap at 0x%08x failed\n", PSX_RAM_BASE);
    exit(1);
  }
#else
  p = memalign(0x1000000, PSX_RAM_SIZE);
#endif
  if (((uintptr_t)p & 0xFFFFFF) != 0 || !((uintptr_t)p & 0x80000000u)) {
    fprintf(stderr, "psx_arena_init: arena %p not 16 MB aligned above 2 GB\n", p);
    exit(1);
  }
  memset(p, 0, PSX_RAM_SIZE);
  g_psx_arena_base = (uintptr_t)p;
}

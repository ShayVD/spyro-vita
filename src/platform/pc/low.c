/* The low image on PC (docs/pc-build.md, D240, D241, D242): Encore's lp64 low image, vendored
 * (third_party/encore/lp64/runtime/low_image):
 *  - Linux: lp64_low_image_dyn.c reserves every free page below 4 GB from .preinit_array, before any shared
 *    library runs, so glibc's malloc, SDL and the drivers land above 4 GB and a host pointer cut to 32 bits
 *    crashes. LP64_LOW_TRAP=0 in the environment skips it.
 *  - Windows: the executable is not large-address-aware (CMakeLists.txt), so the whole process lies below 2 GB;
 *    lp64_low_image_win.c's probe says where memory lands. The disc's binary reads and pread come from
 *    third_party/encore/targets/pc/runtime/winposix.
 *  - macOS (D250 O4): the offset model (src/compat/p32.h): the game's memory lies within 4 GB above the
 *    executable's Mach-O header, and __PAGEZERO covers the low 4 GB, so a pointer cut to 32 bits crashes there
 *    too. src/platform/common/p32_image.c checks the image and stores the data image's offsets.
 *  - Linux on ARM (D250 O5): the offset model too, the image non-PIE above 4 GB as linuxa64's (CMakeLists.txt),
 *    the base its start. Every free page from 64 KiB to 4 GB is reserved here as Encore's lp64_low_image_dyn.c
 *    does on x86-64 (which refuses any other architecture), from .preinit_array, so a pointer cut to 32 bits
 *    points at nothing. LP64_LOW_TRAP=0 skips it.
 * main.c checks the image, the arena and the game stack against 2 GB (4 GB above the base in the offset model);
 * this file reports where things are. */
#include "pc.h"

#include <stdint.h>
#include <stdio.h>

extern const void *psx_low_arena_end(void);
static char s_report[320];

#if defined(__APPLE__)
#include "p32_image.h"

int pc_low_reserved_pages(void) { return 0; /* __PAGEZERO */ }

const char *pc_low_report(void) {
  snprintf(s_report, sizeof s_report, "image %p-%p, arena end %p", (void *)spyro_p32_base,
           (void *)p32_image_end(), psx_low_arena_end());
  return s_report;
}
#elif defined(__aarch64__) && defined(__linux__)
#include "p32_image.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#define TRAP_LIMIT 0x100000000ull /* nothing of the host's below 4 GB */

static int s_reserved_pages, s_budget = 4096;
static uint64_t s_page = 4096;
static char s_maps[65536];

int pc_low_reserved_pages(void) { return s_reserved_pages; }

/* [start, end) PROT_NONE; a mapping placed elsewhere is undone and the range split */
static void reserve(uint64_t start, uint64_t end) {
  void *p;
  if (end <= start || s_budget-- <= 0)
    return;
  p = mmap((void *)(uintptr_t)start, end - start, PROT_NONE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == (void *)(uintptr_t)start) {
    s_reserved_pages += (int)((end - start) / s_page);
    return;
  }
  if (p != MAP_FAILED)
    munmap(p, end - start);
  if (end - start <= s_page)
    return;
  {
    uint64_t mid = start + (((end - start) / 2) & ~(s_page - 1));
    reserve(start, mid);
    reserve(mid, end);
  }
}

/* every gap below 4 GB in /proc/self/maps, before any shared library's constructor (read without stdio or
 * malloc). The kernel's page may be 4, 16 or 64 KB on aarch64. */
static void low_trap(int argc, char **argv, char **envp) {
  uint64_t prev, lo[512], hi[512];
  int n = 0, fd, len = 0, r, i;
  char *l;
  (void)argc;
  (void)argv;
  for (char **e = envp; e && *e; e++)
    if (!strcmp(*e, "LP64_LOW_TRAP=0"))
      return;
  s_page = (uint64_t)getpagesize();
  prev = (0x10000 + s_page - 1) & ~(s_page - 1); /* vm.mmap_min_addr */
  if ((fd = open("/proc/self/maps", O_RDONLY)) < 0)
    return;
  while (len < (int)sizeof s_maps - 1 && (r = (int)read(fd, s_maps + len, sizeof s_maps - 1 - len)) > 0)
    len += r;
  close(fd);
  s_maps[len] = 0;
  for (l = s_maps; *l && n < 512;) {
    lo[n] = strtoull(l, &l, 16);
    hi[n] = strtoull(l + 1, &l, 16);
    n++;
    l = strchr(l, '\n');
    if (!l)
      break;
    l++;
  }
  for (i = 0; i <= n; i++) {
    uint64_t start = prev, end = i < n ? lo[i] : TRAP_LIMIT;
    if (end > TRAP_LIMIT)
      end = TRAP_LIMIT;
    reserve(start, end);
    if (i < n && hi[i] > prev)
      prev = hi[i];
    if (prev >= TRAP_LIMIT)
      break;
  }
}

__attribute__((section(".preinit_array"), used)) static void (*const s_preinit)(int, char **, char **) = low_trap;

const char *pc_low_report(void) {
  snprintf(s_report, sizeof s_report, "image %p-%p, arena end %p", (void *)spyro_p32_base,
           (void *)p32_image_end(), psx_low_arena_end());
  return s_report;
}
#else
#include "lp64_low_image.h"

int pc_low_reserved_pages(void) { return lp64_reserved_pages(); }

#if defined(_WIN32)
/* ELF's linker symbols, which savestate.c stamps a save state's layout with (the build's own identity): a PE has
 * none, so marks in the image */
char etext[1], _edata[1], _end[1];

const char *pc_low_report(void) {
  char probe[256];
  lp64_win_low_probe(probe, sizeof probe);
  snprintf(s_report, sizeof s_report, "arena end %p; %s", psx_low_arena_end(), probe);
  return s_report;
}
#else
extern char __executable_start[], _end[];

const char *pc_low_report(void) {
  snprintf(s_report, sizeof s_report, "image %p-%p, arena end %p", (void *)__executable_start, (void *)_end,
           psx_low_arena_end());
  return s_report;
}
#endif
#endif /* !__APPLE__ && !(__aarch64__ && __linux__) */

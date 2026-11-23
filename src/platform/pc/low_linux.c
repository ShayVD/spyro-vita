/* The low image on desktop Linux (docs/pc-build.md, D240): linux64's model
 * (src/platform/linux32/low_image.c, D174) for a dynamically linked
 * executable, which SDL needs (it loads X11, Wayland, GL and the sound
 * servers' libraries at run time).
 *
 * The executable is non-PIE at 0x400000, so the image, the arena and the
 * game thread's stack (.bss arrays) lie below 2 GB; main.c checks. Nothing
 * of the host's may lie below 4 GB, so a host pointer that reaches a P32
 * field or a u32 by mistake crashes instead of aliasing: every free page
 * from 64 KiB to 4 GB is reserved PROT_NONE from .preinit_array, which the
 * dynamic loader runs before any shared library's constructor. glibc's own
 * malloc then works as it is (linux64 needs its own, one mmap per block:
 * static glibc's sbrk heap starts before any constructor). Its sbrk heap,
 * right after the image, can't grow into the reservation, so it takes
 * mmap'd arenas, above 4 GB. Shared libraries, thread stacks and every later
 * mapping land above 4 GB too.
 *
 * SPYRO_LOW_TRAP=0 in the environment skips the reservation (a driver that
 * insists on memory below 4 GB: the game still runs, without the trap). */
#if defined(__linux__)
#define _GNU_SOURCE 1
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define TRAP_LIMIT 0x100000000ull

extern char __executable_start[], _end[];
extern const void *psx_low_arena_end(void);

static int s_reserved_pages;
static int s_budget = 4096;
static char s_maps[65536];
static char s_report[256];

int pc_low_reserved_pages(void) { return s_reserved_pages; }

const char *pc_low_report(void) {
  snprintf(s_report, sizeof s_report, "image %p-%p, arena end %p", (void *)__executable_start,
           (void *)_end, psx_low_arena_end());
  return s_report;
}

/* [start, end) PROT_NONE; a mapping placed elsewhere is undone and the range
 * split (the kit's lp64 low_image: qemu-x86_64 takes MAP_FIXED_NOREPLACE as a
 * hint for some large ranges) */
static void reserve(uint64_t start, uint64_t end) {
  void *p;
  if (end <= start || s_budget-- <= 0)
    return;
  p = mmap((void *)(uintptr_t)start, end - start, PROT_NONE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == (void *)(uintptr_t)start) {
    s_reserved_pages += (int)((end - start) >> 12);
    return;
  }
  if (p != MAP_FAILED)
    munmap(p, end - start);
  if (end - start <= 4096)
    return;
  {
    uint64_t mid = start + (((end - start) / 2) & ~4095ull);
    reserve(start, mid);
    reserve(mid, end);
  }
}

/* every gap below 4 GB in /proc/self/maps (read without stdio or malloc) */
static void low_trap(int argc, char **argv, char **envp) {
  uint64_t prev = 0x10000; /* vm.mmap_min_addr */
  uint64_t lo[512], hi[512];
  int n = 0, fd, len = 0, r;
  (void)argc;
  (void)argv;
  for (char **e = envp; e && *e; e++)
    if (!strcmp(*e, "SPYRO_LOW_TRAP=0"))
      return;
  if ((fd = open("/proc/self/maps", O_RDONLY)) < 0)
    return;
  while (len < (int)sizeof s_maps - 1 && (r = (int)read(fd, s_maps + len, sizeof s_maps - 1 - len)) > 0)
    len += r;
  close(fd);
  s_maps[len] = 0;
  for (char *l = s_maps; *l && n < 512;) {
    lo[n] = strtoull(l, &l, 16);
    hi[n] = strtoull(l + 1, &l, 16);
    n++;
    l = strchr(l, '\n');
    if (!l)
      break;
    l++;
  }
  for (int i = 0; i <= n; i++) {
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

__attribute__((section(".preinit_array"), used)) static void (*const s_preinit)(int, char **, char **) =
    low_trap;
#endif

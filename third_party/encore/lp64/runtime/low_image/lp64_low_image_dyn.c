/* lp64_low_image_dyn: the low image for a dynamically linked x86-64 Linux executable (lp64/docs/low-image.md), the
 * variant a desktop build needs: SDL, GL drivers and the sound servers' libraries are loaded at run time, which a
 * static executable (lp64_low_image.c) can't do.
 *
 * Link this file (not lp64_low_image.c) into a non-PIE executable (objects -fno-pic, linked -no-pie: the image at
 * 0x400000, below 2 GB) and every free page from 64 KiB to 4 GB is reserved PROT_NONE from .preinit_array, which the
 * dynamic loader runs before any shared library's constructor. Nothing of the host's then lands below 4 GB: glibc's
 * own malloc (its sbrk heap, right after the image, can't grow into the reservation, so it takes mmap'd arenas),
 * shared libraries, every thread stack and later mapping. A host pointer cut to 32 bits points at nothing and crashes
 * instead of aliasing the game's memory. Unlike the static variant it gives no main() and no malloc family: the game
 * keeps its own main and runs its code on a thread whose stack is a .bss array itself (the game's locals must be low;
 * pthread_attr_setstack), and lp64_is_low checks what it must.
 *
 * LP64_LOW_TRAP=0 in the environment skips the reservation (a driver that insists on memory below 4 GB: the game
 * still runs, without the trap). LP64_LOW_IMAGE_MUTANT=1 (tests only) skips it too.
 *
 * aarch64 Linux too, for the offset model's dynamic executable (lp64/docs/low-image.md, "The offset model"): there the
 * image lies above 4 GB (lp64/cmake/linuxa64.toolchain.cmake's link), lp64_is_low means nothing and
 * lp64_p32_image_start is the check, but the reservation is the same trap for a pointer cut to 32 bits instead of
 * encoded. A kernel's page there may be 4, 16 or 64 KB: the reservation takes getpagesize()'s.
 *
 * Linux only (/proc/self/maps, MAP_FIXED_NOREPLACE). From spyro-vita src/platform/pc/low_linux.c (MIT; its D240), with
 * the kit's split of a misplaced reservation (lp64_low_image.c); aarch64 from its src/platform/pc/low.c @ 36d9e07
 * (D266). */
#if !(defined(__x86_64__) || defined(__aarch64__)) || !defined(__linux__)
#error "lp64_low_image_dyn.c is for x86-64 or aarch64 Linux"
#endif
#define _GNU_SOURCE 1
#include "lp64_low_image.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static int s_reserved_pages;
static int s_budget = 4096;
static uint64_t s_page = 4096; /* the kernel's page (getpagesize(); 16 or 64 KB on some aarch64 kernels) */
static char s_maps[65536];

int lp64_is_low(const void *p, size_t n) { return (uintptr_t)p + n <= LP64_LOW_LIMIT; }
int lp64_reserved_pages(void) { return s_reserved_pages; }

/* [start, end) PROT_NONE; a mapping placed elsewhere is undone and the range split (qemu-x86_64 takes
 * MAP_FIXED_NOREPLACE as a hint for some large ranges) */
static void reserve(uint64_t start, uint64_t end) {
  void *p;
  if (end <= start || s_budget-- <= 0) return;
  p = mmap((void *)(uintptr_t)start, end - start, PROT_NONE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == (void *)(uintptr_t)start) {
    s_reserved_pages += (int)((end - start) / s_page);
    return;
  }
  if (p != MAP_FAILED) munmap(p, end - start);
  if (end - start <= s_page) return;
  {
    uint64_t mid = start + (((end - start) / 2) & ~(s_page - 1));
    reserve(start, mid);
    reserve(mid, end);
  }
}

/* every gap below 4 GB in /proc/self/maps (read without stdio or malloc: nothing of libc's is set up yet) */
static void low_trap(int argc, char **argv, char **envp) {
  uint64_t prev, lo[512], hi[512];
  int n = 0, fd, len = 0, r, i;
  char *l;
  (void)argc;
  (void)argv;
  for (char **e = envp; e && *e; e++)
    if (!strcmp(*e, "LP64_LOW_TRAP=0") || !strcmp(*e, "LP64_LOW_IMAGE_MUTANT=1")) return;
  s_page = (uint64_t)getpagesize();
  prev = (0x10000 + s_page - 1) & ~(s_page - 1); /* vm.mmap_min_addr, rounded to a page */
  if ((fd = open("/proc/self/maps", O_RDONLY)) < 0) return;
  while (len < (int)sizeof s_maps - 1 && (r = (int)read(fd, s_maps + len, sizeof s_maps - 1 - len)) > 0) len += r;
  close(fd);
  s_maps[len] = 0;
  for (l = s_maps; *l && n < 512;) {
    lo[n] = strtoull(l, &l, 16);
    hi[n] = strtoull(l + 1, &l, 16);
    n++;
    l = strchr(l, '\n');
    if (!l) break;
    l++;
  }
  for (i = 0; i <= n; i++) {
    uint64_t start = prev, end = i < n ? lo[i] : LP64_TRAP_LIMIT;
    if (end > LP64_TRAP_LIMIT) end = LP64_TRAP_LIMIT;
    reserve(start, end);
    if (i < n && hi[i] > prev) prev = hi[i];
    if (prev >= LP64_TRAP_LIMIT) break;
  }
}

__attribute__((section(".preinit_array"), used)) static void (*const s_preinit)(int, char **, char **) = low_trap;

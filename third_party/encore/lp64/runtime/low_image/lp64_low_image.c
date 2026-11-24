/* lp64_low_image: the low image of a 4-byte-pointer game on 64-bit Linux (lp64_low_image.h, lp64/docs/low-image.md).
 *
 * Everything the game can point at must lie below 2 GB: its pointer fields are 32 bits (lp64_p32.h), hand-written C
 * keeps addresses in 32-bit integers, and a decomp's (int) casts sign-extend when they turn back into pointers. The
 * executable is static and non-PIE at 0x400000; the game's arena is a .bss array; the game runs on a thread whose
 * stack is a .bss array (below), since game code takes the addresses of locals. main() checks all of it before
 * anything else and refuses to run otherwise.
 *
 * So that a host pointer that reaches a P32 field or a 32-bit integer by mistake crashes instead of aliasing low
 * memory, nothing of the host's may be below 4 GB: a truncated pointer must point at nothing.
 *  - malloc and its family are this file's: one mmap per block, unmapped again by free (so a use after free crashes
 *    too). glibc's own malloc can't do it: static glibc starts its heap with sbrk, right after the image, before any
 *    constructor runs, and M_MMAP_THRESHOLD 0 still serves small blocks from that heap's top (measured in spyro-vita:
 *    0x2254230 under qemu-x86_64).
 *  - a constructor reserves every unmapped page below 4 GB (PROT_NONE), so no later mapping (these blocks, thread
 *    stacks, anything) lands there.
 *
 * Linux x86-64 only (/proc/self/maps, MAP_FIXED_NOREPLACE). A console target gets its own version once its loader's
 * addresses are known on hardware. LP64_LOW_IMAGE_MUTANT=1 (tests only) skips the reservation.
 *
 * From spyro-vita src/platform/linux32/low_image.c @ c25a3ea (MIT; its decisions D173, D174). Its spyro_host_main
 * and psx_low_arena_end are the hooks lp64_game_main and lp64_game_memory_end here. */
#if !defined(__x86_64__) || !defined(__linux__)
#error "lp64_low_image.c is for x86-64 Linux"
#endif
#define _GNU_SOURCE 1
#include "lp64_low_image.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

extern char __executable_start[], _end[];

static unsigned char s_game_stack[LP64_GAME_STACK] __attribute__((aligned(4096)));
static int s_reserved_pages;

__attribute__((weak)) const void *lp64_game_memory_end(void) { return NULL; }

int lp64_is_low(const void *p, size_t n) { return (uintptr_t)p + n <= LP64_LOW_LIMIT; }
int lp64_on_game_stack(const void *p) {
  return (const unsigned char *)p >= s_game_stack && (const unsigned char *)p < s_game_stack + LP64_GAME_STACK;
}
int lp64_reserved_pages(void) { return s_reserved_pages; }

/* The allocator: a block is its own mapping; the 16 bytes before it hold the mapping's start and length. */
#define HDR 16u
static void *low_alloc(size_t n, size_t align) {
  const size_t page = 4096;
  size_t len;
  uintptr_t base, user;
  void *m;
  if (align < HDR)
    align = HDR;
  if (n > SIZE_MAX / 4 || align > SIZE_MAX / 4 || (align & (align - 1))) {
    errno = ENOMEM;
    return NULL;
  }
  len = (n + align + HDR + page - 1) & ~(page - 1);
  m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (m == MAP_FAILED) {
    errno = ENOMEM;
    return NULL;
  }
  base = (uintptr_t)m;
  user = (base + HDR + align - 1) & ~(uintptr_t)(align - 1);
  ((size_t *)user)[-2] = base;
  ((size_t *)user)[-1] = len;
  return (void *)user;
}
static size_t low_usable(void *p) { return ((size_t *)p)[-1] - ((uintptr_t)p - ((size_t *)p)[-2]); }
void *malloc(size_t n) { return low_alloc(n, HDR); }
void free(void *p) {
  if (p)
    munmap((void *)((size_t *)p)[-2], ((size_t *)p)[-1]);
}
void *calloc(size_t a, size_t b) { /* fresh mappings are zero */
  if (b && a > SIZE_MAX / b) {
    errno = ENOMEM;
    return NULL;
  }
  return low_alloc(a * b, HDR);
}
void *realloc(void *p, size_t n) {
  void *q;
  size_t u;
  if (!p)
    return malloc(n);
  if (!n) {
    free(p);
    return NULL;
  }
  if (n <= (u = low_usable(p)))
    return p;
  if (!(q = malloc(n)))
    return NULL;
  memcpy(q, p, u);
  free(p);
  return q;
}
void *memalign(size_t a, size_t n) { return low_alloc(n, a); }
void *aligned_alloc(size_t a, size_t n) { return low_alloc(n, a); }
int posix_memalign(void **r, size_t a, size_t n) {
  void *p = low_alloc(n, a);
  if (!p)
    return ENOMEM;
  *r = p;
  return 0;
}
void *valloc(size_t n) { return low_alloc(n, 4096); }
void *pvalloc(size_t n) { return low_alloc((n + 4095) & ~(size_t)4095, 4096); }
size_t malloc_usable_size(void *p) { return p ? low_usable(p) : 0; }

/* [start, end) mapped PROT_NONE. A mapping that lands anywhere else is undone and the range split in two: qemu-x86_64
 * 7.2 treats MAP_FIXED_NOREPLACE as a hint for some large ranges and maps them high (0x10000-0x400000 under a static
 * image at 0x400000), where the kernel would refuse. s_budget bounds the calls if a whole range keeps failing. */
static int s_budget = 4096;
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
  if (end - start > 4096) {
    uint64_t mid = start + (((end - start) / 2) & ~(uint64_t)4095);
    reserve(start, mid);
    reserve(mid, end);
  }
}

/* every gap below 4 GB in /proc/self/maps, mapped PROT_NONE (read without stdio, which would malloc before the
 * reservation) */
static char s_maps[65536];

__attribute__((constructor)) static void low_image_trap(void) {
  uint64_t prev = 0x10000; /* vm.mmap_min_addr */
  /* read it all first: our own mappings would change it under us */
  uint64_t lo[256], hi[256];
  int n = 0, fd, len = 0, r;
#if LP64_LOW_IMAGE_MUTANT
  return;
#endif
  fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0)
    return;
  while (len < (int)sizeof s_maps - 1 && (r = (int)read(fd, s_maps + len, sizeof s_maps - 1 - len)) > 0)
    len += r;
  close(fd);
  s_maps[len] = 0;
  for (char *l = s_maps; *l && n < 256;) {
    lo[n] = strtoull(l, &l, 16);
    hi[n] = strtoull(l + 1, &l, 16);
    n++;
    l = strchr(l, '\n');
    if (!l)
      break;
    l++;
  }
  for (int i = 0; i <= n; i++) {
    uint64_t start = prev, end = i < n ? lo[i] : LP64_TRAP_LIMIT;
    if (end > LP64_TRAP_LIMIT)
      end = LP64_TRAP_LIMIT;
    reserve(start, end);
    if (i < n && hi[i] > prev)
      prev = hi[i];
    if (prev >= LP64_TRAP_LIMIT)
      break;
  }
}

struct game_args {
  int argc;
  char **argv;
  int rc;
};

static void *game_thread(void *arg) {
  struct game_args *g = arg;
  int here;
  if (!lp64_on_game_stack(&here)) {
    fprintf(stderr, "lp64-low-image: the game thread isn't on its low stack (%p)\n", (void *)&here);
    exit(1);
  }
  g->rc = lp64_game_main(g->argc, g->argv);
  return NULL;
}

int main(int argc, char **argv) {
  void *probe = malloc(16);
  const void *game_end = lp64_game_memory_end();
  pthread_attr_t attr;
  pthread_t th;
  struct game_args g = {argc, argv, 1};
  int e;
  if (!lp64_is_low(_end, 0) || !lp64_is_low(game_end, 0) || !lp64_is_low(s_game_stack, LP64_GAME_STACK)) {
    fprintf(stderr,
            "lp64-low-image: the game's memory must lie below 2 GB (the low image): image %p-%p, game memory end "
            "%p, game stack %p-%p\n",
            (void *)__executable_start, (void *)_end, game_end, (void *)s_game_stack,
            (void *)(s_game_stack + LP64_GAME_STACK));
    return 1;
  }
  if ((uintptr_t)probe < LP64_TRAP_LIMIT)
    fprintf(stderr,
            "lp64-low-image: warning: malloc gave %p, below 4 GB: stray host pointers would alias low memory "
            "instead of crashing\n",
            probe);
  free(probe);
  if (getenv("LP64_LOW_IMAGE_REPORT"))
    fprintf(stderr,
            "lp64-low-image: image %p-%p, game memory end %p, game stack %p-%p, %d pages below 4 GB reserved, "
            "malloc at %p\n",
            (void *)__executable_start, (void *)_end, game_end, (void *)s_game_stack,
            (void *)(s_game_stack + LP64_GAME_STACK), s_reserved_pages, probe);
  if ((e = pthread_attr_init(&attr)) || (e = pthread_attr_setstack(&attr, s_game_stack, LP64_GAME_STACK)) ||
      (e = pthread_create(&th, &attr, game_thread, &g)) || (e = pthread_join(th, NULL))) {
    fprintf(stderr, "lp64-low-image: the game thread: %s\n", strerror(e));
    return 1;
  }
  return g.rc;
}

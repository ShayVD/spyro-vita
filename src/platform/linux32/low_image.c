/* linux64's low image (docs/ps4-plan.md P1, decisions D173, D174).
 *
 * Everything the game can point at must lie below 2 GB: its pointer fields
 * are 32 bits (P32, src/compat/p32.h), the hand C keeps addresses in u32s,
 * and the decomp's (int) casts sign-extend when they turn back into
 * pointers. The executable is static and non-PIE at 0x400000; the arena is
 * a .bss array (arena.c); the game runs on a thread whose stack is a .bss
 * array (below), since the hand C takes the addresses of locals. main()
 * checks all three before anything else and refuses to run otherwise.
 *
 * So that a host pointer that reaches a P32 field or a u32 by mistake
 * crashes instead of aliasing low memory, nothing of the host's may be
 * below 4 GB: a truncated pointer must point at nothing.
 *  - malloc and its family are this file's: one mmap per block, unmapped
 *    again by free (so a use after free crashes too). glibc's own malloc
 *    can't do it: static glibc starts its heap with sbrk, after the image,
 *    before any constructor runs, and M_MMAP_THRESHOLD 0 still serves small
 *    blocks from that heap's top (measured: 0x2254230 under qemu-x86_64).
 *  - a constructor reserves every unmapped page below 4 GB (PROT_NONE), so
 *    no later mapping (these blocks, thread stacks, anything) lands there.
 *
 * main.c's main() is spyro_host_main here (src/platform/linux32/CMakeLists.txt). */
#if SPYRO_LOW_IMAGE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define LOW_LIMIT 0x80000000ull   /* the game's addresses: below 2 GB */
#define TRAP_LIMIT 0x100000000ull /* nothing else below 4 GB */
#define GAME_STACK (8u << 20)     /* the main thread's usual 8 MB */

extern char __executable_start[], _end[];
extern const void *psx_low_arena_end(void);
int spyro_host_main(int argc, char **argv);

static unsigned char s_game_stack[GAME_STACK] __attribute__((aligned(4096)));
static int s_reserved_pages;

/* The allocator: a block is its own mapping; the 16 bytes before it hold
 * the mapping's start and length. */
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
static size_t low_usable(void *p) {
  return ((size_t *)p)[-1] - ((uintptr_t)p - ((size_t *)p)[-2]);
}
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

/* every gap below 4 GB in /proc/self/maps, mapped PROT_NONE */
static char s_maps[65536];

/* (read without stdio, which would malloc before the reservation) */
__attribute__((constructor)) static void low_image_trap(void) {
  uint64_t prev = 0x10000; /* vm.mmap_min_addr */
  /* read it all first: our own mappings would change it under us */
  uint64_t lo[256], hi[256];
  int n = 0, fd = open("/proc/self/maps", O_RDONLY), len = 0, r;
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
    uint64_t start = prev, end = i < n ? lo[i] : TRAP_LIMIT;
    if (end > TRAP_LIMIT)
      end = TRAP_LIMIT;
    if (end > start) {
      void *p = mmap((void *)(uintptr_t)start, end - start, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
      if (p != MAP_FAILED)
        s_reserved_pages += (int)((end - start) >> 12);
    }
    if (i < n && hi[i] > prev)
      prev = hi[i];
    if (prev >= TRAP_LIMIT)
      break;
  }
}

static int low(const void *p, size_t len) {
  return (uintptr_t)p + len <= LOW_LIMIT;
}

struct game_args {
  int argc;
  char **argv;
  int rc;
};

static void *game_thread(void *arg) {
  struct game_args *g = arg;
  int here;
  if (!((unsigned char *)&here >= s_game_stack && (unsigned char *)&here < s_game_stack + GAME_STACK)) {
    fprintf(stderr, "spyro: the game thread isn't on its low stack (%p)\n", (void *)&here);
    exit(1);
  }
  g->rc = spyro_host_main(g->argc, g->argv);
  return NULL;
}

int main(int argc, char **argv) {
  void *probe = malloc(16);
  const void *arena_end = psx_low_arena_end();
  if (!low(_end, 0) || !low(arena_end, 0) || !low(s_game_stack, GAME_STACK)) {
    fprintf(stderr,
            "spyro: the game's memory must lie below 2 GB (the low image, D174): image "
            "%p-%p, arena end %p, game stack %p-%p\n",
            (void *)__executable_start, (void *)_end, arena_end, (void *)s_game_stack,
            (void *)(s_game_stack + GAME_STACK));
    return 1;
  }
  if ((uintptr_t)probe < TRAP_LIMIT)
    fprintf(stderr, "spyro: warning: malloc gave %p, below 4 GB: stray host pointers would "
                    "alias low memory instead of crashing\n", probe);
  free(probe);
  if (getenv("SPYRO_LOW_IMAGE_REPORT"))
    fprintf(stderr, "low image: %p-%p, arena end %p, game stack %p-%p, %d pages below 4 GB "
                    "reserved, malloc at %p\n",
            (void *)__executable_start, (void *)_end, arena_end, (void *)s_game_stack,
            (void *)(s_game_stack + GAME_STACK), s_reserved_pages, probe);
  pthread_attr_t attr;
  pthread_t th;
  struct game_args g = {argc, argv, 1};
  int e;
  if ((e = pthread_attr_init(&attr)) || (e = pthread_attr_setstack(&attr, s_game_stack, GAME_STACK)) ||
      (e = pthread_create(&th, &attr, game_thread, &g)) || (e = pthread_join(th, NULL))) {
    fprintf(stderr, "spyro: the game thread: %s\n", strerror(e));
    return 1;
  }
  return g.rc;
}
#endif

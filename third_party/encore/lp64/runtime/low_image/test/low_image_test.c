/* lp64_low_image on x86-64 Linux (static, non-PIE; under qemu-x86_64 on another machine): the game's main runs on
 * the low .bss stack; the image and a .bss arena lie below 2 GB; every malloc-family block, a fresh mapping and a new
 * thread's stack lie above 4 GB; free unmaps; every page from 64 KiB to 4 GB is mapped (the image or the PROT_NONE
 * reservation), so a host pointer cut to 32 bits lands on nothing readable and the read crashes (checked in a forked
 * child). The mutant (LP64_LOW_IMAGE_MUTANT=1: no reservation) must fail.
 * With LP64_TEST_REFUSE the game reports memory ending at 0x90000000: main() must refuse (exit 1) without running
 * the game's main.
 *   low_image_test
 * From spyro-vita src/platform/linux32/low_image.c @ c25a3ea (MIT): what P1 relied on (D174), as a test. */
#include "lp64_low_image.h"

#include <inttypes.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef LP64_TEST_REFUSE
const void *lp64_game_memory_end(void) { return (const void *)(uintptr_t)0x90000000u; }
#endif

static unsigned char s_arena[1 << 20]; /* a game's arena: a .bss array */
static int s_fails;
#define CHECK(c)                                                                                                       \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      printf("low_image_test: FAILED %s (line %d)\n", #c, __LINE__);                                                   \
      s_fails++;                                                                                                       \
    }                                                                                                                  \
  } while (0)

#define HIGH(p) ((uintptr_t)(p) >= 0x100000000ull)

/* /proc/self/maps as ranges with their permissions */
static int s_nmaps;
static struct {
  uint64_t lo, hi;
  char perm[5];
} s_maps[512];
static void read_maps(void) {
  char line[512];
  FILE *f = fopen("/proc/self/maps", "r");
  s_nmaps = 0;
  if (!f)
    return;
  while (s_nmaps < 512 && fgets(line, sizeof line, f)) {
    if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %4s", &s_maps[s_nmaps].lo, &s_maps[s_nmaps].hi,
               s_maps[s_nmaps].perm) == 3)
      s_nmaps++;
  }
  fclose(f);
}
static int map_of(uint64_t a) {
  for (int i = 0; i < s_nmaps; i++)
    if (a >= s_maps[i].lo && a < s_maps[i].hi)
      return i;
  return -1;
}

/* 1 when a page can be mapped at a (it is then unmapped again) */
static int mappable(uint64_t a) {
  void *p = mmap((void *)(uintptr_t)a, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | 0x100000, -1, 0);
  if (p == MAP_FAILED)
    return 0;
  munmap(p, 4096);
  return p == (void *)(uintptr_t)a;
}

static void *thread_local_addr(void *arg) {
  int here;
  (void)arg;
  return (void *)(uintptr_t)HIGH(&here);
}

int lp64_game_main(int argc, char **argv) {
  int here, i, gaps = 0, held = 0;
  unsigned char *p, *q;
  void *r;
  pthread_t th;
  (void)argc;
  (void)argv;
#ifdef LP64_TEST_REFUSE
  printf("low_image_test: FAILED: the game ran with its memory above 2 GB\n");
  return 0;
#endif
  /* the game's memory: low */
  CHECK(lp64_on_game_stack(&here));
  CHECK(lp64_is_low(&here, sizeof here));
  CHECK(lp64_is_low(s_arena, sizeof s_arena));
  CHECK(lp64_is_low((void *)lp64_game_main, 1));

  /* the host's memory: above 4 GB */
  p = malloc(100);
  CHECK(p && HIGH(p) && ((uintptr_t)p & 15) == 0 && malloc_usable_size(p) >= 100);
  memset(p, 0x5a, 100);
  q = realloc(p, 1 << 16);
  CHECK(q && HIGH(q) && q[0] == 0x5a && q[99] == 0x5a);
  p = calloc(1000, 8);
  CHECK(p && HIGH(p) && p[0] == 0 && p[7999] == 0);
  CHECK(posix_memalign(&r, 256, 300) == 0 && HIGH(r) && ((uintptr_t)r & 255) == 0);
  free(r);
  r = aligned_alloc(64, 128);
  CHECK(r && HIGH(r) && ((uintptr_t)r & 63) == 0);
  free(r);
  r = valloc(10);
  CHECK(r && HIGH(r) && ((uintptr_t)r & 4095) == 0);
  free(r);
  r = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(r != MAP_FAILED && HIGH(r));
  CHECK(pthread_create(&th, NULL, thread_local_addr, NULL) == 0 && pthread_join(th, &r) == 0 && r == (void *)1);
  CHECK(realloc(q, 0) == NULL);
  free(p);

  /* free unmaps the block */
  p = malloc(5000);
  read_maps();
  CHECK(map_of((uintptr_t)p) >= 0);
  free(p);
  read_maps();
  CHECK(map_of((uintptr_t)p) < 0);

  /* nothing a mapping could take from 64 KiB to 4 GB: a cut pointer lands on the image or the reservation. A gap in
   * the list whose first and last pages can't be mapped where asked is the emulator's own memory (qemu-x86_64 with
   * guest_base 0 hides its host mappings from the guest's /proc/self/maps; a guest read there crashes too): held,
   * not a hole. */
  read_maps();
  for (uint64_t a = 0x10000, end; a < 0x100000000ull; a = end) {
    i = map_of(a);
    if (i >= 0) {
      end = s_maps[i].hi;
      continue;
    }
    for (i = 0; i < s_nmaps; i++) /* the next mapping (the list is sorted) */
      if (s_maps[i].lo > a)
        break;
    end = i < s_nmaps && s_maps[i].lo < 0x100000000ull ? s_maps[i].lo : 0x100000000ull;
    if (mappable(a) || mappable(end - 4096)) {
      if (gaps++ < 3)
        printf("low_image_test: unreserved below 4 GB at 0x%" PRIx64 "-0x%" PRIx64 "\n", a, end);
    } else
      held++;
  }
  if (held)
    printf("low_image_test: %d gaps below 4 GB held by the emulator (not mappable)\n", held);
  CHECK(gaps == 0);
  CHECK(lp64_reserved_pages() > 0);

  /* a host pointer cut to 32 bits: the read crashes */
  for (i = 0; i < 64; i++) {
    p = malloc(16);
    q = (unsigned char *)(uintptr_t)(uint32_t)(uintptr_t)p;
    if (map_of((uintptr_t)q) >= 0 && strcmp(s_maps[map_of((uintptr_t)q)].perm, "---p") == 0)
      break;
  }
  CHECK(i < 64);
  if (i < 64) {
    pid_t pid = fork();
    int st = 0;
    if (pid == 0) {
      volatile unsigned char c = *(volatile unsigned char *)q;
      (void)c;
      _exit(0);
    }
    CHECK(pid > 0 && waitpid(pid, &st, 0) == pid && WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);
  }

  if (s_fails) {
    printf("low_image_test: %d failed\n", s_fails);
    return 1;
  }
  printf("low_image_test: ok (game stack and .bss below 2 GB, host blocks above 4 GB, %d pages reserved, a cut "
         "pointer crashes)\n",
         lp64_reserved_pages());
  return 0;
}

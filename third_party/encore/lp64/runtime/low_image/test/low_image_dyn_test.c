/* lp64_low_image_dyn on x86-64 Linux (dynamic, non-PIE): the image and a .bss arena lie below 2 GB; a thread on a
 * .bss stack has its locals there; glibc's malloc (small and large blocks), a fresh mapping, a default thread's stack
 * and a shared library's code (libc's) lie above 4 GB; pages below 4 GB are reserved. The mutant
 * (LP64_LOW_IMAGE_MUTANT=1 in the environment: no reservation) must fail.
 *   low_image_dyn_test      (targets/pc/test/desktop_runtime_tests.sh builds and runs it, and its mutant) */
#include "lp64_low_image.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define HIGH(p) ((uintptr_t)(p) >= LP64_TRAP_LIMIT)
static unsigned char s_arena[1 << 20];
static unsigned char s_stack[1 << 20] __attribute__((aligned(4096)));
static int s_fails;
#define CHECK(c)                                                                                                       \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      printf("low_image_dyn_test: FAILED %s (line %d)\n", #c, __LINE__);                                               \
      s_fails++;                                                                                                       \
    }                                                                                                                  \
  } while (0)

static void *on_low(void *arg) {
  int here = 0;
  *(int *)arg = lp64_is_low(&here, sizeof here);
  return NULL;
}
static void *on_default(void *arg) {
  int here = 0;
  *(int *)arg = HIGH(&here);
  return NULL;
}

int main(void) {
  void *small = malloc(24), *big = malloc(4 << 20);
  void *map = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  pthread_t t;
  pthread_attr_t at;
  int low = 0, high = 0;
  CHECK(lp64_is_low((void *)&main, 0) && lp64_is_low(s_arena, sizeof s_arena));
  CHECK(lp64_reserved_pages() > 0);
  CHECK(HIGH(small) && HIGH(big));
  CHECK(HIGH(map));
  CHECK(HIGH((void *)stdout)); /* libc's own data (in a non-PIE program &printf is its PLT stub, low) */
  pthread_attr_init(&at);
  pthread_attr_setstack(&at, s_stack, sizeof s_stack);
  CHECK(pthread_create(&t, &at, on_low, &low) == 0 && pthread_join(t, NULL) == 0 && low);
  CHECK(pthread_create(&t, NULL, on_default, &high) == 0 && pthread_join(t, NULL) == 0 && high);
  printf("low_image_dyn_test: image %p, malloc %p and %p, mmap %p, %d pages reserved: %s\n", (void *)&main, small,
         big, map, lp64_reserved_pages(), s_fails ? "FAILED" : "ok");
  free(small);
  free(big);
  return s_fails ? 1 : 0;
}

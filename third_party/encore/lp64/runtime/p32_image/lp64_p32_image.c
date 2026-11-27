/* lp64_p32_image: the offset model's start-up (lp64/runtime/p32_image/README.md; lp64/docs/low-image.md, "The offset
 * model"). A P32 slot is a 32-bit offset from lp64_p32_base, the image's start, so everything the game points at must
 * lie within 4 GB above it; and the image lies above 4 GB (macOS's __PAGEZERO; on Linux a link above 4 GB,
 * lp64/cmake/linuxa64.toolchain.cmake), so that a pointer cut to 32 bits instead of encoded points at nothing.
 * Linux (ELF, static or dynamic) and macOS (Mach-O). Empty unless LP64_P32_OFFSET.
 *
 * From spyro-vita src/platform/common/p32_image.c @ 36d9e07 (MIT; its D262, D265). Its s1_pie_words table is the
 * `words` argument; its etext/_edata/_end marks on macOS (the game's save-state stamp) stay the game's. */
#include "lp64_p32_image.h"

#if LP64_P32_OFFSET
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

/* the executable's segments (but __PAGEZERO and __LINKEDIT), slid */
char *lp64_p32_image_end(void) {
  const struct mach_header_64 *h = (const void *)lp64_p32_base;
  const struct load_command *lc = (const void *)(h + 1);
  uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0), end = 0;
  for (uint32_t i = 0; i < h->ncmds; i++, lc = (const void *)((const char *)lc + lc->cmdsize)) {
    const struct segment_command_64 *sg = (const void *)lc;
    if (lc->cmd == LC_SEGMENT_64 && sg->vmsize && strcmp(sg->segname, "__PAGEZERO") &&
        strcmp(sg->segname, "__LINKEDIT") && sg->vmaddr + sg->vmsize + slide > end)
      end = sg->vmaddr + sg->vmsize + slide;
  }
  return (char *)end;
}
#else
extern char _end[];
char *lp64_p32_image_end(void) { return _end; }
#endif

int lp64_p32_within(const void *p, size_t len) {
  return (uintptr_t)p >= (uintptr_t)lp64_p32_base && (uintptr_t)p + len - (uintptr_t)lp64_p32_base <= 0x100000000ull;
}

int lp64_p32_image_start(const uint64_t *words, char *why, size_t n) {
  char *end = lp64_p32_image_end();
  int count = 0;
  if ((uintptr_t)lp64_p32_base < 0x100000000ull || !lp64_p32_within(end, 0)) {
    snprintf(why, n, "the image %p-%p must lie above 4 GB, within 4 GB of its start", (void *)lp64_p32_base,
             (void *)end);
    return -1;
  }
  for (const uint64_t *w = words; w && w[0]; w += 2, count++) {
    if (!lp64_p32_within((const void *)(uintptr_t)w[1], 0) || !lp64_p32_within((const void *)(uintptr_t)w[0], 4)) {
      snprintf(why, n, "data word %d: slot 0x%llx, target 0x%llx, outside the image", count,
               (unsigned long long)w[0], (unsigned long long)w[1]);
      return -1;
    }
    *(uint32_t *)(uintptr_t)w[0] = (uint32_t)(w[1] - (uintptr_t)lp64_p32_base);
  }
  return count;
}

#if LP64_P32_CHECK
/* the checking build's stop (lp64_p32.h): a host pointer encoded */
void lp64_p32_check_fail(unsigned long long p, const char *file, int line) {
  fprintf(stderr, "lp64-p32: %s:%d: a host pointer (0x%llx) stored as a 32-bit game address (the offset model's check)\n",
          file, line, p);
  abort();
}
#endif
#else
typedef int lp64_p32_image_empty_; /* ISO C wants a declaration */
#endif

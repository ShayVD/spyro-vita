/* The offset model's start-up (D250 O3, O4; src/compat/p32.h), shared by the
 * headless hosts' main (src/platform/linux32/low_image.c: linuxa64, macos)
 * and the PC's (src/platform/pc/main.c on macOS).
 *
 * A P32 slot is a 32-bit offset from spyro_p32_base, the image's start, so
 * everything the game points at must lie within 4 GB above it; and the image
 * lies above 4 GB (linuxa64's link, macOS's __PAGEZERO), so that a pointer
 * cut to 32 bits instead of encoded points at nothing. Empty elsewhere. */
#include "p32_image.h"

#if SPYRO_P32_OFFSET
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

/* ELF's linker symbols, which savestate.c stamps a save state's layout
 * with (the build's own identity): Mach-O has none, so marks in the image,
 * as the PC's Windows build has (src/platform/pc/low.c) */
char etext[1], _edata[1], _end[1];

/* the executable's segments (but __PAGEZERO and __LINKEDIT), slid */
char *p32_image_end(void) {
  const struct mach_header_64 *h = (const void *)spyro_p32_base;
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
char *p32_image_end(void) { return _end; }
#endif

int p32_within(const void *p, size_t len) {
  return (uintptr_t)p >= (uintptr_t)spyro_p32_base &&
         (uintptr_t)p + len - (uintptr_t)spyro_p32_base <= 0x100000000ull;
}

/* The data image's pointer words (tools/data2s.py --p32-offset): {slot,
 * target} pairs, ending {0, 0}; each word becomes target's offset from the
 * base, as P32_SET stores it. */
extern const uint64_t s1_pie_words[];

int p32_image_start(char *why, size_t n) {
  char *end = p32_image_end();
  int words = 0;
  if ((uintptr_t)spyro_p32_base < 0x100000000ull || !p32_within(end, 0)) {
    snprintf(why, n, "the image %p-%p must lie above 4 GB, within 4 GB of its start",
             (void *)spyro_p32_base, (void *)end);
    return -1;
  }
  for (const uint64_t *w = s1_pie_words; w[0]; w += 2, words++) {
    if (!p32_within((const void *)(uintptr_t)w[1], 0) || !p32_within((const void *)(uintptr_t)w[0], 4)) {
      snprintf(why, n, "data word %d: slot 0x%llx, target 0x%llx, outside the image", words,
               (unsigned long long)w[0], (unsigned long long)w[1]);
      return -1;
    }
    *(uint32_t *)(uintptr_t)w[0] = (uint32_t)(w[1] - (uintptr_t)spyro_p32_base);
  }
  return words;
}

#if SPYRO_P32_CHECK
/* the checking build's stop (src/compat/p32.h): a host pointer encoded */
void p32_check_fail_(unsigned long long p, const char *file, int line) {
  fprintf(stderr, "spyro: %s:%d: a host pointer (0x%llx) stored as a 32-bit game address "
                  "(the offset model's check, D250)\n", file, line, p);
  abort();
}
#endif
#endif

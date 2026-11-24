/* lp64_low_image_win: the low image on 64-bit Windows (lp64/docs/low-image.md). Windows needs no reservation: link
 * the executable with a fixed base of 0x400000, no ASLR and not large-address-aware, and Windows keeps the whole
 * process below 2 GB (image, heaps, every thread stack and DLL). Every address then fits a P32 field and nothing can
 * be cut. With llvm-mingw:
 *   -Wl,--image-base,0x400000 -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va -Wl,-Xlink=-largeaddressaware:no
 * (lld's MinGW driver knows no --disable-large-address-aware: the -Xlink passes lld-link's own flag through.)
 *
 * GetSystemInfo's lpMaximumApplicationAddress is the system's (128 TB) whatever the flag, so it proves nothing:
 * lp64_win_low_probe looks where memory actually lands. Wine (and Proton) don't apply the limit to 64-bit programs;
 * there the probe may say no once the process has grown, and the game must not rely on host memory being low (it
 * never should: only what the game itself addresses must be, its image and .bss). From spyro-vita
 * src/platform/pc/low_win.c (MIT; its D241). */
#if !defined(_WIN32) || !defined(_WIN64)
#error "lp64_low_image_win.c is for 64-bit Windows"
#endif
#include "lp64_low_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

extern IMAGE_DOS_HEADER __ImageBase;

int lp64_is_low(const void *p, size_t n) { return (uintptr_t)p + n <= LP64_LOW_LIMIT; }
int lp64_reserved_pages(void) { return 0; }

int lp64_win_low_probe(char *report, size_t cap) {
  void *heap = malloc(64), *map = VirtualAlloc(NULL, 1 << 20, MEM_RESERVE, PAGE_NOACCESS);
  int here = 0, all = lp64_is_low(&__ImageBase, 0) && heap && lp64_is_low(heap, 64) && map && lp64_is_low(map, 1 << 20) &&
                  lp64_is_low(&here, sizeof here);
  if (report && cap)
    snprintf(report, cap, "image at %p; heap %p, a fresh mapping %p, stack %p: all below 2 GB: %s",
             (void *)&__ImageBase, heap, map, (void *)&here, all ? "yes" : "no");
  free(heap);
  if (map) VirtualFree(map, 0, MEM_RELEASE);
  return all;
}

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
 *    too. src/platform/common/p32_image.c (Encore's lp64_p32_image, D279) checks the image and stores the data image's offsets.
 *  - Linux on ARM (D250 O5): the offset model too, the image non-PIE above 4 GB as linuxa64's (CMakeLists.txt),
 *    the base its start. Encore's lp64_low_image_dyn.c reserves every free page from 64 KiB to 4 GB, from
 *    .preinit_array (D279; spyro's own copy until then, D266), so a pointer cut to 32 bits points at nothing.
 *    LP64_LOW_TRAP=0 skips it.
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
#include "lp64_low_image.h"
#include "p32_image.h"

int pc_low_reserved_pages(void) { return lp64_reserved_pages(); }

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

/* The offset model's start-up (D250 O3, O4; src/compat/p32.h), shared by the
 * headless hosts' main (src/platform/linux32/low_image.c: linuxa64, macos)
 * and the PC's (src/platform/pc/main.c on macOS and Linux on ARM): Encore's
 * lp64_p32_image.c (third_party/encore/lp64/runtime/p32_image, D279), built
 * here so that it sits where spyro's own copy did in the link. Empty unless
 * the offset model. */
#include "p32_image.h"

#if SPYRO_P32_OFFSET && defined(__APPLE__)
/* ELF's linker symbols, which savestate.c stamps a save state's layout
 * with (the build's own identity): Mach-O has none, so marks in the image,
 * as the PC's Windows build has (src/platform/pc/low.c) */
char etext[1], _edata[1], _end[1];
#endif

#include "../../../third_party/encore/lp64/runtime/p32_image/lp64_p32_image.c"

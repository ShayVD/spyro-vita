/* lp64_p32_image: the offset model's start-up and checks (lp64/runtime/p32_image/README.md). Include lp64_p32.h
 * (it is taken from ../p32, or forced in). Empty unless LP64_P32_OFFSET. */
#ifndef LP64_P32_IMAGE_H
#define LP64_P32_IMAGE_H
#include "../p32/lp64_p32.h"

#include <stddef.h>
#include <stdint.h>

#if LP64_P32_OFFSET
/* the image's end: Linux's _end; on macOS the end of the executable's last segment (but __LINKEDIT), slid */
char *lp64_p32_image_end(void);
/* 1 if [p, p + len) lies within the 4 GB above lp64_p32_base, where an offset can reach */
int lp64_p32_within(const void *p, size_t len);
/* Checks that the image lies above 4 GB (so a pointer cut to 32 bits points at nothing) and within 4 GB of its
 * start; then stores the data image's pointer words: `words` is {slot, target} pairs ending {0, 0} (an assembler's
 * `.quad slot, target` table, NULL for none), and each 32-bit slot becomes target's offset, as P32_SET stores it.
 * Call it before anything reads the game's data. Returns the number of words, or -1 with why. */
int lp64_p32_image_start(const uint64_t *words, char *why, size_t n);
#endif

#endif

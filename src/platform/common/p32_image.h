/* The offset model's start-up (D250 O3, O4): Encore's lp64_p32_image
 * (third_party/encore/lp64/runtime/p32_image, D279) under spyro's names;
 * p32_image.c builds it. */
#ifndef SPYRO_P32_IMAGE_H
#define SPYRO_P32_IMAGE_H
#include "../../../third_party/encore/lp64/runtime/p32_image/lp64_p32_image.h"

#if SPYRO_P32_OFFSET
/* the data image's pointer words (tools/data2s.py --p32-offset) */
extern const uint64_t s1_pie_words[];
/* the image's end (Linux's _end; macOS's last segment, slid) */
#define p32_image_end lp64_p32_image_end
/* 1 if [p, p+len) lies within 4 GB above the base */
#define p32_within lp64_p32_within
/* checks the image and stores the data image's offsets, before anything
 * reads the game's data: the number of words, or -1 with why */
#define p32_image_start(why, n) lp64_p32_image_start(s1_pie_words, why, n)
#endif

#endif

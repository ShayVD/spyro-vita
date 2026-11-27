/* The offset model's start-up (D250 O3, O4): p32_image.c. */
#ifndef SPYRO_P32_IMAGE_H
#define SPYRO_P32_IMAGE_H
#include <stddef.h>

#if SPYRO_P32_OFFSET
/* the image's end (Linux's _end; macOS's last segment, slid) */
char *p32_image_end(void);
/* 1 if [p, p+len) lies within 4 GB above the base */
int p32_within(const void *p, size_t len);
/* checks the image and stores the data image's offsets, before anything
 * reads the game's data: the number of words, or -1 with why */
int p32_image_start(char *why, size_t n);
#endif

#endif

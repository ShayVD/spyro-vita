/* asm/memory.s by hand (docs/porting-plan.md §2.2 rank 7). Word loops that
 * stop after writing the word at out + size - 4 (or - 16), as the original;
 * size must be a non-zero multiple of 4 (16 for Memset16). */
#include <stdint.h>

void Memset(void *out, int value, int size) {
  uint32_t *p = out, *last = (uint32_t *)((char *)out + size - 4);
  do {
    *p = (uint32_t)value;
  } while (p++ != last);
}

void Memset16(void *out, int value, int size) {
  uint32_t *p = out, *last = (uint32_t *)((char *)out + size - 16);
  for (;;) {
    p[0] = p[1] = p[2] = p[3] = (uint32_t)value;
    if (p == last)
      return;
    p += 4;
  }
}

void Memcpy(void *out, void *in, int size) {
  uint32_t *d = out, *s = in, *last = (uint32_t *)((char *)out + size - 4);
  do {
    *d = *s++;
  } while (d++ != last);
}

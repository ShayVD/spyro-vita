/* PS1 RAM arena and scratchpad (src/platform/common/arena.c). */
#ifndef PSX_ARENA_H
#define PSX_ARENA_H

#include <stdint.h>

#define PSX_RAM_BASE 0x80000000u
#define PSX_RAM_SIZE 0x200000u

#ifdef __cplusplus
extern "C" {
#endif

extern uintptr_t g_psx_arena_base;       /* host address of PS1 0x80000000 */
extern unsigned char g_psx_scratch[1024]; /* PS1 scratchpad 0x1F800000 */

/* PS1 address -> host pointer into the arena. */
#define PSX_ARENA(a) ((void *)(g_psx_arena_base + ((uint32_t)(a) - PSX_RAM_BASE)))

void psx_arena_init(void);

#ifdef __cplusplus
}
#endif

#endif

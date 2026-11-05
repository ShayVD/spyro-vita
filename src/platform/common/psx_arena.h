/* PS1 RAM arena and scratchpad (src/platform/common/arena.c). */
#ifndef PSX_ARENA_H
#define PSX_ARENA_H

#include <stdint.h>

#define PSX_RAM_BASE 0x80000000u
#define PSX_RAM_SIZE 0x200000u

#ifdef __cplusplus
extern "C" {
#endif

extern uintptr_t g_psx_arena_base;       /* host address of PS1 PSX_MEM_PSX */
extern unsigned char g_psx_scratch[1024]; /* PS1 scratchpad 0x1F800000 */

#if SPYRO_NATIVE_MEM
/* The native build (docs/native-plan.md stage 4, decisions D103): the
 * game's data labels are ordinary globals. tools/data2s.py --image puts them
 * in one section, an image of PS1 RAM from the first label (0x8006BBE0) to
 * main_BSS_END, so a PS1 address there is s1_labels + offset. The rest of
 * the game's memory is the arena. */
#define PSX_LABELS_PSX 0x8006BBE0u /* the first data label */
#define PSX_BSS_END 0x8007AA38u    /* main_BSS_END: overlay space */
#define PSX_MEM_PSX PSX_RAM_BASE
#define PSX_MEM_SIZE PSX_RAM_SIZE
#define PSX_OT_TERMINATOR_OFS 0x6BBE0u /* a copy of D_8006BBE0 (port.c) */
extern char s1_labels[], s1_labels_end[];
/* PS1 address -> host: a data label, or else the arena. */
void *psx_mem_host(uint32_t a);
#define PSX_ARENA(a) psx_mem_host((uint32_t)(a))
#else
#define PSX_MEM_PSX PSX_RAM_BASE
#define PSX_MEM_SIZE PSX_RAM_SIZE
#define PSX_OT_TERMINATOR_OFS 0x6BBE0u /* D_8006BBE0, at its PS1 address */
/* PS1 address -> host pointer into the arena. */
#define PSX_ARENA(a) ((void *)(g_psx_arena_base + ((uint32_t)(a) - PSX_RAM_BASE)))
#endif
/* The OT terminator the game links last (order_table.c): a NOP primitive
 * whose tag is 0xFFFFFF. It must lie in the block the OT's 24-bit links
 * decode in (psx_mem.h). */
#define PSX_OT_TERMINATOR ((void *)(g_psx_arena_base + PSX_OT_TERMINATOR_OFS))

void psx_arena_init(void);

#ifdef __cplusplus
}
#endif

#endif

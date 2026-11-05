/* Port start-up between the platform's main() and the decomp's main()
 * (docs/porting-plan.md §4.3): the parts of PS1 memory the game expects at
 * fixed places, then the game. */
#include "psx_arena.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void spyro_decomp_main(void);
extern void psx_overlays_init(void);
extern uint32_t g_mips_sp;
extern char D_8006BBE0[];               /* OT terminator primitive (order_table.c) */
extern uint32_t *s1_scratch_relocs[]; /* tools/data2s.py: words holding 0x1F80xxxx */

#define EMU_STACK_TOP 0x80020000u /* free on the host: part of the PS1 exe image */

/* Data words that hold scratchpad addresses point at the port's scratchpad,
 * the first KB of the arena (MIPS_SCRATCH_HI in mips2c_rt.h). */
static void scratch_relocs(void) {
  uint32_t **r;
  for (r = s1_scratch_relocs; *r; r++)
    **r = (uint32_t)(uintptr_t)PSX_ARENA(0x80000000u + (**r & 0x3FFu));
}

#if SPYRO_PSX_LAYOUT
/* tools/data2s.py --map: the game's data labels are absolute symbols at their
 * PS1 addresses (decisions D23); copy their initial bytes into the arena. */
typedef struct {
  uint32_t psx_addr;
  const void *image;
  uint32_t image_size, psx_span;
} PsxImage;
extern const PsxImage s1_psx_image[];

static void psx_layout(void) {
  const PsxImage *p;
  for (p = s1_psx_image; p->psx_addr; p++)
    memcpy(PSX_ARENA(p->psx_addr), p->image,
           p->image_size < p->psx_span ? p->image_size : p->psx_span);
}
#endif

#if SPYRO_ARENA_MOVED
/* tools/vita/arena_syms.py (D25): the data labels' pointers, and the data
 * words that hold a label's PS1 address. */
typedef struct {
  void **ptr;
  uint32_t psx_addr;
} ArenaSym;
extern const ArenaSym s1_vita_syms[];
extern uint32_t *const s1_vita_fixups[];

void port_arena_symbols(void (*log)(const char *fmt, ...)) {
  const ArenaSym *s;
  uint32_t *const *f;
  uint32_t delta = (uint32_t)g_psx_arena_base - PSX_RAM_BASE;
  int n = 0, k = 0;
  for (s = s1_vita_syms; s->ptr; s++, n++)
    *s->ptr = PSX_ARENA(s->psx_addr);
  /* before spyro_main's psx_layout copies the images into the arena */
  for (f = s1_vita_fixups; *f; f++, k++)
    **f += delta;
  log("arena at 0x%08x: %d data labels, %d address words moved",
      (unsigned)g_psx_arena_base, n, k);
}
#endif

void spyro_main(void) {
  psx_overlays_init();
#if SPYRO_PSX_LAYOUT
  psx_layout(); /* D_8006BBE0, the OT terminator, is already in place */
#else
  memcpy(PSX_ARENA(0x8006BBE0), D_8006BBE0, 0x14);
#endif
  scratch_relocs();
#if SPYRO_NATIVE_MEM
  {
    /* D100: no mips2c stack in the arena. The hand C still makes the asm's
     * few stores below $sp (render_hand.h hand_spill_regs, particles_hand.c);
     * nothing reads them back, so they land in a small array. */
    static uint32_t s_hand_stack[64];
    g_mips_sp = (uint32_t)(uintptr_t)&s_hand_stack[64];
  }
#else
  g_mips_sp = (uint32_t)(uintptr_t)PSX_ARENA(EMU_STACK_TOP);
#endif
  spyro_decomp_main();
}

void PortFatal(const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "spyro: fatal: ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  fflush(stdout);
  abort();
}

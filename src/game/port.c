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
extern uint32_t D_8006CF48[4], D_8006CF58[16]; /* scratchpad address tables */

#define EMU_STACK_TOP 0x80020000u /* free on the host: part of the PS1 exe image */

static void scratch_tables(uint32_t *t, int n) {
  int i;
  for (i = 0; i < n; i++) {
    if ((t[i] & 0xFFFFFC00u) != 0x1F800000u)
      PortFatal("scratchpad table entry %d = %08X", i, t[i]);
    t[i] = (uint32_t)(uintptr_t)PSX_ARENA(0x80000000u + (t[i] & 0x3FFu));
  }
}

void spyro_main(void) {
  psx_overlays_init();
  memcpy(PSX_ARENA(0x8006BBE0), D_8006BBE0, 0x14);
  /* The scratchpad is the first KB of the arena (MIPS_SCRATCH_HI in mips2c_rt.h). */
  scratch_tables(D_8006CF48, 4);
  scratch_tables(D_8006CF58, 16);
  g_mips_sp = (uint32_t)(uintptr_t)PSX_ARENA(EMU_STACK_TOP);
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

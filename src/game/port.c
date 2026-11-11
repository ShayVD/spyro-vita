/* Port start-up between the platform's main() and the decomp's main()
 * (docs/porting-plan.md §4.3): the parts of PS1 memory the game expects at
 * fixed places, then the game. */
#include "port_controls.h"
#include "psx_arena.h"
#include "psx_region.h"
#include "region_data.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The port's control options (port_controls.h, decisions D47): all off
 * unless the platform turns them on, so the camera is the original's.
 * Defined here, outside the files the 60 Hz rules build again, so that
 * camera_hand.c reads this copy in both rule sets (D142). */
PortControls g_port_controls = {0, 0, 0, 100};

extern void spyro_decomp_main(void);
void PortFatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
extern void psx_overlays_init(void);
extern uint32_t g_mips_sp;
extern char D_8006BBE0[];               /* OT terminator primitive (order_table.c) */
extern uint32_t *s1_scratch_relocs[]; /* tools/data2s.py: words holding 0x1F80xxxx */
void render_ctx_init(void);            /* render_ctx.c (stage 7d, D123) */

#define EMU_STACK_TOP 0x80020000u /* free on the host: part of the PS1 exe image */

/* Data words that hold scratchpad addresses point at the port's scratchpad,
 * the first KB of the arena (MIPS_SCRATCH_HI in mips2c_rt.h), or in the
 * native build g_psx_scratch (D106). */
static void scratch_relocs(void) {
  uint32_t **r;
  for (r = s1_scratch_relocs; *r; r++)
#if SPYRO_NATIVE_MEM
    **r = (uint32_t)(uintptr_t)(g_psx_scratch + (**r & 0x3FFu));
#else
    **r = (uint32_t)(uintptr_t)PSX_ARENA(0x80000000u + (**r & 0x3FFu));
#endif
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

#if SPYRO_NATIVE_MEM
/* psx_arena.h: the host's tools (--poke, --peek, pad scripts, the tour) and
 * start-up name PS1 addresses (decisions D106). */
void *psx_mem_host(uint32_t a) {
  if (a >= PSX_LABELS_PSX && a < PSX_BSS_END)
    return s1_labels + (a - PSX_LABELS_PSX);
  if (a >= PSX_BSS_END && a - PSX_MEM_PSX < PSX_MEM_SIZE)
    return (void *)(g_psx_arena_base + (a - PSX_MEM_PSX));
  return NULL;
}

/* port_prefix.h PSX_KUSEG (D42): a store through a small integer from
 * 0x10000 up lands where the PS1 sends it, in a label or overlay space;
 * one into the executable's image, which the arena build left in an unused
 * part of the arena, lands in a sink nothing reads (none is known: the
 * stores D42 found are all below 0x10000, kernel RAM). */
static unsigned char s_kuseg_sink[256];

uintptr_t psx_kuseg_host(uint32_t a) {
  void *p = psx_mem_host(PSX_RAM_BASE | a);
  return p ? (uintptr_t)p : (uintptr_t)s_kuseg_sink;
}
#endif

void spyro_main(void) {
  psx_overlays_init();
#if SPYRO_PSX_LAYOUT
  psx_layout(); /* D_8006BBE0, the OT terminator, is already in place */
#else
  /* The OT's links reach only the game's memory block (decisions D106) */
  memcpy(PSX_OT_TERMINATOR, D_8006BBE0, 0x14);
#endif
  scratch_relocs();
  render_ctx_init(); /* the presenter's tables (stage 7d, D123) */
  if (region_map(g_Region)) {
    /* PAL's text, tables and music tables, from the player's own executable
     * (D133), once the game's memory is in place: the music tables go over
     * the US labels (D134) */
    RegionDataStats st;
    char err[200];
    if (region_data_load_disc(1, &st, err, sizeof err) != 0)
      PortFatal("%s's data: %s", psx_region()->serial, err);
  }
#if SPYRO_NATIVE_MEM
  /* D100: no mips2c stack in the arena; since D127 (the register-file
   * renderers gone) no code stores below $sp either, so g_mips_sp stays 0. */
#else
  g_mips_sp = (uint32_t)(uintptr_t)PSX_ARENA(EMU_STACK_TOP);
#endif
  spyro_decomp_main();
}

#if SPYRO_MEM_AUDIT
/* psx_mem.h MEM_AUDIT_ (decisions D106): the sites that decoded an address
 * outside overlay space (main_BSS_END to the top of RAM), with the first
 * distinct PS1 addresses each; listed at exit. */
typedef struct {
  const char *file;
  int line;
  unsigned n, nadr;
  uint32_t adr[16];
} MemSite;
static MemSite s_mem[256];
static int s_mem_n;

static void mem_report(void) {
  int i;
  unsigned k;
  for (i = 0; i < s_mem_n; i++) {
    fprintf(stderr, "mem-audit %s:%d n=%u", s_mem[i].file, s_mem[i].line, s_mem[i].n);
    for (k = 0; k < s_mem[i].nadr; k++)
      fprintf(stderr, " %08x", (unsigned)s_mem[i].adr[k]);
    fprintf(stderr, "\n");
  }
  fprintf(stderr, "mem-audit sites=%d\n", s_mem_n);
}

uintptr_t mem_audit(uintptr_t a, const char *file, int line) {
  uint32_t psx = (uint32_t)(a - g_psx_arena_base) + PSX_MEM_PSX;
  MemSite *s = NULL;
  int i;
  unsigned k;
  if (psx >= 0x8007AA38u && psx < PSX_RAM_BASE + PSX_RAM_SIZE)
    return a;
  for (i = 0; i < s_mem_n && !s; i++)
    if (s_mem[i].line == line && strcmp(s_mem[i].file, file) == 0)
      s = &s_mem[i];
  if (!s) {
    if (s_mem_n == 0)
      atexit(mem_report);
    if (s_mem_n == 256)
      return a;
    s = &s_mem[s_mem_n++];
    s->file = file;
    s->line = line;
  }
  s->n++;
  for (k = 0; k < s->nadr; k++)
    if (s->adr[k] == psx)
      return a;
  if (s->nadr < 16)
    s->adr[s->nadr++] = psx;
  return a;
}
#endif

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

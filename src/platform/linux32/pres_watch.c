/* --pres-write-check (stage 7g, decisions D128): the presenter writes none
 * of the game's memory. Around each presenter frame drawn inline (the
 * host's), the game's memory is copied before and compared after: the
 * arena (the block, with the data labels in the arena build), the labels'
 * section (the native build), the scratchpad and the kernel RAM the
 * port's small addresses go to. Any byte changed is a write the by-name
 * link check (tests/pres/link_check.py) can't see: through a pointer, or
 * by a name it takes for read only. On the Vita the presenter runs beside
 * the logic draw, so such a write would race with it.
 *
 * And --pres-check-late (with --pres-check): the presenter's list for the
 * compare is recorded after the logic draw instead of before it, so a
 * frame that depends on anything the logic draw writes (which on the Vita
 * it may read while the logic draw runs) differs from D30's. */
#include "host.h"
#include "present.h"
#include "psx_arena.h"

#include <stdlib.h>
#include <string.h>

extern unsigned char g_psx_kernel_ram[0x10000];
extern int g_Gamestate;

typedef struct {
  const char *name;
  unsigned char *at, *copy;
  uint32_t len, psx; /* its PS1 address, for the log */
} Area;

enum { AREAS = 4, SHOW = 8 };
static Area s_area[AREAS];
static int s_nareas;
static long long s_frames, s_written, s_bytes;
static unsigned s_shown;

static void add(const char *name, void *at, uint32_t len, uint32_t psx) {
  Area *a = &s_area[s_nareas++];
  a->name = name;
  a->at = at;
  a->len = len;
  a->psx = psx;
  a->copy = malloc(len);
}

static void watch(int after) {
  int i, wrote = 0;
  if (!s_nareas) {
    add("the game's memory", (void *)g_psx_arena_base, PSX_MEM_SIZE, PSX_MEM_PSX);
#if SPYRO_NATIVE_MEM
    add("the data labels", s1_labels, (uint32_t)(s1_labels_end - s1_labels), PSX_LABELS_PSX);
#endif
    add("the scratchpad", g_psx_scratch, sizeof g_psx_scratch, 0x1F800000u);
    add("the kernel RAM", g_psx_kernel_ram, sizeof g_psx_kernel_ram, 0x80000000u);
  }
  if (!after) {
    for (i = 0; i < s_nareas; i++)
      if (s_area[i].copy)
        memcpy(s_area[i].copy, s_area[i].at, s_area[i].len);
    return;
  }
  s_frames++;
  for (i = 0; i < s_nareas; i++) {
    const Area *a = &s_area[i];
    uint32_t k, n = 0, first = 0;
    if (!a->copy || !memcmp(a->copy, a->at, a->len))
      continue;
    for (k = 0; k < a->len; k++)
      if (a->copy[k] != a->at[k]) {
        if (!n++)
          first = k;
      }
    wrote = 1;
    s_bytes += n;
    if (s_shown < SHOW) {
      s_shown++;
      host_log("pres write check: frame %lld (gamestate %d): %u bytes of %s written, the first "
               "at 0x%08X (%02X -> %02X)", s_frames, g_Gamestate, n, a->name, a->psx + first,
               a->copy[first], a->at[first]);
    }
    memcpy(a->at, a->copy, a->len); /* put back, so the run stays the game's */
  }
  s_written += wrote;
}

void host_pres_watch_init(void) { g_present_pres_watch = watch; }

int host_pres_watch_report(void) {
  host_log("pres write check: %lld presenter frames, %lld wrote the game's memory (%lld bytes)",
           s_frames, s_written, s_bytes);
  return s_written != 0;
}

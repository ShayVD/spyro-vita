/* asm/order_table.s by hand (docs/porting-plan.md §2.2 rank 7, §5).
 * The world OT is 2048 buckets of {tail, head}; primitives are appended FIFO
 * per bucket and chained from the highest bucket down, with PS1 24-bit
 * links. The chain ends at D_8006BBE0, a NOP primitive whose tag is the
 * 0xFFFFFF terminator; OT links can only reach the game's memory block, so
 * the port keeps its copy there (port.c; psx_arena.h PSX_OT_TERMINATOR:
 * at its PS1 address in the arena, in the native build's block). */
#include "present.h"
#include "psx_arena.h"

#include <stdint.h>

typedef uint32_t u32;

extern u32 *g_WorldOT;
extern u32 *g_HudOT;

#define OT_TERMINATOR ((u32 *)PSX_OT_TERMINATOR)

void nd_list_linked(void); /* native_draw.c: the records so far are this list's */

#if SPYRO_MEM_AUDIT
#include "psx_mem.h"
/* decisions D106: every primitive the linked list reaches, through the
 * walk's decode (psx_mem.h), so the audit sees each one outside overlay
 * space. */
static void *audit_list(void *head) {
  const u32 *p = head;
  while ((*p & 0xFFFFFFu) != 0xFFFFFFu)
    p = (const u32 *)MEM_OT_NEXT(*p);
  return head;
}
#else
#define audit_list(head) (head)
#endif

/* sh to, 0(from); sb to >> 16, 2(from): the PS1 link, leaving the length byte. */
static void link24(u32 *from, void *to) {
  unsigned char *p = (unsigned char *)from;
  u32 a = (u32)(uintptr_t)to;
  p[0] = (unsigned char)a;
  p[1] = (unsigned char)(a >> 8);
  p[2] = (unsigned char)(a >> 16);
}

/* func_80016784: link the OT's buckets (highest first), then the HUD OT,
 * then the terminator; clears every bucket. Returns the head for DrawOTag. */
void *func_80016784(int size) {
  u32 *base = g_WorldOT, *at = base + size * 2, *hud = g_HudOT;
  u32 *tail, *head, *next;

  nd_list_linked();
  for (;;) {
    if (at == base) {
      /* World OT empty: draw the HUD OT alone. */
      tail = (u32 *)(uintptr_t)hud[0];
      head = (u32 *)(uintptr_t)hud[1];
      hud[0] = 0;
      if (!tail)
        return OT_TERMINATOR;
      hud[1] = 0;
      link24(tail, OT_TERMINATOR);
      return audit_list(head);
    }
    tail = (u32 *)(uintptr_t)at[-2];
    at -= 2;
    if (tail)
      break;
  }
  head = (u32 *)(uintptr_t)at[1];
  at[0] = 0;
  at[1] = 0;
  while (at != base) {
    next = (u32 *)(uintptr_t)at[-1];
    at -= 2;
    if (!next)
      continue;
    link24(tail, next);
    tail = (u32 *)(uintptr_t)at[0];
    at[1] = 0;
    at[0] = 0;
  }
  if (hud[1]) {
    link24(tail, (void *)(uintptr_t)hud[1]);
    tail = (u32 *)(uintptr_t)hud[0];
    hud[1] = 0;
    hud[0] = 0;
  }
  link24(tail, OT_TERMINATOR);
  return audit_list(head);
}

/* func_800168A0: append a primitive to world OT bucket pOTIndex. */
void func_800168A0(void *pPoly, int pOTIndex) {
  u32 *bucket = g_WorldOT + pOTIndex * 2;
  u32 *tail = (u32 *)(uintptr_t)bucket[0];
  bucket[0] = (u32)(uintptr_t)pPoly;
  if (tail)
    link24(tail, pPoly);
  else
    bucket[1] = (u32)(uintptr_t)pPoly;
}

/* func_800168DC: append a primitive to the HUD OT. Only C code calls it,
 * with 2D primitives: widescreen keeps their shape (present.c). */
void func_800168DC(void *pPoly) {
  u32 *tail = (u32 *)(uintptr_t)g_HudOT[0];
  port_present_note_2d(pPoly);
  g_HudOT[0] = (u32)(uintptr_t)pPoly;
  if (tail)
    link24(tail, pPoly);
  else
    g_HudOT[1] = (u32)(uintptr_t)pPoly;
}

/* asm/order_table.s by hand (docs/porting-plan.md §2.2 rank 7, §5).
 * The world OT is 2048 buckets of {tail, head}; primitives are appended FIFO
 * per bucket and chained from the highest bucket down, with PS1 24-bit
 * links. The chain ends at D_8006BBE0, a NOP primitive whose tag is the
 * 0xFFFFFF terminator; OT links can only reach arena memory, so the port
 * keeps its copy at the same PS1 address in the arena (port.c). */
#include "present.h"
#include "psx_arena.h"

#include <stdint.h>

typedef uint32_t u32;

extern u32 *g_WorldOT;
extern u32 *g_HudOT;

#define OT_TERMINATOR ((u32 *)PSX_ARENA(0x8006BBE0))

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
      return head;
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
  return head;
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

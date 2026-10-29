/* Scripted controller for the headless linux32 host (docs/porting-plan.md §5).
 * PadInitDirect hands us the game's buffers; each vblank we fill port 1 as a
 * digital pad (status 0, id 0x41, buttons active-low) from --pad FILE:
 *   <vblank> <button>[+<button>...] | none     # comment
 * A line's buttons hold from its vblank until the next line. Port 2 is empty. */
#include "host.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_padCommEnable; /* LIBPAD.C: PadStartCom/PadStopCom */

typedef struct {
  int vblank;
  uint16_t mask; /* pressed buttons, active-high */
} PadEvent;

static const struct {
  const char *name;
  uint16_t bit;
} k_buttons[] = {
    {"select", 0x0001}, {"l3", 0x0002},     {"r3", 0x0004},
    {"start", 0x0008},  {"up", 0x0010},     {"right", 0x0020},
    {"down", 0x0040},   {"left", 0x0080},   {"l2", 0x0100},
    {"r2", 0x0200},     {"l1", 0x0400},     {"r1", 0x0800},
    {"triangle", 0x1000}, {"circle", 0x2000}, {"cross", 0x4000},
    {"square", 0x8000},
};

static PadEvent *s_events;
static int s_nevents;
static int s_next;
static uint16_t s_mask;
static unsigned char *s_pad[2];

static int cmp_event(const void *a, const void *b) {
  const PadEvent *x = a, *y = b;
  return (x->vblank > y->vblank) - (x->vblank < y->vblank);
}

static int parse_buttons(char *s, uint16_t *mask) {
  char *tok;
  *mask = 0;
  if (strcmp(s, "none") == 0)
    return 0;
  for (tok = strtok(s, "+"); tok; tok = strtok(NULL, "+")) {
    size_t i;
    for (i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; i++)
      if (strcmp(tok, k_buttons[i].name) == 0)
        break;
    if (i == sizeof k_buttons / sizeof k_buttons[0])
      return -1;
    *mask |= k_buttons[i].bit;
  }
  return 0;
}

int host_pad_load(const char *path) {
  FILE *f = fopen(path, "r");
  char line[512];
  int lineno = 0, cap = 0;
  if (!f) {
    host_log("pad: cannot open %s", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    char btn[256];
    int v;
    char *hash = strchr(line, '#');
    lineno++;
    if (hash)
      *hash = 0;
    if (sscanf(line, "%d %255s", &v, btn) != 2) {
      char *p = line;
      while (isspace((unsigned char)*p))
        p++;
      if (*p == 0)
        continue;
      host_log("pad: %s:%d: expected '<vblank> <buttons>'", path, lineno);
      fclose(f);
      return -1;
    }
    if (s_nevents == cap) {
      cap = cap ? cap * 2 : 64;
      s_events = realloc(s_events, cap * sizeof *s_events);
    }
    s_events[s_nevents].vblank = v;
    if (parse_buttons(btn, &s_events[s_nevents].mask) != 0) {
      host_log("pad: %s:%d: unknown button in '%s'", path, lineno, btn);
      fclose(f);
      return -1;
    }
    s_nevents++;
  }
  fclose(f);
  qsort(s_events, s_nevents, sizeof *s_events, cmp_event);
  host_log("pad: %d events from %s", s_nevents, path);
  return 0;
}

static void fill(unsigned char *p, int connected, uint16_t mask) {
  if (!p)
    return;
  if (!connected) {
    p[0] = 0xFF;
    return;
  }
  p[0] = 0x00; /* ok */
  p[1] = 0x41; /* type 4 (digital), 1 data halfword */
  p[2] = (unsigned char)~mask;
  p[3] = (unsigned char)(~mask >> 8);
  p[4] = p[5] = p[6] = p[7] = 0x80;
}

void host_pad_vblank(int vblank) {
  while (s_next < s_nevents && s_events[s_next].vblank <= vblank) {
    if (s_events[s_next].mask != s_mask)
      host_log("pad: buttons %04x", s_events[s_next].mask);
    s_mask = s_events[s_next].mask;
    s_next++;
  }
  if (!g_padCommEnable)
    return;
  fill(s_pad[0], 1, s_mask);
  fill(s_pad[1], 0, 0);
}

/* The PsyX_Pad_* calls LIBPAD.C makes. */
void PsyX_Pad_InitPad(int slot, unsigned char *padData) {
  if (slot < 0 || slot > 1)
    return;
  s_pad[slot] = padData;
  fill(padData, slot == 0, 0);
}

int PsyX_Pad_GetStatus(int mtap, int slot) {
  (void)mtap;
  return slot == 0;
}

void PsyX_Pad_Vibrate(int mtap, int slot, unsigned char *table, int len) {
  (void)mtap;
  (void)slot;
  (void)table;
  (void)len;
}

void PsyX_Pad_InternalPadUpdates(void) {}

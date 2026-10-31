/* Scripted controller for the headless linux32 host (docs/porting-plan.md §5).
 * PadInitDirect hands us the game's buffers; each vblank we fill port 1 as a
 * digital pad (status 0, id 0x41, buttons active-low) from --pad FILE:
 *   <vblank> <button>[+<button>...] | none     # comment
 *   @gs=G,level=L +<vblanks> <buttons>         # state-anchored
 * A line's buttons hold from its vblank until the next line. An anchored
 * line fires that many vblanks after g_Gamestate and g_LevelId first match
 * (either may be left out), counting from when the line before it was
 * applied; consecutive lines with the same condition share one anchor. Same
 * syntax and meaning as capture.lua's ORACLE_PAD. In place of the buttons a
 * line may say poke:ADDR=VALUE (hex): it writes that word of PS1 RAM when it
 * fires and leaves the buttons as they were (test set-ups such as putting
 * Spyro somewhere, docs/whole-game-host.md). --pad-after FILE runs once the
 * --tour is done. Port 2 is empty.
 * --analog reports a DualShock in analog mode (id 0x73) instead (decisions
 * D46); a line may then also set the sticks, lx=N ly=N rx=N ry=N (0..255,
 * 128 centred) among its buttons ("up+rx=255"); sticks it doesn't name are
 * centred. With the sticks centred the game plays as with the digital pad. */
#include "host.h"
#include "psx_arena.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_padCommEnable; /* LIBPAD.C: PadStartCom/PadStopCom */

typedef struct {
  int vblank;    /* absolute, or the offset of an anchored line */
  int anchored;  /* an "@gs=G,level=L +N" line */
  int gs, level; /* its condition, -1 = any */
  uint16_t mask; /* pressed buttons, active-high */
  unsigned char sticks[4]; /* right X, right Y, left X, left Y (--analog) */
  int poke;      /* a poke:ADDR=VALUE line: no button change */
  uint32_t addr, value;
} PadEvent;

typedef struct {
  PadEvent *ev;
  int n, next;
  int anchor, anchor_gs, anchor_level, anchor_set;
} PadScript;

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

static PadScript s_main, s_after;
static uint16_t s_mask;
static unsigned char s_sticks[4] = {0x80, 0x80, 0x80, 0x80};
int g_host_pad_analog; /* --analog */
static volatile uint16_t s_live; /* set by serve.c's thread */
static unsigned char *s_pad[2];

static int cmp_event(const void *a, const void *b) {
  const PadEvent *x = a, *y = b;
  return (x->vblank > y->vblank) - (x->vblank < y->vblank);
}

/* Takes the stick tokens (rx=N ...) out of text into sticks (centred if
 * not named); "none" if nothing else is left. Returns -1 on a bad one. */
static int parse_sticks(char *text, unsigned char sticks[4]) {
  static const char *const names[4] = {"rx=", "ry=", "lx=", "ly="};
  char out[256] = "", *tok;
  int i;
  memset(sticks, 0x80, 4);
  for (tok = strtok(text, "+"); tok; tok = strtok(NULL, "+")) {
    for (i = 0; i < 4 && strncmp(tok, names[i], 3) != 0; i++)
      ;
    if (i == 4) {
      if (out[0])
        strncat(out, "+", sizeof out - strlen(out) - 1);
      strncat(out, tok, sizeof out - strlen(out) - 1);
    } else {
      int v = atoi(tok + 3);
      if (v < 0 || v > 255)
        return -1;
      sticks[i] = (unsigned char)v;
    }
  }
  strcpy(text, out[0] ? out : "none");
  return 0;
}

int host_pad_parse(const char *text, uint16_t *mask) {
  char buf[256], *s = buf;
  char *tok;
  snprintf(buf, sizeof buf, "%s", text);
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

static int load(PadScript *ps, const char *path) {
  FILE *f = fopen(path, "r");
  char line[512];
  int lineno = 0, cap = 0, anchored = 0;
  if (!f) {
    host_log("pad: cannot open %s", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    char btn[256], cond[128];
    int v;
    PadEvent e = {0, 0, -1, -1, 0};
    char *hash = strchr(line, '#'), *p = line;
    lineno++;
    if (hash)
      *hash = 0;
    while (isspace((unsigned char)*p))
      p++;
    if (*p == 0)
      continue;
    if (sscanf(p, "@%127s +%d %255s", cond, &v, btn) == 3) {
      char *tok;
      e.anchored = anchored = 1;
      for (tok = strtok(cond, ","); tok; tok = strtok(NULL, ",")) {
        if (sscanf(tok, "gs=%d", &e.gs) != 1 && sscanf(tok, "level=%d", &e.level) != 1) {
          host_log("pad: %s:%d: unknown condition '%s'", path, lineno, tok);
          fclose(f);
          return -1;
        }
      }
    } else if (sscanf(p, "%d %255s", &v, btn) != 2) {
      host_log("pad: %s:%d: expected '<vblank> <buttons>'", path, lineno);
      fclose(f);
      return -1;
    }
    e.vblank = v;
    if (strncmp(btn, "poke:", 5) == 0) {
      if (sscanf(btn + 5, "%x=%x", &e.addr, &e.value) != 2) {
        host_log("pad: %s:%d: expected poke:ADDR=VALUE in '%s'", path, lineno, btn);
        fclose(f);
        return -1;
      }
      e.poke = 1;
    } else if (parse_sticks(btn, e.sticks) != 0 || host_pad_parse(btn, &e.mask) != 0) {
      host_log("pad: %s:%d: unknown button in '%s'", path, lineno, btn);
      fclose(f);
      return -1;
    }
    if (ps->n == cap) {
      cap = cap ? cap * 2 : 64;
      ps->ev = realloc(ps->ev, cap * sizeof *ps->ev);
    }
    ps->ev[ps->n++] = e;
  }
  fclose(f);
  if (!anchored) /* absolute-only scripts may be in any order */
    qsort(ps->ev, ps->n, sizeof *ps->ev, cmp_event);
  host_log("pad: %d events from %s", ps->n, path);
  return 0;
}

int host_pad_load(const char *path) { return load(&s_main, path); }
int host_pad_load_after(const char *path) { return load(&s_after, path); }

/* Apply the script's lines that are due; 1 once all are applied. */
static int run(PadScript *ps, int vblank) {
  int gs = (int)*(uint32_t *)PSX_ARENA(0x800757D8u);    /* g_Gamestate */
  int level = (int)*(uint32_t *)PSX_ARENA(0x8007596Cu); /* g_LevelId */
  while (ps->next < ps->n) {
    PadEvent *e = &ps->ev[ps->next];
    if (!e->anchored) {
      if (e->vblank > vblank)
        return 0;
    } else {
      if (!ps->anchor_set || e->gs != ps->anchor_gs || e->level != ps->anchor_level) {
        if ((e->gs >= 0 && e->gs != gs) || (e->level >= 0 && e->level != level))
          return 0;
        ps->anchor_set = 1;
        ps->anchor_gs = e->gs;
        ps->anchor_level = e->level;
        ps->anchor = vblank;
        host_log("pad: anchor gs=%d level=%d at vblank %d", e->gs, e->level, vblank);
      }
      if (vblank < ps->anchor + e->vblank)
        return 0;
    }
    if (e->poke) {
      *(uint32_t *)PSX_ARENA(e->addr) = e->value;
      host_log("pad: poke %08x = %08x at vblank %d", e->addr, e->value, vblank);
      ps->next++;
      continue;
    }
    if (e->mask != s_mask)
      host_log("pad: buttons %04x", e->mask);
    if (memcmp(e->sticks, s_sticks, 4) != 0)
      host_log("pad: sticks rx %d ry %d lx %d ly %d%s", e->sticks[0], e->sticks[1], e->sticks[2],
               e->sticks[3], g_host_pad_analog ? "" : " (ignored without --analog)");
    s_mask = e->mask;
    memcpy(s_sticks, e->sticks, 4);
    ps->next++;
  }
  return 1;
}

static void fill(unsigned char *p, int connected, uint16_t mask) {
  if (!p)
    return;
  if (!connected) {
    p[0] = 0xFF;
    return;
  }
  p[0] = 0x00; /* ok */
  p[1] = g_host_pad_analog ? 0x73 : 0x41; /* type 7 (analog), 3 halfwords; 4 (digital), 1 */
  p[2] = (unsigned char)~mask;
  p[3] = (unsigned char)(~mask >> 8);
  p[4] = p[5] = p[6] = p[7] = 0x80;
  if (g_host_pad_analog)
    memcpy(p + 4, s_sticks, 4);
}

void host_pad_vblank(int vblank) {
  int done = run(&s_main, vblank);
  int tour = host_tour_vblank(vblank, done);
  if (tour >= 0)
    s_mask = (uint16_t)tour;
  else if (done && host_tour_done()) {
    static int s_after_logged;
    if (!s_after_logged && s_after.n) {
      s_after_logged = 1;
      host_log("pad: after-script starts at vblank %d", vblank);
    }
    run(&s_after, vblank);
  }
  if (!g_padCommEnable)
    return;
  fill(s_pad[0], 1, s_mask | s_live);
  fill(s_pad[1], 0, 0);
}

/* A save state was loaded and the vblank counter set back (D51): absolute
 * lines play again from there, with the buttons and sticks held at that
 * vblank. */
void host_pad_rewind(int vblank) {
  PadScript *ps = &s_main;
  int i;
  s_mask = 0;
  memset(s_sticks, 0x80, 4);
  for (i = 0; i < ps->n; i++) {
    if (ps->ev[i].anchored) {
      host_log("pad: anchored lines don't rewind after a state load");
      return;
    }
    if (ps->ev[i].vblank > vblank)
      break;
    if (!ps->ev[i].poke) {
      s_mask = ps->ev[i].mask;
      memcpy(s_sticks, ps->ev[i].sticks, 4);
    }
  }
  ps->next = i;
}

void host_pad_set_live(uint16_t mask) { s_live = mask; }

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

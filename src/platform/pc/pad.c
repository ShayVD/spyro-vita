/* The PS1 pad on PC (docs/pc-build.md): input.c's state, read once a vblank
 * into the buffers our libpad hands us (psx_pad.h, D114), as the PS4's and
 * the Vita's pad.c fill them. Analog by default: a DualShock (id 0x73) with
 * both sticks; the right stick turns the free camera (port_controls.h, on as
 * on the Vita, the Vita's inverted axes). analog=0: a digital pad (id 0x41). */
#include "pc.h"
#include "host.h"
#include "port_controls.h"
#include "psx_pad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pad= script (tests, CI), the Vita's format (vita/pad.c), ORed with the
 * keyboard and gamepads:
 *   <vblank> <button>[+<button>...] | none     # comment
 * A line's buttons hold from its vblank until the next line; with analog=1
 * lx=N ly=N rx=N ry=N (0..255, 128 centred) set the sticks too. */
typedef struct {
  int vblank;
  uint16_t mask;
  unsigned char sticks[4]; /* right X, right Y, left X, left Y */
} PadEvent;

static const struct {
  const char *name;
  uint16_t bit;
} k_names[] = {
    {"select", 0x0001},   {"l3", 0x0002},     {"r3", 0x0004},    {"start", 0x0008},
    {"up", 0x0010},       {"right", 0x0020},  {"down", 0x0040},  {"left", 0x0080},
    {"l2", 0x0100},       {"r2", 0x0200},     {"l1", 0x0400},    {"r1", 0x0800},
    {"triangle", 0x1000}, {"circle", 0x2000}, {"cross", 0x4000}, {"square", 0x8000},
};

static PadEvent *s_events;
static int s_nevents, s_next;
static uint16_t s_script;
static unsigned char s_script_sticks[4] = {0x80, 0x80, 0x80, 0x80};

static int parse_buttons(char *text, PadEvent *ev) {
  static const char *const stick_names[4] = {"rx=", "ry=", "lx=", "ly="};
  ev->mask = 0;
  memset(ev->sticks, 0x80, 4);
  if (!strcmp(text, "none"))
    return 0;
  for (char *tok = strtok(text, "+"); tok; tok = strtok(NULL, "+")) {
    size_t i;
    for (i = 0; i < 4 && strncmp(tok, stick_names[i], 3) != 0; i++)
      ;
    if (i < 4) {
      int v = atoi(tok + 3);
      if (v < 0 || v > 255)
        return -1;
      ev->sticks[i] = (unsigned char)v;
      continue;
    }
    for (i = 0; i < sizeof k_names / sizeof k_names[0]; i++)
      if (!strcmp(tok, k_names[i].name))
        break;
    if (i == sizeof k_names / sizeof k_names[0])
      return -1;
    ev->mask |= k_names[i].bit;
  }
  return 0;
}

int pc_pad_load(const char *path) {
  char line[256], name[200];
  int v, cap = 0;
  FILE *f = fopen(path, "r");
  if (!f) {
    host_log("pad: cannot open %s", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    char *hash = strchr(line, '#');
    PadEvent ev;
    if (hash)
      *hash = 0;
    if (sscanf(line, "%d %199s", &v, name) != 2)
      continue;
    if (parse_buttons(name, &ev) != 0) {
      host_log("pad: bad buttons in %s: %s", path, line);
      fclose(f);
      return -1;
    }
    if (s_nevents == cap) {
      cap = cap ? cap * 2 : 64;
      s_events = realloc(s_events, (size_t)cap * sizeof *s_events);
    }
    ev.vblank = v;
    s_events[s_nevents++] = ev;
  }
  fclose(f);
  host_log("pad: %d events from %s", s_nevents, path);
  return 0;
}

static unsigned char *s_buf[2];
static uint16_t s_last;
static int s_settle = 4; /* vblanks of centred sticks first (the game calibrates on its first read) */

/* the CONTROLS rows (menu.c, D266) into the game's camera controls: the
 * Vita's defaults (vita.h) unless config.txt or the menu says otherwise */
void pc_pad_controls_apply(void) {
  static int analog = -1;
  if (analog == 0 && g_pc_opts.analog)
    s_settle = 4; /* turned on in the menu: centred sticks first, as at start */
  analog = g_pc_opts.analog;
  if (g_pc_opts.cam_speed < 50 || g_pc_opts.cam_speed > 200) /* config.txt by hand */
    g_pc_opts.cam_speed = g_pc_opts.cam_speed < 50 ? 50 : 200;
  g_port_controls.free_camera = g_pc_opts.analog && g_pc_opts.free_camera;
  g_port_controls.invert_x = g_pc_opts.cam_invert_x;
  g_port_controls.invert_y = g_pc_opts.cam_invert_y;
  g_port_controls.speed = g_pc_opts.cam_speed;
  g_port_controls.no_recentre = !g_pc_opts.cam_recentre;
}

void pc_pad_init(void) {
  pc_pad_controls_apply();
  host_log("pad: %s", g_pc_opts.analog ? (g_pc_opts.free_camera ? "analog, free camera" : "analog")
                                       : "digital");
  if (g_pc_opts.pad[0] && pc_pad_load(g_pc_opts.pad) != 0)
    pc_exit(1);
}

/* the PS1 pad's bytes (the Vita's fill) */
static void fill(unsigned char *p, int connected, uint16_t mask, const unsigned char *sticks) {
  if (!p)
    return;
  if (!connected) {
    p[0] = 0xFF;
    return;
  }
  p[0] = 0x00;
  p[1] = sticks ? 0x73 : 0x41; /* type 7 (analog), 3 halfwords; type 4 (digital), 1 */
  p[2] = (unsigned char)~mask;
  p[3] = (unsigned char)(~mask >> 8);
  p[4] = p[5] = p[6] = p[7] = 0x80;
  if (sticks)
    memcpy(p + 4, sticks, 4);
}

void pc_pad_vblank(void) {
  uint16_t m;
  unsigned char sticks[4];
  pc_input_read(&m, sticks);
  while (s_next < s_nevents && s_events[s_next].vblank <= host_vblank_count()) {
    s_script = s_events[s_next].mask;
    memcpy(s_script_sticks, s_events[s_next++].sticks, 4);
  }
  m |= s_script;
  for (int i = 0; i < 4; i++)
    if (s_script_sticks[i] != 0x80)
      sticks[i] = s_script_sticks[i];
  m = pc_menu_filter(m, pc_input_menu_key()); /* the menu's buttons never reach the game */
  if (s_settle) {
    s_settle--;
    memset(sticks, 0x80, 4);
  }
  if (m != s_last && g_pc_opts.stats > 1)
    host_log("pad: buttons %04x", m);
  s_last = m;
  if (!g_padCommEnable)
    return;
  fill(s_buf[0], 1, m, g_pc_opts.analog ? sticks : NULL);
  fill(s_buf[1], 0, 0, NULL);
}

/* a save state was loaded (savestate_host.c): no script to rewind here */
void host_pad_rewind(int vblank) { (void)vblank; }

/* The psx_pad_* calls our libpad makes (psx_pad.h, D114). */
void psx_pad_init(int slot, unsigned char *padData) {
  static const unsigned char centred[4] = {0x80, 0x80, 0x80, 0x80};
  if (slot < 0 || slot > 1)
    return;
  s_buf[slot] = padData;
  fill(padData, slot == 0, 0, g_pc_opts.analog ? centred : NULL);
}

int psx_pad_status(int mtap, int slot) {
  (void)mtap;
  return slot == 0;
}

void psx_pad_vibrate(int mtap, int slot, unsigned char *table, int len) {
  (void)mtap;
  (void)slot;
  (void)table;
  (void)len;
}

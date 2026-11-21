/* The DualShock 4 as the PS1's pad (docs/ps4-plan.md P3, D193): scePad, read
 * once a vblank, into the buffers our libpad hands us (psx_pad.h, D114), as
 * the Vita's pad.c fills them from SDL. The PS4's button bits are the PS1's
 * (OPTIONS is START); SELECT is the touch pad's click. Analog by default: the
 * left stick moves Spyro, the right stick turns the free camera
 * (port_controls.h, on as on the Vita). With analog=0 the left stick is the
 * d-pad and the right stick's sides are L2/R2, as the Vita's digital mode. */
#include "ps4.h"
#include "host.h"
#include "port_controls.h"
#include "psx_pad.h"

#include <string.h>

#include <orbis/Pad.h>

enum {
  PSX_SELECT = 0x0001, PSX_L3 = 0x0002, PSX_R3 = 0x0004, PSX_START = 0x0008,
  PSX_UP = 0x0010, PSX_RIGHT = 0x0020, PSX_DOWN = 0x0040, PSX_LEFT = 0x0080,
  PSX_L2 = 0x0100, PSX_R2 = 0x0200, PSX_L1 = 0x0400, PSX_R1 = 0x0800,
  PSX_TRIANGLE = 0x1000, PSX_CIRCLE = 0x2000, PSX_CROSS = 0x4000, PSX_SQUARE = 0x8000,
};
#define STICK_DEAD 0x40 /* of 0x80 either side of centre */

static int s_pad = -1;
static unsigned char *s_buf[2];
static uint16_t s_last;
static int s_settle = 4; /* vblanks of centred sticks first (the game calibrates on its first read) */
static unsigned s_reads, s_fails;

int ps4_pad_init(void) {
  int rc = scePadInit();
  if (rc < 0) {
    host_log("pad: scePadInit failed: 0x%x", (unsigned)rc);
    return -1;
  }
  s_pad = scePadOpen(g_ps4_user, 0, 0, NULL);
  if (s_pad < 0) {
    host_log("pad: scePadOpen(user 0x%x) failed: 0x%x", (unsigned)g_ps4_user, (unsigned)s_pad);
    return -1;
  }
  g_port_controls.free_camera = g_ps4_opts.analog; /* the Vita's defaults (vita.h) */
  g_port_controls.invert_x = g_port_controls.invert_y = 1;
  g_port_controls.speed = 100;
  host_log("pad: scePad handle %d, %s", s_pad, g_ps4_opts.analog ? "analog, free camera" : "digital");
  return 0;
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

void ps4_pad_vblank(void) {
  OrbisPadData d;
  uint16_t m = 0;
  unsigned char sticks[4] = {0x80, 0x80, 0x80, 0x80};
  int ok = 0;
  memset(&d, 0, sizeof d);
  if (s_pad >= 0) {
    s_reads++;
    if (scePadReadState(s_pad, &d) == 0 && d.connected)
      ok = 1;
    else
      s_fails++;
  }
  if (ok) {
    m = (uint16_t)(d.buttons & 0xFFFE); /* the PS1's bits; OPTIONS is START */
    if (d.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD)
      m |= PSX_SELECT;
    if (!g_ps4_opts.analog) {
      if (d.leftStick.x < 0x80 - STICK_DEAD) m |= PSX_LEFT;
      if (d.leftStick.x > 0x80 + STICK_DEAD) m |= PSX_RIGHT;
      if (d.leftStick.y < 0x80 - STICK_DEAD) m |= PSX_UP;
      if (d.leftStick.y > 0x80 + STICK_DEAD) m |= PSX_DOWN;
      if (d.rightStick.x < 0x80 - STICK_DEAD) m |= PSX_L2;
      if (d.rightStick.x > 0x80 + STICK_DEAD) m |= PSX_R2;
    } else if (!s_settle) { /* the PS1's order: right x, right y, left x, left y */
      sticks[0] = d.rightStick.x;
      sticks[1] = d.rightStick.y;
      sticks[2] = d.leftStick.x;
      sticks[3] = d.leftStick.y;
    }
  }
  if (s_settle)
    s_settle--;
  if (m != s_last && g_ps4_opts.stats > 1)
    host_log("pad: buttons %04x", m);
  s_last = m;
  if (!g_padCommEnable)
    return;
  fill(s_buf[0], 1, m, g_ps4_opts.analog ? sticks : NULL);
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
  fill(padData, slot == 0, 0, g_ps4_opts.analog ? centred : NULL);
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

unsigned ps4_pad_failures(void) { return s_fails; }
unsigned ps4_pad_reads(void) { return s_reads; }

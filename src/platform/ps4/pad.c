/* The DualShock 4 as the PS1's pad (docs/ps4-plan.md P3-P4, D193, D245):
 * scePad, read once a vblank, into the buffers our libpad hands us
 * (psx_pad.h, D114), as the Vita's pad.c fills them from SDL. The PS4's
 * button bits are the PS1's (OPTIONS is START); SELECT is the touch pad's
 * click. Analog by default: the left stick moves Spyro, the right stick
 * turns the free camera (port_controls.h). With analog=0 the left stick is
 * the d-pad and the right stick's sides are L2/R2, as the Vita's digital
 * mode.
 *
 * The options menu (menu.c): hold SELECT (the touch pad's click) and press
 * START (OPTIONS); with touch_menu=1 also a tap on the touch pad (a finger
 * down and up again within a third of a second, no click), as the Vita's
 * front-screen tap (D46). The CONTROLS rows are the Vita's (D46, D47, D53,
 * D162). */
#include "ps4.h"
#include "host.h"
#include "menu_opts.h"
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
#define TAP_MAX_US 333000 /* a tap: down and up within this, no click */

static int s_pad = -1;
static unsigned char *s_buf[2];
static uint16_t s_last, s_now;
static int s_settle = 4; /* vblanks of centred sticks first (the game calibrates on its first read) */
static unsigned s_reads, s_fails;
static unsigned long long s_touch_t0; /* when the finger came down (0: none) */
static int s_touch_clicked;           /* the click was pressed while it was down */
static int s_tap;                     /* a tap not yet taken */

/* g_ps4_opts' controls -> the game's camera (port_controls.h); at start
 * and from the menu. Turning analog on centres the sticks a few vblanks
 * first. */
void ps4_pad_controls_apply(void) {
  static int analog;
  if (g_ps4_opts.analog && !analog)
    s_settle = 4;
  analog = g_ps4_opts.analog;
  g_port_controls.free_camera = g_ps4_opts.free_camera;
  g_port_controls.invert_x = g_ps4_opts.cam_invert_x;
  g_port_controls.invert_y = g_ps4_opts.cam_invert_y;
  if (g_ps4_opts.cam_speed < 50 || g_ps4_opts.cam_speed > 200) /* config.txt by hand */
    g_ps4_opts.cam_speed = g_ps4_opts.cam_speed < 50 ? 50 : 200;
  g_port_controls.speed = g_ps4_opts.cam_speed;
  g_port_controls.no_recentre = !g_ps4_opts.cam_recentre;
}

static const MenuOption k_rows[] = {
    {.section = MENU_CONTROLS, .label = "ANALOG STICK", .key = "analog",
     .value = &g_ps4_opts.analog, .names = g_menu_off_on, .count = 2,
     .on_change = ps4_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "FREE CAMERA", .key = "free_camera",
     .value = &g_ps4_opts.free_camera, .names = g_menu_off_on, .count = 2,
     .on_change = ps4_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "INVERT CAMERA X", .key = "cam_invert_x",
     .value = &g_ps4_opts.cam_invert_x, .names = g_menu_off_on, .count = 2,
     .on_change = ps4_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "INVERT CAMERA Y", .key = "cam_invert_y",
     .value = &g_ps4_opts.cam_invert_y, .names = g_menu_off_on, .count = 2,
     .on_change = ps4_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "CAMERA SPEED", .key = "cam_speed",
     .value = &g_ps4_opts.cam_speed, .min = 50, .max = 200, .step = 25, .fmt = "%d%%",
     .on_change = ps4_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "CAMERA RECENTRE", .key = "cam_recentre",
     .value = &g_ps4_opts.cam_recentre, .names = g_menu_off_on, .count = 2,
     .on_change = ps4_pad_controls_apply},
    {.section = MENU_CONTROLS, .label = "TOUCH TO OPEN MENU", .key = "touch_menu",
     .value = &g_ps4_opts.touch_menu, .names = g_menu_off_on, .count = 2},
};
MENU_OPTION_LIST(g_menu_controls, k_rows);

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
  ps4_pad_controls_apply();
  host_log("pad: scePad handle %d, %s; free camera %d (invert x %d, y %d, speed %d%%), recentre "
           "%d, touch pad tap opens the menu %d",
           s_pad, g_ps4_opts.analog ? "analog" : "digital", g_ps4_opts.free_camera,
           g_ps4_opts.cam_invert_x, g_ps4_opts.cam_invert_y, g_ps4_opts.cam_speed,
           g_ps4_opts.cam_recentre, g_ps4_opts.touch_menu);
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

/* a finger down and up again soon, with no click between: a tap */
static void touch(const OrbisPadData *d) {
  unsigned long long now = ps4_now_us();
  int down = d->touch.fingers > 0;
  if (down && !s_touch_t0) {
    s_touch_t0 = now ? now : 1;
    s_touch_clicked = 0;
  }
  if (down && (d->buttons & ORBIS_PAD_BUTTON_TOUCH_PAD))
    s_touch_clicked = 1;
  if (!down && s_touch_t0) {
    if (!s_touch_clicked && now - s_touch_t0 <= TAP_MAX_US) {
      s_tap = 1;
      if (g_ps4_opts.touch_menu)
        ps4_menu_request();
    }
    s_touch_t0 = 0;
  }
}

int ps4_pad_take_tap(void) {
  int t = s_tap;
  s_tap = 0;
  return t && g_ps4_opts.touch_menu;
}

/* the controller now, as PS1 bits, and its sticks (PS1 order) */
static uint16_t poll(unsigned char sticks[4]) {
  OrbisPadData d;
  uint16_t m = 0;
  memset(&d, 0, sizeof d);
  if (s_pad < 0)
    return 0;
  s_reads++;
  if (scePadReadState(s_pad, &d) != 0 || !d.connected) {
    s_fails++;
    return 0;
  }
  m = (uint16_t)(d.buttons & 0xFFFE); /* the PS1's bits; OPTIONS is START */
  if (d.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD)
    m |= PSX_SELECT;
  touch(&d);
  if (!g_ps4_opts.analog) {
    if (d.leftStick.x < 0x80 - STICK_DEAD) m |= PSX_LEFT;
    if (d.leftStick.x > 0x80 + STICK_DEAD) m |= PSX_RIGHT;
    if (d.leftStick.y < 0x80 - STICK_DEAD) m |= PSX_UP;
    if (d.leftStick.y > 0x80 + STICK_DEAD) m |= PSX_DOWN;
    if (d.rightStick.x < 0x80 - STICK_DEAD) m |= PSX_L2;
    if (d.rightStick.x > 0x80 + STICK_DEAD) m |= PSX_R2;
  } else if (sticks && !s_settle) { /* the PS1's order: right x, right y, left x, left y */
    sticks[0] = d.rightStick.x;
    sticks[1] = d.rightStick.y;
    sticks[2] = d.leftStick.x;
    sticks[3] = d.leftStick.y;
  }
  return m;
}

/* The menu's own reads (it runs with the game paused): the left stick
 * moves the selection too. */
uint16_t ps4_pad_read(void) {
  OrbisPadData d;
  uint16_t m = poll(NULL);
  memset(&d, 0, sizeof d);
  if (g_ps4_opts.analog && s_pad >= 0 && scePadReadState(s_pad, &d) == 0 && d.connected) {
    if (d.leftStick.y < 0x80 - STICK_DEAD) m |= PSX_UP;
    if (d.leftStick.y > 0x80 + STICK_DEAD) m |= PSX_DOWN;
    if (d.leftStick.x < 0x80 - STICK_DEAD) m |= PSX_LEFT;
    if (d.leftStick.x > 0x80 + STICK_DEAD) m |= PSX_RIGHT;
  }
  return m;
}

void ps4_pad_vblank(void) {
  unsigned char sticks[4] = {0x80, 0x80, 0x80, 0x80};
  uint16_t m = poll(sticks);
  if (s_settle)
    s_settle--;
  s_now = m;
  m = ps4_menu_filter(m);
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

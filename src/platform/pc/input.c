/* Keyboard and gamepads (docs/pc-build.md): Encore's pads (enc_input_poll,
 * enc_pad_read; D247) on the main thread into one PS1 pad state, which the
 * game thread reads each vblank (pad.c).
 *
 * Gamepads through Encore's PC backend (SDL3's gamepad API underneath: any
 * XInput, DualShock, DualSense, Switch Pro... controller SDL knows; up to
 * ENC_PC_PADS of them): face buttons by position (south is
 * Cross, east Circle, west Square, north Triangle), the shoulders L1/R1, the
 * triggers L2/R2, Back SELECT, Start START, the sticks L3/R3 and, analog=1,
 * both sticks as a DualShock's (the left moves Spyro, the right turns the
 * free camera, port_controls.h). Every connected gamepad counts. A trigger
 * counts as L2/R2 from half way (125 of the backend's 0-255).
 *
 * The keyboard (the common PS1 emulator layout):
 *   arrows or W A S D    d-pad
 *   K or Space           Cross (jump, glide)
 *   L                    Circle (flame)
 *   J or Left Shift      Square (charge)
 *   I                    Triangle (look)
 *   Q / E                L1 / R1
 *   1 / 3                L2 / R2 (turn the camera)
 *   Enter                START
 *   Backspace or Tab     SELECT
 * read by gaps.c (G7): the backend's own keyboard map on pad 0 is a
 * different one, so its bits are taken back out of pad 0. F11 and Alt+Enter
 * are the backend's (fullscreen). Esc or F1, or a gamepad's guide button,
 * opens and closes the options menu (menu.c, D275), as SELECT+START does. */
#include "pc.h"
#include "gaps.h"
#include "host.h"

#include "encore/platform.h"
#include "enc_pc.h"

#include <stdlib.h>
#include <string.h>

static const struct {
  uint32_t enc;
  uint16_t bit;
} k_buttons[] = {
    {ENC_BTN_SOUTH, PSX_CROSS},     {ENC_BTN_EAST, PSX_CIRCLE},  {ENC_BTN_WEST, PSX_SQUARE},
    {ENC_BTN_NORTH, PSX_TRIANGLE},  {ENC_BTN_SELECT, PSX_SELECT}, {ENC_BTN_START, PSX_START},
    {ENC_BTN_L1, PSX_L1},           {ENC_BTN_R1, PSX_R1},         {ENC_BTN_L3, PSX_L3},
    {ENC_BTN_R3, PSX_R3},           {ENC_BTN_UP, PSX_UP},         {ENC_BTN_DOWN, PSX_DOWN},
    {ENC_BTN_LEFT, PSX_LEFT},       {ENC_BTN_RIGHT, PSX_RIGHT},
};

#define TRIGGER_ON 125   /* of 255: SDL's 16000 of 32767 before D247 */
#define STICK_DEAD 12000 /* digital mode: a stick as the d-pad */

static EncMutex *s_mu;
static uint16_t s_buttons;
static int s_menu_key;
static unsigned char s_sticks[4] = {0x80, 0x80, 0x80, 0x80};

void pc_input_init(void) { enc_mutex_create(&s_mu); }

static unsigned char axis_byte(int16_t a) { return (unsigned char)((a + 32768) >> 8); }

/* the whole state again from the platform's, once a main-loop pass */
void pc_input_update(void) {
  uint16_t m = pc_gap_keyboard_psx();
  uint32_t kb = pc_gap_backend_keyboard_bits();
  int menu = pc_gap_menu_key();
  unsigned char st[4] = {0x80, 0x80, 0x80, 0x80};
  int best = 0; /* the stick furthest from centre wins */
  for (int p = 0; p < ENC_PC_PADS; p++) {
    EncPad g;
    int lx, ly, rx, ry, d;
    if (enc_pad_read(p, &g) != ENC_OK || !g.connected)
      continue;
    if (p == 0) /* the backend's keyboard map: not ours (gaps.c, G7) */
      g.buttons &= ~kb;
    if (g.buttons & ENC_BTN_MENU) /* the guide button */
      menu = 1;
    for (size_t i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; i++)
      if (g.buttons & k_buttons[i].enc)
        m |= k_buttons[i].bit;
    if (g.l2 >= TRIGGER_ON)
      m |= PSX_L2;
    if (g.r2 >= TRIGGER_ON)
      m |= PSX_R2;
    lx = g.lx;
    ly = g.ly;
    rx = g.rx;
    ry = g.ry;
    if (!g_pc_opts.analog) { /* digital: the left stick is the d-pad, the right's sides L2/R2 */
      if (lx < -STICK_DEAD) m |= PSX_LEFT;
      if (lx > STICK_DEAD) m |= PSX_RIGHT;
      if (ly < -STICK_DEAD) m |= PSX_UP;
      if (ly > STICK_DEAD) m |= PSX_DOWN;
      if (rx < -STICK_DEAD) m |= PSX_L2;
      if (rx > STICK_DEAD) m |= PSX_R2;
      continue;
    }
    d = abs(lx) + abs(ly) + abs(rx) + abs(ry);
    if (d > best) { /* the PS1's order: right x, right y, left x, left y */
      best = d;
      st[0] = axis_byte((int16_t)rx);
      st[1] = axis_byte((int16_t)ry);
      st[2] = axis_byte((int16_t)lx);
      st[3] = axis_byte((int16_t)ly);
    }
  }
  enc_mutex_lock(s_mu);
  s_buttons = m;
  s_menu_key = menu;
  memcpy(s_sticks, st, 4);
  enc_mutex_unlock(s_mu);
}

int pc_input_menu_key(void) {
  int k;
  enc_mutex_lock(s_mu);
  k = s_menu_key;
  enc_mutex_unlock(s_mu);
  return k;
}

void pc_input_read(uint16_t *buttons, unsigned char sticks[4]) {
  enc_mutex_lock(s_mu);
  *buttons = s_buttons;
  memcpy(sticks, s_sticks, 4);
  enc_mutex_unlock(s_mu);
}

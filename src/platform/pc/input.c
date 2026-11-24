/* Keyboard and gamepads (docs/pc-build.md): SDL3 events on the main thread
 * into one PS1 pad state, which the game thread reads each vblank (pad.c).
 *
 * Gamepads through SDL3's gamepad API (any XInput, DualShock, DualSense,
 * Switch Pro... controller SDL knows): face buttons by position (south is
 * Cross, east Circle, west Square, north Triangle), the shoulders L1/R1, the
 * triggers L2/R2, Back SELECT, Start START, the sticks L3/R3 and, analog=1,
 * both sticks as a DualShock's (the left moves Spyro, the right turns the
 * free camera, port_controls.h). Every connected gamepad counts.
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
 * F11 and Alt+Enter are video.c's (fullscreen). */
#include "pc.h"
#include "host.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

enum {
  PSX_SELECT = 0x0001, PSX_L3 = 0x0002, PSX_R3 = 0x0004, PSX_START = 0x0008,
  PSX_UP = 0x0010, PSX_RIGHT = 0x0020, PSX_DOWN = 0x0040, PSX_LEFT = 0x0080,
  PSX_L2 = 0x0100, PSX_R2 = 0x0200, PSX_L1 = 0x0400, PSX_R1 = 0x0800,
  PSX_TRIANGLE = 0x1000, PSX_CIRCLE = 0x2000, PSX_CROSS = 0x4000, PSX_SQUARE = 0x8000,
};

static const struct {
  SDL_Scancode sc;
  uint16_t bit;
} k_keys[] = {
    {SDL_SCANCODE_UP, PSX_UP},         {SDL_SCANCODE_DOWN, PSX_DOWN},
    {SDL_SCANCODE_LEFT, PSX_LEFT},     {SDL_SCANCODE_RIGHT, PSX_RIGHT},
    {SDL_SCANCODE_W, PSX_UP},          {SDL_SCANCODE_S, PSX_DOWN},
    {SDL_SCANCODE_A, PSX_LEFT},        {SDL_SCANCODE_D, PSX_RIGHT},
    {SDL_SCANCODE_K, PSX_CROSS},       {SDL_SCANCODE_SPACE, PSX_CROSS},
    {SDL_SCANCODE_L, PSX_CIRCLE},      {SDL_SCANCODE_J, PSX_SQUARE},
    {SDL_SCANCODE_LSHIFT, PSX_SQUARE}, {SDL_SCANCODE_I, PSX_TRIANGLE},
    {SDL_SCANCODE_Q, PSX_L1},          {SDL_SCANCODE_E, PSX_R1},
    {SDL_SCANCODE_1, PSX_L2},          {SDL_SCANCODE_3, PSX_R2},
    {SDL_SCANCODE_RETURN, PSX_START},  {SDL_SCANCODE_BACKSPACE, PSX_SELECT},
    {SDL_SCANCODE_TAB, PSX_SELECT},
};

static const struct {
  SDL_GamepadButton b;
  uint16_t bit;
} k_buttons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, PSX_CROSS},
    {SDL_GAMEPAD_BUTTON_EAST, PSX_CIRCLE},
    {SDL_GAMEPAD_BUTTON_WEST, PSX_SQUARE},
    {SDL_GAMEPAD_BUTTON_NORTH, PSX_TRIANGLE},
    {SDL_GAMEPAD_BUTTON_BACK, PSX_SELECT},
    {SDL_GAMEPAD_BUTTON_START, PSX_START},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PSX_L1},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PSX_R1},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, PSX_L3},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PSX_R3},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PSX_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PSX_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PSX_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PSX_RIGHT},
};

#define MAX_PADS 8
#define TRIGGER_ON 16000 /* of 32767 */
#define STICK_DEAD 12000 /* digital mode: a stick as the d-pad */

static SDL_Gamepad *s_pads[MAX_PADS];
static SDL_Mutex *s_mu;
static uint16_t s_buttons;
static unsigned char s_sticks[4] = {0x80, 0x80, 0x80, 0x80};

void pc_input_init(void) { s_mu = SDL_CreateMutex(); }

static void add_pad(SDL_JoystickID id) {
  for (int i = 0; i < MAX_PADS; i++)
    if (!s_pads[i]) {
      if ((s_pads[i] = SDL_OpenGamepad(id)))
        host_log("input: gamepad %s (%s)", SDL_GetGamepadName(s_pads[i]),
                 SDL_GetGamepadTypeForID(id) == SDL_GAMEPAD_TYPE_UNKNOWN ? "unknown type" : "known");
      return;
    }
}

static void remove_pad(SDL_JoystickID id) {
  for (int i = 0; i < MAX_PADS; i++)
    if (s_pads[i] && SDL_GetGamepadID(s_pads[i]) == id) {
      host_log("input: gamepad %s gone", SDL_GetGamepadName(s_pads[i]));
      SDL_CloseGamepad(s_pads[i]);
      s_pads[i] = NULL;
    }
}

static unsigned char axis_byte(Sint16 a) { return (unsigned char)((a + 32768) >> 8); }

/* the whole state again from SDL's (after any event) */
static void update(void) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  uint16_t m = 0;
  unsigned char st[4] = {0x80, 0x80, 0x80, 0x80};
  int best = 0; /* the stick furthest from centre wins */
  for (size_t i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++)
    if (keys[k_keys[i].sc])
      m |= k_keys[i].bit;
  for (int p = 0; p < MAX_PADS; p++) {
    SDL_Gamepad *g = s_pads[p];
    Sint16 lx, ly, rx, ry;
    int d;
    if (!g)
      continue;
    for (size_t i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; i++)
      if (SDL_GetGamepadButton(g, k_buttons[i].b))
        m |= k_buttons[i].bit;
    if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_ON)
      m |= PSX_L2;
    if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_ON)
      m |= PSX_R2;
    lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    rx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX);
    ry = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY);
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
      st[0] = axis_byte(rx);
      st[1] = axis_byte(ry);
      st[2] = axis_byte(lx);
      st[3] = axis_byte(ly);
    }
  }
  SDL_LockMutex(s_mu);
  s_buttons = m;
  memcpy(s_sticks, st, 4);
  SDL_UnlockMutex(s_mu);
}

void pc_input_event(const void *e_) {
  const SDL_Event *e = e_;
  switch (e->type) {
  case SDL_EVENT_GAMEPAD_ADDED:
    add_pad(e->gdevice.which);
    break;
  case SDL_EVENT_GAMEPAD_REMOVED:
    remove_pad(e->gdevice.which);
    break;
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP:
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    break;
  default:
    return;
  }
  update();
}

void pc_input_read(uint16_t *buttons, unsigned char sticks[4]) {
  SDL_LockMutex(s_mu);
  *buttons = s_buttons;
  memcpy(sticks, s_sticks, 4);
  SDL_UnlockMutex(s_mu);
}

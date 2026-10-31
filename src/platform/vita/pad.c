/* Vita controls through SDL2's game controller API, as a PS1 pad on port 1
 * (status 0, buttons active-low; the same buffer layout as
 * src/platform/linux32/pad.c). The Vita's buttons map one to one.
 * - analog=1 (default, decisions D46): a DualShock in analog mode (id 0x73)
 *   with both sticks' bytes; the game's own gamepad.c calibrates them, and
 *   Spyro walks, flies and looks with the left stick in any direction (the
 *   game turns a stick pushed past its threshold into the d-pad itself).
 * - analog=0: a digital pad (id 0x41); the left stick drives the d-pad (8
 *   directions), as before D46.
 * L2/R2 turn Spyro's camera (camera.c func_80035F58): a handheld Vita has
 * neither, so the right stick's X axis is L2 (left) / R2 (right), unless
 * free_camera=1 (D47: the game's camera reads the right stick itself, in
 * analog mode), and with reartouch=1 the rear touch pad's left / right half
 * too (D27). A PS TV's DualShock L2/R2/L3/R3 work as themselves. The
 * controls are rows of the options menu (menu.c, CONTROLS). */
#include "vita.h"
#include "../../game/port_controls.h"
#include "menu_opts.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_padCommEnable; /* LIBPAD.C: PadStartCom/PadStopCom */

enum {
  PSX_SELECT = 0x0001, PSX_L3 = 0x0002, PSX_R3 = 0x0004, PSX_START = 0x0008,
  PSX_UP = 0x0010, PSX_RIGHT = 0x0020, PSX_DOWN = 0x0040, PSX_LEFT = 0x0080,
  PSX_L2 = 0x0100, PSX_R2 = 0x0200, PSX_L1 = 0x0400, PSX_R1 = 0x0800,
  PSX_TRIANGLE = 0x1000, PSX_CIRCLE = 0x2000, PSX_CROSS = 0x4000, PSX_SQUARE = 0x8000,
};

static const struct {
  SDL_GameControllerButton b;
  uint16_t bit;
} k_map[] = {
    {SDL_CONTROLLER_BUTTON_A, PSX_CROSS},
    {SDL_CONTROLLER_BUTTON_B, PSX_CIRCLE},
    {SDL_CONTROLLER_BUTTON_X, PSX_SQUARE},
    {SDL_CONTROLLER_BUTTON_Y, PSX_TRIANGLE},
    {SDL_CONTROLLER_BUTTON_BACK, PSX_SELECT},
    {SDL_CONTROLLER_BUTTON_START, PSX_START},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PSX_L1},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PSX_R1},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, PSX_L3},
    {SDL_CONTROLLER_BUTTON_RIGHTSTICK, PSX_R3},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, PSX_UP},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, PSX_DOWN},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, PSX_LEFT},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PSX_RIGHT},
};

#define STICK_DEAD 16000

static SDL_GameController *s_ctl;
static unsigned char *s_pad[2];
static uint16_t s_last;
static int s_settle; /* vblanks left reporting centred sticks (read_sticks) */

/* pad= script, the linux32 host's --pad format (src/platform/linux32/pad.c):
 *   <vblank> <button>[+<button>...] | none     # comment
 * A line's buttons hold from its vblank until the next line. With analog=1
 * a line may also set the sticks, as the host's with --analog: lx=N ly=N
 * rx=N ry=N (0..255, 128 centred); a stick the script moves off centre
 * replaces the controller's. */
typedef struct {
  int vblank;
  uint16_t mask;
  unsigned char sticks[4]; /* right X, right Y, left X, left Y */
} PadEvent;

static const struct {
  const char *name;
  uint16_t bit;
} k_names[] = {
    {"select", PSX_SELECT}, {"l3", PSX_L3}, {"r3", PSX_R3}, {"start", PSX_START},
    {"up", PSX_UP}, {"right", PSX_RIGHT}, {"down", PSX_DOWN}, {"left", PSX_LEFT},
    {"l2", PSX_L2}, {"r2", PSX_R2}, {"l1", PSX_L1}, {"r1", PSX_R1},
    {"triangle", PSX_TRIANGLE}, {"circle", PSX_CIRCLE}, {"cross", PSX_CROSS},
    {"square", PSX_SQUARE},
};

static PadEvent *s_events;
static int s_nevents, s_next;
static uint16_t s_script;
static unsigned char s_script_sticks[4] = {0x80, 0x80, 0x80, 0x80};

static int parse_buttons(char *text, uint16_t *mask, unsigned char sticks[4]) {
  static const char *const stick_names[4] = {"rx=", "ry=", "lx=", "ly="};
  char *tok;
  *mask = 0;
  memset(sticks, 0x80, 4);
  if (!strcmp(text, "none"))
    return 0;
  for (tok = strtok(text, "+"); tok; tok = strtok(NULL, "+")) {
    size_t i;
    for (i = 0; i < 4 && strncmp(tok, stick_names[i], 3) != 0; i++)
      ;
    if (i < 4) {
      int v = atoi(tok + 3);
      if (v < 0 || v > 255)
        return -1;
      sticks[i] = (unsigned char)v;
      continue;
    }
    for (i = 0; i < sizeof k_names / sizeof k_names[0]; i++)
      if (!strcmp(tok, k_names[i].name))
        break;
    if (i == sizeof k_names / sizeof k_names[0])
      return -1;
    *mask |= k_names[i].bit;
  }
  return 0;
}

int vita_pad_load(const char *path) {
  char line[256], name[200];
  int v, cap = 0;
  FILE *f = fopen(path, "r");
  if (!f) {
    vita_log("pad: cannot open %s", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    char *hash = strchr(line, '#');
    uint16_t mask;
    unsigned char sticks[4];
    if (hash)
      *hash = 0;
    if (sscanf(line, "%d %199s", &v, name) != 2)
      continue;
    if (parse_buttons(name, &mask, sticks) != 0) {
      vita_log("pad: bad buttons in %s: %s", path, line);
      fclose(f);
      return -1;
    }
    if (s_nevents == cap) {
      cap = cap ? cap * 2 : 64;
      s_events = realloc(s_events, cap * sizeof *s_events);
    }
    s_events[s_nevents].vblank = v;
    memcpy(s_events[s_nevents].sticks, sticks, 4);
    s_events[s_nevents++].mask = mask;
  }
  fclose(f);
  vita_log("pad: %d events from %s", s_nevents, path);
  return 0;
}

/* sticks: right X, right Y, left X, left Y (0..255, 0x80 centred), the
 * PS1's byte order (gamepad.h GamepadSticks). */
static void fill(unsigned char *p, int connected, uint16_t mask, const unsigned char *sticks) {
  if (!p)
    return;
  if (!connected) {
    p[0] = 0xFF;
    return;
  }
  p[0] = 0x00; /* ok */
  p[1] = sticks ? 0x73 : 0x41; /* type 7 (analog), 3 halfwords; type 4 (digital), 1 */
  p[2] = (unsigned char)~mask;
  p[3] = (unsigned char)(~mask >> 8);
  p[4] = p[5] = p[6] = p[7] = 0x80;
  if (sticks)
    memcpy(p + 4, sticks, 4);
}

/* An SDL axis (-32768..32767) as a PS1 stick byte. */
static unsigned char stick_byte(int v) { return (unsigned char)((v + 32768) >> 8); }

static void open_first(void) {
  int i;
  for (i = 0; i < SDL_NumJoysticks() && !s_ctl; i++)
    if (SDL_IsGameController(i)) {
      s_ctl = SDL_GameControllerOpen(i);
      if (s_ctl)
        vita_log("pad: %s", SDL_GameControllerName(s_ctl));
    }
}

/* g_vita_opts' controls -> the game's camera (port_controls.h); at start
 * and from the menu. Turning analog on centres the sticks a few vblanks
 * first (read_sticks). */
static void controls_apply(void) {
  static int analog;
  if (g_vita_opts.analog && !analog)
    s_settle = 4;
  analog = g_vita_opts.analog;
  g_port_controls.free_camera = g_vita_opts.free_camera;
  g_port_controls.invert_x = g_vita_opts.cam_invert_x;
  g_port_controls.invert_y = g_vita_opts.cam_invert_y;
  if (g_vita_opts.cam_speed < 50 || g_vita_opts.cam_speed > 200) /* config.txt by hand */
    g_vita_opts.cam_speed = g_vita_opts.cam_speed < 50 ? 50 : 200;
  g_port_controls.speed = g_vita_opts.cam_speed;
}

void vita_pad_init(void) {
  open_first();
  if (!s_ctl)
    vita_log("pad: no game controller");
  controls_apply();
  vita_log("pad: %s; free camera %d (invert x %d, y %d, speed %d%%), rear touch %d",
           g_vita_opts.analog ? "analog (DualShock)" : "digital", g_vita_opts.free_camera,
           g_vita_opts.cam_invert_x, g_vita_opts.cam_invert_y, g_vita_opts.cam_speed,
           g_vita_opts.reartouch);
}

/* The options menu's CONTROLS rows (menu.c, D45-D47). */
static const MenuOption k_rows[] = {
    {.section = MENU_CONTROLS, .label = "ANALOG STICK", .key = "analog",
     .value = &g_vita_opts.analog, .names = g_menu_off_on, .count = 2,
     .on_change = controls_apply},
    {.section = MENU_CONTROLS, .label = "FREE CAMERA", .key = "free_camera",
     .value = &g_vita_opts.free_camera, .names = g_menu_off_on, .count = 2,
     .on_change = controls_apply},
    {.section = MENU_CONTROLS, .label = "INVERT CAMERA X", .key = "cam_invert_x",
     .value = &g_vita_opts.cam_invert_x, .names = g_menu_off_on, .count = 2,
     .on_change = controls_apply},
    {.section = MENU_CONTROLS, .label = "INVERT CAMERA Y", .key = "cam_invert_y",
     .value = &g_vita_opts.cam_invert_y, .names = g_menu_off_on, .count = 2,
     .on_change = controls_apply},
    {.section = MENU_CONTROLS, .label = "CAMERA SPEED", .key = "cam_speed",
     .value = &g_vita_opts.cam_speed, .min = 50, .max = 200, .step = 25, .fmt = "%d%%",
     .on_change = controls_apply},
    {.section = MENU_CONTROLS, .label = "REAR TOUCH L2/R2", .key = "reartouch",
     .value = &g_vita_opts.reartouch, .names = g_menu_off_on, .count = 2},
};
MENU_OPTION_LIST(g_menu_controls, k_rows);

void vita_pad_poll(void) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_CONTROLLERDEVICEADDED && !s_ctl)
      open_first();
    else if (e.type == SDL_CONTROLLERDEVICEREMOVED && s_ctl &&
             e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_ctl))) {
      SDL_GameControllerClose(s_ctl);
      s_ctl = NULL;
    }
  }
}

/* The buttons. game: what the game gets (in analog mode the left stick
 * isn't the d-pad, and with the free camera the right stick isn't L2/R2);
 * else what the menu gets (both sticks also move its selection). */
static uint16_t read_mask(int game) {
  uint16_t m = 0;
  size_t i;
  int x, y, analog = game && g_vita_opts.analog;
  if (!s_ctl)
    return 0;
  for (i = 0; i < sizeof k_map / sizeof k_map[0]; i++)
    if (SDL_GameControllerGetButton(s_ctl, k_map[i].b))
      m |= k_map[i].bit;
  if (SDL_GameControllerGetAxis(s_ctl, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384)
    m |= PSX_L2;
  if (SDL_GameControllerGetAxis(s_ctl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384)
    m |= PSX_R2;
  x = SDL_GameControllerGetAxis(s_ctl, SDL_CONTROLLER_AXIS_LEFTX);
  y = SDL_GameControllerGetAxis(s_ctl, SDL_CONTROLLER_AXIS_LEFTY);
  if (!analog) {
    if (x < -STICK_DEAD) m |= PSX_LEFT;
    if (x > STICK_DEAD) m |= PSX_RIGHT;
    if (y < -STICK_DEAD) m |= PSX_UP;
    if (y > STICK_DEAD) m |= PSX_DOWN;
  }
  x = SDL_GameControllerGetAxis(s_ctl, SDL_CONTROLLER_AXIS_RIGHTX);
  if (!(analog && g_vita_opts.free_camera)) {
    if (x < -STICK_DEAD) m |= PSX_L2;
    if (x > STICK_DEAD) m |= PSX_R2;
  }
  if (g_vita_opts.reartouch && SDL_GetNumTouchDevices() > 1) {
    SDL_TouchID rear = SDL_GetTouchDevice(1);
    int i, n = SDL_GetNumTouchFingers(rear);
    for (i = 0; i < n; i++) {
      SDL_Finger *f = SDL_GetTouchFinger(rear, i);
      if (f)
        m |= f->x < 0.5f ? PSX_L2 : PSX_R2;
    }
  }
  return m;
}

/* The controller now, for the port's menu (menu.c). */
uint16_t vita_pad_read(void) {
  SDL_GameControllerUpdate();
  return read_mask(0);
}

/* Both sticks in the PS1's order, or centred while s_settle runs: the game
 * takes its first analog reading as the sticks' centre (gamepad.c,
 * PadVSync -> PadCaliInit), so a stick held when the pad turns analog
 * would be read as centred from then on. */
static void read_sticks(unsigned char *sticks) {
  static const SDL_GameControllerAxis axes[4] = {
      SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, SDL_CONTROLLER_AXIS_LEFTX,
      SDL_CONTROLLER_AXIS_LEFTY};
  int i;
  for (i = 0; i < 4; i++) {
    sticks[i] = s_ctl && !s_settle ? stick_byte(SDL_GameControllerGetAxis(s_ctl, axes[i])) : 0x80;
    if (s_script_sticks[i] != 0x80 && !s_settle)
      sticks[i] = s_script_sticks[i]; /* pad= */
  }
  if (s_settle)
    s_settle--;
}

void vita_pad_vblank(void) {
  uint16_t m;
  int v = vita_vblank_count();
  while (s_next < s_nevents && s_events[s_next].vblank <= v) {
    memcpy(s_script_sticks, s_events[s_next].sticks, 4);
    s_script = s_events[s_next++].mask;
  }
  SDL_GameControllerUpdate();
  m = vita_menu_filter(read_mask(1)) | s_script; /* SELECT+START: the port's menu */
  if (m != s_last && g_vita_opts.stats > 1)
    vita_log("pad: buttons %04x", m);
  s_last = m;
  if (!g_padCommEnable)
    return;
  if (g_vita_opts.analog) {
    unsigned char sticks[4];
    read_sticks(sticks);
    fill(s_pad[0], 1, m, sticks);
  } else {
    fill(s_pad[0], 1, m, NULL);
  }
  fill(s_pad[1], 0, 0, NULL);
}

/* The PsyX_Pad_* calls LIBPAD.C makes. */
void PsyX_Pad_InitPad(int slot, unsigned char *padData) {
  if (slot < 0 || slot > 1)
    return;
  static const unsigned char centred[4] = {0x80, 0x80, 0x80, 0x80};
  s_pad[slot] = padData;
  fill(padData, slot == 0, 0, g_vita_opts.analog ? centred : NULL);
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

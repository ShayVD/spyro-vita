/* The PC layer's last direct SDL3 calls (gaps.h, D247): what Encore's
 * platform interface can't express yet. Each block names its gap; the PR
 * that added this file and docs/pc-build.md list the interface addition
 * that would retire it. Nothing else in src/platform/pc includes SDL but
 * SDL_opengl.h, for the GL types. */
#include "gaps.h"
#include "pc.h"
#include "host.h"
#include "encore/platform.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- G1, G2, G3: folders and the first window size --------------------------------------------------------------- */

static void set_env(const char *k, const char *v) {
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}

void pc_gap_user_dir(char *out, size_t cap) {
  const char *root = getenv("ENC_PC_ROOT");
  if (root && root[0]) {
    size_t n = strlen(root);
    snprintf(out, cap, "%s%s", root, root[n - 1] == '/' || root[n - 1] == '\\' ? "" : "/");
    return;
  }
  {
    /* the folder players have had since D240: spyro-vita/Spyro, not encore/spyro */
    char *pref = SDL_GetPrefPath("spyro-vita", "Spyro");
    snprintf(out, cap, "%s", pref ? pref : "./");
    SDL_free(pref);
  }
  set_env("ENC_PC_ROOT", out);
}

const char *pc_gap_base_dir(void) {
  const char *b = SDL_GetBasePath(); /* SDL's: not freed */
  return b ? b : "";
}

void pc_gap_window_size(int w, int h) {
  char v[32];
  snprintf(v, sizeof v, "%dx%d", w, h);
  set_env("ENC_PC_WINDOW", v);
}

/* ---- G4: the window's extras and the mouse pointer --------------------------------------------------------------- */

#define CURSOR_IDLE_MS 2000
static Uint64 s_moved_ms;
static float s_mx = -1, s_my = -1;
static int s_shown = 1, s_was_fs;

void pc_gap_window_setup(void *w, const char *title, int fullscreen) {
  SDL_Window *win = w;
  SDL_SetWindowTitle(win, title);
  SDL_SetWindowMinimumSize(win, 320, 240);
  if (fullscreen)
    SDL_SetWindowFullscreen(win, true); /* the backend's F11 toggles it back */
  s_moved_ms = SDL_GetTicks();
}

/* as video.c did before D247: hidden in fullscreen, shown when it moves in a
 * window, hidden again after CURSOR_IDLE_MS still. The backend takes every
 * event, so a move is a new position. */
void pc_gap_cursor_update(void *w) {
  SDL_Window *win = w;
  int fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
  Uint64 now = SDL_GetTicks();
  float x, y;
  if (fs != s_was_fs) {
    s_was_fs = fs;
    if (fs)
      SDL_HideCursor();
    else
      SDL_ShowCursor();
    s_shown = !fs;
    s_moved_ms = now;
  }
  SDL_GetMouseState(&x, &y);
  if (x != s_mx || y != s_my) {
    int first = s_mx < 0;
    s_mx = x;
    s_my = y;
    if (!first) {
      s_moved_ms = now;
      if (!s_shown && !fs) {
        SDL_ShowCursor();
        s_shown = 1;
      }
    }
  }
  if (s_shown && now - s_moved_ms > CURSOR_IDLE_MS) {
    SDL_HideCursor();
    s_shown = 0;
  }
}

/* ---- G5, G6: the video driver's name, the game thread's GL context ----------------------------------------------- */

const char *pc_gap_video_driver(void) {
  const char *d = SDL_GetCurrentVideoDriver();
  return d ? d : "none";
}

void *pc_gap_gl_shared_context(void *w, void *main_ctx) {
  SDL_Window *win = w;
  SDL_GLContext c;
  /* the backend's own attributes (3.3 core) are still set from its open */
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
  c = SDL_GL_CreateContext(win); /* current here now */
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
  if (!c)
    host_log("video: the GPU renderer's shared context: %s", SDL_GetError());
  if (!SDL_GL_MakeCurrent(win, (SDL_GLContext)main_ctx))
    host_log("video: SDL_GL_MakeCurrent: %s", SDL_GetError());
  return c;
}

int pc_gap_gl_make_current(void *w, void *ctx) {
  if (!ctx || !SDL_GL_MakeCurrent((SDL_Window *)w, (SDL_GLContext)ctx)) {
    host_log("video: no GL context for the game thread: %s", ctx ? SDL_GetError() : "none");
    return -1;
  }
  return 0;
}

int pc_gap_gl_swap_interval_on(void) {
  int i = 0;
  return SDL_GL_GetSwapInterval(&i) && i != 0;
}

/* ---- G7: the keyboard as spyro's map ------------------------------------------------------------------------------ */

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

/* the backend's built-in map (enc_pc.c's keyboard_buttons), to take back out */
static const struct {
  SDL_Scancode sc;
  uint32_t bit;
} k_backend[] = {
    {SDL_SCANCODE_UP, ENC_BTN_UP},       {SDL_SCANCODE_DOWN, ENC_BTN_DOWN},
    {SDL_SCANCODE_LEFT, ENC_BTN_LEFT},   {SDL_SCANCODE_RIGHT, ENC_BTN_RIGHT},
    {SDL_SCANCODE_Z, ENC_BTN_SOUTH},     {SDL_SCANCODE_X, ENC_BTN_EAST},
    {SDL_SCANCODE_A, ENC_BTN_WEST},      {SDL_SCANCODE_S, ENC_BTN_NORTH},
    {SDL_SCANCODE_Q, ENC_BTN_L1},        {SDL_SCANCODE_W, ENC_BTN_R1},
    {SDL_SCANCODE_1, ENC_BTN_L2},        {SDL_SCANCODE_3, ENC_BTN_R2},
    {SDL_SCANCODE_RETURN, ENC_BTN_START}, {SDL_SCANCODE_RSHIFT, ENC_BTN_SELECT},
    {SDL_SCANCODE_ESCAPE, ENC_BTN_MENU},
};

uint16_t pc_gap_keyboard_psx(void) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  uint16_t m = 0;
  for (size_t i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++)
    if (keys[k_keys[i].sc])
      m |= k_keys[i].bit;
  return m;
}

uint32_t pc_gap_backend_keyboard_bits(void) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  uint32_t m = 0;
  for (size_t i = 0; i < sizeof k_backend / sizeof k_backend[0]; i++)
    if (keys[k_backend[i].sc])
      m |= k_backend[i].bit;
  return m;
}

/* ---- G13, G14: the options menu's key, fullscreen and vsync (D266) ------------------------------------------------ */

int pc_gap_menu_key(void) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  return keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_F1];
}

/* the backend keeps its own idea of fullscreen for F11 (as with --fullscreen,
 * G4): after a change here F11 may take two presses */
void pc_gap_set_fullscreen(void *w, int on) {
  if (!SDL_SetWindowFullscreen((SDL_Window *)w, on != 0))
    host_log("video: fullscreen %d: %s", on, SDL_GetError());
}

int pc_gap_is_fullscreen(void *w) {
  return (SDL_GetWindowFlags((SDL_Window *)w) & SDL_WINDOW_FULLSCREEN) != 0;
}

int pc_gap_gl_set_swap_interval(int on) {
  if (!SDL_GL_SetSwapInterval(on ? 1 : 0)) {
    host_log("video: swap interval %d: %s", on, SDL_GetError());
    return 0;
  }
  return 1;
}

/* ---- G8: a message box -------------------------------------------------------------------------------------------- */

void pc_gap_message_box(const char *title, const char *text) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, NULL);
}

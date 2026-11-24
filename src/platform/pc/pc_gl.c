/* pc_gl.h's pointers, loaded from SDL_GL_GetProcAddress (D242). */
#define PC_GL_NO_RENAME
#include "pc_gl.h"
#include "host.h"

#include <SDL3/SDL.h>

#define PC_GL_DEF(n) __typeof__(n) *pc_##n;
PC_GL_FUNCS(PC_GL_DEF)

int pc_gl_load(void) {
  static int loaded;
  if (loaded)
    return 0;
#define PC_GL_GET(n)                                                                           \
  if (!(pc_##n = (__typeof__(pc_##n))SDL_GL_GetProcAddress(#n))) {                             \
    host_log("gpu: no %s", #n);                                                                \
    return -1;                                                                                 \
  }
  PC_GL_FUNCS(PC_GL_GET)
  loaded = 1;
  return 0;
}

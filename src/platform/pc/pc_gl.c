/* pc_gl.h's pointers, loaded through the platform's GL loader (EncGpu.get_proc,
 * D245; SDL_GL_GetProcAddress underneath) (D243). */
#define PC_GL_NO_RENAME
#include "pc_gl.h"
#include "host.h"

#include "pc.h"

#define PC_GL_DEF(n) __typeof__(n) *pc_##n;
PC_GL_FUNCS(PC_GL_DEF)

int pc_gl_load(void) {
  static int loaded;
  if (loaded)
    return 0;
  if (!g_pc_gl_proc)
    return -1;
#define PC_GL_GET(n)                                                                           \
  if (!(pc_##n = (__typeof__(pc_##n))g_pc_gl_proc(#n))) {                             \
    host_log("gpu: no %s", #n);                                                                \
    return -1;                                                                                 \
  }
  PC_GL_FUNCS(PC_GL_GET)
  loaded = 1;
  return 0;
}

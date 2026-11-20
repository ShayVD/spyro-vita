/* Work counters (issue #160, D131): perfcount.c, over vita-port-kit's
 * vita/runtime/perfcount. VPC_* index g_vpc, the kit's ids. */
#ifndef VITA_PC_H
#define VITA_PC_H

#include "../../../third_party/vita-port-kit/vita/runtime/perfcount/vpk_perfcount.h"

enum {
  VPC_GL_DRAWS,      /* glDrawArrays calls */
  VPC_GL_TRIS,       /* ... their triangles */
  VPC_GR_DRAWS,      /* the game's draws (GR_DrawTriangles), before joining */
  VPC_GR_TRIS,       /* ... their triangles */
  VPC_GL_CLEARS,     /* glClear */
  VPC_TEX_UPLOADS,   /* glTexImage2D with data */
  VPC_TEX_BYTES,     /* ... its bytes */
  VPC_VBO_UPLOADS,   /* glBufferData */
  VPC_VBO_BYTES,     /* ... its bytes */
  VPC_GL_READS,      /* glReadPixels */
  VPC_GL_STATE,      /* state changes: the next three, glEnable/glDisable, blend, scissor,
                        viewport, texture parameters */
  VPC_GL_PROGRAMS,   /* glUseProgram */
  VPC_GL_TEX_BINDS,  /* glBindTexture */
  VPC_GL_FBO_BINDS,  /* glBindFramebuffer */
  VPC_GL_UNIFORMS,   /* glUniform* */
  VPC_GL_ATTRIBS,    /* vertex attributes set, enabled, disabled; buffers bound */
  VPC_OT_PACKETS,    /* OT packets walked (gpu_walk.c), every walk's */
  VPC_VC_PROJ,       /* vertex cache (persp/subpixel, D39): projections recorded */
  VPC_VC_POLYS,      /* ... polygons looked up */
  VPC_VC_VERTS,      /* ... their vertices */
  VPC_VC_MISSES,     /* ... of those not found */
  VPC_PRES_FRAMES,   /* the presenter's in-between frames (60 fps) */
  VPC_VC_DIV,        /* vertex cache's cost (D134), the game thread's: float divisions, records and
                        lookups added */
  VPC_VC_PROBE,      /* ... the table's slots probed */
  VPC_VC_LINES,      /* ... the table's 32-byte lines touched (a model) */
  VPC_VCP_DIV,       /* the same three, the presenter's thread's (D133) */
  VPC_VCP_PROBE,
  VPC_VCP_LINES,
  VPC_COUNT          /* at most VPK_PC_MAX (32) */
};
extern int g_vpc[VPC_COUNT];

/* gpu_gl.c's counters: plain adds into running totals, which perfcount.c
 * pulls once a vblank (as it does the shared code's). Whoever holds the GL
 * makes the GL calls (the render thread, or the game thread without one,
 * never both at once: gpu_gl.c's own s_st is kept the same way), so no
 * atomic is needed on these paths. */
extern unsigned g_vpc_gl[VPC_COUNT];
static inline void vita_pc(int c, unsigned n) { g_vpc_gl[c] += n; }

void vita_pc_init(void);          /* main.c, after config.txt */
void vita_pc_vblank(int vblank);  /* vblank.c, once a vblank on the game thread */
void vita_pc_flush(void);         /* main.c's vita_exit: the last part window */

/* VITA_PC_GL (gpu_gl.c, after vitaGL.h): the GL calls below count
 * themselves; the call sites stay as they were. Each is the function of the
 * same name with one or two plain adds before it (a function-like name inside its
 * own replacement isn't replaced again). */
#ifdef VITA_PC_GL
static inline void vita_pc_state(int c) {
  vita_pc(VPC_GL_STATE, 1);
  if (c >= 0)
    vita_pc(c, 1);
}
static inline unsigned vita_pc_px(GLenum format, GLenum type) {
  if (type != GL_UNSIGNED_BYTE)
    return 2; /* the 16-bit types */
  return format == GL_RGBA ? 4 : format == GL_RGB ? 3 : 1;
}
#define glDrawArrays(m, f, n)                                                                      \
  (vita_pc(VPC_GL_DRAWS, 1), vita_pc(VPC_GL_TRIS, (m) == GL_TRIANGLES ? (unsigned)(n) / 3 : 0),   \
   glDrawArrays(m, f, n))
#define glClear(m) (vita_pc(VPC_GL_CLEARS, 1), glClear(m))
static inline void vita_pc_tex(const void *data, GLsizei w, GLsizei h, GLenum format, GLenum type) {
  if (!data)
    return; /* storage only */
  vita_pc(VPC_TEX_UPLOADS, 1);
  vita_pc(VPC_TEX_BYTES, (unsigned)w * (unsigned)h * vita_pc_px(format, type));
}
#define glTexImage2D(t, l, i, w, h, b, f, ty, d)                                                   \
  (vita_pc_tex(d, w, h, f, ty), glTexImage2D(t, l, i, w, h, b, f, ty, d))
#define glBufferData(t, s, d, u)                                                                   \
  (vita_pc(VPC_VBO_UPLOADS, 1), vita_pc(VPC_VBO_BYTES, (unsigned)(s)), glBufferData(t, s, d, u))
#define glReadPixels(x, y, w, h, f, t, d) (vita_pc(VPC_GL_READS, 1), glReadPixels(x, y, w, h, f, t, d))
#define glUseProgram(p) (vita_pc_state(VPC_GL_PROGRAMS), glUseProgram(p))
#define glBindTexture(t, x) (vita_pc_state(VPC_GL_TEX_BINDS), glBindTexture(t, x))
#define glBindFramebuffer(t, x) (vita_pc_state(VPC_GL_FBO_BINDS), glBindFramebuffer(t, x))
#define glEnable(c) (vita_pc_state(-1), glEnable(c))
#define glDisable(c) (vita_pc_state(-1), glDisable(c))
#define glBlendFunc(s, d) (vita_pc_state(-1), glBlendFunc(s, d))
#define glBlendEquation(m) (vita_pc_state(-1), glBlendEquation(m))
#define glScissor(x, y, w, h) (vita_pc_state(-1), glScissor(x, y, w, h))
#define glViewport(x, y, w, h) (vita_pc_state(-1), glViewport(x, y, w, h))
#define glTexParameteri(t, p, v) (vita_pc_state(-1), glTexParameteri(t, p, v))
#define glUniform1i(l, a) (vita_pc(VPC_GL_UNIFORMS, 1), glUniform1i(l, a))
#define glUniform3f(l, a, b, c) (vita_pc(VPC_GL_UNIFORMS, 1), glUniform3f(l, a, b, c))
#define glUniform4f(l, a, b, c, d) (vita_pc(VPC_GL_UNIFORMS, 1), glUniform4f(l, a, b, c, d))
#define glVertexAttribPointer(i, s, t, n, st, p)                                                   \
  (vita_pc(VPC_GL_ATTRIBS, 1), glVertexAttribPointer(i, s, t, n, st, p))
#define glEnableVertexAttribArray(i) (vita_pc(VPC_GL_ATTRIBS, 1), glEnableVertexAttribArray(i))
#define glDisableVertexAttribArray(i) (vita_pc(VPC_GL_ATTRIBS, 1), glDisableVertexAttribArray(i))
#define glBindBuffer(t, b) (vita_pc(VPC_GL_ATTRIBS, 1), glBindBuffer(t, b))
#endif

#endif

/* The GL 3.3 core calls of the GPU renderer (gpu_gl.c, D243), all from
 * the platform's loader (EncGpu.get_proc, D247; SDL_GL_GetProcAddress
 * underneath): the executable links no GL library (SDL loads it).
 * Including this file renames glFoo to the pointer pc_glFoo, so gpu_gl.c
 * reads as plain GL. The pointers are the same for both of the build's
 * contexts (one driver, one pixel format), so they are loaded once. */
#ifndef SPYRO_PC_GL_H
#define SPYRO_PC_GL_H

#define GL_GLEXT_PROTOTYPES 1 /* the prototypes, for __typeof__ (never called by name) */
#include <SDL3/SDL_opengl.h> /* the GL types and prototypes only: no SDL call (D247, gap G10) */

#define PC_GL_FUNCS(X)                                                                         \
  X(glGetString) X(glGetError) X(glGenTextures) X(glDeleteTextures) X(glBindTexture)           \
  X(glTexParameteri) X(glTexImage2D) X(glTexSubImage2D) X(glActiveTexture) X(glPixelStorei)    \
  X(glGenFramebuffers) X(glDeleteFramebuffers) X(glBindFramebuffer) X(glFramebufferTexture2D) \
  X(glCheckFramebufferStatus) X(glViewport) X(glScissor) X(glEnable) X(glDisable) X(glClear)   \
  X(glClearColor) X(glBlendEquation) X(glBlendFunc) X(glDrawArrays) X(glReadPixels)          \
  X(glGenBuffers) X(glBindBuffer) X(glBufferData) X(glEnableVertexAttribArray)                \
  X(glVertexAttribPointer) X(glGenVertexArrays) X(glBindVertexArray) X(glCreateShader)        \
  X(glShaderSource) X(glCompileShader) X(glGetShaderiv) X(glGetShaderInfoLog)                 \
  X(glDeleteShader) X(glCreateProgram) X(glAttachShader) X(glBindAttribLocation)              \
  X(glLinkProgram) X(glGetProgramiv) X(glGetProgramInfoLog) X(glUseProgram)                   \
  X(glGetUniformLocation) X(glUniform1i) X(glUniform3f) X(glUniform4f) X(glFenceSync)         \
  X(glWaitSync) X(glDeleteSync) X(glFlush) X(glFinish)

#define PC_GL_EXTERN(n) extern __typeof__(n) *pc_##n;
PC_GL_FUNCS(PC_GL_EXTERN)
#undef PC_GL_EXTERN

int pc_gl_load(void); /* 0 = ok; with a context current */

#ifndef PC_GL_NO_RENAME /* pc_gl.c: the real names */
#define glGetString pc_glGetString
#define glGetError pc_glGetError
#define glGenTextures pc_glGenTextures
#define glDeleteTextures pc_glDeleteTextures
#define glBindTexture pc_glBindTexture
#define glTexParameteri pc_glTexParameteri
#define glTexImage2D pc_glTexImage2D
#define glTexSubImage2D pc_glTexSubImage2D
#define glActiveTexture pc_glActiveTexture
#define glPixelStorei pc_glPixelStorei
#define glGenFramebuffers pc_glGenFramebuffers
#define glDeleteFramebuffers pc_glDeleteFramebuffers
#define glBindFramebuffer pc_glBindFramebuffer
#define glFramebufferTexture2D pc_glFramebufferTexture2D
#define glCheckFramebufferStatus pc_glCheckFramebufferStatus
#define glViewport pc_glViewport
#define glScissor pc_glScissor
#define glEnable pc_glEnable
#define glDisable pc_glDisable
#define glClear pc_glClear
#define glClearColor pc_glClearColor
#define glBlendEquation pc_glBlendEquation
#define glBlendFunc pc_glBlendFunc
#define glDrawArrays pc_glDrawArrays
#define glReadPixels pc_glReadPixels
#define glGenBuffers pc_glGenBuffers
#define glBindBuffer pc_glBindBuffer
#define glBufferData pc_glBufferData
#define glEnableVertexAttribArray pc_glEnableVertexAttribArray
#define glVertexAttribPointer pc_glVertexAttribPointer
#define glGenVertexArrays pc_glGenVertexArrays
#define glBindVertexArray pc_glBindVertexArray
#define glCreateShader pc_glCreateShader
#define glShaderSource pc_glShaderSource
#define glCompileShader pc_glCompileShader
#define glGetShaderiv pc_glGetShaderiv
#define glGetShaderInfoLog pc_glGetShaderInfoLog
#define glDeleteShader pc_glDeleteShader
#define glCreateProgram pc_glCreateProgram
#define glAttachShader pc_glAttachShader
#define glBindAttribLocation pc_glBindAttribLocation
#define glLinkProgram pc_glLinkProgram
#define glGetProgramiv pc_glGetProgramiv
#define glGetProgramInfoLog pc_glGetProgramInfoLog
#define glUseProgram pc_glUseProgram
#define glGetUniformLocation pc_glGetUniformLocation
#define glUniform1i pc_glUniform1i
#define glUniform3f pc_glUniform3f
#define glUniform4f pc_glUniform4f
#define glFenceSync pc_glFenceSync
#define glWaitSync pc_glWaitSync
#define glDeleteSync pc_glDeleteSync
#define glFlush pc_glFlush
#define glFinish pc_glFinish
#endif

#endif

/* The GLES2 renderer on Piglet (docs/ps4-plan.md P4, decisions D193,
 * D194): the PS1 picture drawn by the GPU at 1080p.
 *
 * The modules. The player's own libScePigletv2VSH.sprx and
 * libSceShaccVSH.sprx, copied to /data/self/system/common/lib/ (the retail
 * firmware's Piglet has no run-time shader compiler; the pair must match).
 * Loaded with sceKernelLoadStartModule, Piglet first, then the compiler from
 * the same folder, before any egl or gl call: the eboot's imports of
 * libScePigletv2VSH (OpenOrbis's stub) then bind to the loaded copy, and
 * Piglet finds the compiler in the process by itself, so plain
 * glShaderSource/glCompileShader work (2Ship2Harkinian-PS4's loader does
 * the same; its INSTALL.md is what the player follows). We never ship or
 * fetch those files. When they're missing main.c keeps the software
 * renderer and says why: sceKernelLoadStartModule's 0x80020002 is "no such
 * file", 0x8002000D a damaged one.
 *
 * The design (simpler than the Vita's gpu_gl.c, D35, which has to read the
 * GPU back by replaying logs): the software rasterizer (linux32's
 * gpu_soft.c, soft_ names; gr_ps4.c calls it first for every GR_* call)
 * still draws everything into the CPU's VRAM, so StoreImage, the save
 * states, textures drawn by the game and the shots read the PS1's exact
 * pixels, and nothing is ever read back from the GPU. The GPU draws the
 * same splits again:
 *   - s_vram_tex is that VRAM (512x512 RGBA8, two 16-bit words a texel),
 *     uploaded by rows when a draw is about to sample VRAM that changed
 *     since the last upload (a texture page or CLUT the dirty rect
 *     overlaps). The shaders decode the words themselves (4/8-bit CLUT,
 *     15-bit, texel 0 transparent, STP): the Vita's (gpu_gl.c, ours).
 *   - Each draw area the game draws into (its clip rect) is a target: an
 *     FBO covering that VRAM rect at 1080p's scale (3.75 x 4.82: 512 x 224
 *     drawn lines -> 1920 x 1080). A target is made from VRAM, kept in step
 *     with the transfers (ClearImage's colour, LoadImage/MoveImage's pixels
 *     from VRAM), and remade from VRAM when another target's draws wrote
 *     over it.
 *   - The presenter's frames (widescreen, 60 fps; g_present_capture) are
 *     copied from their target into slot textures; the present shows the
 *     slot due, or the PS1 display rect (from its target, or from VRAM:
 *     logos, loading screens, the movies' 24-bit mode on the CPU), 16:9
 *     across the screen or 4:3 in the middle, and the menu's panel over it.
 * Splits with the same state and following vertices are joined into one
 * draw (the Vita's gpu_join, D74). Semi-transparency as the PS1: only STP
 * texels blend (one pass, ONE / ONE_MINUS_SRC_ALPHA, for average, add and
 * quarter; two for subtract). Not modelled (as gpu_soft.c): mask bit,
 * texture window, dithering. */
#include "ps4.h"
#include "host.h"
#include "../../game/present.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orbis/libkernel.h>
#include <orbis/Pigletv2VSH.h> /* EGL and GLES2 too */

#include "psx_gpu.h"

#define LIB_DIR "/data/self/system/common/lib"
#define VW 1024
#define VH 512
#define SCREEN_W 1920
#define SCREEN_H 1080
#define SX (1920.0f / 512.0f)
#define SY (1080.0f / 224.0f)
#define MAX_TARGETS 8
#define NSLOTS (PRESENT_SLOTS + 1) /* the slots and the staging texture */
#define STAGE PRESENT_SLOTS

uint16_t *soft_host_vram(void); /* gpu_soft.c (gr_ps4.c's soft_ names) */
extern DISPENV currentDispEnv;
extern int g_GPUDisabledState;
uint8_t *host_display_rgb(int *pw, int *ph); /* linux32/png.c */

int g_ps4_gpu;

static int s_piglet = -1, s_shacc = -1;

/* ---- the modules ---- */

static int load(const char *name, int *handle, char *why, int whylen) {
  char path[256];
  int start = 0, h;
  snprintf(path, sizeof path, LIB_DIR "/%s", name);
  h = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &start);
  host_log("gpu: sceKernelLoadStartModule(%s): 0x%x, module_start %d", path, (unsigned)h, start);
  host_log_flush();
  if (h < 0) {
    snprintf(why, (size_t)whylen, "%s %s (0x%x); see the README for the GPU renderer",
             (unsigned)h == 0x80020002u ? "no" : "can't load", path, (unsigned)h);
    return -1;
  }
  *handle = h;
  return 0;
}

int ps4_gpu_load(char *why, int whylen) {
  if (s_piglet >= 0)
    return 0;
  if (load("libScePigletv2VSH.sprx", &s_piglet, why, whylen) != 0)
    return -1;
  if (load("libSceShaccVSH.sprx", &s_shacc, why, whylen) != 0)
    return -1;
  return 0;
}

/* ---- state ---- */

typedef struct {
  int x, y, w, h; /* VRAM rect */
  int pw, ph;     /* texture size */
  GLuint tex, fbo;
  int stale;      /* VRAM changed under it (another target's draws): remade before use */
  unsigned used;
} Target;

enum { P_UNTEX, P_TEX, P_VRAM, P_BLIT, NPROG };
typedef struct {
  GLuint prog;
  GLint u_xf, u_semi, s_vram, s_tex;
} Prog;

static EGLDisplay s_dpy = EGL_NO_DISPLAY;
static EGLSurface s_surf = EGL_NO_SURFACE;
static EGLContext s_ctx = EGL_NO_CONTEXT;
static Prog s_prog[NPROG];
static GLuint s_vram_tex, s_vbo, s_qbo, s_ovl_tex;
static int s_ovl_w, s_ovl_h;
static Target s_t[MAX_TARGETS];
static int s_nt;
static unsigned s_clock;
static GLuint s_slot_tex[NSLOTS], s_slot_fbo[NSLOTS];
static int s_slot_lines[NSLOTS], s_slot_w[NSLOTS], s_slot_valid[NSLOTS];
static int s_slot_pw, s_slot_ph; /* the slot textures' size */
static GLuint s_disp_tex;        /* the 24-bit display, converted on the CPU */
static GLuint s_bound_fbo = (GLuint)-1;
static int s_cur_prog = -1;

/* VRAM changed since the last upload: the rect's rows are uploaded before
 * a draw samples inside it (x0 > x1: clean) */
static int s_dx0 = 0, s_dy0 = 0, s_dx1 = VW - 1, s_dy1 = VH - 1;

/* the walk's current split state (GR_SetupClipMode, GR_SetTexture, ...) */
static const GrVertex *s_verts;
static int s_nverts, s_vbo_ok;
static int s_textured, s_blend;
static int s_org_x, s_org_y;
static int s_cx0, s_cy0, s_cx1, s_cy1; /* the clip rect, inclusive */
static Target *s_cur;

/* a joined draw waiting: its state and vertices */
static struct {
  int n, start, count, textured, blend, org_x, org_y, cx0, cy0, cx1, cy1;
  Target *t;
} s_pend;

static struct {
  unsigned splits, draws, uploads, upload_rows, inits, copies, clears, captures, presents;
  unsigned long long swap_us, present_us;
  unsigned errors, last_error;
} s_st;

/* ---- shaders (the Vita's, gpu_gl.c; GLSL ES 1.00 with no #version line,
 * as Piglet wants; highp: the VRAM words need 16 bits) ---- */

#define FS_HEAD                                                                \
  "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"                                        \
  "precision highp float;\n"                                                   \
  "#else\n"                                                                    \
  "precision mediump float;\n"                                                 \
  "#endif\n"
#define GLSL_MD                                                                \
  "float md(float qv, float qn, float qinv) { return qv - qn * floor(qv * qinv); }\n"

static const char k_vs_prim[] =
    "attribute vec4 a_pos;\n" /* x, y, tpage, clut (shorts) */
    "attribute vec4 a_tex;\n" /* u, v, bright, dither (bytes) */
    "attribute vec4 a_col;\n" /* r, g, b, a (normalised) */
    "uniform vec4 u_xf;\n"    /* xy: VRAM offset into the target, zw: 2 * scale / size */
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_pc;\n" /* page x, y; clut x, y (VRAM words) */
    "varying float v_fmt;\n"
    GLSL_MD
    "void main() {\n"
    "  gl_Position = vec4((a_pos.xy + u_xf.xy) * u_xf.zw - 1.0, 0.0, 1.0);\n"
    "  v_uv = a_tex.xy;\n"
    "  v_col = a_col;\n"
    "  float qpage = md(a_pos.z, 65536.0, 1.0 / 65536.0);\n"
    "  float qclut = md(a_pos.w, 65536.0, 1.0 / 65536.0);\n"
    "  v_fmt = md(floor(qpage * (1.0 / 128.0)), 4.0, 0.25);\n"
    "  v_pc = vec4(md(qpage, 16.0, 0.0625) * 64.0, md(floor(qpage * 0.0625), 2.0, 0.5) * 256.0,\n"
    "              md(qclut, 64.0, 0.015625) * 16.0, md(floor(qclut * 0.015625), 512.0, 1.0 / 512.0));\n"
    "}\n";

/* VRAM word (qx, qy) as its low and high byte, 0..255 */
#define FS_VRAM                                                                \
  GLSL_MD                                                                      \
  "uniform sampler2D s_vram;\n"                                                \
  "vec2 vram(float qx, float qy) {\n"                                          \
  "  float qwx = md(qx, 1024.0, 1.0 / 1024.0);\n"                              \
  "  float qwy = md(qy, 512.0, 1.0 / 512.0);\n"                                \
  "  float qhalf = floor(qwx * 0.5);\n"                                        \
  "  vec4 qtex = texture2D(s_vram, vec2(qhalf + 0.5, qwy + 0.5) * (1.0 / 512.0));\n" \
  "  vec2 qbytes = mix(qtex.xy, qtex.zw, qwx - qhalf * 2.0);\n"                \
  "  return floor(qbytes * 255.0 + 0.5);\n"                                    \
  "}\n"                                                                        \
  "vec3 rgb15(vec2 qc) {\n"                                                    \
  "  float qgreen = floor(qc.x * 0.03125) + md(qc.y, 4.0, 0.25) * 8.0;\n"      \
  "  return vec3(md(qc.x, 32.0, 0.03125), qgreen, md(floor(qc.y * 0.25), 32.0, 0.03125)) * (8.0 / 255.0);\n" \
  "}\n"

/* u_semi: x = colour scale and y = alpha for blended pixels (the blend is
 * ONE, ONE_MINUS_SRC_ALPHA); z = 1: draw only STP=0 texels, 2: only STP=1 */
static const char k_fs_untex[] =
    FS_HEAD
    "varying vec4 v_col;\n"
    "uniform vec3 u_semi;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(v_col.xyz * u_semi.x, u_semi.y);\n"
    "}\n";

/* the texel's format from its page (per triangle, as gpu_soft.c) */
static const char k_fs_tex[] =
    FS_HEAD
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_pc;\n"
    "varying float v_fmt;\n"
    "uniform vec3 u_semi;\n"
    FS_VRAM
    "void main() {\n"
    "  vec2 quv = floor(v_uv + 0.002);\n"
    "  quv = quv - 256.0 * floor(quv * (1.0 / 256.0));\n"
    "  vec4 qpc = floor(v_pc + 0.5);\n"
    "  vec2 qc;\n"
    "  if (v_fmt < 0.5) {\n"
    "    vec2 qword = vram(qpc.x + floor(quv.x * 0.25), qpc.y + quv.y);\n"
    "    float qn = md(quv.x, 4.0, 0.25);\n"
    "    float qbyte = mix(qword.x, qword.y, step(2.0, qn));\n"
    "    float qhi = floor(qbyte * 0.0625);\n"
    "    float qidx = mix(qbyte - qhi * 16.0, qhi, md(qn, 2.0, 0.5));\n"
    "    qc = vram(qpc.z + qidx, qpc.w);\n"
    "  } else if (v_fmt < 1.5) {\n"
    "    vec2 qword = vram(qpc.x + floor(quv.x * 0.5), qpc.y + quv.y);\n"
    "    qc = vram(qpc.z + mix(qword.x, qword.y, md(quv.x, 2.0, 0.5)), qpc.w);\n"
    "  } else {\n"
    "    qc = vram(qpc.x + quv.x, qpc.y + quv.y);\n"
    "  }\n"
    "  if (qc.x + qc.y == 0.0) discard;\n"
    "  float qstp = step(128.0, qc.y);\n"
    "  if ((u_semi.z == 1.0 && qstp > 0.5) || (u_semi.z == 2.0 && qstp < 0.5)) discard;\n"
    "  vec3 qrgb = rgb15(qc) * v_col.xyz * (255.0 / 128.0);\n"
    "  gl_FragColor = mix(vec4(qrgb, 1.0), vec4(qrgb * u_semi.x, u_semi.y), qstp);\n"
    "}\n";

/* quads: a_xy in clip space, a_st texture (P_BLIT) or VRAM words (P_VRAM) */
static const char k_vs_quad[] =
    "attribute vec2 a_xy;\n"
    "attribute vec2 a_st;\n"
    "varying vec2 v_st;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_xy, 0.0, 1.0);\n"
    "  v_st = a_st;\n"
    "}\n";

static const char k_fs_vram[] =
    FS_HEAD
    "varying vec2 v_st;\n"
    FS_VRAM
    "void main() {\n"
    "  gl_FragColor = vec4(rgb15(vram(floor(v_st.x), floor(v_st.y))), 1.0);\n"
    "}\n";

static const char k_fs_blit[] =
    FS_HEAD
    "varying vec2 v_st;\n"
    "uniform sampler2D s_tex;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(texture2D(s_tex, v_st).xyz, 1.0);\n"
    "}\n";

static void gl_check(const char *where) {
  GLenum e = glGetError();
  if (e == GL_NO_ERROR)
    return;
  if (s_st.errors++ < 8)
    host_log("gpu: GL error 0x%x at %s", (unsigned)e, where);
  s_st.last_error = e;
}

static GLuint compile(GLenum type, const char *src, const char *what) {
  GLuint s = glCreateShader(type);
  GLint ok = 0;
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024] = "";
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    host_log("gpu: %s didn't compile: %s", what, log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

static int link(Prog *p, const char *vs, const char *fs, int quad, const char *what) {
  unsigned long long t0 = ps4_now_us();
  GLuint v = compile(GL_VERTEX_SHADER, vs, what), f = v ? compile(GL_FRAGMENT_SHADER, fs, what) : 0;
  GLint ok = 0;
  if (!v || !f)
    return -1;
  p->prog = glCreateProgram();
  glAttachShader(p->prog, v);
  glAttachShader(p->prog, f);
  if (quad) {
    glBindAttribLocation(p->prog, 0, "a_xy");
    glBindAttribLocation(p->prog, 1, "a_st");
  } else {
    glBindAttribLocation(p->prog, 0, "a_pos");
    glBindAttribLocation(p->prog, 1, "a_tex");
    glBindAttribLocation(p->prog, 2, "a_col");
  }
  glLinkProgram(p->prog);
  glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
  glDeleteShader(v);
  glDeleteShader(f);
  if (!ok) {
    char log[1024] = "";
    glGetProgramInfoLog(p->prog, sizeof log, NULL, log);
    host_log("gpu: %s didn't link: %s", what, log);
    return -1;
  }
  p->u_xf = glGetUniformLocation(p->prog, "u_xf");
  p->u_semi = glGetUniformLocation(p->prog, "u_semi");
  p->s_vram = glGetUniformLocation(p->prog, "s_vram");
  p->s_tex = glGetUniformLocation(p->prog, "s_tex");
  glUseProgram(p->prog);
  if (p->s_vram >= 0)
    glUniform1i(p->s_vram, 0); /* texture unit 0: VRAM */
  if (p->s_tex >= 0)
    glUniform1i(p->s_tex, 1); /* unit 1: a target, a slot, the overlay */
  host_log("gpu: %s compiled and linked in %llu ms", what, (ps4_now_us() - t0) / 1000);
  return 0;
}

/* ---- GL helpers ---- */

static void use(int p) {
  if (s_cur_prog != p) {
    glUseProgram(s_prog[p].prog);
    s_cur_prog = p;
  }
}

static void bind_fbo(GLuint fbo) {
  if (s_bound_fbo != fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    s_bound_fbo = fbo;
  }
}

static GLuint new_tex(int w, int h, GLenum fmt, int linear) {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

static GLuint new_fbo(GLuint tex) {
  GLuint f;
  GLenum st;
  glGenFramebuffers(1, &f);
  bind_fbo(f);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE)
    host_log("gpu: framebuffer incomplete: 0x%x", (unsigned)st);
  return f;
}

/* A quad: dst in clip space (x0, y0, x1, y1), st its texture or VRAM
 * coordinates at those corners. Program and target already chosen. */
static void quad(const float dst[4], const float st[4]) {
  const float v[16] = {dst[0], dst[1], st[0], st[1], dst[2], dst[1], st[2], st[1],
                       dst[0], dst[3], st[0], st[3], dst[2], dst[3], st[2], st[3]};
  glBindBuffer(GL_ARRAY_BUFFER, s_qbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (const void *)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (const void *)8);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  s_vbo_ok = 0; /* the prim attributes must be set again */
}

static void no_blend(void) {
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
}

/* ---- VRAM ---- */

static void dirty(int x, int y, int w, int h) {
  int x1 = x + w - 1, y1 = y + h - 1;
  if (w <= 0 || h <= 0)
    return;
  if (x < 0 || x1 >= VW) /* (wrapping: all of it) */
    x = 0, x1 = VW - 1;
  if (y < 0 || y1 >= VH)
    y = 0, y1 = VH - 1;
  if (s_dx0 > s_dx1) {
    s_dx0 = x, s_dy0 = y, s_dx1 = x1, s_dy1 = y1;
    return;
  }
  if (x < s_dx0) s_dx0 = x;
  if (y < s_dy0) s_dy0 = y;
  if (x1 > s_dx1) s_dx1 = x1;
  if (y1 > s_dy1) s_dy1 = y1;
}

static int dirty_hits(int x, int y, int w, int h) {
  return s_dx0 <= s_dx1 && x <= s_dx1 && x + w - 1 >= s_dx0 && y <= s_dy1 && y + h - 1 >= s_dy0;
}

static void flush(void);

/* the VRAM texture gets the dirty rows */
static void upload(void) {
  const uint16_t *v = soft_host_vram();
  if (s_dx0 > s_dx1)
    return;
  flush();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, s_dy0, VW / 2, s_dy1 - s_dy0 + 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  v + (size_t)s_dy0 * VW);
  s_st.uploads++;
  s_st.upload_rows += (unsigned)(s_dy1 - s_dy0 + 1);
  s_dx0 = 1, s_dx1 = 0;
}

static void sync_rect(int x, int y, int w, int h) {
  if (dirty_hits(x, y, w, h))
    upload();
}

/* ---- targets ---- */

static void target_viewport(const Target *t) {
  bind_fbo(t->fbo);
  glViewport(0, 0, t->pw, t->ph);
}

/* VRAM rect (x, y, w, h) inside t, as t's own clip space */
static void target_clip(const Target *t, int x, int y, int w, int h, float out[4]) {
  out[0] = (x - t->x) * SX / t->pw * 2 - 1;
  out[1] = (y - t->y) * SY / t->ph * 2 - 1;
  out[2] = (x + w - t->x) * SX / t->pw * 2 - 1;
  out[3] = (y + h - t->y) * SY / t->ph * 2 - 1;
}

/* VRAM's pixels into t's rect (x, y, w, h), clipped to t */
static void copy_into(Target *t, int x, int y, int w, int h) {
  float dst[4], st[4];
  int x1 = x + w, y1 = y + h;
  if (x < t->x) x = t->x;
  if (y < t->y) y = t->y;
  if (x1 > t->x + t->w) x1 = t->x + t->w;
  if (y1 > t->y + t->h) y1 = t->y + t->h;
  if (x >= x1 || y >= y1)
    return;
  sync_rect(x, y, x1 - x, y1 - y);
  flush();
  target_viewport(t);
  no_blend();
  use(P_VRAM);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  target_clip(t, x, y, x1 - x, y1 - y, dst);
  st[0] = (float)x, st[1] = (float)y, st[2] = (float)x1, st[3] = (float)y1;
  quad(dst, st);
  s_st.copies++;
}

static void target_free(Target *t) {
  glDeleteFramebuffers(1, &t->fbo);
  glDeleteTextures(1, &t->tex);
  if (s_bound_fbo == t->fbo)
    s_bound_fbo = (GLuint)-1;
  *t = s_t[--s_nt];
}

static int rect_holds(int ox, int oy, int ow, int oh, int x, int y, int w, int h) {
  return x >= ox && y >= oy && x + w <= ox + ow && y + h <= oy + oh;
}

static int contains(const Target *t, int x, int y, int w, int h) {
  return rect_holds(t->x, t->y, t->w, t->h, x, y, w, h);
}

static int overlaps(const Target *t, int x, int y, int w, int h) {
  return x < t->x + t->w && t->x < x + w && y < t->y + t->h && t->y < y + h;
}

/* the target the clip rect (x, y, w, h) draws into: one that holds it, or a
 * new one (which replaces the targets it holds) */
static Target *target_for(int x, int y, int w, int h) {
  Target *t;
  int i;
  for (i = 0; i < s_nt; i++)
    if (contains(&s_t[i], x, y, w, h)) {
      t = &s_t[i];
      t->used = ++s_clock;
      if (t->stale) {
        t->stale = 0;
        copy_into(t, t->x, t->y, t->w, t->h);
      }
      return t;
    }
  flush();
  for (i = 0; i < s_nt;)
    if (rect_holds(x, y, w, h, s_t[i].x, s_t[i].y, s_t[i].w, s_t[i].h))
      target_free(&s_t[i]);
    else
      i++;
  if (s_nt == MAX_TARGETS) { /* the least used goes: VRAM has its pixels */
    int lru = 0;
    for (i = 1; i < s_nt; i++)
      if (s_t[i].used < s_t[lru].used)
        lru = i;
    target_free(&s_t[lru]);
  }
  t = &s_t[s_nt++];
  memset(t, 0, sizeof *t);
  t->x = x, t->y = y, t->w = w, t->h = h;
  t->pw = (int)(w * SX + 0.999f);
  t->ph = (int)(h * SY + 0.999f);
  t->tex = new_tex(t->pw, t->ph, GL_RGBA, 0);
  t->fbo = new_fbo(t->tex);
  t->used = ++s_clock;
  host_log("gpu: target %d,%d %dx%d at %dx%d (%d targets)", x, y, w, h, t->pw, t->ph, s_nt);
  copy_into(t, x, y, w, h);
  s_st.inits++;
  return t;
}

/* t was drawn in (x, y, w, h): VRAM changed there, and any other target
 * holding part of it must be remade from VRAM before its next use */
static void drawn(const Target *t, int x, int y, int w, int h) {
  int i;
  dirty(x, y, w, h);
  for (i = 0; i < s_nt; i++)
    if (&s_t[i] != t && overlaps(&s_t[i], x, y, w, h))
      s_t[i].stale = 1;
}

/* ---- the GR_* calls (gr_ps4.c calls them after gpu_soft.c's) ---- */

typedef struct {
  DRAWENV drawenv;
  DISPENV dispenv;
} SplitHead; /* PsxGpuSplit starts so (psx_gpu.h; gpu_soft.c) */

void gles_GR_SetupClipMode(const RECT16 *rect, int enable) {
  const SplitHead *sp = (const SplitHead *)rect;
  if (enable) {
    s_org_x = sp->dispenv.disp.x;
    s_org_y = sp->dispenv.disp.y;
  } else {
    s_org_x = sp->drawenv.ofs[0];
    s_org_y = sp->drawenv.ofs[1];
  }
  s_cx0 = rect->x < 0 ? 0 : rect->x;
  s_cy0 = rect->y < 0 ? 0 : rect->y;
  s_cx1 = rect->x + rect->w - 1;
  s_cy1 = rect->y + rect->h - 1;
  if (s_cx1 > VW - 1)
    s_cx1 = VW - 1;
  if (s_cy1 > VH - 1)
    s_cy1 = VH - 1;
  s_cur = s_cx0 <= s_cx1 && s_cy0 <= s_cy1
              ? target_for(s_cx0, s_cy0, s_cx1 - s_cx0 + 1, s_cy1 - s_cy0 + 1)
              : NULL;
}

void gles_GR_SetTexture(TextureID texture, TexFormat fmt) {
  (void)fmt;
  s_textured = texture == g_vramTexture ? 1 : texture == g_whiteTexture ? 0 : -1;
}

void gles_GR_SetBlendMode(BlendMode m) { s_blend = (int)m; }

void gles_GR_UpdateVertexBuffer(const GrVertex *v, int count) {
  flush();
  s_verts = v;
  s_nverts = count;
  if (count <= 0)
    return;
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * sizeof(GrVertex), v, GL_STREAM_DRAW);
  s_vbo_ok = 0;
}

void gles_GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g,
                       unsigned char b) {
  int i;
  if (w <= 0 || h <= 0)
    return;
  flush();
  dirty(x, y, w, h);
  for (i = 0; i < s_nt; i++) {
    Target *t = &s_t[i];
    int x0 = x > t->x ? x : t->x, y0 = y > t->y ? y : t->y;
    int x1 = x + w < t->x + t->w ? x + w : t->x + t->w;
    int y1 = y + h < t->y + t->h ? y + h : t->y + t->h;
    if (x0 >= x1 || y0 >= y1)
      continue;
    target_viewport(t);
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)((x0 - t->x) * SX), (int)((y0 - t->y) * SY), (int)((x1 - x0) * SX + 0.999f),
              (int)((y1 - y0) * SY + 0.999f));
    /* the colour VRAM got (5 bits a channel) */
    glClearColor((float)((r >> 3) << 3 | r >> 5) / 255.0f, (float)((g >> 3) << 3 | g >> 5) / 255.0f,
                 (float)((b >> 3) << 3 | b >> 5) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    s_st.clears++;
  }
  glDisable(GL_SCISSOR_TEST);
}

/* LoadImage or MoveImage: VRAM (gpu_soft.c's) has the pixels; each target
 * under the destination gets them from there */
void gles_GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y) {
  int i;
  (void)src, (void)x, (void)y;
  if (w <= 0 || h <= 0)
    return;
  flush();
  dirty(dst_x, dst_y, w, h);
  for (i = 0; i < s_nt; i++)
    if (overlaps(&s_t[i], dst_x, dst_y, w, h))
      copy_into(&s_t[i], dst_x, dst_y, w, h);
}

/* the prim attributes for s_vbo */
static void prim_attribs(void) {
  if (s_vbo_ok)
    return;
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(GrVertex), (const void *)0);
  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GrVertex), (const void *)8);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GrVertex), (const void *)12);
  s_vbo_ok = 1;
}

static void semi(int p, float scale, float alpha, float pass) {
  glUniform3f(s_prog[p].u_semi, scale, alpha, pass);
}

/* the joined draw waiting, to the GPU */
static void flush(void) {
  Target *t = s_pend.t;
  int p;
  if (!s_pend.n)
    return;
  s_pend.n = 0;
  p = s_pend.textured ? P_TEX : P_UNTEX;
  target_viewport(t);
  use(p);
  if (s_pend.textured) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  }
  glUniform4f(s_prog[p].u_xf, (float)(s_pend.org_x - t->x), (float)(s_pend.org_y - t->y),
              2 * SX / t->pw, 2 * SY / t->ph);
  glEnable(GL_SCISSOR_TEST);
  {
    int x0 = (int)((s_pend.cx0 - t->x) * SX), y0 = (int)((s_pend.cy0 - t->y) * SY);
    int x1 = (int)((s_pend.cx1 + 1 - t->x) * SX + 0.999f), y1 = (int)((s_pend.cy1 + 1 - t->y) * SY + 0.999f);
    glScissor(x0, y0, x1 - x0, y1 - y0);
  }
  prim_attribs();
  switch (s_pend.blend) {
  case BM_NONE:
    glDisable(GL_BLEND);
    semi(p, 1, 1, 0);
    break;
  case BM_SUBTRACT: /* B - F for the semi-transparent pixels, F for the rest */
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    glBlendFunc(GL_ONE, GL_ONE);
    semi(p, 1, 0, s_pend.textured ? 2 : 0);
    glDrawArrays(GL_TRIANGLES, s_pend.start, s_pend.count);
    s_st.draws++;
    glBlendEquation(GL_FUNC_ADD);
    if (!s_pend.textured) {
      glDisable(GL_SCISSOR_TEST);
      return;
    }
    glDisable(GL_BLEND);
    semi(p, 1, 1, 1);
    break;
  default: /* average, add, quarter: F * scale + B * (1 - alpha) */
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (s_pend.blend == BM_AVERAGE)
      semi(p, 0.5f, 0.5f, 0);
    else if (s_pend.blend == BM_ADD)
      semi(p, 1, 0, 0);
    else
      semi(p, 0.25f, 0, 0);
    break;
  }
  glDrawArrays(GL_TRIANGLES, s_pend.start, s_pend.count);
  s_st.draws++;
  glDisable(GL_SCISSOR_TEST);
}

/* a textured draw samples VRAM that changed since the upload: upload first
 * (its pages, 64/128/256 words wide and 256 lines, and its CLUT's row) */
static void sync_textures(int start, int count) {
  int i;
  if (s_dx0 > s_dx1)
    return;
  for (i = 0; i < count; i += 3) {
    const GrVertex *v = s_verts + start + i;
    uint16_t page = (uint16_t)v->page, clut = (uint16_t)v->clut;
    int fmt = (page >> 7) & 3;
    int bx = (page & 0xF) * 64, by = ((page >> 4) & 1) * 256;
    if (dirty_hits(bx, by, fmt == 0 ? 64 : fmt == 1 ? 128 : 256, 256) ||
        (fmt < 2 && dirty_hits((clut & 0x3F) * 16, (clut >> 6) & (VH - 1), fmt == 0 ? 16 : 256, 1))) {
      upload();
      return;
    }
  }
}

void gles_GR_DrawTriangles(int start, int triangles) {
  int count = triangles * 3;
  if (!s_verts || !s_cur || s_textured < 0 || start < 0 || triangles <= 0 ||
      start + count > s_nverts)
    return;
  s_st.splits++;
  if (s_textured)
    sync_textures(start, count);
  if (s_pend.n && s_pend.t == s_cur && s_pend.textured == s_textured &&
      s_pend.blend == s_blend && s_pend.org_x == s_org_x && s_pend.org_y == s_org_y &&
      s_pend.cx0 == s_cx0 && s_pend.cy0 == s_cy0 && s_pend.cx1 == s_cx1 && s_pend.cy1 == s_cy1 &&
      s_pend.start + s_pend.count == start) {
    s_pend.count += count;
  } else {
    flush();
    s_pend.n = 1;
    s_pend.t = s_cur;
    s_pend.start = start;
    s_pend.count = count;
    s_pend.textured = s_textured;
    s_pend.blend = s_blend;
    s_pend.org_x = s_org_x, s_pend.org_y = s_org_y;
    s_pend.cx0 = s_cx0, s_pend.cy0 = s_cy0, s_pend.cx1 = s_cx1, s_pend.cy1 = s_cy1;
  }
  drawn(s_cur, s_cx0, s_cy0, s_cx1 - s_cx0 + 1, s_cy1 - s_cy0 + 1);
}

/* ---- the presenter's frames on the GPU (g_present_capture) ---- */

static int shown_top(int h) { return h == 240 ? 8 : 0; }

static void slots_alloc(int pw, int ph) {
  int i;
  if (s_slot_pw == pw && s_slot_ph == ph)
    return;
  for (i = 0; i < NSLOTS; i++) {
    if (s_slot_fbo[i]) {
      glDeleteFramebuffers(1, &s_slot_fbo[i]);
      glDeleteTextures(1, &s_slot_tex[i]);
      if (s_bound_fbo == s_slot_fbo[i])
        s_bound_fbo = (GLuint)-1;
    }
    s_slot_tex[i] = new_tex(pw, ph, GL_RGBA, 1);
    s_slot_fbo[i] = new_fbo(s_slot_tex[i]);
    s_slot_valid[i] = 0;
  }
  s_slot_pw = pw, s_slot_ph = ph;
  host_log("gpu: %d presentation textures of %dx%d", NSLOTS, pw, ph);
}

/* the VRAM rect a presenter's draw just drew -> the staging texture: its
 * drawn lines, from the target that holds it (else from VRAM) */
static void pres_capture(int x, int y, int w, int h) {
  int top = shown_top(h), i;
  float dst[4] = {-1, -1, 1, 1}, st[4];
  Target *t = NULL;
  flush();
  y += top;
  h -= 2 * top;
  if (w <= 0 || h <= 0)
    return;
  slots_alloc((int)(512 * SX + 0.999f), (int)(256 * SY + 0.999f));
  for (i = 0; i < s_nt && !t; i++)
    if (contains(&s_t[i], x, y, w, h) && !s_t[i].stale)
      t = &s_t[i];
  bind_fbo(s_slot_fbo[STAGE]);
  no_blend();
  {
    int pw = (int)(w * SX + 0.5f), ph = (int)(h * SY + 0.5f);
    glViewport(0, 0, pw, ph);
    s_slot_w[STAGE] = pw;
    s_slot_lines[STAGE] = ph;
  }
  if (t) {
    use(P_BLIT);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    st[0] = (x - t->x) * SX / t->pw;
    st[1] = (y - t->y) * SY / t->ph;
    st[2] = (x + w - t->x) * SX / t->pw;
    st[3] = (y + h - t->y) * SY / t->ph;
    glActiveTexture(GL_TEXTURE0);
  } else {
    sync_rect(x, y, w, h);
    use(P_VRAM);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_vram_tex);
    st[0] = (float)x, st[1] = (float)y, st[2] = (float)(x + w), st[3] = (float)(y + h);
  }
  quad(dst, st);
  s_slot_valid[STAGE] = 1;
  s_st.captures++;
}

static void pres_commit(const PresentFrame *f) {
  int s = f->slot, tmp;
  GLuint g;
  flush();
  if (s < 0 || s >= PRESENT_SLOTS)
    return;
  g = s_slot_tex[s], s_slot_tex[s] = s_slot_tex[STAGE], s_slot_tex[STAGE] = g;
  g = s_slot_fbo[s], s_slot_fbo[s] = s_slot_fbo[STAGE], s_slot_fbo[STAGE] = g;
  tmp = s_slot_w[s], s_slot_w[s] = s_slot_w[STAGE], s_slot_w[STAGE] = tmp;
  tmp = s_slot_lines[s], s_slot_lines[s] = s_slot_lines[STAGE], s_slot_lines[STAGE] = tmp;
  tmp = s_slot_valid[s], s_slot_valid[s] = s_slot_valid[STAGE], s_slot_valid[STAGE] = tmp;
}

static const PresentCapture k_capture = {pres_capture, pres_commit, NULL};

const PresentCapture *ps4_gpu_capture(void) { return &k_capture; }

/* ---- the present ---- */

void ps4_gpu_overlay(const uint8_t *rgb, int w, int h) {
  if (!rgb || w <= 0 || h <= 0) {
    s_ovl_w = s_ovl_h = 0;
    return;
  }
  if (!s_ovl_tex || w != s_ovl_w || h != s_ovl_h) {
    if (s_ovl_tex)
      glDeleteTextures(1, &s_ovl_tex);
    s_ovl_tex = new_tex(w, h, GL_RGB, 0);
  }
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, s_ovl_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glActiveTexture(GL_TEXTURE0);
  s_ovl_w = w, s_ovl_h = h;
}

/* screen pixels (x, y from the top, w, h) as clip space */
static void screen_clip(int x, int y, int w, int h, float out[4]) {
  out[0] = (float)x / SCREEN_W * 2 - 1;
  out[2] = (float)(x + w) / SCREEN_W * 2 - 1;
  out[1] = 1 - (float)y / SCREEN_H * 2; /* the top: texture row 0 is VRAM's top */
  out[3] = 1 - (float)(y + h) / SCREEN_H * 2;
}

static int box43(int h) { return h == 256 ? 1440 : 1542; }

/* the PS1 display rect: from the target that holds it, else from VRAM (the
 * movies' 24-bit mode converted on the CPU) */
static void show_display(void) {
  const DISPENV *d = &currentDispEnv;
  int h = d->disp.h > 0 ? d->disp.h : 240, w = d->disp.w, top = shown_top(h), bw = box43(h), i;
  int x = d->disp.x, y = d->disp.y + top, lines = h - 2 * top;
  float dst[4], st[4];
  Target *t = NULL;
  if (w <= 0 || g_GPUDisabledState)
    return;
  screen_clip((SCREEN_W - bw) / 2, 0, bw, SCREEN_H, dst);
  if (d->isrgb24) {
    int pw, ph;
    uint8_t *rgb = host_display_rgb(&pw, &ph);
    if (!rgb)
      return;
    if (!s_disp_tex)
      s_disp_tex = new_tex(1024, 512, GL_RGB, 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_disp_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw > 1024 ? 1024 : pw, ph > 512 ? 512 : ph, GL_RGB,
                    GL_UNSIGNED_BYTE, rgb);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    free(rgb);
    use(P_BLIT);
    st[0] = 0, st[1] = (float)shown_top(ph) / 512, st[2] = (float)pw / 1024;
    st[3] = (float)(ph - shown_top(ph)) / 512;
    quad(dst, st);
    glActiveTexture(GL_TEXTURE0);
    return;
  }
  for (i = 0; i < s_nt && !t; i++)
    if (contains(&s_t[i], x, y, w, lines) && !s_t[i].stale)
      t = &s_t[i];
  if (t) {
    use(P_BLIT);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    st[0] = (x - t->x) * SX / t->pw;
    st[1] = (y - t->y) * SY / t->ph;
    st[2] = (x + w - t->x) * SX / t->pw;
    st[3] = (y + lines - t->y) * SY / t->ph;
    glActiveTexture(GL_TEXTURE0);
  } else {
    sync_rect(x, y, w, lines);
    use(P_VRAM);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_vram_tex);
    st[0] = (float)x, st[1] = (float)y, st[2] = (float)(x + w), st[3] = (float)(y + lines);
  }
  quad(dst, st);
}

int ps4_gpu_present(void) {
  unsigned long long t0 = ps4_now_us(), t1;
  int slot = port_present_frame_slot();
  float dst[4], st[4];
  flush();
  bind_fbo(0);
  glViewport(0, 0, SCREEN_W, SCREEN_H);
  no_blend();
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  if (slot >= 0 && s_slot_valid[slot]) {
    int bw = port_present_frame_is_wide() ? SCREEN_W
                                          : box43(port_present_slot_h(slot));
    screen_clip((SCREEN_W - bw) / 2, 0, bw, SCREEN_H, dst);
    use(P_BLIT);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_slot_tex[slot]);
    st[0] = 0, st[1] = 0;
    st[2] = (float)s_slot_w[slot] / s_slot_pw;
    st[3] = (float)s_slot_lines[slot] / s_slot_ph;
    glActiveTexture(GL_TEXTURE0);
    quad(dst, st);
  } else
    show_display();
  if (s_ovl_w) {
    screen_clip((SCREEN_W - s_ovl_w * 2) / 2, (SCREEN_H - s_ovl_h * 2) / 2, s_ovl_w * 2,
                s_ovl_h * 2, dst);
    use(P_BLIT);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_ovl_tex);
    glActiveTexture(GL_TEXTURE0);
    st[0] = 0, st[1] = 0, st[2] = 1, st[3] = 1;
    quad(dst, st);
  }
  t1 = ps4_now_us();
  eglSwapBuffers(s_dpy, s_surf);
  s_st.swap_us += ps4_now_us() - t1;
  s_st.present_us += t1 - t0;
  s_st.presents++;
  gl_check("present");
  return 1;
}

void ps4_gpu_report(int vblank) {
  unsigned n = s_st.presents ? s_st.presents : 1;
  host_log("gpu %d: %u pictures, %u splits in %u draws, %u uploads (%u rows), %u target copies, "
           "%u clears, %u targets made, %u captures; per picture %llu us building, %llu us in "
           "eglSwapBuffers; %u GL errors (last 0x%x)",
           vblank, s_st.presents, s_st.splits, s_st.draws, s_st.uploads, s_st.upload_rows,
           s_st.copies, s_st.clears, s_st.inits, s_st.captures, s_st.present_us / n,
           s_st.swap_us / n, s_st.errors, s_st.last_error);
  memset(&s_st, 0, sizeof s_st);
}

/* ---- set-up ---- */

/* Piglet's memory (2Ship2Harkinian-PS4's candidates, the first one that
 * eglGetDisplay accepts; MiB): system shared, video shared, flexible,
 * command buffer, LCUE buffer. A configuration Piglet can't satisfy shows
 * up as eglGetDisplay's failure. */
static const unsigned k_mem[][5] = {
    {128, 512, 236, 4, 4}, {128, 512, 208, 4, 4}, {128, 512, 208, 1, 1},
    {64, 128, 208, 1, 1},  {64, 128, 176, 1, 1},  {64, 128, 64, 1, 1},
};

static int egl_up(void) {
  OrbisPglConfig cfg;
  static OrbisPglWindow win = {0, SCREEN_W, SCREEN_H, 0};
  EGLint major = 0, minor = 0, n = 0;
  EGLConfig conf;
  static const EGLint attrs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                 EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
                                 EGL_SAMPLE_BUFFERS, 0, EGL_SAMPLES, 0,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                 EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  static const EGLint win_attrs[] = {EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE};
  size_t i;
  for (i = 0; i < sizeof k_mem / sizeof k_mem[0]; i++) {
    memset(&cfg, 0, sizeof cfg);
    cfg.size = sizeof cfg;
    cfg.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT | ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60;
    cfg.processOrder = 1;
    cfg.systemSharedMemorySize = (uint64_t)k_mem[i][0] << 20;
    cfg.videoSharedMemorySize = (uint64_t)k_mem[i][1] << 20;
    cfg.maxMappedFlexibleMemory = (uint64_t)k_mem[i][2] << 20;
    cfg.drawCommandBufferSize = k_mem[i][3] << 20;
    cfg.lcueResourceBufferSize = k_mem[i][4] << 20;
    cfg.dbgPosCmd_0x40 = SCREEN_W;
    cfg.dbgPosCmd_0x44 = SCREEN_H;
    cfg.unk_0x5C = 2;
    if (!scePigletSetConfigurationVSH(&cfg)) {
      host_log("gpu: scePigletSetConfigurationVSH refused memory set %zu", i);
      continue;
    }
    s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_dpy != EGL_NO_DISPLAY) {
      host_log("gpu: Piglet memory %u/%u/%u/%u/%u MiB (system, video, flexible, commands, LCUE)",
               k_mem[i][0], k_mem[i][1], k_mem[i][2], k_mem[i][3], k_mem[i][4]);
      break;
    }
    host_log("gpu: eglGetDisplay refused memory set %zu: 0x%x", i, (unsigned)eglGetError());
  }
  if (s_dpy == EGL_NO_DISPLAY)
    return -1;
  if (!eglInitialize(s_dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API)) {
    host_log("gpu: eglInitialize/eglBindAPI: 0x%x", (unsigned)eglGetError());
    return -1;
  }
  /* before the surface: set after eglMakeCurrent, 1 made eglSwapBuffers
   * block forever in 2Ship2Harkinian-PS4 */
  if (!eglSwapInterval(s_dpy, g_ps4_opts.gpu_vsync ? 1 : 0))
    host_log("gpu: eglSwapInterval(%d): 0x%x", g_ps4_opts.gpu_vsync, (unsigned)eglGetError());
  if (!eglChooseConfig(s_dpy, attrs, &conf, 1, &n) || n < 1) {
    host_log("gpu: eglChooseConfig: 0x%x, %d configs", (unsigned)eglGetError(), n);
    return -1;
  }
  s_surf = eglCreateWindowSurface(s_dpy, conf, &win, win_attrs);
  if (s_surf == EGL_NO_SURFACE) {
    host_log("gpu: eglCreateWindowSurface: 0x%x", (unsigned)eglGetError());
    return -1;
  }
  s_ctx = eglCreateContext(s_dpy, conf, EGL_NO_CONTEXT, ctx_attrs);
  if (s_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx)) {
    host_log("gpu: eglCreateContext/eglMakeCurrent: 0x%x", (unsigned)eglGetError());
    return -1;
  }
  host_log("gpu: EGL %d.%d; GL_VERSION %s; GL_RENDERER %s; GLSL %s", major, minor,
           (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER),
           (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
  {
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    GLint mts = 0, range[2] = {0, 0}, prec = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mts);
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &prec);
    host_log("gpu: max texture %d, fragment highp float precision %d (range %d..%d); extensions "
             "%.300s", mts, prec, range[0], range[1], ext ? ext : "(none)");
  }
  return 0;
}

static void egl_down(void) {
  if (s_dpy == EGL_NO_DISPLAY)
    return;
  eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (s_ctx != EGL_NO_CONTEXT)
    eglDestroyContext(s_dpy, s_ctx);
  if (s_surf != EGL_NO_SURFACE)
    eglDestroySurface(s_dpy, s_surf);
  s_ctx = EGL_NO_CONTEXT;
  s_surf = EGL_NO_SURFACE; /* (no eglTerminate: Piglet can't start again after it) */
}

int ps4_gpu_init(void) {
  unsigned long long t0 = ps4_now_us();
  if (egl_up() != 0 || link(&s_prog[P_UNTEX], k_vs_prim, k_fs_untex, 0, "untextured") != 0 ||
      link(&s_prog[P_TEX], k_vs_prim, k_fs_tex, 0, "textured") != 0 ||
      link(&s_prog[P_VRAM], k_vs_quad, k_fs_vram, 1, "VRAM copy") != 0 ||
      link(&s_prog[P_BLIT], k_vs_quad, k_fs_blit, 1, "blit") != 0) {
    host_log("gpu: the GLES2 renderer couldn't start");
    egl_down();
    return -1;
  }
  s_cur_prog = -1;
  s_vram_tex = new_tex(VW / 2, VH, GL_RGBA, 0);
  glGenBuffers(1, &s_vbo);
  glGenBuffers(1, &s_qbo);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DITHER);
  s_dx0 = 0, s_dy0 = 0, s_dx1 = VW - 1, s_dy1 = VH - 1; /* the first draw uploads it all */
  gl_check("init");
  host_log("gpu: the GLES2 renderer is up in %llu ms (1080p targets, the software rasterizer "
           "keeping VRAM)", (ps4_now_us() - t0) / 1000);
  return 0;
}

/* a save state was loaded: VRAM is all new, every target is remade */
void ps4_gpu_vram_loaded(void) {
  int i;
  if (!g_ps4_gpu)
    return;
  flush();
  s_dx0 = 0, s_dy0 = 0, s_dx1 = VW - 1, s_dy1 = VH - 1;
  for (i = 0; i < s_nt; i++)
    s_t[i].stale = 1;
}

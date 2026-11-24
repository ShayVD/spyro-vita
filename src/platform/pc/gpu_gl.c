/* PC GPU renderer (gpu=1, the default; decisions D242): PsyCross's GR_*
 * interface on OpenGL 3.3 core, drawn at the window's resolution.
 *
 * A trimmed copy of the Vita's src/platform/vita/gpu_gl.c (D35 and on): the
 * same model, the same shaders (GLSL ES 1.00, turned into GLSL 3.30 by
 * glsl330() below), the same targets, logs and replays. The Vita's file is
 * untouched; what is left out of this copy, and why, is in D242:
 *   - vitaGL, sce*, CDRAM, the GXM meters, gpu_parts/gpu_time/gpu_bench/
 *     gpu_fill: the Vita's own tools;
 *   - the render thread (gpu_thread.c): here the game thread draws, on a GL
 *     context of its own shared with the main thread's (video.c);
 *   - the decoded-texture atlas (tex_cache, tex_filter), gpu_lean,
 *     gpu_fused, nd_direct, gpu_join, gpu_opaque, gpu_uber, dynres: the
 *     Vita's GPU and CPU budget, which a desktop GPU doesn't need;
 *   - the GL state shadow (D171): a desktop driver checks its own state.
 * Kept: persp= and subpixel= (the vertex cache's exact projections, D39,
 * #177/#178: the Vita's defaults, on here too).
 *
 *   - s_vram is the PS1 VRAM as the CPU sees it (LoadImage, ClearImage,
 *     StoreImage), and the texture every textured prim samples: the shaders
 *     read the 16-bit words themselves (4/8-bit CLUT lookup, 15-bit, texel 0
 *     transparent, STP).
 *   - Each draw area the game draws into is a "target": an FBO covering that
 *     VRAM rect at the window's scale. Inside its rect a target is newer
 *     than s_vram; LoadImage/MoveImage into it are drawn into it too,
 *     ClearImage clears both.
 *   - s_vram gets a target's pixels back only when something reads that
 *     VRAM (StoreImage: the pause and inventory backgrounds; MoveImage; a
 *     textured prim on its pages; a 24-bit display): each target keeps a log
 *     of what was drawn into it since it was last cleared whole, and the
 *     software rasterizer (gpu_soft.c, soft_ names) replays it: the PS1's
 *     exact pixels, no GPU sync. A log that grew too long is read back from
 *     the GPU instead (drawn down to 1x).
 *   - Pictures: the presenter's captures (PresentCapture, frames.c) and the
 *     PS1 display at vblanks with no presenter frame are drawn into picture
 *     textures, one per slot of frames.c's ring, and fenced; the main thread
 *     waits on the fence and scales the picture into the window (video.c).
 * Semi-transparency follows the PS1: only texels with STP set blend (one
 * premultiplied pass for average/add/quarter, two passes for subtract).
 * Not modelled (as in gpu_soft.c): mask bit, texture window, dithering. */
#include "pc.h"
#include "host.h"
#include "pc_gl.h"

#include <SDL3/SDL.h>

#include <assert.h> /* PsyCross headers use static_assert */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "../../game/present.h"
#include "psx_fill.h"
#include "psx_vcache.h"

extern DISPENV currentDispEnv; /* libgpu.c: what the PS1 scans out */
extern int g_GPUDisabledState;

#define VW 1024
#define VH 512
#define MAX_TARGETS 8
#define MAX_PENDING 64
#define PICS 8 /* frames.c's RING */

static uint16_t s_vram[VW * VH];
static int s_vram_dirty = 1; /* s_vram newer than the GPU's copy */

typedef struct { /* the walk's PsxGpuSplit starts like this (psx_gpu.h, gpu_soft.c) */
  DRAWENV drawenv;
  DISPENV dispenv;
} SplitHead;

/* What has been drawn into a target since its log began (when it was made,
 * or last cleared whole). Replayed through the software rasterizer, it gives
 * s_vram the target's pixels as the PS1 (and gpu=0) would have drawn them. */
enum { LOG_DRAW, LOG_COPY, LOG_CLEAR };
typedef struct {
  int kind;
  SplitHead head;   /* LOG_DRAW: the split's environments */
  int enable;       /* LOG_DRAW: GR_SetupClipMode's enable */
  TextureID tex;
  TexFormat fmt;
  BlendMode blend;
  int x, y, w, h;   /* LOG_COPY, LOG_CLEAR: VRAM rect */
  uint16_t color;   /* LOG_CLEAR */
  int n;            /* LOG_DRAW: vertices */
  size_t off;       /* LOG_DRAW, LOG_COPY: into data */
} LogEntry;
#define LOG_MAX_DATA (8 << 20)

typedef struct {
  uint16_t *base;   /* the target's rect when the log began */
  int solid;        /* base is all of color, not written (D111) */
  uint16_t color;
  LogEntry *ents;
  int nents, capents;
  uint8_t *data;
  size_t ndata, capdata;
  int ok;           /* the log has everything since base */
} TargetLog;

typedef struct {
  int x, y, w, h;  /* VRAM rect */
  int pw, ph;      /* texture size */
  GLuint tex, fbo;
  int cpu_stale;   /* drawn since s_vram last got its pixels */
  int tex_stale;   /* s_vram changed in its rect (a clear) without an upload */
  unsigned used;   /* last use, for eviction */
  TargetLog log;
} Target;

/* the software rasterizer (gpu_soft.c, soft_ names): replays the logs */
uint16_t *soft_host_vram(void);
void soft_GR_SetupClipMode(const RECT16 *rect, int enable);
void soft_GR_SetTexture(TextureID texture, TexFormat texFormat);
void soft_GR_SetBlendMode(BlendMode blendMode);
void soft_GR_UpdateVertexBuffer(const GrVertex *vertices, int count);
void soft_GR_DrawTriangles(int start_vertex, int triangles);

static Target s_targets[MAX_TARGETS];
static int s_ntargets;
static float s_sx, s_sy;     /* target pixels per VRAM pixel */
static int s_scale_w, s_scale_h, s_scale_wide; /* what they were set from */

typedef struct {
  Target *t;
  int x, y, w, h;
} Pending; /* VRAM rects LoadImage/MoveImage wrote inside a target */
static Pending s_pending[MAX_PENDING];
static int s_npending;

enum { P_UNTEX, P_T4, P_T8, P_T16, P_VRAM15, P_TEX, P_COUNT };
typedef struct {
  GLuint prog;
  GLint xf, semi, vram, tex;
} Prog;
static Prog s_prog[P_COUNT];

static GLuint s_vram_tex, s_vbo, s_qvbo, s_vao_prim, s_vao_quad, s_down_tex, s_down_fbo;
static int s_down_w, s_down_h;

/* the split being drawn (GR_SetupClipMode .. GR_DrawTriangles) */
static Target *s_cur;
static int s_org_x, s_org_y;
static int s_clip_x0, s_clip_y0, s_clip_x1, s_clip_y1; /* VRAM, exclusive end */
static int s_texmode = P_UNTEX; /* P_UNTEX..P_T16, or -1: not drawn */
static BlendMode s_blend;
static GLuint s_bound_fbo = (GLuint)-1;
static unsigned s_use;

static SplitHead s_head; /* the split's envs, for the target's log */
static int s_head_enable;
static TextureID s_texid;
static TexFormat s_texfmt;

static GrVertex s_vbuf[MAX_VERTEX_BUFFER_SIZE];
static const GrVertex *s_vorig; /* PsyCross's own buffer as it was given */
static int s_nverts;

/* persp= and subpixel= (D39, #178): the vertex with the vertex cache's
 * answer, its offset from the integer position (pixels) and its depth as w
 * (the GPU divides by w when it interpolates; w = 1 is the PS1's affine
 * mapping). The Vita's XVertex without the atlas's fields. */
typedef struct {
  GrVertex g;
  float dxy[2]; /* then w: one attribute (a_qw) */
  float w;
} XVertex;
static XVertex s_xbuf[MAX_VERTEX_BUFFER_SIZE];
static int s_persp, s_subpixel, s_vc; /* s_vc: either, the vcache is read */

/* per report */
static struct {
  unsigned draws, tris, uploads, readbacks, replays, copies, moves, pictures, displays;
  uint64_t upload_us, readback_us, picture_us;
} s_st;

static uint64_t now_us(void) { return SDL_GetTicksNS() / 1000; }

/* ---- shaders: the Vita's GLSL ES 1.00, made GLSL 3.30 core by glsl330() ---- */

#define GLSL_MD                                                                \
  "float md(float qv, float qn, float qinv) { return qv - qn * floor(qv * qinv); }\n"

static const char k_vs_prim[] =
    "attribute vec4 a_pos;\n" /* x, y, tpage, clut (shorts) */
    "attribute vec4 a_tex;\n" /* u, v, bright, dither (bytes) */
    "attribute vec4 a_col;\n" /* r, g, b, a (normalised) */
    "uniform vec4 u_xf;\n"    /* xy: offset to target pixels, zw: 2 / size */
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_pc;\n"    /* page x, y; clut x, y (VRAM words) */
    GLSL_MD
    "void main() {\n"
    "  gl_Position = vec4((a_pos.xy + u_xf.xy) * u_xf.zw - 1.0, 0.0, 1.0);\n"
    "  v_uv = a_tex.xy;\n"
    "  v_col = a_col;\n"
    "  float qpage = md(a_pos.z, 65536.0, 1.0 / 65536.0);\n"
    "  float qclut = md(a_pos.w, 65536.0, 1.0 / 65536.0);\n"
    "  v_pc = vec4(md(qpage, 16.0, 0.0625) * 64.0, md(floor(qpage * 0.0625), 2.0, 0.5) * 256.0,\n"
    "              md(qclut, 64.0, 0.015625) * 16.0, md(floor(qclut * 0.015625), 512.0, 1.0 / 512.0));\n"
    "}\n";

/* k_vs_prim for XVertex (the Vita's k_vs_prim_x without the atlas's
 * attributes): clip coordinates times w, perspective-correct varyings */
static const char k_vs_prim_x[] =
    "attribute vec4 a_pos;\n"
    "attribute vec4 a_tex;\n"
    "attribute vec4 a_col;\n"
    "attribute vec3 a_qw;\n"    /* offset from the integer position (pixels), w */
    "uniform vec4 u_xf;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_pc;\n"
    GLSL_MD
    "void main() {\n"
    "  vec2 qxy = a_pos.xy + a_qw.xy;\n"
    "  gl_Position = vec4(((qxy + u_xf.xy) * u_xf.zw - 1.0) * a_qw.z, 0.0, a_qw.z);\n"
    "  v_uv = a_tex.xy;\n"
    "  v_col = a_col;\n"
    "  float qpage = md(a_pos.z, 65536.0, 1.0 / 65536.0);\n"
    "  float qclut = md(a_pos.w, 65536.0, 1.0 / 65536.0);\n"
    "  v_pc = vec4(md(qpage, 16.0, 0.0625) * 64.0, md(floor(qpage * 0.0625), 2.0, 0.5) * 256.0,\n"
    "              md(qclut, 64.0, 0.015625) * 16.0, md(floor(qclut * 0.015625), 512.0, 1.0 / 512.0));\n"
    "}\n";

/* VRAM word (qx, qy) as its low and high byte, 0..255. The texture is
 * 512x512 RGBA8: texel i holds words 2i (r, g) and 2i+1 (b, a). */
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
 * ONE, ONE_MINUS_SRC_ALPHA); z = 1: draw only STP=0 texels, 2: only STP=1. */
static const char k_fs_prim_head[] =
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_pc;\n"
    "uniform vec3 u_semi;\n" FS_VRAM;

static const char k_fs_untex[] =
    "void main() {\n"
    "  gl_FragColor = vec4(v_col.xyz * u_semi.x, u_semi.y);\n"
    "}\n";

static const char k_fs_tex_body[] =
    "void main() {\n"
    "  vec2 quv = floor(v_uv + 0.002);\n"
    "  quv = quv - 256.0 * floor(quv * (1.0 / 256.0));\n"
    "  vec4 qpc = floor(v_pc + 0.5);\n"
    "#if FMT == 0\n"
    "  vec2 qword = vram(qpc.x + floor(quv.x * 0.25), qpc.y + quv.y);\n"
    "  float qn = md(quv.x, 4.0, 0.25);\n"
    "  float qbyte = mix(qword.x, qword.y, step(2.0, qn));\n"
    "  float qhi = floor(qbyte * 0.0625);\n"
    "  float qidx = mix(qbyte - qhi * 16.0, qhi, md(qn, 2.0, 0.5));\n"
    "  vec2 qc = vram(qpc.z + qidx, qpc.w);\n"
    "#elif FMT == 1\n"
    "  vec2 qword = vram(qpc.x + floor(quv.x * 0.5), qpc.y + quv.y);\n"
    "  vec2 qc = vram(qpc.z + mix(qword.x, qword.y, md(quv.x, 2.0, 0.5)), qpc.w);\n"
    "#else\n"
    "  vec2 qc = vram(qpc.x + quv.x, qpc.y + quv.y);\n"
    "#endif\n"
    "  if (qc.x + qc.y == 0.0) discard;\n"
    "  float qstp = step(128.0, qc.y);\n"
    "  if ((u_semi.z == 1.0 && qstp > 0.5) || (u_semi.z == 2.0 && qstp < 0.5)) discard;\n"
    "  vec3 qrgb = rgb15(qc) * v_col.xyz * (255.0 / 128.0);\n"
    "  gl_FragColor = mix(vec4(qrgb, 1.0), vec4(qrgb * u_semi.x, u_semi.y), qstp);\n"
    "}\n";

static const char k_vs_quad[] =
    "attribute vec2 a_ndc;\n"
    "attribute vec2 a_src;\n"
    "varying vec2 v_src;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_ndc, 0.0, 1.0);\n"
    "  v_src = a_src;\n"
    "}\n";

static const char k_fs_vram15[] = /* v_src: VRAM coordinates */
    "varying vec2 v_src;\n" FS_VRAM
    "void main() {\n"
    "  vec2 qp = floor(v_src + 0.002);\n"
    "  gl_FragColor = vec4(rgb15(vram(qp.x, qp.y)), 1.0);\n"
    "}\n";

static const char k_fs_tex[] = /* v_src: texture coordinates */
    "varying vec2 v_src;\n"
    "uniform sampler2D s_tex;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(texture2D(s_tex, v_src).xyz, 1.0);\n"
    "}\n";

/* GLSL ES 1.00 -> 3.30 core: a #version line, and the old words replaced
 * where they stand as whole words (attribute, varying, texture2D,
 * gl_FragColor), as the source is joined. The Vita's text stays as it is. */
static char *glsl330(GLenum type, const char *const *src, int n) {
  static const char *const from[4] = {"attribute", "varying", "texture2D", "gl_FragColor"};
  const char *to[4] = {"in", type == GL_VERTEX_SHADER ? "out" : "in", "texture", "o_frag"};
  size_t cap = 256;
  char *out, *o;
  int i;
  for (i = 0; i < n; i++)
    cap += strlen(src[i]) * 2;
  out = o = malloc(cap);
  o += sprintf(o, "#version 330 core\n%s",
               type == GL_FRAGMENT_SHADER ? "out vec4 o_frag;\n" : "");
  for (i = 0; i < n; i++) {
    const char *s = src[i];
    while (*s) {
      int k, done = 0;
      int word_start = s == src[i] || !(s[-1] == '_' || (s[-1] >= 'a' && s[-1] <= 'z') ||
                                        (s[-1] >= 'A' && s[-1] <= 'Z') || (s[-1] >= '0' && s[-1] <= '9'));
      for (k = 0; word_start && k < 4 && !done; k++) {
        size_t l = strlen(from[k]);
        char c;
        if (strncmp(s, from[k], l))
          continue;
        c = s[l];
        if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
          continue;
        o += sprintf(o, "%s", to[k]);
        s += l;
        done = 1;
      }
      if (!done)
        *o++ = *s++;
    }
  }
  *o = 0;
  return out;
}

static GLuint compile(GLenum type, const char *const *src, int n, const char *what) {
  GLuint s = glCreateShader(type);
  GLint ok = 0;
  char *text = glsl330(type, src, n);
  const char *t = text;
  glShaderSource(s, 1, &t, NULL);
  glCompileShader(s);
  free(text);
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char info[2048] = "";
    glGetShaderInfoLog(s, sizeof info, NULL, info);
    host_log("gpu: %s %s shader does not compile: %s", what,
             type == GL_VERTEX_SHADER ? "vertex" : "fragment", info);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

static int link(Prog *p, const char *const *vs, int nvs, const char *const *fs, int nfs, int quad,
                const char *what) {
  GLuint v = compile(GL_VERTEX_SHADER, vs, nvs, what);
  GLuint f = compile(GL_FRAGMENT_SHADER, fs, nfs, what);
  GLint ok = 0;
  if (!v || !f)
    return -1;
  p->prog = glCreateProgram();
  glAttachShader(p->prog, v);
  glAttachShader(p->prog, f);
  if (quad) {
    glBindAttribLocation(p->prog, 0, "a_ndc");
    glBindAttribLocation(p->prog, 1, "a_src");
  } else {
    glBindAttribLocation(p->prog, 0, "a_pos");
    glBindAttribLocation(p->prog, 1, "a_tex");
    glBindAttribLocation(p->prog, 2, "a_col");
    glBindAttribLocation(p->prog, 3, "a_qw");
  }
  glLinkProgram(p->prog);
  glDeleteShader(v);
  glDeleteShader(f);
  glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char info[2048] = "";
    glGetProgramInfoLog(p->prog, sizeof info, NULL, info);
    host_log("gpu: %s program does not link: %s", what, info);
    return -1;
  }
  p->xf = glGetUniformLocation(p->prog, "u_xf");
  p->semi = glGetUniformLocation(p->prog, "u_semi");
  p->vram = glGetUniformLocation(p->prog, "s_vram");
  p->tex = glGetUniformLocation(p->prog, "s_tex");
  glUseProgram(p->prog);
  if (p->vram >= 0)
    glUniform1i(p->vram, 0);
  if (p->tex >= 0)
    glUniform1i(p->tex, 1);
  return 0;
}

static int build_programs(void) {
  static const char *const fmt_def[3] = {"#define FMT 0\n", "#define FMT 1\n", "#define FMT 2\n"};
  const char *qvs[1] = {k_vs_quad}, *vs[1] = {s_vc ? k_vs_prim_x : k_vs_prim};
  const char *fs[3];
  int i;
  fs[0] = k_fs_tex;
  if (link(&s_prog[P_TEX], qvs, 1, fs, 1, 1, "blit"))
    return -1;
  fs[0] = k_fs_vram15;
  if (link(&s_prog[P_VRAM15], qvs, 1, fs, 1, 1, "vram15"))
    return -1;
  fs[0] = k_fs_prim_head;
  fs[1] = k_fs_untex;
  if (link(&s_prog[P_UNTEX], vs, 1, fs, 2, 0, "untextured"))
    return -1;
  for (i = 0; i < 3; i++) {
    fs[0] = fmt_def[i];
    fs[1] = k_fs_prim_head;
    fs[2] = k_fs_tex_body;
    if (link(&s_prog[P_T4 + i], vs, 1, fs, 3, 0, i == 0 ? "4-bit" : i == 1 ? "8-bit" : "15-bit"))
      return -1;
  }
  return 0;
}

/* ---- textures and targets ---- */

static GLuint new_tex(int w, int h, const void *rgba) {
  GLuint t;
  glGenTextures(1, &t);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glActiveTexture(GL_TEXTURE0);
  return t;
}

static GLuint new_fbo(GLuint tex, int w, int h, const char *what) {
  GLuint f;
  glGenFramebuffers(1, &f);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    host_log("gpu: %s FBO %dx%d incomplete", what, w, h);
  s_bound_fbo = f;
  return f;
}

static void bind_fbo(GLuint fbo) {
  if (s_bound_fbo != fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    s_bound_fbo = fbo;
  }
}

/* s_vram -> the GPU, whole (1 MB) */
static void upload_vram(void) {
  uint64_t t0;
  int i;
  if (!s_vram_dirty)
    return;
  t0 = now_us();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VW / 2, VH, GL_RGBA, GL_UNSIGNED_BYTE, s_vram);
  s_vram_dirty = 0;
  for (i = 0; i < s_ntargets; i++)
    s_targets[i].tex_stale = 0;
  s_st.uploads++;
  s_st.upload_us += now_us() - t0;
}

/* a quad: ndc x0, y0, x1, y1 and its source rect */
static void quad(const float ndc[4], const float src[4]) {
  float v[24];
  static const int ix[6] = {0, 2, 0, 2, 2, 0}, iy[6] = {1, 1, 3, 1, 3, 3};
  int i;
  for (i = 0; i < 6; i++) {
    v[i * 4] = ndc[ix[i]];
    v[i * 4 + 1] = ndc[iy[i]];
    v[i * 4 + 2] = src[ix[i]];
    v[i * 4 + 3] = src[iy[i]];
  }
  glBindVertexArray(s_vao_quad);
  glBindBuffer(GL_ARRAY_BUFFER, s_qvbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void no_blend(void) {
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
}

/* texture tex on unit 1 (the blit's s_tex), nearest or linear */
static void bind_tex1(GLuint tex, int linear) {
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  glActiveTexture(GL_TEXTURE0);
}

/* Draw VRAM rect (x, y, w, h) from s_vram into target t. */
static void copy_into(Target *t, int x, int y, int w, int h) {
  float ndc[4], src[4];
  bind_fbo(t->fbo);
  glViewport(0, 0, t->pw, t->ph);
  no_blend();
  glUseProgram(s_prog[P_VRAM15].prog);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  ndc[0] = (float)(x - t->x) / t->w * 2 - 1;
  ndc[1] = (float)(y - t->y) / t->h * 2 - 1;
  ndc[2] = (float)(x + w - t->x) / t->w * 2 - 1;
  ndc[3] = (float)(y + h - t->y) / t->h * 2 - 1;
  src[0] = (float)x;
  src[1] = (float)y;
  src[2] = (float)(x + w);
  src[3] = (float)(y + h);
  quad(ndc, src);
  s_st.copies++;
}

/* VRAM rect (x, y, w, h) of target t from target src's pixels at (x + dx,
 * y + dy): a MoveImage between targets keeps the targets' resolution. */
static void move_into(Target *t, const Target *src, int x, int y, int w, int h, int dx, int dy) {
  float ndc[4], uv[4];
  bind_fbo(t->fbo);
  glViewport(0, 0, t->pw, t->ph);
  no_blend();
  glUseProgram(s_prog[P_TEX].prog);
  bind_tex1(src->tex, 0);
  ndc[0] = (float)(x - t->x) / t->w * 2 - 1;
  ndc[1] = (float)(y - t->y) / t->h * 2 - 1;
  ndc[2] = (float)(x + w - t->x) / t->w * 2 - 1;
  ndc[3] = (float)(y + h - t->y) / t->h * 2 - 1;
  uv[0] = (float)(x + dx - src->x) / src->w;
  uv[1] = (float)(y + dy - src->y) / src->h;
  uv[2] = (float)(x + w + dx - src->x) / src->w;
  uv[3] = (float)(y + h + dy - src->y) / src->h;
  quad(ndc, uv);
  s_st.copies++;
  s_st.moves++;
}

static void flush_pending(void) {
  int i;
  if (!s_npending)
    return;
  upload_vram();
  for (i = 0; i < s_npending; i++)
    copy_into(s_pending[i].t, s_pending[i].x, s_pending[i].y, s_pending[i].w, s_pending[i].h);
  s_npending = 0;
}

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* ---- target logs ---- */

static void log_begin(Target *t, int from_vram, uint16_t color) {
  TargetLog *l = &t->log;
  int i;
  if (!l->base)
    l->base = malloc((size_t)t->w * t->h * 2);
  l->solid = !from_vram;
  l->color = color;
  if (from_vram && l->base)
    for (i = 0; i < t->h; i++)
      memcpy(l->base + i * t->w, s_vram + (t->y + i) * VW + t->x, (size_t)t->w * 2);
  l->nents = 0;
  l->ndata = 0;
  l->ok = l->base != NULL;
}

static void log_free(Target *t) {
  free(t->log.base);
  free(t->log.ents);
  free(t->log.data);
  memset(&t->log, 0, sizeof t->log);
}

/* a new entry with `bytes` of data, or NULL (the log is then incomplete) */
static LogEntry *log_add(Target *t, int kind, size_t bytes) {
  TargetLog *l = &t->log;
  LogEntry *e;
  if (!l->ok)
    return NULL;
  if (l->ndata + bytes > LOG_MAX_DATA) {
    l->ok = 0;
    return NULL;
  }
  if (l->nents == l->capents) {
    int cap = l->capents ? l->capents * 2 : 256;
    LogEntry *n = realloc(l->ents, cap * sizeof *n);
    if (!n) {
      l->ok = 0;
      return NULL;
    }
    l->ents = n;
    l->capents = cap;
  }
  if (l->ndata + bytes > l->capdata) {
    size_t cap = l->capdata ? l->capdata : 64 << 10;
    uint8_t *n;
    while (cap < l->ndata + bytes)
      cap *= 2;
    n = realloc(l->data, cap);
    if (!n) {
      l->ok = 0;
      return NULL;
    }
    l->data = n;
    l->capdata = cap;
  }
  e = &l->ents[l->nents++];
  memset(e, 0, sizeof *e);
  e->kind = kind;
  e->off = l->ndata;
  l->ndata += bytes;
  return e;
}

/* s_vram gets target t's pixels by replaying its log in the software
 * rasterizer (its VRAM is free scratch with gpu=1). 0 = done. */
static int replay(Target *t) {
  const TargetLog *l = &t->log;
  uint16_t *sv = soft_host_vram();
  int i, k;
  if (!l->ok)
    return -1;
  memcpy(sv, s_vram, sizeof s_vram);
  for (i = 0; i < t->h; i++)
    if (l->solid)
      psx_fill16(sv + (t->y + i) * VW + t->x, l->color, t->w);
    else
      memcpy(sv + (t->y + i) * VW + t->x, l->base + i * t->w, (size_t)t->w * 2);
  for (k = 0; k < l->nents; k++) {
    const LogEntry *e = &l->ents[k];
    switch (e->kind) {
    case LOG_DRAW:
      soft_GR_SetupClipMode(&e->head.drawenv.clip, e->enable);
      soft_GR_SetTexture(e->tex, e->fmt);
      soft_GR_SetBlendMode(e->blend);
      soft_GR_UpdateVertexBuffer((const GrVertex *)(l->data + e->off), e->n);
      soft_GR_DrawTriangles(0, e->n / 3);
      break;
    case LOG_COPY:
      for (i = 0; i < e->h; i++)
        memcpy(sv + (e->y + i) * VW + e->x, l->data + e->off + (size_t)i * e->w * 2,
               (size_t)e->w * 2);
      break;
    case LOG_CLEAR:
      for (i = 0; i < e->h; i++)
        psx_fill16(sv + (e->y + i) * VW + e->x, e->color, e->w);
      break;
    }
  }
  for (i = 0; i < t->h; i++)
    memcpy(s_vram + (t->y + i) * VW + t->x, sv + (t->y + i) * VW + t->x, (size_t)t->w * 2);
  s_st.replays++;
  return 0;
}

/* s_vram gets target t's pixels: replayed (above), or else drawn down to 1x
 * (nearest) on the GPU and read back. */
static void readback(Target *t) {
  static uint8_t *buf;
  float ndc[4] = {-1, -1, 1, 1}, src[4] = {0, 0, 1, 1};
  uint64_t t0;
  int i, j;
  if (!t->cpu_stale)
    return;
  t0 = now_us();
  flush_pending();
  if (replay(t) == 0)
    goto done;
  if (s_down_w != t->w || s_down_h != t->h) {
    if (s_down_tex)
      glDeleteTextures(1, &s_down_tex);
    if (s_down_fbo)
      glDeleteFramebuffers(1, &s_down_fbo);
    s_down_tex = new_tex(t->w, t->h, NULL);
    s_down_fbo = new_fbo(s_down_tex, t->w, t->h, "read-back");
    s_down_w = t->w;
    s_down_h = t->h;
    free(buf);
    buf = malloc((size_t)t->w * t->h * 4);
  }
  bind_fbo(s_down_fbo);
  glViewport(0, 0, t->w, t->h);
  no_blend();
  glUseProgram(s_prog[P_TEX].prog);
  bind_tex1(t->tex, 0);
  quad(ndc, src);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
  for (i = 0; i < t->h; i++) {
    const uint8_t *s = buf + (size_t)i * t->w * 4;
    uint16_t *d = s_vram + ((t->y + i) & (VH - 1)) * VW;
    for (j = 0; j < t->w; j++, s += 4)
      d[(t->x + j) & (VW - 1)] = (uint16_t)(s[0] >> 3 | (s[1] >> 3) << 5 | (s[2] >> 3) << 10);
  }
done:
  t->cpu_stale = 0;
  s_vram_dirty = 1;
  s_st.readbacks++;
  s_st.readback_us += now_us() - t0;
}

/* s_vram is about to be read in this rect: bring in what targets drew. */
static void sync_rect(int x, int y, int w, int h) {
  int i;
  for (i = 0; i < s_ntargets; i++) {
    Target *t = &s_targets[i];
    if (!overlap(x, y, w, h, t->x, t->y, t->w, t->h))
      continue;
    if (t->cpu_stale)
      readback(t);
    if (t->tex_stale) /* the GPU's VRAM texture is behind there */
      s_vram_dirty = 1;
  }
}

/* Pending copies hold Target pointers: they go out before any target is
 * dropped or moved (the last one moves into slot i). */
static void drop_target(int i) {
  flush_pending();
  readback(&s_targets[i]);
  if (s_targets[i].tex_stale)
    s_vram_dirty = 1;
  glDeleteFramebuffers(1, &s_targets[i].fbo);
  glDeleteTextures(1, &s_targets[i].tex);
  log_free(&s_targets[i]);
  s_bound_fbo = (GLuint)-1;
  s_targets[i] = s_targets[--s_ntargets];
}

/* Spyro draws 224 of the display's 240 lines (initialization.c: draw areas
 * 512x224, 8 lines into each 240-line display); the other 16 stay black, in
 * a TV's overscan, and aren't shown (D67). */
static int shown_top(int dh) { return dh == 240 ? 8 : 0; }

/* The box the picture fills in a window of w x h pixels: 16:9 (wide) or 4:3,
 * as large as fits (video.c draws it there). */
static void box(int w, int h, int wide, int *bw, int *bh) {
  float aspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f, fw = (float)w, fh = fw / aspect;
  if (fh > h) {
    fh = (float)h;
    fw = fh * aspect;
  }
  *bw = (int)(fw + 0.5f);
  *bh = (int)(fh + 0.5f);
}

static int s_res; /* res=N: the PS1's pixels times N (1..8); 0: the window's */

static void set_scale(void) {
  int dw = currentDispEnv.disp.w > 0 ? currentDispEnv.disp.w : 512;
  int dh = currentDispEnv.disp.h > 0 ? currentDispEnv.disp.h : 240;
  int ww = g_pc_draw_w > 0 ? g_pc_draw_w : g_pc_opts.win_w;
  int wh = g_pc_draw_h > 0 ? g_pc_draw_h : g_pc_opts.win_h, bw, bh;
  s_scale_w = ww;
  s_scale_h = wh;
  s_scale_wide = g_present.widescreen;
  if (s_res > 0) {
    s_sx = s_sy = (float)s_res;
  } else {
    box(ww, wh, g_present.widescreen, &bw, &bh);
    s_sx = (float)bw / dw;
    s_sy = (float)bh / (dh - 2 * shown_top(dh));
    if (s_sx < 1)
      s_sx = 1;
    if (s_sy < 1)
      s_sy = 1;
  }
  host_log("gpu: targets at %.3f x %.3f the PS1's pixels (display %dx%d, window %dx%d)", s_sx, s_sy,
           dw, dh, ww, wh);
}

/* every target drawn again at a new scale (the window resized): dropping
 * a target first gives VRAM its pixels (drop_target) */
static void rescale(void) {
  while (s_ntargets > 0)
    drop_target(0);
  s_cur = NULL; /* the next split finds its target again */
  s_sx = s_sy = 0;
}

/* The target for draw area (x, y, w, h): one that contains it, or a new one
 * (absorbing any it overlaps). */
static Target *target_for(int x, int y, int w, int h) {
  int i, x1 = x + w, y1 = y + h;
  Target *t;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x1 > VW)
    x1 = VW;
  if (y1 > VH)
    y1 = VH;
  if (x1 <= x || y1 <= y)
    return NULL;
  for (i = 0; i < s_ntargets; i++) {
    t = &s_targets[i];
    if (x >= t->x && y >= t->y && x1 <= t->x + t->w && y1 <= t->y + t->h) {
      t->used = ++s_use;
      return t;
    }
  }
  if (!s_sx)
    set_scale();
  flush_pending();
  for (i = 0; i < s_ntargets;) {
    t = &s_targets[i];
    if (overlap(x, y, x1 - x, y1 - y, t->x, t->y, t->w, t->h)) {
      if (t->x < x) x = t->x;
      if (t->y < y) y = t->y;
      if (t->x + t->w > x1) x1 = t->x + t->w;
      if (t->y + t->h > y1) y1 = t->y + t->h;
      drop_target(i);
      i = 0; /* the union may now overlap others */
    } else {
      i++;
    }
  }
  if (s_ntargets == MAX_TARGETS) { /* evict the least recently used */
    int lru = 0;
    for (i = 1; i < s_ntargets; i++)
      if (s_targets[i].used < s_targets[lru].used)
        lru = i;
    drop_target(lru);
  }
  t = &s_targets[s_ntargets++];
  memset(t, 0, sizeof *t);
  t->x = x;
  t->y = y;
  t->w = x1 - x;
  t->h = y1 - y;
  t->pw = (int)(t->w * s_sx + 0.999f);
  t->ph = (int)(t->h * s_sy + 0.999f);
  t->tex = new_tex(t->pw, t->ph, NULL);
  t->fbo = new_fbo(t->tex, t->pw, t->ph, "target");
  t->used = ++s_use;
  host_log("gpu: target %d: VRAM %d,%d %dx%d -> %dx%d", s_ntargets - 1, t->x, t->y, t->w, t->h,
           t->pw, t->ph);
  upload_vram();
  copy_into(t, t->x, t->y, t->w, t->h); /* starts as what VRAM holds */
  log_begin(t, 1, 0);
  return t;
}

/* ---- GR_* (called through gpu.c) ---- */

void gl_GR_SetupClipMode(const RECT16 *rect, int enable) {
  const SplitHead *sp = (const SplitHead *)rect;
  s_head = *sp;
  s_head_enable = enable;
  if (enable) {
    s_org_x = sp->dispenv.disp.x;
    s_org_y = sp->dispenv.disp.y;
  } else {
    s_org_x = sp->drawenv.ofs[0];
    s_org_y = sp->drawenv.ofs[1];
  }
  s_clip_x0 = rect->x < 0 ? 0 : rect->x;
  s_clip_y0 = rect->y < 0 ? 0 : rect->y;
  s_clip_x1 = rect->x + rect->w > VW ? VW : rect->x + rect->w;
  s_clip_y1 = rect->y + rect->h > VH ? VH : rect->y + rect->h;
  s_cur = target_for(s_clip_x0, s_clip_y0, s_clip_x1 - s_clip_x0, s_clip_y1 - s_clip_y0);
}

void gl_GR_SetOffscreenState(const RECT16 *rect, int enable) {
  (void)rect;
  (void)enable; /* handled in GR_SetupClipMode */
}

void gl_GR_SetTexture(TextureID texture, TexFormat texFormat) {
  s_texid = texture;
  s_texfmt = texFormat;
  if (texture == g_whiteTexture)
    s_texmode = P_UNTEX;
  else if (texture == g_vramTexture)
    s_texmode = texFormat == TF_4_BIT ? P_T4 : texFormat == TF_8_BIT ? P_T8 : P_T16;
  else
    s_texmode = -1;
}

void gl_GR_SetBlendMode(BlendMode blendMode) { s_blend = blendMode; }
void gl_GR_SetStencilMode(int drawPrim) { (void)drawPrim; }
void gl_GR_SetOverrideTextureSize(int width, int height) {
  (void)width;
  (void)height;
}
void gl_GR_PushDebugLabel(const char *label) { (void)label; }
void gl_GR_PopDebugLabel(void) {}
void gl_GR_UpdateVRAM(void) {}
void gl_GR_ReadFramebufferDataToVRAM(void) {}

/* s_vbuf -> s_xbuf (D39): a polygon's vertices carry the vertex cache's
 * answer (psx_vcache.h): its exact position (subpixel=1, or a vertex the
 * GTE clamped, #178), and its depth as w (persp=1). As the Vita's expand()
 * with tex_filter and tex_cache off. */
static void expand(int count) {
  int i;
  for (i = 0; i < count; i++) {
    XVertex *x = s_xbuf + i;
    const VCacheVert *c = &g_vcache_side[i];
    uint32_t ref;
    x->g = s_vbuf[i];
    x->dxy[0] = x->dxy[1] = 0;
    x->w = 1;
    memcpy(&ref, &s_vbuf[i].tcx, sizeof ref);
    if (!(ref & VREF_POLY))
      continue;
    if (s_subpixel || (ref & VREF_CLAMP)) {
      x->dxy[0] = c->dx;
      x->dxy[1] = c->dy;
    }
    if (s_persp)
      x->w = c->w;
  }
}

/* The vertices into the VBO. Triangles with an edge >= 1024 x or >= 512 y
 * are collapsed: the PS1 GPU skips them (gpu_soft.c's rule). */
void gl_GR_UpdateVertexBuffer(const GrVertex *vertices, int count) {
  int i, k;
  if (count > MAX_VERTEX_BUFFER_SIZE)
    count = MAX_VERTEX_BUFFER_SIZE;
  s_vorig = vertices;
  memcpy(s_vbuf, vertices, (size_t)count * sizeof *vertices);
  for (i = 0; i + 2 < count; i += 3) {
    GrVertex *v = s_vbuf + i;
    for (k = 0; k < 3; k++) {
      int dx = v[k].x - v[(k + 1) % 3].x, dy = v[k].y - v[(k + 1) % 3].y;
      if (dx >= 1024 || dx <= -1024 || dy >= 512 || dy <= -512) {
        v[1] = v[2] = v[0];
        memset(&v[0].tcx, 0, 4); /* no vcache offsets: it stays a point */
        memset(&v[1].tcx, 0, 4);
        memset(&v[2].tcx, 0, 4);
        break;
      }
    }
  }
  s_nverts = count;
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  if (s_vc) {
    expand(count);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * sizeof *s_xbuf, s_xbuf, GL_STREAM_DRAW);
  } else {
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * sizeof *s_vbuf, s_vbuf, GL_STREAM_DRAW);
  }
}

/* A textured prim samples VRAM: targets drawn over its pages (or CLUT rows)
 * since the last readback have to come back to s_vram first. */
static void sync_pages(int start, int n) {
  int i, last_page = -1, last_clut = -1;
  for (i = 0; i < n; i += 3) {
    const GrVertex *v = s_vbuf + start + i;
    int page = (uint16_t)v->page, clut = (uint16_t)v->clut;
    if (page != last_page) {
      int pw = s_texmode == P_T4 ? 64 : s_texmode == P_T8 ? 128 : 256;
      sync_rect((page & 15) * 64, ((page >> 4) & 1) * 256, pw, 256);
      last_page = page;
    }
    if (s_texmode != P_T16 && clut != last_clut) {
      sync_rect((clut & 63) * 16, (clut >> 6) & 511, s_texmode == P_T4 ? 16 : 256, 1);
      last_clut = clut;
    }
  }
}

static int any_stale(void) {
  int i;
  for (i = 0; i < s_ntargets; i++)
    if (s_targets[i].cpu_stale || s_targets[i].tex_stale)
      return 1;
  return 0;
}

static void draw(const Prog *p, int start, int n, float scale, float alpha, float sel) {
  glUniform3f(p->semi, scale, alpha, sel);
  glDrawArrays(GL_TRIANGLES, start, n);
}

void gl_GR_DrawTriangles(int start_vertex, int triangles) {
  Target *t = s_cur;
  int n = triangles * 3, x0, y0, x1, y1;
  const Prog *p;
  if (!t || triangles <= 0 || start_vertex < 0 || start_vertex + n > s_nverts)
    return;
  if (s_texmode < 0) {
    static int warned;
    if (!warned++)
      host_log("gpu: PsyX custom textures (DR_PSYX_TEX) are not drawn");
    return;
  }
  if (s_texmode != P_UNTEX && any_stale())
    sync_pages(start_vertex, n);
  flush_pending();
  if (s_texmode != P_UNTEX)
    upload_vram();

  x0 = (int)((s_clip_x0 - t->x) * s_sx + 0.5f);
  y0 = (int)((s_clip_y0 - t->y) * s_sy + 0.5f);
  x1 = (int)((s_clip_x1 - t->x) * s_sx + 0.5f);
  y1 = (int)((s_clip_y1 - t->y) * s_sy + 0.5f);
  bind_fbo(t->fbo);
  glViewport(0, 0, t->pw, t->ph);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x0, y0, x1 - x0, y1 - y0);
  p = &s_prog[s_texmode];
  glUseProgram(p->prog);
  /* VRAM x = vertex x + org; + half a target pixel: the PS1 samples at the
   * pixel's corner, GL at its centre */
  glUniform4f(p->xf, (float)(s_org_x - t->x) + 0.5f / s_sx, (float)(s_org_y - t->y) + 0.5f / s_sy,
              2.0f / t->w, 2.0f / t->h);
  if (s_texmode != P_UNTEX) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  }
  glBindVertexArray(s_vao_prim);

  switch (s_blend) {
  case BM_NONE:
    glDisable(GL_BLEND);
    draw(p, start_vertex, n, 1, 1, 0);
    break;
  case BM_SUBTRACT:
    if (s_texmode != P_UNTEX) { /* STP=0 texels opaque, then STP=1 subtract */
      glDisable(GL_BLEND);
      draw(p, start_vertex, n, 1, 1, 1);
    }
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    glBlendFunc(GL_ONE, GL_ONE);
    draw(p, start_vertex, n, 1, 1, s_texmode != P_UNTEX ? 2 : 0);
    glBlendEquation(GL_FUNC_ADD);
    break;
  default: /* B/2 + F/2, B + F, B + F/4: premultiplied, ONE, 1 - alpha */
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (s_blend == BM_AVERAGE)
      draw(p, start_vertex, n, 0.5f, 0.5f, 0);
    else if (s_blend == BM_ADD)
      draw(p, start_vertex, n, 1, 0, 0);
    else
      draw(p, start_vertex, n, 0.25f, 0, 0);
    break;
  }
  {
    LogEntry *e = log_add(t, LOG_DRAW, (size_t)n * sizeof(GrVertex));
    if (e) {
      e->head = s_head;
      e->enable = s_head_enable;
      e->tex = s_texid;
      e->fmt = s_texfmt;
      e->blend = s_blend;
      e->n = n;
      memcpy(t->log.data + e->off, s_vorig + start_vertex, (size_t)n * sizeof(GrVertex));
    }
  }
  t->cpu_stale = 1;
  s_st.draws++;
  s_st.tris += triangles;
}

/* ---- VRAM transfers (coordinates wrap like the PS1's) ---- */

/* gl_GR_CopyVRAM's MoveImage, while its source lies inside one target:
 * that target, and source minus destination */
static const Target *s_move_src;
static int s_move_dx, s_move_dy;

/* rect (x, y, w, h) of VRAM changed in s_vram: targets over it get it too */
static void wrote_rect(int x, int y, int w, int h) {
  int i;
  s_vram_dirty = 1;
  for (i = 0; i < s_ntargets; i++) {
    Target *t = &s_targets[i];
    int x0 = x > t->x ? x : t->x, y0 = y > t->y ? y : t->y;
    int x1 = x + w < t->x + t->w ? x + w : t->x + t->w;
    int y1 = y + h < t->y + t->h ? y + h : t->y + t->h;
    if (x0 >= x1 || y0 >= y1)
      continue;
    {
      LogEntry *e = log_add(t, LOG_COPY, (size_t)(x1 - x0) * (y1 - y0) * 2);
      int r;
      if (e) {
        e->x = x0, e->y = y0, e->w = x1 - x0, e->h = y1 - y0;
        for (r = 0; r < e->h; r++)
          memcpy(t->log.data + e->off + (size_t)r * e->w * 2, s_vram + (y0 + r) * VW + x0,
                 (size_t)e->w * 2);
      }
    }
    if (s_move_src && s_move_src != t) {
      flush_pending(); /* in order, and the source's own pending writes first */
      move_into(t, s_move_src, x0, y0, x1 - x0, y1 - y0, s_move_dx, s_move_dy);
      continue;
    }
    if (s_npending == MAX_PENDING)
      flush_pending();
    s_pending[s_npending].t = t;
    s_pending[s_npending].x = x0;
    s_pending[s_npending].y = y0;
    s_pending[s_npending].w = x1 - x0;
    s_pending[s_npending].h = y1 - y0;
    s_npending++;
  }
}

/* fn on the rect, split where it wraps into up to four unwrapped pieces */
static void pieces(int x, int y, int w, int h, void (*fn)(int, int, int, int)) {
  int px = x & (VW - 1), py = y & (VH - 1), k;
  int w0 = px + w > VW ? VW - px : w, h0 = py + h > VH ? VH - py : h;
  for (k = 0; k < 4; k++) {
    int rx = k & 1 ? 0 : px, rw = k & 1 ? w - w0 : w0;
    int ry = k & 2 ? 0 : py, rh = k & 2 ? h - h0 : h0;
    if (rw > 0 && rh > 0)
      fn(rx, ry, rw, rh);
  }
}

static uint16_t s_clear15;     /* gl_GR_ClearVRAM's colour */
static float s_clear_rgb[3];   /* and as GL's clear colour */

/* A clear inside a target needn't reach the GPU's VRAM texture: the target
 * is cleared itself, and whatever later samples or copies from there
 * uploads first (sync_rect). */
static void clear_dirty(int x, int y, int w, int h) {
  int k;
  for (k = 0; k < s_ntargets; k++) {
    Target *t = &s_targets[k];
    if (x >= t->x && y >= t->y && x + w <= t->x + t->w && y + h <= t->y + t->h) {
      t->tex_stale = 1;
      return;
    }
  }
  s_vram_dirty = 1;
}

static void clear_targets(int x, int y, int w, int h) {
  int k;
  for (k = 0; k < s_ntargets; k++) {
    Target *t = &s_targets[k];
    int x0 = x > t->x ? x : t->x, y0 = y > t->y ? y : t->y;
    int x1 = x + w < t->x + t->w ? x + w : t->x + t->w;
    int y1 = y + h < t->y + t->h ? y + h : t->y + t->h;
    int sx0, sy0, sx1, sy1;
    if (x0 >= x1 || y0 >= y1)
      continue;
    if (x0 == t->x && y0 == t->y && x1 == t->x + t->w && y1 == t->y + t->h) {
      log_begin(t, 0, s_clear15); /* cleared whole: the log starts again */
    } else {
      LogEntry *e = log_add(t, LOG_CLEAR, 0);
      if (e) {
        e->x = x0, e->y = y0, e->w = x1 - x0, e->h = y1 - y0;
        e->color = s_clear15;
      }
    }
    sx0 = (int)((x0 - t->x) * s_sx + 0.5f);
    sy0 = (int)((y0 - t->y) * s_sy + 0.5f);
    sx1 = x1 == t->x + t->w ? t->pw : (int)((x1 - t->x) * s_sx + 0.5f);
    sy1 = y1 == t->y + t->h ? t->ph : (int)((y1 - t->y) * s_sy + 0.5f);
    bind_fbo(t->fbo);
    glClearColor(s_clear_rgb[0], s_clear_rgb[1], s_clear_rgb[2], 1.0f);
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

static void clear_piece(int x, int y, int w, int h) { psx_fill_rect(s_vram, x, y, w, h, s_clear15); }

void gl_GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g,
                     unsigned char b) {
  if (w <= 0 || h <= 0)
    return;
  /* each row's run in one go: the same pixels as one at a time (D111) */
  s_clear15 = (uint16_t)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
  pieces(x, y, w, h, clear_piece);
  pieces(x, y, w, h, clear_dirty);
  flush_pending();
  s_clear_rgb[0] = (r >> 3) * 8 / 255.0f;
  s_clear_rgb[1] = (g >> 3) * 8 / 255.0f;
  s_clear_rgb[2] = (b >> 3) * 8 / 255.0f;
  pieces(x, y, w, h, clear_targets);
  glDisable(GL_SCISSOR_TEST);
}

/* PsyX's GL backend clears the window here; GR_ClearVRAM did the targets. */
void gl_GR_Clear(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b) {
  (void)x, (void)y, (void)w, (void)h, (void)r, (void)g, (void)b;
}

/* LoadImage (src = host data, stride w) or MoveImage (src = NULL, VRAM). */
void gl_GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y) {
  int i, j;
  if (w <= 0 || h <= 0)
    return;
  if (!src) {
    static uint16_t tmp[VW * VH];
    pieces(x, y, w, h, sync_rect);
    if (x >= 0 && y >= 0 && x + w <= VW && y + h <= VH && dst_x >= 0 && dst_y >= 0 &&
        dst_x + w <= VW && dst_y + h <= VH) {
      for (i = 0; i < s_ntargets; i++) {
        const Target *t = &s_targets[i];
        if (x >= t->x && y >= t->y && x + w <= t->x + t->w && y + h <= t->y + t->h) {
          s_move_src = t;
          s_move_dx = x - dst_x;
          s_move_dy = y - dst_y;
        }
      }
    }
    for (i = 0; i < h; i++)
      for (j = 0; j < w; j++)
        tmp[i * w + j] = s_vram[((y + i) & (VH - 1)) * VW + ((x + j) & (VW - 1))];
    src = tmp;
    x = y = 0;
  }
  for (i = 0; i < h; i++) {
    const uint16_t *s = (const uint16_t *)src + (y + i) * w + x;
    uint16_t *row = s_vram + ((dst_y + i) & (VH - 1)) * VW;
    for (j = 0; j < w; j++)
      row[(dst_x + j) & (VW - 1)] = s[j];
  }
  pieces(dst_x, dst_y, w, h, wrote_rect);
  s_move_src = NULL;
}

void gl_GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h) {
  int i, j;
  pieces(x, y, dst_w, dst_h, sync_rect);
  for (i = 0; i < dst_h; i++) {
    const uint16_t *row = s_vram + ((y + i) & (VH - 1)) * VW;
    for (j = 0; j < dst_w; j++)
      *dst++ = row[(x + j) & (VW - 1)];
  }
}

uint16_t *gl_host_vram(void) { return s_vram; }

/* ---- pictures (frames.c's ring; drawn here, shown by video.c) ---- */

/* One texture per slot of frames.c's ring, at least the picture's size.
 * ready: fenced on the game thread once the picture is drawn (the main
 * thread's GL waits for it before sampling); done: fenced on the main
 * thread after its last draw from the picture (the game thread's GL waits
 * for it before drawing the slot again). frames.c's mutex orders who
 * touches which: the game thread only slots the main thread doesn't hold. */
static struct {
  GLuint tex, fbo;
  int tw, th; /* the texture's size */
  GLsync ready, done;
} s_pic[PICS];

/* game thread: slot's picture w x h, its FBO bound and viewport set */
static void pic_begin(int slot, int w, int h) {
  if (s_pic[slot].done) { /* the main thread's last draw from it */
    glWaitSync(s_pic[slot].done, 0, GL_TIMEOUT_IGNORED);
    glDeleteSync(s_pic[slot].done);
    s_pic[slot].done = 0;
  }
  if (s_pic[slot].ready) {
    glDeleteSync(s_pic[slot].ready);
    s_pic[slot].ready = 0;
  }
  if (w > s_pic[slot].tw || h > s_pic[slot].th) {
    if (s_pic[slot].tex) {
      glDeleteFramebuffers(1, &s_pic[slot].fbo);
      glDeleteTextures(1, &s_pic[slot].tex);
    }
    s_pic[slot].tw = w > s_pic[slot].tw ? w : s_pic[slot].tw;
    s_pic[slot].th = h > s_pic[slot].th ? h : s_pic[slot].th;
    s_pic[slot].tex = new_tex(s_pic[slot].tw, s_pic[slot].th, NULL);
    s_pic[slot].fbo = new_fbo(s_pic[slot].tex, s_pic[slot].tw, s_pic[slot].th, "picture");
  }
  bind_fbo(s_pic[slot].fbo);
  glViewport(0, 0, w, h);
  no_blend();
  s_st.pictures++;
}

static void pic_end(int slot) {
  s_pic[slot].ready = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush(); /* the fence reaches the GPU before the main thread waits on it */
}

/* Where the part of target t inside VRAM rect (x, y, w, h) goes in a
 * picture of pw x ph pixels that shows that rect: pos = pixels (x0, y0, x1,
 * y1, top row first), uv = the target's texture rect. When the picture has
 * the target's own scale the part starts on a whole pixel and gets one pixel
 * per target pixel, so nothing is resampled. 0: t isn't inside the rect. */
static int place(const Target *t, int x, int y, int w, int h, int pw, int ph, float pos[4],
                 float uv[4]) {
  int x0 = x > t->x ? x : t->x, y0 = y > t->y ? y : t->y;
  int x1 = x + w < t->x + t->w ? x + w : t->x + t->w;
  int y1 = y + h < t->y + t->h ? y + h : t->y + t->h;
  float kx = (float)pw / w, ky = (float)ph / h;
  float sx = (float)t->pw / t->w, sy = (float)t->ph / t->h;
  float tx0 = (x0 - t->x) * sx, tx1 = (x1 - t->x) * sx;
  float ty0 = (y0 - t->y) * sy, ty1 = (y1 - t->y) * sy;
  if (x0 >= x1 || y0 >= y1)
    return 0;
  pos[0] = (x0 - x) * kx, pos[2] = (x1 - x) * kx;
  pos[1] = (y0 - y) * ky, pos[3] = (y1 - y) * ky;
  if (kx - sx < 0.01f && sx - kx < 0.01f) {
    tx0 = floorf(tx0 + 0.5f);
    tx1 = x1 == t->x + t->w ? t->pw : floorf(tx1 + 0.5f);
    pos[0] = floorf(pos[0] + 0.5f);
    pos[2] = pos[0] + (tx1 - tx0);
  }
  if (ky - sy < 0.01f && sy - ky < 0.01f) {
    ty0 = floorf(ty0 + 0.5f);
    ty1 = y1 == t->y + t->h ? t->ph : floorf(ty1 + 0.5f);
    pos[1] = floorf(pos[1] + 0.5f);
    pos[3] = pos[1] + (ty1 - ty0);
  }
  uv[0] = tx0 / t->pw, uv[2] = tx1 / t->pw;
  uv[1] = ty0 / t->ph, uv[3] = ty1 / t->ph;
  return 1;
}

/* VRAM rect (x, y, w, h) -> the bound picture, pw x ph: s_vram under it
 * (base) or black, then every target over it at the targets' resolution.
 * Inside an FBO the image's top is NDC -1, as in copy_into(). */
static void compose(int x, int y, int w, int h, int pw, int ph, int base) {
  float ndc[4] = {-1, -1, 1, 1}, src[4];
  int i;
  if (base) {
    upload_vram();
    glUseProgram(s_prog[P_VRAM15].prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_vram_tex);
    src[0] = (float)x, src[1] = (float)y, src[2] = (float)(x + w), src[3] = (float)(y + h);
    quad(ndc, src);
  } else {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  glUseProgram(s_prog[P_TEX].prog);
  for (i = 0; i < s_ntargets; i++) {
    const Target *t = &s_targets[i];
    float pos[4];
    if (!place(t, x, y, w, h, pw, ph, pos, src))
      continue;
    ndc[0] = pos[0] / pw * 2 - 1;
    ndc[2] = pos[2] / pw * 2 - 1;
    ndc[1] = pos[1] / ph * 2 - 1;
    ndc[3] = pos[3] / ph * 2 - 1;
    bind_tex1(t->tex, 0);
    quad(ndc, src);
  }
}

/* present.c's capture: the VRAM rect a presentation pass just drew -> the
 * picture of ring slot `slot`, at the targets' resolution (its drawn lines
 * only: Spyro's 224 of NTSC's 240). */
void pc_gpu_capture(int slot, int x, int y, int w, int h, int *pw, int *ph) {
  uint64_t t0 = now_us();
  int top = shown_top(h);
  y += top;
  h -= 2 * top;
  if (!s_sx)
    set_scale();
  flush_pending();
  *pw = (int)(w * s_sx + 0.999f);
  *ph = (int)(h * s_sy + 0.999f);
  pic_begin(slot, *pw, *ph);
  compose(x, y, w, h, *pw, *ph, 0);
  pic_end(slot);
  s_st.picture_us += now_us() - t0;
}

/* The PS1 display as it is now -> slot's picture, at the targets' scale. -1: a 24-bit display (the boot logos, movies) or none, which
 * frames.c converts on the CPU from s_vram (pc_gpu_sync_display first). */
int pc_gpu_display(int slot, int *pw, int *ph) {
  const DISPENV *d = &currentDispEnv;
  int dw = d->disp.w > 0 ? d->disp.w : 320, dh = d->disp.h > 0 ? d->disp.h : 240;
  int top = shown_top(dh), sy = d->disp.y + top, sh = dh - 2 * top;
  uint64_t t0 = now_us();
  if (d->disp.w <= 0 || g_GPUDisabledState || d->isrgb24)
    return -1;
  if (!s_sx)
    set_scale();
  flush_pending();
  /* at the targets' own scale, so they are copied 1:1 (place); video.c
   * shows it 4:3 */
  *pw = (int)(dw * s_sx + 0.999f);
  *ph = (int)(sh * s_sy + 0.999f);
  pic_begin(slot, *pw, *ph);
  compose(d->disp.x, sy, dw, sh, *pw, *ph, 1);
  pic_end(slot);
  s_st.displays++;
  s_st.picture_us += now_us() - t0;
  return 0;
}

/* targets over the display rect -> s_vram (a 24-bit display, CPU pictures) */
void pc_gpu_sync_display(void) {
  const DISPENV *d = &currentDispEnv;
  int w = d->disp.w > 0 ? d->disp.w : 320, h = d->disp.h > 0 ? d->disp.h : 240;
  if (d->isrgb24)
    w = w * 3 / 2;
  pieces(d->disp.x, d->disp.y, w, h, sync_rect);
}

/* After a picture is filed: the window's size or the aspect changed since
 * the targets were made, so they are made again at the new scale (at most
 * every quarter second while a window is being dragged). */
void pc_gpu_picture_end(void) {
  static uint64_t last;
  uint64_t now;
  if (!s_sx || s_res > 0)
    return;
  if (g_pc_draw_w == s_scale_w && g_pc_draw_h == s_scale_h && g_present.widescreen == s_scale_wide)
    return;
  now = now_us();
  if (now - last < 250000)
    return;
  last = now;
  rescale();
}

/* main thread: slot's picture texture, after the game thread's drawing */
unsigned pc_gpu_picture_tex(int slot, int *tw, int *th) {
  if (s_pic[slot].ready)
    glWaitSync(s_pic[slot].ready, 0, GL_TIMEOUT_IGNORED);
  *tw = s_pic[slot].tw;
  *th = s_pic[slot].th;
  return s_pic[slot].tex;
}

/* main thread: drawn from slot's picture this refresh (the slot is held
 * until a newer frame is taken) */
void pc_gpu_picture_shown(int slot) {
  if (s_pic[slot].done)
    glDeleteSync(s_pic[slot].done);
  s_pic[slot].done = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush();
}

/* game thread, with its context current: 0 = ok */
int pc_gpu_init(void) {
  if (pc_gl_load() != 0)
    return -1;
  s_persp = g_pc_opts.persp != 0;
  s_subpixel = g_pc_opts.subpixel != 0;
  s_vc = s_persp || s_subpixel;
  s_res = g_pc_opts.res;
  if (build_programs() != 0)
    return -1;
  if (s_vc)
    psx_vcache_enable(2); /* the GTE's exact projections recorded (D39) */
  glGenVertexArrays(1, &s_vao_prim);
  glGenVertexArrays(1, &s_vao_quad);
  glGenBuffers(1, &s_vbo);
  glGenBuffers(1, &s_qvbo);
  glBindVertexArray(s_vao_prim);
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  if (s_vc) {
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(XVertex), (void *)offsetof(XVertex, g.x));
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(XVertex),
                          (void *)offsetof(XVertex, g.u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(XVertex),
                          (void *)offsetof(XVertex, g.r));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(XVertex), (void *)offsetof(XVertex, dxy));
  } else {
    glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(GrVertex), (void *)offsetof(GrVertex, x));
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GrVertex),
                          (void *)offsetof(GrVertex, u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GrVertex),
                          (void *)offsetof(GrVertex, r));
  }
  glBindVertexArray(s_vao_quad);
  glBindBuffer(GL_ARRAY_BUFFER, s_qvbo);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  s_vram_tex = new_tex(VW / 2, VH, NULL);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_STENCIL_TEST);
  host_log("gpu: GPU renderer ready (gpu=1), persp=%d subpixel=%d, res=%s%d (D242)", s_persp,
           s_subpixel, s_res ? "" : "window", s_res);
  return 0;
}

void pc_gpu_report(int vblank) {
  unsigned n = s_st.pictures ? s_st.pictures : 1;
  host_log("gpu %d: %u draws, %u triangles, %u pictures (%u of the display; %llu us each on the "
           "game thread), %u VRAM uploads (%llu us), %u copies into targets (%u moves), %u "
           "readbacks (%u replayed; %llu us), %d targets at %dx%d",
           vblank, s_st.draws, s_st.tris, s_st.pictures, s_st.displays,
           (unsigned long long)(s_st.picture_us / n), s_st.uploads,
           (unsigned long long)s_st.upload_us, s_st.copies, s_st.moves, s_st.readbacks,
           s_st.replays, (unsigned long long)s_st.readback_us, s_ntargets,
           s_ntargets ? s_targets[0].pw : 0, s_ntargets ? s_targets[0].ph : 0);
  memset(&s_st, 0, sizeof s_st);
}

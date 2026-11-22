/* The GLES2 renderer on Piglet (docs/ps4-plan.md P4, decisions D193,
 * D199): the PS1 picture drawn by the GPU at 1080p.
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
#include <sys/stat.h>

#include <orbis/libkernel.h>
#include <orbis/Pigletv2VSH.h> /* EGL and GLES2 too */

#include "psx_gpu.h"
#include "psx_vcache.h"

#define LIB_DIR "/data/self/system/common/lib"
#define VW 1024
#define VH 512
#define SCREEN_W 1920
#define SCREEN_H 1080
/* VRAM pixels to target pixels: 1080p (res=native) or 1 (res=ps1, the
 * PICTURE row; ps4_gpu_set_res) */
#define SX s_sx
#define SY s_sy
static float s_sx = 1920.0f / 512.0f, s_sy = 1080.0f / 224.0f;
#define MAX_TARGETS 8
#define NSLOTS (PRESENT_SLOTS + 1) /* the slots and the staging texture */
#define STAGE PRESENT_SLOTS

uint16_t *soft_host_vram(void); /* gpu_soft.c (gr_ps4.c's soft_ names) */
extern DISPENV currentDispEnv;
extern int g_GPUDisabledState;
uint8_t *host_display_rgb(int *pw, int *ph); /* linux32/png.c */

int g_ps4_gpu;

static int s_piglet = -1, s_shacc = -1;
static int s_firmware; /* the Piglet loaded is the firmware's (not the player's pair) */

/* How the shaders come (D199): compiled from our GLSL by the player's
 * Piglet pair, or the pre-compiled binaries (ps4_shaders.c, from a run
 * with shader_dump=1) on the firmware's own Piglet: what every player has,
 * the owner's option C. */
enum { SH_NONE, SH_COMPILE, SH_BINARY };
static int s_shader_mode;

typedef struct {
  const char *name;
  int fragment;
  uint32_t hash;
  uint32_t format; /* glShaderBinary's, as glPigletGetShaderBinarySCE reported it */
  const unsigned char *data;
  size_t size;
} Ps4ShaderBlob;
extern const Ps4ShaderBlob g_ps4_shader_blobs[]; /* ps4_shaders.c (tools/ps4/shaders_c.py) */

static int blob_count(void) {
  int n = 0;
  while (g_ps4_shader_blobs[n].name)
    n++;
  return n;
}

/* ---- the modules ---- */

static int load_path(const char *path, int *handle) {
  int start = 0, h = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &start);
  host_log("gpu: sceKernelLoadStartModule(%s): 0x%x, module_start %d", path, (unsigned)h, start);
  host_log_flush();
  if (h < 0)
    return h;
  *handle = h;
  return 0;
}

/* The firmware's Piglet and this build's own shaders first, what every
 * player has (the owner's choice: the pkg uses its own shaders, even with
 * the pair installed); the player's pair when the build has none, when
 * shader_compile=1 (capturing new binaries: shader_dump saves them), or
 * when the firmware's doesn't load. */
static int load_firmware(void) {
  char path[256];
  const char *word = sceKernelGetFsSandboxRandomWord();
  snprintf(path, sizeof path, "/%s/common/lib/libScePigletv2VSH.sprx", word ? word : "system");
  if (load_path(path, &s_piglet) != 0)
    return -1;
  s_firmware = 1;
  s_shader_mode = SH_BINARY;
  return 0;
}

int ps4_gpu_load(char *why, int whylen) {
  int rc = 0, nblobs = blob_count(), want_binary = nblobs && !g_ps4_opts.shader_compile;
  if (s_shader_mode != SH_NONE)
    return 0;
  if (want_binary)
    load_firmware();
  if (s_shader_mode == SH_NONE) {
    rc = load_path(LIB_DIR "/libScePigletv2VSH.sprx", &s_piglet);
    if (rc == 0) {
      rc = load_path(LIB_DIR "/libSceShaccVSH.sprx", &s_shacc);
      if (rc != 0) { /* a Piglet without its compiler: only binaries work */
        host_log("gpu: the player's Piglet without libSceShaccVSH.sprx (0x%x)", (unsigned)rc);
        s_shader_mode = nblobs ? SH_BINARY : SH_NONE;
      } else
        s_shader_mode = SH_COMPILE;
    } else if (nblobs && !want_binary)
      load_firmware();
  }
  if (s_shader_mode == SH_NONE) {
    if (nblobs)
      snprintf(why, (size_t)whylen, "the system's Piglet didn't load (0x%x)", (unsigned)rc);
    else
      snprintf(why, (size_t)whylen,
               "this build has no pre-compiled shaders and " LIB_DIR
               "/libScePigletv2VSH.sprx can't be loaded (0x%x)", (unsigned)rc);
    return -1;
  }
  host_log("gpu: shaders %s (%d pre-compiled in this build)",
           s_shader_mode == SH_COMPILE ? "compiled by the player's Piglet pair"
           : s_firmware                ? "pre-compiled, on the firmware's Piglet"
                                       : "pre-compiled, on the player's Piglet",
           nblobs);
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
static GLuint s_vram_tex, s_vbo, s_qbo, s_sbo, s_ovl_tex;
/* persp=1, subpixel=1 (the PICTURE rows, the Vita's D39): polygons' vertex
 * cache answers (psx_vcache.h), a side stream: dx, dy (1/256 pixel) and w */
static int s_persp, s_subpixel, s_vc, s_side_ok;
static VCacheVert s_side[MAX_VERTEX_BUFFER_SIZE + 6];
static int s_ovl_w, s_ovl_h;
static Target s_t[MAX_TARGETS];
static int s_nt;
static unsigned s_clock;
static GLuint s_slot_tex[NSLOTS], s_slot_fbo[NSLOTS];
static int s_slot_lines[NSLOTS], s_slot_w[NSLOTS], s_slot_valid[NSLOTS];
static int s_slot_pw, s_slot_ph; /* the slot textures' size */
static GLuint s_disp_tex;        /* the 24-bit display, converted on the CPU */
static int s_disp_w, s_disp_h;   /* its size */
static uint32_t s_disp_px[640 * 512]; /* RGBA, the movies' frame */
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
  unsigned long long swap_us, present_us, upload_us, disp24_us;
  unsigned disp24;
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

/* a_off and a_w: the vertex cache's answer (persp, subpixel; else 0 and 1,
 * the same picture): clip coordinates times w give perspective-correct
 * varyings, as the Vita's k_vs_prim_x */
static const char k_vs_prim[] =
    "attribute vec4 a_pos;\n" /* x, y, tpage, clut (shorts) */
    "attribute vec4 a_tex;\n" /* u, v, bright, dither (bytes) */
    "attribute vec4 a_col;\n" /* r, g, b, a (normalised) */
    "attribute vec2 a_off;\n" /* sub-pixel offset (1/256 pixel) */
    "attribute float a_w;\n"  /* view depth (1: none) */
    "uniform vec4 u_xf;\n"    /* xy: VRAM offset into the target, zw: 2 * scale / size */
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_pc;\n" /* page x, y; clut x, y (VRAM words) */
    "varying float v_fmt;\n"
    GLSL_MD
    "void main() {\n"
    "  vec2 qxy = a_pos.xy + a_off * (1.0 / 256.0);\n"
    "  gl_Position = vec4(((qxy + u_xf.xy) * u_xf.zw - 1.0) * a_w, 0.0, a_w);\n"
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

/* FNV-1a of a shader's source: the binaries are filed under it */
static uint32_t src_hash(const char *src) {
  uint32_t h = 2166136261u;
  while (*src)
    h = (h ^ (unsigned char)*src++) * 16777619u;
  return h;
}

/* Compiled by the player's pair: a compiled shader as the binary the firmware's Piglet
 * takes back (Piglet's own container: the GLSL and its machine code),
 * through Piglet's undocumented glPigletGetShaderBinarySCE (resolved by
 * eglGetProcAddress, per shader, as ioQuake3-PS4 captures its shaders), to
 * /data/spyro/shaders/NAME-vs|fs-HASH.bin (once: a file already there is
 * kept; shader_dump=0 saves none): glShaderBinary's format in 4
 * little-endian bytes, then the binary (tools/ps4/shaders_c.py reads them). */
typedef void (*PigletGetShaderBinary)(GLuint shader, GLsizei size, GLsizei *length,
                                      GLenum *format, void *binary);

static void dump_shader(GLuint s, const char *name, int fragment, uint32_t hash) {
  static PigletGetShaderBinary get;
  static unsigned char buf[1 << 18];
  char path[256];
  GLsizei len = 0;
  GLenum fmt = 0;
  FILE *f;
  if (!get)
    get = (PigletGetShaderBinary)eglGetProcAddress("glPigletGetShaderBinarySCE");
  if (!get) {
    host_log("gpu: shader_dump: no glPigletGetShaderBinarySCE");
    return;
  }
  get(s, (GLsizei)sizeof buf, &len, &fmt, buf);
  if (len <= 0 || (size_t)len > sizeof buf) {
    host_log("gpu: shader_dump: %s %s: length %d", name, fragment ? "fs" : "vs", (int)len);
    return;
  }
  mkdir(PS4_DATA_DIR "/shaders", 0777);
  snprintf(path, sizeof path, PS4_DATA_DIR "/shaders/%s-%s-%08x.bin", name,
           fragment ? "fs" : "vs", (unsigned)hash);
  if ((f = fopen(path, "rb")) != NULL) { /* saved by an earlier run */
    fclose(f);
    return;
  }
  f = fopen(path, "wb");
  if (!f) {
    host_log("gpu: shader_dump: can't write %s", path);
    return;
  }
  {
    uint32_t le = (uint32_t)fmt;
    unsigned char h[4] = {(unsigned char)le, (unsigned char)(le >> 8), (unsigned char)(le >> 16),
                          (unsigned char)(le >> 24)};
    fwrite(h, 1, 4, f);
  }
  fwrite(buf, 1, (size_t)len, f);
  fclose(f);
  host_log("gpu: shader_dump: %s (%d bytes, format 0x%x)", path, (int)len, (unsigned)fmt);
}

static GLuint compile(GLenum type, const char *src, const char *name) {
  int fragment = type == GL_FRAGMENT_SHADER;
  uint32_t hash = src_hash(src);
  GLuint s = glCreateShader(type);
  GLint ok = 0;
  if (s_shader_mode == SH_BINARY) {
    const Ps4ShaderBlob *b;
    GLenum e;
    for (b = g_ps4_shader_blobs; b->name; b++)
      if (!strcmp(b->name, name) && b->fragment == fragment && b->hash == hash)
        break;
    if (!b->name) {
      host_log("gpu: no pre-compiled %s %s shader for this source (%08x)", name,
               fragment ? "fragment" : "vertex", (unsigned)hash);
      glDeleteShader(s);
      return 0;
    }
    while (glGetError() != GL_NO_ERROR)
      ;
    glShaderBinary(1, &s, (GLenum)b->format, b->data, (GLsizei)b->size);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok); /* Piglet marks a loaded binary compiled */
    if ((e = glGetError()) != GL_NO_ERROR || !ok) {
      host_log("gpu: glShaderBinary(%s %s, %zu bytes, format 0x%x): error 0x%x, status %d", name,
               fragment ? "fs" : "vs", b->size, (unsigned)b->format, (unsigned)e, ok);
      glDeleteShader(s);
      return 0;
    }
    return s;
  }
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024] = "";
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    host_log("gpu: %s %s didn't compile: %s", name, fragment ? "fs" : "vs", log);
    glDeleteShader(s);
    return 0;
  }
  if (g_ps4_opts.shader_dump)
    dump_shader(s, name, fragment, hash);
  return s;
}

/* what: the program's name, also its fragment shader's in the binaries;
 * vs_name: its vertex shader's (two programs share one) */
static int link(Prog *p, const char *vs, const char *vs_name, const char *fs, int quad,
                const char *what) {
  unsigned long long t0 = ps4_now_us();
  GLuint v = compile(GL_VERTEX_SHADER, vs, vs_name), f = v ? compile(GL_FRAGMENT_SHADER, fs, what) : 0;
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
    glBindAttribLocation(p->prog, 3, "a_off");
    glBindAttribLocation(p->prog, 4, "a_w");
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
  glDisableVertexAttribArray(3);
  glDisableVertexAttribArray(4);
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
  const uint16_t *v;
  if (s_dx0 > s_dx1)
    return;
  v = host_vram(); /* (the rasterizer's threads done first) */
  flush();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  {
    unsigned long long t0 = ps4_now_us();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, s_dy0, VW / 2, s_dy1 - s_dy0 + 1, GL_RGBA,
                    GL_UNSIGNED_BYTE, v + (size_t)s_dy0 * VW);
    s_st.upload_us += ps4_now_us() - t0;
  }
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
  s_side_ok = 0;
  if (s_vc && count <= MAX_VERTEX_BUFFER_SIZE + 6) {
    /* a polygon's vertices (VREF_POLY in their last 4 bytes) have the walk's
     * answers at the same indices in g_vcache_side; the others none */
    int i;
    for (i = 0; i < count; i++) {
      uint32_t ref;
      VCacheVert *o = &s_side[i];
      memcpy(&ref, &v[i].tcx, sizeof ref);
      if (ref & VREF_POLY) {
        const VCacheVert *c = &g_vcache_side[i];
        o->dx = s_subpixel ? c->dx : 0;
        o->dy = s_subpixel ? c->dy : 0;
        o->w = s_persp && c->w > 0 ? c->w : 1.0f;
      } else {
        o->dx = o->dy = 0;
        o->w = 1.0f;
      }
    }
    glBindBuffer(GL_ARRAY_BUFFER, s_sbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * sizeof(VCacheVert), s_side,
                 GL_STREAM_DRAW);
    s_side_ok = 1;
  }
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
  if (s_side_ok) {
    glBindBuffer(GL_ARRAY_BUFFER, s_sbo);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(3, 2, GL_SHORT, GL_FALSE, sizeof(VCacheVert), (const void *)0);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(VCacheVert), (const void *)4);
  } else { /* no answers: the integer position, w 1 */
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glVertexAttrib2f(3, 0, 0);
    glVertexAttrib1f(4, 1);
  }
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
    s_slot_tex[i] = new_tex(pw, ph, GL_RGBA, SX > 1.5f); /* res=ps1: the PS1's pixels, sharp */
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
    /* the movies' 24-bit mode: 3 bytes a pixel, decoded from VRAM into
     * RGBA (video.c's way) and uploaded at its own size. GPU runs 6-8 took
     * ~58 ms a picture here, through host_display_rgb's malloc'd copy and a
     * 1024x512 GL_RGB texture. */
    unsigned long long t0 = ps4_now_us();
    const uint8_t *vb = (const uint8_t *)host_vram();
    int pw = w > 640 ? 640 : w, ph = lines, yy, xx;
    for (yy = 0; yy < ph; yy++) {
      const uint8_t *b = vb + (size_t)((y + yy) & 511) * 2048;
      uint32_t *o = s_disp_px + (size_t)yy * pw;
      int off = x * 2;
      for (xx = 0; xx < pw; xx++, off += 3)
        o[xx] = 0xFF000000u | (uint32_t)b[(off + 2) & 2047] << 16 |
                (uint32_t)b[(off + 1) & 2047] << 8 | b[off & 2047];
    }
    glActiveTexture(GL_TEXTURE1);
    if (!s_disp_tex || s_disp_w != pw || s_disp_h != ph) {
      if (s_disp_tex)
        glDeleteTextures(1, &s_disp_tex);
      s_disp_tex = new_tex(pw, ph, GL_RGBA, 1);
      s_disp_w = pw, s_disp_h = ph;
    }
    glBindTexture(GL_TEXTURE_2D, s_disp_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, s_disp_px);
    use(P_BLIT);
    st[0] = 0, st[1] = 0, st[2] = 1, st[3] = 1;
    quad(dst, st);
    glActiveTexture(GL_TEXTURE0);
    s_st.disp24_us += ps4_now_us() - t0;
    s_st.disp24++;
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
  {
    static unsigned first;
    unsigned long long tf = 0;
    if (first < 3) { /* the first pictures: the GL work finished apart from the
                      * swap, each step flushed (GPU runs 3-5 hung in the first swap) */
      GLenum e = glGetError();
      host_log("gpu: picture %u built in %llu us (GL error 0x%x); glFinish", first,
               ps4_now_us() - t0, (unsigned)e);
      host_log_flush();
      g_ps4_where = "glFinish";
      tf = ps4_now_us();
      glFinish();
      tf = ps4_now_us() - tf;
    }
    t1 = ps4_now_us();
    g_ps4_where = "eglSwapBuffers";
    eglSwapBuffers(s_dpy, s_surf);
    g_ps4_where = "the present";
    s_st.swap_us += ps4_now_us() - t1;
    if (first < 3) {
      host_log("gpu: picture %u: %llu us in glFinish, %llu us in eglSwapBuffers", first, tf,
               ps4_now_us() - t1);
      host_log_flush();
      first++;
    }
  }
  s_st.present_us += t1 - t0;
  s_st.presents++;
  gl_check("present");
  return 1;
}

void ps4_gpu_report(int vblank) {
  unsigned n = s_st.presents ? s_st.presents : 1;
  host_log("gpu %d: %u pictures, %u splits in %u draws, %u uploads (%u rows), %u target copies, "
           "%u clears, %u targets made, %u captures; per picture %llu us building, %llu us in "
           "eglSwapBuffers; %u GL errors (last 0x%x); uploads %llu us; %u 24-bit pictures, %llu us "
           "each",
           vblank, s_st.presents, s_st.splits, s_st.draws, s_st.uploads, s_st.upload_rows,
           s_st.copies, s_st.clears, s_st.inits, s_st.captures, s_st.present_us / n,
           s_st.swap_us / n, s_st.errors, s_st.last_error, s_st.upload_us,
           s_st.disp24, s_st.disp24 ? s_st.disp24_us / s_st.disp24 : 0);
  memset(&s_st, 0, sizeof s_st);
}

/* ---- set-up ---- */

/* Piglet's memory (2Ship2Harkinian-PS4's candidates; MiB): system shared,
 * video shared, flexible, command buffer, LCUE buffer. Piglet takes its
 * configuration once: GPU run 1 tried all six in turn and eglGetDisplay
 * refused each (no EGL error), the first (236 MiB of flexible memory, more
 * than our image leaves of the app's) having stuck. So the first that
 * fits the memory free now is chosen, and set once. */
static const unsigned k_mem[][5] = {
    {128, 512, 236, 4, 4}, {128, 512, 208, 4, 4}, {128, 512, 208, 1, 1},
    {64, 128, 208, 1, 1},  {64, 128, 176, 1, 1},  {64, 128, 64, 1, 1},
};
#define FLEX_SPARE (32u << 20) /* flexible memory left for malloc and the libraries */

/* OpenOrbis's libkernel.h passes the outputs by value: our own prototypes */
int32_t ps4_avail_flex(size_t *size) __asm__("sceKernelAvailableFlexibleMemorySize");
int32_t ps4_avail_direct(off_t start, off_t end, size_t align, off_t *phys, size_t *size)
    __asm__("sceKernelAvailableDirectMemorySize");

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
  size_t i, flex = 0, direct = 0;
  off_t phys = 0;
  int rf = ps4_avail_flex(&flex);
  int rd = ps4_avail_direct(0, (off_t)sceKernelGetDirectMemorySize(), 0, &phys, &direct);
  host_log("gpu: memory free: flexible %zu MiB (0x%x), direct %zu MiB in one block (0x%x) of "
           "%zu MiB", flex >> 20, (unsigned)rf, direct >> 20, (unsigned)rd,
           (size_t)sceKernelGetDirectMemorySize() >> 20);
  for (i = 0; i < sizeof k_mem / sizeof k_mem[0]; i++) {
    size_t want_flex = ((size_t)k_mem[i][2] << 20) + FLEX_SPARE;
    size_t want_direct = (size_t)(k_mem[i][0] + k_mem[i][1] + k_mem[i][3] + k_mem[i][4]) << 20;
    if ((rf < 0 || flex >= want_flex) && (rd < 0 || direct >= want_direct))
      break;
  }
  if (i == sizeof k_mem / sizeof k_mem[0])
    i--; /* the smallest, whatever was measured */
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
  host_log("gpu: Piglet memory %u/%u/%u/%u/%u MiB (system, video, flexible, commands, LCUE)",
           k_mem[i][0], k_mem[i][1], k_mem[i][2], k_mem[i][3], k_mem[i][4]);
  if (!scePigletSetConfigurationVSH(&cfg)) {
    host_log("gpu: scePigletSetConfigurationVSH refused it");
    return -1;
  }
  s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (s_dpy == EGL_NO_DISPLAY) {
    OrbisPglConfig got;
    memset(&got, 0, sizeof got);
    got.size = sizeof got;
    host_log("gpu: eglGetDisplay refused it: 0x%x", (unsigned)eglGetError());
    if (scePigletGetConfigurationVSH(&got))
      host_log("gpu: Piglet's configuration now: size 0x%x flags 0x%x order %u system %llu MiB "
               "video %llu MiB flexible %llu MiB commands %u KiB LCUE %u KiB", got.size, got.flags,
               got.processOrder, (unsigned long long)(got.systemSharedMemorySize >> 20),
               (unsigned long long)(got.videoSharedMemorySize >> 20),
               (unsigned long long)(got.maxMappedFlexibleMemory >> 20),
               got.drawCommandBufferSize >> 10, got.lcueResourceBufferSize >> 10);
    else
      host_log("gpu: scePigletGetConfigurationVSH failed too");
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
  if (egl_up() != 0 || link(&s_prog[P_UNTEX], k_vs_prim, "prim", k_fs_untex, 0, "untex") != 0 ||
      link(&s_prog[P_TEX], k_vs_prim, "prim", k_fs_tex, 0, "tex") != 0 ||
      link(&s_prog[P_VRAM], k_vs_quad, "quad", k_fs_vram, 1, "vram") != 0 ||
      link(&s_prog[P_BLIT], k_vs_quad, "quad", k_fs_blit, 1, "blit") != 0) {
    host_log("gpu: the GLES2 renderer couldn't start");
    if (s_shader_mode == SH_BINARY && !g_ps4_opts.shader_compile) {
      /* the shipped binaries don't suit this firmware's Piglet: the next
       * start compiles with the player's pair, if they have it */
      ps4_marker_write(PS4_SHADER_COMPILE, "the shipped shaders failed on this Piglet");
      host_log("gpu: wrote " PS4_SHADER_COMPILE ": the next start compiles with the player's "
               "Piglet pair (" LIB_DIR ")");
    }
    egl_down();
    return -1;
  }
  s_cur_prog = -1;
  s_vram_tex = new_tex(VW / 2, VH, GL_RGBA, 0);
  glGenBuffers(1, &s_vbo);
  glGenBuffers(1, &s_qbo);
  glGenBuffers(1, &s_sbo);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DITHER);
  s_dx0 = 0, s_dy0 = 0, s_dx1 = VW - 1, s_dy1 = VH - 1; /* the first draw uploads it all */
  gl_check("init");
  { /* a bare clear and swap before any of the game's work, each step logged
     * (GPU runs 3-5 hung in their first eglSwapBuffers) */
    unsigned long long t;
    int k;
    for (k = 0; k < 2; k++) {
      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      host_log("gpu: start-up swap %d (interval %d): glFinish", k, g_ps4_opts.gpu_vsync);
      host_log_flush();
      g_ps4_where = "glFinish (start-up)";
      t = ps4_now_us();
      glFinish();
      host_log("gpu: start-up swap %d: glFinish %llu us; eglSwapBuffers", k, ps4_now_us() - t);
      host_log_flush();
      g_ps4_where = "eglSwapBuffers (start-up)";
      t = ps4_now_us();
      if (!eglSwapBuffers(s_dpy, s_surf))
        host_log("gpu: start-up swap %d: eglSwapBuffers: 0x%x", k, (unsigned)eglGetError());
      host_log("gpu: start-up swap %d: eglSwapBuffers %llu us", k, ps4_now_us() - t);
      host_log_flush();
    }
    g_ps4_where = "starting";
  }
  host_log("gpu: the GLES2 renderer is up in %llu ms (1080p targets, the software rasterizer "
           "keeping VRAM)", (ps4_now_us() - t0) / 1000);
  return 0;
}

/* The PICTURE rows (picture_ps4.c). persp, subpixel: the vertex cache
 * records the GTE's projections (mode 2) and the walk leaves each polygon's
 * answers (psx_vcache.h); the walk's direct vertices (D99) skip it, so
 * they're off meanwhile. From the next vertex buffer on. */
void ps4_gpu_set_options(int persp, int subpixel) {
  extern int g_nd_direct_ok;
  if (!g_ps4_gpu)
    return;
  flush();
  s_persp = persp != 0;
  s_subpixel = subpixel != 0;
  s_vc = s_persp || s_subpixel;
  psx_vcache_enable(s_vc ? 2 : 0);
  g_nd_direct_ok = !s_vc;
  s_side_ok = 0;
  s_vbo_ok = 0;
  host_log("gpu: persp=%d subpixel=%d (the vertex cache %s)", s_persp, s_subpixel,
           s_vc ? "on" : "off");
}

/* res: 0 (native) the targets at 1080p, 1 (ps1) at the PS1's own pixels;
 * every target is made again at the new size from VRAM (which the software
 * rasterizer keeps), and the presentation textures with the next capture */
void ps4_gpu_set_res(int res) {
  float sx = res ? 1.0f : 1920.0f / 512.0f, sy = res ? 1.0f : 1080.0f / 224.0f;
  int had = s_cur != NULL;
  if (!g_ps4_gpu || (sx == s_sx && sy == s_sy))
    return;
  flush();
  while (s_nt)
    target_free(&s_t[0]);
  s_sx = sx, s_sy = sy;
  s_cur = had ? target_for(s_cx0, s_cy0, s_cx1 - s_cx0 + 1, s_cy1 - s_cy0 + 1) : NULL;
  host_log("gpu: res=%s: targets at %.2fx%.2f a VRAM pixel", res ? "ps1" : "native", s_sx, s_sy);
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

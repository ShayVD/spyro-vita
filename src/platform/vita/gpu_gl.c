/* Vita GPU renderer (gpu=1, decisions D35): PsyCross's GR_* interface on
 * vitaGL. PsyX_GPU.cpp's OT walker hands us the same GrVertex triangles and
 * splits as the software rasterizer (src/platform/linux32/gpu_soft.c); here
 * they are drawn by the GPU at the screen's resolution.
 *
 *   - s_vram is the PS1 VRAM as the CPU sees it (LoadImage, ClearImage,
 *     StoreImage). It is also the texture every textured prim samples: the
 *     shaders read the 16-bit words themselves (4/8-bit CLUT lookup, 15-bit,
 *     texel 0 transparent, STP), as PsyX_render.cpp's shaders do.
 *   - Each draw area the game draws into (Spyro: its two 512x224 buffers) is
 *     a "target": an FBO covering that VRAM rect at a higher resolution
 *     (config.txt res=: the screen's pixels by default, decisions D40).
 *     Inside its rect a target is newer than s_vram; LoadImage/MoveImage into
 *     it are drawn into it too (a MoveImage from another target at the
 *     targets' resolution), ClearImage clears both.
 *   - s_vram gets a target's pixels back only when something reads that
 *     VRAM: StoreImage (the pause and inventory background), MoveImage from
 *     it, a textured prim whose page overlaps it, a screenshot, a 24-bit
 *     display. By default each target keeps a log of what was drawn into it
 *     since it was last cleared whole, and the software rasterizer replays
 *     it: the PS1's exact pixels, no GPU sync (gpu_readback=0, or a log that
 *     grew too long: drawn down to 1x and read with glReadPixels).
 *   - The present draws the displayed VRAM rect from s_vram, then the targets
 *     on top, 4:3 into the 960x544 screen.
 * Semi-transparency follows the PS1: only texels with STP set blend (one
 * premultiplied pass for average/add/quarter, two passes for subtract).
 * Not modelled (as in gpu_soft.c): mask bit, texture window, dithering.
 * Optional (decisions D39, all off by default): tex_filter= filters textures
 * after the palette lookup; persp= and subpixel= use the GTE's exact
 * projections (src/platform/common/vcache.c) for perspective-correct
 * textures and sub-pixel vertices. Any of them switches to an expanded
 * vertex (XVertex); with all off the draws are as before. */
#include "vita.h"

#include <assert.h> /* PsyCross headers use static_assert */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include <vitashark.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>

#include "PsyX/PsyX_render.h"
#include "gpu/PsyX_GPU.h"
#include "../../game/present.h"
#include "psx_vcache.h"

#define VW 1024
#define VH 512
#define SCREEN_W 960
#define SCREEN_H 544
/* The 4:3 picture's width on the screen (and at res=native the targets').
 * 544 * 4 / 3 is 725, but a 725-wide render target lost its last column
 * (black in every Vita3K capture, main's renderer too); 726 fills it. */
#define AREA43_W 726
#define MAX_TARGETS 8
#define MAX_PENDING 64

uint8_t *host_display_rgb(int *pw, int *ph); /* linux32/png.c */
int host_png_write(const char *path, const uint8_t *rgb, int w, int h);

static uint16_t s_vram[VW * VH];
static int s_vram_dirty = 1; /* s_vram newer than the GPU's copy */
static int s_aflush = 1;     /* s_vram changed: the decoded atlas starts over (D41) */

typedef struct { /* PsyX_GPU.cpp's GPUDrawSplit starts like this (gpu_soft.c) */
  DRAWENV drawenv;
  DISPENV dispenv;
} SplitHead;

/* What has been drawn into a target since its log began (when it was made,
 * or last cleared whole). Replayed through the software rasterizer, it gives
 * s_vram the target's pixels as the PS1 (and gpu=0) would have drawn them,
 * with no GPU readback (which Vita3K can't do; decisions D35). */
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
static float s_sx, s_sy; /* target pixels per VRAM pixel */

typedef struct {
  Target *t;
  int x, y, w, h;
} Pending; /* VRAM rects LoadImage/MoveImage wrote inside a target */
static Pending s_pending[MAX_PENDING];
static int s_npending;

enum { P_UNTEX, P_T4, P_T8, P_T16, P_VRAM15, P_TEX, P_COUNT };
typedef struct {
  GLuint prog;
  GLint xf, semi, vram, tex, atlas;
} Prog;
static Prog s_prog[P_COUNT];

/* The primitive programs (P_UNTEX..P_T16) of each picture option set: off;
 * expanded vertex, nearest texel from VRAM; nearest texel from the atlas
 * (tex_cache); bilinear by hand (tex_filter=1's blended draws, which need
 * STP per texel); 3-point (tex_filter=2); the GPU's bilinear (tex_filter=1's
 * opaque draws, D43). Only the configured set (and what it falls back to)
 * is built, or all of them for bench= (D41). */
enum { V_PLAIN, V_X0, V_A0, V_FB, V_F3, V_HB, V_COUNT };
static Prog s_vprog[V_COUNT][P_T16 + 1];
static Prog s_dprog[3]; /* the atlas's decode pass, per texture format */

static GLuint s_vram_tex, s_vbo, s_rgb_tex, s_down_tex, s_down_fbo;
static int s_down_w, s_down_h;

/* the split being drawn (GR_SetupClipMode .. GR_DrawTriangles) */
static Target *s_cur;
static int s_org_x, s_org_y;
static int s_clip_x0, s_clip_y0, s_clip_x1, s_clip_y1; /* VRAM, exclusive end */
static int s_texmode = P_UNTEX; /* P_UNTEX..P_T16, or -1: not drawn */
static BlendMode s_blend;
static GLuint s_bound_fbo = (GLuint)-1;
static unsigned s_gen, s_use;

static SplitHead s_head; /* the split's envs, for the target's log */
static int s_head_enable;
static TextureID s_texid;
static TexFormat s_texfmt;

static GrVertex s_vbuf[MAX_VERTEX_BUFFER_SIZE];
static const GrVertex *s_vorig; /* PsyX's own buffer, as it was given */
static int s_nverts;

/* The expanded vertex (D39, D43): PsyX's own vertex, copied whole, then
 * what the options add. g.tcx/g.tcy (the vcache's bytes in PsyX's buffer)
 * become "filtered from the atlas" and "wraps within its tile". dxy and w
 * are the vcache's sub-pixel offset (1/256 pixel) and depth: the GPU
 * divides by w when it interpolates (w = 1 is the PS1's affine mapping). */
typedef struct {
  GrVertex g;
  int16_t box[4]; /* u0, v0, u1, v1: page texels the filter may read */
  int16_t off[2]; /* atlas texel minus page texel, for the triangle (D41) */
  int16_t dxy[2];
  float w;
} XVertex;
static XVertex s_xbuf[MAX_VERTEX_BUFFER_SIZE];
/* per triangle: 0 nearest texel from VRAM, 1 nearest from the atlas,
 * 2 filtered from the atlas */
static uint8_t s_tkind[MAX_VERTEX_BUFFER_SIZE / 3 + 1];
static int s_xmode;    /* any of the three below: draws use s_xbuf */
static int s_xattr;    /* attribute arrays 3-6 are enabled */
static int s_filter;   /* tex_filter=: 0 off, 1 bilinear, 2 3-point (D43) */
static int s_persp;    /* persp=1 */
static int s_subpixel; /* subpixel=1 */
static int s_cache;    /* tex_cache=1: unfiltered textures from the atlas too (D41) */
static int s_vc;       /* persp or subpixel: the vcache records and is read (D43) */

/* per 300-vblank report */
static struct {
  unsigned draws, tris, uploads, readbacks, replays, copies, moves, presents;
  uint64_t upload_us, readback_us, expand_us;
} s_st;

static uint64_t now_us(void) { return sceKernelGetProcessTimeWide(); }

/* One frame per report is timed on the GPU: glFinish before its first draw
 * (the GPU is idle from there) and again before its swap. "frame" is from
 * that first draw to the GPU being done, CPU work included; "tail" is how
 * long the GPU still worked after the CPU had sent everything. A small tail
 * means the GPU keeps up with the CPU at this resolution. */
static struct {
  int state; /* 0 idle, 1 armed, 2 timing */
  uint64_t t0;
  unsigned frame_us, tail_us, n, seq, taken;
} s_sample;

/* bench= (D41): time the next picture too, and read the last one timed */
void vita_gpu_arm_sample(void) {
  if (!s_sample.state)
    s_sample.state = 1;
}

int vita_gpu_take_sample(unsigned *frame_us, unsigned *tail_us) {
  if (s_sample.seq == s_sample.taken)
    return 0;
  s_sample.taken = s_sample.seq;
  *frame_us = s_sample.frame_us;
  *tail_us = s_sample.tail_us;
  return 1;
}

static void sample_begin(void) {
  if (s_sample.state == 1) {
    glFinish();
    s_sample.t0 = now_us();
    s_sample.state = 2;
  }
}

/* every swap goes through here */
static void swap(void) {
  if (s_sample.state == 2) {
    uint64_t t1 = now_us(), t2;
    glFinish();
    t2 = now_us();
    s_sample.frame_us = (unsigned)(t2 - s_sample.t0);
    s_sample.tail_us = (unsigned)(t2 - t1);
    s_sample.n++;
    s_sample.seq++;
    s_sample.state = 0;
  }
  vglSwapBuffers(GL_FALSE);
}

/* ---- shaders (GLSL ES 1.00, run through vitaGL's translator) ---- */

/* No divisions and no mod(): Vita3K's shader recompiler mishandles some
 * (a floor() result divided by a constant vector, as a texture coordinate,
 * stops the frame). Every divisor here is a power of two, so multiplying by
 * its reciprocal is exact, and md(x, n) is mod() for n = 2^k. */
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

/* k_vs_prim for the expanded vertex (D39). Clip coordinates times w: the
 * same picture for w = 1, perspective-correct varyings otherwise. */
static const char k_vs_prim_x[] =
    "attribute vec4 a_pos;\n"   /* x, y, tpage, clut (shorts), as k_vs_prim */
    "attribute vec4 a_tex;\n"   /* u, v, bright, dither (bytes) */
    "attribute vec4 a_col;\n"
    "attribute vec4 a_box;\n"   /* u0, v0, u1, v1: page texels the filter may read */
    "attribute vec4 a_offd;\n"  /* atlas minus page texel; sub-pixel offset (1/256) */
    "attribute float a_w;\n"
    "attribute vec2 a_flags;\n" /* filtered from the atlas, wraps (bytes) */
    "uniform vec4 u_xf;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_pc;\n"
    "varying vec4 v_box;\n"
    "varying vec2 v_filt;\n"
    "varying vec2 v_off;\n"
    GLSL_MD
    "void main() {\n"
    "  vec2 qxy = a_pos.xy + a_offd.zw * (1.0 / 256.0);\n"
    "  gl_Position = vec4(((qxy + u_xf.xy) * u_xf.zw - 1.0) * a_w, 0.0, a_w);\n"
    "  v_uv = a_tex.xy;\n"
    "  v_filt = a_flags;\n"
    "  v_box = a_box;\n"
    "  v_off = a_offd.xy;\n"
    "  v_col = a_col;\n"
    "  float qpage = md(a_pos.z, 65536.0, 1.0 / 65536.0);\n"
    "  float qclut = md(a_pos.w, 65536.0, 1.0 / 65536.0);\n"
    "  v_pc = vec4(md(qpage, 16.0, 0.0625) * 64.0, md(floor(qpage * 0.0625), 2.0, 0.5) * 256.0,\n"
    "              md(qclut, 64.0, 0.015625) * 16.0, md(floor(qclut * 0.015625), 512.0, 1.0 / 512.0));\n"
    "}\n";

/* VRAM word (qx, qy) as its low and high byte, 0..255. The texture is
 * 512x512 RGBA8: texel i holds words 2i (r, g) and 2i+1 (b, a). Names are
 * q-prefixed: vitaGL's translator adds its own (a 'y' among them). */
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

/* The texel quv of the page qpc (VRAM words: page x, y; clut x, y) as its
 * 16-bit colour qc (low, high byte), for FMT 0 (4-bit), 1 (8-bit), 2 (15-bit) */
#define TEX_DECODE                                                             \
  "#if FMT == 0\n"                                                             \
  "  vec2 qword = vram(qpc.x + floor(quv.x * 0.25), qpc.y + quv.y);\n"         \
  "  float qn = md(quv.x, 4.0, 0.25);\n"                                       \
  "  float qbyte = mix(qword.x, qword.y, step(2.0, qn));\n"                    \
  "  float qhi = floor(qbyte * 0.0625);\n"                                     \
  "  float qidx = mix(qbyte - qhi * 16.0, qhi, md(qn, 2.0, 0.5));\n"           \
  "  vec2 qc = vram(qpc.z + qidx, qpc.w);\n"                                   \
  "#elif FMT == 1\n"                                                           \
  "  vec2 qword = vram(qpc.x + floor(quv.x * 0.5), qpc.y + quv.y);\n"          \
  "  vec2 qc = vram(qpc.z + mix(qword.x, qword.y, md(quv.x, 2.0, 0.5)), qpc.w);\n" \
  "#else\n"                                                                    \
  "  vec2 qc = vram(qpc.x + quv.x, qpc.y + quv.y);\n"                          \
  "#endif\n"

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

/* tex_filter= (D39, D41): textured polygons are filtered from the decoded
 * atlas (v_filt.x), where each texel is already RGB with its kind in alpha:
 * 0 transparent, 1 opaque, 0.5 opaque with STP. The filter (FILTER 1:
 * N64-style 3-point, 2: bilinear) reads texels clamped to the triangle's UV
 * box, so neighbouring textures never bleed in, or wrapped within its
 * 32x32 tile (v_filt.y; expand() says which). Transparent texels get no
 * weight and the colour is renormalised over the rest, so cut-out edges
 * don't darken; a pixel is drawn where at least half the weight is opaque.
 * STP is the weighted majority, the same in both subtract passes.
 * Everything else (sprites, tiles, anything not in the atlas) is drawn by
 * the persp/subpixel-only programs, which sample the nearest texel from
 * VRAM: draw() splits a draw into runs of each. One shader with both paths
 * behind a branch drew nothing in Vita3K (texture reads in a dynamic
 * branch), so there is no branch. */
static const char k_fs_filt_body[] =
    "varying vec4 v_box;\n"
    "varying vec2 v_filt;\n"
    "varying vec2 v_off;\n"
    "uniform sampler2D s_atlas;\n"
    /* rgb; w: 0 transparent, 1 opaque, 2 opaque with STP; q in the page */
    "vec4 qtap(vec2 q) {\n"
    "  vec4 qt = texture2D(s_atlas, (q + floor(v_off + 0.5) + 0.5) * (1.0 / 2048.0));\n"
    "  return vec4(qt.xyz, step(0.5, qt.w) * (1.0 + step(qt.w, 0.998)));\n"
    "}\n"
    "void main() {\n"
    "  vec3 qrgb;\n"
    "  float qstp;\n"
    "  {\n"
    "    vec4 qbox = floor(v_box + 0.5);\n"
    "    float qwrap = step(0.5, v_filt.y);\n"
    "    vec2 qf = v_uv - 0.5;\n"
    "    vec2 qb = floor(qf);\n"
    "    vec2 qfr = qf - qb;\n"
    "    vec2 qr0 = qb - qbox.xy, qr1 = qr0 + 1.0;\n"
    "    vec2 q0 = mix(clamp(qb, qbox.xy, qbox.zw), qbox.xy + qr0 - 32.0 * floor(qr0 * 0.03125), qwrap);\n"
    "    vec2 q1 = mix(clamp(qb + 1.0, qbox.xy, qbox.zw), qbox.xy + qr1 - 32.0 * floor(qr1 * 0.03125), qwrap);\n"
    "    vec4 qt10 = qtap(vec2(q1.x, q0.y));\n"
    "    vec4 qt01 = qtap(vec2(q0.x, q1.y));\n"
    "#if FILTER == 1\n"
    "    float qlo = step(qfr.x + qfr.y, 1.0);\n"
    "    vec4 qta = qtap(mix(q1, q0, qlo));\n"
    "    vec3 qw = mix(vec3(qfr.x + qfr.y - 1.0, 1.0 - qfr.y, 1.0 - qfr.x),\n"
    "                  vec3(1.0 - qfr.x - qfr.y, qfr.x, qfr.y), qlo);\n"
    "    vec4 qtb = vec4(0.0);\n"
    "    float qwb = 0.0;\n"
    "#else\n"
    "    vec4 qta = qtap(q0);\n"
    "    vec4 qtb = qtap(q1);\n"
    "    vec3 qw = vec3((1.0 - qfr.x) * (1.0 - qfr.y), qfr.x * (1.0 - qfr.y), (1.0 - qfr.x) * qfr.y);\n"
    "    float qwb = qfr.x * qfr.y;\n"
    "#endif\n"
    "    vec4 qcode = vec4(qta.w, qt10.w, qt01.w, qtb.w);\n"
    "    vec4 qa = vec4(qw, qwb) * min(qcode, 1.0);\n"
    "    float qsum = qa.x + qa.y + qa.z + qa.w;\n"
    "    if (qsum < 0.5) discard;\n"
    "    qrgb = (qta.xyz * qa.x + qt10.xyz * qa.y + qt01.xyz * qa.z + qtb.xyz * qa.w) / qsum;\n"
    "    qstp = step(0.5 * qsum, dot(qa, step(1.5, qcode)));\n"
    "  }\n"
    "  if ((u_semi.z == 1.0 && qstp > 0.5) || (u_semi.z == 2.0 && qstp < 0.5)) discard;\n"
    "  qrgb = qrgb * v_col.xyz * (255.0 / 128.0);\n"
    "  gl_FragColor = mix(vec4(qrgb, 1.0), vec4(qrgb * u_semi.x, u_semi.y), qstp);\n"
    "}\n";

/* tex_cache= (D41): the nearest texel, from the decoded atlas: one fetch
 * instead of k_fs_tex_body's index and CLUT reads, the same pixels. The
 * page coordinates are interpolated as k_fs_tex_body's are and the atlas
 * offset added after: atlas coordinates (up to 1023) interpolated lost
 * enough precision to pick the neighbouring texel at a few pixels. */
static const char k_fs_atlas_body[] =
    "varying vec4 v_box;\n"
    "varying vec2 v_off;\n"
    "uniform sampler2D s_atlas;\n"
    "void main() {\n"
    "  vec4 qbox = floor(v_box + 0.5);\n"
    "  vec2 q = clamp(floor(v_uv + 0.002), qbox.xy, qbox.zw) + floor(v_off + 0.5);\n"
    "  vec4 qt = texture2D(s_atlas, (q + 0.5) * (1.0 / 2048.0));\n"
    "  if (qt.w < 0.5) discard;\n"
    "  float qstp = step(qt.w, 0.998);\n"
    "  if ((u_semi.z == 1.0 && qstp > 0.5) || (u_semi.z == 2.0 && qstp < 0.5)) discard;\n"
    "  vec3 qrgb = qt.xyz * v_col.xyz * (255.0 / 128.0);\n"
    "  gl_FragColor = mix(vec4(qrgb, 1.0), vec4(qrgb * u_semi.x, u_semi.y), qstp);\n"
    "}\n";

/* tex_filter=1 on opaque draws (D43): the atlas sampled with the GPU's own
 * bilinear filter, one fetch. The manual taps above cost the hardware about
 * as much as the old palette decode (13 ms of GPU a frame at native
 * resolution). Transparent texels are black with alpha 0, so the fetched
 * colour is already weighted by coverage: dividing by alpha renormalises
 * it, and a pixel is drawn where at least half the weight is opaque, as in
 * k_fs_filt_body. The UV is clamped half a texel inside the box, so the
 * neighbour block gets no weight, or wrapped within the tile, whose atlas
 * entry has a 16-texel margin decoded from the tile's other side. STP plays
 * no part in an opaque draw. */
static const char k_fs_hw_body[] =
    "varying vec4 v_box;\n"
    "varying vec2 v_filt;\n"
    "varying vec2 v_off;\n"
    "uniform sampler2D s_atlas;\n"
    "void main() {\n"
    "  vec4 qbox = floor(v_box + 0.5);\n"
    "  vec2 qr = v_uv - qbox.xy;\n"
    "  vec2 qp = mix(clamp(v_uv, qbox.xy + 0.5, qbox.zw + 0.5),\n"
    "                qbox.xy + qr - 32.0 * floor(qr * 0.03125), step(0.5, v_filt.y));\n"
    "  vec4 qt = texture2D(s_atlas, (qp + floor(v_off + 0.5)) * (1.0 / 2048.0));\n"
    "  if (qt.w < 0.5) discard;\n"
    "  gl_FragColor = vec4(qt.xyz * (1.0 / qt.w) * v_col.xyz * (255.0 / 128.0), 1.0);\n"
    "}\n";

/* The atlas's decode pass (D41, D43): one texel of a page -> RGB, and in
 * alpha 0 transparent (black), 1 opaque, 254/255 opaque with STP. v_src is
 * the texel relative to v_org.xy, wrapped every v_org.z texels (32: a tile
 * with its margin; 256: no wrap); v_org.w = 1 / v_org.z. */
static const char k_vs_decode[] =
    "attribute vec2 a_ndc;\n"
    "attribute vec2 a_src;\n" /* texel from the origin (centres at +0.5) */
    "attribute vec2 a_pc;\n"  /* tpage, clut */
    "attribute vec4 a_org;\n" /* origin in the page, period, 1 / period */
    "varying vec2 v_src;\n"
    "varying vec4 v_pc;\n"
    "varying vec4 v_org;\n"
    GLSL_MD
    "void main() {\n"
    "  gl_Position = vec4(a_ndc, 0.0, 1.0);\n"
    "  v_src = a_src;\n"
    "  v_org = a_org;\n"
    "  float qpage = md(a_pc.x, 65536.0, 1.0 / 65536.0);\n"
    "  float qclut = md(a_pc.y, 65536.0, 1.0 / 65536.0);\n"
    "  v_pc = vec4(md(qpage, 16.0, 0.0625) * 64.0, md(floor(qpage * 0.0625), 2.0, 0.5) * 256.0,\n"
    "              md(qclut, 64.0, 0.015625) * 16.0, md(floor(qclut * 0.015625), 512.0, 1.0 / 512.0));\n"
    "}\n";

static const char k_fs_decode[] =
    "varying vec2 v_src;\n"
    "varying vec4 v_pc;\n"
    "varying vec4 v_org;\n" FS_VRAM
    "void main() {\n"
    "  vec2 qs = floor(v_src);\n"
    "  vec2 quv = floor(v_org.xy + 0.5) + qs - v_org.z * floor(qs * v_org.w + 0.001);\n"
    "  vec4 qpc = floor(v_pc + 0.5);\n"
    TEX_DECODE
    "  gl_FragColor = vec4(rgb15(qc), step(0.5, qc.x + qc.y) * mix(1.0, 254.0 / 255.0, step(128.0, qc.y)));\n"
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

/* vitaGL reports GL_COMPILE_STATUS true even when its shader compiler failed
 * (and the program then can't be used), so the compiler's own messages are
 * what tells: an error during a compile fails it. */
static int s_shark_errors;

static void shark_log(const char *msg, shark_log_level level, int line) {
  vita_log("gpu: shader compiler %s, line %d: %s",
           level == SHARK_LOG_ERROR ? "error" : level == SHARK_LOG_WARNING ? "warning" : "info",
           line, msg);
  if (level == SHARK_LOG_ERROR)
    s_shark_errors++;
}

static GLuint compile(GLenum type, const char *const *src, int n, const char *what) {
  GLuint s = glCreateShader(type);
  GLint ok = 0;
  int errors = s_shark_errors;
  glShaderSource(s, n, src, NULL);
  glCompileShader(s);
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok || s_shark_errors != errors) {
    char info[1024] = "";
    glGetShaderInfoLog(s, sizeof info, NULL, info);
    vita_log("gpu: %s %s shader does not compile: %s", what,
             type == GL_VERTEX_SHADER ? "vertex" : "fragment", info);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

static int link(Prog *p, const char *const *vs, int nvs, const char *const *fs, int nfs,
                int quad, const char *what) {
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
    if (vs[0] == k_vs_decode) {
      glBindAttribLocation(p->prog, 3, "a_pc");
      glBindAttribLocation(p->prog, 4, "a_org");
    }
  } else {
    glBindAttribLocation(p->prog, 0, "a_pos");
    glBindAttribLocation(p->prog, 1, "a_tex");
    glBindAttribLocation(p->prog, 2, "a_col");
    if (vs[0] == k_vs_prim_x) {
      glBindAttribLocation(p->prog, 3, "a_box");
      glBindAttribLocation(p->prog, 4, "a_offd");
      glBindAttribLocation(p->prog, 5, "a_w");
      glBindAttribLocation(p->prog, 6, "a_flags");
    }
  }
  glLinkProgram(p->prog);
  glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char info[1024] = "";
    glGetProgramInfoLog(p->prog, sizeof info, NULL, info);
    vita_log("gpu: %s program does not link: %s", what, info);
    return -1;
  }
  p->xf = glGetUniformLocation(p->prog, "u_xf");
  p->semi = glGetUniformLocation(p->prog, "u_semi");
  p->vram = glGetUniformLocation(p->prog, "s_vram");
  p->tex = glGetUniformLocation(p->prog, "s_tex");
  p->atlas = glGetUniformLocation(p->prog, "s_atlas");
  glUseProgram(p->prog);
  if (p->vram >= 0)
    glUniform1i(p->vram, 0);
  if (p->tex >= 0)
    glUniform1i(p->tex, 1);
  if (p->atlas >= 0) /* unit 1, s_tex's: vitaGL ignored a third unit (D41) */
    glUniform1i(p->atlas, 1);
  return 0;
}

static int variant_of(int filter, int cache, int xmode) {
  return filter == 1 ? V_FB : filter == 2 ? V_F3 : cache ? V_A0 : xmode ? V_X0 : V_PLAIN;
}

/* The primitive programs of option set v; 0 = ok. */
static int build_variant(int v) {
  static const char *const fmt_def[3] = {"#define FMT 0\n", "#define FMT 1\n", "#define FMT 2\n"};
  static const char *const filt_def[3] = {"", "#define FILTER 1\n", "#define FILTER 2\n"};
  const char *vs[1] = {v == V_PLAIN ? k_vs_prim : k_vs_prim_x};
  const char *fs[4];
  /* FILTER 1 in k_fs_filt_body is 3-point, 2 bilinear */
  int i, filter = v == V_F3 ? 1 : v == V_FB ? 2 : 0, atlas = filter || v == V_A0 || v == V_HB;
  if (s_vprog[v][P_UNTEX].prog)
    return 0;
  /* what the atlas doesn't hold, the sprites, and tex_filter=1's opaque draws */
  if (atlas && (build_variant(V_X0) || (v != V_A0 && build_variant(V_A0)) ||
                (v == V_FB && build_variant(V_HB))))
    return -1;
  fs[0] = k_fs_prim_head;
  fs[1] = k_fs_untex;
  if (link(&s_vprog[v][P_UNTEX], vs, 1, fs, 2, 0, "untextured"))
    return -1;
  for (i = 0; i < 3; i++) {
    fs[0] = fmt_def[i];
    fs[1] = k_fs_prim_head;
    fs[2] = k_fs_tex_body;
    if (filter) {
      fs[2] = filt_def[filter];
      fs[3] = k_fs_filt_body;
    } else if (v == V_HB) {
      fs[2] = k_fs_hw_body;
    } else if (atlas) {
      fs[2] = k_fs_atlas_body;
    }
    if (link(&s_vprog[v][P_T4 + i], vs, 1, fs, filter ? 4 : 3, 0,
             i == 0 ? "4-bit" : i == 1 ? "8-bit" : "15-bit"))
      return -1;
  }
  if (atlas && !s_dprog[0].prog) {
    const char *dvs[1] = {k_vs_decode};
    for (i = 0; i < 3; i++) {
      fs[0] = fmt_def[i];
      fs[1] = k_fs_decode;
      if (link(&s_dprog[i], dvs, 1, fs, 2, 1, "atlas decode"))
        return -1;
    }
  }
  return 0;
}

/* 0 = ok. The blit alone is enough to show the software rasterizer's
 * picture: if only the others fail, *present_only is set. */
static int build_programs(int *present_only) {
  const char *qvs[1] = {k_vs_quad};
  const char *fs[1];
  int v;
  fs[0] = k_fs_tex;
  if (!s_prog[P_TEX].prog && link(&s_prog[P_TEX], qvs, 1, fs, 1, 1, "blit"))
    return -1;
  *present_only = 1;
  fs[0] = k_fs_vram15;
  if (!s_prog[P_VRAM15].prog && link(&s_prog[P_VRAM15], qvs, 1, fs, 1, 1, "vram15"))
    return 0;
  for (v = 0; v < V_COUNT; v++)
    if ((g_vita_opts.bench || v == variant_of(s_filter, s_cache, s_xmode)) && build_variant(v))
      return 0;
  memcpy(s_prog, s_vprog[variant_of(s_filter, s_cache, s_xmode)], sizeof s_vprog[0]);
  *present_only = 0;
  return 0;
}

/* ---- textures and targets ---- */

static GLuint new_tex(int w, int h, const void *rgba) {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return t;
}

static void bind_fbo(GLuint fbo) {
  if (s_bound_fbo != fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    s_bound_fbo = fbo;
  }
}

/* s_vram -> the GPU, whole (1 MB). glTexImage2D gives the texture fresh
 * storage each time, so draws already queued keep sampling the old VRAM and
 * vitaGL frees it once the GPU is done. (Pointing the texture at our own
 * block with vglOverloadTexDataPointer was cheaper, but in Vita3K only the
 * first block was ever sampled.) */
static void upload_vram(void) {
  uint64_t t0;
  int i;
  if (!s_vram_dirty)
    return;
  t0 = now_us();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VW / 2, VH, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_vram);
  s_vram_dirty = 0;
  for (i = 0; i < s_ntargets; i++)
    s_targets[i].tex_stale = 0;
  s_st.uploads++;
  s_st.upload_us += now_us() - t0;
}

static void quad(const float ndc[4], const float src[4]) {
  /* two triangles: x0,y0 x1,y0 x0,y1 / x1,y0 x1,y1 x0,y1 */
  float p[12], s[12];
  static const int ix[6] = {0, 2, 0, 2, 2, 0}, iy[6] = {1, 1, 3, 1, 3, 3};
  int i;
  for (i = 0; i < 6; i++) {
    p[i * 2] = ndc[ix[i]];
    p[i * 2 + 1] = ndc[iy[i]];
    s[i * 2] = src[ix[i]];
    s[i * 2 + 1] = src[iy[i]];
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  if (s_xattr) {
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);
    glDisableVertexAttribArray(6);
    s_xattr = 0;
  }
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, p);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, s);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void no_blend(void) {
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
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
 * y + dy): a MoveImage between targets (the game-over and credits freeze
 * frames) keeps the targets' resolution. s_vram got the same move. */
static void move_into(Target *t, const Target *src, int x, int y, int w, int h, int dx, int dy) {
  float ndc[4], uv[4];
  bind_fbo(t->fbo);
  glViewport(0, 0, t->pw, t->ph);
  no_blend();
  glUseProgram(s_prog[P_TEX].prog);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, src->tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture(GL_TEXTURE0);
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

/* ---- decoded-texture atlas (tex_filter, D41) ----
 *
 * The filter reads 3 or 4 texels a pixel. Decoding each one from VRAM
 * (index fetch, nibble or byte, CLUT fetch, 15-bit unpack) made the
 * filtered shader several times the SGX's budget on hardware. So each
 * texture block a filtered polygon uses (format, page, CLUT, and the 32x32
 * texel blocks its UV box covers) is decoded once, by a GPU pass, into a
 * 1024x1024 RGBA8 atlas, and kept there while VRAM stays as it is (in
 * gameplay it does: no VRAM uploads for minutes). Any change to s_vram
 * starts over, in the other of two atlases, so draws already queued keep
 * theirs. Blocks over a target (a page or CLUT the game draws into) are
 * never cached: those polygons take the nearest texel from VRAM, as do
 * sprites and anything once the atlas is full. A repeating 32x32 tile
 * (wrap) takes 64x64: the tile with a 16-texel margin from its other
 * sides, so the GPU's own bilinear filter crosses its edges (D43). 2048x2048:
 * at 16:9 the 1024 atlas was 824-900 blocks full on hardware already. */
#define ATLAS 2048
#define ACELL 32 /* texels per block side */
#define AGRID (ATLAS / ACELL)
#define AMAP 4096
#define AMAXNEW 512 /* blocks decoded per vertex buffer */

typedef struct {
  uint32_t k0, k1, gen;
  uint16_t ax, ay; /* atlas texel of the entry's first block */
} AEntry;
static AEntry s_amap[AMAP];
static uint32_t s_agen = 1;
static GLuint s_atex[2], s_afbo[2];
static int s_acur;
static int s_ax, s_ay, s_ah; /* shelf allocator, in blocks */
static int s_afull;
/* decode quads per format, as packed client arrays (like quad()'s) */
static float s_aq_ndc[3][AMAXNEW * 12], s_aq_src[3][AMAXNEW * 12], s_aq_pc[3][AMAXNEW * 12];
static float s_aq_org[3][AMAXNEW * 24];
static int s_alin[2]; /* the atlas's GL filter is GL_LINEAR now */
static int s_aqn[3];
static struct {
  unsigned blocks, decoded, misses, used;
} s_ast;

static void atlas_reset(void) {
  int i;
  if (!s_atex[0]) {
    for (i = 0; i < 2; i++) {
      s_atex[i] = new_tex(ATLAS, ATLAS, NULL);
      glGenFramebuffers(1, &s_afbo[i]);
      glBindFramebuffer(GL_FRAMEBUFFER, s_afbo[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_atex[i], 0);
    }
    s_bound_fbo = (GLuint)-1;
  }
  s_acur ^= 1;
  s_ax = s_ay = s_ah = 0;
  s_ast.used = 0;
  s_afull = 0;
  s_aflush = 0;
  if (++s_agen == 0) {
    memset(s_amap, 0, sizeof s_amap);
    s_agen = 1;
  }
}

/* The atlas texel of page texel (bx0 * 32, by0 * 32) of texture (fmt,
 * page, clut), with blocks up to (bx1, by1) decoded around it (or, wrap,
 * the one tile with its margin), or -1. */
static int atlas_find(int fmt, int page, int clut, int bx0, int by0, int bx1, int by1, int wrap,
                      int *ax, int *ay) {
  uint32_t k0 = (uint32_t)fmt | (uint32_t)(page & 31) << 2 | (uint32_t)(uint16_t)clut << 7;
  uint32_t k1 = (uint32_t)(bx0 | by0 << 3 | bx1 << 6 | by1 << 9 | wrap << 12);
  uint32_t h = ((k0 * 0x9E3779B1u) ^ (k1 * 0x85EBCA6Bu)) >> 20, n;
  int tw = bx1 - bx0 + 1, th = by1 - by0 + 1; /* blocks of texels */
  int bw = wrap ? 2 : tw, bh = wrap ? 2 : th; /* blocks of the atlas */
  int mg = wrap ? ACELL / 2 : 0, px, py, vx, vw, i;
  AEntry *e = NULL;
  for (n = 0; n < 16; n++) {
    AEntry *f = &s_amap[(h + n) & (AMAP - 1)];
    if (f->gen != s_agen) {
      e = f;
      break;
    }
    if (f->k0 == k0 && f->k1 == k1) {
      *ax = f->ax + mg;
      *ay = f->ay + mg;
      return 0;
    }
  }
  if (!e || s_afull || s_aqn[fmt] == AMAXNEW)
    return -1;
  /* not over a target: what the game draws there changes every frame */
  px = (page & 15) * 64;
  py = (page >> 4 & 1) * 256;
  vx = px + (bx0 * ACELL >> (2 - fmt));
  vw = tw * ACELL >> (2 - fmt);
  for (i = 0; i < s_ntargets; i++) {
    const Target *t = &s_targets[i];
    if (overlap(vx, py + by0 * ACELL, vw, th * ACELL, t->x, t->y, t->w, t->h) ||
        (fmt < 2 && overlap((clut & 63) * 16, clut >> 6 & 511, fmt ? 256 : 16, 1, t->x, t->y,
                            t->w, t->h)))
      return -1;
  }
  if (s_ax + bw > AGRID) {
    s_ax = 0;
    s_ay += s_ah;
    s_ah = 0;
  }
  if (s_ay + bh > AGRID) {
    s_afull = 1;
    return -1;
  }
  e->gen = s_agen;
  e->k0 = k0;
  e->k1 = k1;
  e->ax = (uint16_t)(s_ax * ACELL);
  e->ay = (uint16_t)(s_ay * ACELL);
  {
    /* the decode quad: atlas rect <- texels from the block origin (centres
     * at +0.5), the margin wrapping within the tile */
    static const int ix[6] = {0, 2, 0, 2, 2, 0}, iy[6] = {1, 1, 3, 1, 3, 3};
    float r[4], t[4], period = wrap ? (float)ACELL : 256.0f;
    float *qn = s_aq_ndc[fmt] + s_aqn[fmt] * 12, *qs = s_aq_src[fmt] + s_aqn[fmt] * 12;
    float *qp = s_aq_pc[fmt] + s_aqn[fmt] * 12, *qo = s_aq_org[fmt] + s_aqn[fmt] * 24;
    r[0] = (float)e->ax / ATLAS * 2 - 1;
    r[2] = (float)(e->ax + bw * ACELL) / ATLAS * 2 - 1;
    r[1] = (float)e->ay / ATLAS * 2 - 1;
    r[3] = (float)(e->ay + bh * ACELL) / ATLAS * 2 - 1;
    t[0] = (float)-mg;
    t[2] = (float)(bw * ACELL - mg);
    t[1] = (float)-mg;
    t[3] = (float)(bh * ACELL - mg);
    for (i = 0; i < 6; i++) {
      qn[i * 2] = r[ix[i]];
      qn[i * 2 + 1] = r[iy[i]];
      qs[i * 2] = t[ix[i]];
      qs[i * 2 + 1] = t[iy[i]];
      qp[i * 2] = (float)(uint16_t)page;
      qp[i * 2 + 1] = (float)(uint16_t)clut;
      qo[i * 4] = (float)(bx0 * ACELL);
      qo[i * 4 + 1] = (float)(by0 * ACELL);
      qo[i * 4 + 2] = period;
      qo[i * 4 + 3] = 1.0f / period;
    }
    s_aqn[fmt]++;
  }
  s_ax += bw;
  if (bh > s_ah)
    s_ah = bh;
  s_ast.decoded += (unsigned)(bw * bh);
  s_ast.used += (unsigned)(bw * bh);
  *ax = e->ax + mg;
  *ay = e->ay + mg;
  return 0;
}

/* The blocks found new since the last call, decoded into the atlas. */
static void atlas_decode(void) {
  int fmt;
  if (!s_aqn[0] && !s_aqn[1] && !s_aqn[2])
    return;
  upload_vram();
  bind_fbo(s_afbo[s_acur]);
  glViewport(0, 0, ATLAS, ATLAS);
  no_blend();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glEnableVertexAttribArray(3);
  glEnableVertexAttribArray(4);
  glDisableVertexAttribArray(5);
  glDisableVertexAttribArray(6);
  for (fmt = 0; fmt < 3; fmt++) {
    if (!s_aqn[fmt])
      continue;
    glUseProgram(s_dprog[fmt].prog);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, s_aq_ndc[fmt]);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, s_aq_src[fmt]);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 0, s_aq_pc[fmt]);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 0, s_aq_org[fmt]);
    glDrawArrays(GL_TRIANGLES, 0, s_aqn[fmt] * 6);
    s_aqn[fmt] = 0;
  }
  glDisableVertexAttribArray(3);
  glDisableVertexAttribArray(4);
  s_xattr = 0;
}

/* ---- target logs ---- */

static void log_begin(Target *t, int from_vram, uint16_t color) {
  TargetLog *l = &t->log;
  int i, j;
  if (!l->base)
    l->base = malloc((size_t)t->w * t->h * 2);
  for (i = 0; i < t->h; i++)
    for (j = 0; j < t->w; j++)
      l->base[i * t->w + j] = from_vram ? s_vram[(t->y + i) * VW + t->x + j] : color;
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
  int i, j, k;
  if (!l->ok || !g_vita_opts.gpu_readback)
    return -1;
  memcpy(sv, s_vram, sizeof s_vram);
  for (i = 0; i < t->h; i++)
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
        for (j = 0; j < e->w; j++)
          sv[(e->y + i) * VW + e->x + j] = e->color;
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
    s_down_tex = new_tex(t->w, t->h, NULL);
    if (!s_down_fbo)
      glGenFramebuffers(1, &s_down_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_down_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_down_tex, 0);
    s_bound_fbo = s_down_fbo;
    s_down_w = t->w;
    s_down_h = t->h;
    free(buf);
    buf = malloc((size_t)t->w * t->h * 4);
  }
  bind_fbo(s_down_fbo);
  glViewport(0, 0, t->w, t->h);
  no_blend();
  glUseProgram(s_prog[P_TEX].prog);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, t->tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture(GL_TEXTURE0);
  quad(ndc, src);
  glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
  for (i = 0; i < t->h; i++) {
    const uint8_t *s = buf + (size_t)i * t->w * 4;
    uint16_t *d = s_vram + ((t->y + i) & (VH - 1)) * VW;
    for (j = 0; j < t->w; j++, s += 4)
      d[(t->x + j) & (VW - 1)] = (uint16_t)(s[0] >> 3 | (s[1] >> 3) << 5 | (s[2] >> 3) << 10);
  }
done:
  t->cpu_stale = 0;
  s_vram_dirty = s_aflush = 1;
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
      s_vram_dirty = 1; /* inside a target: never in the atlas */
  }
}

/* Pending copies hold Target pointers: they go out before any target is
 * dropped or moved (the last one moves into slot i). Left behind, they would
 * draw into a deleted FBO, which on the GPU can be memory reused since. */
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

static void set_scale(void) {
  /* the screen's pixels: the display rect fills a 4:3 area 544 lines high,
   * or the whole width with widescreen (presentation frames, D30) */
  int dw = currentDispEnv.disp.w > 0 ? currentDispEnv.disp.w : 512;
  int dh = currentDispEnv.disp.h > 0 ? currentDispEnv.disp.h : 240;
  if (g_vita_opts.gpu_res > 0) { /* res=ps1 (1) or res=N: N x the PS1's */
    s_sx = s_sy = (float)g_vita_opts.gpu_res;
  } else { /* res=native */
    s_sx = (float)(g_present.widescreen ? SCREEN_W : AREA43_W) / dw;
    s_sy = (float)SCREEN_H / dh;
  }
  vita_log("gpu: targets at %.3f x %.3f the PS1's pixels (display %dx%d)", s_sx, s_sy, dw, dh);
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
  s_aflush = 1; /* atlas blocks decoded from what is now a target's rect */
  t->x = x;
  t->y = y;
  t->w = x1 - x;
  t->h = y1 - y;
  t->pw = (int)(t->w * s_sx + 0.999f);
  t->ph = (int)(t->h * s_sy + 0.999f);
  t->tex = new_tex(t->pw, t->ph, NULL);
  glGenFramebuffers(1, &t->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
  s_bound_fbo = t->fbo;
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    vita_log("gpu: target FBO %dx%d incomplete", t->pw, t->ph);
  t->used = ++s_use;
  vita_log("gpu: target %d: VRAM %d,%d %dx%d -> %dx%d", s_ntargets - 1, t->x, t->y, t->w,
           t->h, t->pw, t->ph);
  upload_vram();
  copy_into(t, t->x, t->y, t->w, t->h); /* starts as what VRAM holds */
  log_begin(t, 1, 0);
  return t;
}

/* ---- GR_* (called through src/platform/vita/gpu.c) ---- */

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

/* s_vbuf -> s_xbuf (D39). A polygon's vertices carry the vertex cache's
 * answer (psx_vcache.h): its exact position (subpixel=1), and its depth as w
 * when every vertex of the polygon was found (persp=1). Polygons are
 * filtered (tex_filter=), clamped to each triangle's UV box, its lowest to
 * its highest u and v: Spyro's polygons give their texture's last texel as
 * the far corner (a 32-texel texture is u0..u0+31; logs/run14-host-tridump),
 * and a triangle has two opposite corners of its quad. 88% of the world's
 * textured triangles map one whole aligned 32x32 tile, repeated from polygon
 * to polygon, and subdivided near terrain maps its quarters: those wrap
 * within the tile, so the filter blends across polygon edges as the texture
 * was drawn to. u, v and box stay in page coordinates; off is where the
 * triangle's blocks are in the decoded atlas (D41). */
static void expand(int count) {
  uint64_t t0 = now_us();
  int i, k;
  if ((s_filter || s_cache) && (s_aflush || s_afull))
    atlas_reset();
  for (i = 0; i < count; i += 3) {
    const GrVertex *v = s_vbuf + i;
    int n = count - i < 3 ? count - i : 3;
    int u0 = 255, v0 = 255, u1 = 0, v1 = 0, wrap = 0, filt = 0, kind = 0, du = 0, dv = 0;
    uint32_t ref0;
    for (k = 0; k < n; k++) {
      u0 = v[k].u < u0 ? v[k].u : u0;
      v0 = v[k].v < v0 ? v[k].v : v0;
      u1 = v[k].u > u1 ? v[k].u : u1;
      v1 = v[k].v > v1 ? v[k].v : v1;
    }
    /* inside one aligned 32x32 tile and at least half of it: a repeating
     * ground tile, or a quarter of one (subdivided near terrain), so the
     * filter wraps within the tile instead of stopping at the edge */
    if ((u0 >> 5) == (u1 >> 5) && (v0 >> 5) == (v1 >> 5) && u1 - u0 >= 15 && v1 - v0 >= 15) {
      wrap = 1;
      u0 &= ~31;
      v0 &= ~31;
      u1 = u0 + 31;
      v1 = v0 + 31;
    }
    /* a textured prim: from its blocks in the atlas, filtered if it is a
     * polygon and tex_filter is on, else the nearest texel (tex_cache, and
     * sprites when filtering) */
    memcpy(&ref0, &v[0].tcx, sizeof ref0);
    wrap = wrap && s_filter; /* the margin is only for the filter */
    if ((s_filter || s_cache) && v[0].bright == 2 && (v[0].page >> 7 & 3) < 3) {
      int ax, ay;
      s_ast.blocks++;
      if (!atlas_find(v[0].page >> 7 & 3, v[0].page, v[0].clut, u0 >> 5, v0 >> 5, u1 >> 5,
                      v1 >> 5, wrap, &ax, &ay)) {
        filt = s_filter && (ref0 & VREF_POLY);
        kind = filt ? 2 : 1;
        du = ax - (u0 & ~31);
        dv = ay - (v0 & ~31);
      } else {
        s_ast.misses++;
      }
    }
    s_tkind[i / 3] = (uint8_t)kind;
    {
      /* the triangle's tail (box, off, no sub-pixel offset), then each
       * vertex: PsyX's vertex whole, its vcache bytes replaced */
      const int16_t tail[6] = {(int16_t)u0, (int16_t)v0, (int16_t)u1, (int16_t)v1,
                               (int16_t)du, (int16_t)dv};
      for (k = 0; k < n; k++) {
        XVertex *x = s_xbuf + i + k;
        x->g = v[k];
        x->g.tcx = (char)filt;
        x->g.tcy = (char)(wrap && filt);
        memcpy(x->box, tail, sizeof tail);
        x->dxy[0] = x->dxy[1] = 0;
        x->w = 1;
      }
    }
    if (s_vc) { /* persp, subpixel: the vcache's answer, in vertex order */
      for (k = 0; k < n; k++) {
        XVertex *x = s_xbuf + i + k;
        const VCacheVert *c = &g_vcache_side[i + k];
        uint32_t ref;
        memcpy(&ref, &v[k].tcx, sizeof ref);
        if (!(ref & VREF_POLY))
          continue;
        if (s_subpixel) {
          x->dxy[0] = c->dx;
          x->dxy[1] = c->dy;
        }
        if (s_persp)
          x->w = c->w;
      }
    }
  }
  if (s_filter || s_cache)
    atlas_decode();
  s_st.expand_us += now_us() - t0;
}

/* The vertices into the VBO (vitaGL gives a fresh buffer per glBufferData).
 * Triangles with an edge >= 1024 x or >= 512 y are collapsed: the PS1 GPU
 * skips them (gpu_soft.c's rule). */
void gl_GR_UpdateVertexBuffer(const GrVertex *vertices, int count) {
  int i, k;
  if (count > MAX_VERTEX_BUFFER_SIZE)
    count = MAX_VERTEX_BUFFER_SIZE;
  memcpy(s_vbuf, vertices, (size_t)count * sizeof *vertices);
  s_vorig = vertices;
  if (s_vc)
    psx_vcache_set_base(vertices);
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
  if (s_xmode) {
    expand(count); /* may draw into the atlas, with other buffers bound */
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * sizeof *s_xbuf, s_xbuf, GL_STREAM_DRAW);
    return;
  }
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * sizeof *vertices, s_vbuf, GL_STREAM_DRAW);
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

static float s_xf[4]; /* this split's u_xf */

static void draw(int start, int n, float scale, float alpha, float sel) {
  const Prog *p = &s_prog[s_texmode];
  int t, end;
  if (!(s_filter || s_cache) || s_texmode == P_UNTEX) {
    glUniform3f(p->semi, scale, alpha, sel);
    glDrawArrays(GL_TRIANGLES, start, n);
    return;
  }
  /* runs of each kind of triangle (s_tkind), each with its own program, in
   * draw order */
  for (t = start / 3, end = (start + n) / 3; t < end;) {
    int f = s_tkind[t], e = t + 1;
    while (e < end && s_tkind[e] == f)
      e++;
    int hw = f == 2 && s_filter == 1 && s_blend == BM_NONE;
    p = hw ? &s_vprog[V_HB][s_texmode] : f == 2 ? &s_prog[s_texmode]
                                                 : &s_vprog[f ? V_A0 : V_X0][s_texmode];
    if (f && s_alin[s_acur] != hw) { /* the GPU's bilinear, or exact texels */
      glActiveTexture(GL_TEXTURE1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, hw ? GL_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, hw ? GL_LINEAR : GL_NEAREST);
      glActiveTexture(GL_TEXTURE0);
      s_alin[s_acur] = hw;
    }
    glUseProgram(p->prog);
    glUniform4f(p->xf, s_xf[0], s_xf[1], s_xf[2], s_xf[3]);
    glUniform3f(p->semi, scale, alpha, sel);
    glDrawArrays(GL_TRIANGLES, t * 3, (e - t) * 3);
    t = e;
  }
}

void gl_GR_DrawTriangles(int start_vertex, int triangles) {
  const Prog *p;
  Target *t = s_cur;
  int n = triangles * 3, x0, y0, x1, y1;
  if (!t || triangles <= 0 || start_vertex < 0 || start_vertex + n > s_nverts)
    return;
  if (s_texmode < 0) {
    static int warned;
    if (!warned++)
      vita_log("gpu: PsyX custom textures (DR_PSYX_TEX) are not drawn");
    return;
  }
  if (g_vita_opts.gpu_dump && (unsigned)(vita_vblank_count() - g_vita_opts.gpu_dump) < 2) {
    const GrVertex *v = s_vorig + start_vertex;
    vita_log("gpu_dump: target %d mode %d blend %d tris %d org %d,%d clip %d,%d-%d,%d | "
             "xy %d,%d %d,%d %d,%d page %04x clut %04x uv %d,%d rgb %d,%d,%d",
             (int)(t - s_targets), s_texmode, (int)s_blend, triangles, s_org_x, s_org_y,
             s_clip_x0, s_clip_y0, s_clip_x1, s_clip_y1, v[0].x, v[0].y, v[1].x, v[1].y, v[2].x,
             v[2].y, (uint16_t)v[0].page, (uint16_t)v[0].clut, v[0].u, v[0].v, v[0].r, v[0].g,
             v[0].b);
  }
  sample_begin();
  if (s_texmode != P_UNTEX && any_stale())
    sync_pages(start_vertex, n);
  flush_pending();
  if (s_texmode != P_UNTEX)
    upload_vram();

  bind_fbo(t->fbo);
  glViewport(0, 0, t->pw, t->ph);
  x0 = (int)((s_clip_x0 - t->x) * s_sx + 0.5f);
  y0 = (int)((s_clip_y0 - t->y) * s_sy + 0.5f);
  x1 = (int)((s_clip_x1 - t->x) * s_sx + 0.5f);
  y1 = (int)((s_clip_y1 - t->y) * s_sy + 0.5f);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x0, y0, x1 - x0, y1 - y0);

  p = &s_prog[s_texmode];
  glUseProgram(p->prog);
  /* VRAM x = vertex x + org; + half a target pixel: the PS1 samples at the
   * pixel's corner, GL at its centre. (Half a PS1 pixel would shift what is
   * drawn against what LoadImage put there, and leave the target's first
   * rows and columns uncovered, once targets have more pixels than VRAM.) */
  s_xf[0] = (float)(s_org_x - t->x) + 0.5f / s_sx;
  s_xf[1] = (float)(s_org_y - t->y) + 0.5f / s_sy;
  s_xf[2] = 2.0f / t->w;
  s_xf[3] = 2.0f / t->h;
  glUniform4f(p->xf, s_xf[0], s_xf[1], s_xf[2], s_xf[3]);
  if (s_texmode != P_UNTEX) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_vram_tex);
  }
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  if (s_xmode) {
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glEnableVertexAttribArray(6);
    s_xattr = 1;
    glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(XVertex), (void *)offsetof(XVertex, g.x));
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(XVertex), (void *)offsetof(XVertex, g.u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(XVertex), (void *)offsetof(XVertex, g.r));
    glVertexAttribPointer(3, 4, GL_SHORT, GL_FALSE, sizeof(XVertex), (void *)offsetof(XVertex, box));
    glVertexAttribPointer(4, 4, GL_SHORT, GL_FALSE, sizeof(XVertex), (void *)offsetof(XVertex, off));
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(XVertex), (void *)offsetof(XVertex, w));
    glVertexAttribPointer(6, 2, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(XVertex), (void *)offsetof(XVertex, g.tcx));
    if ((s_filter || s_cache) && s_texmode != P_UNTEX) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, s_atex[s_acur]);
      glActiveTexture(GL_TEXTURE0);
    }
  } else {
    if (s_xattr) { /* bench= switched the options off */
      glDisableVertexAttribArray(3);
      glDisableVertexAttribArray(4);
      glDisableVertexAttribArray(5);
      glDisableVertexAttribArray(6);
      s_xattr = 0;
    }
    glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(GrVertex), (void *)offsetof(GrVertex, x));
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GrVertex), (void *)offsetof(GrVertex, u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GrVertex), (void *)offsetof(GrVertex, r));
  }

  switch (s_blend) {
  case BM_NONE:
    glDisable(GL_BLEND);
    draw(start_vertex, n, 1, 1, 0);
    break;
  case BM_SUBTRACT:
    if (s_texmode != P_UNTEX) { /* STP=0 texels opaque, then STP=1 subtract */
      glDisable(GL_BLEND);
      draw(start_vertex, n, 1, 1, 1);
    }
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    glBlendFunc(GL_ONE, GL_ONE);
    draw(start_vertex, n, 1, 1, s_texmode != P_UNTEX ? 2 : 0);
    glBlendEquation(GL_FUNC_ADD);
    break;
  default: /* B/2 + F/2, B + F, B + F/4: premultiplied, ONE, 1 - alpha */
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (s_blend == BM_AVERAGE)
      draw(start_vertex, n, 0.5f, 0.5f, 0);
    else if (s_blend == BM_ADD)
      draw(start_vertex, n, 1, 0, 0);
    else
      draw(start_vertex, n, 0.25f, 0, 0);
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
  s_gen++;
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
  s_vram_dirty = s_aflush = 1;
  s_gen++;
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

static uint16_t s_clear15; /* gl_GR_ClearVRAM's colour, for clear_targets */

/* A clear inside a target (the game's per-frame background clear) needn't
 * reach the GPU's VRAM texture: the target is cleared itself, and whatever
 * later samples or copies from there uploads first (sync_rect). Only clears
 * outside every target make the texture out of date. */
static void clear_dirty(int x, int y, int w, int h) {
  int k;
  for (k = 0; k < s_ntargets; k++) {
    Target *t = &s_targets[k];
    if (x >= t->x && y >= t->y && x + w <= t->x + t->w && y + h <= t->y + t->h) {
      t->tex_stale = 1;
      return;
    }
  }
  s_vram_dirty = s_aflush = 1;
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
    sx0 = (int)((x0 - t->x) * s_sx + 0.5f);
    sy0 = (int)((y0 - t->y) * s_sy + 0.5f);
    sx1 = x1 == t->x + t->w ? t->pw : (int)((x1 - t->x) * s_sx + 0.5f);
    sy1 = y1 == t->y + t->h ? t->ph : (int)((y1 - t->y) * s_sy + 0.5f);
    if (x0 == t->x && y0 == t->y && x1 == t->x + t->w && y1 == t->y + t->h) {
      log_begin(t, 0, s_clear15); /* cleared whole: the log starts again */
    } else {
      LogEntry *e = log_add(t, LOG_CLEAR, 0);
      if (e) {
        e->x = x0, e->y = y0, e->w = x1 - x0, e->h = y1 - y0;
        e->color = s_clear15;
      }
    }
    bind_fbo(t->fbo);
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

void gl_GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g,
                     unsigned char b) {
  uint16_t c = (uint16_t)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
  int i, j;
  if (w <= 0 || h <= 0)
    return;
  for (i = 0; i < h; i++) {
    uint16_t *row = s_vram + ((y + i) & (VH - 1)) * VW;
    for (j = 0; j < w; j++)
      row[(x + j) & (VW - 1)] = c;
  }
  pieces(x, y, w, h, clear_dirty);
  s_gen++;
  flush_pending();
  s_clear15 = c;
  glClearColor((r >> 3) * 8 / 255.0f, (g >> 3) * 8 / 255.0f, (b >> 3) * 8 / 255.0f, 1.0f);
  pieces(x, y, w, h, clear_targets);
  glDisable(GL_SCISSOR_TEST);
}

/* PsyX's GL backend clears the window here; GR_ClearVRAM did the targets. */
void gl_GR_Clear(int x, int y, int w, int h, unsigned char r, unsigned char g,
                 unsigned char b) {
  (void)x, (void)y, (void)w, (void)h, (void)r, (void)g, (void)b;
}

/* LoadImage (src = host data, stride w) or MoveImage (src = NULL, VRAM). */
void gl_GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x,
                    int dst_y) {
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

/* ---- set-up, present, shots ---- */

/* vitaGL's init_gxm (gxm.o) treats a successful sceAppMgrGetBudgetInfo as
 * "this is a system app" and then sets GXM up for one (sceGxmVshInitialize,
 * the budget's memory). A game on a Vita gets an error from it; Vita3K's stub
 * returns success, and then nothing vitaGL draws reaches the screen. This
 * wrapper (-Wl,--wrap=sceAppMgrGetBudgetInfo) gives the game's answer on
 * both. */
int __wrap_sceAppMgrGetBudgetInfo(void *info) {
  (void)info;
  return (int)0x80800001; /* any error: "not a system app" */
}

int vita_gpu_init(void) {
  SceKernelFreeMemorySizeInfo mem = {.size = sizeof mem};
  int present_only = 0;
  sceKernelGetFreeMemorySize(&mem);
  vita_log("gpu: free memory: user %u KB, CDRAM %u KB, phycont %u KB",
           (unsigned)(mem.size_user / 1024), (unsigned)(mem.size_cdram / 1024),
           (unsigned)(mem.size_phycont / 1024));
  vglSetupRuntimeShaderCompiler(SHARK_OPT_DEFAULT, 0, 0, 0); /* exact float maths */
  /* vitaGL's pools: all free RAM but 32 MB (newlib's heap is already
   * reserved), CDRAM and phycont. The installed vitaGL's return value is not
   * success: it is 1 when the resolution had to be clamped to the display's
   * maximum, 0 otherwise (vgl.o, vglInitWithCustomSizes.part.0), and it
   * initialises either way. */
  if (vglInitExtended(0, SCREEN_W, SCREEN_H, 32 << 20, SCE_GXM_MULTISAMPLE_NONE))
    vita_log("gpu: vitaGL clamped the resolution to the display's");
  vglWaitVblankStart(GL_TRUE);
  vita_log("gpu: vitaGL %s, %s; pools KB: RAM %u, VRAM %u, phycont %u",
           (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER),
           (unsigned)(vglMemTotal(VGL_MEM_RAM) >> 10), (unsigned)(vglMemTotal(VGL_MEM_VRAM) >> 10),
           (unsigned)(vglMemTotal(VGL_MEM_PHYCONT) >> 10));
  shark_install_log_cb(shark_log);
  s_filter = g_vita_opts.tex_filter < 0 ? 0 : g_vita_opts.tex_filter > 2 ? 2 : g_vita_opts.tex_filter;
  s_persp = g_vita_opts.persp != 0;
  s_subpixel = g_vita_opts.subpixel != 0;
  s_cache = g_vita_opts.tex_cache != 0;
  s_vc = s_persp || s_subpixel;
  s_xmode = s_filter || s_vc || s_cache;
  if (build_programs(&present_only) != 0)
    return -1;
  if ((s_xmode || g_vita_opts.bench) && present_only) {
    vita_log("gpu: the shaders for tex_filter/persp/subpixel don't compile: drawing without them");
    s_xmode = s_filter = s_persp = s_subpixel = s_cache = s_vc = 0;
    g_vita_opts.bench = 0;
    if (build_programs(&present_only) != 0)
      return -1;
  }
  if (s_xmode) {
    /* projections recorded only for persp/subpixel; polygons marked for the
     * filter; nothing for tex_cache alone (D43) */
    psx_vcache_enable(s_vc ? 2 : s_filter ? 1 : 0);
    vita_log("gpu: tex_filter=%d persp=%d subpixel=%d tex_cache=%d (D39, D41)", s_filter, s_persp,
             s_subpixel, s_cache);
  }
  s_vram_tex = new_tex(VW / 2, VH, NULL);
  s_rgb_tex = new_tex(4, 4, NULL);
  glGenBuffers(1, &s_vbo);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_STENCIL_TEST);
  bind_fbo(0);
  glViewport(0, 0, SCREEN_W, SCREEN_H);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  vglSwapBuffers(GL_FALSE);
  if (present_only) {
    vita_log("gpu: the GPU renderer's shaders don't compile: using the software renderer, "
             "shown through vitaGL (as gpu=0)");
    return 1;
  }
  vita_log("gpu: GPU renderer ready (gpu=1)");
  return 0;
}

/* targets over the display rect -> s_vram (screenshots, 24-bit display) */
void vita_gpu_sync_display(void) {
  const DISPENV *d = &currentDispEnv;
  int w = d->disp.w > 0 ? d->disp.w : 320, h = d->disp.h > 0 ? d->disp.h : 240;
  if (d->isrgb24)
    w = w * 3 / 2;
  pieces(d->disp.x, d->disp.y, w, h, sync_rect);
}

static struct {
  unsigned gen, vgen;
  DISPENV disp;
  int disabled, valid;
} s_last;
static int s_hd_shot; /* vblank of a pending 960x544 shot, 0 = none */

void vita_gpu_shot_hd(int vblank) { s_hd_shot = vblank; }

static void write_hd_shot(void) {
  char path[256];
  uint8_t *rgba = malloc(SCREEN_W * SCREEN_H * 4), *rgb = malloc(SCREEN_W * SCREEN_H * 3);
  int x, y;
  glReadPixels(0, 0, SCREEN_W, SCREEN_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  for (y = 0; y < SCREEN_H; y++) /* GL's row 0 is the bottom */
    for (x = 0; x < SCREEN_W; x++)
      memcpy(rgb + ((size_t)y * SCREEN_W + x) * 3,
             rgba + ((size_t)(SCREEN_H - 1 - y) * SCREEN_W + x) * 4, 3);
  snprintf(path, sizeof path, VITA_DATA_DIR "/shots/shot-%d-hd.png", s_hd_shot);
  if (host_png_write(path, rgb, SCREEN_W, SCREEN_H) == 0)
    vita_log("shot %s", path);
  free(rgba);
  free(rgb);
  s_hd_shot = 0;
}

/* Where the part of target t inside VRAM rect (x, y, w, h) goes in a
 * picture of pw x ph pixels that shows that rect: pos = pixels (x0, y0, x1,
 * y1, top row first), uv = the target's texture rect. When the picture has
 * the target's own scale (res=native, presentation frames) the part starts on
 * a whole pixel and gets one pixel per target pixel, so nothing is resampled
 * (Spyro's buffers start 8 lines into the 240-line display: 18.13 of the
 * screen's 544). 0: t isn't inside the rect. */
static int place(const Target *t, int x, int y, int w, int h, int pw, int ph, float pos[4],
                 float uv[4]) {
  int x0 = x > t->x ? x : t->x, y0 = y > t->y ? y : t->y;
  int x1 = x + w < t->x + t->w ? x + w : t->x + t->w;
  int y1 = y + h < t->y + t->h ? y + h : t->y + t->h;
  float kx = (float)pw / w, ky = (float)ph / h;
  float sx = (float)t->pw / t->w, sy = (float)t->ph / t->h; /* as drawn */
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

/* ---- presentation frames (src/game/present.c: widescreen, 60 fps; D30) ---- */

/* PRESENT_SLOTS textures for present.c's queue, plus a staging one: a
 * presentation pass's picture is copied from the targets on the GPU (no
 * readback, no replay) and filed by swapping handles. */
#define STAGING PRESENT_SLOTS
static GLuint s_ptex[PRESENT_SLOTS + 1], s_pfbo[PRESENT_SLOTS + 1];
static int s_pw, s_ph;
static int s_pres_on_screen = -1; /* slot the screen shows, -1: the display rect */

static void pres_alloc(int w, int h) {
  int i;
  if (s_pw == w && s_ph == h)
    return;
  for (i = 0; i <= PRESENT_SLOTS; i++) {
    if (s_ptex[i]) {
      glDeleteFramebuffers(1, &s_pfbo[i]);
      glDeleteTextures(1, &s_ptex[i]);
    }
    s_ptex[i] = new_tex(w, h, NULL);
    glGenFramebuffers(1, &s_pfbo[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, s_pfbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_ptex[i], 0);
    s_bound_fbo = s_pfbo[i];
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      vita_log("gpu: presentation FBO %dx%d incomplete", w, h);
  }
  s_pw = w;
  s_ph = h;
  s_pres_on_screen = -1;
  vita_log("gpu: presentation frames %dx%d", w, h);
}

/* The VRAM rect a presentation pass just drew -> staging: every target over
 * it, at the targets' resolution; black elsewhere (Spyro's border lines).
 * Inside an FBO the image's top is NDC -1, as in copy_into(). */
static void pres_capture(int x, int y, int w, int h) {
  float ndc[4], src[4];
  int i;
  if (!s_sx)
    set_scale();
  flush_pending();
  pres_alloc((int)(w * s_sx + 0.999f), (int)(h * s_sy + 0.999f));
  bind_fbo(s_pfbo[STAGING]);
  glViewport(0, 0, s_pw, s_ph);
  no_blend();
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(s_prog[P_TEX].prog);
  for (i = 0; i < s_ntargets; i++) {
    const Target *t = &s_targets[i];
    float pos[4];
    if (!place(t, x, y, w, h, s_pw, s_ph, pos, src))
      continue;
    ndc[0] = pos[0] / s_pw * 2 - 1;
    ndc[2] = pos[2] / s_pw * 2 - 1;
    ndc[1] = pos[1] / s_ph * 2 - 1;
    ndc[3] = pos[3] / s_ph * 2 - 1;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0);
    quad(ndc, src);
  }
}

static void pres_commit(int slot) {
  GLuint t = s_ptex[slot], f = s_pfbo[slot];
  s_ptex[slot] = s_ptex[STAGING];
  s_pfbo[slot] = s_pfbo[STAGING];
  s_ptex[STAGING] = t;
  s_pfbo[STAGING] = f;
  if (slot == s_pres_on_screen)
    s_pres_on_screen = -1; /* new content: draw it again */
}

static const PresentCapture s_pres_capture = {pres_capture, pres_commit};

const struct PresentCapture *vita_gpu_present_capture(void) { return &s_pres_capture; }

/* The options panel (menu.c), drawn 1:1 in the middle over the picture. */
static const uint8_t *s_ovl;
static int s_ovl_w, s_ovl_h, s_ovl_dirty;
static GLuint s_ovl_tex;

void vita_gpu_overlay(const uint8_t *rgb, int w, int h) {
  s_ovl = rgb;
  s_ovl_w = w;
  s_ovl_h = h;
  s_ovl_dirty = 1;
}

void vita_gpu_force_redraw(void) {
  s_last.valid = 0;
  s_pres_on_screen = -1;
}

/* Widescreen switched (menu.c): every target drawn again at the new width.
 * Dropping a target first gives VRAM its pixels (drop_target). */
void vita_gpu_rescale(void) {
  while (s_ntargets > 0)
    drop_target(0);
  s_cur = NULL; /* the next split finds its target again */
  s_sx = s_sy = 0;
  s_last.valid = 0;
}

/* Save states (D51): VRAM as the PS1 has it. Every target dropped first:
 * drop_target() replays its log into s_vram (no GPU readback). Load: the
 * caller overwrites s_vram, then it's newer than the GPU's copy and the
 * decoded atlas starts over; targets are made again as the game draws. */
uint16_t *vita_gpu_vram_take(void) {
  vita_gpu_rescale();
  return s_vram;
}

void vita_gpu_vram_loaded(void) {
  s_vram_dirty = 1;
  s_aflush = 1;
}

static void draw_overlay(void) {
  float ndc[4], src[4] = {0, 0, 1, 1};
  int x0 = (SCREEN_W - s_ovl_w) / 2, y0 = (SCREEN_H - s_ovl_h) / 2;
  if (!s_ovl)
    return;
  if (!s_ovl_tex)
    s_ovl_tex = new_tex(4, 4, NULL);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, s_ovl_tex);
  if (s_ovl_dirty) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, s_ovl_w, s_ovl_h, 0, GL_RGB, GL_UNSIGNED_BYTE, s_ovl);
    s_ovl_dirty = 0;
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture(GL_TEXTURE0);
  bind_fbo(0);
  glViewport(0, 0, SCREEN_W, SCREEN_H);
  no_blend();
  glUseProgram(s_prog[P_TEX].prog);
  ndc[0] = (float)x0 / SCREEN_W * 2 - 1;
  ndc[2] = (float)(x0 + s_ovl_w) / SCREEN_W * 2 - 1;
  ndc[1] = 1 - (float)y0 / SCREEN_H * 2;
  ndc[3] = 1 - (float)(y0 + s_ovl_h) / SCREEN_H * 2;
  quad(ndc, src);
}

/* Presentation frame `slot` on the screen: full width when widescreen, else
 * 4:3 in the middle. GPU slots (gpu=1) or present.c's pixels (the software
 * rasterizer shown through vitaGL). */
static int present_slot(int slot) {
  int wide = port_present_frame_is_wide(), area_w = wide ? SCREEN_W : AREA43_W;
  int area_x = (SCREEN_W - area_w) / 2;
  float ndc[4], src[4] = {0, 0, 1, 1};
  if (!port_present_frame_new() && s_pres_on_screen == slot && !s_hd_shot)
    return 0; /* the display keeps showing it */
  prof_enter(PROF_PRESENT);
  s_pres_on_screen = slot;
  s_last.valid = 0; /* the display rect must be drawn again when it's back */
  bind_fbo(0);
  glViewport(0, 0, SCREEN_W, SCREEN_H);
  no_blend();
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glActiveTexture(GL_TEXTURE1);
  if (g_present_capture) {
    glBindTexture(GL_TEXTURE_2D, s_ptex[slot]);
  } else {
    const uint16_t *px = port_present_slot_pixels(slot);
    static uint8_t rgb[PRESENT_W * PRESENT_H * 3];
    int i;
    for (i = 0; px && i < PRESENT_W * PRESENT_H; i++) {
      int r = px[i] & 31, g = (px[i] >> 5) & 31, b = (px[i] >> 10) & 31;
      rgb[i * 3] = (uint8_t)(r << 3 | r >> 2);
      rgb[i * 3 + 1] = (uint8_t)(g << 3 | g >> 2);
      rgb[i * 3 + 2] = (uint8_t)(b << 3 | b >> 2);
    }
    glBindTexture(GL_TEXTURE_2D, s_rgb_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, PRESENT_W, PRESENT_H, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_vita_opts.filter ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_vita_opts.filter ? GL_LINEAR : GL_NEAREST);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(s_prog[P_TEX].prog);
  ndc[0] = (float)area_x / SCREEN_W * 2 - 1;
  ndc[2] = (float)(area_x + area_w) / SCREEN_W * 2 - 1;
  ndc[1] = 1;
  ndc[3] = -1;
  quad(ndc, src);
  draw_overlay();
  if (s_hd_shot)
    write_hd_shot();
  s_st.presents++;
  prof_leave();
  prof_enter(PROF_WAIT);
  swap();
  prof_leave();
  return 1;
}

/* The display rect 4:3 in the middle of the screen. Returns 1 if it was a
 * new picture. */
static int present_display(void) {
  const DISPENV *d = &currentDispEnv;
  int dw = d->disp.w > 0 ? d->disp.w : 320, dh = d->disp.h > 0 ? d->disp.h : 240;
  int area_w = AREA43_W, area_x = (SCREEN_W - area_w) / 2, i;
  float ndc[4], src[4];
  prof_enter(PROF_PRESENT);
  if (s_last.valid && s_last.gen == s_gen && s_last.vgen == prof_vram_gen() &&
      s_last.disabled == g_GPUDisabledState && !memcmp(&s_last.disp, d, sizeof s_last.disp) &&
      !s_hd_shot) {
    prof_leave();
    return 0; /* the display keeps showing the last picture */
  }
  s_last.valid = 1;
  s_last.gen = s_gen;
  s_last.vgen = prof_vram_gen();
  s_last.disabled = g_GPUDisabledState;
  s_last.disp = *d;
  flush_pending();
  bind_fbo(0);
  glViewport(0, 0, SCREEN_W, SCREEN_H);
  no_blend();
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  ndc[0] = (float)area_x / SCREEN_W * 2 - 1;
  ndc[2] = (float)(area_x + area_w) / SCREEN_W * 2 - 1;
  ndc[1] = 1;
  ndc[3] = -1;
  if (d->disp.w > 0 && !g_GPUDisabledState) {
    /* the boot logos (24-bit), and everything when the software
     * rasterizer draws: converted on the CPU */
    if (d->isrgb24 || !g_vita_gpu) {
      int w, h;
      uint8_t *rgb;
      vita_gpu_sync_display();
      rgb = host_display_rgb(&w, &h);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, s_rgb_tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_vita_opts.filter ? GL_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_vita_opts.filter ? GL_LINEAR : GL_NEAREST);
      glActiveTexture(GL_TEXTURE0);
      free(rgb);
      glUseProgram(s_prog[P_TEX].prog);
      src[0] = src[1] = 0;
      src[2] = src[3] = 1;
      quad(ndc, src);
    } else {
      upload_vram();
      glUseProgram(s_prog[P_VRAM15].prog);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, s_vram_tex);
      src[0] = (float)d->disp.x;
      src[1] = (float)d->disp.y;
      src[2] = (float)(d->disp.x + dw);
      src[3] = (float)(d->disp.y + dh);
      quad(ndc, src);
      /* then each target over the display, at its place */
      glUseProgram(s_prog[P_TEX].prog);
      for (i = 0; i < s_ntargets; i++) {
        const Target *t = &s_targets[i];
        float pos[4];
        if (!place(t, d->disp.x, d->disp.y, dw, dh, area_w, SCREEN_H, pos, src))
          continue;
        ndc[0] = (area_x + pos[0]) / SCREEN_W * 2 - 1;
        ndc[2] = (area_x + pos[2]) / SCREEN_W * 2 - 1;
        ndc[1] = 1 - pos[1] / SCREEN_H * 2;
        ndc[3] = 1 - pos[3] / SCREEN_H * 2;
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, t->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_vita_opts.filter ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_vita_opts.filter ? GL_LINEAR : GL_NEAREST);
        glActiveTexture(GL_TEXTURE0);
        quad(ndc, src);
      }
    }
  }
  draw_overlay();
  if (s_hd_shot)
    write_hd_shot();
  s_st.presents++;
  prof_leave();
  prof_enter(PROF_WAIT);
  swap();
  prof_leave();
  return 1;
}

/* One frame: a presentation frame (widescreen, 60 fps), or the display
 * rect. Returns 1 if it was a new picture. */
int vita_gpu_present(void) {
  int slot = port_present_frame_slot();
  if (slot >= 0)
    return present_slot(slot);
  s_pres_on_screen = -1;
  return present_display();
}

/* The picture on screen drawn and swapped again, without taking a new
 * presentation frame (shot_hold: Vita3K can show an older swap buffer). */
void vita_gpu_repeat(void) {
  int slot = s_pres_on_screen;
  vita_gpu_force_redraw();
  if (slot >= 0)
    present_slot(slot);
  else
    present_display();
}

/* bench= (D41) and the options menu (picture.c, D48): other picture
 * options from the next frame on. res as gpu_res (0 native, 1 the PS1's,
 * 2-4 N times it); a change redraws every target. An option set whose
 * programs weren't built at start-up is compiled now, once (the Vita's
 * shader compiler takes a moment). Returns 0, or -1 if they don't compile:
 * the options stay as they were. */
int vita_gpu_set_options(int res, int filter, int persp, int subpixel, int cache) {
  static int failed[V_COUNT];
  int v = variant_of(filter, cache, filter || persp || subpixel || cache);
  if (!s_vprog[v][P_UNTEX].prog || failed[v]) {
    uint64_t t0 = now_us();
    if (failed[v] || build_variant(v)) {
      failed[v] = 1;
      vita_log("gpu: the programs for tex_filter=%d persp=%d subpixel=%d don't compile: "
               "options unchanged", filter, persp, subpixel);
      return -1;
    }
    vita_log("gpu: programs for tex_filter=%d persp=%d subpixel=%d tex_cache=%d compiled in %u ms",
             filter, persp, subpixel, cache,
             (unsigned)((now_us() - t0) / 1000));
  }
  s_filter = filter;
  s_persp = persp;
  s_subpixel = subpixel;
  s_cache = cache;
  s_vc = persp || subpixel;
  s_xmode = filter || persp || subpixel || cache;
  memcpy(s_prog, s_vprog[v], sizeof s_vprog[0]);
  psx_vcache_enable(s_vc ? 2 : filter ? 1 : 0);
  if (res != g_vita_opts.gpu_res) {
    g_vita_opts.gpu_res = res;
    vita_gpu_rescale();
  }
  return 0;
}

void vita_gpu_report(int vblank) {
  vita_log("gpu %d: %u draws, %u triangles, %u presents, %u VRAM uploads (%u.%02u ms), "
           "%u copies into targets (%u moves between them), %u readbacks (%u replayed; "
           "%u.%02u ms), %d targets at %dx%d",
           vblank, s_st.draws, s_st.tris, s_st.presents, s_st.uploads,
           (unsigned)(s_st.upload_us / 1000), (unsigned)(s_st.upload_us % 1000 / 10),
           s_st.copies, s_st.moves, s_st.readbacks, s_st.replays,
           (unsigned)(s_st.readback_us / 1000), (unsigned)(s_st.readback_us % 1000 / 10),
           s_ntargets, s_ntargets ? s_targets[0].pw : 0, s_ntargets ? s_targets[0].ph : 0);
  if (s_sample.n)
    vita_log("gpu %d: timed frame %u.%02u ms, GPU tail after the CPU %u.%02u ms", vblank,
             s_sample.frame_us / 1000, s_sample.frame_us % 1000 / 10, s_sample.tail_us / 1000,
             s_sample.tail_us % 1000 / 10);
  s_sample.n = 0;
  if (g_vita_opts.gpu_time)
    s_sample.state = 1;
  if (s_vc) {
    const VCacheStats *c = &g_vcache_stats;
    unsigned f = c->frames ? c->frames : 1;
    vita_log("vcache %d: %u frames; per frame %u projected (peak %u), %u invalid, %u ambiguous, "
             "%u unrecorded; %u polygons, %u%% with every vertex found, %u%% of vertices found; "
             "expand %u.%02u ms",
             vblank, c->frames, c->records / f, c->peak, c->invalid / f, c->ambiguous / f,
             c->full / f, c->polys / f, c->polys ? (unsigned)(100ull * c->persp / c->polys) : 0,
             c->verts ? (unsigned)(100ull * c->hits / c->verts) : 0,
             (unsigned)(s_st.expand_us / 1000), (unsigned)(s_st.expand_us % 1000 / 10));
    psx_vcache_stats_reset();
  }
  if (s_filter || s_cache) {
    vita_log("atlas %d: %u textured triangles looked up, %u not in the atlas (from VRAM), "
             "%u blocks decoded; %u of %u blocks in use; expand %u.%02u ms",
             vblank, s_ast.blocks, s_ast.misses, s_ast.decoded, s_ast.used, AGRID * AGRID,
             (unsigned)(s_st.expand_us / 1000), (unsigned)(s_st.expand_us % 1000 / 10));
    s_ast.blocks = s_ast.misses = s_ast.decoded = 0;
  }
  memset(&s_st, 0, sizeof s_st);
}

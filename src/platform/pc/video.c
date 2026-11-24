/* The window and the picture (docs/pc-build.md, D240): Encore's GPU mode
 * with GL 3.3 core (enc_video_open with ENC_GFX_GL, enc_video_gpu_begin /
 * _present: the backend owns the window, the context, the swap; D247), on
 * the main thread.
 *
 * At every display refresh (vsync=1; the swap waits for it) the newest frame
 * the game thread filed whose due vblank the game's clock has reached
 * (frames.c) is uploaded to one texture and drawn as a single quad: 4:3, or
 * 16:9 for the presenter's widescreen frames (512 pixels drawn for the wider
 * view, docs/presentation.md), as large as the window allows, centred, black
 * bars around. filter=1 scales it linearly, filter=0 by nearest neighbour.
 * F11 or Alt+Enter switches between the window and (borderless, the
 * desktop's own mode) fullscreen (the backend's); the window can be resized
 * freely. The title, the minimum size, fullscreen at start, the idle mouse
 * pointer and the game thread's shared context are gaps.c's (G4, G6). */
#include "pc.h"
#include "gaps.h"
#include "host.h"

#include "encore/platform.h"

#include <SDL3/SDL_opengl.h> /* the GL types only (gap G10) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the GL calls used here, all from the platform's loader (EncGpu.get_proc):
 * the executable links no GL library (SDL loads it), GL 1.1's included */
typedef const GLubyte *(APIENTRY *PFN_GetString)(GLenum);
typedef void(APIENTRY *PFN_GenTextures)(GLsizei, GLuint *);
typedef void(APIENTRY *PFN_BindTexture)(GLenum, GLuint);
typedef void(APIENTRY *PFN_TexParameteri)(GLenum, GLenum, GLint);
typedef void(APIENTRY *PFN_PixelStorei)(GLenum, GLint);
typedef void(APIENTRY *PFN_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                       const void *);
typedef void(APIENTRY *PFN_TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                                          GLenum, const void *);
typedef void(APIENTRY *PFN_Viewport)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *PFN_ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_Clear)(GLbitfield);
typedef void(APIENTRY *PFN_DrawArrays)(GLenum, GLint, GLsizei);
typedef void(APIENTRY *PFN_ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
#define GL_FUNCS(X)                                                                         \
  X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                            \
  X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                            \
  X(PFNGLCREATESHADERPROC, glCreateShader)                                                  \
  X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                  \
  X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                    \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                          \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                \
  X(PFNGLATTACHSHADERPROC, glAttachShader)                                                  \
  X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                    \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                  \
  X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                        \
  X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                      \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                      \
  X(PFNGLUNIFORM1IPROC, glUniform1i)                                                        \
  X(PFNGLUNIFORM4FPROC, glUniform4f)                                                        \
  X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                                \
  X(PFN_GetString, glGetString)                                                             \
  X(PFN_GenTextures, glGenTextures)                                                         \
  X(PFN_BindTexture, glBindTexture)                                                         \
  X(PFN_TexParameteri, glTexParameteri)                                                     \
  X(PFN_PixelStorei, glPixelStorei)                                                         \
  X(PFN_TexImage2D, glTexImage2D)                                                           \
  X(PFN_TexSubImage2D, glTexSubImage2D)                                                     \
  X(PFN_Viewport, glViewport)                                                               \
  X(PFN_ClearColor, glClearColor)                                                           \
  X(PFN_Clear, glClear)                                                                     \
  X(PFN_DrawArrays, glDrawArrays)                                                           \
  X(PFN_ReadPixels, glReadPixels)
#define PC_GL_DECL(type, name) static type p_##name;
GL_FUNCS(PC_GL_DECL)
#define PC_GL_LOAD(type, name)                                                                 \
  if (!(p_##name = (type)s_gpu.get_proc(#name))) {                                          \
    host_log("video: no %s", #name);                                                        \
    return -1;                                                                              \
  }

static EncGpu s_gpu;  /* the window, the context and the loader (the backend's) */
static void *s_game_gl; /* the GPU renderer's shared context (gpu=1, D243; gaps.c G6) */
void *(*g_pc_gl_proc)(const char *name);
volatile int g_pc_draw_w, g_pc_draw_h;
static GLuint s_prog, s_tex, s_vao;
static GLint s_u_rect, s_u_uv;
static int s_tex_w, s_tex_h;
static PcFrame s_cur; /* the frame on screen (its pixels uploaded) */
static int s_have, s_cur_gpu;
static EncMutex *s_shot_mu;
static char s_shot_path[1200];
static int s_shot_due;
static unsigned s_refreshes, s_new_frames;
static unsigned long long s_upload_us, s_swap_us;
static int s_vsync_on;

static const char *k_vs =
    "#version 330 core\n"
    "uniform vec4 u_rect;\n" /* the quad in clip space: x0, y0, x1, y1 */
    "uniform vec4 u_uv;\n"   /* its texture coordinates: u1, v1 (the frame in the texture) */
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "  gl_Position = vec4(mix(u_rect.xy, u_rect.zw, c), 0.0, 1.0);\n"
    "  v_uv = vec2(c.x, 1.0 - c.y) * u_uv.xy;\n"
    "}\n";
static const char *k_fs =
    "#version 330 core\n"
    "uniform sampler2D s_pic;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_col;\n"
    "void main() { o_col = vec4(texture(s_pic, v_uv).rgb, 1.0); }\n";

static GLuint shader(GLenum kind, const char *src) {
  GLuint s = p_glCreateShader(kind);
  GLint ok = 0;
  p_glShaderSource(s, 1, &src, NULL);
  p_glCompileShader(s);
  p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    p_glGetShaderInfoLog(s, sizeof log, NULL, log);
    host_log("video: shader: %s", log);
    return 0;
  }
  return s;
}

static int gl_setup(void) {
  GLuint vs, fs;
  GLint ok = 0;
  GL_FUNCS(PC_GL_LOAD)
  if (!(vs = shader(GL_VERTEX_SHADER, k_vs)) || !(fs = shader(GL_FRAGMENT_SHADER, k_fs)))
    return -1;
  s_prog = p_glCreateProgram();
  p_glAttachShader(s_prog, vs);
  p_glAttachShader(s_prog, fs);
  p_glLinkProgram(s_prog);
  p_glGetProgramiv(s_prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    p_glGetProgramInfoLog(s_prog, sizeof log, NULL, log);
    host_log("video: program: %s", log);
    return -1;
  }
  p_glUseProgram(s_prog);
  s_u_rect = p_glGetUniformLocation(s_prog, "u_rect");
  s_u_uv = p_glGetUniformLocation(s_prog, "u_uv");
  p_glUniform1i(p_glGetUniformLocation(s_prog, "s_pic"), 0);
  p_glGenVertexArrays(1, &s_vao); /* core profile: a draw needs one bound, empty is fine */
  p_glBindVertexArray(s_vao);
  p_glGenTextures(1, &s_tex);
  p_glActiveTexture(GL_TEXTURE0);
  p_glBindTexture(GL_TEXTURE_2D, s_tex);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  p_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
  return 0;
}

int pc_video_init(void) {
  const EncPlatformInfo *info = enc_platform_info();
  /* headless (SDL's dummy or offscreen driver) has no display to wait for:
   * no vsync there, nor where the swap interval is refused (below) */
  int vsync = g_pc_opts.vsync && !(info->caps & ENC_CAP_HEADLESS);
  /* refresh_hz is the display's, so the backend never paces the swap to a
   * 60 Hz clock of its own: the picture goes out at every display refresh
   * and frames.c picks the newest frame due (gap G11) */
  EncVideoConfig vc = {.source_width = 640, .source_height = 480, .refresh_hz = info->refresh_hz,
                       .aspect = ENC_ASPECT_4_3, .vsync = vsync, .smooth = g_pc_opts.filter,
                       .gfx = ENC_GFX_GL, .gfx_major = 3, .gfx_minor = 3};
  int rc;
  enc_mutex_create(&s_shot_mu);
  if (!(info->gfx_apis & (1u << ENC_GFX_GL))) {
    host_log("video: no GL 3.3 core context on this machine (%s)", pc_gap_video_driver());
    return -1;
  }
  if ((rc = enc_video_open(&vc)) != ENC_OK || (rc = enc_video_gpu(&s_gpu)) != ENC_OK) {
    host_log("video: enc_video_open (GL 3.3 core): %s", enc_error_name(rc));
    return -1;
  }
  /* G12: a driver that refuses the swap interval (Mesa under Xvfb) would have
   * the backend pace every present to a 60 Hz clock of its own, and a slow
   * picture then falls behind the frames due. Before D247 the PC ran without
   * vsync there; reopen so it still does. */
  if (vsync && !pc_gap_gl_swap_interval_on()) {
    enc_video_close();
    vc.vsync = vsync = 0;
    if ((rc = enc_video_open(&vc)) != ENC_OK || (rc = enc_video_gpu(&s_gpu)) != ENC_OK) {
      host_log("video: enc_video_open (GL 3.3 core, no vsync): %s", enc_error_name(rc));
      return -1;
    }
  }
  g_pc_gl_proc = s_gpu.get_proc;
  pc_gap_window_setup(s_gpu.window, "Spyro the Dragon", g_pc_opts.fullscreen);
  if (gl_setup() != 0)
    return -1;
  if (g_pc_opts.gpu) /* the game thread's context (gpu_gl.c draws on it), sharing textures
                      * and sync objects with this one; made here, on the main thread, and
                      * made current on the game thread (pc_video_game_context) */
    s_game_gl = pc_gap_gl_shared_context(s_gpu.window, s_gpu.context);
  s_vsync_on = vsync;
  host_log("video: %s, %s; GL %s; window %dx%d%s, display at %d Hz, vsync %s",
           pc_gap_video_driver(), (const char *)p_glGetString(GL_RENDERER),
           (const char *)p_glGetString(GL_VERSION), g_pc_opts.win_w, g_pc_opts.win_h,
           g_pc_opts.fullscreen ? " (fullscreen)" : "", info->refresh_hz, s_vsync_on ? "on" : "off");
  return 0;
}

int pc_video_game_context(void) { return pc_gap_gl_make_current(s_gpu.window, s_game_gl); }

void pc_video_close(void) { enc_video_close(); }

void pc_video_shot(const char *path, int due) {
  enc_mutex_lock(s_shot_mu);
  snprintf(s_shot_path, sizeof s_shot_path, "%s", path);
  s_shot_due = due;
  enc_mutex_unlock(s_shot_mu);
}

/* the window's pixels as a PNG (after the draw, before the swap) */
static void take_shot(int w, int h) {
  char path[1200];
  uint8_t *rgb;
  enc_mutex_lock(s_shot_mu);
  snprintf(path, sizeof path, "%s", s_shot_path);
  s_shot_path[0] = 0;
  enc_mutex_unlock(s_shot_mu);
  if (!path[0] || !(rgb = malloc((size_t)w * h * 3)))
    return;
  p_glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
  for (int y = 0; y < h / 2; y++) { /* GL's rows go up */
    uint8_t *a = rgb + (size_t)y * w * 3, *b = rgb + (size_t)(h - 1 - y) * w * 3;
    for (int i = 0; i < w * 3; i++) {
      uint8_t t = a[i];
      a[i] = b[i];
      b[i] = t;
    }
  }
  if (host_png_write(path, rgb, w, h) == 0)
    host_log("shot %s (%dx%d)", path, w, h);
  free(rgb);
}

void pc_video_frame(void) {
  int ww, wh, now = pc_vblank_now();
  PcFrame f;
  EncGpuFrame gf;
  unsigned long long t0 = pc_now_us(), t1;
  if (enc_video_gpu_begin(&gf) != ENC_OK) /* the main context current, the drawable's size */
    return;
  if (pc_frames_take(now, &f)) {
    if (f.rgba) {
      /* upload it now; the slot goes back at once */
      p_glBindTexture(GL_TEXTURE_2D, s_tex);
      if (f.w > s_tex_w || f.h > s_tex_h) {
        s_tex_w = f.w > s_tex_w ? (f.w > 512 ? 1024 : 512) : s_tex_w;
        s_tex_h = f.h > s_tex_h ? 512 : s_tex_h;
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s_tex_w, s_tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                       NULL);
      }
      p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.w, f.h, GL_RGBA, GL_UNSIGNED_BYTE, f.rgba);
      pc_frames_release();
    }
    /* a GPU picture (gpu_gl.c) stays in its slot, held, while it is shown */
    s_cur = f;
    s_cur.rgba = NULL;
    s_cur_gpu = f.rgba == NULL;
    s_have = 1;
    s_new_frames++;
  }
  t1 = pc_now_us();
  s_upload_us += t1 - t0;
  ww = gf.width;
  wh = gf.height;
  g_pc_draw_w = ww;
  g_pc_draw_h = wh;
  p_glViewport(0, 0, ww, wh);
  p_glClearColor(0, 0, 0, 1);
  p_glClear(GL_COLOR_BUFFER_BIT);
  if (s_have && ww > 0 && wh > 0) {
    /* the largest 4:3 (or 16:9) box in the window */
    float aspect = s_cur.wide ? 16.0f / 9.0f : 4.0f / 3.0f, bw = (float)ww, bh = bw / aspect;
    if (bh > wh) {
      bh = (float)wh;
      bw = bh * aspect;
    }
    {
      float x0 = -bw / ww, y0 = -bh / wh, u1, v1;
      GLint m = g_pc_opts.filter ? GL_LINEAR : GL_NEAREST;
      if (s_cur_gpu) {
        int tw, th;
        p_glBindTexture(GL_TEXTURE_2D, pc_gpu_picture_tex(s_cur.slot, &tw, &th));
        u1 = (float)s_cur.w / tw;
        v1 = (float)s_cur.h / th;
      } else {
        p_glBindTexture(GL_TEXTURE_2D, s_tex);
        u1 = (float)s_cur.w / s_tex_w;
        v1 = (float)s_cur.h / s_tex_h;
      }
      p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m);
      p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m);
      p_glUniform4f(s_u_rect, x0, y0, -x0, -y0);
      p_glUniform4f(s_u_uv, u1, v1, 0, 0);
      p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      if (s_cur_gpu)
        pc_gpu_picture_shown(s_cur.slot);
    }
  }
  if (s_shot_path[0] && s_have && s_cur.due >= s_shot_due)
    take_shot(ww, wh);
  pc_gap_cursor_update(s_gpu.window);
  enc_video_gpu_present(); /* swaps; never paced (refresh_hz is the display's) */
  s_swap_us += pc_now_us() - t1;
  s_refreshes++;
  if (!s_vsync_on)
    enc_sleep_us(1000); /* no vsync: about 1000 pictures a second at most */
}

void pc_video_report(int vblank) {
  static unsigned r0, n0;
  unsigned r = s_refreshes - r0 ? s_refreshes - r0 : 1;
  host_log("video %d: %u refreshes, %u new frames; per refresh %llu us upload, %llu us draw+swap",
           vblank, s_refreshes - r0, s_new_frames - n0, s_upload_us / r, s_swap_us / r);
  r0 = s_refreshes;
  n0 = s_new_frames;
  s_upload_us = s_swap_us = 0;
}

/* Optional presentation features over the original 30 Hz logic
 * (docs/presentation.md): 16:9 widescreen and 60 fps interpolated frames.
 * Both are off by default, and then PortPresentDraw() is GamestateDraw():
 * nothing here runs.
 *
 * The render path is the game's own GamestateDraw. A presentation pass
 * snapshots everything the game can see (the 2 MB RAM arena with the
 * scratchpad, the GTE registers, PsyCross's draw and display envs), adjusts
 * that state (narrower projection, blended camera and mobys), runs
 * GamestateDraw with VSync virtualised, captures the picture and restores the
 * snapshot, so its side effects never reach the game. The logic pass is the
 * original GamestateDraw. With faithful widescreen its DrawOTag is skipped:
 * only its side effects remain (m_WasDrawn, the sector flags, the animation
 * queues), computed with the original 4:3 frustum. */
#include "present.h"

#include "psx_arena.h"
#include "psx_spu.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "camera.h"
#include "graphics.h"
#include "moby.h"
#include "spyro.h"

#include <gtereg.h>
#include <libetc.h>
#include <libgpu.h>

extern DRAWENV activeDrawEnv; /* PsyCross LIBGPU.C */
extern DISPENV activeDispEnv;
extern int PsyX_Sys_GetVBlankCount(void); /* the platform's vblank counter */
extern void PsyX_WaitForTimestep(int count); /* the platform's VSync(0) */
extern void GamestateDraw(void);
extern unsigned int g_mips_v1; /* mips2c_rt.c */

PresentConfig g_present;
PresentStats g_present_stats;
int g_present_narrow; /* projection narrowed to 3/4 (camera matrix, HUD) */

/* Widescreen: 4:3 -> 16:9 narrows x by 3/4 about the screen centre. */
#define WS_NUM 3
#define WS_DEN 4
#define SCREEN_CX 256

int port_present_active(void) { return g_present.widescreen || g_present.interp; }

static long long now_us(void) {
  return (long long)clock() * 1000000 / CLOCKS_PER_SEC;
}

/* ---------- projection (camera.c, draw.c and r_moby's HUD path) ---------- */

/* camera.c CameraUpdateMatrices and draw.c's level-transition matrix: the
 * game already scales the Y row by 320/512 for the 4:3 TV. Narrowing the X
 * row the same way squeezes the picture into 512 columns for a 16:9 display
 * and widens every frustum test done in this space (r_moby's moby culling,
 * for one) by the same factor. */
void PortProjectionHook(short pM[3][3]) {
  int j;
  if (!g_present_narrow)
    return;
  for (j = 0; j < 3; j++)
    pM[0][j] = (short)(pM[0][j] * WS_NUM / WS_DEN);
}

/* r_moby.s HUD models (the 3D letters, counters and icons) project with
 * OFX = their screen x and a fixed diag(1, 0.625, 1) matrix
 * (func_80022A2C at 0x80022D2C and 0x80022D78; tools/mips2c/hints.yaml). */
unsigned int port_ws_hud_ofx(unsigned int ofx) {
  long long d = (long long)(int)ofx - (SCREEN_CX << 16);
  return (unsigned int)((SCREEN_CX << 16) + d * WS_NUM / WS_DEN);
}

unsigned int port_ws_hud_r11(unsigned int r11r12) {
  short r11 = (short)(r11r12 & 0xFFFF);
  return (r11r12 & 0xFFFF0000u) | (unsigned short)(r11 * WS_NUM / WS_DEN);
}

/* ---------- 2D primitives (widescreen) ---------- */

/* C-built 2D primitives go into the HUD OT through func_800168DC
 * (order_table.c). For DrawOTag their x is narrowed about the centre so
 * they keep their shape on a 16:9 display, then restored. Full-width ones
 * (fades, letterbox bars, separators) and the grayscale screen copy behind
 * the pause menus stay as they are. */
#define MAX_2D 512
#define MAX_SAVED 4096
static void *s_2d[MAX_2D];
static int s_n2d;
static int s_record_2d;
static struct {
  uint32_t *at;
  uint32_t old;
} s_saved[MAX_SAVED];
static int s_nsaved;

void port_present_note_2d(void *prim) {
  if (s_record_2d && s_n2d < MAX_2D)
    s_2d[s_n2d++] = prim;
}

static int narrow_x(int x) {
  int d = x - SCREEN_CX;
  /* round to nearest, symmetric about the centre */
  return SCREEN_CX + (d >= 0 ? (d * WS_NUM + WS_DEN / 2) / WS_DEN
                             : -((-d * WS_NUM + WS_DEN / 2) / WS_DEN));
}

static void save_word(uint32_t *w) {
  if (s_nsaved < MAX_SAVED) {
    s_saved[s_nsaved].at = w;
    s_saved[s_nsaved].old = *w;
    s_nsaved++;
  }
}

static void set_x(uint32_t *w, int x) {
  *w = (*w & 0xFFFF0000u) | (uint16_t)x;
}

#define XY_X(w) ((int)(int16_t)((w) & 0xFFFF))

/* One GP0 drawing command at p (words after the tag). Collects the words
 * holding vertex xy. Returns the number of xy words, 0 if not a drawing
 * command. */
static int prim_xy_words(uint32_t *p, int len, uint32_t **xy, int *rect_w) {
  uint32_t code = p[0] >> 24;
  int n = 0, i, k;
  *rect_w = -1;
  if ((code & 0xE0) == 0x20) { /* polygon */
    int verts = (code & 0x08) ? 4 : 3;
    int tex = (code & 0x04) != 0, gour = (code & 0x10) != 0;
    k = 1;
    for (i = 0; i < verts; i++) {
      if (gour && i > 0)
        k++;
      if (k >= len)
        return 0;
      xy[n++] = &p[k++];
      if (tex)
        k++;
    }
    return n;
  }
  if ((code & 0xE0) == 0x40) { /* line or polyline */
    int gour = (code & 0x10) != 0, poly = (code & 0x08) != 0;
    k = 1;
    for (i = 0; k < len; i++) {
      if (gour && i > 0) {
        if (poly && (p[k] & 0xF000F000u) == 0x50005000u)
          break;
        k++;
      }
      if (k >= len || (poly && i >= 2 && (p[k] & 0xF000F000u) == 0x50005000u))
        break;
      if (n < 8)
        xy[n++] = &p[k];
      k++;
      if (!poly && i == 1)
        break;
    }
    return n;
  }
  if ((code & 0xE0) == 0x60) { /* rectangle (TILE/SPRT) */
    int size = (code >> 3) & 3, tex = (code & 0x04) != 0;
    static const int fixed[4] = {0, 1, 8, 16};
    if (len < 2)
      return 0;
    xy[n++] = &p[1];
    if (size == 0 && len > 2 + tex)
      *rect_w = (int)(p[2 + tex] & 0xFFFF);
    else
      *rect_w = fixed[size];
    return n;
  }
  return 0;
}

/* The pause, inventory and flight-results backgrounds: POLY_FT4 strips of a
 * grayscale copy of the last frame, CLUT at VRAM (512,224) (draw.c). The copy
 * is already a widescreen frame, so it is shown full width. */
static int is_screen_copy(const uint32_t *p) {
  uint32_t code = p[0] >> 24;
  return (code & 0xFC) == 0x2C && (p[2] >> 16) == ((224u << 6) | (512u >> 4));
}

static void squeeze_2d(void) {
  int i, j;
  s_nsaved = 0;
  for (i = 0; i < s_n2d; i++) {
    uint32_t *tag = (uint32_t *)s_2d[i];
    int len = (int)(tag[0] >> 24);
    uint32_t *xy[8];
    int n, rect_w, minx = 1 << 30, maxx = -(1 << 30);
    if (len == 0)
      continue;
    n = prim_xy_words(tag + 1, len, xy, &rect_w);
    if (n == 0 || is_screen_copy(tag + 1))
      continue;
    for (j = 0; j < n; j++) {
      int x = XY_X(*xy[j]);
      minx = x < minx ? x : minx;
      maxx = x > maxx ? x : maxx;
    }
    if (rect_w > 0)
      maxx += rect_w;
    if (maxx - minx >= 511)
      continue; /* full width */
    if (rect_w >= 0) {
      /* TILE/SPRT can't change width: move its centre */
      int cx = XY_X(*xy[0]) + rect_w / 2;
      save_word(xy[0]);
      set_x(xy[0], narrow_x(cx) - rect_w / 2);
    } else {
      for (j = 0; j < n; j++) {
        save_word(xy[j]);
        set_x(xy[j], narrow_x(XY_X(*xy[j])));
      }
    }
    g_present_stats.squeezed_2d++;
  }
}

static void unsqueeze_2d(void) {
  while (s_nsaved > 0) {
    s_nsaved--;
    *s_saved[s_nsaved].at = s_saved[s_nsaved].old;
  }
  s_n2d = 0;
}

/* ---------- presenter: captured frames, shown one per vblank ---------- */

#define QUEUE 4
static uint16_t s_frames[QUEUE][PRESENT_W * PRESENT_H];
static uint8_t s_frame_wide[QUEUE];
static int s_qhead, s_qcount; /* oldest queued frame, number queued */
static uint16_t s_shown[PRESENT_W * PRESENT_H];
static int s_shown_wide, s_have_shown;
static int s_last_push_vblank = -1000;

static uint16_t s_capture[PRESENT_W * PRESENT_H];
static int s_captured, s_capture_on;

static void capture_now(void) {
  RECT16 r;
  r.x = 0;
  r.y = activeDrawEnv.ofs[1];
  r.w = PRESENT_W;
  r.h = PRESENT_H;
  StoreImage(&r, (u_long *)s_capture);
  s_captured = 1;
}

static void push_capture(int wide) {
  int slot;
  if (!s_captured)
    return;
  s_captured = 0;
  if (s_qcount == QUEUE) { /* drop the oldest */
    s_qhead = (s_qhead + 1) % QUEUE;
    s_qcount--;
  }
  slot = (s_qhead + s_qcount) % QUEUE;
  memcpy(s_frames[slot], s_capture, sizeof s_capture);
  s_frame_wide[slot] = (uint8_t)wide;
  s_qcount++;
  s_last_push_vblank = PsyX_Sys_GetVBlankCount();
}

const uint16_t *port_present_frame(void) {
  if (!port_present_active())
    return NULL;
  if (s_qcount > 0) {
    memcpy(s_shown, s_frames[s_qhead], sizeof s_shown);
    s_shown_wide = s_frame_wide[s_qhead];
    s_qhead = (s_qhead + 1) % QUEUE;
    s_qcount--;
    s_have_shown = 1;
  }
  /* Nothing presented lately (boot logos, loading, code that draws from
   * GamestateUpdate): show the PS1 display. */
  if (!s_have_shown || PsyX_Sys_GetVBlankCount() - s_last_push_vblank > 4)
    return NULL;
  return s_shown;
}

int port_present_frame_is_wide(void) { return s_shown_wide; }

/* ---------- PsyCross hooks ---------- */

static int s_suppress_ot; /* logic pass under faithful widescreen */

static int ot_pre(u_long *p) {
  (void)p;
  if (s_suppress_ot) {
    s_n2d = 0;
    return 1;
  }
  if (s_record_2d)
    squeeze_2d();
  return 0;
}

static void ot_post(u_long *p) {
  (void)p;
  unsqueeze_2d();
  if (s_capture_on)
    capture_now();
}

/* Inside a presentation pass time stands still: VSync(0) doesn't wait and
 * VSync(-1) is far enough ahead that the game's "two vblanks since the last
 * frame" loops end at once. The snapshot puts D_80075950 back. */
static int pass_vsync(int mode) {
  (void)mode;
  return PsyX_Sys_GetVBlankCount() + 1000;
}

/* --present-stats: LIBETC's VSync, timing the waits (the host mixes audio
 * and writes screenshots there) so the draw cost can leave them out. */
static int stats_vsync(int mode) {
  if (mode == 0) {
    long long t = now_us();
    PsyX_WaitForTimestep(1);
    g_present_stats.us_vsync += now_us() - t;
  }
  return PsyX_Sys_GetVBlankCount();
}

static void hooks_init(void) {
  static int done;
  if (done)
    return;
  done = 1;
  g_psyxDrawOTagPre = ot_pre;
  g_psyxDrawOTagPost = ot_post;
}

/* ---------- snapshot of the game-visible state ---------- */

static unsigned char *s_arena_snap;
static unsigned char s_scratch_snap[sizeof g_psx_scratch];
static GTERegisters s_gte_snap;
static DRAWENV s_drawenv_snap;
static DISPENV s_dispenv_snap;
static unsigned int s_v1_snap;

static void snap_take(void) {
  long long t = now_us();
  if (!s_arena_snap)
    s_arena_snap = malloc(PSX_RAM_SIZE);
  memcpy(s_arena_snap, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  memcpy(s_scratch_snap, g_psx_scratch, sizeof s_scratch_snap);
  s_gte_snap = gteRegs;
  s_drawenv_snap = activeDrawEnv;
  s_dispenv_snap = activeDispEnv;
  s_v1_snap = g_mips_v1;
  g_present_stats.us_snapshot += now_us() - t;
}

/* --present-stats 2: which pages did the pass change? (sizes a cheaper
 * snapshot, docs/presentation.md) */
static void dirty_scan(void) {
  const unsigned char *a = (const unsigned char *)g_psx_arena_base;
  long long n = 0;
  int i;
  for (i = 0; i < PRESENT_PAGES; i++)
    if (memcmp(a + i * 4096, s_arena_snap + i * 4096, 4096) != 0) {
      g_present_stats.dirty_union[i / 32] |= 1u << (i % 32);
      n++;
    }
  g_present_stats.dirty_pages += n;
  if (n > g_present_stats.dirty_max)
    g_present_stats.dirty_max = n;
}

static void snap_restore(void) {
  if (g_present.stats > 1)
    dirty_scan();
  long long t = now_us();
  memcpy((void *)g_psx_arena_base, s_arena_snap, PSX_RAM_SIZE);
  memcpy(g_psx_scratch, s_scratch_snap, sizeof s_scratch_snap);
  gteRegs = s_gte_snap;
  activeDrawEnv = s_drawenv_snap;
  activeDispEnv = s_dispenv_snap;
  g_mips_v1 = s_v1_snap;
  g_present_stats.us_snapshot += now_us() - t;
}

/* ---------- interpolation: transforms of the previous tick ---------- */

#define MAX_MOBYS 1024
typedef struct {
  short cls;
  signed char state;
  Vector3D pos;
  Vector3D8 rot;
} MobyXf;

static struct {
  int valid, gamestate, level;
  Vector3D cam_pos;
  Vector3D16 cam_rot;
  Vector3D spyro_pos;
  Vector3D8 spyro_rot;
  int nmobys;
  MobyXf mobys[MAX_MOBYS];
} s_prev;

static int count_mobys(void) {
  int n = 0;
  if (!g_LevelMobys)
    return 0;
  while (n < MAX_MOBYS && (signed char)g_LevelMobys[n].m_State != -1)
    n++;
  return n;
}

static long long s_t_update;

void PortPresentBeforeUpdate(void) {
  int i;
  if (g_present.stats)
    s_t_update = now_us();
  if (!g_present.interp)
    return;
  s_prev.valid = 1;
  s_prev.gamestate = g_Gamestate;
  s_prev.level = g_LevelId;
  s_prev.cam_pos = g_Camera.m_Position;
  s_prev.cam_rot = g_Camera.m_Rotation;
  s_prev.spyro_pos = g_Spyro.m_Position;
  s_prev.spyro_rot = g_Spyro.m_bodyRotation;
  s_prev.nmobys = g_present.interp_mobys ? count_mobys() : 0;
  for (i = 0; i < s_prev.nmobys; i++) {
    Moby *m = &g_LevelMobys[i];
    s_prev.mobys[i].cls = m->m_Class;
    s_prev.mobys[i].state = (signed char)m->m_State;
    s_prev.mobys[i].pos = m->m_Position;
    s_prev.mobys[i].rot = m->m_Rotation;
  }
}

/* Halfway between the previous tick and this one, measured from this one
 * (so values stay in the game's own range). Jumps (teleports, respawns,
 * camera cuts) are not blended. */
#define JUMP 0x2000
static int mid_i(int prev, int cur, int *ok) {
  int d = cur - prev;
  if (d > JUMP || d < -JUMP)
    *ok = 0;
  return cur - d / 2;
}

static int mid_vec(Vector3D *out, const Vector3D *prev, const Vector3D *cur) {
  int ok = 1;
  Vector3D r;
  r.x = mid_i(prev->x, cur->x, &ok);
  r.y = mid_i(prev->y, cur->y, &ok);
  r.z = mid_i(prev->z, cur->z, &ok);
  if (ok)
    *out = r;
  return ok;
}

static short mid_angle12(short prev, short cur) {
  int d = ((cur - prev + 0x800) & 0xFFF) - 0x800;
  return (short)(cur - d / 2);
}

static unsigned char mid_angle8(unsigned char prev, unsigned char cur) {
  int d = (signed char)(unsigned char)(cur - prev);
  return (unsigned char)(cur - d / 2);
}

static void mid_rot8(Vector3D8 *cur, const Vector3D8 *prev) {
  cur->x = mid_angle8(prev->x, cur->x);
  cur->y = mid_angle8(prev->y, cur->y);
  cur->z = mid_angle8(prev->z, cur->z);
}

static int can_interp(void) {
  return s_prev.valid && s_prev.gamestate == g_Gamestate &&
         s_prev.level == g_LevelId;
}

/* Blend in place (the snapshot restores it). Camera rotation feeds
 * CameraUpdateMatrices at the start of GamestateDraw. Returns 0 if the
 * camera jumped: no in-between frame then. */
static int blend_state(void) {
  int i, n;
  Vector3D cam = g_Camera.m_Position;
  if (!mid_vec(&cam, &s_prev.cam_pos, &g_Camera.m_Position))
    return 0;
  g_Camera.m_Position = cam;
  g_Camera.m_Rotation.x = mid_angle12(s_prev.cam_rot.x, g_Camera.m_Rotation.x);
  g_Camera.m_Rotation.y = mid_angle12(s_prev.cam_rot.y, g_Camera.m_Rotation.y);
  g_Camera.m_Rotation.z = mid_angle12(s_prev.cam_rot.z, g_Camera.m_Rotation.z);
  if (!g_present.interp_mobys)
    return 1;
  if (mid_vec(&g_Spyro.m_Position, &s_prev.spyro_pos, &g_Spyro.m_Position))
    mid_rot8(&g_Spyro.m_bodyRotation, &s_prev.spyro_rot);
  n = count_mobys();
  if (n > s_prev.nmobys)
    n = s_prev.nmobys;
  for (i = 0; i < n; i++) {
    Moby *m = &g_LevelMobys[i];
    const MobyXf *p = &s_prev.mobys[i];
    if (m->m_Class != p->cls || (signed char)m->m_State < 0 || p->state < 0)
      continue;
    if (mid_vec(&m->m_Position, &p->pos, &m->m_Position))
      mid_rot8(&m->m_Rotation, &p->rot);
  }
  return 1;
}

/* ---------- passes ---------- */

static void presentation_pass(int wide) {
  long long t = now_us();
  int (*vsync_hook)(int) = g_psyxVSyncHook;
  g_present_narrow = wide;
  s_record_2d = wide;
  s_n2d = 0;
  s_capture_on = 1;
  g_psyxVSyncHook = pass_vsync;
  g_psx_spu_frozen = 1;
  GamestateDraw();
  g_psx_spu_frozen = 0;
  g_present_stats.spu_dropped = g_psx_spu_dropped;
  g_psyxVSyncHook = vsync_hook;
  s_capture_on = 0;
  s_record_2d = 0;
  g_present_narrow = 0;
  push_capture(wide);
  g_present_stats.passes++;
  g_present_stats.us_pass += now_us() - t;
}

/* main.c, instead of GamestateDraw() (patches/spyro-1-port/0005). */
void PortPresentDraw(void) {
  int wide_pass, logic_wide;
  long long t;
  if (g_present.stats) {
    g_present_stats.us_update += now_us() - s_t_update;
    g_present_stats.ticks++;
  }
  if (!port_present_active()) {
    if (g_present.stats) {
      t = now_us();
      g_psyxVSyncHook = stats_vsync;
      GamestateDraw();
      g_psyxVSyncHook = NULL;
      g_present_stats.logic_passes++;
      g_present_stats.us_logic += now_us() - t;
    } else {
      GamestateDraw();
    }
    return;
  }
  hooks_init();
  /* faithful widescreen: picture from a presentation pass, logic pass 4:3 */
  wide_pass = g_present.widescreen && !g_present.ws_logic;
  logic_wide = g_present.widescreen && g_present.ws_logic;

  if (g_present.interp || wide_pass) {
    snap_take();
    if (g_present.interp) {
      if (can_interp() && blend_state()) {
        presentation_pass(g_present.widescreen);
        g_present_stats.interp_frames++;
      } else {
        g_present_stats.skipped_interp++;
      }
      snap_restore();
    }
    if (wide_pass) {
      presentation_pass(1);
      snap_restore();
    }
  }

  /* The logic pass: the game's own draw, side effects and all. */
  t = now_us();
  if (g_present.stats)
    g_psyxVSyncHook = stats_vsync;
  s_suppress_ot = wide_pass;
  g_present_narrow = logic_wide;
  s_record_2d = logic_wide;
  s_capture_on = !wide_pass;
  GamestateDraw();
  s_capture_on = 0;
  s_record_2d = 0;
  g_present_narrow = 0;
  s_suppress_ot = 0;
  g_psyxVSyncHook = NULL;
  if (!wide_pass)
    push_capture(logic_wide);
  g_present_stats.logic_passes++;
  g_present_stats.us_logic += now_us() - t;
}

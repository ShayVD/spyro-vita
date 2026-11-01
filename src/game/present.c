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
#include "lod.h"
#include "savestate.h"

#include "psx_arena.h"
#include "psx_spu.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "buffers.h"
#include "common.h"
#include "camera.h"
#include "environment.h"
#include "gamepad.h"
#include "graphics.h"
#include "moby.h"
#include "renderers.h"
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
int g_present_narrow; /* projection narrowed to 17/21 (camera matrix, HUD) */

/* Widescreen narrows x about the screen centre by 17/21 (D67). The picture
 * shown is the 512x224 the game draws (draw areas 8 lines into the 240-line
 * display, initialization.c), filling the 960x544 screen: 4:3 at 240 lines
 * is 0.625 x per y, at 960x544 from 512x224 x gets 1.875/2.4286 of y, so x
 * narrows by 0.625 / 0.772 = 17/21 (3/4 was 16:9 over all 240 lines). */
#define WS_NUM 17
#define WS_DEN 21
#define SCREEN_CX 256

int port_present_active(void) { return g_present.widescreen || g_present.interp; }

static long long clock_cpu_us(void) {
  return (long long)clock() * 1000000 / CLOCKS_PER_SEC;
}

long long (*g_present_clock_us)(void) = clock_cpu_us;
long long (*g_present_render_us)(void);

static long long now_us(void) { return g_present_clock_us(); }

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

unsigned int port_ws_r1112(unsigned int r11r12) {
  short r11 = (short)(r11r12 & 0xFFFF), r12 = (short)(r11r12 >> 16);
  return (unsigned short)(r11 * WS_NUM / WS_DEN) |
         (unsigned int)(unsigned short)(r12 * WS_NUM / WS_DEN) << 16;
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

/* A ring of PRESENT_SLOTS frames: up to QUEUE waiting and the one on
 * screen. Without a
 * platform capture (g_present_capture NULL) a slot holds the pixels
 * (StoreImage of the drawn area); with one (the Vita's GPU renderer) the
 * platform keeps the picture in its own slot and we only number them. */
#define QUEUE 4
static uint16_t s_frames[PRESENT_SLOTS][PRESENT_W * PRESENT_H];
static uint8_t s_frame_wide[PRESENT_SLOTS];
static int s_qhead, s_qcount; /* oldest queued slot, number queued */
static int s_shown = -1;      /* slot on screen */
static int s_in_game_draw;    /* inside GamestateDraw (a pass or the real one) */
static int s_foreign_vblank = -1; /* last DrawOTag from anywhere else */
static int s_popped;          /* the last port_present_frame_slot() took a new one */
static int s_last_push_vblank = -1000;

const PresentCapture *g_present_capture;

static uint16_t s_capture[PRESENT_W * PRESENT_H];
static int s_captured, s_capture_on;

static void capture_now(void) {
  RECT16 r;
  r.x = 0;
  r.y = activeDrawEnv.ofs[1];
  r.w = PRESENT_W;
  r.h = PRESENT_H;
  if (g_present_capture) {
    g_present_capture->capture(r.x, r.y, r.w, r.h);
  } else {
    int frozen = g_psyxVramFrozen;
    g_psyxVramFrozen = 0; /* our own read, not the game's */
    StoreImage(&r, (u_long *)s_capture);
    g_psyxVramFrozen = frozen;
  }
  s_captured = 1;
}

static void push_capture(int wide) {
  int slot;
  if (!s_captured)
    return;
  s_captured = 0;
  if (s_qcount == QUEUE) { /* drop the oldest */
    s_qhead = (s_qhead + 1) % PRESENT_SLOTS;
    s_qcount--;
  }
  /* This may be the slot on screen (after a run of drops); harmless: the
   * queue isn't empty now, so the next vblank shows a newer slot. */
  slot = (s_qhead + s_qcount) % PRESENT_SLOTS;
  if (g_present_capture)
    g_present_capture->commit(slot);
  else
    memcpy(s_frames[slot], s_capture, sizeof s_capture);
  s_frame_wide[slot] = (uint8_t)wide;
  s_qcount++;
  s_last_push_vblank = PsyX_Sys_GetVBlankCount();
}

/* A save state was loaded (D51): the queued frames are from before it, and
 * the vblank counts the fallback compares were the old timeline's. */
void port_present_reset(void) {
  s_qcount = 0;
  s_shown = -1;
  s_captured = 0;
  s_foreign_vblank = -1;
  s_last_push_vblank = -1000;
}

int port_present_frame_slot(void) {
  if (!port_present_active()) {
    s_qcount = 0; /* switched off (a menu): drop what was queued */
    s_shown = -1;
    return -1;
  }
  s_popped = s_qcount > 0;
  if (s_qcount > 0) {
    s_shown = s_qhead;
    s_qhead = (s_qhead + 1) % PRESENT_SLOTS;
    s_qcount--;
  }
  /* The game drew outside GamestateDraw since our last frame (loading
   * screens, code that draws from GamestateUpdate): that picture is the PS1
   * display's. A slow tick alone keeps the last frame on screen: falling
   * back then made the picture jump between 16:9 and 4:3. */
  if (s_shown < 0 || s_foreign_vblank > s_last_push_vblank) {
    g_present_stats.display_fallbacks++;
    return -1;
  }
  return s_shown;
}

const uint16_t *port_present_slot_pixels(int slot) {
  return slot < 0 || slot >= PRESENT_SLOTS || g_present_capture ? NULL : s_frames[slot];
}

const uint16_t *port_present_shown_pixels(void) {
  if (s_shown < 0 || g_present_capture || s_foreign_vblank > s_last_push_vblank)
    return NULL;
  return s_frames[s_shown];
}

const uint16_t *port_present_frame(void) {
  int slot = port_present_frame_slot();
  return slot < 0 || g_present_capture ? NULL : s_frames[slot];
}

int port_present_frame_new(void) { return s_popped; }

int port_present_frame_is_wide(void) {
  return s_shown >= 0 && s_frame_wide[s_shown];
}

/* ---------- PsyCross hooks ---------- */

static int s_suppress_ot; /* logic pass under faithful widescreen */

static int ot_pre(u_long *p) {
  (void)p;
  if (!s_in_game_draw)
    s_foreign_vblank = PsyX_Sys_GetVBlankCount();
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

/* LIBETC's VSync, timing the waits (the host mixes audio and writes
 * screenshots there; the Vita mixes, presents and waits for the display) so
 * the game's own CPU time can leave them out: --present-stats and
 * interp_budget_us. */
static long long s_tick_vsync_us;

static int timed_vsync(int mode) {
  if (mode == 0) {
    long long t = now_us(), d;
    PsyX_WaitForTimestep(1);
    d = now_us() - t;
    g_present_stats.us_vsync += d;
    s_tick_vsync_us += d;
  }
  return PsyX_Sys_GetVBlankCount();
}

/* ---------- the primitive space (D54) ---------- */

/* GamestateDraw points each draw at its DB's primitive buffer (AllocateBuffers:
 * lower, upper, then the HUD OT and the world OT), 0x1C000 bytes or 0x13000
 * in reduced-space levels, with the HUD's mobys and the sky and particle
 * renderers' limit (D_80075780 - 0x400, r_cyclorama.s, r_particles.s) at
 * +0x1C000. The world and moby renderers check no bound. The original 4:3
 * view fits: the ending of level 64 (reduced space) reaches 73 KB of 76. The
 * wider frustum of a 16:9 draw went past the upper buffer into the OTs.
 * DrawOTag takes the primitives at once here, so with any presentation
 * option or draw distance on, every draw starts at the lower buffer and spans
 * both, as the pause screens' draws already start there (draw.c
 * func_8001A40C). The HUD's mobys and that limit go to the top, 0x600 under
 * the HUD OT. Those 0x600 bytes are the save fairy's card buffer
 * (func_800314B4 reads the save's header there and builds the save there);
 * the original's fairy draw keeps them free by moving the mobys down to
 * +0x1BA00 (draw.c func_8001D718), which it does only when the top is the
 * HUD OT itself, so the port keeps them free in every draw (D61). With
 * everything off nothing changes. */
static int poly_space_on(void) { return port_present_active() || g_lod.scale > LOD_ONE; }

void *port_poly_start(void *db_buf) {
  return poly_space_on() ? g_Buffers.m_LowerPolyBuffer : db_buf;
}

#define FAIRY_CARD_BUF 0x600 /* g_Buffers.m_HudOTStart - 0x600, func_800314B4 */

void PortPolySpace(void) {
  void *top = (char *)g_Buffers.m_HudOTStart - FAIRY_CARD_BUF;
  if (!poly_space_on())
    return;
  D_800757B0 = g_Buffers.m_LowerPolyBuffer;
  D_80075780 = top;
  D_800756FC = top;
  g_HudMobys = top;
}

/* Every GamestateDraw: shown = its picture is shown (lod.c, D49). */
static void game_draw(int shown) {
  port_lod_draw_begin(shown);
  GamestateDraw();
  port_lod_draw_end(shown);
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

/* A pass may write anywhere the draw path writes, but the game only reads
 * some of it again (docs/presentation.md, --present-dirty):
 *  - the scratchpad (first KB of the arena) and g_psx_scratch;
 *  - the game's data, sdata, sbss and bss, 0x8006BBE0-0x8007AA38 (psx.map
 *    main_DATA_START..main_BSS_END): camera, Spyro, g_SonyImage, the globals;
 *  - the level's moby array (m_WasDrawn, animation state, blended transforms);
 *  - the particle space under m_SharedAnimations (AllocateBuffers, 4BEF8.c);
 *  - bytes 0x18-0x1B of every sector header, a cache func_800258F0 reads
 *    (`lw $gp, 0x18($s1)`) and rewrites (`sb $sp, 0x18..0x1B($s1)`).
 * Those are saved and restored. Everything else a pass writes is scratch the
 * next real draw rewrites before anyone reads it: the two primitive buffers
 * and the OTs (linking clears every bucket) and the emulated stack below
 * the caller's $sp. --present-dirty keeps a full copy as well and reports
 * any other page left changed. */
#define DATA_LO 0x8006BBE0u
#define DATA_HI 0x8007AA38u
#define STACK_LO 0x80010000u /* mips2c's emulated stack, top 0x80020000 */
#define STACK_HI 0x80020000u
#define PARTICLE_SPACE 0x2000
#define OT_BYTES 0x4000
#define MAX_REGIONS 4

typedef struct {
  unsigned char *at;
  uint32_t len;
} Region;

static Region s_regions[MAX_REGIONS];
static int s_nregions;
static unsigned char *s_region_snap;
static size_t s_region_cap;
static unsigned char *s_arena_full; /* --present-dirty only */
static unsigned char s_scratch_snap[sizeof g_psx_scratch];
static GTERegisters s_gte_snap;
static DRAWENV s_drawenv_snap;
static DISPENV s_dispenv_snap;
static unsigned int s_v1_snap;

#define SECTOR_CACHE 0x18
#define MAX_SECTORS 4096
static uint32_t s_sector_snap[MAX_SECTORS];
static int s_nsectors;

static int count_mobys(void);

static int in_arena(const void *p, uint32_t len) {
  uintptr_t a = (uintptr_t)p;
  return a >= g_psx_arena_base && len <= PSX_RAM_SIZE &&
         a - g_psx_arena_base <= PSX_RAM_SIZE - len;
}

static void add_region(void *at, uint32_t len) {
  if (s_nregions < MAX_REGIONS && len && in_arena(at, len)) {
    s_regions[s_nregions].at = at;
    s_regions[s_nregions].len = len;
    s_nregions++;
  }
}

static void collect_regions(void) {
  s_nregions = 0;
  add_region(PSX_ARENA(0x80000000u), sizeof g_psx_scratch);
  add_region(PSX_ARENA(DATA_LO), DATA_HI - DATA_LO);
  add_region(g_LevelMobys, (uint32_t)(count_mobys() + 1) * sizeof(Moby));
  add_region(g_Buffers.m_ParticleSpaceStart, PARTICLE_SPACE);
}

static unsigned char *sector_cache(int i) {
  unsigned char *sec =
      (unsigned char *)(uintptr_t)((uint32_t *)g_Environment.m_SectorPointer)[i];
  return in_arena(sec + SECTOR_CACHE, 4) ? sec + SECTOR_CACHE : NULL;
}

static void sectors_take(void) {
  int i;
  s_nsectors = 0;
  if (!in_arena(g_Environment.m_SectorPointer, 4) ||
      g_Environment.m_SectorCount <= 0 || g_Environment.m_SectorCount > MAX_SECTORS)
    return;
  s_nsectors = g_Environment.m_SectorCount;
  for (i = 0; i < s_nsectors; i++) {
    unsigned char *c = sector_cache(i);
    if (c)
      memcpy(&s_sector_snap[i], c, 4);
  }
}

static void sectors_restore(void) {
  int i;
  for (i = 0; i < s_nsectors; i++) {
    unsigned char *c = sector_cache(i);
    if (c)
      memcpy(c, &s_sector_snap[i], 4);
  }
}

static void snap_take(void) {
  long long t = now_us();
  size_t need = 0;
  unsigned char *o;
  int i;
  collect_regions();
  for (i = 0; i < s_nregions; i++)
    need += s_regions[i].len;
  if (need > s_region_cap) {
    free(s_region_snap);
    s_region_snap = malloc(need);
    s_region_cap = need;
  }
  for (o = s_region_snap, i = 0; i < s_nregions; o += s_regions[i].len, i++)
    memcpy(o, s_regions[i].at, s_regions[i].len);
  sectors_take();
  g_present_stats.snapshot_bytes += (long long)need + s_nsectors * 4;
  if (g_present.stats > 1) {
    if (!s_arena_full)
      s_arena_full = malloc(PSX_RAM_SIZE);
    memcpy(s_arena_full, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  }
  memcpy(s_scratch_snap, g_psx_scratch, sizeof s_scratch_snap);
  s_gte_snap = gteRegs;
  s_drawenv_snap = activeDrawEnv;
  s_dispenv_snap = activeDispEnv;
  s_v1_snap = g_mips_v1;
  g_present_stats.us_snapshot += now_us() - t;
}

/* PS1 address range [lo, hi) the next real draw rewrites: the primitive
 * buffers and both OTs (AllocateBuffers: lower, higher, HUD OT, world OT). */
static int scratch_range(uint32_t *lo, uint32_t *hi) {
  if (!in_arena(g_Buffers.m_LowerPolyBuffer, 1) ||
      !in_arena(g_Buffers.m_WorldOTStart, OT_BYTES))
    return 0;
  *lo = 0x80000000u +
        (uint32_t)((uintptr_t)g_Buffers.m_LowerPolyBuffer - g_psx_arena_base);
  *hi = 0x80000000u +
        (uint32_t)((uintptr_t)g_Buffers.m_WorldOTStart - g_psx_arena_base) +
        OT_BYTES;
  return 1;
}

/* Does page `page` differ from the full copy outside the scratch areas? */
static int page_missed(int page) {
  const unsigned char *a = (const unsigned char *)g_psx_arena_base;
  uint32_t lo = 0x80000000u + (uint32_t)page * 4096, hi = lo + 4096;
  uint32_t s_lo, s_hi, k;
  if (lo >= STACK_LO && hi <= STACK_HI)
    return 0;
  if (!scratch_range(&s_lo, &s_hi) || hi <= s_lo || lo >= s_hi)
    return 1;
  for (k = lo; k < hi; k++)
    if ((k < s_lo || k >= s_hi) &&
        a[k - 0x80000000u] != s_arena_full[k - 0x80000000u])
      return 1;
  return 0;
}

/* --present-dirty: which pages did the pass change, and did the regions
 * cover every one the game can read? Runs after the region restore, against
 * the full copy; then restores the full copy so the run stays exact. */
static void dirty_check(void) {
  const unsigned char *a = (const unsigned char *)g_psx_arena_base;
  long long n = 0;
  int i;
  for (i = 0; i < PRESENT_PAGES; i++) {
    if (memcmp(a + i * 4096, s_arena_full + i * 4096, 4096) == 0)
      continue;
    g_present_stats.dirty_union[i / 32] |= 1u << (i % 32);
    n++;
    if (page_missed(i)) {
      g_present_stats.missed_union[i / 32] |= 1u << (i % 32);
      g_present_stats.missed_pages++;
    }
  }
  g_present_stats.dirty_pages += n;
  if (n > g_present_stats.dirty_max)
    g_present_stats.dirty_max = n;
  memcpy((void *)g_psx_arena_base, s_arena_full, PSX_RAM_SIZE);
}

static void snap_restore(void) {
  long long t = now_us();
  const unsigned char *o;
  int i;
  for (o = s_region_snap, i = 0; i < s_nregions; o += s_regions[i].len, i++)
    memcpy(s_regions[i].at, o, s_regions[i].len);
  sectors_restore();
  g_present_stats.snapshot_bytes += (long long)(o - s_region_snap) + s_nsectors * 4;
  memcpy(g_psx_scratch, s_scratch_snap, sizeof s_scratch_snap);
  gteRegs = s_gte_snap;
  activeDrawEnv = s_drawenv_snap;
  activeDispEnv = s_dispenv_snap;
  g_mips_v1 = s_v1_snap;
  g_present_stats.us_snapshot += now_us() - t;
  if (g_present.stats > 1)
    dirty_check(); /* after the timing: it isn't part of the cost */
}

/* ---------- interpolation: transforms of the previous tick ---------- */

#define MAX_MOBYS 1024
#define MAX_PARTICLES (PARTICLE_SPACE / 32) /* loaders.c: 256 slots, 0xFF ends */
/* An animation track: animation, next animation, frame, next frame and
 * progress toward the next frame (a new frame every `len` units). */
typedef struct {
  unsigned char anim, next, frame, nframe, prog;
} Track;

typedef struct {
  short cls;
  signed char state;
  Vector3D pos;
  Vector3D8 rot;
  Track track;
} MobyXf;

static struct {
  int valid, gamestate, level;
  Vector3D cam_pos;
  Vector3D16 cam_rot;
  Vector3D spyro_pos;
  Vector3D8 spyro_rot[3]; /* body, head, tail */
  Track spyro_track[3];
  int nmobys;
  MobyXf mobys[MAX_MOBYS];
  int have_particles;
  Particle particles[MAX_PARTICLES];
} s_prev;

/* Moby animation (AnimationState, func_800522C0): 64 progress units per
 * frame. Spyro's body, head and tail (pete.c func_8003CB24 and kin): 16. */
#define MOBY_FRAME 64
#define SPYRO_FRAME 16

static Track moby_track(const Moby *m) {
  Track t;
  t.anim = m->m_AnimationState.m_Animation;
  t.next = m->m_AnimationState.m_NextAnimation;
  t.frame = m->m_AnimationState.m_Frame;
  t.nframe = m->m_AnimationState.m_NextFrame;
  t.prog = m->m_AnimationState.m_FrameProgress;
  return t;
}

static void set_moby_track(Moby *m, const Track *t) {
  m->m_AnimationState.m_Animation = t->anim;
  m->m_AnimationState.m_NextAnimation = t->next;
  m->m_AnimationState.m_Frame = t->frame;
  m->m_AnimationState.m_NextFrame = t->nframe;
  m->m_AnimationState.m_FrameProgress = t->prog;
}

/* Spyro's tracks k = 0 body, 1 head, 2 tail (r_pete reads these bytes). */
static Track spyro_track(int k) {
  const unsigned char *anims = &g_Spyro.m_bodyAnimation;
  const unsigned char *frames = &g_Spyro.m_bodyAnimationFrame;
  const unsigned char *progs = &g_Spyro.m_bodyFrameProgress;
  Track t;
  t.anim = anims[2 * k];
  t.next = anims[2 * k + 1];
  t.frame = frames[2 * k];
  t.nframe = frames[2 * k + 1];
  t.prog = progs[k];
  return t;
}

static void set_spyro_track(int k, const Track *t) {
  unsigned char *anims = &g_Spyro.m_bodyAnimation;
  unsigned char *frames = &g_Spyro.m_bodyAnimationFrame;
  unsigned char *progs = &g_Spyro.m_bodyFrameProgress;
  anims[2 * k] = t->anim;
  anims[2 * k + 1] = t->next;
  frames[2 * k] = t->frame;
  frames[2 * k + 1] = t->nframe;
  progs[k] = t->prog;
}

static Vector3D8 *spyro_rot(int k) {
  return k == 0 ? &g_Spyro.m_bodyRotation
                : k == 1 ? &g_Spyro.m_headRotation : &g_Spyro.m_tailRotation;
}

/* Halfway along a track. A tick advances progress and crosses at most one
 * frame (then frame = the old next frame, anim = the old next anim), so the
 * midpoint is either between the same two frames or on one side of the
 * crossing. Anything else (a new animation started, a loop restarted) keeps
 * this tick's pose. Returns 1 if the track was blended. */
static int mid_track(Track *cur, const Track *prev, int len) {
  int mid;
  if (cur->anim == prev->anim && cur->next == prev->next &&
      cur->frame == prev->frame && cur->nframe == prev->nframe) {
    if (cur->prog < prev->prog)
      return 0;
    cur->prog = (unsigned char)((prev->prog + cur->prog) / 2);
    return 1;
  }
  if (cur->anim == prev->next && cur->frame == prev->nframe &&
      prev->prog < len && cur->prog < len) {
    mid = (prev->prog + cur->prog + len) / 2;
    if (mid >= len) {
      cur->prog = (unsigned char)(mid - len);
    } else {
      *cur = *prev;
      cur->prog = (unsigned char)mid;
    }
    return 1;
  }
  return 0;
}

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
  long long t;
  int i;
  savestate_point(); /* the main loop top: save states happen here (D51) */
  if (g_present.stats || g_present.interp_budget_us)
    s_t_update = now_us();
  if (!g_present.interp) {
    s_prev.valid = 0; /* switched on later: no blending with an old tick */
    return;
  }
  t = now_us();
  s_prev.valid = 1;
  s_prev.gamestate = g_Gamestate;
  s_prev.level = g_LevelId;
  s_prev.cam_pos = g_Camera.m_Position;
  s_prev.cam_rot = g_Camera.m_Rotation;
  s_prev.spyro_pos = g_Spyro.m_Position;
  for (i = 0; i < 3; i++) {
    s_prev.spyro_rot[i] = *spyro_rot(i);
    s_prev.spyro_track[i] = spyro_track(i);
  }
  s_prev.nmobys = g_present.interp_mobys ? count_mobys() : 0;
  for (i = 0; i < s_prev.nmobys; i++) {
    Moby *m = &g_LevelMobys[i];
    s_prev.mobys[i].cls = m->m_Class;
    s_prev.mobys[i].state = (signed char)m->m_State;
    s_prev.mobys[i].pos = m->m_Position;
    s_prev.mobys[i].rot = m->m_Rotation;
    s_prev.mobys[i].track = moby_track(m);
  }
  s_prev.have_particles = g_present.interp_anim &&
                          in_arena(g_Particles, sizeof s_prev.particles);
  if (s_prev.have_particles)
    memcpy(s_prev.particles, g_Particles, sizeof s_prev.particles);
  g_present_stats.us_blend += now_us() - t;
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

/* Particles keep their slot for life (particle_alloc.s); a slot whose class
 * and render type match and whose timer moved on a little is the same
 * particle one tick later. A reused slot starts its timer again. */
#define PARTICLE_JUMP 0x800
static void blend_particles(void) {
  int i, k;
  for (i = 0; i < MAX_PARTICLES && g_Particles[i].m_RenderType != 0xFF; i++) {
    Particle *c = &g_Particles[i];
    const Particle *p = &s_prev.particles[i];
    short *cp = &c->m_Data.m_Position.x;
    const short *pp = &p->m_Data.m_Position.x;
    int d[3];
    if (c->m_Class != p->m_Class || c->m_RenderType != p->m_RenderType ||
        c->m_Timer < p->m_Timer || c->m_Timer - p->m_Timer > 16)
      continue;
    for (k = 0; k < 3; k++) {
      d[k] = cp[k] - pp[k];
      if (d[k] > PARTICLE_JUMP || d[k] < -PARTICLE_JUMP)
        break;
    }
    if (k < 3)
      continue;
    for (k = 0; k < 3; k++)
      cp[k] = (short)(cp[k] - d[k] / 2);
    g_present_stats.particles_blended++;
  }
}

static void count_track(int blended) {
  if (blended)
    g_present_stats.anim_blended++;
  else
    g_present_stats.anim_kept++;
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
  if (mid_vec(&g_Spyro.m_Position, &s_prev.spyro_pos, &g_Spyro.m_Position)) {
    for (i = 0; i < 3; i++) {
      Track t = spyro_track(i);
      mid_rot8(spyro_rot(i), &s_prev.spyro_rot[i]);
      if (g_present.interp_anim) {
        count_track(mid_track(&t, &s_prev.spyro_track[i], SPYRO_FRAME));
        set_spyro_track(i, &t);
      }
    }
  }
  n = count_mobys();
  if (n > s_prev.nmobys)
    n = s_prev.nmobys;
  for (i = 0; i < n; i++) {
    Moby *m = &g_LevelMobys[i];
    const MobyXf *p = &s_prev.mobys[i];
    if (m->m_Class != p->cls || (signed char)m->m_State < 0 || p->state < 0)
      continue;
    if (mid_vec(&m->m_Position, &p->pos, &m->m_Position)) {
      Track t = moby_track(m);
      mid_rot8(&m->m_Rotation, &p->rot);
      if (g_present.interp_anim) {
        count_track(mid_track(&t, &p->track, MOBY_FRAME));
        set_moby_track(m, &t);
      }
    }
  }
  if (s_prev.have_particles && in_arena(g_Particles, sizeof s_prev.particles))
    blend_particles();
  return 1;
}

/* ---------- passes ---------- */

/* interp_budget_us bookkeeping (PortPresentDraw) */
static long long s_busy_wo_interp, s_interp_cost;

/* While the logic draws 4:3 (attract demos, ws_exact), the 16:9 passes keep
 * their own copy of the renderer's visibility state: D_800771C8, one byte
 * per sector that r_environment's last draw found visible, which
 * func_800521C0 reads at the start of the next draw to drop mobys in hidden
 * sectors; and the sector caches (SECTOR_CACHE). With the 4:3 copy, mobys
 * in sectors only the 16:9 view shows were dropped: they popped in at the
 * edges. */
#define SECTOR_FLAGS 0x800771C8u /* psx.map D_800771C8, 0x100 bytes */
static uint8_t s_wide_flags[0x100];
static uint32_t s_wide_cache[MAX_SECTORS];
static int s_wide_valid, s_wide_level, s_wide_nsectors;

static void wide_state_in(void) {
  int i;
  if (!s_wide_valid || s_wide_level != g_LevelId ||
      s_wide_nsectors != g_Environment.m_SectorCount)
    return;
  memcpy(PSX_ARENA(SECTOR_FLAGS), s_wide_flags, sizeof s_wide_flags);
  for (i = 0; i < s_wide_nsectors && i < MAX_SECTORS; i++) {
    unsigned char *c = sector_cache(i);
    if (c)
      memcpy(c, &s_wide_cache[i], 4);
  }
}

static void wide_state_out(void) {
  int i;
  memcpy(s_wide_flags, PSX_ARENA(SECTOR_FLAGS), sizeof s_wide_flags);
  s_wide_nsectors = 0;
  if (in_arena(g_Environment.m_SectorPointer, 4) && g_Environment.m_SectorCount > 0 &&
      g_Environment.m_SectorCount <= MAX_SECTORS) {
    s_wide_nsectors = g_Environment.m_SectorCount;
    for (i = 0; i < s_wide_nsectors; i++) {
      unsigned char *c = sector_cache(i);
      if (c)
        memcpy(&s_wide_cache[i], c, 4);
    }
  }
  s_wide_level = g_LevelId;
  s_wide_valid = 1;
}

/* Runs one pass and leaves its picture in s_capture; the caller queues it.
 * own_state: the pass keeps its own visibility state (wide_state_in). */
static void presentation_pass(int wide, int own_state) {
  long long t = now_us();
  int (*vsync_hook)(int) = g_psyxVSyncHook;
  g_present_narrow = wide;
  s_record_2d = wide;
  s_n2d = 0;
  s_capture_on = 1;
  g_psyxVSyncHook = pass_vsync;
  g_psx_spu_frozen = 1;
  g_psyxVramFrozen = 1; /* the pause screen's VRAM saves happen once, for real */
  if (own_state)
    wide_state_in();
  s_in_game_draw = 1;
  game_draw(1);
  s_in_game_draw = 0;
  if (own_state)
    wide_state_out();
  g_psyxVramFrozen = 0;
  g_psx_spu_frozen = 0;
  g_present_stats.spu_dropped = g_psx_spu_dropped;
  g_psyxVSyncHook = vsync_hook;
  s_capture_on = 0;
  s_record_2d = 0;
  g_present_narrow = 0;
  g_present_stats.passes++;
  g_present_stats.us_pass += now_us() - t;
}

/* main.c, instead of GamestateDraw() (patches/spyro-1-port/0005). */
void PortPresentDraw(void) {
  int wide_pass, logic_wide, interp, timed;
  long long t, t_interp = 0;
  if (g_present.stats) {
    g_present_stats.us_update += now_us() - s_t_update;
    g_present_stats.ticks++;
  }
  timed = g_present.stats || g_present.interp_budget_us;
  if (!port_present_active()) {
    if (g_present.stats) {
      t = now_us();
      s_tick_vsync_us = 0;
      g_psyxVSyncHook = timed_vsync;
      game_draw(1);
      g_psyxVSyncHook = NULL;
      g_present_stats.logic_passes++;
      g_present_stats.us_logic += now_us() - t - s_tick_vsync_us;
    } else {
      game_draw(1);
    }
    return;
  }
  hooks_init();
  /* Widescreen: one 16:9 draw (the logic draw itself), except in attract
   * demos (or with ws_exact), where the logic must stay the original 4:3 one
   * for the recorded input to replay: there the logic draw stays 4:3 and
   * hidden, and the picture comes from a 16:9 presentation pass. */
  {
    int exact = g_present.ws_exact || (!g_present.ws_logic && g_DemoMode != DEMO_MODE_NONE);
    wide_pass = g_present.widescreen && exact;
    logic_wide = g_present.widescreen && !exact;
  }

  interp = g_present.interp && can_interp();
  if (g_present.interp && !interp)
    g_present_stats.skipped_interp++;
  /* interp_budget_us: the last tick's game CPU time without the in-between
   * frame, plus what that frame cost when it last ran, must fit. Otherwise
   * this tick shows its real frame for both vblanks (30 fps for a moment)
   * instead of running late, which would slow the game itself (D28). */
  if (interp && g_present.interp_budget_us &&
      (s_busy_wo_interp + s_interp_cost > g_present.interp_budget_us ||
       (g_present_render_us && 2 * g_present_render_us() > g_present.interp_budget_us))) {
    interp = 0;
    g_present_stats.budget_skips++;
  }
  if (interp || wide_pass) {
    snap_take();
    if (interp) {
      t_interp = now_us();
      long long tb = now_us();
      int blended = blend_state();
      g_present_stats.us_blend += now_us() - tb;
      if (blended) {
        presentation_pass(g_present.widescreen, wide_pass);
        push_capture(g_present.widescreen); /* shown before this tick's frame */
        g_present_stats.interp_frames++;
      } else {
        g_present_stats.skipped_interp++;
      }
      snap_restore();
      t_interp = now_us() - t_interp;
      s_interp_cost = t_interp;
    }
    if (wide_pass) {
      presentation_pass(1, 1); /* queued after the logic pass, below */
      snap_restore();
    }
  }

  /* The logic pass: the game's own draw, side effects and all. */
  t = now_us();
  s_tick_vsync_us = 0;
  if (timed)
    g_psyxVSyncHook = timed_vsync;
  s_suppress_ot = wide_pass;
  g_present_narrow = logic_wide;
  s_record_2d = logic_wide;
  s_capture_on = !wide_pass;
  s_in_game_draw = 1;
  game_draw(!wide_pass);
  s_in_game_draw = 0;
  s_capture_on = 0;
  s_record_2d = 0;
  g_present_narrow = 0;
  s_suppress_ot = 0;
  g_psyxVSyncHook = NULL;
  /* This tick's frame goes in the queue where the original draw finishes,
   * so real frames are shown on the same vblanks as without presentation. */
  push_capture(wide_pass || logic_wide);
  g_present_stats.logic_passes++;
  g_present_stats.us_logic += now_us() - t - (timed ? s_tick_vsync_us : 0);
  if (g_present.interp_budget_us)
    s_busy_wo_interp = now_us() - s_t_update - s_tick_vsync_us - t_interp;
}

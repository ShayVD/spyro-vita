/* The presenter's draw (stage 7e, decisions D125; pres_draw.h): its
 * context from the game's state, the frame, and the calls the presenter's
 * objects make in place of the game's (pres_names.h). Ordinary game code,
 * on the game thread. */
#include "pres_draw.h"
#include "pres_snap.h"

#include "lod.h"
#include "native_draw.h"
#include "present.h"
#include "render_ctx.h"
#ifndef RENDER_CTX_READABLE
#define RENDER_CTX_READABLE 0 /* before 7d2 */
#endif
#include "psx_arena.h"
#include "psx_clock.h"
#include "psx_walk.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdlib.h>
#include <string.h>

#include "buffers.h"
#include "common.h"
#include "environment.h"
#include "graphics.h"
#include "moby.h"
#include "spyro.h"
#include "renderers.h"

#include <libgpu.h>

#if SPYRO_PRESENTER
const int g_pres_built = 1; /* GamestateDraw_pres: pres_draw.o (cmake/presenter.cmake) */
#else
const int g_pres_built = 0;
void GamestateDraw_pres(void) {}
#endif

#define OT_BYTES 0x4000
#define FAIRY_CARD_BUF 0x600 /* present.c PortPolySpace */

extern char D_80077DD8[], D_8006FCF4[], D_800771C8[];
extern void *g_pres_snap_D_800756FC, *g_pres_snap_g_HudMobys;
unsigned pres_lod_state_size(void);
void pres_lod_state_set(const unsigned char *p);

void pres_draw_want(void) {
  if (g_pres_built)
    render_ctx_want_area();
}

/* fast_render 2 or 3 (the host's and the Vita's defaults, D124): the
 * presenter's renderers are 3's readable code, the same pictures as both */
int pres_draw_available(void) {
  static int s_labels = -1; /* stage 7g (D128): the named labels where the mirror copies from */
  if (!g_pres_built || !render_ctx_area() || !(g_fast_render_ro == 2 || g_fast_render_ro == 3))
    return 0;
  if (s_labels < 0)
    s_labels = pres_snap_misplaced() == NULL;
  return s_labels;
}

/* ---- the calls the presenter's objects make in place of the game's ---- */

/* Inside the presenter time stands still (D30's pass_vsync, until D129). VSync(0)
 * doesn't wait, and VSync(-1) is far enough ahead that the draw's "two
 * vblanks since the last frame" loops end at once; D_80075950 is the
 * snapshot's. Stage 7g (D128): but each VSync(0) moves the presenter's own
 * count on one, as a vblank would. On the flight results' first tick the
 * draw sets the pacer from VSync(-1) and then waits for two vblanks after
 * it (flight.c: the tail after both branches), which a count that never
 * moves would wait for forever (D129's check draws that tick; D30's pass
 * never did, nor does the in-between frame: a gamestate's first tick has
 * none). The draws use VSync's value for the pacer alone, so no frame
 * changes. pres_draw_prepare starts the count. */
static int s_vsyncs;

int pres_VSync(int mode) {
  if (mode == 0)
    s_vsyncs++;
  return psx_clock_count() + 1000 + s_vsyncs;
}

/* VRAM transfers: the presenter never makes them (the logic draw makes
 * the pause screens' copies once, for real; D30's pass dropped them under
 * g_psx_vram_frozen until D129) */
void pres_LoadImage(RECT *r, u_long *p) { (void)r, (void)p; }
void pres_StoreImage(RECT *r, u_long *p) { (void)r, (void)p; }
void pres_MoveImage(RECT *r, int x, int y) { (void)r, (void)x, (void)y; }
void pres_ClearImage(RECT *r, u_char cr, u_char cg, u_char cb) { (void)r, (void)cr, (void)cg, (void)cb; }
/* PAL's clear of both 512x256 buffers (initialization.c, D134), which its
 * game-over draw makes at tick 15 (D139): ClearImage twice, so dropped too */
void pres_ClearFrameBuffersPal(void) {}

/* present.c PortPolySpace (D54), on the presenter's buffers: every draw
 * starts at the lower buffer and spans both, the HUD's mobys and the
 * limit 0x600 under the HUD OT. It is called only while a presentation
 * option is on, which the presenter needs. */
void pres_PortPolySpace(void) {
  const uint32_t lower = pres_snap_rebase((uint32_t)(uintptr_t)g_Buffers.m_LowerPolyBuffer);
  const uint32_t top = pres_snap_rebase((uint32_t)(uintptr_t)g_Buffers.m_HudOTStart) - FAIRY_CARD_BUF;
  memcpy(g_pres_prim_next, &lower, 4);
  memcpy(g_pres_prim_end, &top, 4);
  g_pres_snap_D_800756FC = (void *)(uintptr_t)top;
  g_pres_snap_g_HudMobys = (void *)(uintptr_t)top;
}

/* present.c PortProjectionHook on the presenter's 16:9 switch */
void pres_PortProjectionHook(short pM[3][3]) {
  int j;
  if (!g_pres_narrow)
    return;
  for (j = 0; j < 3; j++)
    pM[0][j] = (short)(pM[0][j] * 17 / 21);
}


/* native_draw.c nd_list_linked on the presenter's cursor: the walk's mode
 * for the list (the game's rule, into the presenter's own: its walk takes
 * it, stage 7f), and the cursor off its pool. The game's pool, serial and
 * g_psx_walk_nd don't move. */
int g_pres_walk_nd;

void pres_nd_list_linked(void) {
  int nd = g_native_draw;
  if (g_native_draw && g_nd_direct && g_nd_direct_ok)
    nd |= g_nd_direct == 2 ? ND_WALK_RANGES : ND_WALK_DIRECT;
  g_pres_walk_nd = nd;
  g_pres_nd.base = g_pres_nd.wp = g_pres_nd.lim = NULL;
}

/* ---- the context ---- */

static u32 s_serial;

/* The presenter's records go to a pool of its own, as big as the game's
 * (stage 7f, D126: 7e used the game's other pool, which the logic draw
 * may write while the presenter runs beside it), under a serial of its
 * own; its walk finds them there (PsxGpuWalk's nd_pool). The game's walk
 * is joined first, as before: the presenter starts from the GPU state it
 * leaves. */
#define POOL_WORDS (256u * 1024u) /* native_draw.c's */
static u32 *s_pool;

void pres_draw_pool(const uint32_t **w, uint32_t *cap) {
  *w = s_pool;
  *cap = s_pool ? POOL_WORDS : 0;
}

static void records_begin(void) {
  const u32 pool = 0; /* the marker's bit: its walk reads s_pool whatever it says */
  psx_walk_join();
  if (!s_pool && g_nd_pool[0].w) /* native_draw on */
    s_pool = malloc(POOL_WORDS * sizeof(u32));
  g_pres_nd.base = g_pres_nd.wp = s_pool;
  g_pres_nd.lim = s_pool ? s_pool + POOL_WORDS - ND_MAXW : NULL;
  g_pres_nd.id = ND_MAGIC | pool << 11 | s_serial;
  g_pres_nd.key = g_pres_nd.id << 21;
  g_pres_nd.on = 0;
  s_serial = (s_serial + 1) & ND_SERIAL;
  memset(g_pres_nd_side, 0, sizeof(NdSide) * ND_SIDE_N);
}

static int sector_count(void) {
  const int n = g_Environment.m_SectorCount;
  uintptr_t a = (uintptr_t)g_Environment.m_SectorPointer;
  if (n <= 0 || a < g_psx_arena_base || a - g_psx_arena_base >= PSX_MEM_SIZE)
    return 0;
  return n < PRES_SEEN ? n : PRES_SEEN;
}

static u32 sector_anims(int i) {
  const u32 sec = MIPS_KUSEG(((const u32 *)g_Environment.m_SectorPointer)[i]);
  const uintptr_t a = (uintptr_t)(sec + 0x18u);
  return a >= g_psx_arena_base && a - g_psx_arena_base < PSX_MEM_SIZE ? *(const u32 *)a : 0;
}

/* LOD's state as the game's port_lod_draw_begin(1) leaves it for a
 * shown draw (the game's state is put back), its buffer addresses moved onto
 * the presenter's */
static unsigned char s_lod_game[512], s_lod[512];
static void lod_begin(void) {
  const unsigned n = port_lod_state_size();
  unsigned i;
  if (n > sizeof s_lod || n != pres_lod_state_size())
    return;
  port_lod_state_get(s_lod_game);
  port_lod_draw_begin(1);
  port_lod_state_get(s_lod);
  port_lod_state_set(s_lod_game);
  for (i = 0; i + 4 <= n; i += 4) {
    uint32_t w;
    memcpy(&w, s_lod + i, 4);
    if (g_pres_regions[PRES_REGION_BUFFERS].len && w - g_pres_regions[PRES_REGION_BUFFERS].lo <=
                                                       g_pres_regions[PRES_REGION_BUFFERS].len)
      w = pres_snap_rebase(w);
    memcpy(s_lod + i, &w, 4);
  }
  pres_lod_state_set(s_lod);
}

/* The renderers' state as the logic draw finds it in the game's: the register
 * file, the scratchpad, the spill and work areas, the sector flags and
 * bytes, the shadow list, the glows, sparkles and flame. */
static void context(int wide, const uint8_t *flags, const uint32_t *caches, int ncaches) {
  int i, n;
  g_pres_gte = gteRegs;
#if SPYRO_NATIVE_MEM
  memcpy(g_pres_scratch, g_psx_scratch, sizeof g_pres_scratch);
#else
  memcpy(g_pres_scratch, (void *)g_psx_arena_base, sizeof g_pres_scratch);
#endif
#if !RENDER_CTX_READABLE /* 7d2's readable renderers have no spill area or hand stack */
  memcpy(g_pres_spill, D_80077DD8, PRES_SPILL);
#endif
  memcpy(g_pres_work, D_8006FCF4, PRES_WORK);
  memcpy(g_pres_seen, flags ? (const void *)flags : (const void *)D_800771C8, PRES_SEEN);
  memcpy(g_pres_moby_shadows, &g_MobyShadows, PRES_SHADOWS);
  memcpy(g_pres_glows, g_Glows, PRES_GLOWS);
  pres_snap_fields((unsigned char *)g_pres_glows + 0x8, PRES_GLOWS / 0x24, 0x24); /* each glow's position */
  memcpy(g_pres_sparkles, g_Sparkles, PRES_SPARKLES);
  memcpy(g_pres_spyro_flame, &g_SpyroFlame, PRES_FLAME);
  n = sector_count();
  for (i = 0; i < n; i++)
    g_pres_sec_anims[i] = caches ? (i < ncaches ? caches[i] : 0) : sector_anims(i);
#if !RENDER_CTX_READABLE
  g_pres_mips_sp = render_ctx_stack_top();
#endif
  g_pres_narrow = wide;
  g_pres_rtp_hook = NULL;
  g_pres_rtp_value_hook = NULL;
  records_begin();
  lod_begin();
}

/* The overlay functions the game would call through its pointers, as
 * indices the presenter's pres_flight5, pres_flight4 and pres_balloon4
 * (pres_ovl.c) map onto its own copies. */
int g_pres_ovl[PRES_OVLS] = {-1, -1, -1};
long long g_pres_ovl_unknown;
#if SPYRO_PRESENTER
#include "overlay_pointers.h"
#define FLIGHT(n) void Flight5_LEVEL_##n(void); int Flight4_LEVEL_##n(int, Vector3D *, int);
#define BALLOON(n) void Balloonist4_LEVEL_##n(void);
PRES_FLIGHT_LEVELS(FLIGHT)
PRES_BALLOON_LEVELS(BALLOON)
#define F5(n) (void *)Flight5_LEVEL_##n,
#define F4(n) (void *)Flight4_LEVEL_##n,
#define B4(n) (void *)Balloonist4_LEVEL_##n,
static int ovl_index(int which, const void *fn, void *const *game, int n) {
  int i;
  for (i = 0; i < n; i++)
    if (fn == game[i])
      return g_pres_ovl[which] = i;
  g_pres_ovl[which] = -1;
  return -1;
}
static void ovl_pick(void) {
  static void *const flight5[] = {PRES_FLIGHT_LEVELS(F5) NULL};
  static void *const flight4[] = {PRES_FLIGHT_LEVELS(F4) NULL};
  static void *const balloon4[] = {PRES_BALLOON_LEVELS(B4) NULL};
  ovl_index(PRES_OVL_FLIGHT5, (const void *)D_8007567C, flight5, PRES_FLIGHTS);
  ovl_index(PRES_OVL_FLIGHT4, (const void *)D_800758C4, flight4, PRES_FLIGHTS);
  ovl_index(PRES_OVL_BALLOON4, (const void *)D_800758D8, balloon4, PRES_BALLOONS);
  /* the gamestates that call them: a pointer the presenter can't follow */
  if ((g_Gamestate == GS_FlightResults && (g_pres_ovl[PRES_OVL_FLIGHT5] < 0 || g_pres_ovl[PRES_OVL_FLIGHT4] < 0)) ||
      (g_Gamestate == GS_Balloonist && g_pres_ovl[PRES_OVL_BALLOON4] < 0))
    g_pres_ovl_unknown++;
}
#else
static void ovl_pick(void) {}
#endif

int pres_draw_prepare(int blend, int wide, const uint8_t *flags, const uint32_t *caches,
                      int ncaches) {
  if (!pres_draw_available() || !pres_snap_take(blend))
    return 0;
  s_vsyncs = 0;
  ovl_pick();
  context(wide, flags, caches, ncaches);
  return 1;
}


void pres_draw_visibility(uint8_t *flags, uint32_t *caches, int ncaches) {
  int i, n = sector_count();
  memcpy(flags, g_pres_seen, PRES_SEEN);
  for (i = 0; i < n && i < ncaches; i++)
    caches[i] = g_pres_sec_anims[i];
}

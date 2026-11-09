/* The presenter's context (stage 7d, decisions D123; render_ctx.h): the
 * state render_ctx_names.h renames the renderers' onto, and the calls the
 * presenter builds make in place of the game's (nd_begin, nd_end). Since
 * 7d2 (D124) the presenter build is fast_render=3's readable code: its GTE
 * is the control registers alone, and it has no scratchpad, spill area or
 * hand stack, only its working memory. LOD's hooks are lod_pres.c's. */
#include "render_ctx.h"
#include "gte_native.h"
#include "psx_arena.h"

#include <stdint.h>
#include <string.h>

/* the control registers (the data half only a view, render_ctx_names.h) */
GTERegisters g_pres_gte;
/* the renderers' working memory (render_hand.h SCRATCH_BASE) */
unsigned char g_pres_scratch[1024] __attribute__((aligned(1024)));
char g_pres_work[PRES_WORK] __attribute__((aligned(4)));
char g_pres_seen[PRES_SEEN] __attribute__((aligned(4)));
char g_pres_moby_shadows[PRES_SHADOWS] __attribute__((aligned(4)));
char g_pres_particles[PRES_PARTICLES] __attribute__((aligned(4)));
char g_pres_glows[PRES_GLOWS] __attribute__((aligned(4)));
char g_pres_sparkles[PRES_SPARKLES] __attribute__((aligned(4)));
char g_pres_spyro_flame[PRES_FLAME] __attribute__((aligned(4)));
char g_pres_prim_next[4] __attribute__((aligned(4)));
char g_pres_prim_end[4] __attribute__((aligned(4)));
char g_pres_prim_full[4] __attribute__((aligned(4)));
char g_pres_world_ot[4] __attribute__((aligned(4)));
char g_pres_hud_ot[4] __attribute__((aligned(4)));
NdCursor g_pres_nd;
NdSide g_pres_nd_side[ND_SIDE_N];
unsigned g_pres_nd_fallback;
int g_pres_narrow;

/* Nothing to set up since 7d2 (D124): the subdivision tables are read as
 * offsets into the working memory. Kept for the callers. */
void render_ctx_init(void) {}

/* The vertex cache's hook on a native projection (gte_ops.c's ngte_vcache,
 * on the presenter's hook) */
void pres_ngte_vcache(const NgteProj *p, unsigned int vxy, unsigned int vz, const NgteRtp *o) {
  const int proj[15] = {p->r11, p->r12, p->r13, p->r21, p->r22, p->r23, p->r31, p->r32, p->r33,
                        p->trx, p->try_, p->trz, p->ofx, p->ofy, p->h};
  g_pres_rtp_value_hook(proj, ngte_vx(vxy), ngte_vy(vxy), ngte_vz(vz), o->sxy, (int)o->sz3);
}
void (*g_pres_rtp_hook)(int idx, int sf);
void (*g_pres_rtp_value_hook)(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3);

/* native_draw.c's, on the presenter's cursor (its check isn't the
 * presenter's: --native-draw-check runs the game's builds) */
void pres_nd_begin(void) { g_pres_nd.on = g_native_draw == 1 && g_fast_render_ro >= 2; }
void pres_nd_end(void) { g_pres_nd.on = 0; }

/* The sectors the presenter animates (env_hand.c): a copy each, in the
 * area after its buffers, from the start again at each environment draw. A
 * sector: its header (0x1C bytes), the low-poly data (SEC_HIGH's top byte,
 * in words), the high-poly vertices (its low byte, words), colours (its
 * second byte, near and far: two words each) and polygons (its third byte,
 * four words each). */
u32 g_pres_sec_anims[PRES_SEEN];
unsigned g_pres_sector_misses;
unsigned g_pres_face_misses; /* moby_hand.c: face lists too long for the copy */
/* stage 7f (D126): the game's draw replaces faces in a copy while the
 * presenter draws beside it (present.c sets it), and the lists too long
 * for that copy, replaced in place */
int g_faces_copy;
unsigned g_faces_in_place;
static uintptr_t s_sec_next;
RenderCtxSector g_pres_sectors[PRES_SECTORS_MAX];
int g_pres_nsectors;
void pres_sectors_reset(void) {
  s_sec_next = g_psx_arena_extra_at ? g_psx_arena_extra_at + PRES_SECTORS_AT : 0;
  g_pres_nsectors = 0;
}
u32 pres_sector_copy(u32 sec) {
  const u32 h = *(const u32 *)(uintptr_t)(sec + 0x14);
  const u32 len = 0x1Cu + (h >> 24) * 4u + (h & 0xFFu) * 4u + ((h >> 8) & 0xFFu) * 8u + ((h >> 16) & 0xFFu) * 16u;
  uintptr_t at = s_sec_next;
  if (!at || at + len > g_psx_arena_extra_at + PRES_AREA) {
    g_pres_sector_misses++;
    return 0;
  }
  memcpy((void *)at, (const void *)(uintptr_t)sec, len);
  s_sec_next = (at + len + 3u) & ~(uintptr_t)3u;
  if (g_pres_nsectors < PRES_SECTORS_MAX) {
    g_pres_sectors[g_pres_nsectors].sec = sec;
    g_pres_sectors[g_pres_nsectors].copy = (u32)at;
    g_pres_sectors[g_pres_nsectors++].len = len;
  }
  return (u32)at;
}

/* the area (arena.c) */
void render_ctx_want_area(void) { g_psx_arena_extra = PRES_AREA; }
uintptr_t render_ctx_area(void) { return g_psx_arena_extra_at; }

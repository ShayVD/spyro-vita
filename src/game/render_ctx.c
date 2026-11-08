/* The presenter's context (stage 7d, decisions D123; render_ctx.h): the
 * state render_ctx_names.h renames the renderers' onto, and the calls the
 * presenter builds make in place of the game's (nd_begin, nd_end). The GTE
 * register file and commands are render_ctx_gte_regs.c's and
 * render_ctx_gte_ops.c's; LOD's hooks are lod_pres.c's. */
#include "render_ctx.h"
#include "psx_arena.h"

#include <stdint.h>
#include <string.h>

/* the scratchpad, as aligned as the PS1's (D106) */
unsigned char g_pres_scratch[1024] __attribute__((aligned(1024)));
char g_pres_spill[PRES_SPILL] __attribute__((aligned(4)));
char g_pres_work[PRES_WORK] __attribute__((aligned(4)));
char g_pres_seen[PRES_SEEN] __attribute__((aligned(4)));
char g_pres_moby_shadows[PRES_SHADOWS] __attribute__((aligned(4)));
char g_pres_particles[PRES_PARTICLES] __attribute__((aligned(4)));
char g_pres_glows[PRES_GLOWS] __attribute__((aligned(4)));
char g_pres_sparkles[PRES_SPARKLES] __attribute__((aligned(4)));
char g_pres_spyro_flame[PRES_FLAME] __attribute__((aligned(4)));
static u32 s_stack[PRES_STACK / 4];
u32 g_pres_mips_sp;
char g_pres_prim_next[4] __attribute__((aligned(4)));
char g_pres_prim_end[4] __attribute__((aligned(4)));
char g_pres_prim_full[4] __attribute__((aligned(4)));
char g_pres_world_ot[4] __attribute__((aligned(4)));
char g_pres_hud_ot[4] __attribute__((aligned(4)));
NdCursor g_pres_nd;
NdSide g_pres_nd_side[ND_SIDE_N];
unsigned g_pres_nd_fallback;
int g_pres_narrow;

/* The subdivision tables whose words are scratchpad addresses (env_hand.c;
 * port.c scratch_relocs), as the presenter's: its own scratchpad's. */
extern char D_8006CF48[], D_8006CF58[], D_8006D0D8[];
u32 g_pres_grid2[4], g_pres_grid4[16], g_pres_inner4[4];
static void tables(u32 *to, const char *from, int n) {
  int i;
  for (i = 0; i < n; i++)
    to[i] = (u32)(uintptr_t)g_pres_scratch + (((const u32 *)from)[i] & 0x3FFu);
}
void render_ctx_init(void) {
  tables(g_pres_grid2, D_8006CF48, 4);
  tables(g_pres_grid4, D_8006CF58, 16);
  tables(g_pres_inner4, D_8006D0D8, 4);
}

/* The top, less the 0x20 bytes of argument space mips2c's entries leave
 * above $sp (render_hand.h HAND_RENDERER). */
u32 render_ctx_stack_top(void) { return (u32)(uintptr_t)(s_stack + PRES_STACK / 4) - 0x20u; }

/* native_draw.c's, on the presenter's cursor (its check isn't the
 * presenter's: --native-draw-check runs the game's builds) */
void pres_nd_begin(void) { g_pres_nd.on = g_native_draw == 1 && g_fast_render_ro == 2; }
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

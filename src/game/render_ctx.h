/* The presenter's context (stage 7d, decisions D123; docs/native-plan.md
 * stage 7, "The plan refreshed (D119)").
 *
 * Each renderer file is compiled once more as NAME_hand_pres.c: the
 * native-GTE code with every piece of state it writes renamed onto the
 * presenter's own (render_ctx_names.h). Its entries are NAME_pres, and a
 * call of one touches nothing the game's own draw uses: the GTE register
 * file, the scratchpad, the spill area and hand stack, the work area, the
 * sector flags, the primitive buffer and OTs, the record pool, LOD's state.
 * So the presenter (7e) can draw while the game's draw runs (7f). The
 * logic's build is unchanged. Since 7d2 (D124) the presenter build is
 * fast_render=3's readable code: of the GTE it has the control registers
 * only, and in place of the scratchpad, spill area and hand stack, its own
 * working memory.
 *
 * This header is the game side's view: the presenter's state by its own
 * names, the entries, and the host's check (--render-ctx-check,
 * render_ctx_check.c). */
#ifndef RENDER_CTX_H
#define RENDER_CTX_H
/* 7d2 (D124): the presenter builds are fast_render=3's readable code; its
 * context has the GTE's control registers only, and no scratchpad copy,
 * spill area or hand stack (7e keys on this) */
#define RENDER_CTX_READABLE 1

#include "mips2c_rt.h"
#include "native_draw.h"
#include <gtereg.h>

/* the presenter builds' entries (render_hand.h HAND_RENDERER) */
#ifndef RENDER_CTX_FN
#define RENDER_CTX_FN
typedef u32 (*RenderCtxFn)(u32, u32, u32, u32);
#endif
u32 func_800258F0_pres(u32, u32, u32, u32); /* the environment */
u32 func_8001F798_pres(u32, u32, u32, u32); /* the mobys */
u32 func_80022A2C_pres(u32, u32, u32, u32); /* the lit mobys */
u32 func_80020F34_pres(u32, u32, u32, u32); /* the specular mobys */
u32 func_8004EBA8_pres(u32, u32, u32, u32); /* the sky */
u32 func_80023AC4_pres(u32, u32, u32, u32); /* Spyro */
u32 func_80059A48_pres(u32, u32, u32, u32); /* Spyro's shadow */
u32 func_80059F8C_pres(u32, u32, u32, u32); /* the mobys' shadows */
u32 func_80058D64_pres(u32, u32, u32, u32); /* the flame */
u32 func_800573C8_pres(u32, u32, u32, u32); /* the particles */
u32 func_80058BA8_pres(u32, u32, u32, u32); /* the glows and sparkles */
u32 func_80058864_pres(u32, u32, u32, u32); /* the dragon rescue's ring */

/* ---- the presenter's state (render_ctx_names.h maps the game's names) ---- */
/* the control registers (CP2C); the data half is a view the shared
 * headers compile against, which render_ctx_check keeps unused */
extern GTERegisters g_pres_gte;
extern unsigned char g_pres_scratch[1024]; /* the working memory */
enum {
  PRES_WORK = 0x31A0,  /* D_8006FCF4 (g_SonyImage's space) */
  PRES_SEEN = 0x100,   /* D_800771C8 */
  PRES_SHADOWS = 0xC,  /* g_MobyShadows (MobyShadow) */
  PRES_PARTICLES = 4,  /* g_Particles: the particle list's pointer */
  PRES_GLOWS = 0x240,  /* g_Glows: 16 of 0x24 bytes */
  PRES_SPARKLES = 0xC0, /* g_Sparkles: 8 of 0x18 bytes */
  PRES_FLAME = 0x138,  /* g_SpyroFlame */
};
extern char g_pres_work[PRES_WORK], g_pres_seen[PRES_SEEN];
extern char g_pres_moby_shadows[PRES_SHADOWS], g_pres_particles[PRES_PARTICLES];
extern char g_pres_glows[PRES_GLOWS], g_pres_sparkles[PRES_SPARKLES], g_pres_spyro_flame[PRES_FLAME];
extern char g_pres_prim_next[4], g_pres_prim_end[4], g_pres_prim_full[4];
extern char g_pres_world_ot[4], g_pres_hud_ot[4];
extern NdCursor g_pres_nd;
extern NdSide g_pres_nd_side[ND_SIDE_N];
extern unsigned g_pres_nd_fallback;
extern int g_pres_narrow;
extern void (*g_pres_rtp_hook)(int idx, int sf);
extern void (*g_pres_rtp_value_hook)(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3);
extern int g_pres_lod_k, g_pres_lod_pass, g_pres_lod_stats;

/* At start (nothing to do since D124). */
void render_ctx_init(void);

/* The presenter's area: PRES_AREA bytes the arena's allocation adds after
 * the game's memory (arena.c), so OT links and packed addresses reach it
 * as they reach the game's. 0 until psx_arena_init, and only when
 * render_ctx_want_area() was called before it. Its first 2 MB are the
 * presenter's buffers (in 7d the check's mirror of them), then the sectors
 * it animates (env_hand.c). */
enum { PRES_SECTORS_AT = 0x200000, PRES_SECTORS = 0x40000, PRES_AREA = PRES_SECTORS_AT + PRES_SECTORS };
/* the four animation bytes of each sector header, as the presenter has
 * them (env_hand.c SEC_ANIMS_OF); sectors the presenter animated and had
 * no room for, since the start */
extern u32 g_pres_sec_anims[PRES_SEEN];
extern unsigned g_pres_sector_misses, g_pres_face_misses;
/* the sectors copied since the environment draw began (the first
 * PRES_SECTORS_MAX), for the check */
enum { PRES_SECTORS_MAX = 64 };
typedef struct {
  u32 sec, copy, len;
} RenderCtxSector;
extern RenderCtxSector g_pres_sectors[PRES_SECTORS_MAX];
extern int g_pres_nsectors;
void render_ctx_want_area(void);
uintptr_t render_ctx_area(void);

/* ---- the host's check (render_ctx_check.c) ---- */
/* --render-ctx-check: each renderer call of the game's draw also runs the
 * presenter build from the same inputs, in its own context, with the
 * game's memory read-only; the two must write the same primitives,
 * records, OT links and side outputs and leave the same register file. */
extern int g_render_ctx_check;
u32 render_ctx_check(const char *name, RenderCtxFn logic, RenderCtxFn pres, u32 a0, u32 a1, u32 a2,
                     u32 a3);
void render_ctx_check_set_log(void (*log)(const char *fmt, ...));
/* the counts so far, by renderer; 0 when nothing differed */
int render_ctx_check_report(void);

#endif

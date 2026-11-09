/* The presenter build's names (stage 7d, decisions D123; render_ctx.h).
 *
 * Included first, before any header, by each NAME_hand_pres.c and by the
 * presenter's copies of the GTE (render_ctx_gte.c): every piece of state a
 * renderer writes, and every function it calls that writes state, is
 * renamed here onto the presenter's own (render_ctx.c), so the same source
 * compiled a second time touches nothing the game's own draw uses. The
 * logic's build doesn't include this file and doesn't change.
 *
 * What a renderer reaches and where it goes:
 *  - the GTE's control registers: the presenter's own (g_pres_gte; since
 *    7d2, D124, the presenter build is fast_render=3's code, which uses no
 *    data register or GTE command, so its GTERegisters' data half is only a
 *    view the shared headers compile against, and render_ctx_check makes
 *    sure nothing touches it);
 *  - the renderers' working memory (render_hand.h SCRATCH_BASE: at
 *    fast_render=3 g_render_work, no longer the PS1 scratchpad);
 *  - the work area D_8006FCF4 (the moby tables and records, the sector and
 *    polygon lists, the moby shadow list), the sector flags D_800771C8 and
 *    g_MobyShadows (the shadow list's texture and next entry);
 *  - the primitive buffer's pointer, end and full flag (D_800757B0,
 *    D_80075780, D_800758B0) and the two OT pointers (g_WorldOT, g_HudOT):
 *    the presenter's buffer and OTs, wherever render_ctx_begin puts them;
 *  - native_draw's record cursor, side table and fallback count, and the
 *    calls that set the cursor on and off (nd_begin, nd_end);
 *  - the vertex cache's two hooks (null unless the presenter sets them);
 *  - the HUD's 16:9 narrowing switch (g_present_narrow);
 *  - LOD's switches and its renderer hooks (lod.c), on the presenter's own
 *    LOD state (render_ctx.c).
 * Two writes went to shared data and have presenter code of their own
 * instead (RENDER_PRES in the files): the moby renderers' face
 * replacements (moby_hand.c ReplaceFaces) and the environment animations
 * applied into a visible sector's geometry (env_hand.c SectorAnimate), with
 * the four animation bytes of each sector header (SEC_CACHE).
 *
 * tests/render_ctx_link.sh checks the objects: an undefined symbol of a
 * presenter object must be one of the read-only ones it lists. */
#ifndef RENDER_CTX_NAMES_H
#define RENDER_CTX_NAMES_H

#define RENDER_PRES 1
/* 7d2 (D124): the presenter builds are fast_render=3's readable code; its
 * context has the GTE's control registers only, and no scratchpad copy,
 * spill area or hand stack (7e keys on this) */
#define RENDER_CTX_READABLE 1

/* the GTE: the control registers (render_hand.h bans the rest in the
 * presenter's build; the names below only let the shared headers' unused
 * inline code compile) */
#define gteRegs g_pres_gte
#define M2C_GTE_SLOW(f) pres_##f
#define m2c_gte_0x138041C pres_gte_0x138041C
#define m2c_gte_0x170000C pres_gte_0x170000C
#define m2c_gte_0x180001 pres_gte_0x180001
#define m2c_gte_0x190003D pres_gte_0x190003D
#define m2c_gte_0x198003D pres_gte_0x198003D
#define m2c_gte_0x1A0003E pres_gte_0x1A0003E
#define m2c_gte_0x1A8003E pres_gte_0x1A8003E
#define m2c_gte_0x280030 pres_gte_0x280030
#define m2c_gte_0x41E012 pres_gte_0x41E012
#define m2c_gte_0x43E012 pres_gte_0x43E012
#define m2c_gte_0x45E012 pres_gte_0x45E012
#define m2c_gte_0x480012 pres_gte_0x480012
#define m2c_gte_0x486012 pres_gte_0x486012
#define m2c_gte_0x49E012 pres_gte_0x49E012
#define m2c_gte_0x780010 pres_gte_0x780010
#define m2c_gte_0x980011 pres_gte_0x980011
#define m2c_gte_0xA00428 pres_gte_0xA00428
/* the moves mips2c_rt.h's inline code names (M2C_GTE_SLOW); no presenter
 * code calls them, so none is defined */
unsigned int pres_MFC2(int reg);
void pres_MTC2(unsigned int value, int reg);
void pres_CTC2(unsigned int value, int reg);
unsigned int pres_CFC2(int reg);
#define g_psyxRtpHook g_pres_rtp_hook
#define g_psyxRtpValueHook g_pres_rtp_value_hook
#define ngte_vcache pres_ngte_vcache

/* the working memory (fast_render=3's, render_hand.h) */
#define g_render_work g_pres_scratch

/* the work area and the sector flags */
#define D_8006FCF4 g_pres_work
#define D_800771C8 g_pres_seen
/* the moby shadow list's texture and next entry (MobyShadow, 12 bytes),
 * which the lit mobys advance and the shadows read */
#define g_MobyShadows g_pres_moby_shadows

/* game state the draw advances or leaves for later renderers: the particle
 * list's pointer (a byte of each particle is cleared), the glows and the
 * sparkles (aged by g_DeltaTime), Spyro's flame (Spyro's matrix for the
 * flame renderer). 7e's snapshot owns them; here they are the presenter's
 * copies. */
#define g_Particles g_pres_particles
#define g_Glows g_pres_glows
#define g_Sparkles g_pres_sparkles
#define g_SpyroFlame g_pres_spyro_flame

/* the primitive buffer and the OTs */
#define D_800757B0 g_pres_prim_next
#define D_80075780 g_pres_prim_end
#define D_800758B0 g_pres_prim_full
#define g_WorldOT g_pres_world_ot
#define g_HudOT g_pres_hud_ot

/* native_draw's records */
#define g_nd g_pres_nd
#define g_nd_side g_pres_nd_side
#define g_nd_fallback g_pres_nd_fallback
#define nd_begin pres_nd_begin
#define nd_end pres_nd_end

/* the HUD's narrowing */
#define g_present_narrow g_pres_narrow

/* LOD */
#define g_lod_k g_pres_lod_k
#define g_lod_pass g_pres_lod_pass
#define g_lod_stats g_pres_lod_stats
#define port_lod_env pres_lod_env
#define port_lod_moby_detail pres_lod_moby_detail
#define port_lod_moby_box pres_lod_moby_box
#define port_lod_moby_skip pres_lod_moby_skip
#define port_lod_moby_drawn pres_lod_moby_drawn
#define port_lod_moby_pass2 pres_lod_moby_pass2
#define port_lod_records_full pres_lod_records_full
#define port_lod_lit_full pres_lod_lit_full
#define port_lod_env_full pres_lod_env_full
#define port_lod_env_end pres_lod_env_end
#define port_lod_near_full pres_lod_near_full
#define port_lod_env_lists pres_lod_env_lists

/* stage 7e (D125): the game state the renderers read that the presenter
 * has its own of, the snapshot's copies (src/game/pres/pres_snap_names.h:
 * the camera and Spyro blended, the rest as the game had it after the
 * update) */
#include "pres/pres_snap_names.h"

#endif

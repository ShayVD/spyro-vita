/* The presenter build's names (stage 7e, decisions D124; cmake/presenter.cmake).
 *
 * Included first (-include) by every file of the draw path built again for
 * the presenter: the decomp's gamestates/draw.c, camera.c and
 * environment.c, and our order_table.c and the hand C they call. No decomp
 * file is edited; the matching build doesn't see this.
 *
 * What it maps:
 *  - the renderers' state (render_ctx_names.h, 7d's) and the snapshot's
 *    labels (pres_snap_names.h): the presenter reads and writes only its
 *    own copies;
 *  - the entry, GamestateDraw, onto GamestateDraw_pres, the one name the
 *    presenter's object keeps global (the rest of its functions are made
 *    local, so a call between them stays in the presenter's copies);
 *  - each renderer onto its 7d build (NAME_pres);
 *  - D30's hooks: VSync doesn't wait (pres_VSync), and LoadImage,
 *    StoreImage, MoveImage and ClearImage are dropped (pres_*Image), as
 *    D30's freeze drops them;
 *  - present.c's hooks the draw calls, onto the presenter's own: the
 *    primitive space (PortPolySpace), the 16:9 projection
 *    (PortProjectionHook), the HUD audit (port_present_hud_list), and
 *    native_draw's list end (nd_list_linked).
 * What stays the game's, as the presenter's output on the game thread (7f
 * routes them): DrawOTag, PutDrawEnv, PutDispEnv, DrawSync, and
 * port_present_note_2d (the 2D primitives for the 16:9 narrowing). */
#ifndef PRES_NAMES_H
#define PRES_NAMES_H

#include "render_ctx_names.h"
#include "pres_snap_names.h"

#define PRES_DRAW 1

/* Once 7d2 is in (render_ctx.h, render_ctx_names.h say RENDER_CTX_READABLE),
 * the renderers' presenter builds are fast_render=3's readable code, with no
 * GTE data registers, scratchpad, spill area or hand stack of their own: the
 * culls and the sky's clipping are built on that code too (moby_cull.c,
 * sky_clip.c: their entries NAME_pres), and libgte's matrix product sets
 * only the rotation it leaves (LIBGTE_CONTROL_ONLY). Before, the
 * register-file code (fast_render=2's) on 7d's context. */
#ifndef RENDER_CTX_READABLE
#define RENDER_CTX_READABLE 0
#endif
#if RENDER_CTX_READABLE
#define LIBGTE_CONTROL_ONLY 1
#define func_8001F158 func_8001F158_pres
#define func_800208FC func_800208FC_pres
#define func_8004F000 func_8004F000_pres
#define func_8004F4BC func_8004F4BC_pres
#define func_80050240 func_80050240_pres
#define func_8004FEA0 func_8004FEA0_pres
#endif

/* the entry */
#define GamestateDraw GamestateDraw_pres

/* the moby array's pointer, rebased onto the snapshot's copy (lod_pres.c
 * calls the same copy g_pres_level_mobys; pres_snap.c sets both) */
#define g_LevelMobys g_pres_snap_g_LevelMobys

/* the work area by its decomp name (render_ctx_names.h maps D_8006FCF4) */
#define g_SonyImage g_pres_work

/* the renderers: 7d's presenter builds (render_ctx.h) */
#define func_800258F0 func_800258F0_pres
#define func_8001F798 func_8001F798_pres
#define func_80022A2C func_80022A2C_pres
#define func_80020F34 func_80020F34_pres
#define func_8004EBA8 func_8004EBA8_pres
#define func_80023AC4 func_80023AC4_pres
#define func_80059A48 func_80059A48_pres
#define func_80059F8C func_80059F8C_pres
#define func_80058D64 func_80058D64_pres
#define func_800573C8 func_800573C8_pres
#define func_80058BA8 func_80058BA8_pres
#define func_80058864 func_80058864_pres

/* D30's hooks */
#define VSync pres_VSync
#define LoadImage pres_LoadImage
#define StoreImage pres_StoreImage
#define MoveImage pres_MoveImage
#define ClearImage pres_ClearImage

/* present.c's and native_draw.c's, the presenter's own (pres_draw.c) */
#define PortPolySpace pres_PortPolySpace
#define PortProjectionHook pres_PortProjectionHook
#define port_present_hud_list pres_hud_list
#define nd_list_linked pres_nd_list_linked

#endif

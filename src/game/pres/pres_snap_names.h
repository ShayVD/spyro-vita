/* The snapshot's labels (stage 7e, decisions D125; pres_snap.h).
 *
 * Each game label the presenter's draw reads or writes and the game
 * changes during play is renamed here onto the presenter's copy,
 * g_pres_snap_LABEL. tools/pres_snap_syms.py puts each copy at its label's
 * own offset in a mirror of the data region (g_pres_mirror), so the copies
 * keep the labels' layout. pres_snap.c fills them from the game's after
 * the update (pres_snap_take), then blends and rebases them.
 *
 * Included by pres_names.h (the draw path built again, src/game/pres/) and
 * by render_ctx_names.h (the renderers' presenter builds, 7d), so both
 * read the same copies.
 *
 * What is not here: the game's constant tables, the level's data and the
 * pointers set when a level loads (tests/pres/link_check.py lists those it
 * allows); the renderers' own state (render_ctx_names.h). */
#ifndef PRES_SNAP_NAMES_H
#define PRES_SNAP_NAMES_H

/* blended between the two ticks (pres_snap.c, as present.c blend_state) */
#define g_Camera g_pres_snap_g_Camera
#define g_Spyro g_pres_snap_g_Spyro

/* pointers the snapshot rebases onto the presenter's copies (pres_snap.c):
 * the DBs' primitive buffers and OTs, and the DB drawn (the moby array's,
 * g_LevelMobys, is pres_names.h's: lod_pres.c has its own name for it) */
#define g_DB g_pres_snap_g_DB
#define g_CurDB g_pres_snap_g_CurDB

/* written by the draw (GamestateDraw, PortPolySpace): the HUD's mobys and
 * the sky's limit in the primitive buffer */
#define g_HudMobys g_pres_snap_g_HudMobys
#define D_800756FC g_pres_snap_D_800756FC
/* the pacer's vblanks (draw.c's VSync loop; pres_VSync's) */
#define D_80075950 g_pres_snap_D_80075950
/* the HUD: its mobys, which the draw sets out and the lit list points into */
#define g_Hud g_pres_snap_g_Hud

/* written by the sky's set-up (cyclorama_hand.c, sky_clip_hand.c): the
 * portals' sky angles, which the draw turns, the portal's depth, screen
 * box, outline and points, the fog's distance and colour, and the sky
 * renderers' list */
#define D_80075858 g_pres_snap_D_80075858
#define D_800758FC g_pres_snap_D_800758FC
#define D_80075934 g_pres_snap_D_80075934
#define D_8007AA00 g_pres_snap_D_8007AA00
#define D_80077EA0 g_pres_snap_D_80077EA0
#define D_80078DD8 g_pres_snap_D_80078DD8
#define D_8007575C g_pres_snap_D_8007575C
#define D_800757D4 g_pres_snap_D_800757D4
#define D_80075798 g_pres_snap_D_80075798

/* read: the game's state the update changes (7a's read set, D104) */
#define g_Gamestate g_pres_snap_g_Gamestate
#define g_DemoMode g_pres_snap_g_DemoMode
#define D_80075784 g_pres_snap_D_80075784 /* an attract demo's extra vblank */
#define g_DeltaTime g_pres_snap_g_DeltaTime
#define g_LevelTicks g_pres_snap_g_LevelTicks
#define g_Fade g_pres_snap_g_Fade
#define g_ScreenBorderEnabled g_pres_snap_g_ScreenBorderEnabled
#define D_800756C0 g_pres_snap_D_800756C0 /* the border's other switch */
#define g_IsFlightLevel g_pres_snap_g_IsFlightLevel
#define g_IsSpyroHidden g_pres_snap_g_IsSpyroHidden
#define g_Cyclorama g_pres_snap_g_Cyclorama
#define g_PortalCount g_pres_snap_g_PortalCount
#define g_Portals g_pres_snap_g_Portals
#define D_800770C8 g_pres_snap_D_800770C8 /* the light's colours */
#define g_TracerCount g_pres_snap_g_TracerCount
#define g_TracerLists g_pres_snap_g_TracerLists
#define g_TracerPointCount g_pres_snap_g_TracerPointCount
/* read by GamestateDraw's other branches only (7g's), copied all the same */
#define g_TitlescreenState g_pres_snap_g_TitlescreenState
#define g_CreditsStage g_pres_snap_g_CreditsStage

#endif

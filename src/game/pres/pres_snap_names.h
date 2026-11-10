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
/* read by GamestateDraw's other branches (stage 7g, D128) */
#define g_TitlescreenState g_pres_snap_g_TitlescreenState
#define g_CreditsStage g_pres_snap_g_CreditsStage

/* Stage 7g (D128): what the other gamestates' draws read or write
 * (tools/pres_inventory.py --all-gamestates), the overlays' draws
 * included: the title, the flight results (Flight5_LEVEL_*), the credits.
 * Each is copied as it is after the update, so the presenter starts from
 * D30's pass's state and its writes stay its own. */
/* the level's environment (the credits set its culling distance) and the
 * buffers' pointers (the flight results, the pause screens and the exits
 * draw from the lower buffer up and put the HUD's mobys under the HUD OT:
 * rebased onto the presenter's buffers, pres_snap.c) */
#define g_Environment g_pres_snap_g_Environment
#define g_Buffers g_pres_snap_g_Buffers
/* the sounds the flight results play (spu.c PlaySound on the copies; the
 * SPU's writes are dropped, as D30's freeze dropped them) and libc's rand
 * seed PlaySound draws from */
#define g_Spu g_pres_snap_g_Spu
#define D_80075AC0 g_pres_snap_D_80075AC0
/* the pause, inventory and flight-results screens */
#define D_8007568C g_pres_snap_D_8007568C
#define D_8007569C g_pres_snap_D_8007569C
#define D_80075720 g_pres_snap_D_80075720
#define D_80075744 g_pres_snap_D_80075744
#define D_80075748 g_pres_snap_D_80075748
#define D_80075754 g_pres_snap_D_80075754
#define D_800757C8 g_pres_snap_D_800757C8
#define D_800757CC g_pres_snap_D_800757CC
#define D_800758B8 g_pres_snap_D_800758B8
#define D_800758F4 g_pres_snap_D_800758F4
#define D_80075900 g_pres_snap_D_80075900
#define D_80075908 g_pres_snap_D_80075908
#define D_80075914 g_pres_snap_D_80075914
#define g_ActAvailable g_pres_snap_g_ActAvailable
#define g_ActEnabled g_pres_snap_g_ActEnabled
#define g_DragonTotal g_pres_snap_g_DragonTotal
#define g_EggTotal g_pres_snap_g_EggTotal
#define g_GemTotal g_pres_snap_g_GemTotal
#define g_Homeworld g_pres_snap_g_Homeworld
#define g_LevelIndex g_pres_snap_g_LevelIndex
#define g_LevelDragonCount g_pres_snap_g_LevelDragonCount
#define g_LevelEggCount g_pres_snap_g_LevelEggCount
#define g_LevelGemCount g_pres_snap_g_LevelGemCount
#define g_FlightCollected g_pres_snap_g_FlightCollected
#define g_FlightCourseRecords g_pres_snap_g_FlightCourseRecords
#define g_FlightObjectiveCounters g_pres_snap_g_FlightObjectiveCounters
#define D_800758D0 g_pres_snap_D_800758D0
/* the level transitions and entrances */
#define D_8007587C g_pres_snap_D_8007587C
#define D_80075910 g_pres_snap_D_80075910
#define g_LevelTransChestDuration g_pres_snap_g_LevelTransChestDuration
#define g_LevelTransGems g_pres_snap_g_LevelTransGems
#define g_LevelTransHudActive g_pres_snap_g_LevelTransHudActive
#define g_LevelTransTicks g_pres_snap_g_LevelTransTicks
#define g_NGemsSinceLevelEntry g_pres_snap_g_NGemsSinceLevelEntry
#define g_NextLevelId g_pres_snap_g_NextLevelId
#define g_PreviousLevelIndex g_pres_snap_g_PreviousLevelIndex
#define g_RecentGemsCollected g_pres_snap_g_RecentGemsCollected
/* the respawn and the game over */
#define D_80075940 g_pres_snap_D_80075940
#define g_GameOverLetterDisplayTicks g_pres_snap_g_GameOverLetterDisplayTicks
#define g_GameOverRotY g_pres_snap_g_GameOverRotY
#define g_GameOverRotZ g_pres_snap_g_GameOverRotZ
#define g_GameOverSkyboxRotY g_pres_snap_g_GameOverSkyboxRotY
#define g_GameOverSkyboxRotZ g_pres_snap_g_GameOverSkyboxRotZ
#define g_GameOverTextRot g_pres_snap_g_GameOverTextRot
#define g_GameOverTicks g_pres_snap_g_GameOverTicks
#define g_LoadStage g_pres_snap_g_LoadStage
/* the dragons, the fairy, the balloonist */
#define D_80076248 g_pres_snap_D_80076248
#define D_800777E8 g_pres_snap_D_800777E8
#define g_DragonCutscene g_pres_snap_g_DragonCutscene
#define g_FairyCutscene g_pres_snap_g_FairyCutscene
/* the title, its attract demos' text, and the cutscenes */
#define g_CutsceneLayout g_pres_snap_g_CutsceneLayout
#define g_VisitedFlags g_pres_snap_g_VisitedFlags
/* the credits: the letters (D_8007589C, D_800756E4 in use) and the
 * roll's timer */
#define D_8007589C g_pres_snap_D_8007589C
#define D_800756E4 g_pres_snap_D_800756E4
#define g_CreditsTimer g_pres_snap_g_CreditsTimer
/* tables the game sets once (copied all the same: they sit among the
 * labels the update writes). The decomp's strings.c tables (the dragons',
 * levels', homeworlds' and the balloonists' names and lines) are C data in
 * the port, not in the data region, and constant: the presenter reads the
 * game's (tests/pres/link_check.py) */
#define g_FairyDialogueBoxSizes g_pres_snap_g_FairyDialogueBoxSizes
#define g_GameOverTextXY g_pres_snap_g_GameOverTextXY
#define g_GameOverTextZ g_pres_snap_g_GameOverTextZ
#define g_GrayscalePalette g_pres_snap_g_GrayscalePalette
#define g_TargetDragonCounts g_pres_snap_g_TargetDragonCounts
#define g_TargetEggCounts g_pres_snap_g_TargetEggCounts
#define g_TargetGemCounts g_pres_snap_g_TargetGemCounts
#define g_TitlescreenAnimIn g_pres_snap_g_TitlescreenAnimIn
#define g_TitlescreenAnimOut g_pres_snap_g_TitlescreenAnimOut
#define g_TitlescreenSelColors g_pres_snap_g_TitlescreenSelColors
#define g_TitlescreenSprites g_pres_snap_g_TitlescreenSprites

#endif

/* Compat: the options panel moved to core/include/encore/menu_panel.h (roadmap E4). This header keeps the old
 * include path and the old vpk_ names for games that build targets/vita/runtime/modmenu; new code includes
 * "encore/menu_panel.h" and calls the enc_ names. The first path is Encore's own layout (and a game's
 * third_party/encore), the second a game vendored before targets/ existed
 * (third_party/vita-port-kit/vita/runtime/modmenu). */
#ifndef VPK_MENU_PANEL_H
#define VPK_MENU_PANEL_H

#if __has_include("../../../../core/include/encore/menu_panel.h")
#include "../../../../core/include/encore/menu_panel.h"
#elif __has_include("../../../core/include/encore/menu_panel.h")
#include "../../../core/include/encore/menu_panel.h"
#else
#error "Encore's core/include/encore/menu_panel.h is not vendored beside this file: vendor the paths in NEEDS (tools/vendor.sh does)"
#endif

#define VpkMenuRow             EncMenuRow
#define VPK_MENU_PANEL_W       ENC_MENU_PANEL_W
#define VPK_MENU_PANEL_LINES   ENC_MENU_PANEL_LINES
#define vpk_menu_panel_height  enc_menu_panel_height
#define vpk_menu_panel_top     enc_menu_panel_top
#define vpk_menu_panel_draw    enc_menu_panel_draw
#define vpk_menu_panel_message enc_menu_panel_message

#endif /* VPK_MENU_PANEL_H */

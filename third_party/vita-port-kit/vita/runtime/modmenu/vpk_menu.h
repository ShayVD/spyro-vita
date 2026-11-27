/* Compat: the options table and the config.txt rewrite moved to core/include/encore/menu.h (roadmap E4). This header
 * keeps the old include path and the old vpk_ names for games that build targets/vita/runtime/modmenu; new code
 * includes "encore/menu.h" and calls the enc_ names. The first path is Encore's own layout (and a game's
 * third_party/encore), the second a game vendored before targets/ existed
 * (third_party/vita-port-kit/vita/runtime/modmenu). */
#ifndef VPK_MENU_H
#define VPK_MENU_H

#if __has_include("../../../../core/include/encore/menu.h")
#include "../../../../core/include/encore/menu.h"
#elif __has_include("../../../core/include/encore/menu.h")
#include "../../../core/include/encore/menu.h"
#else
#error "Encore's core/include/encore/menu.h is not vendored beside this file: vendor the paths in NEEDS (tools/vendor.sh does)"
#endif
#include "vpk_menu_panel.h"

#define VpkMenuOption       EncMenuOption
#define VpkMenuList         EncMenuList
#define VPK_MENU_LIST       ENC_MENU_LIST
#define vpk_menu_off_on     enc_menu_off_on
#define vpk_menu_opt_text   enc_menu_opt_text
#define vpk_menu_opt_change enc_menu_opt_change
#define vpk_menu_layout     enc_menu_layout
#define VpkMenuRow          EncMenuRow
#define vpk_menu_load       enc_menu_load
#define vpk_menu_save       enc_menu_save
#define vpk_config_rewrite  enc_config_rewrite

#endif /* VPK_MENU_H */

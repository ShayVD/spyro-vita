/* Compat: the options panel moved to core/src/enc_menu_panel.c (roadmap E4). Games that compile
 * targets/vita/runtime/modmenu/vpk_menu_panel.c keep doing so: this builds core's file (the two paths as in
 * vpk_menu_panel.h). */
#if __has_include("../../../../core/src/enc_menu_panel.c")
#include "../../../../core/src/enc_menu_panel.c"
#elif __has_include("../../../core/src/enc_menu_panel.c")
#include "../../../core/src/enc_menu_panel.c"
#else
#error "Encore's core/src/enc_menu_panel.c is not vendored beside this file: vendor the paths in NEEDS (tools/vendor.sh does)"
#endif

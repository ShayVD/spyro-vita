/* Compat: the options table and the config.txt rewrite moved to core/src/enc_menu.c (roadmap E4). Games that compile
 * targets/vita/runtime/modmenu/vpk_menu.c keep doing so: this builds core's file (the two paths as in vpk_menu.h). */
#if __has_include("../../../../core/src/enc_menu.c")
#include "../../../../core/src/enc_menu.c"
#elif __has_include("../../../core/src/enc_menu.c")
#include "../../../core/src/enc_menu.c"
#else
#error "Encore's core/src/enc_menu.c is not vendored beside this file: vendor the paths in NEEDS (tools/vendor.sh does)"
#endif

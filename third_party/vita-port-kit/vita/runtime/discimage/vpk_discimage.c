/* Compat: the disc image layer moved to core/src/enc_discimage.c (roadmap E4). Games that compile
 * targets/vita/runtime/discimage/vpk_discimage.c keep doing so: this builds core's file (the two paths as in
 * vpk_discimage.h). */
#if __has_include("../../../../core/src/enc_discimage.c")
#include "../../../../core/src/enc_discimage.c"
#elif __has_include("../../../core/src/enc_discimage.c")
#include "../../../core/src/enc_discimage.c"
#else
#error "Encore's core/src/enc_discimage.c is not vendored beside this file: vendor the paths in NEEDS (tools/vendor.sh does)"
#endif

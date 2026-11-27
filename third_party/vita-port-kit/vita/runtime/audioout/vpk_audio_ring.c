/* Compat: the sound ring moved to core/src/enc_audio_ring.c (roadmap E4). Games that compile
 * targets/vita/runtime/audioout/vpk_audio_ring.c keep doing so: this builds core's file (the two paths as in
 * vpk_audio_ring.h). */
#if __has_include("../../../../core/src/enc_audio_ring.c")
#include "../../../../core/src/enc_audio_ring.c"
#elif __has_include("../../../core/src/enc_audio_ring.c")
#include "../../../core/src/enc_audio_ring.c"
#else
#error "Encore's core/src/enc_audio_ring.c is not vendored beside this file: vendor the paths in NEEDS (tools/vendor.sh does)"
#endif

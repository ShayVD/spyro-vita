/* Compat: the sound ring moved to core/include/encore/audio_ring.h (roadmap E4). This header keeps the old include
 * path and the old vpk_ names for games that build targets/vita/runtime/audioout; new code includes
 * "encore/audio_ring.h" and calls the enc_ names. The first path is Encore's own layout (and a game's
 * third_party/encore), the second a game vendored before targets/ existed
 * (third_party/vita-port-kit/vita/runtime/audioout). */
#ifndef VPK_AUDIO_RING_H
#define VPK_AUDIO_RING_H

#if __has_include("../../../../core/include/encore/audio_ring.h")
#include "../../../../core/include/encore/audio_ring.h"
#elif __has_include("../../../core/include/encore/audio_ring.h")
#include "../../../core/include/encore/audio_ring.h"
#else
#error "Encore's core/include/encore/audio_ring.h is not vendored beside this file: vendor the paths in NEEDS (tools/vendor.sh does)"
#endif

#define VPK_AUDIO_RING_MAX_FRAMES ENC_AUDIO_RING_MAX_FRAMES
#define VPK_AUDIO_RING_NO_CUT     ENC_AUDIO_RING_NO_CUT
#define VpkAudioRingConfig        EncAudioRingConfig
#define VpkAudioRing              EncAudioRing
#define vpk_audio_ring_init       enc_audio_ring_init
#define vpk_audio_ring_push       enc_audio_ring_push
#define vpk_audio_ring_level      enc_audio_ring_level
#define vpk_audio_ring_clear      enc_audio_ring_clear
#define vpk_audio_ring_pull       enc_audio_ring_pull
#define vpk_audio_ring_ppm        enc_audio_ring_ppm

#endif /* VPK_AUDIO_RING_H */

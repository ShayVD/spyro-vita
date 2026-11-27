/* Compat: the disc image layer moved to core/include/encore/discimage.h (roadmap E4). This header keeps the old
 * include path and the old vpk_ names for games that build targets/vita/runtime/discimage; new code includes
 * "encore/discimage.h" and calls the enc_ names. The first path is Encore's own layout (and a game's
 * third_party/encore), the second a game vendored before targets/ existed
 * (third_party/vita-port-kit/vita/runtime/discimage). */
#ifndef VPK_DISCIMAGE_H
#define VPK_DISCIMAGE_H

#if __has_include("../../../../core/include/encore/discimage.h")
#include "../../../../core/include/encore/discimage.h"
#elif __has_include("../../../core/include/encore/discimage.h")
#include "../../../core/include/encore/discimage.h"
#else
#error "Encore's core/include/encore/discimage.h is not vendored beside this file: vendor the paths in NEEDS (tools/vendor.sh does)"
#endif

#define VPK_DISC_RAW           ENC_DIMG_RAW
#define VPK_DISC_ISO           ENC_DIMG_ISO
#define VPK_DISC_CHD           ENC_DIMG_CHD
#define VPK_DISC_PBP           ENC_DIMG_PBP
#define VPK_DISC_ECM           ENC_DIMG_ECM
#define VPK_DISC_UNKNOWN       ENC_DIMG_UNKNOWN
#define VPK_DISC_UNREADABLE    ENC_DIMG_UNREADABLE
#define VpkDiscLayout          EncDimgLayout
#define VpkDisc                EncDimg
#define vpk_disc_open_source   enc_dimg_open_source
#define vpk_disc_sniff         enc_dimg_sniff
#define vpk_disc_open          enc_dimg_open
#define vpk_disc_open_any      enc_dimg_open_any
#define vpk_disc_is_open       enc_dimg_is_open
#define vpk_disc_close         enc_dimg_close
#define vpk_disc_read_raw      enc_dimg_read_raw
#define vpk_disc_read          enc_dimg_read
#define vpk_disc_find          enc_dimg_find
#define VpkDiscEntry           EncDimgEntry
#define vpk_disc_walk          enc_dimg_walk
#define VPK_CUE_AUDIO          ENC_CUE_AUDIO
#define VPK_CUE_CDG            ENC_CUE_CDG
#define VPK_CUE_MODE1_2048     ENC_CUE_MODE1_2048
#define VPK_CUE_MODE1_2352     ENC_CUE_MODE1_2352
#define VPK_CUE_MODE2_2336     ENC_CUE_MODE2_2336
#define VPK_CUE_MODE2_2352     ENC_CUE_MODE2_2352
#define VPK_CUE_MODE2_2448     ENC_CUE_MODE2_2448
#define VPK_CUE_OTHER          ENC_CUE_OTHER
#define VPK_CUE_TRACKS         ENC_CUE_TRACKS
#define VpkCueTrack            EncCueTrack
#define VpkCue                 EncCue
#define vpk_cue_parse          enc_cue_parse
#define vpk_cue_data_track     enc_cue_data_track
#define vpk_cue_file           enc_cue_file
#define VPK_IMG_OK             ENC_IMG_OK
#define VPK_IMG_NONE           ENC_IMG_NONE
#define VPK_IMG_KIND           ENC_IMG_KIND
#define VPK_IMG_CUE_UNREADABLE ENC_IMG_CUE_UNREADABLE
#define VPK_IMG_CUE_BAD        ENC_IMG_CUE_BAD
#define VPK_IMG_CUE_TRACK      ENC_IMG_CUE_TRACK
#define VPK_IMG_CUE_MISSING    ENC_IMG_CUE_MISSING
#define VpkDiscFound           EncDimgFound
#define VpkDiscSearch          EncDimgSearch
#define vpk_disc_find_image    enc_dimg_find_image
#define vpk_disc_boot_serial   enc_dimg_boot_serial
#define VpkSha1                EncSha1
#define vpk_sha1_init          enc_sha1_init
#define vpk_sha1_update        enc_sha1_update
#define vpk_sha1_hex           enc_sha1_hex

#endif /* VPK_DISCIMAGE_H */

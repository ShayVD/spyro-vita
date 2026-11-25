/* The PC backend's extra calls (targets/pc/encore/enc_pc.c): for a PC build's own menus and for its tests. Not part
 * of the platform interface: only PC builds have them. */
#ifndef ENC_PC_H
#define ENC_PC_H

#include <stdint.h>

#include "encore/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENC_PC_PADS 4 /* game controllers, by port in the order they connect; the keyboard is on port 0 too */

struct SDL_Window;
struct SDL_Window *enc_pc_window(void); /* the window while the video is open, else NULL */
uint64_t enc_pc_presents(void);         /* enc_video_present / enc_video_gpu_present calls so far */
/* CPU frames: the window's pixels as last presented, 0x00RRGGBB (alpha unspecified), w x h of the renderer's
 * output (enc_platform_info()->display_width / _height). */
int enc_pc_read_pixels(uint32_t *out, int w, int h);

#ifdef __cplusplus
}
#endif
#endif

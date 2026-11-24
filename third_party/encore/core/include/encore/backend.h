/* Helpers for writing a backend of encore/platform.h (not for games): the CPU picture path every target starts
 * with, and the event queue. Portable C in core/src. */
#ifndef ENCORE_BACKEND_H
#define ENCORE_BACKEND_H

#include "encore/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clears dst (dst_w x dst_h pixels, dst_stride pixels a row) to black and scales frame into box, nearest pixel,
 * converting to 32-bit pixels: 0x00RRGGBB when bgr is 0, 0x00BBGGRR (A8B8G8R8 displays: the Vita, GL's RGBA bytes)
 * when 1. ENC_ERR_ARG for a frame with a bad format or a stride shorter than a row. */
int enc_blit_scaled(const EncFrame *frame, uint32_t *dst, int dst_w, int dst_h, int dst_stride, EncRect box,
                    int bgr);

/* The event queue behind enc_event_poll and enc_event_set_handler (core/src/enc_event.c). A backend calls
 * enc_event_emit from the thread that runs enc_input_poll (gathering what its system callbacks flagged), and
 * enc_event_reset in enc_platform_init. time_us is stamped with enc_time_us(). */
void enc_event_emit(int type, int width, int height, uint64_t suspended_us);
void enc_event_reset(void);

#ifdef __cplusplus
}
#endif
#endif

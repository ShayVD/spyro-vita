/* The headless host backend's extra calls (core/host/enc_host.c): what a test or a scripted run uses to drive and
 * read a game with no screen, no pad and no speaker. Not part of the platform interface: only host builds have it. */
#ifndef ENC_HOST_H
#define ENC_HOST_H

#include <stdint.h>

#include "encore/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENC_HOST_PADS 4

void enc_host_set_pad(int port, const EncPad *pad);            /* what the next enc_input_poll reads */
void enc_host_set_touch(int panel, const EncTouch *pts, int n); /* n <= 10 */
void enc_host_request_quit(void);                               /* should_quit turns 1; QUIT at the next poll */
/* The display as last presented: 0x00RRGGBB pixels, width x height (ENC_HOST_DISPLAY, default 1280x720). */
const uint32_t *enc_host_display(int *width, int *height);
uint64_t enc_host_presents(void);                               /* enc_video_present calls so far */
uint64_t enc_host_audio_played(void);                           /* frames the fake speaker has consumed */
void enc_host_set_motion(int port, const EncMotion *m);         /* what the next poll samples (time_us is set) */
void enc_host_rumble(int port, int *low, int *high, int *duration_ms); /* the last enc_pad_rumble */
/* The open text input's result at its next poll: text (NULL keeps the current text) and ENC_TEXT_PENDING / _DONE /
 * _CANCELLED. Ignored when none is open. */
void enc_host_set_text(const char *text, int result);
/* A system suspend, as a target's backend sees one: the clock (enc_time_us, the vblank, the fake speaker) stands
 * still until enc_host_resume, which drops the queued audio; SUSPEND and RESUME (with the time) come at the next
 * polls. An enc_vblank_wait while suspended waits for the resume, as a frozen app would. */
void enc_host_suspend(void);
void enc_host_resume(void);
void enc_host_set_focus(int focused);                           /* FOCUS_LOST (0) / FOCUS_GAINED (1) at the next poll */

#ifdef __cplusplus
}
#endif
#endif

/* What Encore's platform interface (encore/platform.h, API 2) can't say yet,
 * D245. The PC layer runs on the interface and its PC backend
 * (third_party/encore/targets/pc/encore/enc_pc.c); these few calls are the
 * only ones left that reach SDL3 directly, all in gaps.c, each with the
 * smallest addition to the interface that would retire it. */
#ifndef SPYRO_PC_GAPS_H
#define SPYRO_PC_GAPS_H

#include <stddef.h>
#include <stdint.h>

/* G1 files: the user folder by the game's own vendor and app names (the
 * backend's is fixed at "encore/<game>"), set as the backend's root
 * (ENC_PC_ROOT) so enc_path(ENC_DIR_SAVE) is the save folder players have
 * now. An ENC_PC_ROOT already set wins. Ends in '/'. Before enc_platform_init. */
void pc_gap_user_dir(char *out, size_t cap);
/* G2 files: the folder the executable is in ("" when unknown), ends in '/'. */
const char *pc_gap_base_dir(void);
/* G3 window: its first size (window=WxH, through ENC_PC_WINDOW). Before init. */
void pc_gap_window_size(int w, int h);
/* G4 window: after enc_video_open: the title, a 320x240 minimum, fullscreen
 * at start (--fullscreen); and each frame, the mouse pointer hidden when
 * still for 2 s or in fullscreen (cursor_update). */
void pc_gap_window_setup(void *sdl_window, const char *title, int fullscreen);
void pc_gap_cursor_update(void *sdl_window);
/* G5 video: the video driver's name for the log ("x11", "windows"...). */
const char *pc_gap_video_driver(void);
/* G6 GL: a second context sharing the window's (the game thread's, D243),
 * made on the main thread, which keeps its own current; and making a context
 * current on the calling thread. */
void *pc_gap_gl_shared_context(void *sdl_window, void *main_context);
int pc_gap_gl_make_current(void *sdl_window, void *context);
/* G7 input: the keyboard as spyro's own map (input.c's table: WASD, K L J I,
 * Space...) in PS1 pad bits, and the ENC_BTN_ bits the backend's built-in
 * map (arrows, Z X A S, Q W, 1 3, Enter, Right Shift, Esc) put on pad 0 from
 * the keys held now, which input.c takes back out. */
uint16_t pc_gap_keyboard_psx(void);
uint32_t pc_gap_backend_keyboard_bits(void);
/* G8 a message box (no disc, no display) over the log line. */
void pc_gap_message_box(const char *title, const char *text);

#endif

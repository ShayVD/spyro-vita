/* What Encore's platform interface (encore/platform.h, API 2) can't say yet,
 * D247. The PC layer runs on the interface and its PC backend
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
/* G12 video: whether the window's GL swap really waits for the display (the
 * swap interval took), on the thread with the window's context current. */
int pc_gap_gl_swap_interval_on(void);
/* G7 input: the keyboard as spyro's own map (input.c's table: WASD, K L J I,
 * Space...) in PS1 pad bits, and the ENC_BTN_ bits the backend's built-in
 * map (arrows, Z X A S, Q W, 1 3, Enter, Right Shift, Esc) put on pad 0 from
 * the keys held now, which input.c takes back out. */
uint16_t pc_gap_keyboard_psx(void);
uint32_t pc_gap_backend_keyboard_bits(void);
/* G15 the options menu (D266): whether Esc or F1 is held (the backend's map
 * has Esc as ENC_BTN_MENU, but not F1); and, on the main thread, the
 * window's fullscreen as the menu sets it (as F11 does) and as it is now. */
int pc_gap_menu_key(void);
void pc_gap_set_fullscreen(void *sdl_window, int on);
int pc_gap_is_fullscreen(void *sdl_window);
/* G16 video: the swap interval changed on the window's context (the menu's
 * VSYNC row); returns whether it took. Main thread. */
int pc_gap_gl_set_swap_interval(int on);
/* G8 a message box (no disc, no display) over the log line. */
void pc_gap_message_box(const char *title, const char *text);

#endif

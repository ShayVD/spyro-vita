/* The port's own options panel (src/platform/common/menu_panel.c): drawn on
 * the CPU into an RGB image that the platform shows over the game. Plain C,
 * no platform calls, so the host can test it. */
#ifndef MENU_PANEL_H
#define MENU_PANEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A line: a row (label and value), or a section header (value NULL). */
typedef struct {
  const char *label;
  const char *value;
} MenuRow;

#define MENU_PANEL_W 640
#define MENU_PANEL_LINES 13 /* lines shown at most; more scroll (D45) */

/* Height of a panel showing `lines` lines (at most MENU_PANEL_LINES: 500
 * pixels, inside the Vita's 544). */
int menu_panel_height(int lines);

/* The first line to show so that line `sel` is in view, given the last
 * first line `top` (scrolls only as far as needed; a header just above
 * `sel` is brought in with it). */
int menu_panel_top(int n, int sel, int top, int lines);

/* Title, lines top..top+lines-1 of rows (row `sel` highlighted, its value
 * between < >; headers in the title's colour), arrows if more lines are
 * above or below, and a footer, into rgb (MENU_PANEL_W x
 * menu_panel_height(lines), 3 bytes a pixel). Text is upper case ASCII;
 * characters the font lacks are drawn as spaces. */
void menu_panel_draw(uint8_t *rgb, const char *title, const MenuRow *rows, int n,
                     int sel, int top, int lines, const char *footer);

#ifdef __cplusplus
}
#endif

#endif

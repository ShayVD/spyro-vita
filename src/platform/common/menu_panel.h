/* The port's own options panel (src/platform/common/menu_panel.c): drawn on
 * the CPU into an RGB image that the platform shows over the game. Plain C,
 * no platform calls, so the host can test it. */
#ifndef MENU_PANEL_H
#define MENU_PANEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *label;
  const char *value;
} MenuRow;

#define MENU_PANEL_W 640

/* Height of a panel with n rows. */
int menu_panel_height(int n);

/* Title, rows (row `sel` highlighted, its value between < >) and a footer,
 * into rgb (MENU_PANEL_W x menu_panel_height(n), 3 bytes a pixel). Text is
 * upper case ASCII; characters the font lacks are drawn as spaces. */
void menu_panel_draw(uint8_t *rgb, const char *title, const MenuRow *rows, int n,
                     int sel, const char *footer);

#ifdef __cplusplus
}
#endif

#endif

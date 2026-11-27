/* The port's own options panel, drawn on the CPU into an RGB image that the
 * platform shows over the game (decisions D45): Encore's
 * enc_menu_panel (third_party/encore/core, D110),
 * under the names this game's code uses. */
#ifndef MENU_PANEL_H
#define MENU_PANEL_H

#include "../../../third_party/encore/core/include/encore/menu_panel.h"

typedef EncMenuRow MenuRow;
#define MENU_PANEL_W ENC_MENU_PANEL_W
#define MENU_PANEL_LINES ENC_MENU_PANEL_LINES
#define menu_panel_height enc_menu_panel_height
#define menu_panel_top enc_menu_panel_top
#define menu_panel_draw enc_menu_panel_draw
#define menu_panel_message enc_menu_panel_message

#endif

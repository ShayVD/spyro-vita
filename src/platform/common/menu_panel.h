/* The port's own options panel, drawn on the CPU into an RGB image that the
 * platform shows over the game (decisions D45): vita-port-kit's
 * vpk_menu_panel (third_party/vita-port-kit/vita/runtime/modmenu, D110),
 * under the names this game's code uses. */
#ifndef MENU_PANEL_H
#define MENU_PANEL_H

#include "../../../third_party/vita-port-kit/vita/runtime/modmenu/vpk_menu_panel.h"

typedef VpkMenuRow MenuRow;
#define MENU_PANEL_W VPK_MENU_PANEL_W
#define MENU_PANEL_LINES VPK_MENU_PANEL_LINES
#define menu_panel_height vpk_menu_panel_height
#define menu_panel_top vpk_menu_panel_top
#define menu_panel_draw vpk_menu_panel_draw
#define menu_panel_message vpk_menu_panel_message

#endif

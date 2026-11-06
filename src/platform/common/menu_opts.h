/* The port's options as a table (decisions D45): each feature file lists
 * its own rows (a const MenuOption[] and a MenuOptionList), the platform's
 * menu shows them by section and saves them to config.txt. Plain C, no
 * platform calls, so the host can test it (tests/menu_panel).
 *
 * A row is one of:
 * - named values: `names[0..count-1]` for the values 0..count-1, or for
 *   `values[0..count-1]` when `values` is set (a value read from config.txt
 *   that isn't in the list is shown with `fmt` and steps to the nearest);
 * - a number: `min`..`max` by `step` (default 1), shown with `fmt`
 *   (default "%d"), clamped (not wrapped) at the ends;
 * - an action (`value` NULL): X or RIGHT calls `action`; it returns 1 to
 *   close the menu. `names[0]`, if set, is shown as its value.
 * `on_change` runs after a value changed in the menu (not on config load).
 * `visible`, if set, hides the row while it returns 0 (its key is still
 * loaded and saved). A value in config.txt may be a value's name (any case,
 * "ps1" for "PS1") or a number.
 * `key` NULL: not saved or loaded. Rows are designated initialisers, so new
 * fields can be added without touching other files.
 * The table is vita-port-kit's vpk_menu (third_party/vita-port-kit/vita/
 * runtime/modmenu, D110); this game's sections and names are here. */
#ifndef MENU_OPTS_H
#define MENU_OPTS_H

#include "menu_panel.h"
#include "../../../third_party/vita-port-kit/vita/runtime/modmenu/vpk_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  MENU_PICTURE,
  MENU_CONTROLS,
  MENU_GAME,
  MENU_SAVE,
  MENU_NSECTIONS
} MenuSection;

typedef VpkMenuOption MenuOption;
typedef VpkMenuList MenuOptionList;
/* A feature file's rows: MENU_OPTION_LIST(g_menu_controls, k_rows); */
#define MENU_OPTION_LIST VPK_MENU_LIST

extern const char *const g_menu_section_names[MENU_NSECTIONS];
#define g_menu_off_on vpk_menu_off_on /* names for a 0/1 switch: count 2 */

#define menu_opt_text vpk_menu_opt_text
#define menu_opt_change vpk_menu_opt_change
/* The menu's lines: a header per section that has rows, then its rows in
 * list order (vpk_menu_layout with this game's sections). */
#define menu_opts_layout(lists, nlists, rows, opts, max) \
  vpk_menu_layout((lists), (nlists), g_menu_section_names, MENU_NSECTIONS, (rows), (opts), (max))
#define menu_opts_load vpk_menu_load
#define menu_opts_save vpk_menu_save

#ifdef __cplusplus
}
#endif

#endif

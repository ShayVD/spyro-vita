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
 * fields can be added without touching other files. */
#ifndef MENU_OPTS_H
#define MENU_OPTS_H

#include "menu_panel.h"

#include <stddef.h>

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

typedef struct MenuOption {
  MenuSection section;
  const char *label;          /* upper case (the panel's font) */
  const char *key;            /* config.txt key; NULL: not saved */
  int *value;                 /* usually a g_vita_opts field; NULL: an action */
  const char *const *names;   /* value names, or NULL: a number */
  const int *values;          /* with names: the value each name stands for */
  int count;                  /* with names: how many */
  int min, max, step;         /* a number */
  const char *fmt;            /* a number (or a value not in `values`) */
  void (*on_change)(void);
  int (*action)(void);        /* an action row: 1 closes the menu */
  int (*visible)(void);       /* NULL: always shown */
} MenuOption;

typedef struct {
  const MenuOption *opts;
  int n;
} MenuOptionList;

/* A feature file's rows: MENU_OPTION_LIST(g_menu_controls, k_rows); */
#define MENU_OPTION_LIST(name, arr) \
  const MenuOptionList name = {arr, (int)(sizeof arr / sizeof arr[0])}

extern const char *const g_menu_section_names[MENU_NSECTIONS];
extern const char *const g_menu_off_on[2]; /* names for a 0/1 switch: count 2 */

/* The value as shown (buf: at least 32 bytes). */
const char *menu_opt_text(const MenuOption *o, char *buf, size_t len);

/* One step in direction dir (+1/-1). Returns 1 if the value changed (and
 * on_change ran), 2 if an action asked to close the menu, else 0. */
int menu_opt_change(const MenuOption *o, int dir);

/* The menu's lines: a header per section that has rows, then its rows in
 * list order. Returns the number of lines (at most max); opts[i] is the
 * row on line i, NULL for a header (rows[i].value NULL). */
int menu_opts_layout(const MenuOptionList *const *lists, int nlists, MenuRow *rows,
                     const MenuOption **opts, int max);

/* config.txt: `key=v` into the row with that key. Returns 1 if a row took it. */
int menu_opts_load(const MenuOptionList *const *lists, int nlists, const char *key,
                   const char *v);

/* config.txt rewritten: lines with a row's key (and the `drop` keys,
 * NULL-terminated) removed, the rest kept in order, then `head` (a line or
 * more, may be NULL) and every row's key=value. Returns 0, or -1 if the
 * file can't be written. */
int menu_opts_save(const MenuOptionList *const *lists, int nlists, const char *path,
                   const char *const *drop, const char *head);

#ifdef __cplusplus
}
#endif

#endif

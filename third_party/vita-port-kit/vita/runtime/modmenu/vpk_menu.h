/* A port's options as a table: each feature file lists its own rows (a
 * const VpkMenuOption[] and a VpkMenuList), the platform's menu shows them
 * by section and saves them to config.txt. Plain C, no platform calls, so a
 * host can test it (test/menu_test.c). Sections are the game's: a row's
 * `section` indexes the names the game passes to vpk_menu_layout.
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
 *
 * vpk_config_rewrite is the config.txt writer on its own, for a menu that
 * keeps its own rows: in place (a key's first line rewritten) or appended.
 *
 * From spyro-vita src/platform/common/menu_opts.h / menu_opts.c @ 987363e
 * (its decisions D45); the in-place rewrite from kh1-decomp
 * host/vita/menu.c's save() @ 85befa4a. */
#ifndef VPK_MENU_H
#define VPK_MENU_H

#include "vpk_menu_panel.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VpkMenuOption {
  int section;                /* an index into the game's section names */
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
} VpkMenuOption;

typedef struct {
  const VpkMenuOption *opts;
  int n;
  void (*on_open)(void); /* optional: each time the menu opens, before it is
                            drawn (rows whose value text depends on the game) */
} VpkMenuList;

/* A feature file's rows: VPK_MENU_LIST(g_menu_controls, k_rows); */
#define VPK_MENU_LIST(name, arr) \
  const VpkMenuList name = {arr, (int)(sizeof arr / sizeof arr[0]), NULL}

extern const char *const vpk_menu_off_on[2]; /* names for a 0/1 switch: count 2 */

/* The value as shown (buf: at least 32 bytes). */
const char *vpk_menu_opt_text(const VpkMenuOption *o, char *buf, size_t len);

/* One step in direction dir (+1/-1). Returns 1 if the value changed (and
 * on_change ran), 2 if an action asked to close the menu, else 0. */
int vpk_menu_opt_change(const VpkMenuOption *o, int dir);

/* The menu's lines: a header per section that has rows (the sections in
 * order, section_names[0..nsections-1]), then its rows in list order.
 * Returns the number of lines (at most max); opts[i] is the row on line i,
 * NULL for a header (rows[i].value NULL). */
int vpk_menu_layout(const VpkMenuList *const *lists, int nlists, const char *const *section_names,
                    int nsections, VpkMenuRow *rows, const VpkMenuOption **opts, int max);

/* config.txt: `key=v` into the row with that key. Returns 1 if a row took it. */
int vpk_menu_load(const VpkMenuList *const *lists, int nlists, const char *key,
                   const char *v);

/* config.txt rewritten: lines with a row's key (and the `drop` keys,
 * NULL-terminated) removed, the rest kept in order, then `head` (a line or
 * more, may be NULL) and every row's key=value. Returns 0, or -1 if the
 * file can't be written. */
int vpk_menu_save(const VpkMenuList *const *lists, int nlists, const char *path,
                   const char *const *drop, const char *head);

/* config.txt's text rewritten with the keys' new values (key[i]=value[i]),
 * into out (cap bytes; the result is cut short rather than overrun):
 *  - append (in_place 0): lines with one of the keys or a `drop` key
 *    (NULL-terminated, may be NULL) removed, the rest kept in order, then
 *    `head` (a line or more, may be NULL), then every key=value;
 *  - in place (in_place 1): each key's first line rewritten where it is,
 *    its later lines dropped, the keys it did not have added at the end in
 *    order. `alias` (pairs: other name, key; NULL-terminated, may be NULL)
 *    names a line's key by another name.
 * Comments (#) and lines without '=' are kept; a last line without '\n'
 * gets one. Returns the length written. */
size_t vpk_config_rewrite(const char *old, size_t old_len, const char *const *key,
                          const char *const *value, int nkeys, const char *const *drop,
                          const char *head, int in_place, const char *const *alias, char *out,
                          size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* VPK_MENU_H */

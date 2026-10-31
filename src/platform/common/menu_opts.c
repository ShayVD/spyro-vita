/* The port's options table (menu_opts.h, decisions D45): showing, stepping,
 * loading and saving rows, whoever defines them. */
#include "menu_opts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const g_menu_section_names[MENU_NSECTIONS] = {"PICTURE", "CONTROLS", "GAME",
                                                          "SAVE"};
const char *const g_menu_off_on[2] = {"OFF", "ON"};

/* names[] index of the value, or -1. */
static int name_index(const MenuOption *o) {
  int i;
  if (!o->values)
    return *o->value >= 0 && *o->value < o->count ? *o->value : -1;
  for (i = 0; i < o->count; i++)
    if (o->values[i] == *o->value)
      return i;
  return -1;
}

const char *menu_opt_text(const MenuOption *o, char *buf, size_t len) {
  int i;
  if (!o->value)
    return o->names ? o->names[0] : "";
  if (o->names && (i = name_index(o)) >= 0)
    return o->names[i];
  snprintf(buf, len, o->fmt ? o->fmt : "%d", *o->value);
  return buf;
}

int menu_opt_change(const MenuOption *o, int dir) {
  int old, i;
  if (!o->value)
    return o->action && dir > 0 && o->action() ? 2 : 0;
  old = *o->value;
  if (o->names && o->count > 0) {
    i = name_index(o);
    if (i < 0 && o->values) { /* not in the list (config.txt): the nearest */
      int best = 0, k;
      for (k = 1; k < o->count; k++)
        if (abs(o->values[k] - old) < abs(o->values[best] - old))
          best = k;
      i = best;
    } else if (i < 0) {
      i = 0;
    } else {
      i = (i + o->count + dir) % o->count;
    }
    *o->value = o->values ? o->values[i] : i;
  } else {
    int step = o->step ? o->step : 1, v = old + dir * step;
    *o->value = v < o->min ? o->min : v > o->max ? o->max : v;
  }
  if (*o->value == old)
    return 0;
  if (o->on_change)
    o->on_change();
  return 1;
}

int menu_opts_layout(const MenuOptionList *const *lists, int nlists, MenuRow *rows,
                     const MenuOption **opts, int max) {
  int s, l, i, n = 0;
  for (s = 0; s < MENU_NSECTIONS; s++) {
    int header = 0;
    for (l = 0; l < nlists; l++)
      for (i = 0; lists[l] && i < lists[l]->n; i++) {
        const MenuOption *o = &lists[l]->opts[i];
        if ((int)o->section != s || n + 2 > max)
          continue;
        if (!header) {
          rows[n].label = g_menu_section_names[s];
          rows[n].value = NULL;
          opts[n++] = NULL;
          header = 1;
        }
        rows[n].label = o->label;
        rows[n].value = "";
        opts[n++] = o;
      }
  }
  return n;
}

static const MenuOption *find(const MenuOptionList *const *lists, int nlists, const char *key,
                              size_t len) {
  int l, i;
  for (l = 0; l < nlists; l++)
    for (i = 0; lists[l] && i < lists[l]->n; i++) {
      const MenuOption *o = &lists[l]->opts[i];
      if (o->key && o->value && strlen(o->key) == len && !strncmp(o->key, key, len))
        return o;
    }
  return NULL;
}

int menu_opts_load(const MenuOptionList *const *lists, int nlists, const char *key,
                   const char *v) {
  const MenuOption *o = find(lists, nlists, key, strlen(key));
  if (!o)
    return 0;
  *o->value = atoi(v);
  return 1;
}

static int dropped(const MenuOptionList *const *lists, int nlists, const char *const *drop,
                   const char *line) {
  const char *eq = strchr(line, '=');
  size_t len;
  if (line[0] == '#' || !eq)
    return 0;
  len = (size_t)(eq - line);
  for (; drop && *drop; drop++)
    if (strlen(*drop) == len && !strncmp(*drop, line, len))
      return 1;
  return find(lists, nlists, line, len) != NULL;
}

int menu_opts_save(const MenuOptionList *const *lists, int nlists, const char *path,
                   const char *const *drop, const char *head) {
  char *keep = NULL, line[512];
  size_t n = 0, cap = 0, len;
  int l, i;
  FILE *f = fopen(path, "r");
  if (f) {
    while (fgets(line, sizeof line, f)) {
      if (dropped(lists, nlists, drop, line))
        continue;
      len = strlen(line);
      if (n + len + 1 > cap) {
        cap = (n + len + 1) * 2;
        keep = realloc(keep, cap);
      }
      memcpy(keep + n, line, len);
      n += len;
      if (len && line[len - 1] != '\n')
        keep[n++] = '\n';
    }
    fclose(f);
  }
  f = fopen(path, "w");
  if (!f) {
    free(keep);
    return -1;
  }
  if (n)
    fwrite(keep, 1, n, f);
  if (head)
    fputs(head, f);
  for (l = 0; l < nlists; l++)
    for (i = 0; lists[l] && i < lists[l]->n; i++) {
      const MenuOption *o = &lists[l]->opts[i];
      if (o->key && o->value)
        fprintf(f, "%s=%d\n", o->key, *o->value);
    }
  fclose(f);
  free(keep);
  return 0;
}

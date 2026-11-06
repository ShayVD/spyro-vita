/* A port's options table (vpk_menu.h): showing, stepping, loading and
 * saving rows, whoever defines them; and the config.txt rewrite.
 * From spyro-vita src/platform/common/menu_opts.c @ 987363e and kh1-decomp
 * host/vita/menu.c's save() @ 85befa4a. */
#include "vpk_menu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const vpk_menu_off_on[2] = {"OFF", "ON"};

/* names[] index of the value, or -1. */
static int name_index(const VpkMenuOption *o) {
  int i;
  if (!o->values)
    return *o->value >= 0 && *o->value < o->count ? *o->value : -1;
  for (i = 0; i < o->count; i++)
    if (o->values[i] == *o->value)
      return i;
  return -1;
}

const char *vpk_menu_opt_text(const VpkMenuOption *o, char *buf, size_t len) {
  int i;
  if (!o->value)
    return o->names ? o->names[0] : "";
  if (o->names && (i = name_index(o)) >= 0)
    return o->names[i];
  snprintf(buf, len, o->fmt ? o->fmt : "%d", *o->value);
  return buf;
}

int vpk_menu_opt_change(const VpkMenuOption *o, int dir) {
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

int vpk_menu_layout(const VpkMenuList *const *lists, int nlists, const char *const *section_names,
                    int nsections, VpkMenuRow *rows, const VpkMenuOption **opts, int max) {
  int s, l, i, n = 0;
  for (s = 0; s < nsections; s++) {
    int header = 0;
    for (l = 0; l < nlists; l++)
      for (i = 0; lists[l] && i < lists[l]->n; i++) {
        const VpkMenuOption *o = &lists[l]->opts[i];
        if (o->section != s || n + 2 > max || (o->visible && !o->visible()))
          continue;
        if (!header) {
          rows[n].label = section_names[s];
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

/* config.txt values are lower case, row names upper case */
static int same_name(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
      return 0;
  return *a == *b;
}

static const VpkMenuOption *find(const VpkMenuList *const *lists, int nlists, const char *key,
                              size_t len) {
  int l, i;
  for (l = 0; l < nlists; l++)
    for (i = 0; lists[l] && i < lists[l]->n; i++) {
      const VpkMenuOption *o = &lists[l]->opts[i];
      if (o->key && o->value && strlen(o->key) == len && !strncmp(o->key, key, len))
        return o;
    }
  return NULL;
}

int vpk_menu_load(const VpkMenuList *const *lists, int nlists, const char *key,
                   const char *v) {
  const VpkMenuOption *o = find(lists, nlists, key, strlen(key));
  int i;
  if (!o)
    return 0;
  *o->value = atoi(v);
  for (i = 0; o->names && i < o->count; i++)
    if (same_name(o->names[i], v))
      *o->value = o->values ? o->values[i] : i;
  return 1;
}

/* ---- config.txt ---- */

static int key_is(const char *line, size_t len, const char *k) { return strlen(k) == len && !strncmp(k, line, len); }

static size_t put(char *out, size_t n, size_t cap, const char *a, size_t len) {
  if (n + len >= cap)
    len = n + 1 < cap ? cap - n - 1 : 0;
  memcpy(out + n, a, len);
  n += len;
  if (cap)
    out[n] = 0;
  return n;
}

static size_t put_kv(char *out, size_t n, size_t cap, const char *k, const char *v) {
  n = put(out, n, cap, k, strlen(k));
  n = put(out, n, cap, "=", 1);
  n = put(out, n, cap, v, strlen(v));
  return put(out, n, cap, "\n", 1);
}

static int in_list(const char *line, size_t len, const char *const *list) {
  for (; list && *list; list++)
    if (key_is(line, len, *list))
      return 1;
  return 0;
}

size_t vpk_config_rewrite(const char *old, size_t old_len, const char *const *key,
                          const char *const *value, int nkeys, const char *const *drop,
                          const char *head, int in_place, const char *const *alias, char *out,
                          size_t cap) {
  char done[256];
  const char *p = old, *end = old ? old + old_len : NULL;
  size_t n = 0;
  int i;
  memset(done, 0, sizeof done);
  if (cap)
    out[0] = 0;
  while (p && p < end) {
    const char *nl = memchr(p, '\n', (size_t)(end - p)), *e = nl ? nl : end, *eq, *k = p;
    size_t len = (size_t)(e - p), klen = 0;
    int which = -1, gone = 0;
    eq = memchr(p, '=', len);
    if (p[0] != '#' && eq) {
      const char *const *a;
      klen = (size_t)(eq - p);
      for (a = alias; a && a[0]; a += 2)
        if (key_is(p, klen, a[0])) {
          k = a[1];
          klen = strlen(a[1]);
          break;
        }
      for (i = 0; i < nkeys && which < 0; i++)
        if (key_is(k, klen, key[i]))
          which = i;
      gone = !in_place && which < 0 && in_list(k, klen, drop);
    }
    if (which < 0 && !gone) {
      n = put(out, n, cap, p, len);
      n = put(out, n, cap, "\n", 1);
    } else if (in_place && which < (int)sizeof done && !done[which]) {
      n = put_kv(out, n, cap, key[which], value[which]);
      done[which] = 1;
    }
    p = nl ? nl + 1 : end;
  }
  if (!in_place && head)
    n = put(out, n, cap, head, strlen(head));
  for (i = 0; i < nkeys; i++)
    if (!in_place || i >= (int)sizeof done || !done[i])
      n = put_kv(out, n, cap, key[i], value[i]);
  return n;
}

int vpk_menu_save(const VpkMenuList *const *lists, int nlists, const char *path,
                  const char *const *drop, const char *head) {
  const char *keys[256], *values[256];
  char nums[256][16], *old = NULL, *out;
  size_t n = 0, cap = 0, len, got;
  int l, i, nk = 0;
  FILE *f;
  for (l = 0; l < nlists; l++)
    for (i = 0; lists[l] && i < lists[l]->n && nk < 256; i++) {
      const VpkMenuOption *o = &lists[l]->opts[i];
      if (o->key && o->value) {
        snprintf(nums[nk], sizeof nums[nk], "%d", *o->value);
        keys[nk] = o->key;
        values[nk] = nums[nk];
        nk++;
      }
    }
  if ((f = fopen(path, "r"))) {
    char buf[4096];
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
      if (n + got > cap) {
        char *b = realloc(old, cap = (n + got) * 2);
        if (!b)
          break;
        old = b;
      }
      memcpy(old + n, buf, got);
      n += got;
    }
    fclose(f);
  }
  len = n + 64 + (head ? strlen(head) : 0);
  for (i = 0; i < nk; i++)
    len += strlen(keys[i]) + strlen(values[i]) + 2;
  out = malloc(len + 1);
  f = out ? fopen(path, "w") : NULL;
  if (!f) {
    free(old);
    free(out);
    return -1;
  }
  len = vpk_config_rewrite(old, n, keys, values, nk, drop, head, 0, NULL, out, len + 1);
  fwrite(out, 1, len, f);
  fclose(f);
  free(old);
  free(out);
  return 0;
}

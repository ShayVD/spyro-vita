/* The touch panels (vpk_touch.h).
 * From kh1-decomp host/vita/pad.c and menu.c @ 85befa4a and spyro-vita src/platform/vita/pad.c @ 987363e. */
#include "vpk_touch.h"

#include <psp2/touch.h>

static VpkTouchPanel s_panel[2] = {{0, 0, 1919, 1087}, {0, 0, 1919, 1087}};

void vpk_touch_init(void) {
  int p;
  for (p = 0; p < 2; p++) {
    SceTouchPanelInfo pi;
    sceTouchSetSamplingState(p, SCE_TOUCH_SAMPLING_STATE_START);
    if (sceTouchGetPanelInfo(p, &pi) >= 0 && pi.maxAaX > pi.minAaX && pi.maxAaY > pi.minAaY) {
      s_panel[p].min_x = pi.minAaX, s_panel[p].min_y = pi.minAaY;
      s_panel[p].max_x = pi.maxAaX, s_panel[p].max_y = pi.maxAaY;
    } else { /* the front panel's range */
      s_panel[p].min_x = s_panel[p].min_y = 0;
      s_panel[p].max_x = 1919, s_panel[p].max_y = 1087;
    }
  }
}

const VpkTouchPanel *vpk_touch_panel(int port) { return &s_panel[port & 1]; }

int vpk_touch_read(int port, VpkTouchFinger *f, int max) {
  const VpkTouchPanel *pi = &s_panel[port & 1];
  SceTouchData d;
  int i, n;
  if (sceTouchPeek(port, &d, 1) < 1)
    return -1;
  n = (int)d.reportNum < max ? (int)d.reportNum : max;
  if (n > SCE_TOUCH_MAX_REPORT)
    n = SCE_TOUCH_MAX_REPORT;
  for (i = 0; i < n; i++) {
    f[i].x = d.report[i].x;
    f[i].y = d.report[i].y;
    f[i].u = (float)(d.report[i].x - pi->min_x) / (pi->max_x - pi->min_x);
    f[i].v = (float)(d.report[i].y - pi->min_y) / (pi->max_y - pi->min_y);
    f[i].id = d.report[i].id;
  }
  return n;
}

unsigned vpk_touch_quarters(const VpkTouchFinger *f, int n, int split_x, int split_y) {
  unsigned m = 0;
  int i;
  for (i = 0; i < n; i++) {
    int left = f[i].x < split_x, top = f[i].y < split_y;
    m |= top ? (left ? VPK_TOUCH_UL : VPK_TOUCH_UR) : (left ? VPK_TOUCH_LL : VPK_TOUCH_LR);
  }
  return m;
}

void vpk_touch_middle(int port, float *x, float *y) {
  const VpkTouchPanel *pi = &s_panel[port & 1];
  *x = pi->min_x + 0.5f * (pi->max_x - pi->min_x);
  *y = pi->min_y + 0.5f * (pi->max_y - pi->min_y);
}

int vpk_tap_poll(VpkTap *t, int n, float x, float y, int id, unsigned long long now) {
  float dx, dy, d;
  if (n == 0) {
    int tap = t->down && !(t->cancel_multi && t->multi) && now - t->t0 <= t->max_us && t->moved < t->max_move;
    t->down = 0;
    return tap;
  }
  if (!t->down) {
    t->down = 1, t->multi = n > 1, t->id = id, t->t0 = now;
    t->x0 = t->x = x, t->y0 = t->y = y, t->moved = 0;
    return 0;
  }
  if (n > 1 || id != t->id)
    t->multi = 1;
  t->x = x, t->y = y;
  dx = x - t->x0, dy = y - t->y0;
  dx = dx < 0 ? -dx : dx, dy = dy < 0 ? -dy : dy;
  d = dx > dy ? dx : dy;
  if (d > t->moved)
    t->moved = d;
  return 0;
}

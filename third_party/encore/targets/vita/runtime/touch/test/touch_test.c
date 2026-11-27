/* vpk_touch against a fake sceTouch: the panels' areas (and the fallback when the call fails), fingers raw and
 * 0..1, a failed read, the rear quarters (kh1-decomp's split at 960 x 500, spyro-vita's halves at the middle),
 * and the tap detector with both games' settings: kh1-decomp's (0.6 s, under 41 of its half-size pixels on
 * either axis, a second finger allowed) and spyro-vita's (0.3 s, under 0.04 of the panel, no second finger). */
#include <stdio.h>
#include <string.h>
#include <psp2/touch.h>
#include "../vpk_touch.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("touch_test: FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int started[2], info_fails, peek_fails;
static SceTouchData data[2];
int sceTouchSetSamplingState(unsigned int port, int state) { started[port & 1] = state; return 0; }
int sceTouchGetPanelInfo(unsigned int port, SceTouchPanelInfo *pi) {
  if (info_fails) return -1;
  memset(pi, 0, sizeof *pi);
  if (port == 1) pi->minAaX = 0, pi->minAaY = 108, pi->maxAaX = 1919, pi->maxAaY = 889; /* the rear panel's */
  else pi->maxAaX = 1919, pi->maxAaY = 1087;
  return 0;
}
int sceTouchPeek(unsigned int port, SceTouchData *d, unsigned int n) {
  (void)n;
  if (peek_fails) return 0;
  *d = data[port & 1];
  return 1;
}
static void finger(int port, int i, int x, int y, int id) {
  data[port].report[i].x = (short)x, data[port].report[i].y = (short)y, data[port].report[i].id = (unsigned char)id;
  if ((int)data[port].reportNum <= i) data[port].reportNum = (unsigned)i + 1;
}

int main(void) {
  VpkTouchFinger f[VPK_TOUCH_MAX];
  float mx, my;
  int n;
  info_fails = 1;
  vpk_touch_init();
  CHECK(vpk_touch_panel(VPK_TOUCH_BACK)->max_y == 1087); /* the fallback */
  info_fails = 0;
  vpk_touch_init();
  CHECK(started[0] == 1 && started[1] == 1);
  CHECK(vpk_touch_panel(VPK_TOUCH_BACK)->min_y == 108 && vpk_touch_panel(VPK_TOUCH_BACK)->max_y == 889);

  finger(1, 0, 100, 200, 3);
  finger(1, 1, 1500, 700, 4);
  n = vpk_touch_read(VPK_TOUCH_BACK, f, VPK_TOUCH_MAX);
  CHECK(n == 2 && f[0].x == 100 && f[0].y == 200 && f[0].id == 3 && f[1].id == 4);
  CHECK(f[0].u > 0.0520f && f[0].u < 0.0522f && f[1].v > 0.757f && f[1].v < 0.759f);
  CHECK(vpk_touch_read(VPK_TOUCH_BACK, f, 1) == 1);
  peek_fails = 1;
  CHECK(vpk_touch_read(VPK_TOUCH_BACK, f, VPK_TOUCH_MAX) == -1);
  peek_fails = 0;

  /* kh1-decomp: quarters split at (960, 500) */
  n = vpk_touch_read(VPK_TOUCH_BACK, f, VPK_TOUCH_MAX);
  CHECK(vpk_touch_quarters(f, n, 960, 500) == (VPK_TOUCH_UL | VPK_TOUCH_LR));
  finger(1, 0, 960, 499, 3);
  n = vpk_touch_read(VPK_TOUCH_BACK, f, 1);
  CHECK(vpk_touch_quarters(f, n, 960, 500) == VPK_TOUCH_UR);
  CHECK(vpk_touch_quarters(f, 0, 960, 500) == 0);
  /* spyro-vita: halves at the middle */
  vpk_touch_middle(VPK_TOUCH_BACK, &mx, &my);
  CHECK(mx == 959.5f && my == 498.5f);
  finger(1, 0, 959, 0, 3);
  n = vpk_touch_read(VPK_TOUCH_BACK, f, 1);
  CHECK((vpk_touch_quarters(f, n, (int)(mx + 1), 0) & (VPK_TOUCH_UL | VPK_TOUCH_LL)) && f[0].u < 0.5f);

  { /* kh1-decomp's tap: 0.6 s, a move of 41 or more of its pixels cancels, a second finger does not */
    VpkTap t;
    memset(&t, 0, sizeof t);
    t.max_us = 600000, t.max_move = 41;
    CHECK(!vpk_tap_poll(&t, 1, 480, 272, 1, 1000));
    CHECK(!vpk_tap_poll(&t, 2, 520, 272, 1, 2000)); /* 40 across, and a second finger */
    CHECK(vpk_tap_poll(&t, 0, 0, 0, 0, 601000) && t.x0 == 480 && t.y0 == 272);
    CHECK(!vpk_tap_poll(&t, 1, 480, 272, 1, 700000) && !vpk_tap_poll(&t, 1, 480, 313, 1, 700100));
    CHECK(!vpk_tap_poll(&t, 0, 0, 0, 0, 700200)); /* 41 down */
    CHECK(!vpk_tap_poll(&t, 1, 10, 10, 1, 800000) && !vpk_tap_poll(&t, 0, 0, 0, 0, 1400001)); /* too long */
    CHECK(!vpk_tap_poll(&t, 0, 0, 0, 0, 1500000)); /* nothing down */
  }
  { /* spyro-vita's tap: 0.3 s, under 0.04, one finger */
    VpkTap t;
    memset(&t, 0, sizeof t);
    t.max_us = 300000, t.max_move = 0.04f, t.cancel_multi = 1;
    vpk_tap_poll(&t, 1, 0.5f, 0.5f, 7, 0);
    vpk_tap_poll(&t, 1, 0.53f, 0.49f, 7, 1000);
    CHECK(vpk_tap_poll(&t, 0, 0, 0, 0, 300000) && t.x == 0.53f && t.y == 0.49f);
    vpk_tap_poll(&t, 1, 0.5f, 0.5f, 7, 400000);
    vpk_tap_poll(&t, 1, 0.5f, 0.5f, 8, 401000); /* another finger */
    CHECK(!vpk_tap_poll(&t, 0, 0, 0, 0, 402000));
    vpk_tap_poll(&t, 1, 0.5f, 0.5f, 7, 500000);
    vpk_tap_poll(&t, 1, 0.5f, 0.55f, 7, 501000);
    CHECK(!vpk_tap_poll(&t, 0, 0, 0, 0, 502000) && t.moved > 0.04f);
  }
  printf("touch_test: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}

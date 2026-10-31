/* Draws the Vita options panel (menu_panel.c) with sample rows, checks a
 * few pixels (title colour present, selected bar, border) and writes the
 * image as a PPM for a look. */
#include "menu_panel.h"

#include <stdio.h>
#include <stdlib.h>

static int count(const uint8_t *rgb, int n, int r, int g, int b) {
  int i, k = 0;
  for (i = 0; i < n; i++)
    k += rgb[i * 3] == r && rgb[i * 3 + 1] == g && rgb[i * 3 + 2] == b;
  return k;
}

int main(int argc, char **argv) {
  static const MenuRow rows[] = {
      {"WIDESCREEN", "ON"}, {"60 FPS (EXPERIMENTAL)", "NO ANIM"}, {"60 FPS BUDGET", "26 MS"}};
  int h = menu_panel_height(3), fails = 0;
  uint8_t *rgb = calloc((size_t)MENU_PANEL_W * h, 3);
  FILE *f;
  menu_panel_draw(rgb, "SPYRO VITA OPTIONS", rows, 3, 1,
                  "UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE  O: CLOSE");
  if (count(rgb, MENU_PANEL_W * h, 245, 205, 70) < 200)
    printf("FAIL: title colour missing\n"), fails++;
  if (count(rgb, MENU_PANEL_W * h, 58, 66, 120) < 5000)
    printf("FAIL: selected row bar missing\n"), fails++;
  if (rgb[0] != 200 || rgb[1] != 160 || rgb[2] != 40)
    printf("FAIL: border\n"), fails++;
  if (argc > 1 && (f = fopen(argv[1], "wb"))) {
    fprintf(f, "P6\n%d %d\n255\n", MENU_PANEL_W, h);
    fwrite(rgb, 3, (size_t)MENU_PANEL_W * h, f);
    fclose(f);
  }
  printf("menu panel %dx%d: %s\n", MENU_PANEL_W, h, fails ? "FAIL" : "ok");
  free(rgb);
  return fails != 0;
}

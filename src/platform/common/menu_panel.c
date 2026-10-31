/* The port's options panel, drawn on the CPU (menu_panel.h): a 5x7 bitmap
 * font scaled up, a dark box, the selected row on a lighter bar, section
 * headers smaller in gold, and a window of lines that scrolls (D45). */
#include "menu_panel.h"

#include <string.h>

/* 5x7 glyphs, one byte per row, bit 4 is the leftmost pixel. */
static const struct {
  char c;
  uint8_t rows[7];
} k_font[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'/', {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
};

#define TEXT_SCALE 3  /* rows: 15x21 pixel glyphs */
#define SMALL_SCALE 2 /* footer */
#define PAD 24
#define TITLE_H 40
#define ROW_H 30
#define FOOTER_H 34
#define ARROW_X (MENU_PANEL_W - PAD / 2 - 2) /* scroll arrows: centre x */

static const uint8_t k_bg[3] = {18, 22, 38};
static const uint8_t k_border[3] = {200, 160, 40};
static const uint8_t k_bar[3] = {58, 66, 120};
static const uint8_t k_title[3] = {245, 205, 70};
static const uint8_t k_text[3] = {235, 235, 240};
static const uint8_t k_value[3] = {140, 220, 255};
static const uint8_t k_dim[3] = {150, 150, 165};

int menu_panel_height(int lines) {
  return PAD + TITLE_H + lines * ROW_H + FOOTER_H + PAD / 2;
}

int menu_panel_top(int n, int sel, int top, int lines) {
  int want = sel > 0 ? sel - 1 : 0; /* the line above too: its header, if any */
  if (n <= lines)
    return 0;
  if (want < top)
    top = want;
  if (sel >= top + lines)
    top = sel - lines + 1;
  if (top > n - lines)
    top = n - lines;
  return top < 0 ? 0 : top;
}

static const uint8_t *glyph(char c) {
  size_t i;
  if (c >= 'a' && c <= 'z')
    c = (char)(c - 'a' + 'A');
  for (i = 0; i < sizeof k_font / sizeof k_font[0]; i++)
    if (k_font[i].c == c)
      return k_font[i].rows;
  return NULL;
}

static int text_width(const char *s, int scale) {
  int n = (int)strlen(s);
  return n ? n * 6 * scale - scale : 0;
}

static void fill(uint8_t *rgb, int h, int x0, int y0, int w, int hh, const uint8_t c[3]) {
  int x, y;
  for (y = y0; y < y0 + hh && y < h; y++)
    for (x = x0; x < x0 + w && x < MENU_PANEL_W; x++) {
      uint8_t *p = rgb + ((size_t)y * MENU_PANEL_W + x) * 3;
      p[0] = c[0], p[1] = c[1], p[2] = c[2];
    }
}

static void text(uint8_t *rgb, int h, int x, int y, const char *s, int scale, const uint8_t c[3]) {
  for (; *s; s++, x += 6 * scale) {
    const uint8_t *g = glyph(*s);
    int r, b;
    if (!g)
      continue;
    for (r = 0; r < 7; r++)
      for (b = 0; b < 5; b++)
        if (g[r] & (0x10 >> b))
          fill(rgb, h, x + b * scale, y + r * scale, scale, scale, c);
  }
}

/* A small triangle pointing up or down, centred on x, its flat side at y. */
static void arrow(uint8_t *rgb, int h, int x, int y, int up, const uint8_t c[3]) {
  int r; /* r: half the width of the line r - 5 (up) or 5 - r (down) away */
  for (r = 0; r < 6; r++)
    fill(rgb, h, x - r, up ? y - 5 + r : y + 5 - r, 2 * r + 1, 1, c);
}

void menu_panel_draw(uint8_t *rgb, const char *title, const MenuRow *rows, int n,
                     int sel, int top, int lines, const char *footer) {
  int h = menu_panel_height(lines), i, y, y0 = PAD + TITLE_H;
  fill(rgb, h, 0, 0, MENU_PANEL_W, h, k_border);
  fill(rgb, h, 3, 3, MENU_PANEL_W - 6, h - 6, k_bg);
  text(rgb, h, (MENU_PANEL_W - text_width(title, TEXT_SCALE)) / 2, PAD, title, TEXT_SCALE,
       k_title);
  y = y0;
  for (i = top; i < n && i < top + lines; i++, y += ROW_H) {
    char value[64];
    int ty = y + (ROW_H - 7 * TEXT_SCALE) / 2;
    if (!rows[i].value) { /* a section header: small gold text on a rule */
      int tw = text_width(rows[i].label, SMALL_SCALE);
      ty = y + ROW_H - 7 * SMALL_SCALE - 4;
      text(rgb, h, PAD, ty, rows[i].label, SMALL_SCALE, k_title);
      fill(rgb, h, PAD + tw + 8, ty + 3 * SMALL_SCALE, MENU_PANEL_W - 2 * PAD - tw - 8, 2,
           k_border);
      continue;
    }
    if (i == sel) {
      fill(rgb, h, PAD / 2, y, MENU_PANEL_W - PAD, ROW_H, k_bar);
      value[0] = '<', value[1] = ' ';
      strncpy(value + 2, rows[i].value, sizeof value - 5);
      value[sizeof value - 3] = 0;
      strcat(value, " >");
    } else {
      strncpy(value, rows[i].value, sizeof value - 1);
      value[sizeof value - 1] = 0;
    }
    text(rgb, h, PAD, ty, rows[i].label, TEXT_SCALE, k_text);
    text(rgb, h, MENU_PANEL_W - PAD - text_width(value, TEXT_SCALE), ty, value, TEXT_SCALE,
         k_value);
  }
  if (top > 0)
    arrow(rgb, h, ARROW_X, y0 - 10, 1, k_title);
  if (top + lines < n)
    arrow(rgb, h, ARROW_X, y0 + lines * ROW_H + 4, 0, k_title);
  y = y0 + lines * ROW_H;
  text(rgb, h, (MENU_PANEL_W - text_width(footer, SMALL_SCALE)) / 2,
       y + (FOOTER_H - 7 * SMALL_SCALE) / 2 + 4, footer, SMALL_SCALE, k_dim);
}

/* The disc screens (decisions D60): a menu panel (menu_panel.c) on a dark
 * screen, drawn on the CPU into one 960x544 framebuffer and shown with
 * sceDisplaySetFrameBuf. They come before either renderer exists (the disc
 * is opened first), so they work the same with gpu=1 and gpu=0, and vitaGL
 * or SDL takes the display over afterwards as usual. */
#include "vita.h"
#include "menu_panel.h"

#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <stdlib.h>
#include <string.h>

#define FB_W 960
#define FB_H 544
#define FB_SIZE 0x200000 /* 960 * 544 * 4, rounded up to the CDRAM block size */

static SceUID s_block = -1;
static uint32_t *s_fb;
static uint8_t *s_panel;

int vita_boot_panel(const char *title, const char *text, const char *footer) {
  SceDisplayFrameBuf fb;
  int h, x, y, x0, y0;
  if (s_block < 0) {
    s_block = sceKernelAllocMemBlock("disc screen", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                     FB_SIZE, NULL);
    if (s_block < 0 || sceKernelGetMemBlockBase(s_block, (void **)&s_fb) < 0) {
      vita_log("disc screen: no framebuffer (0x%08x)", (unsigned)s_block);
      s_block = -1;
      return -1;
    }
    s_panel = malloc((size_t)MENU_PANEL_W * menu_panel_height(MENU_PANEL_LINES) * 3);
  }
  if (!s_panel)
    return -1;
  h = menu_panel_message(s_panel, title, text, footer);
  x0 = (FB_W - MENU_PANEL_W) / 2;
  y0 = (FB_H - h) / 2;
  for (y = 0; y < FB_H; y++)
    for (x = 0; x < FB_W; x++) {
      uint32_t c = 0xFF120A08; /* A8B8G8R8: a dark blue, as the menu's surround */
      if (x >= x0 && x < x0 + MENU_PANEL_W && y >= y0 && y < y0 + h) {
        const uint8_t *p = s_panel + ((size_t)(y - y0) * MENU_PANEL_W + (x - x0)) * 3;
        c = 0xFF000000u | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
      }
      s_fb[y * FB_W + x] = c;
    }
  memset(&fb, 0, sizeof fb);
  fb.size = sizeof fb;
  fb.base = s_fb;
  fb.pitch = FB_W;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = FB_W;
  fb.height = FB_H;
  return sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0 ? -1 : 0;
}

/* Once the renderer has shown a picture, the framebuffer isn't on screen. */
void vita_boot_close(void) {
  if (s_block < 0)
    return;
  sceKernelFreeMemBlock(s_block);
  s_block = -1;
  s_fb = NULL;
  free(s_panel);
  s_panel = NULL;
}

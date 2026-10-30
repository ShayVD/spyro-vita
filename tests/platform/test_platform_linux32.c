/* Tests the linux32 headless platform layer (docs/porting-plan.md §5): an OT
 * in the PS1 arena goes through PsyCross's walker into the soft GPU, then we
 * check VRAM pixels; plus VRAM transfers, virtual vblanks and the pad script.
 * Writes <outdir>/test-display.png and test-vram.png. */
#include "host.h"
#include "psx_arena.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx/libetc.h"
#include "psx/libgte.h"
#include "psx/libgpu.h"
#include "psx/libpad.h"

/* libcd/libspu are another module; the vblank code only needs these. */
__attribute__((weak)) void psx_cd_vblank(void) {}
__attribute__((weak)) void psx_spu_vblank(void) {}

static int s_fail;

#define RGB15(r, g, b) ((uint16_t)((r) | (g) << 5 | (b) << 10))
#define DY 256 /* draw/display buffer at VRAM y = 256 */

static void expect_px(int x, int y, uint16_t want, const char *what) {
  uint16_t got = host_vram()[y * 1024 + x];
  if (got != want) {
    printf("FAIL %s: vram(%d,%d) = %04x, want %04x\n", what, x, y, got, want);
    s_fail++;
  }
}

static void expect(int cond, const char *what) {
  if (!cond) {
    printf("FAIL %s\n", what);
    s_fail++;
  }
}

static unsigned char *s_heap;
static void *arena_alloc(size_t n) {
  void *p = s_heap;
  s_heap += (n + 3) & ~3u;
  return p;
}

/* 16x16 4-bpp texture at (640,0): index 0 (transparent) for u < 4, then a
 * checkerboard of 1 (red, opaque) and 2 (green, STP set). */
#define TEX_X 640
#define CLUT_X 512
#define CLUT_Y 0
static void upload_texture(void) {
  uint16_t pix[16 * 4], clut[16];
  RECT16 r;
  int u, v;
  memset(pix, 0, sizeof pix);
  for (v = 0; v < 16; v++)
    for (u = 0; u < 16; u++) {
      int idx = u < 4 ? 0 : ((u + v) & 1) + 1;
      pix[v * 4 + u / 4] |= (uint16_t)(idx << ((u & 3) * 4));
    }
  memset(clut, 0, sizeof clut);
  clut[1] = RGB15(31, 0, 0);
  clut[2] = RGB15(0, 31, 0) | 0x8000;
  setRECT(&r, TEX_X, 0, 4, 16);
  LoadImage(&r, (u_long *)pix);
  setRECT(&r, CLUT_X, CLUT_Y, 16, 1);
  LoadImage(&r, (u_long *)clut);
}

static POLY_FT4 *ft4(int x, int y, int c, int abr, int semi) {
  POLY_FT4 *p = arena_alloc(sizeof *p);
  setPolyFT4(p);
  setRGB0(p, c, c, c);
  setXY4(p, x, y, x + 16, y, x, y + 16, x + 16, y + 16);
  setUV4(p, 0, 0, 16, 0, 0, 16, 16, 16);
  p->tpage = getTPage(0, abr, TEX_X, 0);
  p->clut = getClut(CLUT_X, CLUT_Y);
  setSemiTrans(p, semi);
  return p;
}

static void test_draw(const char *outdir) {
  DRAWENV de;
  DISPENV di;
  u_long *ot;
  POLY_F3 *f3;
  POLY_G4 *g4;
  POLY_F4 *f4, *clipf4;
  POLY_FT4 *ft;
  DR_TPAGE *tp;
  char path[1024];
  const uint16_t bg = RGB15(0, 0, 8);

  SetDefDrawEnv(&de, 0, DY, 320, 240);
  de.isbg = 1;
  de.r0 = 0, de.g0 = 0, de.b0 = 64;
  SetDefDispEnv(&di, 0, DY, 320, 240);
  PutDispEnv(&di);
  PutDrawEnv(&de);
  expect(de.dfe == 1, "SetDefDrawEnv gives dfe=1");

  ot = arena_alloc(8 * sizeof(u_long));
  ClearOTagR(ot, 8);

  f3 = arena_alloc(sizeof *f3); /* drawn first */
  setPolyF3(f3);
  setRGB0(f3, 255, 0, 0);
  setXY3(f3, 10, 10, 60, 10, 10, 60);
  addPrim(ot + 7, f3);

  g4 = arena_alloc(sizeof *g4);
  setPolyG4(g4);
  setRGB0(g4, 0, 0, 0);
  setRGB1(g4, 255, 0, 0);
  setRGB2(g4, 0, 0, 255);
  setRGB3(g4, 255, 0, 255);
  setXY4(g4, 80, 10, 144, 10, 80, 74, 144, 74);
  addPrim(ot + 6, g4);

  /* addPrim evaluates p twice: build first. */
  ft = ft4(160, 10, 128, 0, 0); /* unmodulated */
  addPrim(ot + 5, ft);
  ft = ft4(180, 10, 64, 0, 0); /* half brightness */
  addPrim(ot + 5, ft);
  ft = ft4(200, 10, 128, 0, 1); /* semi, average */
  addPrim(ot + 2, ft);

  clipf4 = arena_alloc(sizeof *clipf4); /* crosses the right clip edge */
  setPolyF4(clipf4);
  setRGB0(clipf4, 255, 255, 255);
  setXY4(clipf4, 300, 100, 340, 100, 300, 120, 340, 120);
  addPrim(ot + 4, clipf4);

  tp = arena_alloc(sizeof *tp); /* additive for the next untextured prim */
  setDrawTPage(tp, 1, 0, getTPage(0, 1, 0, 0));
  addPrim(ot + 1, tp);
  f4 = arena_alloc(sizeof *f4);
  setPolyF4(f4);
  setRGB0(f4, 0, 128, 0);
  setXY4(f4, 20, 20, 40, 20, 20, 40, 40, 40);
  setSemiTrans(f4, 1);
  addPrim(ot + 0, f4);

  DrawOTag(ot + 7);
  DrawSync(0);

  /* F3: top-left fill rule; the hypotenuse x+y=70 and right/bottom vertices
   * are not drawn. Prim y + offset 256 = VRAM y. */
  expect_px(10, 10 + DY, RGB15(31, 0, 0), "F3 top-left vertex");
  expect_px(59, 10 + DY, RGB15(31, 0, 0), "F3 top edge");
  expect_px(60, 10 + DY, bg, "F3 right vertex excluded");
  expect_px(10, 59 + DY, RGB15(31, 0, 0), "F3 left edge");
  expect_px(10, 60 + DY, bg, "F3 bottom vertex excluded");
  expect_px(40, 30 + DY, bg, "F3 hypotenuse excluded");
  expect_px(10, 9 + DY, bg, "isbg clear");
  /* G4: colours interpolate across the quad. */
  expect_px(80, 10 + DY, RGB15(0, 0, 0), "G4 v0");
  expect_px(143, 10 + DY, RGB15(31, 0, 0), "G4 near v1");
  expect_px(80, 73 + DY, RGB15(0, 0, 31), "G4 near v2");
  expect_px(111, 10 + DY, RGB15(15, 0, 0), "G4 halfway");
  expect_px(144, 10 + DY, bg, "G4 right edge excluded");
  /* FT4: 4-bpp CLUT texels; index 0 transparent; STP kept in bit 15. */
  expect_px(160, 10 + DY, bg, "FT4 texel 0 transparent");
  expect_px(164, 10 + DY, RGB15(31, 0, 0), "FT4 texel 1");
  expect_px(165, 10 + DY, RGB15(0, 31, 0) | 0x8000, "FT4 texel 2 (STP)");
  expect_px(175, 25 + DY, RGB15(31, 0, 0), "FT4 last texel");
  expect_px(176, 10 + DY, bg, "FT4 right edge excluded");
  expect_px(184, 10 + DY, RGB15(15, 0, 0), "FT4 modulated 64/128");
  /* Semi FT4 (average): STP texels blend with the background, others not. */
  expect_px(204, 10 + DY, RGB15(31, 0, 0), "semi FT4 opaque texel");
  expect_px(205, 10 + DY, RGB15(0, 15, 4) | 0x8000, "semi FT4 averaged texel");
  /* Semi F4 after DR_TPAGE abr=1: additive over the red F3. */
  expect_px(25, 25 + DY, RGB15(31, 16, 0), "semi F4 additive");
  /* Draw-area clip at x = 320. */
  expect_px(319, 100 + DY, RGB15(31, 31, 31), "clip inside");
  expect_px(320, 100 + DY, 0, "clip outside");

  snprintf(path, sizeof path, "%s/test-display.png", outdir);
  expect(host_shot_display(path) == 0, "write display png");
  snprintf(path, sizeof path, "%s/test-vram.png", outdir);
  expect(host_shot_vram(path) == 0, "write vram png");
  {
    unsigned char sig[8] = {0};
    FILE *f = fopen(path, "rb");
    expect(f && fread(sig, 1, 8, f) == 8 && memcmp(sig, "\x89PNG\r\n\x1a\n", 8) == 0,
           "png signature");
    if (f)
      fclose(f);
  }
}

static void test_transfers(void) {
  uint16_t in[6] = {1, 2, 3, 4, 5, 6}, out[6];
  RECT16 r;
  setRECT(&r, 1022, 500, 3, 2); /* wraps in x */
  LoadImage(&r, (u_long *)in);
  expect_px(1023, 500, 2, "LoadImage");
  expect_px(0, 500, 3, "LoadImage wraps x");
  memset(out, 0, sizeof out);
  StoreImage(&r, (u_long *)out);
  expect(memcmp(in, out, sizeof in) == 0, "StoreImage round trip");
  MoveImage(&r, 600, 300);
  DrawSync(0);
  expect_px(600, 300, 1, "MoveImage");
  expect_px(602, 301, 6, "MoveImage last");
  setRECT(&r, 600, 300, 2, 2);
  ClearImage(&r, 255, 0, 255);
  expect_px(601, 301, RGB15(31, 0, 31), "ClearImage colour");
}

static unsigned char s_pad1[34], s_pad2[34];
static int s_cb_count, s_cb_vblank, s_cb_buttons;

static void vsync_cb(void) {
  s_cb_count++;
  s_cb_vblank = VSync(-1);
  s_cb_buttons = s_pad1[2] | s_pad1[3] << 8;
}

static void test_time_and_pad(const char *outdir) {
  char path[1024];
  FILE *f;
  int v0, i;
  snprintf(path, sizeof path, "%s/test-pad.txt", outdir);
  f = fopen(path, "w");
  fprintf(f, "# test script\n3 cross+start\n\n5 none   # release\n");
  fclose(f);
  expect(host_pad_load(path) == 0, "pad script loads");

  PadInitDirect(s_pad1, s_pad2);
  PadStartCom();
  VSyncCallback(vsync_cb);
  v0 = VSync(-1);
  expect(VSync(-1) == v0, "VSync(-1) does not advance");
  for (i = 0; i < 5; i++) {
    VSync(0);
    if (VSync(-1) == 3) {
      expect(s_pad1[0] == 0 && s_pad1[1] == 0x41, "pad status/type");
      expect(s_cb_buttons == (0xFFFF & ~(0x4000 | 0x0008)),
             "callback sees cross+start, active low");
    }
    if (VSync(-1) == 4)
      expect(s_cb_buttons == (0xFFFF & ~(0x4000 | 0x0008)), "buttons hold");
  }
  expect(VSync(-1) == v0 + 5, "VSync(0) is one vblank");
  expect(s_cb_count == 5 && s_cb_vblank == v0 + 5, "callback once per vblank");
  expect(s_cb_buttons == 0xFFFF, "released at vblank 5");
  expect(s_pad2[0] == 0xFF, "port 2 empty");
  VSyncCallback(NULL);
}

/* The game-side trace (src/game/trace.c) isn't linked into this test. */
void spyro_trace_vblank(int vblank) { (void)vblank; }
void spyro_trace_close(void) {}
void spyro_moby_dump(const char *path) { (void)path; }

int main(int argc, char **argv) {
  const char *outdir = argc > 1 ? argv[1] : ".";
  psx_arena_init();
  s_heap = PSX_ARENA(0x80100000u);
  upload_texture();
  test_draw(outdir);
  test_transfers();
  test_time_and_pad(outdir);
  if (s_fail) {
    printf("%d check(s) failed\n", s_fail);
    return 1;
  }
  printf("all platform_linux32 checks passed\n");
  return 0;
}

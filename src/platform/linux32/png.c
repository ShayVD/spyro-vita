/* Screenshots for the headless linux32 host (docs/porting-plan.md §5): a tiny
 * PNG writer (stored deflate blocks, CRC32, Adler32; no zlib) and the
 * picture a TV would show: the VRAM rect of the last PutDispEnv, 15-bit or
 * 24-bit (isrgb24, the boot logos), black while SetDispMask(0). With
 * --out960 (on by itself with --widescreen or --interp) the picture is the
 * presentation frame (src/game/present.c) scaled to 960x544: full width when
 * widescreen, pillarboxed 4:3 otherwise. */
#include "host.h"
#include "../../game/present.h" /* also built by the Vita target */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/PsyX_GPU.h"

static uint32_t s_crc_table[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
  if (!s_crc_table[1]) {
    uint32_t i, k, c;
    for (i = 0; i < 256; i++) {
      for (c = i, k = 0; k < 8; k++)
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      s_crc_table[i] = c;
    }
  }
  while (n--)
    crc = s_crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return crc;
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n) {
  uint8_t hdr[8], tail[4];
  uint32_t crc;
  put32(hdr, n);
  memcpy(hdr + 4, type, 4);
  crc = crc32_update(0xFFFFFFFFu, hdr + 4, 4);
  crc = crc32_update(crc, data, n) ^ 0xFFFFFFFFu;
  put32(tail, crc);
  fwrite(hdr, 1, 8, f);
  fwrite(data, 1, n, f);
  fwrite(tail, 1, 4, f);
}

int host_png_write(const char *path, const uint8_t *rgb, int w, int h) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  size_t raw_len = (size_t)(w * 3 + 1) * h;
  size_t nblocks = raw_len / 65535 + 1;
  size_t z_len = 2 + raw_len + nblocks * 5 + 4;
  uint8_t ihdr[13], *z, *zp;
  uint32_t a = 1, b = 0;
  size_t done = 0;
  int y, x;
  FILE *f = fopen(path, "wb");
  if (!f) {
    host_log("png: cannot write %s", path);
    return -1;
  }
  z = malloc(z_len);
  zp = z;
  *zp++ = 0x78;
  *zp++ = 0x01;
  for (y = 0; y < h; y++) {
    for (x = -1; x < w * 3; x++) {
      uint8_t c = x < 0 ? 0 : rgb[(size_t)y * w * 3 + x]; /* filter 0 */
      if (done % 65535 == 0) {
        size_t left = raw_len - done, len = left < 65535 ? left : 65535;
        *zp++ = left <= 65535;
        *zp++ = len & 0xFF, *zp++ = len >> 8;
        *zp++ = ~len & 0xFF, *zp++ = (~len >> 8) & 0xFF;
      }
      *zp++ = c;
      a = (a + c) % 65521;
      b = (b + a) % 65521;
      done++;
    }
  }
  put32(zp, b << 16 | a);
  zp += 4;

  put32(ihdr, w);
  put32(ihdr + 4, h);
  ihdr[8] = 8;  /* bit depth */
  ihdr[9] = 2;  /* RGB */
  ihdr[10] = ihdr[11] = ihdr[12] = 0;
  fwrite(sig, 1, 8, f);
  chunk(f, "IHDR", ihdr, 13);
  chunk(f, "IDAT", z, (uint32_t)(zp - z));
  chunk(f, "IEND", NULL, 0);
  free(z);
  return fclose(f) == 0 ? 0 : -1;
}

static uint8_t c5to8(int c) { return (uint8_t)(c << 3 | c >> 2); }

static uint8_t *ps1_display_rgb(int *pw, int *ph) {
  const DISPENV *d = &currentDispEnv; /* what the PS1 scans out now */
  int w = d->disp.w > 0 ? d->disp.w : 320, h = d->disp.h > 0 ? d->disp.h : 240;
  uint8_t *rgb = calloc((size_t)w * h, 3);
  const uint16_t *vram = host_vram();
  int x, y;
  if (d->disp.w > 0 && !g_GPUDisabledState) {
    for (y = 0; y < h; y++) {
      const uint8_t *row =
          (const uint8_t *)(vram + ((d->disp.y + y) & 511) * 1024);
      uint8_t *o = rgb + (size_t)y * w * 3;
      for (x = 0; x < w; x++, o += 3) {
        if (d->isrgb24) {
          int off = d->disp.x * 2 + x * 3;
          o[0] = row[off & 2047];
          o[1] = row[(off + 1) & 2047];
          o[2] = row[(off + 2) & 2047];
        } else {
          uint16_t p = vram[((d->disp.y + y) & 511) * 1024 + ((d->disp.x + x) & 1023)];
          o[0] = c5to8(p & 31);
          o[1] = c5to8((p >> 5) & 31);
          o[2] = c5to8((p >> 10) & 31);
        }
      }
    }
  }
  *pw = w;
  *ph = h;
  return rgb;
}

/* ---- 960x544 output ---- */

#define OUT_W 960
#define OUT_H 544

/* linux32: one presentation frame per vblank (the Vita takes them when it
 * presents, video.c and gpu_gl.c). */
void host_present_vblank(void) { (void)port_present_frame(); }

/* Bilinear scale of src (sw x sh RGB) into the dst rectangle x0..x0+w. */
static void scale_into(uint8_t *dst, int x0, int w, const uint8_t *src, int sw,
                       int sh) {
  int x, y, c;
  for (y = 0; y < OUT_H; y++) {
    int fy = (int)(((long long)(2 * y + 1) * sh * 256 / (2 * OUT_H)) - 128);
    int y0 = fy < 0 ? 0 : fy >> 8, ty = fy < 0 ? 0 : fy & 255;
    int y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
    for (x = 0; x < w; x++) {
      int fx = (int)(((long long)(2 * x + 1) * sw * 256 / (2 * w)) - 128);
      int xa = fx < 0 ? 0 : fx >> 8, tx = fx < 0 ? 0 : fx & 255;
      int xb = xa + 1 < sw ? xa + 1 : sw - 1;
      const uint8_t *p00 = src + ((size_t)y0 * sw + xa) * 3;
      const uint8_t *p01 = src + ((size_t)y0 * sw + xb) * 3;
      const uint8_t *p10 = src + ((size_t)y1 * sw + xa) * 3;
      const uint8_t *p11 = src + ((size_t)y1 * sw + xb) * 3;
      uint8_t *o = dst + ((size_t)y * OUT_W + x0 + x) * 3;
      for (c = 0; c < 3; c++) {
        int top = p00[c] * (256 - tx) + p01[c] * tx;
        int bot = p10[c] * (256 - tx) + p11[c] * tx;
        o[c] = (uint8_t)((top * (256 - ty) + bot * ty + 32768) >> 16);
      }
    }
  }
}

uint8_t *host_display_rgb(int *pw, int *ph) {
  uint8_t *src, *out;
  int sw, sh, wide = 0, i;
  const uint16_t *px = g_host_opts.out960 ? port_present_shown_pixels() : NULL;
  if (!px)
    if (!g_host_opts.out960 || g_host_opts.out960 == 2)
      return ps1_display_rgb(pw, ph);
  if (px) {
    sw = PRESENT_W, sh = PRESENT_H;
    src = malloc((size_t)sw * sh * 3);
    for (i = 0; i < sw * sh; i++) {
      src[i * 3 + 0] = c5to8(px[i] & 31);
      src[i * 3 + 1] = c5to8((px[i] >> 5) & 31);
      src[i * 3 + 2] = c5to8((px[i] >> 10) & 31);
    }
    if (g_host_opts.out960 == 2) { /* --present-native: the frame as drawn */
      *pw = sw;
      *ph = sh;
      return src;
    }
    wide = port_present_frame_is_wide();
  } else {
    src = ps1_display_rgb(&sw, &sh);
  }
  out = calloc((size_t)OUT_W * OUT_H, 3);
  if (wide)
    scale_into(out, 0, OUT_W, src, sw, sh);
  else /* 4:3 at 544 lines is 725 wide */
    scale_into(out, (OUT_W - 725) / 2, 725, src, sw, sh);
  free(src);
  *pw = OUT_W;
  *ph = OUT_H;
  return out;
}

int host_shot_display(const char *path) {
  int w, h, r;
  uint8_t *rgb = host_display_rgb(&w, &h);
  r = host_png_write(path, rgb, w, h);
  free(rgb);
  return r;
}

int host_shot_vram(const char *path) {
  uint8_t *rgb = malloc(1024 * 512 * 3);
  const uint16_t *vram = host_vram();
  int i, r;
  for (i = 0; i < 1024 * 512; i++) {
    rgb[i * 3 + 0] = c5to8(vram[i] & 31);
    rgb[i * 3 + 1] = c5to8((vram[i] >> 5) & 31);
    rgb[i * 3 + 2] = c5to8((vram[i] >> 10) & 31);
  }
  r = host_png_write(path, rgb, 1024, 512);
  free(rgb);
  if (r == 0) { /* and the raw 16-bit words next to it, for tools/vram.py */
    char raw[1024];
    FILE *f;
    const char *dot = strrchr(path, '.');
    snprintf(raw, sizeof raw, "%.*s.bin",
             dot ? (int)(dot - path) : (int)strlen(path), path);
    f = fopen(raw, "wb");
    if (!f || fwrite(vram, 2, 1024 * 512, f) != 1024 * 512)
      r = -1;
    if (f)
      fclose(f);
  }
  return r;
}

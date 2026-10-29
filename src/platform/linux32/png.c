/* Screenshots for the headless linux32 host (docs/porting-plan.md §5): a tiny
 * PNG writer (stored deflate blocks, CRC32, Adler32; no zlib) and the
 * picture a TV would show: the VRAM rect of the last PutDispEnv, 15-bit or
 * 24-bit (isrgb24, the boot logos), black while SetDispMask(0). */
#include "host.h"

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

uint8_t *host_display_rgb(int *pw, int *ph) {
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

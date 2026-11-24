/* enc_blit_scaled (encore/backend.h): the frame into its box, any source format to 32-bit, nearest pixel. */
#include "encore/backend.h"

#include <string.h>

static uint32_t rgb_at(const EncFrame *f, int x, int y) {
  const uint8_t *row = (const uint8_t *)f->pixels + (size_t)y * (size_t)f->stride;
  uint32_t p, r, g, b;
  switch (f->format) {
  case ENC_PIX_XRGB8888: return ((const uint32_t *)row)[x] & 0xffffffu;
  case ENC_PIX_XBGR8888:
    p = ((const uint32_t *)row)[x];
    return ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
  case ENC_PIX_RGB565:
    p = ((const uint16_t *)row)[x];
    r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
  case ENC_PIX_XBGR1555:
  default:
    p = ((const uint16_t *)row)[x];
    r = p & 31, g = (p >> 5) & 31, b = (p >> 10) & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
  }
}

int enc_blit_scaled(const EncFrame *f, uint32_t *dst, int dst_w, int dst_h, int dst_stride, EncRect box, int bgr) {
  int x, y;
  if (!f || !f->pixels || f->width <= 0 || f->height <= 0 || (unsigned)f->format > ENC_PIX_XBGR1555)
    return ENC_ERR_ARG;
  if (f->stride < f->width * (f->format <= ENC_PIX_XBGR8888 ? 4 : 2)) return ENC_ERR_ARG;
  if (!dst || dst_w <= 0 || dst_h <= 0 || dst_stride < dst_w) return ENC_ERR_ARG;
  if (box.x < 0 || box.y < 0 || box.w < 0 || box.h < 0 || box.x + box.w > dst_w || box.y + box.h > dst_h)
    return ENC_ERR_ARG;
  for (y = 0; y < dst_h; y++) memset(dst + (size_t)y * (size_t)dst_stride, 0, (size_t)dst_w * sizeof *dst);
  for (y = 0; y < box.h; y++) {
    int sy = (int)((long)y * f->height / box.h);
    uint32_t *out = dst + (size_t)(box.y + y) * (size_t)dst_stride + box.x;
    for (x = 0; x < box.w; x++) {
      uint32_t p = rgb_at(f, (int)((long)x * f->width / box.w), sy);
      out[x] = bgr ? ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff) : p;
    }
  }
  return ENC_OK;
}

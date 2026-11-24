/* The portable parts of Encore's platform interface (core/include/encore/platform.h): error names and the aspect
 * box, the same on every target. The box math is the one piece of "present" both kh1-vita and spyro-vita shared
 * (docs/roadmap.md, K4 notes). */
#include "encore/platform.h"

const char *enc_error_name(int err) {
  switch (err) {
  case ENC_OK: return "ok";
  case ENC_ERR_UNSUPPORTED: return "unsupported";
  case ENC_ERR_ARG: return "bad argument";
  case ENC_ERR_IO: return "i/o error";
  case ENC_ERR_NOMEM: return "out of memory";
  case ENC_ERR_SYSTEM: return "system error";
  case ENC_ERR_STATE: return "wrong state";
  default: return "unknown error";
  }
}

EncRect enc_aspect_box(int src_w, int src_h, int dst_w, int dst_h, int aspect) {
  EncRect r = {0, 0, dst_w, dst_h};
  long num, den; /* the box's width : height */
  if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
    r.w = r.h = 0;
    return r;
  }
  switch (aspect) {
  case ENC_ASPECT_STRETCH:
    return r;
  case ENC_ASPECT_INTEGER: {
    int k = dst_w / src_w < dst_h / src_h ? dst_w / src_w : dst_h / src_h;
    if (k < 1) { /* bigger than the display: fall back to the 4:3 box */
      num = 4, den = 3;
      break;
    }
    r.w = src_w * k, r.h = src_h * k;
    r.x = (dst_w - r.w) / 2, r.y = (dst_h - r.h) / 2;
    return r;
  }
  case ENC_ASPECT_16_9: num = 16, den = 9; break;
  case ENC_ASPECT_4_3:
  default: num = 4, den = 3; break;
  }
  if ((long)dst_w * den > (long)dst_h * num) { /* the display is wider: bars left and right */
    r.h = dst_h;
    r.w = (int)(((long)dst_h * num + den / 2) / den);
  } else { /* taller: bars above and below */
    r.w = dst_w;
    r.h = (int)(((long)dst_w * den + num / 2) / num);
  }
  r.x = (dst_w - r.w) / 2, r.y = (dst_h - r.h) / 2;
  return r;
}

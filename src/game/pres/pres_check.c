/* --pres-check (stage 7e, decisions D125; pres_draw.h): every in-between
 * frame in play is drawn twice from the same state, by the presenter and
 * by D30's pass, and the two lists DrawOTag is given are compared record
 * for record. Only D30's is walked and shown, so every file the run
 * writes is the same as with --interp alone.
 *
 * A frame record (s_frame[which]): the draw and display environments put
 * before DrawOTag, the 2D primitives noted for the 16:9 narrowing, then
 * the list in its order: each packet's length and words after its link,
 * and for a native_draw marker (native_draw.h) each of its records' kind
 * and words instead. The primitives' addresses aren't compared (the
 * presenter's buffers are its own), only what they say. */
#include "pres_draw.h"

#include "native_draw.h"
#include "psx_gpu.h"
#include "psx_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_pres_check;

enum { FRAME_MAX = 1u << 20, SHOW = 8 };

typedef struct {
  uint32_t *w;
  uint32_t n;
  int full, on;
  uint32_t packets, records;
} Frame;

static Frame s_frame[2];
static unsigned long s_ticks, s_same, s_differ, s_skipped[5], s_packets, s_records;
static unsigned s_shown;
static void (*s_log)(const char *fmt, ...);

static void put(Frame *f, uint32_t w) {
  if (f->n < FRAME_MAX)
    f->w[f->n++] = w;
  else
    f->full = 1;
}

static void put_words(Frame *f, const void *p, uint32_t bytes) {
  uint32_t i, w;
  for (i = 0; i + 4 <= bytes; i += 4) {
    memcpy(&w, (const char *)p + i, 4);
    put(f, w);
  }
}

/* A polygon's words after its tag, without the bits the PS1 GPU ignores:
 * the high half of a textured polygon's third and fourth UV words (pad),
 * and the code byte of a gouraud polygon's later colours. They hold what
 * the buffer held before (the presenter's buffers are its own), so they
 * aren't compared. */
static void put_poly(Frame *f, const uint32_t *w, uint32_t n) {
  const uint32_t code = w[0] >> 24;
  const int gouraud = code & 0x10, quad = code & 0x08, tex = code & 0x04;
  uint32_t i = 0, v, nv = quad ? 4 : 3;
  for (v = 0; v < nv && i < n; v++) {
    if (v == 0 || gouraud) /* the colour */
      put(f, v == 0 ? w[i] : w[i] & 0xFFFFFFu), i++;
    if (i < n) /* x, y */
      put(f, w[i]), i++;
    if (tex && i < n) /* u, v and the CLUT, the page, or pad */
      put(f, v < 2 ? w[i] : w[i] & 0xFFFFu), i++;
  }
  for (; i < n; i++)
    put(f, w[i]);
}

static const uint32_t *next_packet(const uint32_t *p) {
  return (*p & 0xFFFFFFu) == 0xFFFFFFu ? NULL : (const uint32_t *)MEM_OT_NEXT(*p);
}

/* a marker's records: the pool by its bit (or the list's own pool, the
 * presenter's, own_w), the first record's offset */
static void records(Frame *f, const uint32_t *m, const uint32_t *own_w, uint32_t own_cap) {
  const NdPool *pool = &g_nd_pool[m[1] >> 11 & 1];
  const uint32_t *const pw = own_w ? own_w : pool->w, cap = own_w ? own_cap : pool->cap;
  uint32_t off = m[2], guard = 0;
  while (off != ND_END && pw && off + 2 <= cap && guard++ < FRAME_MAX) {
    const uint32_t *r = pw + off, kind = r[1], body = ND_BODY(kind);
    if (off + 2 + body > cap)
      break;
    put(f, 0xEC000000u | kind);
    if (kind <= ND_K_GT4)
      put_poly(f, r + 2, body);
    else
      put_words(f, r + 2, body * 4);
    f->records++;
    off = r[0];
  }
}

void pres_check_frame(int which, const void *ot_head, const DRAWENV *de, const DISPENV *dd,
                      const uint32_t *pool, uint32_t pool_cap) {
  Frame *f = &s_frame[which & 1];
  const uint32_t *p = ot_head;
  uint32_t guard = 0;
  if (!f->w && !(f->w = malloc(FRAME_MAX * sizeof(uint32_t))))
    return;
  if (f->on) /* one list per frame in play: a second is recorded after it */
    put(f, 0xEEEEEEEEu);
  f->on = 1;
  put_words(f, de, sizeof *de);
  put_words(f, dd, sizeof *dd);
  for (; p && guard++ < FRAME_MAX; p = next_packet(p)) {
    const uint32_t len = *p >> 24;
    if (!len)
      continue;
    if (len == 2 && (p[1] & 0xFFF000u) == ND_MAGIC && !(p[1] >> 24)) {
      records(f, p, pool, pool_cap);
      continue;
    }
    put(f, 0xED000000u | len);
    if ((p[1] >> 29) == 1) /* a polygon (GP0 0x20-0x3F) */
      put_poly(f, p + 1, len);
    else
      put_words(f, p + 1, len * 4);
    f->packets++;
  }
}

static void reset(void) {
  int i;
  for (i = 0; i < 2; i++) {
    s_frame[i].n = 0;
    s_frame[i].full = s_frame[i].on = 0;
    s_frame[i].packets = s_frame[i].records = 0;
  }
}

void pres_check_compare(void) {
  const Frame *d = &s_frame[0], *p = &s_frame[1];
  uint32_t i, n = d->n < p->n ? d->n : p->n;
  s_ticks++;
  if (!d->on || !p->on) {
    pres_check_skip(d->on ? 1 : 2);
    return;
  }
  for (i = 0; i < n && d->w[i] == p->w[i]; i++)
    ;
  s_packets += d->packets;
  s_records += d->records;
  if (i == n && d->n == p->n && !d->full && !p->full) {
    s_same++;
  } else {
    s_differ++;
    if (s_shown < SHOW && s_log) {
      const char *dump = getenv("PRES_CHECK_DUMP"); /* debug: the first pair's words */
      if (dump && !s_shown) {
        int k;
        for (k = 0; k < 2; k++) {
          char path[512];
          FILE *fp;
          snprintf(path, sizeof path, "%s.%s", dump, k ? "pres" : "d30");
          if ((fp = fopen(path, "w"))) {
            uint32_t j;
            for (j = 0; j < s_frame[k].n; j++)
              fprintf(fp, "%08X\n", s_frame[k].w[j]);
            fclose(fp);
          }
        }
      }
      s_shown++;
      s_log("pres check: frame %lu differs at word %u of %u (D30's) / %u (presenter's): "
            "%08X / %08X%s",
            s_ticks, i, d->n, p->n, i < d->n ? d->w[i] : 0, i < p->n ? p->w[i] : 0,
            d->full || p->full ? " (a record full)" : "");
    }
  }
  reset();
}

/* why: 0 not play, 1 the presenter drew nothing, 2 D30's pass drew
 * nothing, 3 the camera jumped, 4 the presenter not there (its area,
 * fast_render) */
void pres_check_skip(int why) {
  s_skipped[why >= 0 && why < 5 ? why : 0]++;
  reset();
}

int pres_check_report(void (*log)(const char *fmt, ...)) {
  if (log)
    log("pres check: %lu frames compared, %lu identical, %lu differ (%lu packets, %lu records "
        "in D30's); not compared: %lu other gamestates, %lu without the presenter's, %lu without "
        "D30's, %lu camera jumps, %lu without the presenter",
        s_same + s_differ, s_same, s_differ, s_packets, s_records, s_skipped[0], s_skipped[1],
        s_skipped[2], s_skipped[3], s_skipped[4]);
  return s_differ != 0;
}

void pres_check_set_log(void (*log)(const char *fmt, ...)) { s_log = log; }

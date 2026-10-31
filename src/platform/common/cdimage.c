/* The disc read straight from a raw image in the data folder (psx_cdimage.h,
 * decisions D60). Finding the image, its sector size and first LBA, the
 * ISO9660 walk and the XA test are tools/extract_disc.py's (D58), so the
 * file table is the manifest.json it would write. Reads go through two
 * windows of 32 raw sectors, one fread each, so a level load or the XA
 * stream costs one file-system call per 32 sectors. */
#include "psx_cdimage.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RAW 2352
#define WIN_SECTORS 32
#define MAX_NAMES 256
#define SCAN_BYTES (2448 * 300) /* the extractor's: a pregap of up to ~4 s */

#define XA_FORM2 0x1000
#define XA_INTERLEAVED 0x2000
#define XA_CDDA 0x4000

/* SHA-1 of a good dump of SCUS-94228 (extract_disc.py's KNOWN). */
#define EXE_NAME "SCUS_942.28"
#define EXE_SHA1 "84e3728ab94720d0873e2514adf4aade4935e0c5"
#define WAD_NAME "WAD.WAD"
#define WAD_SHA1 "ab68001377ea93fb309a5c2a35e772c9d1f57e0b"

static const unsigned char k_sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0xff, 0xff, 0xff, 0};

static struct {
  FILE *fp;
  char path[512];
  char name[128]; /* for messages: the file name alone */
  int sector;     /* 2352 or 2448 */
  int base;       /* LBA of the image's first sector */
  int nsect;      /* whole sectors in the file */
  long size;
} s_img;

static struct {
  int lba, n;
  unsigned stamp;
  unsigned char *buf;
} s_win[2];
static unsigned s_stamp;

static char s_marker[512];
static void (*s_progress)(int percent);

void psx_cd_set_verify(const char *marker_path, void (*progress)(int percent)) {
  snprintf(s_marker, sizeof s_marker, "%s", marker_path ? marker_path : "");
  s_progress = progress;
}

const char *psx_cdimage_path(void) { return s_img.fp ? s_img.path : NULL; }

/* ---- SHA-1 (FIPS 180-4) ---- */

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_block(PsxSha1 *s, const unsigned char *p) {
  unsigned int w[80], a, b, c, d, e, t;
  int i;
  for (i = 0; i < 16; i++)
    w[i] = (unsigned int)p[i * 4] << 24 | (unsigned int)p[i * 4 + 1] << 16 |
           (unsigned int)p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for (; i < 80; i++)
    w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
  for (i = 0; i < 80; i++) {
    if (i < 20)
      t = ((b & c) | (~b & d)) + 0x5a827999;
    else if (i < 40)
      t = (b ^ c ^ d) + 0x6ed9eba1;
    else if (i < 60)
      t = ((b & c) | (b & d) | (c & d)) + 0x8f1bbcdc;
    else
      t = (b ^ c ^ d) + 0xca62c1d6;
    t += ROL(a, 5) + e + w[i];
    e = d, d = c, c = ROL(b, 30), b = a, a = t;
  }
  s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

void psx_sha1_init(PsxSha1 *s) {
  static const unsigned int h0[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476,
                                     0xc3d2e1f0};
  memcpy(s->h, h0, sizeof h0);
  s->len = 0;
}

void psx_sha1_update(PsxSha1 *s, const void *data, unsigned n) {
  const unsigned char *p = data;
  unsigned have = (unsigned)(s->len & 63);
  s->len += n;
  if (have) {
    unsigned k = 64 - have < n ? 64 - have : n;
    memcpy(s->buf + have, p, k);
    p += k, n -= k, have += k;
    if (have < 64)
      return;
    sha1_block(s, s->buf);
  }
  for (; n >= 64; p += 64, n -= 64)
    sha1_block(s, p);
  memcpy(s->buf, p, n);
}

void psx_sha1_hex(PsxSha1 *s, char out[41]) {
  unsigned long long bits = s->len * 8;
  unsigned char pad[72] = {0x80};
  unsigned have = (unsigned)(s->len & 63), k = have < 56 ? 56 - have : 120 - have, i;
  for (i = 0; i < 8; i++)
    pad[k + i] = (unsigned char)(bits >> (56 - 8 * i));
  psx_sha1_update(s, pad, k + 8);
  for (i = 0; i < 20; i++)
    sprintf(out + i * 2, "%02x", (s->h[i / 4] >> (24 - 8 * (i % 4))) & 0xff);
}

/* ---- messages ---- */

static int fail(PsxCdError *err, int code, const char *title, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#include <stdarg.h>
static int fail(PsxCdError *err, int code, const char *title, const char *fmt, ...) {
  va_list ap;
  err->code = code;
  snprintf(err->title, sizeof err->title, "%s", title);
  va_start(ap, fmt);
  vsnprintf(err->text, sizeof err->text, fmt, ap);
  va_end(ap);
  return -1;
}

/* ---- finding the image ---- */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int name_cmp(const void *a, const void *b) {
  const char *x = *(const char *const *)a, *y = *(const char *const *)b;
  for (; *x && lower(*x) == lower(*y); x++, y++)
    ;
  return lower(*x) - lower(*y);
}

static int has_ext(const char *name, const char *ext) {
  size_t n = strlen(name), e = strlen(ext);
  return n > e && name_cmp(&(const char *){name + n - e}, &ext) == 0;
}

static int bcd(int b) { return (b >> 4) * 10 + (b & 15); }

enum { K_RAW, K_ISO, K_CHD, K_PBP, K_ECM, K_UNKNOWN, K_UNREADABLE };

/* What the file is; for K_RAW also its sector size and first LBA (the first
 * sector with a sync pattern whose next sector has one too, as D58). */
static int sniff(const char *path, int *sector, int *base) {
  FILE *f = fopen(path, "rb");
  unsigned char *head;
  int n, kind = K_UNKNOWN;
  if (!f)
    return K_UNREADABLE;
  head = malloc(SCAN_BYTES);
  n = head ? (int)fread(head, 1, SCAN_BYTES, f) : 0;
  if (n >= 8 && memcmp(head, "MComprHD", 8) == 0)
    kind = K_CHD;
  else if (n >= 4 && memcmp(head, "\0PBP", 4) == 0)
    kind = K_PBP;
  else if (n >= 4 && memcmp(head, "ECM\0", 4) == 0)
    kind = K_ECM;
  for (int size = RAW; kind == K_UNKNOWN && size <= 2448; size += 2448 - RAW)
    for (int k = 0; (k + 2) * size <= n; k++) {
      const unsigned char *at = head + k * size;
      if (memcmp(at, k_sync, 12) == 0 && memcmp(at + size, k_sync, 12) == 0) {
        kind = K_RAW;
        *sector = size;
        *base = (bcd(at[12]) * 60 + bcd(at[13])) * 75 + bcd(at[14]) - 150 - k;
        break;
      }
    }
  if (kind == K_UNKNOWN && n >= 16 * 2048 + 6 && memcmp(head + 16 * 2048 + 1, "CD001", 5) == 0)
    kind = K_ISO;
  free(head);
  fclose(f);
  return kind;
}

/* A word at the start of p, ignoring case. */
static int starts(const char *p, const char *word) {
  for (; *word; p++, word++)
    if (lower(*p) != lower(*word))
      return 0;
  return 1;
}

/* The image a .cue's first FILE line names, next to it; its first TRACK must
 * be 1, MODE2/2352 (or 2448), as extract_disc.py wants. A name that differs
 * only in case is taken too (the Vita's memory card ignores case, Linux
 * doesn't), and a folder in it is dropped. */
static int resolve_cue(const char *dir, const char *cue, char **names, int nnames, char *out,
                       size_t cap, PsxCdError *err) {
  char path[512], text[4096], want[256] = "";
  const char *p, *q, *base;
  int n, i, track_ok = -1;
  FILE *f;
  snprintf(path, sizeof path, "%s/%s", dir, cue);
  if (!(f = fopen(path, "rb")))
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE", "%s can't be opened.", cue);
  n = (int)fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[n < 0 ? 0 : n] = 0;
  for (p = text; *p; p = *q ? q + 1 : q) {
    q = p + strcspn(p, "\r\n");
    while (*p == ' ' || *p == '\t')
      p++;
    if (!want[0] && starts(p, "FILE") && (p[4] == ' ' || p[4] == '\t')) {
      const char *s = p + 4, *e;
      while (*s == ' ' || *s == '\t')
        s++;
      if (*s == '"')
        e = memchr(++s, '"', (size_t)(q - s));
      else
        for (e = s; e < q && *e != ' ' && *e != '\t'; e++)
          ;
      if (e && e > s && (size_t)(e - s) < sizeof want) {
        memcpy(want, s, (size_t)(e - s));
        want[e - s] = 0;
      }
    } else if (track_ok < 0 && starts(p, "TRACK")) {
      char *e;
      long t = strtol(p + 5, &e, 10);
      while (*e == ' ' || *e == '\t')
        e++;
      track_ok = t == 1 && (starts(e, "MODE2/2352") || starts(e, "MODE2/2448"));
    }
  }
  if (!want[0])
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE CUE FILE",
                "%s has no FILE line this port can read. Make the .cue again, or copy the .bin "
                "alone.",
                cue);
  if (track_ok != 1)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE CUE FILE",
                "%s: track 1 isn't MODE2/2352, as a PlayStation data track should be.", cue);
  for (p = base = want; *p; p++)
    if (*p == '/' || *p == '\\')
      base = p + 1;
  for (i = 0; i < nnames; i++)
    if (name_cmp(&base, &names[i]) == 0) {
      snprintf(out, cap, "%s/%s", dir, names[i]);
      return 0;
    }
  return fail(err, PSX_CD_ERR_BAD_IMAGE, "THE .BIN IS MISSING",
              "%s names %s, which isn't in %s/. Copy both files.", cue, base, dir);
}

static int open_image(const char *path, const char *name, int sector, int base,
                      PsxCdError *err) {
  FILE *f = fopen(path, "rb");
  long size;
  if (!f)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE", "%s can't be opened.", name);
  if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0) {
    fclose(f);
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE",
                "%s can't be read (or is over 2 GB).", name);
  }
  setvbuf(f, NULL, _IONBF, 0); /* the windows are the buffer */
  memset(&s_img, 0, sizeof s_img);
  s_img.fp = f;
  snprintf(s_img.path, sizeof s_img.path, "%s", path);
  snprintf(s_img.name, sizeof s_img.name, "%s", name);
  s_img.sector = sector;
  s_img.base = base;
  s_img.size = size;
  s_img.nsect = (int)(size / sector);
  for (int i = 0; i < 2; i++) {
    if (!s_win[i].buf)
      s_win[i].buf = malloc((size_t)WIN_SECTORS * 2448);
    s_win[i].n = 0;
  }
  return 0;
}

static void say(const char *what) { fprintf(stderr, "libcd: %s\n", what); }

/* The first usable image in dir: .cue files first, then .bin/.img (and any
 * other candidate whose data is a raw image), each in name order ignoring
 * case. With none, the first candidate's reason. */
static int find_image(const char *dir, PsxCdError *err) {
  static const char *const exts[] = {".cue", ".bin", ".img", ".iso", ".chd", ".pbp", ".ecm"};
  char *names[MAX_NAMES], path[512], msg[700];
  int nnames = 0, i, pass, found = -1, sector = 0, base = 0;
  PsxCdError first = {0};
  DIR *d = opendir(dir);
  struct dirent *e;
  if (!d)
    return fail(err, PSX_CD_ERR_NOT_FOUND, "NO DISC IMAGE FOUND",
                "Copy your Spyro the Dragon (USA) disc image to %s/: the .bin and .cue of a "
                "raw dump (a CloneCD .img works too). See the README.",
                dir);
  while ((e = readdir(d)) && nnames < MAX_NAMES)
    for (i = 0; i < (int)(sizeof exts / sizeof *exts); i++)
      if (has_ext(e->d_name, exts[i])) {
        names[nnames++] = strdup(e->d_name);
        break;
      }
  closedir(d);
  qsort(names, (size_t)nnames, sizeof *names, name_cmp);

  for (pass = 0; pass < 2 && found < 0; pass++)
    for (i = 0; i < nnames && found < 0; i++) {
      const char *name = names[i];
      PsxCdError why = {0};
      int kind;
      if ((pass == 0) != has_ext(name, ".cue"))
        continue;
      if (pass == 0) {
        if (resolve_cue(dir, name, names, nnames, path, sizeof path, &why) == 0) {
          kind = sniff(path, &sector, &base);
          if (kind == K_RAW) {
            const char *bin = strrchr(path, '/') + 1;
            snprintf(msg, sizeof msg, "disc image %s (from %s), %d-byte sectors, first LBA %d",
                     path, name, sector, base);
            say(msg);
            if (open_image(path, bin, sector, base, err) == 0)
              found = i;
            else
              return -1;
            continue;
          }
          fail(&why, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE",
               "%s names %s, which isn't a raw CD image. Dump the disc as .bin + .cue "
               "(MODE2/2352).",
               name, strrchr(path, '/') + 1);
        }
      } else {
        snprintf(path, sizeof path, "%s/%s", dir, name);
        kind = sniff(path, &sector, &base);
        switch (kind) {
        case K_RAW:
          snprintf(msg, sizeof msg, "disc image %s, %d-byte sectors, first LBA %d", path, sector,
                   base);
          say(msg);
          if (open_image(path, name, sector, base, err) != 0)
            return -1;
          found = i;
          continue;
        case K_ISO:
          fail(&why, PSX_CD_ERR_ISO, "AN ISO HAS NO MUSIC",
               "%s is a 2048-byte .iso, which has lost the game's music. Dump the disc again as "
               ".bin + .cue (raw, MODE2/2352) and copy both to %s/.",
               name, dir);
          break;
        case K_CHD:
          fail(&why, PSX_CD_ERR_CONVERT, "CONVERT THE CHD FIRST",
               "%s is a CHD. Convert it on a PC with MAME's chdman:\n"
               "chdman extractcd -i game.chd -o game.cue\n"
               "then copy the .bin and .cue to %s/.",
               name, dir);
          break;
        case K_PBP:
          fail(&why, PSX_CD_ERR_CONVERT, "A PBP CAN'T BE USED",
               "%s is a PSP EBOOT.PBP, which has lost the game's music. Dump your disc as "
               ".bin + .cue (MODE2/2352) and copy both to %s/.",
               name, dir);
          break;
        case K_ECM:
          fail(&why, PSX_CD_ERR_CONVERT, "UNPACK THE ECM FIRST",
               "%s is an ECM file. Unpack it on a PC with unecm (it gives the .bin back), then "
               "copy the .bin and .cue to %s/.",
               name, dir);
          break;
        case K_UNREADABLE:
          fail(&why, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE", "%s can't be opened.", name);
          break;
        default:
          fail(&why, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE",
               "%s isn't a raw CD image. Dump the disc as .bin + .cue (MODE2/2352).", name);
          break;
        }
      }
      snprintf(msg, sizeof msg, "%s: %s", name, why.title);
      say(msg);
      if (!first.code)
        first = why;
    }
  for (i = 0; i < nnames; i++)
    free(names[i]);
  if (found >= 0)
    return 0;
  if (first.code) {
    *err = first;
    return -1;
  }
  return fail(err, PSX_CD_ERR_NOT_FOUND, "NO DISC IMAGE FOUND",
              "Copy your Spyro the Dragon (USA) disc image to %s/: the .bin and .cue of a raw "
              "dump (a CloneCD .img works too). See the README.",
              dir);
}

/* ---- reading ---- */

int psx_cdimage_read(int lba, int off, void *buf, int n) {
  int rel = lba - s_img.base, w;
  unsigned long long t0;
  if (!s_img.fp || rel < 0 || rel >= s_img.nsect || off < 0 || off + n > RAW)
    return -1;
  for (w = 0; w < 2; w++)
    if (s_win[w].n && lba >= s_win[w].lba && lba < s_win[w].lba + s_win[w].n)
      break;
  if (w == 2) { /* refill the one used longest ago */
    int count = s_img.nsect - rel < WIN_SECTORS ? s_img.nsect - rel : WIN_SECTORS, got;
    w = s_win[0].stamp <= s_win[1].stamp ? 0 : 1;
    t0 = psx_cd_now();
    s_win[w].n = 0;
    if (fseek(s_img.fp, (long)rel * s_img.sector, SEEK_SET) != 0)
      return -1;
    got = (int)fread(s_win[w].buf, 1, (size_t)count * s_img.sector, s_img.fp);
    psx_cd_io_add(got > 0 ? (unsigned)got : 0, t0);
    s_win[w].lba = lba;
    s_win[w].n = got > 0 ? got / s_img.sector : 0;
    if (!s_win[w].n)
      return -1;
  }
  s_win[w].stamp = ++s_stamp;
  memcpy(buf, s_win[w].buf + (size_t)(lba - s_win[w].lba) * s_img.sector + off, (size_t)n);
  return n;
}

void psx_cdimage_close(void) {
  if (s_img.fp)
    fclose(s_img.fp);
  memset(&s_img, 0, sizeof s_img);
  s_win[0].n = s_win[1].n = 0;
}

static int read_form1(int lba, int count, unsigned char *out) {
  for (int i = 0; i < count; i++)
    if (psx_cdimage_read(lba + i, 24, out + i * 2048, 2048) != 2048)
      return -1;
  return 0;
}

/* ---- the ISO9660 directory, as extract_disc.py walks it ---- */

typedef struct {
  PsxCdImageFile *v;
  int n, cap, bad_lba;
} Table;

static int add(Table *t, const PsxCdImageFile *f) {
  if (t->n == t->cap) {
    PsxCdImageFile *v = realloc(t->v, sizeof *v * (size_t)(t->cap = t->cap ? t->cap * 2 : 32));
    if (!v)
      return -1;
    t->v = v;
  }
  t->v[t->n++] = *f;
  return 0;
}

static int walk(Table *t, const char *rel, int lba, int size, int depth) {
  int nsect = (size + 2047) / 2048, pos = 0;
  unsigned char *data;
  if (depth > 8 || nsect <= 0 || nsect > 64)
    return -1;
  data = malloc((size_t)nsect * 2048);
  if (!data || read_form1(lba, nsect, data) != 0) {
    t->bad_lba = lba;
    free(data);
    return -1;
  }
  while (pos < size) {
    const unsigned char *rec = data + pos;
    int len = rec[0], name_len, xa_attr = 0, su;
    PsxCdImageFile f;
    char name[64];
    if (len == 0) {
      pos = (pos / 2048 + 1) * 2048;
      continue;
    }
    if (len < 34 || pos + len > nsect * 2048)
      break;
    name_len = rec[32];
    su = 33 + name_len + (1 - name_len % 2);
    if (len - su >= 14 && rec[su + 6] == 'X' && rec[su + 7] == 'A')
      xa_attr = rec[su + 4] << 8 | rec[su + 5];
    pos += len;
    if (name_len == 1 && rec[33] <= 1) /* . and .. */
      continue;
    if (name_len >= (int)sizeof name)
      continue;
    memcpy(name, rec + 33, (size_t)name_len);
    name[name_len] = 0;
    if (strchr(name, ';'))
      *strchr(name, ';') = 0;
    memset(&f, 0, sizeof f);
    snprintf(f.path, sizeof f.path, "%s%s%s", rel, *rel ? "/" : "", name);
    f.lba = rec[2] | rec[3] << 8 | rec[4] << 16 | rec[5] << 24;
    f.size = rec[10] | rec[11] << 8 | rec[12] << 16 | rec[13] << 24;
    if (rec[25] & 2) {
      if (walk(t, f.path, f.lba, f.size, depth + 1) != 0) {
        free(data);
        return -1;
      }
      continue;
    }
    if (xa_attr & XA_CDDA)
      continue; /* none on this disc; the extractor refuses them */
    f.xa = (xa_attr & (XA_FORM2 | XA_INTERLEAVED)) != 0;
    if (!f.xa && f.size > 0) { /* the first sector's submode says Form 2 */
      unsigned char sub[8];
      if (psx_cdimage_read(f.lba, 16, sub, 8) == 8 && (sub[2] & 0x20))
        f.xa = 1;
    }
    if (add(t, &f) != 0) {
      free(data);
      return -1;
    }
  }
  free(data);
  return 0;
}

static int by_lba(const void *a, const void *b) {
  const PsxCdImageFile *x = a, *y = b;
  return (x->lba > y->lba) - (x->lba < y->lba);
}

/* ---- is it SCUS-94228? ---- */

static const PsxCdImageFile *lookup(const Table *t, const char *path) {
  for (int i = 0; i < t->n; i++)
    if (name_cmp(&(const char *){t->v[i].path}, &path) == 0)
      return &t->v[i];
  return NULL;
}

static int hash_file(const PsxCdImageFile *f, char hex[41], int progress) {
  static unsigned char sec[2048];
  PsxSha1 s;
  int nsect = (f->size + 2047) / 2048, last = -1;
  psx_sha1_init(&s);
  for (int i = 0; i < nsect; i++) {
    int n = f->size - i * 2048 < 2048 ? f->size - i * 2048 : 2048;
    if (psx_cdimage_read(f->lba + i, 24, sec, 2048) != 2048)
      return -1;
    psx_sha1_update(&s, sec, (unsigned)n);
    if (progress && s_progress && i * 100 / nsect != last)
      s_progress(last = i * 100 / nsect);
  }
  psx_sha1_hex(&s, hex);
  return 0;
}

/* SYSTEM.CNF's BOOT line as a serial ("SCES-00900"), and the region. */
static void boot_serial(const Table *t, char *serial, size_t cap, const char **region) {
  const PsxCdImageFile *cnf = lookup(t, "SYSTEM.CNF");
  unsigned char buf[2048 + 1];
  const char *p;
  size_t n = 0;
  serial[0] = 0;
  *region = "";
  if (!cnf || cnf->size <= 0 || read_form1(cnf->lba, 1, buf) != 0)
    return;
  buf[cnf->size < 2048 ? cnf->size : 2048] = 0;
  if (!(p = strstr((char *)buf, "cdrom:")))
    return;
  for (p += 6; *p == '\\' || *p == '/'; p++)
    ;
  for (; *p && *p != ';' && *p > ' ' && n + 2 < cap; p++) {
    if (*p == '\\' || *p == '/')
      n = 0; /* a folder: keep the last part */
    else if (*p != '.' && *p != '_')
      serial[n++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    if (n == 4 && *p != '\\' && *p != '/')
      serial[n++] = '-';
  }
  serial[n] = 0;
  if (!strncmp(serial, "SCUS", 4) || !strncmp(serial, "SLUS", 4))
    *region = "USA";
  else if (serial[0] == 'S' && serial[2] == 'E')
    *region = "PAL, Europe";
  else if (serial[0] == 'S' && (serial[2] == 'P' || serial[2] == 'J'))
    *region = "Japan";
}

static int verified_before(char *key, size_t cap) {
  struct stat st;
  char line[700] = "";
  FILE *f;
  if (stat(s_img.path, &st) != 0)
    return 0;
  snprintf(key, cap, "%ld %lld %s", s_img.size, (long long)st.st_mtime, s_img.path);
  if (!(f = fopen(s_marker, "rb")))
    return 0;
  if (!fgets(line, sizeof line, f))
    line[0] = 0;
  fclose(f);
  line[strcspn(line, "\r\n")] = 0;
  return strcmp(line, key) == 0;
}

static int check_disc(const Table *t, PsxCdError *err) {
  const PsxCdImageFile *exe = lookup(t, EXE_NAME), *wad = lookup(t, WAD_NAME);
  char hex[41], serial[32], key[700], msg[800];
  const char *region;
  if (!exe) {
    boot_serial(t, serial, sizeof serial, &region);
    if (!serial[0])
      return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG DISC",
                  "%s isn't a PlayStation game disc. Spyro Vita needs Spyro the Dragon (USA), "
                  "SCUS-94228.",
                  s_img.name);
    return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG DISC",
                "This is %s%s%s%s, not Spyro the Dragon (USA). Spyro Vita needs SCUS-94228.",
                serial, *region ? " (" : "", region, *region ? ")" : "");
  }
  if (hash_file(exe, hex, 0) != 0)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "THE IMAGE IS DAMAGED",
                "%s can't be read from %s. Copy the image again, or dump the disc again.",
                EXE_NAME, s_img.name);
  if (strcmp(hex, EXE_SHA1) != 0)
    return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG VERSION OF THE GAME",
                "%s on this disc isn't the one Spyro Vita needs (SHA-1 %.8s, needs %.8s): "
                "another release of the game (Greatest Hits, a demo) or a bad dump.",
                EXE_NAME, hex, EXE_SHA1);
  if (!s_marker[0])
    return 0;
  if (!wad)
    return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG DISC", "This disc has no %s.", WAD_NAME);
  if (verified_before(key, sizeof key)) {
    say("WAD.WAD checked before (same image, size and date)");
    return 0;
  }
  if (hash_file(wad, hex, 1) != 0)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "THE IMAGE IS DAMAGED",
                "%s can't be read from %s. Copy the image again, or dump the disc again.",
                WAD_NAME, s_img.name);
  if (strcmp(hex, WAD_SHA1) != 0)
    return fail(err, PSX_CD_ERR_WRONG_DISC, "BAD DUMP",
                "%s on this disc doesn't match a good dump of SCUS-94228 (SHA-1 %.8s, needs "
                "%.8s). Dump the disc again.",
                WAD_NAME, hex, WAD_SHA1);
  if (key[0]) {
    FILE *f = fopen(s_marker, "wb");
    if (f) {
      fprintf(f, "%s\n", key);
      fclose(f);
    }
  }
  snprintf(msg, sizeof msg, "WAD.WAD matches a good dump; marker %s", s_marker);
  say(msg);
  return 0;
}

/* The image ends before sector need (an LBA): copied or dumped only in part. */
static int cut_short(PsxCdError *err, int need) {
  return fail(err, PSX_CD_ERR_BAD_IMAGE, "THE IMAGE IS CUT SHORT",
              "%s is cut short: %ld MB of the %ld MB or more the game needs. Copy it again, or "
              "dump the disc again.",
              s_img.name, s_img.size >> 20,
              (long)((long long)(need - s_img.base) * s_img.sector >> 20));
}

int psx_cdimage_open(const char *dir, PsxCdImageFile **files, int *nfiles, PsxCdError *err) {
  unsigned char pvd[2048];
  Table t = {0};
  int i, need = 0;
  psx_cdimage_close();
  if (find_image(dir, err) != 0)
    return -1;
  if (read_form1(16, 1, pvd) != 0 || memcmp(pvd + 1, "CD001", 5) != 0) {
    fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE",
         "%s has no CD file system where a PlayStation disc has one. Dump the disc again as "
         ".bin + .cue (MODE2/2352).",
         s_img.name);
    goto bad;
  }
  {
    const unsigned char *root = pvd + 156;
    int lba = root[2] | root[3] << 8 | root[4] << 16 | root[5] << 24;
    int size = root[10] | root[11] << 8 | root[12] << 16 | root[13] << 24;
    t.bad_lba = lba;
    if (walk(&t, "", lba, size, 0) != 0 || t.n == 0) {
      if (t.bad_lba - s_img.base >= s_img.nsect) {
        cut_short(err, t.bad_lba + 1);
        goto bad;
      }
      fail(err, PSX_CD_ERR_BAD_IMAGE, "THE IMAGE IS DAMAGED",
           "%s: the disc's file list can't be read (sector %d). Copy the image again, or dump "
           "the disc again.",
           s_img.name, t.bad_lba);
      goto bad;
    }
  }
  qsort(t.v, (size_t)t.n, sizeof *t.v, by_lba);
  for (i = 0; i < t.n; i++) {
    int end = t.v[i].lba + (t.v[i].size + 2047) / 2048;
    if (end > need)
      need = end;
  }
  if (need - s_img.base > s_img.nsect) {
    cut_short(err, need);
    goto bad;
  }
  if (check_disc(&t, err) != 0)
    goto bad;
  *files = t.v;
  *nfiles = t.n;
  return 0;
bad:
  free(t.v);
  psx_cdimage_close();
  return -1;
}

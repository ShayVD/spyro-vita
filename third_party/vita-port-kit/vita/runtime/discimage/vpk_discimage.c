/* The disc image layer (vpk_discimage.h).
 * From kh1-decomp host/port/cd_image.c @ 85befa4a and spyro-vita src/platform/common/cdimage.c @ 987363e. */
#define _FILE_OFFSET_BITS 64 /* 32-bit hosts: off_t and fseeko past 2 GiB */
#include "vpk_discimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

#define WIN_SECTORS 32
#define SCAN_BYTES (2448 * 300) /* a pregap of up to ~4 s */
#define RAW 2352                /* what a raw sector serves: sync, header, subheader, data, EDC / ECC */

static const unsigned char k_sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};

/* ---- the file: sceIo on the Vita, stdio elsewhere ---- */

#ifdef __vita__
typedef int File;
#define NO_FILE (-1)
static File f_open(const char *path) { int fd = sceIoOpen(path, SCE_O_RDONLY, 0); return fd < 0 ? NO_FILE : fd; }
static void f_close(File f) { sceIoClose(f); }
static int f_seek(File f, long long at) { return sceIoLseek(f, at, SCE_SEEK_SET) == at ? 0 : -1; }
static long long f_size(File f) { return sceIoLseek(f, 0, SCE_SEEK_END); }
static int f_read(File f, void *buf, int n) { return sceIoRead(f, buf, (unsigned)n); }
#else
typedef FILE *File;
#define NO_FILE NULL
static File f_open(const char *path) { return fopen(path, "rb"); }
static void f_close(File f) { fclose(f); }
static int f_seek(File f, long long at) { return fseeko(f, (off_t)at, SEEK_SET); }
static long long f_size(File f) { return fseeko(f, 0, SEEK_END) == 0 ? (long long)ftello(f) : -1; }
static int f_read(File f, void *buf, int n) { return (int)fread(buf, 1, (size_t)n, f); }
#endif

#ifdef __vita__
#define D_FILE(d) ((d)->fd)
#else
#define D_FILE(d) ((File)(d)->fp)
#endif
#define D_OPEN(d) ((d)->open)

/* ---- what the file is ---- */

static int bcd(int b) { return (b >> 4) * 10 + (b & 15); }

int vpk_disc_sniff(const char *path, VpkDiscLayout *l) {
  File f = f_open(path);
  unsigned char *head;
  int n, kind = VPK_DISC_UNKNOWN, size, k;
  if (f == NO_FILE)
    return VPK_DISC_UNREADABLE;
  head = malloc(SCAN_BYTES);
  n = head ? f_read(f, head, SCAN_BYTES) : 0;
  f_close(f);
  if (n >= 8 && memcmp(head, "MComprHD", 8) == 0)
    kind = VPK_DISC_CHD;
  else if (n >= 4 && memcmp(head, "\0PBP", 4) == 0)
    kind = VPK_DISC_PBP;
  else if (n >= 4 && memcmp(head, "ECM\0", 4) == 0)
    kind = VPK_DISC_ECM;
  /* raw: the first sector with a sync pattern whose next sector has one too; its MSF header gives the first LBA */
  for (size = RAW; kind == VPK_DISC_UNKNOWN && size <= 2448; size += 2448 - RAW)
    for (k = 0; (k + 2) * size <= n; k++) {
      const unsigned char *at = head + k * size;
      if (memcmp(at, k_sync, 12) == 0 && memcmp(at + size, k_sync, 12) == 0) {
        kind = VPK_DISC_RAW;
        l->sector = size;
        l->base = (bcd(at[12]) * 60 + bcd(at[13])) * 75 + bcd(at[14]) - 150 - k;
        /* sector 16's mode (where the volume descriptor is): 1 has its data at 16, 2 (form 1) at 24 */
        k = 16 - l->base;
        l->data_off = k >= 0 && (k + 1) * size <= n && head[k * size + 15] == 1 ? 16 : 24;
        break;
      }
    }
  if (kind == VPK_DISC_UNKNOWN && n >= 16 * 2048 + 6 && memcmp(head + 16 * 2048 + 1, "CD001", 5) == 0) {
    kind = VPK_DISC_ISO;
    l->sector = 2048;
    l->data_off = 0;
    l->base = 0;
  }
  free(head);
  return kind;
}

/* ---- opening ---- */

int vpk_disc_open(VpkDisc *d, const char *path, const VpkDiscLayout *l, int windows) {
  File f = f_open(path);
  long long size;
  int i;
  unsigned char *keep[2] = {d->win[0].buf, d->win[1].buf};
  unsigned long long (*now)(void) = d->now;
  void (*io)(unsigned, unsigned long long) = d->io;
  memset(d, 0, sizeof *d);
  d->now = now;
  d->io = io;
  d->win[0].buf = keep[0];
  d->win[1].buf = keep[1];
  if (f == NO_FILE)
    return -1;
  if ((size = f_size(f)) <= 0) {
    f_close(f);
    return -2;
  }
  d->open = 1;
#ifdef __vita__
  d->fd = f;
#else
  d->fp = f;
  if (windows)
    setvbuf(f, NULL, _IONBF, 0); /* the windows are the buffer */
#endif
  d->l = *l;
  d->size = size;
  d->nsect = (int)(size / l->sector);
  d->windows = windows;
  for (i = 0; windows && i < 2; i++)
    if (!d->win[i].buf)
      d->win[i].buf = malloc((size_t)WIN_SECTORS * 2448);
  return 0;
}

int vpk_disc_is_open(const VpkDisc *d) { return D_OPEN(d); }

void vpk_disc_close(VpkDisc *d) {
  if (D_OPEN(d))
    f_close(D_FILE(d));
  d->open = 0;
  d->fp = NULL;
  d->win[0].n = d->win[1].n = 0;
}

int vpk_disc_open_any(VpkDisc *d, const char *path, int windows) {
  VpkDiscLayout l;
  unsigned char pvd[2048];
  int kind = vpk_disc_sniff(path, &l);
  if (kind != VPK_DISC_RAW && kind != VPK_DISC_ISO)
    return -1;
  if (vpk_disc_open(d, path, &l, windows) != 0)
    return -1;
  /* the primary volume descriptor at sector 16: type 1, "CD001" */
  if (vpk_disc_read(d, 16, 1, pvd) != 0 || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
    vpk_disc_close(d);
    return -1;
  }
  return 0;
}

/* ---- reading ---- */

static unsigned long long now(VpkDisc *d) { return d->now ? d->now() : 0; }

int vpk_disc_read_raw(VpkDisc *d, int lba, int off, void *buf, int n) {
  int rel = lba - d->l.base, w, limit = d->l.sector < RAW ? d->l.sector : RAW;
  unsigned long long t0;
  if (!D_OPEN(d) || rel < 0 || rel >= d->nsect || off < 0 || n < 0 || off + n > limit)
    return -1;
  if (!d->windows) {
    int got;
    t0 = now(d);
    if (f_seek(D_FILE(d), (long long)rel * d->l.sector + off) != 0)
      return -1;
    got = f_read(D_FILE(d), buf, n);
    if (d->io)
      d->io(got > 0 ? (unsigned)got : 0, t0);
    return got == n ? n : -1;
  }
  for (w = 0; w < 2; w++)
    if (d->win[w].n && lba >= d->win[w].lba && lba < d->win[w].lba + d->win[w].n)
      break;
  if (w == 2) { /* refill the one used longest ago */
    int count = d->nsect - rel < WIN_SECTORS ? d->nsect - rel : WIN_SECTORS, got;
    w = d->win[0].stamp <= d->win[1].stamp ? 0 : 1;
    if (!d->win[w].buf)
      return -1;
    t0 = now(d);
    d->win[w].n = 0;
    if (f_seek(D_FILE(d), (long long)rel * d->l.sector) != 0)
      return -1;
    got = f_read(D_FILE(d), d->win[w].buf, count * d->l.sector);
    if (d->io)
      d->io(got > 0 ? (unsigned)got : 0, t0);
    d->win[w].lba = lba;
    d->win[w].n = got > 0 ? got / d->l.sector : 0;
    if (!d->win[w].n)
      return -1;
  }
  d->win[w].stamp = ++d->stamp;
  memcpy(buf, d->win[w].buf + (size_t)(lba - d->win[w].lba) * d->l.sector + off, (size_t)n);
  return n;
}

int vpk_disc_read(VpkDisc *d, int lba, int count, void *buf) {
  unsigned char *p = buf;
  int i;
  for (i = 0; i < count; i++, p += 2048)
    if (vpk_disc_read_raw(d, lba + i, d->l.data_off, p, 2048) != 2048)
      return -1;
  return 0;
}

/* ---- ISO9660 ---- */

static unsigned le32(const unsigned char *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* an ISO name ("SYSTEM.CNF;1", n bytes) against one component of the path (want[0..wn)), case-insensitive, either
 * side's ";version" ignored */
static int same_name(const unsigned char *iso, int n, const char *want, int wn) {
  int i;
  for (i = 0; i < n; i++)
    if (iso[i] == ';') {
      n = i;
      break;
    }
  for (i = 0; i < wn; i++)
    if (want[i] == ';') {
      wn = i;
      break;
    }
  if (n != wn)
    return 0;
  for (i = 0; i < n; i++)
    if (upper(iso[i]) != upper((unsigned char)want[i]))
      return 0;
  return 1;
}

/* the record named want[0..wn) in the directory at (lba, size): its extent, size and whether it is a directory */
static int find_in_dir(VpkDisc *d, unsigned lba, unsigned size, const char *want, int wn, unsigned *out_lba,
                       unsigned *out_size, int *is_dir) {
  unsigned char sec[2048];
  unsigned done;
  for (done = 0; done < size; done += 2048, lba++) {
    unsigned i = 0;
    if (vpk_disc_read(d, (int)lba, 1, sec) != 0)
      return 0;
    /* records do not cross sectors; a zero length ends the sector's list */
    while (i < 2048 && sec[i] != 0) {
      const unsigned char *r = sec + i;
      int nl = r[32];
      if (i + r[0] > 2048)
        break;
      /* names 0 and 1 are "." and ".." */
      if (!(nl == 1 && r[33] <= 1) && same_name(r + 33, nl, want, wn)) {
        *out_lba = le32(r + 2);
        *out_size = le32(r + 10);
        *is_dir = (r[25] & 2) != 0;
        return 1;
      }
      i += r[0];
    }
  }
  return 0;
}

int vpk_disc_find(VpkDisc *d, const char *name, unsigned *lsn, unsigned *size) {
  unsigned char pvd[2048];
  unsigned lba, sz;
  int dir = 1;
  if (!D_OPEN(d) || vpk_disc_read(d, 16, 1, pvd) != 0)
    return -1;
  /* the root directory's record in the primary volume descriptor */
  lba = le32(pvd + 156 + 2);
  sz = le32(pvd + 156 + 10);
  while (*name == '\\' || *name == '/')
    name++;
  while (*name) {
    int n = 0;
    if (!dir)
      return -1;
    while (name[n] && name[n] != '\\' && name[n] != '/')
      n++;
    if (!find_in_dir(d, lba, sz, name, n, &lba, &sz, &dir))
      return -1;
    name += n;
    while (*name == '\\' || *name == '/')
      name++;
  }
  if (dir)
    return -1;
  *lsn = lba;
  *size = sz;
  return 0;
}

static int walk(VpkDisc *d, const char *rel, int lba, int size, int depth,
                int (*fn)(void *user, const VpkDiscEntry *e), void *user) {
  int nsect = (size + 2047) / 2048, pos = 0, r;
  unsigned char *data;
  if (depth > 8 || nsect <= 0 || nsect > 64)
    return -1;
  data = malloc((size_t)nsect * 2048);
  if (!data || vpk_disc_read(d, lba, nsect, data) != 0) {
    d->bad_lba = lba;
    free(data);
    return -1;
  }
  while (pos < size) {
    const unsigned char *rec = data + pos;
    int len = rec[0], name_len, xa_attr = 0, su;
    VpkDiscEntry e;
    char name[64], path[128];
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
    snprintf(path, sizeof path, "%s%s%s", rel, *rel ? "/" : "", name);
    e.path = path;
    e.lba = (int)le32(rec + 2);
    e.size = (int)le32(rec + 10);
    e.xa_attr = xa_attr;
    if (rec[25] & 2)
      r = walk(d, path, e.lba, e.size, depth + 1, fn, user);
    else
      r = fn(user, &e);
    if (r != 0) {
      free(data);
      return r;
    }
  }
  free(data);
  return 0;
}

int vpk_disc_walk(VpkDisc *d, int (*fn)(void *user, const VpkDiscEntry *e), void *user) {
  unsigned char pvd[2048];
  int lba, size;
  d->bad_lba = 16;
  if (vpk_disc_read(d, 16, 1, pvd) != 0)
    return -1;
  lba = (int)le32(pvd + 156 + 2);
  size = (int)le32(pvd + 156 + 10);
  d->bad_lba = lba;
  return walk(d, "", lba, size, 0, fn, user);
}

/* ---- SHA-1 (FIPS 180-4) ---- */

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_block(VpkSha1 *s, const unsigned char *p) {
  unsigned int w[80], a, b, c, dd, e, t;
  int i;
  for (i = 0; i < 16; i++)
    w[i] = (unsigned int)p[i * 4] << 24 | (unsigned int)p[i * 4 + 1] << 16 | (unsigned int)p[i * 4 + 2] << 8 |
           p[i * 4 + 3];
  for (; i < 80; i++)
    w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  a = s->h[0], b = s->h[1], c = s->h[2], dd = s->h[3], e = s->h[4];
  for (i = 0; i < 80; i++) {
    if (i < 20)
      t = ((b & c) | (~b & dd)) + 0x5a827999;
    else if (i < 40)
      t = (b ^ c ^ dd) + 0x6ed9eba1;
    else if (i < 60)
      t = ((b & c) | (b & dd) | (c & dd)) + 0x8f1bbcdc;
    else
      t = (b ^ c ^ dd) + 0xca62c1d6;
    t += ROL(a, 5) + e + w[i];
    e = dd, dd = c, c = ROL(b, 30), b = a, a = t;
  }
  s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += dd, s->h[4] += e;
}

void vpk_sha1_init(VpkSha1 *s) {
  static const unsigned int h0[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
  memcpy(s->h, h0, sizeof h0);
  s->len = 0;
}

void vpk_sha1_update(VpkSha1 *s, const void *data, unsigned n) {
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

void vpk_sha1_hex(VpkSha1 *s, char out[41]) {
  unsigned long long bits = s->len * 8;
  unsigned char pad[72] = {0x80};
  unsigned have = (unsigned)(s->len & 63), k = have < 56 ? 56 - have : 120 - have, i;
  for (i = 0; i < 8; i++)
    pad[k + i] = (unsigned char)(bits >> (56 - 8 * i));
  vpk_sha1_update(s, pad, k + 8);
  for (i = 0; i < 20; i++)
    sprintf(out + i * 2, "%02x", (s->h[i / 4] >> (24 - 8 * (i % 4))) & 0xff);
}

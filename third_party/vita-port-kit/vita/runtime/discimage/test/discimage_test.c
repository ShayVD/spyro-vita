/* vpk_discimage against synthetic images (no game data): a small ISO9660 tree written as a 2048-byte .iso, a raw
 * mode 1 image (2352), a raw mode 2 form 1 image with a 2-second pregap missing (first LBA 150, 2352) and a raw
 * image with subchannel data (2448).  Each must sniff to its layout, find files by path (case, separators,
 * ";1"), walk the same tree (with an XA attribute), read the same data straight and through the windows, and
 * refuse reads past the end.  A sparse .iso past 3 GiB reads its last sector (64-bit offsets on a 32-bit host).
 * Plus the containers sniff as such, and SHA-1's FIPS vectors.
 *   discimage_test [scratch directory] */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../vpk_discimage.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("discimage_test: FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

#define NSECT 40
static unsigned char img[NSECT][2048];

static void le32(unsigned char *p, unsigned v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; }

/* a directory record at p: returns its length */
static int rec(unsigned char *p, const char *name, unsigned lba, unsigned size, int dir, int xa) {
  int nl = name[0] ? (int)strlen(name) : 1, len = 33 + nl + (1 - nl % 2) + (xa ? 14 : 0);
  memset(p, 0, (size_t)len);
  p[0] = (unsigned char)len;
  le32(p + 2, lba);
  le32(p + 10, size);
  p[25] = dir ? 2 : 0;
  p[32] = (unsigned char)nl;
  memcpy(p + 33, name, (size_t)nl);
  if (xa) {
    unsigned char *su = p + 33 + nl + (1 - nl % 2);
    su[4] = (unsigned char)(xa >> 8), su[5] = (unsigned char)xa, su[6] = 'X', su[7] = 'A';
  }
  return len;
}

static void build(void) {
  unsigned char *d;
  int i, at;
  memset(img, 0, sizeof img);
  for (i = 0; i < NSECT; i++) /* every sector's data names it */
    img[i][2047] = (unsigned char)i, img[i][2046] = 0xA5;
  img[16][0] = 1;
  memcpy(img[16] + 1, "CD001", 5);
  rec(img[16] + 156, "\0", 18, 2048, 1, 0);
  d = img[18]; /* root: . .. SYSTEM.CNF;1 DATA/ MUSIC.STR;1 (XA) */
  at = rec(d, "\0", 18, 2048, 1, 0);
  at += rec(d + at, "\1", 18, 2048, 1, 0);
  at += rec(d + at, "SYSTEM.CNF;1", 20, 30, 0, 0);
  at += rec(d + at, "DATA", 19, 2048, 1, 0);
  at += rec(d + at, "MUSIC.STR;1", 30, 4096, 0, 0x2000 | 0x1000);
  d = img[19]; /* DATA: . .. LEVEL.BIN;1 */
  at = rec(d, "\0", 19, 2048, 1, 0);
  at += rec(d + at, "\1", 18, 2048, 1, 0);
  rec(d + at, "LEVEL.BIN;1", 24, 5000, 0, 0);
  memcpy(img[20], "BOOT = cdrom:\\GAME.EXE;1\r\n", 26);
}

static int tobcd(int v) { return (v / 10) << 4 | (v % 10); }

/* the image as a file: sector bytes ssize, the data at off, mode byte, first LBA base */
static void write_image(const char *path, int ssize, int off, int mode, int base) {
  FILE *f = fopen(path, "wb");
  unsigned char s[2448];
  int i;
  for (i = base; i < NSECT; i++) {
    memset(s, 0, sizeof s);
    if (ssize > 2048) {
      int a = i + 150;
      memcpy(s, "\0\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\0", 12);
      s[12] = (unsigned char)tobcd(a / 75 / 60), s[13] = (unsigned char)tobcd(a / 75 % 60), s[14] = (unsigned char)tobcd(a % 75);
      s[15] = (unsigned char)mode;
      if (mode == 2)
        s[18] = s[22] = (unsigned char)(i == 30 ? 0x20 : 0x08); /* submode: form 2 for the XA file's first */
    }
    memcpy(s + off, img[i], 2048);
    fwrite(s, 1, (size_t)ssize, f);
  }
  fclose(f);
}

typedef struct { int n, xa; char paths[8][64]; int lbas[8]; } Seen;
static int saw(void *user, const VpkDiscEntry *e) {
  Seen *s = user;
  if (s->n < 8) snprintf(s->paths[s->n], 64, "%s", e->path), s->lbas[s->n] = e->lba;
  if (e->xa_attr == 0x3000) s->xa++;
  s->n++;
  return 0;
}

static void one(const char *path, int ssize, int off, int mode, int base) {
  static VpkDisc d;
  VpkDiscLayout l;
  unsigned char a[2048 * 2], b[2048 * 2], sub[8];
  unsigned lba = 0, size = 0;
  Seen s;
  int w;
  write_image(path, ssize, off, mode, base);
  memset(&l, 0, sizeof l);
  CHECK(vpk_disc_sniff(path, &l) == (ssize == 2048 ? VPK_DISC_ISO : VPK_DISC_RAW));
  CHECK(l.sector == ssize && l.data_off == off && l.base == base);
  for (w = 0; w < 2; w++) {
    CHECK(vpk_disc_open_any(&d, path, w) == 0 && vpk_disc_is_open(&d));
    CHECK(d.nsect == NSECT - base);
    CHECK(vpk_disc_find(&d, "\\SYSTEM.CNF;1", &lba, &size) == 0 && lba == 20 && size == 30);
    CHECK(vpk_disc_find(&d, "/data/level.bin", &lba, &size) == 0 && lba == 24 && size == 5000);
    CHECK(vpk_disc_find(&d, "DATA", &lba, &size) == -1);          /* a directory */
    CHECK(vpk_disc_find(&d, "\\NOPE.TXT;1", &lba, &size) == -1);
    CHECK(vpk_disc_find(&d, "SYSTEM.CNF\\X", &lba, &size) == -1); /* a file is not a directory */
    CHECK(vpk_disc_read(&d, 24, 2, w ? b : a) == 0);
    memset(&s, 0, sizeof s);
    CHECK(vpk_disc_walk(&d, saw, &s) == 0 && s.n == 3 && s.xa == 1);
    CHECK(!strcmp(s.paths[0], "SYSTEM.CNF") && !strcmp(s.paths[1], "DATA/LEVEL.BIN") && !strcmp(s.paths[2], "MUSIC.STR"));
    CHECK(vpk_disc_read(&d, NSECT - 1, 1, a + 2048) == 0 && a[2048 + 2047] == NSECT - 1);
    CHECK(vpk_disc_read(&d, NSECT, 1, a + 2048) == -1);
    CHECK(vpk_disc_read_raw(&d, 20, 0, sub, 8) == 8);
    if (mode == 2)
      CHECK(vpk_disc_read_raw(&d, 30, 16, sub, 8) == 8 && (sub[2] & 0x20));
    if (base)
      CHECK(vpk_disc_read(&d, base - 1, 1, a + 2048) == -1);
    CHECK(vpk_disc_read_raw(&d, 20, ssize < 2352 ? 2047 : 2351, sub, 2) == -1); /* past the sector */
    vpk_disc_close(&d);
    CHECK(!vpk_disc_is_open(&d));
  }
  CHECK(!memcmp(a, b, 2048) && a[2047] == 24);
  CHECK(b[2047] == 24 && b[4095] == 25 && b[2046] == 0xA5);
}

static void sha(const char *msg, unsigned n, const char *want) {
  VpkSha1 s;
  char hex[41];
  vpk_sha1_init(&s);
  vpk_sha1_update(&s, msg, n);
  vpk_sha1_hex(&s, hex);
  CHECK(!strcmp(hex, want));
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  char p[512];
  static VpkDisc d;
  VpkDiscLayout l;
  FILE *f;
  build();
  snprintf(p, sizeof p, "%s/t.iso", dir), one(p, 2048, 0, 0, 0);
  snprintf(p, sizeof p, "%s/t1.bin", dir), one(p, 2352, 16, 1, 0);
  snprintf(p, sizeof p, "%s/t2.bin", dir), one(p, 2352, 24, 2, 0);
  snprintf(p, sizeof p, "%s/t2p.bin", dir), one(p, 2352, 24, 2, 3);
  snprintf(p, sizeof p, "%s/t2s.img", dir), one(p, 2448, 24, 2, 0);
  { /* past 3 GiB: the .iso again with one more sector at LBA 1,600,000, the gap sparse */
    unsigned char big[2048];
    snprintf(p, sizeof p, "%s/big.iso", dir);
    write_image(p, 2048, 0, 0, 0);
    f = fopen(p, "r+b");
    memset(big, 0x5A, sizeof big);
    CHECK(f && fseeko(f, (off_t)1600000 * 2048, SEEK_SET) == 0 && fwrite(big, 1, 2048, f) == 2048);
    if (f) fclose(f);
    memset(big, 0, sizeof big);
    CHECK(vpk_disc_open_any(&d, p, 0) == 0 && d.nsect == 1600001);
    CHECK(vpk_disc_read(&d, 1600000, 1, big) == 0 && big[0] == 0x5A && big[2047] == 0x5A);
    vpk_disc_close(&d);
    remove(p);
  }
  snprintf(p, sizeof p, "%s/t.chd", dir);
  f = fopen(p, "wb"), fwrite("MComprHD", 1, 8, f), fclose(f);
  CHECK(vpk_disc_sniff(p, &l) == VPK_DISC_CHD && vpk_disc_open_any(&d, p, 0) == -1 && !vpk_disc_is_open(&d));
  snprintf(p, sizeof p, "%s/none.bin", dir);
  CHECK(vpk_disc_sniff(p, &l) == VPK_DISC_UNREADABLE);
  sha("abc", 3, "a9993e364706816aba3e25717850c26c9cd0d89d");
  sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  sha("", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  printf("discimage_test: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}

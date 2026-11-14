/* vpk_discimage against synthetic images (no game data): a small ISO9660 tree written as a 2048-byte .iso, a raw
 * mode 1 image (2352), a raw mode 2 form 1 image with a 2-second pregap missing (first LBA 150, 2352) and a raw
 * image with subchannel data (2448).  Each must sniff to its layout, find files by path (case, separators,
 * ";1"), walk the same tree (with an XA attribute), read the same data straight and through the windows, and
 * refuse reads past the end.  A sparse .iso past 3 GiB reads its last sector (64-bit offsets on a 32-bit host).
 * A source of sectors in place of a file serves the same tree.  Plus the containers sniff as such, and SHA-1's
 * FIPS vectors.  Cue sheets parse (multi-file, pregaps, modes, case,
 * BOM, CRLF), and folders of such images, written in the scratch directory, pick the image spyro-vita's search
 * would: every .cue first, name order ignoring case, a .cue's file found in another case or folder, a .cue the game
 * can't use passed over for its .bin, a multi-file cue's data track, the first reason when nothing is usable.  The
 * boot serial from SYSTEM.CNF (PS1 and PS2).  Built with -DDISCIMAGE_MUTANT=1 (no cue-first pass) or 2 (a cue's
 * first track, audio or not) it must fail.
 *   discimage_test [scratch directory] */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
  memcpy(img[20], "BOOT = cdrom:\\SLES_012.34;1\r\n", 29);
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

/* ---- a source of sectors in place of a file ---- */

static int src_asks, src_fail_at = -1;
static int src_read(void *user, int lba, unsigned char raw[2352]) {
  src_asks++;
  if (lba == src_fail_at) { /* a failing source that wrote first */
    memset(raw, 0xEE, 2352);
    return -1;
  }
  memset(raw, 0, 2352);
  raw[15] = 2;
  raw[18] = raw[22] = (unsigned char)(lba == 30 ? 0x20 : 0x08);
  memcpy(raw + 24, ((unsigned char (*)[2048])user)[lba], 2048);
  return 0;
}

static void source_tests(const char *file_image) {
  static VpkDisc d;
  VpkDiscLayout l;
  unsigned char a[2048 * 2], sub[8];
  unsigned lba = 0, size = 0;
  Seen s;
  CHECK(vpk_disc_open_source(&d, NULL, img, NSECT) == -1 && !vpk_disc_is_open(&d));
  CHECK(vpk_disc_open_source(&d, src_read, img, 0) == -1 && !vpk_disc_is_open(&d));
  CHECK(vpk_disc_open_source(&d, src_read, img, NSECT) == 0 && vpk_disc_is_open(&d));
  CHECK(d.nsect == NSECT && d.l.sector == 2352 && d.l.data_off == 24 && d.l.base == 0);
  CHECK(vpk_disc_find(&d, "\\SYSTEM.CNF;1", &lba, &size) == 0 && lba == 20 && size == 30);
  CHECK(vpk_disc_find(&d, "/data/level.bin", &lba, &size) == 0 && lba == 24 && size == 5000);
  memset(&s, 0, sizeof s);
  CHECK(vpk_disc_walk(&d, saw, &s) == 0 && s.n == 3 && s.xa == 1);
  CHECK(!strcmp(s.paths[0], "SYSTEM.CNF") && !strcmp(s.paths[1], "DATA/LEVEL.BIN") && !strcmp(s.paths[2], "MUSIC.STR"));
  CHECK(vpk_disc_read(&d, 24, 2, a) == 0 && a[2047] == 24 && a[4095] == 25 && a[2046] == 0xA5);
  /* the last sector given is kept: its subheader and its data cost one ask */
  src_asks = 0;
  CHECK(vpk_disc_read_raw(&d, 30, 16, sub, 8) == 8 && (sub[2] & 0x20) && src_asks == 1);
  CHECK(vpk_disc_read_raw(&d, 30, 24, a, 2048) == 2048 && a[2047] == 30 && src_asks == 1);
  CHECK(vpk_disc_read_raw(&d, 31, 24, a, 2048) == 2048 && a[2047] == 31 && src_asks == 2);
  /* the ends: of the disc (the source isn't asked) and of the sector */
  CHECK(vpk_disc_read(&d, NSECT, 1, a) == -1 && vpk_disc_read(&d, -1, 1, a) == -1 && src_asks == 2);
  CHECK(vpk_disc_read(&d, NSECT - 1, 1, a) == 0 && a[2047] == NSECT - 1);
  CHECK(vpk_disc_read_raw(&d, 20, 2351, sub, 2) == -1);
  /* a source that fails: the read fails, and the bytes it wrote aren't served as the sector kept before */
  CHECK(vpk_disc_read(&d, 21, 1, a) == 0 && a[2047] == 21);
  src_fail_at = 22;
  CHECK(vpk_disc_read(&d, 22, 1, a) == -1);
  CHECK(vpk_disc_read(&d, 21, 1, a) == 0 && a[2047] == 21 && a[0] != 0xEE);
  src_fail_at = 18; /* the root directory: the walk names the sector */
  memset(&s, 0, sizeof s);
  CHECK(vpk_disc_walk(&d, saw, &s) != 0 && d.bad_lba == 18);
  src_fail_at = -1;
  /* a file opened over it, then the source again, then closed: each open closes the one before */
  memset(&l, 0, sizeof l);
  CHECK(vpk_disc_sniff(file_image, &l) == VPK_DISC_RAW && vpk_disc_open(&d, file_image, &l, 0) == 0);
  src_asks = 0;
  CHECK(d.source == NULL && vpk_disc_read(&d, 24, 1, a) == 0 && a[2047] == 24 && src_asks == 0);
  CHECK(vpk_disc_open_source(&d, src_read, img, NSECT) == 0);
  CHECK(vpk_disc_read(&d, 25, 1, a) == 0 && a[2047] == 25 && src_asks == 1);
  vpk_disc_close(&d);
  CHECK(!vpk_disc_is_open(&d) && d.source == NULL && d.source_buf == NULL);
  CHECK(vpk_disc_read(&d, 25, 1, a) == -1);
  vpk_disc_close(&d); /* twice is harmless */
}

/* ---- cue sheets and the folder search ---- */

#define CUE(text) vpk_cue_parse(text, (int)strlen(text), &c)
static void cue_tests(void) {
  static VpkCue c;
  static const char multi[] = /* a BOM, CRLF, REM / CATALOG, keywords in lower case, a WAVE file first */
      "\xef\xbb\xbfREM GENRE Game\r\nCATALOG 0000000000000\r\n"
      "file \"Game (Track 1).wav\" WAVE\r\n  track 01 audio\r\n    index 01 00:00:00\r\n"
      "FILE \"Game (Track 2).bin\" BINARY\r\n  TRACK 02 MODE2/2352\r\n    INDEX 00 00:00:00\r\n"
      "    INDEX 01 00:02:00\r\n"
      "FILE \"Game (Track 3).bin\" BINARY\r\n  TRACK 03 AUDIO\r\n    PREGAP 00:02:00\r\n    INDEX 01 00:00:00\r\n";
  static const char single[] = /* one file, unquoted with a blank in it, tabs, a pregap kept in the file */
      "FILE my game.bin BINARY\n\tTRACK 1 MODE1/2352\n\t\tINDEX 1 00:00:00\n"
      "\tTRACK 2 AUDIO\n\t\tINDEX 0 01:02:03\n\t\tINDEX 1 01:04:03\n\tTRACK 3 CDG\n\tTRACK 4 CDI/2336\n"
      "\tTRACK 5 MODE2/2448\n\tTRACK 6 MODE1/2048\n\tTRACK 7 MODE2/2336";
  CHECK(vpk_cue_parse(multi, (int)sizeof multi - 1, &c) == 3 && c.nfiles == 3);
  CHECK(!strcmp(vpk_cue_file(&c, 0), "Game (Track 1).wav") && !strcmp(vpk_cue_file(&c, 2), "Game (Track 3).bin"));
  CHECK(c.track[0].number == 1 && c.track[0].mode == VPK_CUE_AUDIO && c.track[0].file == 0);
  CHECK(c.track[1].number == 2 && c.track[1].mode == VPK_CUE_MODE2_2352 && c.track[1].sector == 2352);
  CHECK(c.track[1].file == 1 && c.track[1].index0 == 0 && c.track[1].index1 == 150 && c.track[1].pregap == 0);
  CHECK(c.track[2].mode == VPK_CUE_AUDIO && c.track[2].file == 2 && c.track[2].pregap == 150);
  CHECK(c.track[2].index0 == -1 && c.track[2].index1 == 0);
  CHECK(vpk_cue_data_track(&c) == 1);
  CHECK(vpk_cue_parse(single, (int)sizeof single - 1, &c) == 7 && c.nfiles == 1);
  CHECK(!strcmp(vpk_cue_file(&c, 0), "my game.bin") && vpk_cue_data_track(&c) == 0);
  CHECK(c.track[0].mode == VPK_CUE_MODE1_2352 && c.track[1].index0 == (62 * 75 + 3) && c.track[1].index1 == 64 * 75 + 3);
  CHECK(c.track[2].mode == VPK_CUE_CDG && c.track[2].sector == 2448);
  CHECK(c.track[3].mode == VPK_CUE_OTHER && c.track[3].sector == 2336);
  CHECK(c.track[4].mode == VPK_CUE_MODE2_2448 && c.track[5].mode == VPK_CUE_MODE1_2048);
  CHECK(c.track[6].mode == VPK_CUE_MODE2_2336 && c.track[6].file == 0); /* the last line without its newline */
  CHECK(CUE("TRACK 01 MODE2/2352\nFILE a.bin BINARY\n") == -1); /* a track before any file */
  CHECK(CUE("REM nothing\n") == -1);
  CHECK(CUE("FILE a.bin BINARY\n") == -1); /* no track */
  CHECK(CUE("FILE a.bin BINARY\nTRACK 01 AUDIO\nFILE b.wav WAVE\nTRACK 02 CDG\n") == 2 &&
        vpk_cue_data_track(&c) == -1);
  CHECK(CUE("FILE a.bin\nTRACK 01 MODE2/2352\n") == 1 && !strcmp(vpk_cue_file(&c, 0), "a.bin"));
}

static void put(const char *dir, const char *name, const char *text) {
  char p[512];
  FILE *f;
  snprintf(p, sizeof p, "%s/%s", dir, name);
  f = fopen(p, "wb");
  if (f) fputs(text, f), fclose(f);
}

/* an image file in dir: mode 1 (data at 16), mode 2 (at 24, first LBA base), a cooked .iso (2048), or audio
 * (no sync, no volume descriptor) */
enum { M1, M2, ISO, AUDIO };
static void img_file(const char *dir, const char *name, int what, int base) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  if (what == AUDIO) {
    unsigned char s[2352];
    FILE *f = fopen(p, "wb");
    int i;
    memset(s, 0x11, sizeof s);
    for (i = 0; f && i < NSECT; i++)
      fwrite(s, 1, sizeof s, f);
    if (f) fclose(f);
  } else if (what == ISO)
    write_image(p, 2048, 0, 0, 0);
  else
    write_image(p, 2352, what == M1 ? 16 : 24, what == M1 ? 1 : 2, base);
}

static char case_dir[512];
static const char *mkcase(const char *top, const char *name) {
  snprintf(case_dir, sizeof case_dir, "%s/%s", top, name);
  mkdir(case_dir, 0755);
  return case_dir;
}

typedef struct { int n; int status[8]; char name[8][64]; } Tried;
static void tried(void *user, const VpkDiscFound *f) {
  Tried *t = user;
  if (t->n < 8) t->status[t->n] = f->status, snprintf(t->name[t->n], 64, "%.63s", f->name);
  t->n++;
}

/* the found image opens and its files are there */
static int usable(const VpkDiscFound *f) {
  static VpkDisc d;
  unsigned lba = 0, size = 0;
  int ok = vpk_disc_open(&d, f->path, &f->l, 1) == 0 && vpk_disc_find(&d, "DATA/LEVEL.BIN", &lba, &size) == 0 &&
           lba == 24 && size == 5000;
  vpk_disc_close(&d);
  return ok;
}

static const char *tail(const char *path) { const char *s = strrchr(path, '/'); return s ? s + 1 : path; }

static void search_tests(const char *top) {
  VpkDiscSearch s;
  VpkDiscFound f;
  Tried t;
  const char *d;
  const unsigned raw = 1u << VPK_DISC_RAW, mode2 = 1u << VPK_CUE_MODE2_2352 | 1u << VPK_CUE_MODE2_2448;

  /* nothing there, or no folder */
  d = mkcase(top, "empty");
  put(d, "readme.txt", "x");
  CHECK(vpk_disc_find_image(d, NULL, &f) == -1 && f.status == VPK_IMG_NONE);
  CHECK(vpk_disc_find_image("/nonexistent/vpk", NULL, &f) == -1 && f.status == VPK_IMG_NONE);

  /* every .cue before the images, whatever their names: z.cue beats a.bin */
  d = mkcase(top, "order");
  img_file(d, "a.bin", M2, 0);
  img_file(d, "z.bin", M1, 0);
  put(d, "z.cue", "FILE \"z.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
  CHECK(vpk_disc_find_image(d, NULL, &f) == 0 && f.status == VPK_IMG_OK && f.cue && !strcmp(f.name, "z.cue"));
  CHECK(!strcmp(f.image, "z.bin") && !strcmp(tail(f.path), "z.bin") && f.kind == VPK_DISC_RAW);
  CHECK(f.l.data_off == 16 && f.track == 1 && f.mode == VPK_CUE_MODE1_2352 && usable(&f));

  /* spyro-vita's "several": a.cue names a missing file, a.iso is cooked, b.cue names its .bin in another case and
   * folder (with a pregap); with raw images only, b.cue wins after a.cue */
  d = mkcase(top, "several");
  put(d, "a.cue", "FILE \"missing.bin\" BINARY\n  TRACK 01 MODE2/2352\n");
  img_file(d, "a.iso", ISO, 0);
  put(d, "b.cue", "FILE \"dump\\B.Bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
  img_file(d, "b.bin", M2, 3);
  img_file(d, "c.bin", M2, 0);
  memset(&s, 0, sizeof s);
  memset(&t, 0, sizeof t);
  s.kinds = raw, s.cue_modes = mode2, s.seen = tried, s.user = &t;
  CHECK(vpk_disc_find_image(d, &s, &f) == 0 && !strcmp(f.name, "b.cue") && !strcmp(f.image, "b.bin"));
  CHECK(f.l.base == 3 && f.l.data_off == 24 && usable(&f));
  CHECK(t.n == 2 && t.status[0] == VPK_IMG_CUE_MISSING && !strcmp(t.name[0], "a.cue") && t.status[1] == VPK_IMG_OK);
  /* with b.cue gone, the .iso is passed over (not a kind it takes) for b.bin; the .iso when it takes one */
  snprintf(case_dir, sizeof case_dir, "%s/several/b.cue", top), remove(case_dir);
  d = mkcase(top, "several");
  memset(&t, 0, sizeof t);
  CHECK(vpk_disc_find_image(d, &s, &f) == 0 && !f.cue && !strcmp(f.name, "b.bin") && f.l.base == 3);
  CHECK(t.n == 3 && t.status[1] == VPK_IMG_KIND && !strcmp(t.name[1], "a.iso")); /* a.cue, a.iso, b.bin */
  CHECK(vpk_disc_find_image(d, NULL, &f) == 0 && !strcmp(f.name, "a.iso") && f.kind == VPK_DISC_ISO &&
        f.l.sector == 2048 && usable(&f));

  /* a .cue whose data track is a mode the game doesn't take is passed over for its .bin, found on its own */
  d = mkcase(top, "cuemode");
  put(d, "game.cue", "FILE game.bin BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n");
  img_file(d, "game.bin", M2, 0);
  memset(&t, 0, sizeof t);
  s.seen = tried, s.user = &t;
  CHECK(vpk_disc_find_image(d, &s, &f) == 0 && !f.cue && !strcmp(f.name, "game.bin") && usable(&f));
  CHECK(t.n == 2 && t.status[0] == VPK_IMG_CUE_TRACK);
  CHECK(vpk_disc_find_image(d, NULL, &f) == 0 && f.cue && f.mode == VPK_CUE_MODE1_2048); /* any mode */

  /* a multi-file cue whose data track isn't the first: its file is the image */
  d = mkcase(top, "multi");
  put(d, "Game.cue", "FILE \"Game (Track 1).wav\" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
                     "FILE \"Game (Track 2).bin\" BINARY\n  TRACK 02 MODE2/2352\n    INDEX 01 00:00:00\n");
  put(d, "Game (Track 1).wav", "RIFF");
  img_file(d, "Game (Track 2).bin", M2, 0);
  CHECK(vpk_disc_find_image(d, &s, &f) == 0 && f.cue && f.track == 2 && f.mode == VPK_CUE_MODE2_2352);
  CHECK(!strcmp(f.image, "Game (Track 2).bin") && usable(&f));

  /* a Redump multi-track dump without its .cue: Track 01 first in name order, any case of extension */
  d = mkcase(top, "redump");
  img_file(d, "game (track 03).BIN", AUDIO, 0);
  img_file(d, "Game (Track 02).bin", AUDIO, 0);
  img_file(d, "Game (Track 01).bin", M2, 0);
  CHECK(vpk_disc_find_image(d, &s, &f) == 0 && !strcmp(f.name, "Game (Track 01).bin") && usable(&f));

  /* nothing usable: the first candidate's reason, and every candidate tried in order */
  d = mkcase(top, "none");
  put(d, "a.cue", "REM no file\n");
  put(d, "b.cue", "FILE b.bin BINARY\n  TRACK 01 AUDIO\n");
  img_file(d, "b.bin", AUDIO, 0);
  put(d, "c.chd", "MComprHD");
  memset(&t, 0, sizeof t);
  CHECK(vpk_disc_find_image(d, &s, &f) == -1 && f.status == VPK_IMG_CUE_BAD && !strcmp(f.name, "a.cue"));
  CHECK(t.n == 4 && t.status[1] == VPK_IMG_CUE_TRACK && t.status[2] == VPK_IMG_KIND && t.status[3] == VPK_IMG_KIND);
  s.seen = NULL;
  snprintf(case_dir, sizeof case_dir, "%s/none/a.cue", top), remove(case_dir);
  snprintf(case_dir, sizeof case_dir, "%s/none/b.cue", top), remove(case_dir);
  d = mkcase(top, "none");
  CHECK(vpk_disc_find_image(d, &s, &f) == -1 && f.status == VPK_IMG_KIND && f.kind == VPK_DISC_UNKNOWN &&
        !strcmp(f.name, "b.bin"));
  s.kinds = 1u << VPK_DISC_CHD; /* a game could take one */
  CHECK(vpk_disc_find_image(d, &s, &f) == 0 && f.kind == VPK_DISC_CHD && !strcmp(f.name, "c.chd"));
}

static void serial(const char *cnf, int want, const char *serial_want) {
  char out[32];
  CHECK(vpk_disc_boot_serial(cnf, (int)strlen(cnf), out, sizeof out) == want && !strcmp(out, serial_want));
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
  snprintf(p, sizeof p, "%s/t2.bin", dir), source_tests(p);
  snprintf(p, sizeof p, "%s/t.chd", dir);
  f = fopen(p, "wb"), fwrite("MComprHD", 1, 8, f), fclose(f);
  CHECK(vpk_disc_sniff(p, &l) == VPK_DISC_CHD && vpk_disc_open_any(&d, p, 0) == -1 && !vpk_disc_is_open(&d));
  snprintf(p, sizeof p, "%s/none.bin", dir);
  CHECK(vpk_disc_sniff(p, &l) == VPK_DISC_UNREADABLE);
  cue_tests();
  search_tests(dir);
  serial("BOOT = cdrom:\\SCUS_942.28;1\r\nTCB = 4\r\n", 0, "SCUS-94228");
  serial("BOOT2 = cdrom0:\\SLUS_203.70;1\nVER = 1.00\nVMODE = NTSC\n", 0, "SLUS-20370");
  serial("VMODE = PAL\n  boot=cdrom:\\S0\\sles_009.00;1\n", 0, "SLES-00900");
  serial("BOOT = cdrom:GAME.EXE;1", 0, "GAME-EXE");
  serial("TCB = 4\nBOOTX = 1\n", -1, "");
  { /* the image's own SYSTEM.CNF, read off it */
    unsigned lba = 0, size = 0;
    char cnf[2048], out[16];
    snprintf(p, sizeof p, "%s/t2.bin", dir);
    CHECK(vpk_disc_open_any(&d, p, 0) == 0 && vpk_disc_find(&d, "SYSTEM.CNF", &lba, &size) == 0);
    CHECK(vpk_disc_read(&d, (int)lba, 1, cnf) == 0 && vpk_disc_boot_serial(cnf, (int)size, out, sizeof out) == 0);
    CHECK(!strcmp(out, "SLES-01234"));
    vpk_disc_close(&d);
  }
  sha("abc", 3, "a9993e364706816aba3e25717850c26c9cd0d89d");
  sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  sha("", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  printf("discimage_test: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}

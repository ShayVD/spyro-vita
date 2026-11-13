/* The PS1 Classic EBOOT.PBP reader (src/platform/common/pbp.c, decisions
 * D157). usage: test_pbp WORK [PSN_DIR [FILES_DIR]]
 *   WORK       scratch folder (made)
 *   PSN_DIR    folders holding an EBOOT.PBP each (disc/psn/NPUI94228/...);
 *              each is extracted into WORK/<folder> and must pass the region
 *              table's hashes; without any, those cases skip (exit 77)
 *   FILES_DIR  the extracted dump (disc/files): every file the manifest
 *              names must match it byte for byte (US copies only) */
#include "psx_aes128.h"
#include "psx_cd.h"
#include "psx_cdimage.h"
#include "psx_cdnative.h"
#include "psx_hooks.h"
#include "psx_pbp.h"
#include "psx_region.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

/* libspu's output, which the platform provides: nothing plays here */
void psx_audio_out(const short *pcm, int frames) {
  (void)pcm;
  (void)frames;
}

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);             \
      failures++;                                                              \
    }                                                                          \
  } while (0)

/* ---- AES-128: FIPS 197 appendix C.1 ---- */

static void test_aes(void) {
  static const unsigned char key[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  static const unsigned char plain[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                          0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  static const unsigned char cipher[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                           0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
  unsigned char out[16], iv[16], two[32], back[32];
  PsxAes128 a;
  psx_aes128_init(&a, key);
  psx_aes128_encrypt(&a, plain, out);
  CHECK(memcmp(out, cipher, 16) == 0);
  psx_aes128_decrypt(&a, cipher, out);
  CHECK(memcmp(out, plain, 16) == 0);
  /* CBC: the second block chains on the first, and decrypting undoes it in place */
  memset(iv, 0, 16);
  memcpy(two, plain, 16);
  memcpy(two + 16, plain, 16);
  psx_aes128_cbc_encrypt(&a, iv, two, two, 32);
  CHECK(memcmp(two, cipher, 16) == 0);
  CHECK(memcmp(two + 16, cipher, 16) != 0);
  CHECK(memcmp(iv, two + 16, 16) == 0);
  memset(iv, 0, 16);
  psx_aes128_cbc_decrypt(&a, iv, two, back, 32);
  CHECK(memcmp(back, plain, 16) == 0 && memcmp(back + 16, plain, 16) == 0);
}

/* ---- PBPs the test makes: a plain header and blocks of its own ---- */

#define BLOCK 0x9300
#define PSAR 0x400

static void put32(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8); }

static unsigned char pattern(int block, unsigned i) { return (unsigned char)(block ? i * 13 + 5 : i * 7 + 1); }

/* A PBP at path (TEST00001) whose image holds the given packed blocks: each
 * `packed[i]` of `size[i]` bytes with `marker[i]` (1: LZRC, else deflate), laid
 * one after the other. Returns the file's bytes' count written, 0 on failure. */
static unsigned make_pbp(const char *path, int nblocks, const unsigned char *const *packed, const unsigned *size,
                         const unsigned *marker) {
  unsigned total = 0, off = 0, i, filesize;
  unsigned char *file, *sfo, *psar;
  FILE *f;
  for (i = 0; i < (unsigned)nblocks; i++)
    total += size[i];
  filesize = PSAR + 0x100000 + total;
  file = calloc(1, filesize);
  if (!file)
    return 0;
  memcpy(file, "\0PBP", 4);
  put32(file + 4, 0x00010000);
  for (i = 0; i < 8; i++)
    put32(file + 8 + 4 * i, 0x28 + 60); /* every part after the SFO empty */
  put32(file + 8, 0x28);
  put32(file + 8 + 28, PSAR);
  sfo = file + 0x28;
  memcpy(sfo, "\0PSF", 4);
  put32(sfo + 4, 0x0101);
  put32(sfo + 8, 36);
  put32(sfo + 12, 44);
  put32(sfo + 16, 1);
  put16(sfo + 20, 0);
  put16(sfo + 22, 0x0204);
  put32(sfo + 24, 10);
  put32(sfo + 28, 16);
  put32(sfo + 32, 0);
  memcpy(sfo + 36, "DISC_ID\0", 8);
  memcpy(sfo + 44, "TEST00001\0", 10);
  psar = file + PSAR;
  memcpy(psar, "PSISOIMG0000", 12);
  put32(psar + 0xBFC, 0x100000);
  memcpy(psar + 0x400, "_TEST_00001", 11);
  for (i = 0; i < (unsigned)nblocks; i++) {
    put32(psar + 0x4000 + 32 * i, off);
    put16(psar + 0x4004 + 32 * i, size[i]);
    put16(psar + 0x4006 + 32 * i, marker[i]);
    memcpy(psar + 0x100000 + off, packed[i], size[i]);
    off += size[i];
  }
  f = fopen(path, "wb");
  if (!f || fwrite(file, 1, filesize, f) != filesize)
    filesize = 0;
  if (f)
    fclose(f);
  free(file);
  return filesize;
}

/* a block of raw bytes as one stored deflate block (5 bytes longer) */
static unsigned stored_deflate(unsigned char *out, const unsigned char *raw) {
  out[0] = 1;
  put16(out + 1, BLOCK);
  put16(out + 3, BLOCK ^ 0xFFFF);
  memcpy(out + 5, raw, BLOCK);
  return BLOCK + 5;
}

/* a Mode 2 sector at raw: the mode byte, a subheader (form 2 or form 1), data */
static void mode2_sector(unsigned char *raw, int form2, const unsigned char *data, unsigned n) {
  memset(raw, 0, PSX_PBP_SECTOR);
  raw[15] = 2;
  raw[18] = raw[22] = (unsigned char)(form2 ? 0x20 : 0x08);
  if (data)
    memcpy(raw + 24, data, n);
}

/* a directory record at rec for name, extent lba, size; dir: a folder;
 * xa_attr nonzero adds the CD-XA system-use field. Returns its length. */
static int dir_record(unsigned char *rec, const char *name, unsigned lba, unsigned size, int dir, unsigned xa_attr) {
  int nl = name[0] == '\0' ? 1 : (int)strlen(name), len = 33 + nl + (1 - nl % 2) + (xa_attr ? 14 : 0);
  memset(rec, 0, (size_t)len);
  rec[0] = (unsigned char)len;
  put32(rec + 2, lba);
  put32(rec + 10, size);
  rec[25] = (unsigned char)(dir ? 2 : 0);
  rec[32] = (unsigned char)nl;
  if (name[0] == '\0')
    rec[33] = (unsigned char)name[1]; /* "." and "..": one byte, 0 or 1 ("\0\0", "\0\1") */
  else
    memcpy(rec + 33, name, (size_t)nl);
  if (xa_attr) {
    unsigned char *su = rec + 33 + nl + (1 - nl % 2);
    su[4] = (unsigned char)(xa_attr >> 8), su[5] = (unsigned char)xa_attr;
    su[6] = 'X', su[7] = 'A';
  }
  return len;
}

/* A two-block image (32 sectors) with a file system: the volume descriptor at
 * 16, the root directory at 18 (`root_size` bytes: one record for `name`, a
 * file at 20 of `file_size` bytes, with `xa_attr`), the file's sectors from
 * 20 on as `sector(k)` makes them. */
static void iso_image(unsigned char *raw, const char *name, unsigned root_size, unsigned file_size, unsigned xa_attr,
                      void (*sector)(unsigned char *raw, int k)) {
  unsigned char data[2048];
  int k;
  for (k = 0; k < 32; k++)
    mode2_sector(raw + k * PSX_PBP_SECTOR, 0, NULL, 0);
  memset(data, 0, sizeof data);
  data[0] = 1;
  memcpy(data + 1, "CD001", 5);
  dir_record(data + 156, "\0\0", 18, root_size, 1, 0);
  memcpy(raw + 16 * PSX_PBP_SECTOR + 24, data, 2048);
  memset(data, 0, sizeof data);
  k = dir_record(data, "\0\0", 18, root_size, 1, 0);
  k += dir_record(data + k, "\0\1", 18, root_size, 1, 0);
  dir_record(data + k, name, 20, file_size, 0, xa_attr);
  memcpy(raw + 18 * PSX_PBP_SECTOR + 24, data, 2048);
  for (k = 20; k < 32; k++)
    sector(raw + k * PSX_PBP_SECTOR, k - 20);
}

static void xa_pattern_sector(unsigned char *raw, int k) {
  unsigned i;
  (void)k;
  mode2_sector(raw, 1, NULL, 0);
  for (i = 0; i < 2332; i++) /* the subheader and data as the EDC vector's pattern */
    raw[16 + i] = (unsigned char)(i * 7 + 1);
  raw[18] |= 0x20, raw[22] |= 0x20; /* and the submode's Form 2 bit, in both copies of the subheader */
}

static void test_fake(const char *work) {
  char path[512], out[512], id[16];
  unsigned char *raw = malloc(2 * BLOCK), *pk0 = malloc(BLOCK + 64), *pk1 = malloc(BLOCK + 64);
  const unsigned char *packed[2];
  unsigned size[2], marker[2], i;
  PsxPbp *p;
  PsxPbpError err;
  int region;
  unsigned char *sectors = malloc(32 * PSX_PBP_SECTOR);
  CHECK(raw && pk0 && pk1 && sectors);
  if (!raw || !pk0 || !pk1 || !sectors)
    return;
  snprintf(path, sizeof path, "%s/fake.pbp", work);
  snprintf(out, sizeof out, "%s/fake-out", work);

  /* 1. a deflate-stored block and an LZRC-stored block read back whole and in parts */
  for (i = 0; i < BLOCK; i++)
    raw[i] = pattern(0, i), raw[BLOCK + i] = pattern(1, i);
  size[0] = stored_deflate(pk0, raw);
  pk1[0] = 0x80; /* an LZRC stream that is stored: its length big-endian, then the bytes */
  pk1[1] = BLOCK >> 24, pk1[2] = (BLOCK >> 16) & 0xFF, pk1[3] = (BLOCK >> 8) & 0xFF, pk1[4] = BLOCK & 0xFF;
  memcpy(pk1 + 5, raw + BLOCK, BLOCK);
  size[1] = BLOCK + 5;
  packed[0] = pk0, packed[1] = pk1;
  marker[0] = 0, marker[1] = 1;
  CHECK(make_pbp(path, 2, packed, size, marker) != 0);
  CHECK(psx_pbp_peek_disc_id(path, id, sizeof id) == 0 && strcmp(id, "TEST00001") == 0);
  CHECK(psx_pbp_open(&p, path, &err) == 0);
  if (!p)
    return;
  CHECK(strcmp(psx_pbp_disc_id(p), "TEST00001") == 0);
  CHECK(!psx_pbp_encrypted(p));
  CHECK(psx_pbp_sectors(p) == 32);
  CHECK(psx_pbp_read_sectors(p, 0, 32, sectors) == 0);
  CHECK(memcmp(sectors, raw, 2 * BLOCK) == 0);
  CHECK(psx_pbp_read_sectors(p, 20, 1, sectors) == 0 && sectors[0] == pattern(1, 4 * PSX_PBP_SECTOR));
  CHECK(psx_pbp_read_sectors(p, 31, 2, sectors) != 0); /* past the end */
  CHECK(psx_pbp_extract(p, out, &region, NULL, NULL, &err) != 0 && err.code == PSX_PBP_ERR_BLOCK); /* no file system */
  psx_pbp_close(p);

  /* 2. deflate with fixed and with dynamic Huffman codes (zlib's output for
   * "spyro" x 40 and for the bytes (i * 31 + (i >> 5)) & 0xFF, i < 3000), each
   * a short block, the rest of it zero */
  {
    static const unsigned char fixed[10] = {0x2b, 0x2e, 0xa8, 0x2c, 0xca, 0x2f, 0x1e, 0xfa, 0x04, 0x00};
    static const char dyn_hex[] =
        "85ca2722000000004014a318c5282846318a51508c6214a3a018c5284641318a518c826214a318c50846318a518c826214a318c5"
        "0bb8cb17109f57db33bd75f5199d55d931be76f61a9e56da32bc74f4189c54d8d03fb77717f07fd847984718406844284248460"
        "841784238465846184168452843484788407843384758479840e844a842c8468841f842b846d8469841e845a843c84748400844b"
        "8473840584018446842284648410845784638415841184568432847c84088447847b840d8409844e842a846c8418845f846b841d"
        "8419845e843a8472840484408427840384458441842684628414845084378413845584518436847a840c8448842f840b844d8449"
        "842e846a841c8458843f841b845d8fd23fc02";
    unsigned char dyn[280];
    unsigned n = (unsigned)strlen(dyn_hex) / 2, j;
    for (j = 0; j < n; j++) {
      unsigned v;
      sscanf(dyn_hex + 2 * j, "%2x", &v);
      dyn[j] = (unsigned char)v;
    }
    packed[0] = fixed, packed[1] = dyn;
    size[0] = sizeof fixed, size[1] = n;
    marker[0] = marker[1] = 0;
    CHECK(make_pbp(path, 2, packed, size, marker) != 0);
    CHECK(psx_pbp_open(&p, path, &err) == 0);
    if (p) {
      CHECK(psx_pbp_read_sectors(p, 0, 32, sectors) == 0);
      for (j = 0; j < 200; j++)
        if (sectors[j] != "spyro"[j % 5])
          break;
      CHECK(j == 200 && sectors[200] == 0 && sectors[BLOCK - 1] == 0);
      for (j = 0; j < 3000; j++)
        if (sectors[BLOCK + j] != ((j * 31 + (j >> 5)) & 0xFF))
          break;
      CHECK(j == 3000 && sectors[BLOCK + 3000] == 0);
      psx_pbp_close(p);
    }
  }

  /* 3. refused: an LZRC stream whose context shift is past a byte (lc 8), and a
   * block table that points past the file */
  pk1[0] = 0x08;
  packed[0] = pk0, packed[1] = pk1;
  size[0] = stored_deflate(pk0, raw), size[1] = 64;
  marker[0] = 0, marker[1] = 1;
  CHECK(make_pbp(path, 2, packed, size, marker) != 0);
  CHECK(psx_pbp_open(&p, path, &err) == 0);
  if (p) {
    CHECK(psx_pbp_read_sectors(p, 0, 16, sectors) == 0);
    CHECK(psx_pbp_read_sectors(p, 16, 1, sectors) != 0);
    psx_pbp_close(p);
  }
  {
    unsigned filesize = make_pbp(path, 1, packed, size, marker);
    FILE *f = fopen(path, "r+b");
    unsigned char e[8] = {0};
    CHECK(filesize && f);
    if (f) { /* entry 0's offset: past the data */
      put32(e, filesize);
      put16(e + 4, size[0]);
      fseek(f, PSAR + 0x4000, SEEK_SET);
      fwrite(e, 1, 8, f);
      fclose(f);
    }
    CHECK(psx_pbp_open(&p, path, &err) != 0 && err.code == PSX_PBP_ERR_BLOCK);
  }

  /* 4. a file system: one XA file of 12 sectors whose sectors carry the Form 2
   * EDC when asked (the vector 0xc4f118ff for the pattern); then one whose
   * root directory is longer than the walker allows, and one whose file is
   * named "..": both refused */
  iso_image(raw, "XA.STR", 2048, 12 * 2048, 0x1000, xa_pattern_sector);
  size[0] = stored_deflate(pk0, raw), size[1] = stored_deflate(pk1, raw + BLOCK);
  packed[0] = pk0, packed[1] = pk1;
  marker[0] = marker[1] = 0;
  CHECK(make_pbp(path, 2, packed, size, marker) != 0);
  CHECK(psx_pbp_open(&p, path, &err) == 0);
  if (p) {
    char full[600];
    FILE *f;
    unsigned char sec[2336];
    int r = psx_pbp_extract(p, out, &region, NULL, NULL, &err);
    /* not Spyro: refused after the files were written and removed again */
    CHECK(r != 0 && err.code == PSX_PBP_ERR_WRONG_DISC);
    snprintf(full, sizeof full, "%s/XA.STR", out);
    CHECK(fopen(full, "rb") == NULL);
    /* the sectors themselves, with the error codes on */
    psx_pbp_set_edc(p, 1);
    CHECK(psx_pbp_read_sectors(p, 20, 1, sectors) == 0);
    memcpy(sec, sectors + 16, 2336);
    CHECK(sec[2332] == 0xff && sec[2333] == 0x18 && sec[2334] == 0xf1 && sec[2335] == 0xc4);
    psx_pbp_set_edc(p, 0);
    CHECK(psx_pbp_read_sectors(p, 20, 1, sectors) == 0 && (sectors[16 + 2332] | sectors[16 + 2335]) == 0);
    (void)f;
    psx_pbp_close(p);
  }
  iso_image(raw, "XA.STR", 65 * 2048, 12 * 2048, 0x1000, xa_pattern_sector);
  size[0] = stored_deflate(pk0, raw), size[1] = stored_deflate(pk1, raw + BLOCK);
  CHECK(make_pbp(path, 2, packed, size, marker) != 0);
  CHECK(psx_pbp_open(&p, path, &err) == 0);
  if (p) {
    CHECK(psx_pbp_extract(p, out, &region, NULL, NULL, &err) != 0 && err.code == PSX_PBP_ERR_BLOCK);
    psx_pbp_close(p);
  }
  iso_image(raw, "..", 2048, 12 * 2048, 0, xa_pattern_sector);
  size[0] = stored_deflate(pk0, raw), size[1] = stored_deflate(pk1, raw + BLOCK);
  CHECK(make_pbp(path, 2, packed, size, marker) != 0);
  CHECK(psx_pbp_open(&p, path, &err) == 0);
  if (p) {
    CHECK(psx_pbp_extract(p, out, &region, NULL, NULL, &err) != 0 && err.code == PSX_PBP_ERR_BLOCK);
    psx_pbp_close(p);
  }

  /* 5. not a PBP at all */
  CHECK(psx_pbp_peek_disc_id("/nonexistent/EBOOT.PBP", id, sizeof id) != 0);
  CHECK(psx_pbp_open(&p, "/nonexistent/EBOOT.PBP", &err) != 0 && err.code == PSX_PBP_ERR_UNREADABLE);
  free(raw);
  free(pk0);
  free(pk1);
  free(sectors);
}

/* ---- the region table's matchers (psx_region.h, D158) ---- */

static int has_none(void *user, const char *name) {
  (void)user, (void)name;
  return 0;
}
static int has_us(void *user, const char *name) {
  (void)user;
  return strcmp(name, "SCUS_942.28") == 0;
}

static void test_region(void) {
  const PsxRegion *r;
  CHECK((r = psx_region_by_disc_id("SCUS94228")) && r->id == PSX_REGION_US);
  CHECK((r = psx_region_by_disc_id("sces01438")) && r->id == PSX_REGION_PAL);
  CHECK((r = psx_region_by_disc_id("SCPS10083")) && r->id == PSX_REGION_JP && !r->supported);
  CHECK(psx_region_by_disc_id("NPUI94228") == NULL);
  CHECK(psx_region_by_disc_id("SCUS9422") == NULL);
  CHECK(psx_region_by_disc_id("") == NULL);
  CHECK((r = psx_region_of_disc("SCES_014.38", has_none, NULL)) && r->id == PSX_REGION_PAL);
  CHECK((r = psx_region_of_disc("", has_us, NULL)) && r->id == PSX_REGION_US);
  CHECK((r = psx_region_of_disc(NULL, has_us, NULL)) && r->id == PSX_REGION_US);
  CHECK((r = psx_region_of_disc("SLUS_000.00", has_us, NULL)) && r->id == PSX_REGION_US); /* boots something else: by the files */
  CHECK(psx_region_of_disc("SLUS_000.00", has_none, NULL) == NULL);
}

/* ---- the digital copies ---- */

static int same_file(const char *a, const char *b) {
  FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
  static unsigned char x[1 << 16], y[1 << 16];
  int same = fa && fb;
  while (same) {
    size_t n = fread(x, 1, sizeof x, fa), m = fread(y, 1, sizeof y, fb);
    if (n != m || memcmp(x, y, n) != 0)
      same = 0;
    if (n == 0)
      break;
  }
  if (fa)
    fclose(fa);
  if (fb)
    fclose(fb);
  return same;
}

static void progress(void *user, int percent, const char *file) {
  int *last = user;
  if (percent / 10 != *last / 10)
    fprintf(stderr, "  %d%% %s\n", percent, file);
  *last = percent;
}

/* ---- the copy read in place, as the game does (cdimage.c, D157): a folder
 * holding only the EBOOT.PBP opens as the disc, and every file read through
 * libcd is the extracted one; the native reads (cdnative.c) give the same
 * bytes ---- */

static long file_size(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static void test_in_place(const char *work, const char *name, const char *extracted, int supported) {
  char dir[512], link[512], target[520], path[600], line[600];
  FILE *m;
  unsigned char *ours, *theirs, raw[PSX_CD_RAW_SECTOR];
  int files = 0, xa_files = 0;
  snprintf(dir, sizeof dir, "%s/%s-disc", work, name);
  mkdir(dir, 0777);
  snprintf(link, sizeof link, "%s/EBOOT.PBP", dir);
  /* work is build/linux32/tests/pbp/work: the copy is five folders up */
  snprintf(target, sizeof target, "%s/../../../../../disc/psn/%s/EBOOT.PBP", work, name);
  remove(link);
  if (symlink(target, link) != 0 || file_size(link) < 0) {
    fprintf(stderr, "  (no link to the copy from %s: the in-place cases skipped)\n", dir);
    return;
  }
  if (!supported) { /* the negative control: recognised, refused as not supported yet */
    CHECK(psx_cd_open(dir) != 0 && psx_cd_error()->code == PSX_CD_ERR_NOT_SUPPORTED);
    fprintf(stderr, "  in place: %s (as it should be: %s)\n", psx_cd_error()->title, psx_cd_error()->text);
    return;
  }
  CHECK(psx_cd_open(dir) == 0);
  if (psx_cd_error()->code != PSX_CD_OK) {
    fprintf(stderr, "  %s: %s\n", psx_cd_error()->title, psx_cd_error()->text);
    return;
  }
  CHECK(psx_cdimage_pbp() != NULL);
  CHECK(strcmp(psx_cd_source(), link) == 0);
  snprintf(path, sizeof path, "%s/manifest.json", extracted);
  m = fopen(path, "rb");
  CHECK(m != NULL);
  if (!m)
    return;
  ours = malloc(8 << 20);
  theirs = malloc(8 << 20);
  while (fgets(line, sizeof line, m)) {
    char *q = strstr(line, "\"path\": \""), *e;
    long size;
    FILE *f;
    if (!q)
      continue;
    q += 9;
    e = strchr(q, '"');
    if (!e)
      continue;
    *e = 0;
    snprintf(path, sizeof path, "%s/%s", extracted, q);
    size = file_size(path);
    CHECK(size >= 0);
    f = fopen(path, "rb");
    if (!f)
      continue;
    files++;
    if (strstr(q, ".STR")) { /* an XA file: its sectors through psx_cd_read_raw, a sample of them */
      int lba = -1, sz = 0, k, nsect;
      /* the manifest's lba and size follow on the next lines */
      if (fgets(line, sizeof line, m) && sscanf(line, " \"lba\": %d", &lba) == 1 &&
          fgets(line, sizeof line, m) && sscanf(line, " \"size\": %d", &sz) == 1) {
        nsect = (sz + 2047) / 2048;
        for (k = 0; k < nsect; k += 997) {
          unsigned char want[PSX_CD_RAW_SECTOR];
          fseek(f, (long)k * PSX_CD_RAW_SECTOR, SEEK_SET);
          CHECK(fread(want, 1, PSX_CD_RAW_SECTOR, f) == PSX_CD_RAW_SECTOR);
          CHECK(psx_cd_read_raw(lba + k, raw) == 0);
          /* the game's reads leave the error codes zero (a Form 2 sector's EDC at 2332, a Form 1
           * filler sector's EDC and ECC from 2056); the extractor computes them (D158) */
          {
            int data = (raw[2] & 0x20) ? 2332 : 2056, j, tail = 0;
            for (j = data; j < PSX_CD_RAW_SECTOR; j++)
              tail |= raw[j];
            if (memcmp(raw, want, (size_t)data) != 0 || tail) {
              CHECK(!"an XA sector read in place differs from the extracted one");
              break;
            }
          }
        }
        xa_files++;
      } else {
        CHECK(!"manifest lines not as expected");
      }
    } else if (size <= 8 << 20) { /* a data file whole, through libcd */
      int got = psx_cd_read_file(q, ours, 8 << 20);
      CHECK(got == size);
      CHECK(fread(theirs, 1, (size_t)size, f) == (size_t)size);
      CHECK(got == size && memcmp(ours, theirs, (size_t)size) == 0);
    } else { /* WAD.WAD, SOURCE.TRD: the first 4 MB and the last 4 MB, through the native reads */
      int lba = -1, k;
      if (fgets(line, sizeof line, m) && sscanf(line, " \"lba\": %d", &lba) == 1) {
        int nsect = (int)((size + 2047) / 2048), at[2] = {0, nsect - 2048}, count = 2048;
        if (psx_cdn_start() == 0)
          for (k = 0; k < 2; k++) {
            long want = size - (long)at[k] * 2048 < (long)count * 2048 ? size - (long)at[k] * 2048 : (long)count * 2048;
            psx_cdn_read_post(lba + at[k], count, ours);
            psx_cdn_read_finish(lba + at[k], count, ours);
            fseek(f, (long)at[k] * 2048, SEEK_SET);
            CHECK(fread(theirs, 1, (size_t)want, f) == (size_t)want);
            CHECK(memcmp(ours, theirs, (size_t)want) == 0);
          }
        else
          CHECK(!"no native read worker");
      } else {
        CHECK(!"manifest lines not as expected");
      }
    }
    fclose(f);
  }
  fclose(m);
  free(ours);
  free(theirs);
  CHECK(xa_files == 6 && (files == 16 || files == 10));
  fprintf(stderr, "  in place: %d files read through libcd as the extracted ones (%d XA files sampled)\n", files, xa_files);
}

static int test_copy(const char *work, const char *dir, const char *name, const char *files_dir) {
  char path[512], out[512], line[600];
  PsxPbp *p;
  PsxPbpError err;
  unsigned char raw[PSX_PBP_SECTOR];
  int region = -1, last = -1, n = 0;
  FILE *m;
  snprintf(path, sizeof path, "%s/%s/EBOOT.PBP", dir, name);
  if (psx_pbp_open(&p, path, &err) != 0) {
    fprintf(stderr, "%s: %s (skipped)\n", path, err.text);
    return 0;
  }
  fprintf(stderr, "%s: %s \"%s\", %s, %d sectors\n", path, psx_pbp_disc_id(p), psx_pbp_title(p),
          psx_pbp_encrypted(p) ? "PGD" : "plain", psx_pbp_sectors(p));
  CHECK(psx_pbp_read_sectors(p, 16, 1, raw) == 0 && raw[24] == 1 && memcmp(raw + 25, "CD001", 5) == 0);
  snprintf(out, sizeof out, "%s/%s", work, name);
  CHECK(psx_pbp_extract(p, out, &region, progress, &last, &err) == 0);
  if (err.code != PSX_PBP_OK)
    fprintf(stderr, "  %s\n", err.text);
  psx_pbp_close(p);
  if (region < 0)
    return 1;
  fprintf(stderr, "  %s (%s): the executable and WAD.WAD match the region table\n",
          psx_region_info(region)->serial, psx_region_info(region)->area);
  snprintf(path, sizeof path, "%s/manifest.json", out);
  m = fopen(path, "rb");
  CHECK(m != NULL);
  if (!m)
    return 1;
  /* every "path" the manifest names, against the dump's files */
  while (fgets(line, sizeof line, m)) {
    char *q = strstr(line, "\"path\": \""), *e, dump[512], ours[512];
    if (!q)
      continue;
    q += 9;
    e = strchr(q, '"');
    if (!e)
      continue;
    *e = 0;
    n++;
    snprintf(ours, sizeof ours, "%s/%s", out, q);
    snprintf(dump, sizeof dump, "%s/%s", files_dir, q);
    if (region == PSX_REGION_US && files_dir[0]) {
      struct stat st;
      if (stat(dump, &st) == 0) {
        int same = same_file(ours, dump);
        CHECK(same);
        fprintf(stderr, "  %s %s\n", same ? "same" : "DIFFERS", q);
      }
    }
  }
  fclose(m);
  CHECK(n == (region == PSX_REGION_JP ? 10 : 16)); /* the Japanese disc has no S0/ demo */
  test_in_place(work, name, out, psx_region_info(region)->supported);
  return 1;
}

int main(int argc, char **argv) {
  const char *work = argc > 1 ? argv[1] : "pbp-work", *psn = argc > 2 ? argv[2] : "";
  const char *files = argc > 3 ? argv[3] : "";
  DIR *d;
  struct dirent *e;
  int copies = 0;
  mkdir(work, 0777);
  test_aes();
  test_region();
  test_fake(work);
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  d = psn[0] ? opendir(psn) : NULL;
  while (d && (e = readdir(d)))
    if (e->d_name[0] != '.')
      copies += test_copy(work, psn, e->d_name, files);
  if (d)
    closedir(d);
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  if (!copies) {
    fprintf(stderr, "no EBOOT.PBP under %s: the digital copies' cases skipped\n", psn[0] ? psn : "(none)");
    return 77;
  }
  fprintf(stderr, "OK: %d digital cop%s\n", copies, copies == 1 ? "y" : "ies");
  return 0;
}

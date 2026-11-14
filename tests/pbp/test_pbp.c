/* The PS1 Classic EBOOT.PBP reader (src/platform/common/pbp.c, decisions
 * D157). usage: test_pbp WORK [PSN_DIR [FILES_DIR]]
 *   WORK       scratch folder (made)
 *   PSN_DIR    folders holding an EBOOT.PBP each (disc/psn/NPUI94228/...,
 *              or build/linux32/pbp-made/... from tests/pbp/make_pbp.py); each is
 *              extracted into WORK/<folder> and must pass the region
 *              table's hashes; without any, those cases skip (exit 77)
 *   FILES_DIR  the extracted dump (disc/files): every file the manifest
 *              names must match it byte for byte (US copies only)
 *
 * Without a copy the cases are: AES against FIPS 197, the region matchers,
 * PBPs the test makes (stored, deflate and LZRC blocks, the last from the
 * LZRC encoder below, which codes every branch of the decoder's model), and
 * hostile ones (headers, tables and streams that must be refused). The PGD
 * container's decryption runs only on a real PlayStation Store copy. */
#define _FILE_OFFSET_BITS 64 /* the 2 GiB file's case */
#include "psx_aes128.h"
#include "psx_cd.h"
#include "psx_cdimage.h"
#include "psx_cdnative.h"
#include "psx_hooks.h"
#include "psx_pbp.h"
#include "psx_region.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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
    /* the extractor computed the error codes for its own files only: the handle's reads are as before it */
    CHECK(psx_pbp_read_sectors(p, 20, 1, sectors) == 0 && (sectors[16 + 2332] | sectors[16 + 2335]) == 0);
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
  { /* names that would break manifest.json for its readers: refused, as ".." is */
    static const char *const names[] = {"A\"B.BIN", "A{B.BIN", "A}B.BIN", "A\nB.BIN", "A\\B.BIN"};
    for (i = 0; i < sizeof names / sizeof *names; i++) {
      iso_image(raw, names[i], 2048, 12 * 2048, 0, xa_pattern_sector);
      size[0] = stored_deflate(pk0, raw), size[1] = stored_deflate(pk1, raw + BLOCK);
      CHECK(make_pbp(path, 2, packed, size, marker) != 0);
      CHECK(psx_pbp_open(&p, path, &err) == 0);
      if (p) {
        CHECK(psx_pbp_extract(p, out, &region, NULL, NULL, &err) != 0 && err.code == PSX_PBP_ERR_BLOCK);
        psx_pbp_close(p);
      }
    }
  }

  /* 5. not a PBP at all */
  CHECK(psx_pbp_peek_disc_id("/nonexistent/EBOOT.PBP", id, sizeof id) != 0);
  CHECK(psx_pbp_open(&p, "/nonexistent/EBOOT.PBP", &err) != 0 && err.code == PSX_PBP_ERR_UNREADABLE);
  free(raw);
  free(pk0);
  free(pk1);
  free(sectors);
}

/* ---- an LZRC encoder: the decoder's model (pbp.c's lzrc_decompress) run
 * the other way, so a block here is compressed for real: adaptive literals,
 * matches of every length class, distances of every class with their direct
 * bits, the states between. It is written from the decoder, not from Sony's
 * encoder: it shows the decoder decodes every branch of its own model and
 * keeps doing so; that the model is the PlayStation Store's is what the
 * copies under disc/psn show (D157). ---- */

typedef struct {
  unsigned char *out;
  unsigned n, cap;
  unsigned long long low; /* the range coder, with LZMA's carry handling */
  unsigned range, cache_size;
  unsigned char cache;
  unsigned state, pos, prev, lc; /* the model's, as the decoder keeps them */
  unsigned char literal[8 * 255], dist_bits[24 * 8], dist[15 * 8], match[8 * 8], len[7 * 64];
} Enc;

static void enc_put(Enc *e, unsigned char b) {
  if (e->n < e->cap)
    e->out[e->n] = b;
  e->n++;
}

static void enc_shift(Enc *e) {
  if ((unsigned)e->low < 0xFF000000u || (unsigned)(e->low >> 32) != 0) {
    unsigned char c = e->cache;
    do {
      enc_put(e, (unsigned char)(c + (unsigned char)(e->low >> 32)));
      c = 0xFF;
    } while (--e->cache_size);
    e->cache = (unsigned char)((unsigned)e->low >> 24);
  }
  e->cache_size++;
  e->low = (unsigned long long)((unsigned)e->low << 8);
}

/* where the decoder takes a byte: once, before a bit, when the range is under 2^24 */
static void enc_normalize(Enc *e) {
  if (e->range < 0x01000000u) {
    e->range <<= 8;
    enc_shift(e);
  }
}

static void enc_bit(Enc *e, unsigned char *prob, unsigned bit) {
  unsigned bound, p = *prob;
  enc_normalize(e);
  bound = (e->range >> 8) * p;
  p -= p >> 3;
  if (bit) {
    e->range = bound;
    *prob = (unsigned char)(p + 31);
  } else {
    e->low += bound;
    e->range -= bound;
    *prob = (unsigned char)p;
  }
}

/* v, a number of n + 2 bits whose top bit is set, in rc_number's order: bits
 * n and n - 1 under node 3, the middle ones direct, the low three under
 * nodes 0, 1, 2 */
static void enc_number(Enc *e, unsigned char *cell, int stride, unsigned n, unsigned v) {
  int k = (int)n;
  unsigned i;
  if (n >= 3) {
    enc_bit(e, cell + 3 * stride, (v >> k) & 1), k--;
    if (n >= 4) {
      enc_bit(e, cell + 3 * stride, (v >> k) & 1), k--;
      if (n >= 5) {
        enc_normalize(e);
        for (i = n; i >= 5; i--, k--) {
          e->range >>= 1;
          if (!((v >> k) & 1))
            e->low += e->range;
        }
      }
    }
  }
  enc_bit(e, cell, (v >> k) & 1), k--;
  if (n >= 1) {
    enc_bit(e, cell + stride, (v >> k) & 1), k--;
    if (n >= 2)
      enc_bit(e, cell + 2 * stride, (v >> k) & 1);
  }
}

static void enc_init(Enc *e, unsigned char *out, unsigned cap, unsigned lc) {
  memset(e, 0, sizeof *e);
  e->out = out;
  e->cap = cap;
  e->range = 0xFFFFFFFFu;
  e->cache_size = 1;
  e->lc = lc;
  memset(e->literal, 0x80, sizeof e->literal);
  memset(e->dist_bits, 0x80, sizeof e->dist_bits);
  memset(e->dist, 0x80, sizeof e->dist);
  memset(e->match, 0x80, sizeof e->match);
  memset(e->len, 0x80, sizeof e->len);
}

static void enc_literal(Enc *e, unsigned char byte) {
  unsigned ctx, node = 1;
  int i;
  enc_bit(e, e->match + e->state, 0);
  if (e->state > 0)
    e->state--;
  ctx = ((((e->pos & 7) << 8) | e->prev) >> e->lc) & 7;
  for (i = 7; i >= 0; i--) {
    unsigned b = (byte >> i) & 1u;
    enc_bit(e, e->literal + ctx * 255 + node - 1, b);
    node = (node << 1) | b;
  }
  e->pos++;
  e->prev = byte;
}

static unsigned log2_floor(unsigned v) {
  unsigned n = 0;
  while (v >> (n + 1))
    n++;
  return n;
}

/* count bytes (2..256) from distance back (1..65535; 1..255 for a count
 * under 5); last: the last byte copied */
static void enc_match(Enc *e, unsigned distance, unsigned count, unsigned char last) {
  unsigned length = count - 1, bits = log2_floor(length), big = bits >= 2, sym, node = 1, step;
  int i;
  enc_bit(e, e->match + e->state, 1);
  for (step = 1; step <= 7; step++) {
    enc_bit(e, e->match + step * 8 + e->state, step <= bits);
    if (step > bits)
      break;
  }
  if (bits > 0) {
    unsigned lc = bits - 1, posbits = (e->pos << lc) & 3;
    enc_number(e, e->len + lc * 64 + posbits * 8 + e->state, 8, lc, length);
  }
  sym = distance >= 2 ? log2_floor(distance) : 0; /* the distance's class: 3 bits, or 4 for a long match */
  for (i = big ? 3 : 2; i >= 0; i--) {
    unsigned b = (sym >> i) & 1u;
    enc_bit(e, e->dist_bits + (node + 7 * big) * 8 + bits, b);
    node = (node << 1) | b;
  }
  if (sym)
    enc_number(e, e->dist + (sym - 1) * 8 + 3, -1, sym - 1, distance);
  e->pos += count;
  e->state = ((e->pos + 1) & 1) + 6;
  e->prev = last;
}

/* the stream's end: the coder flushed, the context shift in the first byte.
 * Returns its length, or 0 if it didn't fit. */
static unsigned enc_finish(Enc *e) {
  int i;
  for (i = 0; i < 5; i++)
    enc_shift(e);
  if (e->n > e->cap)
    return 0;
  e->out[0] = (unsigned char)e->lc;
  return e->n;
}

static unsigned rnd(unsigned *seed) {
  *seed = *seed * 1664525u + 1013904223u;
  return *seed >> 8;
}

/* A block of BLOCK bytes at raw, built token by token (literal runs, and
 * matches of any length class with any distance class the block reaches
 * yet) and coded into out as it is built. Returns the stream's length;
 * classes[]: how many matches of each length class (0..7) and distance class
 * (8 + 0..15) it holds. */
static unsigned lzrc_block(unsigned char *raw, unsigned char *out, unsigned cap, unsigned lc, unsigned seed,
                           unsigned classes[24]) {
  static Enc e;
  unsigned i;
  enc_init(&e, out, cap, lc);
  memset(classes, 0, 24 * sizeof *classes);
  while (e.pos < BLOCK) {
    unsigned left = BLOCK - e.pos;
    if (e.pos < 64 || left < 2 || rnd(&seed) % 3 == 0) { /* literals: a run of text-like and of any bytes */
      unsigned run = 1 + rnd(&seed) % 6;
      for (i = 0; i < run && e.pos < BLOCK; i++) {
        unsigned char b = (unsigned char)(rnd(&seed) % 4 ? 'a' + rnd(&seed) % 20 : rnd(&seed));
        raw[e.pos] = b;
        enc_literal(&e, b);
      }
    } else {
      unsigned bits = rnd(&seed) % 8, length, count, sym, distance, at = e.pos;
      length = bits ? (1u << bits) + rnd(&seed) % (1u << bits) : 1;
      count = length + 1;
      if (count > left)
        count = left, length = count - 1, bits = log2_floor(length);
      sym = rnd(&seed) % (bits >= 2 ? 16 : 8); /* any class with any length */
      if (rnd(&seed) % 4 == 0) /* one in four: the furthest class this match may have, so the last ones get their share */
        sym = bits >= 2 ? 15 : 7;
      while (sym && (1u << sym) > e.pos) /* no further back than the block reaches yet */
        sym--;
      distance = sym ? (1u << sym) + rnd(&seed) % (1u << sym) : 1;
      if (distance > e.pos)
        distance = e.pos;
      for (i = 0; i < count; i++) /* byte by byte: a distance under the count repeats */
        raw[at + i] = raw[at + i - distance];
      enc_match(&e, distance, count, raw[at + count - 1]);
      classes[log2_floor(length)]++;
      classes[8 + (distance >= 2 ? log2_floor(distance) : 0)]++;
    }
  }
  return enc_finish(&e);
}

/* deflate with the fixed codes, by hand: a bit writer (codes go in from
 * their top bit, everything else from the bottom) */
typedef struct {
  unsigned char *out;
  unsigned n, bit;
} Bits;
static void bits_put(Bits *b, unsigned v, int n, int code) {
  int i;
  for (i = 0; i < n; i++) {
    unsigned x = code ? (v >> (n - 1 - i)) & 1u : (v >> i) & 1u;
    if (b->bit == 0)
      b->out[b->n] = 0;
    b->out[b->n] |= (unsigned char)(x << b->bit);
    if (++b->bit == 8)
      b->bit = 0, b->n++;
  }
}
/* one final fixed-code block: a zero byte, then `matches` copies of 258
 * bytes from one byte back (1 + 258 * matches zero bytes) */
static unsigned fixed_zeros(unsigned char *out, unsigned matches) {
  Bits b = {out, 0, 0};
  unsigned i;
  bits_put(&b, 1, 1, 0);       /* the last block */
  bits_put(&b, 1, 2, 0);       /* fixed codes */
  bits_put(&b, 0x30, 8, 1);    /* literal 0 */
  for (i = 0; i < matches; i++) {
    bits_put(&b, 0xC5, 8, 1);  /* length code 285: 258 bytes */
    bits_put(&b, 0, 5, 1);     /* distance code 0: one byte back */
  }
  bits_put(&b, 0, 7, 1);       /* end of block */
  return b.n + (b.bit != 0);
}

/* bytes at off of the file at path changed, or the file cut to off (data NULL) */
static void poke(const char *path, long off, const void *data, unsigned n) {
  FILE *f = fopen(path, "r+b");
  CHECK(f != NULL);
  if (!f)
    return;
  if (data) {
    fseek(f, off, SEEK_SET);
    CHECK(fwrite(data, 1, n, f) == n);
  }
  fclose(f);
  if (!data)
    CHECK(truncate(path, (off_t)off) == 0);
}

static void poke32(const char *path, long off, unsigned v) {
  unsigned char b[4];
  put32(b, v);
  poke(path, off, b, 4);
}

/* psx_pbp_open of path must fail with code; the text is there for the player */
static void refused(const char *path, int code, const char *what) {
  PsxPbp *p = (PsxPbp *)1;
  PsxPbpError err;
  int r;
  memset(&err, 0, sizeof err);
  r = psx_pbp_open(&p, path, &err);
  if (r == 0 || p != NULL || err.code != code || !err.text[0]) {
    fprintf(stderr, "FAIL: %s: open gave %d, code %d (wanted %d), \"%s\"\n", what, r, err.code, code, err.text);
    failures++;
  }
  if (r == 0 && p)
    psx_pbp_close(p);
}

#define DATA_AT (PSAR + 0x100000) /* the file offset of the first block's bytes in make_pbp's files */

/* A real LZRC stream: compressed blocks read back whole and in parts, and
 * hostile ones refused. */
static void test_lzrc(const char *work) {
  char path[512];
  unsigned char *raw = malloc(4 * BLOCK), *pk[4], *sectors = malloc(64 * PSX_PBP_SECTOR), *zero = calloc(1, BLOCK);
  const unsigned char *packed[4];
  unsigned size[4], marker[4], classes[24], sum[24] = {0}, i, k;
  PsxPbp *p = NULL;
  PsxPbpError err;
  for (i = 0; i < 4; i++)
    pk[i] = malloc(BLOCK + 64);
  CHECK(raw && sectors && zero && pk[0] && pk[1] && pk[2] && pk[3]);
  if (!raw || !sectors || !zero || !pk[0] || !pk[1] || !pk[2] || !pk[3])
    return;
  snprintf(path, sizeof path, "%s/lzrc.pbp", work);

  /* 1. four blocks: mixed tokens with context shifts 0 and 5, a block of
   * zeros (one literal, then 256-byte matches one byte back), and a deflate
   * block between LZRC ones (the marker chooses the decoder block by block) */
  size[0] = lzrc_block(raw, pk[0], BLOCK + 64, 0, 1, classes);
  for (i = 0; i < 24; i++)
    sum[i] += classes[i];
  size[1] = lzrc_block(raw + BLOCK, pk[1], BLOCK + 64, 5, 2, classes);
  for (i = 0; i < 24; i++)
    sum[i] += classes[i];
  {
    static Enc e;
    enc_init(&e, pk[2], BLOCK + 64, 3);
    enc_literal(&e, 0);
    while (e.pos < BLOCK) {
      unsigned count = BLOCK - e.pos < 256 ? BLOCK - e.pos : 256;
      if (count < 2) {
        enc_literal(&e, 0);
        continue;
      }
      enc_match(&e, 1, count, 0);
    }
    size[2] = enc_finish(&e);
    memset(raw + 2 * BLOCK, 0, BLOCK);
  }
  for (i = 0; i < BLOCK; i++)
    raw[3 * BLOCK + i] = pattern(1, i);
  size[3] = stored_deflate(pk[3], raw + 3 * BLOCK);
  CHECK(size[0] && size[0] < BLOCK && size[1] && size[1] < BLOCK); /* compressed, not stored */
  CHECK(size[2] && size[2] < 400);                                 /* 37,632 zeros in a few hundred bytes */
  for (i = 0; i < 8; i++)
    CHECK(sum[i] > 20); /* every length class: 2, 3-4, 5-8 ... 129-256 bytes */
  for (i = 0; i < 16; i++)
    CHECK(sum[8 + i] >= 5); /* every distance class, to 32768 and over */
  fprintf(stderr, "lzrc: blocks of %u bytes coded in %u, %u and %u (zeros); matches by length class", BLOCK, size[0],
          size[1], size[2]);
  for (i = 0; i < 8; i++)
    fprintf(stderr, " %u", sum[i]);
  fprintf(stderr, ", by distance class");
  for (i = 0; i < 16; i++)
    fprintf(stderr, " %u", sum[8 + i]);
  fprintf(stderr, "\n");
  /* the order in the file: LZRC, zeros, deflate, LZRC */
  packed[0] = pk[0], packed[1] = pk[2], packed[2] = pk[3], packed[3] = pk[1];
  {
    unsigned s0 = size[0], s1 = size[1], s2 = size[2], s3 = size[3];
    size[0] = s0, size[1] = s2, size[2] = s3, size[3] = s1;
  }
  marker[0] = marker[1] = marker[3] = 1, marker[2] = 0;
  CHECK(make_pbp(path, 4, packed, size, marker) != 0);
  CHECK(psx_pbp_open(&p, path, &err) == 0);
  if (p) {
    CHECK(psx_pbp_sectors(p) == 64);
    CHECK(psx_pbp_read_sectors(p, 0, 64, sectors) == 0); /* the four in one run */
    CHECK(memcmp(sectors, raw, BLOCK) == 0);
    CHECK(memcmp(sectors + BLOCK, zero, BLOCK) == 0);
    CHECK(memcmp(sectors + 2 * BLOCK, raw + 3 * BLOCK, BLOCK) == 0);
    CHECK(memcmp(sectors + 3 * BLOCK, raw + BLOCK, BLOCK) == 0);
    for (k = 0; k < 64; k += 5) { /* sector by sector, backwards and forwards across the blocks */
      unsigned lba = (k * 37) % 64;
      const unsigned char *want = lba < 16 ? raw + lba * PSX_PBP_SECTOR
                                  : lba < 32 ? zero
                                  : lba < 48 ? raw + 3 * BLOCK + (lba - 32) * PSX_PBP_SECTOR
                                             : raw + BLOCK + (lba - 48) * PSX_PBP_SECTOR;
      CHECK(psx_pbp_read_sectors(p, (int)lba, 1, sectors) == 0 && memcmp(sectors, want, PSX_PBP_SECTOR) == 0);
    }
    { /* a clone decodes the same from its own handle */
      PsxPbp *c = NULL;
      CHECK(psx_pbp_clone(p, &c) == 0 && c);
      if (c) {
        CHECK(psx_pbp_read_sectors(c, 40, 20, sectors) == 0);
        CHECK(memcmp(sectors, raw + 3 * BLOCK + 8 * PSX_PBP_SECTOR, 8 * PSX_PBP_SECTOR) == 0);
        CHECK(memcmp(sectors + 8 * PSX_PBP_SECTOR, raw + BLOCK, 12 * PSX_PBP_SECTOR) == 0);
        psx_pbp_close(c);
      }
    }

    /* 2. the first stream damaged a byte at a time, 400 times over: each
     * read fails or gives a block, and none hangs, crashes or leaves the
     * handle unusable (the stream as it was decodes again after) */
    {
      unsigned seed = 99, ok = 0, bad = 0;
      for (k = 0; k < 400; k++) {
        unsigned at = k < 16 ? k : rnd(&seed) % size[0];
        unsigned char was = pk[0][at], now = (unsigned char)(was ^ (1u << (rnd(&seed) % 8)));
        PsxPbp *c = NULL;
        poke(path, DATA_AT + (long)at, &now, 1);
        if (psx_pbp_clone(p, &c) == 0 && c) {
          if (psx_pbp_read_sectors(c, 0, 16, sectors) == 0)
            ok++;
          else
            bad++;
          psx_pbp_close(c);
        }
        poke(path, DATA_AT + (long)at, &was, 1);
      }
      CHECK(ok + bad == 400 && bad > 0);
      fprintf(stderr, "lzrc: of 400 one-bit changes of a stream %u were refused, %u decoded (to other bytes)\n", bad, ok);
      { /* p's own cache holds the good run: a fresh handle reads the restored file */
        PsxPbp *c = NULL;
        CHECK(psx_pbp_clone(p, &c) == 0 && c && psx_pbp_read_sectors(c, 0, 16, sectors) == 0 &&
              memcmp(sectors, raw, BLOCK) == 0);
        psx_pbp_close(c);
      }
    }
    psx_pbp_close(p);
  }

  /* 3. streams that must be refused, each as block 1 after a good block 0 */
  {
    static Enc e;
    unsigned char *bad = pk[3];
    unsigned n, good = size[0];
    struct {
      const char *what;
      unsigned n;
    } cases[8];
    unsigned ncases = 0;
    unsigned char *all = malloc(8 * (BLOCK + 64));
    CHECK(all != NULL);
    if (!all)
      return;
#define ADD(name, len)                                                         \
  do {                                                                         \
    memcpy(all + ncases * (BLOCK + 64), bad, (len));                           \
    cases[ncases].what = (name), cases[ncases].n = (len), ncases++;            \
  } while (0)
    /* a match before any byte exists to copy */
    enc_init(&e, bad, BLOCK + 64, 0);
    enc_match(&e, 1, 2, 0);
    n = enc_finish(&e);
    ADD("a match at the block's start", n);
    /* a match that runs past the block's end */
    enc_init(&e, bad, BLOCK + 64, 0);
    for (i = 0; i < BLOCK - 1; i++)
      enc_literal(&e, (unsigned char)(i * 7));
    enc_match(&e, 1, 2, 0);
    n = enc_finish(&e);
    CHECK(n != 0);
    ADD("a match past the block's end", n);
    /* a distance further back than the bytes decoded */
    enc_init(&e, bad, BLOCK + 64, 0);
    for (i = 0; i < 10; i++)
      enc_literal(&e, (unsigned char)i);
    enc_match(&e, 11, 8, 0);
    n = enc_finish(&e);
    ADD("a distance before the block's start", n);
    /* the good stream cut in half: the coder runs out of input */
    memcpy(bad, pk[0], good / 2);
    ADD("a stream cut in half", good / 2);
    /* a stored stream longer than its input, and one longer than a block */
    bad[0] = 0x80, bad[1] = 0, bad[2] = 0, bad[3] = BLOCK >> 8, bad[4] = BLOCK & 0xFF;
    ADD("a stored stream longer than its bytes", 100);
    bad[1] = 0, bad[2] = 0, bad[3] = (BLOCK + 1) >> 8, bad[4] = (BLOCK + 1) & 0xFF;
    ADD("a stored stream longer than a block", BLOCK + 6);
    /* too short to hold the coder's first bytes */
    ADD("a stream of 3 bytes", 3);
    for (k = 0; k < ncases; k++) {
      packed[0] = pk[0], packed[1] = all + k * (BLOCK + 64);
      size[0] = good, size[1] = cases[k].n;
      marker[0] = marker[1] = 1;
      CHECK(make_pbp(path, 2, packed, size, marker) != 0);
      p = NULL;
      CHECK(psx_pbp_open(&p, path, &err) == 0);
      if (!p)
        continue;
      if (psx_pbp_read_sectors(p, 16, 1, sectors) == 0) {
        fprintf(stderr, "FAIL: lzrc: %s was decoded\n", cases[k].what);
        failures++;
      }
      /* the damaged block doesn't take the good one with it, in one request or after */
      CHECK(psx_pbp_read_sectors(p, 0, 32, sectors) != 0);
      CHECK(psx_pbp_read_sectors(p, 0, 16, sectors) == 0 && memcmp(sectors, raw, BLOCK) == 0);
      psx_pbp_close(p);
    }
#undef ADD
    free(all);
  }
  for (i = 0; i < 4; i++)
    free(pk[i]);
  free(raw);
  free(sectors);
  free(zero);
}

/* Hostile deflate streams, headers and tables: every one refused with its
 * code and a text, none read past its buffers (the checks are pbp.c's; this
 * holds them in place). */
static void test_hostile(const char *work) {
  char path[512], id[16];
  unsigned char *raw = malloc(BLOCK), *pk0 = malloc(BLOCK + 64), *pk1 = malloc(BLOCK + 64);
  unsigned char *sectors = malloc(32 * PSX_PBP_SECTOR);
  const unsigned char *packed[2];
  unsigned size[2], marker[2] = {0, 0}, i, filesize;
  PsxPbp *p = NULL;
  PsxPbpError err;
  CHECK(raw && pk0 && pk1 && sectors);
  if (!raw || !pk0 || !pk1 || !sectors)
    return;
  snprintf(path, sizeof path, "%s/hostile.pbp", work);
  for (i = 0; i < BLOCK; i++)
    raw[i] = pattern(0, i);
  packed[0] = pk0, packed[1] = pk1;

  /* 1. deflate: streams that fit a block decode, the same ones too long, cut
   * short or pointing before their output don't */
  {
    static const unsigned char before_start[2] = {0x03, 0x02}; /* fixed codes: 3 bytes from 1 back, at byte 0 */
    static const unsigned char bad_stored[5] = {0x01, 0x00, 0x10, 0x00, 0x10}; /* a stored block whose two lengths disagree */
    static const unsigned char reserved[1] = {0x07};                         /* block type 3 */
    struct {
      const char *what;
      const unsigned char *data;
      unsigned n;
      int ok;
    } cases[8];
    unsigned ncases = 0, n_fit, n_over, k;
    unsigned char *fit = malloc(4096), *over = malloc(4096);
    CHECK(fit && over);
    if (!fit || !over)
      return;
    n_fit = fixed_zeros(fit, 145);   /* 1 + 145 * 258 = 37,411 bytes: under a block, the rest zero */
    n_over = fixed_zeros(over, 146); /* 37,669: 37 bytes over */
    cases[ncases].what = "a fixed-code block of 37,411 zeros", cases[ncases].data = fit, cases[ncases].n = n_fit,
    cases[ncases++].ok = 1;
    cases[ncases].what = "a fixed-code block 37 bytes over", cases[ncases].data = over, cases[ncases].n = n_over,
    cases[ncases++].ok = 0;
    cases[ncases].what = "a fixed-code block cut before its end", cases[ncases].data = fit, cases[ncases].n = n_fit / 2,
    cases[ncases++].ok = 0;
    cases[ncases].what = "a match before the output's start", cases[ncases].data = before_start, cases[ncases].n = 2,
    cases[ncases++].ok = 0;
    cases[ncases].what = "a stored block with unequal lengths", cases[ncases].data = bad_stored, cases[ncases].n = 5,
    cases[ncases++].ok = 0;
    cases[ncases].what = "a reserved block type", cases[ncases].data = reserved, cases[ncases].n = 1,
    cases[ncases++].ok = 0;
    for (k = 0; k < ncases; k++) {
      size[0] = stored_deflate(pk0, raw);
      packed[1] = cases[k].data, size[1] = cases[k].n;
      CHECK(make_pbp(path, 2, packed, size, marker) != 0);
      p = NULL;
      CHECK(psx_pbp_open(&p, path, &err) == 0);
      if (!p)
        continue;
      memset(sectors, 0xAA, 16 * PSX_PBP_SECTOR);
      if ((psx_pbp_read_sectors(p, 16, 16, sectors) == 0) != cases[k].ok) {
        fprintf(stderr, "FAIL: deflate: %s %s\n", cases[k].what, cases[k].ok ? "wasn't decoded" : "was decoded");
        failures++;
      }
      if (cases[k].ok) { /* all zeros, the short block's tail too */
        for (i = 0; i < BLOCK && sectors[i] == 0; i++)
          ;
        CHECK(i == BLOCK);
      }
      CHECK(psx_pbp_read_sectors(p, 0, 16, sectors) == 0 && memcmp(sectors, raw, BLOCK) == 0);
      psx_pbp_close(p);
    }
    free(fit);
    free(over);
    packed[1] = pk1;
  }

  /* 2. the PBP's own header and PARAM.SFO. make_pbp's file: the header's
   * offsets at 8.., PARAM.SFO at 0x28, DATA.PSAR at 0x400 */
  size[0] = stored_deflate(pk0, raw);
  filesize = make_pbp(path, 1, packed, size, marker);
  CHECK(filesize != 0);
#define FRESH() CHECK(make_pbp(path, 1, packed, size, marker) == filesize)
  poke32(path, 8, 8); /* PARAM.SFO inside the header */
  refused(path, PSX_PBP_ERR_NOT_PBP, "PARAM.SFO inside the header");
  CHECK(psx_pbp_peek_disc_id(path, id, sizeof id) != 0);
  FRESH();
  poke32(path, 12, 0x20); /* its end before its start */
  refused(path, PSX_PBP_ERR_NOT_PBP, "PARAM.SFO ending before it starts");
  FRESH();
  poke32(path, 12, 0x28 + (1u << 16) + 1); /* 64 KB and a byte: larger than any */
  refused(path, PSX_PBP_ERR_NOT_PBP, "a PARAM.SFO over 64 KB");
  FRESH();
  poke32(path, 36, filesize + 100); /* DATA.PSAR past the file */
  refused(path, PSX_PBP_ERR_NOT_PBP, "DATA.PSAR past the file");
  CHECK(psx_pbp_peek_disc_id(path, id, sizeof id) != 0);
  FRESH();
  poke32(path, 36, 0xFFFFFFF0u);
  refused(path, PSX_PBP_ERR_NOT_PBP, "DATA.PSAR at 4 GB");
  FRESH();
  poke32(path, 36, 0x30); /* inside PARAM.SFO */
  refused(path, PSX_PBP_ERR_NOT_PBP, "DATA.PSAR inside PARAM.SFO");
  FRESH();
  poke(path, 0, "\0PBX", 4);
  refused(path, PSX_PBP_ERR_NOT_PBP, "another magic");
  CHECK(psx_pbp_peek_disc_id(path, id, sizeof id) != 0);
  /* PARAM.SFO's tables pointing out of it: no disc ID, and the image still opens */
  {
    static const struct {
      long off;
      unsigned v;
    } sfo[] = {{0x28 + 8, 0xFFFFFFF0u},  /* the key table */
               {0x28 + 12, 0xFFFFFFF0u}, /* the data table */
               {0x28 + 16, 0x7FFFFFFFu}, /* 2 billion entries */
               {0x28 + 24, 0xFFFFFFFFu}, /* the entry's length */
               {0x28 + 32, 0xFFFFFFF0u}, /* the entry's data offset */
               {0x28 + 20, 0x0204FFFFu}}; /* the entry's key offset */
    for (i = 0; i < sizeof sfo / sizeof *sfo; i++) {
      FRESH();
      poke32(path, sfo[i].off, sfo[i].v);
      strcpy(id, "unset");
      CHECK(psx_pbp_peek_disc_id(path, id, sizeof id) == 0);
      CHECK(i == 2 ? strcmp(id, "TEST00001") == 0 : id[0] == 0); /* the count alone harms nothing: the entry is there */
      p = NULL;
      CHECK(psx_pbp_open(&p, path, &err) == 0 && p);
      if (p) {
        CHECK(i == 2 ? strcmp(psx_pbp_disc_id(p), "TEST00001") == 0 : psx_pbp_disc_id(p)[0] == 0);
        psx_pbp_close(p);
      }
    }
  }
  /* an ID longer than the caller's buffer is cut, not written past it */
  FRESH();
  {
    char small[4] = "zzz", guard = small[3];
    CHECK(psx_pbp_peek_disc_id(path, small, 3) == 0 && strcmp(small, "TE") == 0 && small[3] == guard);
  }
  /* no room at all: -1, and no file handle is left open however often it is
   * asked (with the process allowed 64 handles for the while, 300 asks that
   * each kept one would leave none for the read after them) */
  {
    struct rlimit was, low;
    int limited = getrlimit(RLIMIT_NOFILE, &was) == 0;
    if (limited) {
      low = was;
      low.rlim_cur = 64;
      limited = setrlimit(RLIMIT_NOFILE, &low) == 0;
    }
    for (i = 0; i < 300; i++)
      if (psx_pbp_peek_disc_id(path, id, 0) != -1)
        break;
    CHECK(i == 300);
    CHECK(psx_pbp_peek_disc_id(path, id, sizeof id) == 0 && strcmp(id, "TEST00001") == 0);
    if (limited)
      setrlimit(RLIMIT_NOFILE, &was);
    else
      fprintf(stderr, "  (the handle limit couldn't be lowered: the leak case checked less than it says)\n");
  }

  /* 3. DATA.PSAR's image header and block table */
  FRESH();
  poke(path, PSAR, "PSTITLEIMG000000", 16);
  refused(path, PSX_PBP_ERR_MULTI_DISC, "a multi-disc title");
  FRESH();
  poke(path, PSAR, "NPUMDIMG", 8);
  refused(path, PSX_PBP_ERR_NOT_PBP, "a PSP game's PBP");
  FRESH();
  poke(path, PSAR + 0x400 + 0xB6600 - 1, NULL, 0); /* the file ends a byte inside the image header */
  refused(path, PSX_PBP_ERR_UNREADABLE, "a file cut inside the image header");
  FRESH();
  poke32(path, PSAR + 0xBFC, filesize); /* the blocks' base past the file */
  refused(path, PSX_PBP_ERR_BLOCK, "a data base past the file");
  FRESH();
  poke32(path, PSAR + 0xBFC, 0xFFFFFFFFu);
  refused(path, PSX_PBP_ERR_BLOCK, "a data base at 4 GB");
  FRESH();
  poke32(path, PSAR + 0x4004, 0); /* entry 0's size: the table ends at once */
  refused(path, PSX_PBP_ERR_BLOCK, "an empty block table");
  FRESH();
  poke32(path, PSAR + 0x4004, BLOCK + 65); /* a block larger than any block's stored size */
  refused(path, PSX_PBP_ERR_BLOCK, "a block of 0x9341 bytes");
  FRESH();
  poke32(path, PSAR + 0x4000, 0xFFFFFFF0u); /* an offset that wraps with its size */
  refused(path, PSX_PBP_ERR_BLOCK, "a block offset near 4 GB");
  FRESH();
  poke32(path, PSAR + 0x4000, filesize - DATA_AT - size[0] + 1); /* the block's last byte is one past the file */
  refused(path, PSX_PBP_ERR_BLOCK, "a block one byte past the file");
  FRESH();
  poke32(path, PSAR + 0x4000 + 32, 0x7FFFFFF0u); /* a second entry, far past the file */
  poke32(path, PSAR + 0x4004 + 32, 0x100);
  refused(path, PSX_PBP_ERR_BLOCK, "a second block past the file");
  /* a file that ends inside a block the table accepted (cut after the open): the read fails, nothing else */
  FRESH();
  p = NULL;
  CHECK(psx_pbp_open(&p, path, &err) == 0 && p);
  if (p) {
    poke(path, (long)filesize - 100, NULL, 0);
    CHECK(psx_pbp_read_sectors(p, 0, 16, sectors) != 0);
    CHECK(psx_pbp_read_sectors(p, -1, 1, sectors) != 0 && psx_pbp_read_sectors(p, 16, 1, sectors) != 0);
    psx_pbp_close(p);
  }

  /* 4. a PGD where the image header is, which isn't one Sony signed: refused
   * as a PGD it can't read, by the DRM type or by the first MAC (the checks
   * that come before any decryption; a real one needs a real copy) */
  FRESH();
  {
    unsigned char pgd[0x90];
    memset(pgd, 0x5A, sizeof pgd);
    memcpy(pgd, "\0PGD", 4);
    put32(pgd + 4, 1);
    put32(pgd + 8, 1); /* DRM type 1: the fixed keys' kind */
    poke(path, PSAR + 0x400, pgd, sizeof pgd);
    refused(path, PSX_PBP_ERR_PGD, "a PGD with a wrong signature");
    put32(pgd + 4, 2); /* key index 2: the MAC wrapped once more */
    poke(path, PSAR + 0x400, pgd, sizeof pgd);
    refused(path, PSX_PBP_ERR_PGD, "a PGD of key index 2 with a wrong signature");
    put32(pgd + 8, 2); /* a console's own key */
    poke(path, PSAR + 0x400, pgd, sizeof pgd);
    refused(path, PSX_PBP_ERR_PGD, "a PGD of DRM type 2");
  }

  /* 5. a file of 2 GiB and over (sparse): refused for its size, by name */
  {
    int fd;
    FRESH();
    fd = open(path, O_WRONLY);
    CHECK(fd >= 0);
    if (fd >= 0) {
      if (ftruncate(fd, (off_t)0x80000100ll) == 0) {
        close(fd);
        p = (PsxPbp *)1;
        memset(&err, 0, sizeof err);
        CHECK(psx_pbp_open(&p, path, &err) != 0 && p == NULL && err.code == PSX_PBP_ERR_NOT_PBP);
        CHECK(strstr(err.text, "larger than") != NULL);
        CHECK(psx_pbp_peek_disc_id(path, id, sizeof id) != 0);
      } else {
        close(fd);
        fprintf(stderr, "  (no 2 GiB sparse file on this file system: that case skipped)\n");
      }
    }
    remove(path);
  }
#undef FRESH
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

static void test_in_place(const char *work, const char *psn, const char *name, const char *extracted, int supported) {
  char dir[512], link[512], target[PATH_MAX], copy[600], path[600], line[600];
  FILE *m;
  unsigned char *ours, *theirs, raw[PSX_CD_RAW_SECTOR];
  int files = 0, xa_files = 0;
  snprintf(dir, sizeof dir, "%s/%s-disc", work, name);
  mkdir(dir, 0777);
  snprintf(link, sizeof link, "%s/EBOOT.PBP", dir);
  snprintf(copy, sizeof copy, "%s/%s/EBOOT.PBP", psn, name);
  remove(link);
  if (!realpath(copy, target) || symlink(target, link) != 0 || file_size(link) < 0) {
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
  test_in_place(work, dir, name, out, psx_region_info(region)->supported);
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
  test_lzrc(work);
  test_hostile(work);
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

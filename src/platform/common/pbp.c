/* A PS1 Classic's EBOOT.PBP (psx_pbp.h, decisions D157).
 *
 * The container: a PBP header ("\0PBP", a version, eight offsets: PARAM.SFO,
 * four pictures, a sound, DATA.PSP and DATA.PSAR). DATA.PSAR starts with
 * "PSISOIMG0000" for one disc ("PSTITLEIMG000000" for a multi-disc title,
 * which this game isn't). In the image's own layout, the game ID is at
 * 0x400, the table of contents at 0x800, the block data's base (0x100000)
 * at 0xBFC, the title at 0x122C and the block table at 0x4000: 32-byte
 * entries of offset (from the base), stored size, a marker and a hash,
 * ending at the first entry of size 0. A block is 16 sectors of 2352
 * bytes (0x9300); stored at that size it is plain, smaller it is compressed:
 * Sony's copies with the PSP's LZRC range coder (the marker is 1), ones
 * made with PSX2PSP with raw deflate.
 *
 * Sony's copies keep 0xB6600 bytes from 0x400 of the PSAR in a PGD
 * container ("\0PGD"): the header fields and the block table, encrypted
 * with the PSP's AMCTRL cipher and signed with its MAC, both over KIRK's
 * AES-CBC with fixed keys (slots 0x38, 0x39, 0x63) and the DNAS key. The MAC
 * is a CBC-MAC with CMAC's padding whose per-file key is XORed in at the
 * end; the key itself is recovered from the key MAC at 0x70 by undoing that
 * last step (the method psxtract, Adrenaline's popcorn and PPSSPP use), so
 * no licence file or KEYS.BIN is needed. The cipher is AES-CBC in the
 * decrypt direction over a counter stream, XORed onto the data. Decrypted
 * byte X of the container is byte 0x400 + X of the plain layout.
 *
 * The algorithms are written here from their descriptions in the public
 * research (kirk-engine, tpunix's amctrl and lzrc, JPCSP); the constants are
 * the PSP's. The LZRC decoder follows the public-domain pkg2zip's structure
 * (Mārtiņš Možeiko, after tpu's libkirk). Nothing here is copied from a GPL
 * project. */
#include "psx_pbp.h"
#include "psx_aes128.h"
#include "psx_cdimage.h" /* SHA-1 */
#include "psx_region.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define BLOCK_BYTES (PSX_PBP_SECTOR * PSX_PBP_BLOCK_SECTORS) /* 0x9300 */
#define PACKED_MAX (BLOCK_BYTES + 64) /* a block stored as one deflate stored block is 5 bytes longer */
#define RUN_BLOCKS 8                  /* blocks read from the file in one call: a worker piece (D158) */
#define RUN_SLACK 64                  /* zero bytes past a run: the range decoder may look past its stream */
#define PACKED_RUN (RUN_BLOCKS * PACKED_MAX + RUN_SLACK)
#define PGD_BYTES 0xB6600
#define PGD_HEADER 0x90
#define TABLE_AT (0x4000 - 0x400) /* the block table in the layout from 0x400 */
#define MAX_BLOCKS 32256          /* (0x100000 - 0x4000) / 32 */
#define MAX_FILES 256

/* ---- the PSP's constants ---- */

static const unsigned char kirk_key38[16] = {0x12, 0x46, 0x8D, 0x7E, 0x1C, 0x42, 0x20, 0x9B,
                                             0xBA, 0x54, 0x26, 0x83, 0x5E, 0xB0, 0x33, 0x03};
static const unsigned char kirk_key39[16] = {0xC4, 0x3B, 0xB6, 0xD6, 0x53, 0xEE, 0x67, 0x49,
                                             0x3E, 0xA9, 0x5F, 0xBC, 0x0C, 0xED, 0x6F, 0x8A};
static const unsigned char kirk_key63[16] = {0x9C, 0x9B, 0x13, 0x72, 0xF8, 0xC6, 0x40, 0xCF,
                                             0x1C, 0x62, 0xF5, 0xD5, 0x92, 0xDD, 0xB5, 0x82};
static const unsigned char dnas_key[16] = {0xED, 0xE2, 0x5D, 0x2D, 0xBB, 0xF8, 0x12, 0xE5,
                                           0x3C, 0x5C, 0x59, 0x32, 0xFA, 0xE3, 0xE2, 0x43};
static const unsigned char amctrl_mac_xor[16] = {0xE3, 0x50, 0xED, 0x1D, 0x91, 0x0A, 0x1F, 0xD0,
                                                 0x29, 0xBB, 0x1C, 0x3E, 0xF3, 0x40, 0x77, 0xFB};
static const unsigned char amctrl_key_out[16] = {0x13, 0x5F, 0xA4, 0x7C, 0xAB, 0x39, 0x5B, 0xA4,
                                                 0x76, 0xB8, 0xCC, 0xA9, 0x8F, 0x3A, 0x04, 0x45};
static const unsigned char amctrl_key_in[16] = {0x67, 0x8D, 0x7F, 0xA3, 0x2A, 0x9C, 0xA0, 0xD1,
                                                0x50, 0x8A, 0xD8, 0x38, 0x5E, 0x4B, 0x01, 0x7E};

/* ---- the LZRC decoder's adaptive probabilities ---- */

/* The PS1 Classics' LZRC is the PSP's range coder with its own contexts
 * (the literal context takes the output position's low bits with the last
 * byte, the length and distance trees are shaped differently), as psxtract's
 * reverse engineering documents it. One 8-bit probability per tree node. */
typedef struct {
  /* the coder's state first: with the tables before it the decoder ran at
   * 58% of this speed (the hot fields 2.8 KB into the struct; D158) */
  const unsigned char *in;
  unsigned in_ptr, in_len;         /* in_len: the stream; up to RUN_SLACK bytes past it may be read */
  unsigned range, code;
  unsigned lc;
  unsigned char literal[8 * 255];  /* [context][node - 1], nodes 1..255 */
  unsigned char dist_bits[24 * 8]; /* [node][length context]: nodes 1..7, or 8..22 for long matches */
  unsigned char dist[15 * 8];      /* [distance bits][k], k 0..3 */
  unsigned char match[8 * 8];      /* [step][state]: step 0 is literal-or-match, 1..7 the length's bits */
  unsigned char len[7 * 64];       /* [length context][position bits + k][state] */
} Lzrc;

/* The file: a raw descriptor, read by offset (no stdio buffer between the
 * card and the decoder). */
#ifdef __vita__
typedef SceUID Fd;
static Fd fd_open(const char *path) { SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0); return fd < 0 ? -1 : fd; }
static void fd_close(Fd fd) { sceIoClose(fd); }
static long fd_size(Fd fd) { return (long)sceIoLseek(fd, 0, SCE_SEEK_END); }
static int fd_pread(Fd fd, void *buf, unsigned n, long off) {
  int got = (int)sceIoPread(fd, buf, (SceSize)n, (SceOff)off);
  return got < 0 ? 0 : got;
}
#else
typedef int Fd;
static Fd fd_open(const char *path) { return open(path, O_RDONLY); }
static void fd_close(Fd fd) { close(fd); }
static long fd_size(Fd fd) { return (long)lseek(fd, 0, SEEK_END); }
static int fd_pread(Fd fd, void *buf, unsigned n, long off) {
  unsigned got = 0;
  while (got < n) {
    ssize_t r = pread(fd, (char *)buf + got, n - got, (off_t)off + (off_t)got);
    if (r < 0 && errno == EINTR)
      continue; /* a signal (the host's --step, a timer): the read goes on */
    if (r <= 0)
      break;
    got += (unsigned)r;
  }
  return (int)got;
}
#endif

struct PsxPbp {
  Fd fd;
  long file_size;
  char path[512];
  char disc_id[16], title[128];
  int encrypted;
  long psar;       /* DATA.PSAR's offset in the file */
  long data_base;  /* psar + the block data's base */
  int nblocks;
  unsigned *block_off;      /* from data_base */
  unsigned short *block_size;
  unsigned short *block_marker;
  int edc;         /* compute the Form 2 sectors' EDC (the extractor; the game leaves it) */
  unsigned long long (*now)(void); /* the stats' clock, or NULL */
  PsxPbpStats st;
  int cached;      /* the block in cache, or -1 */
  int run_first, run_n; /* the blocks whose compressed bytes are in packed, or run_n 0 */
  unsigned char cache[BLOCK_BYTES];
  unsigned char packed[PACKED_RUN];
};

static int fail(PsxPbpError *err, int code, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static int fail(PsxPbpError *err, int code, const char *fmt, ...) {
  va_list ap;
  if (err) {
    err->code = code;
    va_start(ap, fmt);
    vsnprintf(err->text, sizeof err->text, fmt, ap);
    va_end(ap);
  }
  return -1;
}

static unsigned le32(const unsigned char *p) {
  return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}
static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }

static int read_at(Fd fd, long off, void *buf, unsigned n) {
  return off >= 0 && fd_pread(fd, buf, n, off) == (int)n ? 0 : -1;
}

static void edc_table(void);
static void ecc_tables(void);

/* ---- AMCTRL: the MAC ---- */

typedef struct {
  int type;               /* 1, or 3 (key index above 1: the MAC is wrapped once more) */
  unsigned char key[16];  /* the CBC chain */
  unsigned char pad[16];  /* the last 1..16 bytes, for the final block */
  unsigned pad_n;
} BbMac;

static void mac_init(BbMac *m, int type) {
  memset(m, 0, sizeof *m);
  m->type = type;
}

/* CBC-encrypt whole blocks under key slot 0x38, chained through m->key. */
static void mac_blocks(BbMac *m, const unsigned char *data, unsigned n) {
  PsxAes128 a;
  unsigned i;
  psx_aes128_init(&a, kirk_key38);
  for (i = 0; i + 16 <= n; i += 16) {
    unsigned char x[16];
    int j;
    for (j = 0; j < 16; j++)
      x[j] = (unsigned char)(data[i + j] ^ m->key[j]);
    psx_aes128_encrypt(&a, x, m->key);
  }
}

/* All but the last 1..16 bytes seen so far go through the chain; those stay
 * in pad for the final block. */
static void mac_update(BbMac *m, const unsigned char *data, unsigned n) {
  unsigned total = m->pad_n + n, keep, body, first;
  if (total <= 16) {
    memcpy(m->pad + m->pad_n, data, n);
    m->pad_n = total;
    return;
  }
  keep = total & 15 ? total & 15 : 16;
  body = total - keep; /* a multiple of 16, at least 16 */
  first = 16 - m->pad_n;  /* bytes of data that complete the pad's block */
  memcpy(m->pad + m->pad_n, data, first);
  mac_blocks(m, m->pad, 16);
  mac_blocks(m, data + first, body - 16);
  memcpy(m->pad, data + (body - m->pad_n), keep);
  m->pad_n = keep;
}

static void dbl(unsigned char b[16]) {
  int i, carry = b[0] & 0x80;
  for (i = 0; i < 15; i++)
    b[i] = (unsigned char)((b[i] << 1) | (b[i + 1] >> 7));
  b[15] = (unsigned char)((b[15] << 1) ^ (carry ? 0x87 : 0));
}

/* The MAC of what was fed: with vkey the file's MAC, without it the value
 * the key is recovered from. Leaves m as it is (so the same feed can be
 * asked for both). */
static void mac_final(const BbMac *m, const unsigned char *vkey, unsigned char out[16]) {
  PsxAes128 a;
  unsigned char t[16], pad[16], x[16];
  int i;
  psx_aes128_init(&a, kirk_key38);
  memset(t, 0, 16);
  psx_aes128_encrypt(&a, t, t);
  dbl(t);
  memcpy(pad, m->pad, 16);
  if (m->pad_n < 16) {
    dbl(t);
    pad[m->pad_n] = 0x80;
    if (m->pad_n + 1 < 16)
      memset(pad + m->pad_n + 1, 0, 16 - m->pad_n - 1);
  }
  for (i = 0; i < 16; i++)
    x[i] = (unsigned char)(pad[i] ^ t[i] ^ m->key[i]);
  psx_aes128_encrypt(&a, x, x);
  for (i = 0; i < 16; i++)
    x[i] ^= amctrl_mac_xor[i];
  if (vkey) {
    for (i = 0; i < 16; i++)
      x[i] ^= vkey[i];
    psx_aes128_encrypt(&a, x, x);
  }
  memcpy(out, x, 16);
}

/* A stored MAC as the chain produced it: a type-3 MAC is wrapped under key
 * slot 0x63 once more. */
static void mac_unwrap(const BbMac *m, const unsigned char stored[16], unsigned char out[16]) {
  if (m->type == 3) {
    PsxAes128 a;
    psx_aes128_init(&a, kirk_key63);
    psx_aes128_decrypt(&a, stored, out);
  } else {
    memcpy(out, stored, 16);
  }
}

static int mac_check(const BbMac *m, const unsigned char stored[16], const unsigned char *vkey) {
  unsigned char want[16], have[16];
  mac_final(m, vkey, want);
  mac_unwrap(m, stored, have);
  return memcmp(want, have, 16) == 0;
}

/* The file's key from its key MAC: the MAC's last step is E(x ^ vkey) under
 * slot 0x38, so vkey = x ^ D(mac). */
static void mac_getkey(const BbMac *m, const unsigned char stored[16], unsigned char vkey[16]) {
  PsxAes128 a;
  unsigned char x[16], d[16];
  int i;
  mac_final(m, NULL, x);
  mac_unwrap(m, stored, d);
  psx_aes128_init(&a, kirk_key38);
  psx_aes128_decrypt(&a, d, d);
  for (i = 0; i < 16; i++)
    vkey[i] = (unsigned char)(x[i] ^ d[i]);
}

/* ---- AMCTRL: the cipher (type 1, mode 2) ---- */

/* data ^= the keystream for (hdr_key ^ vkey): AES-CBC decryption under slot
 * 0x63 of counter blocks whose first 12 bytes come from the key through
 * slot 0x39. */
static void cipher_xor(const unsigned char hdr_key[16], const unsigned char vkey[16],
                       unsigned char *data, unsigned n) {
  PsxAes128 a39, a63;
  unsigned char t[16], iv[16], ctr[16], ks[16];
  unsigned seed = 1, i;
  int j;
  for (j = 0; j < 16; j++)
    t[j] = (unsigned char)(hdr_key[j] ^ vkey[j] ^ amctrl_key_in[j]);
  psx_aes128_init(&a39, kirk_key39);
  psx_aes128_decrypt(&a39, t, t);
  for (j = 0; j < 16; j++)
    t[j] ^= amctrl_key_out[j];
  memset(iv, 0, 16); /* the first piece's chain starts from zero */
  psx_aes128_init(&a63, kirk_key63);
  for (i = 0; i < n; i += 16, seed++) {
    unsigned m = n - i < 16 ? n - i : 16;
    memcpy(ctr, t, 12);
    ctr[12] = (unsigned char)seed;
    ctr[13] = (unsigned char)(seed >> 8);
    ctr[14] = (unsigned char)(seed >> 16);
    ctr[15] = (unsigned char)(seed >> 24);
    psx_aes128_decrypt(&a63, ctr, ks);
    for (j = 0; j < (int)m; j++)
      data[i + j] ^= (unsigned char)(ks[j] ^ iv[j]);
    memcpy(iv, ctr, 16);
  }
}

/* ---- the PGD container ---- */

/* buf holds the container (PGD_BYTES). On success its payload is decrypted
 * in place at buf + PGD_HEADER and its size returned; else -1. */
static int pgd_decrypt(unsigned char *buf, PsxPbpError *err) {
  unsigned key_index = le32(buf + 4), drm_type = le32(buf + 8);
  unsigned data_size, block_size, data_offset, align, nblk;
  unsigned char vkey[16], hdr[0x30];
  BbMac m;
  if (memcmp(buf, "\0PGD", 4) != 0)
    return fail(err, PSX_PBP_ERR_PGD, "no PGD header where the image header should be");
  if (drm_type != 1)
    return fail(err, PSX_PBP_ERR_PGD,
                "the image header is a PGD of DRM type %u, which needs a console's own key", drm_type);
  mac_init(&m, key_index > 1 ? 3 : 1);
  mac_update(&m, buf, 0x80);
  if (!mac_check(&m, buf + 0x80, dnas_key))
    return fail(err, PSX_PBP_ERR_PGD, "the image header's signature (DNAS MAC) is wrong: damaged?");
  mac_init(&m, key_index > 1 ? 3 : 1);
  mac_update(&m, buf, 0x70);
  mac_getkey(&m, buf + 0x70, vkey);
  if (!mac_check(&m, buf + 0x70, vkey))
    return fail(err, PSX_PBP_ERR_PGD, "the image header's key can't be recovered from its MAC");
  memcpy(hdr, buf + 0x30, 0x30);
  cipher_xor(buf + 0x10, vkey, hdr, 0x30);
  data_size = le32(hdr + 0x14);
  block_size = le32(hdr + 0x18);
  data_offset = le32(hdr + 0x1C);
  align = (data_size + 15) & ~15u;
  if (block_size == 0 || (block_size & (block_size - 1)) || data_offset != PGD_HEADER ||
      align > PGD_BYTES - PGD_HEADER)
    return fail(err, PSX_PBP_ERR_PGD, "the image header's PGD fields make no sense (size 0x%x, block 0x%x, data at 0x%x)",
                data_size, block_size, data_offset);
  nblk = (align + block_size - 1) / block_size;
  if ((unsigned long long)data_offset + align + nblk * 16ull > PGD_BYTES)
    return fail(err, PSX_PBP_ERR_PGD, "the image header's PGD table runs past the container");
  if (data_size > PGD_BYTES - PGD_HEADER || data_size < TABLE_AT + 32)
    return fail(err, PSX_PBP_ERR_PGD, "the image header's PGD holds 0x%x bytes, not an image header", data_size);
  mac_init(&m, key_index > 1 ? 3 : 1);
  mac_update(&m, buf + data_offset + align, nblk * 16);
  if (!mac_check(&m, buf + 0x60, vkey))
    return fail(err, PSX_PBP_ERR_PGD, "the image header's block hashes don't match: damaged?");
  cipher_xor(hdr, vkey, buf + data_offset, align);
  return (int)data_size;
}

/* ---- LZRC ---- */

/* The next input byte. The stream's buffer has RUN_SLACK bytes past in_len
 * (zeros, or the next block's bytes, which a stream that ended never uses),
 * so no check per byte: lzrc_decompress checks in_ptr at the end. */
static inline unsigned rc_byte(Lzrc *rc) { return rc->in[rc->in_ptr++]; }

static void rc_init(Lzrc *rc, const unsigned char *in, unsigned in_len) {
  rc->in = in;
  rc->in_len = in_len;
  rc->in_ptr = 0;
  rc->range = 0xFFFFFFFFu;
  rc->lc = rc_byte(rc);
  rc->code = rc_byte(rc) << 24;
  rc->code |= rc_byte(rc) << 16;
  rc->code |= rc_byte(rc) << 8;
  rc->code |= rc_byte(rc);
  memset(rc->literal, 0x80, sizeof *rc - offsetof(Lzrc, literal)); /* every table: they lie together at the end */
}

static inline void rc_normalize(Lzrc *rc) {
  if (rc->range < 0x01000000u) {
    rc->range <<= 8;
    rc->code = (rc->code << 8) | rc_byte(rc);
  }
}

/* One bit under an adaptive probability. */
static inline unsigned rc_bit(Lzrc *rc, unsigned char *prob) {
  unsigned bound, p = *prob;
  rc_normalize(rc);
  bound = (rc->range >> 8) * p;
  p -= p >> 3;
  if (rc->code < bound) {
    rc->range = bound;
    *prob = (unsigned char)(p + 31);
    return 1;
  }
  *prob = (unsigned char)p;
  rc->code -= bound;
  rc->range -= bound;
  return 0;
}

/* A number of n + 1 bits (n = 0..): the high bits from the tree's nodes
 * k = 3, 3 and then direct bits, the low ones from nodes 0, 1, 2; the node k
 * is cell[k * stride]. *first is the bit read from node 0. */
static unsigned rc_number(Lzrc *rc, unsigned char *cell, int stride, unsigned n, unsigned *first) {
  unsigned v = 1, i;
  if (n >= 3) {
    v = (v << 1) + rc_bit(rc, cell + 3 * stride);
    if (n >= 4) {
      v = (v << 1) + rc_bit(rc, cell + 3 * stride);
      if (n >= 5) {
        rc_normalize(rc);
        for (i = n; i >= 5; i--) {
          v <<= 1;
          rc->range >>= 1;
          if (rc->code < rc->range)
            v++;
          else
            rc->code -= rc->range;
        }
      }
    }
  }
  *first = rc_bit(rc, cell);
  v = (v << 1) + *first;
  if (n >= 1) {
    v = (v << 1) + rc_bit(rc, cell + 1 * stride);
    if (n >= 2)
      v = (v << 1) + rc_bit(rc, cell + 2 * stride);
  }
  return v;
}

/* in (in_len bytes, with RUN_SLACK readable past them) into out (out_len
 * bytes). Returns the bytes decoded, or -1 for a stream that doesn't fit or
 * runs past its input. */
static int lzrc_decompress(unsigned char *out, unsigned out_len, const unsigned char *in, unsigned in_len) {
  Lzrc l, *rc = &l; /* on the stack: its address never leaves the inlined coder, so its state stays in registers */
  unsigned state = 0, pos = 0, prev = 0;
  if (in_len < 5)
    return -1;
  rc_init(rc, in, in_len);
  if (rc->lc & 0x80) { /* stored */
    if (rc->code > out_len || rc->code > in_len - 5)
      return -1;
    memcpy(out, in + 5, rc->code);
    return (int)rc->code;
  }
  if (rc->lc > 7)
    return -1; /* the literal context shifts by it: real streams use small values */
  for (;;) {
    unsigned step, bits, ctx, length, first, big, node, diff, bit, distance, end;
    if (pos == out_len)
      return rc->in_ptr > rc->in_len + 4 ? -1 : (int)pos; /* the coder's lookahead is 4 bytes */
    if (!rc_bit(rc, rc->match + state)) { /* a literal */
      if (state > 0)
        state--;
      ctx = ((((pos & 7) << 8) | prev) >> rc->lc) & 7;
      node = 1;
      do
        node = (node << 1) + rc_bit(rc, rc->literal + ctx * 255 + node - 1);
      while (node < 256);
      out[pos++] = prev = (unsigned char)node;
      if (rc->in_ptr > rc->in_len + 4) /* a stream of literals past its input: the slack is 64 bytes */
        return -1;
      continue;
    }
    /* a match: its length's bit count from the match tree's steps 1..7 */
    bits = 0;
    for (step = 1; step <= 7; step++) {
      bit = rc_bit(rc, rc->match + step * 8 + state);
      if (!bit)
        break;
      bits++;
    }
    big = 0;
    first = 0;
    if (bits > 0) {
      unsigned lc = bits - 1, posbits = (pos << lc) & 3;
      length = rc_number(rc, rc->len + lc * 64 + posbits * 8 + state, 8, lc, &first);
      if (length != 3 && (lc > 0 || first))
        big = 1;
    } else {
      length = 1;
    }
    /* its distance's bit count: a tree of 3 levels, or 4 for a long match */
    node = 1;
    do {
      diff = (node << 4) - (big ? 0x80 : 0x40);
      bit = rc_bit(rc, rc->dist_bits + (node + 7 * big) * 8 + bits);
      node = (node << 1) + bit;
    } while ((int)diff < 0);
    if ((int)diff > 0 || bit) {
      unsigned n;
      if (!bit)
        diff -= 8;
      n = diff / 8;
      distance = rc_number(rc, rc->dist + n * 8 + 3, -1, n, &first); /* its nodes lie in reverse */
    } else {
      distance = 1;
    }
    end = pos + length + 1;
    if (distance > pos || end > out_len || rc->in_ptr > rc->in_len + 4)
      return -1;
    state = ((end + 1) & 1) + 6;
    if (end - pos >= 32 && distance >= end - pos) { /* a long copy with no overlap */
      memcpy(out + pos, out + pos - distance, end - pos);
      pos = end;
    } else { /* most matches are a few bytes: a call would cost more */
      for (; pos < end; pos++)
        out[pos] = out[pos - distance];
    }
    prev = out[pos - 1];
  }
}

/* ---- raw deflate (RFC 1951), for PSX2PSP-made images ---- */

typedef struct {
  const unsigned char *in;
  unsigned in_len, in_pos, bitbuf, bitcnt;
  unsigned char *out;
  unsigned out_len, out_pos;
  int bad;
} Inflate;

typedef struct {
  unsigned short count[16]; /* codes of each length */
  unsigned short symbol[288];
} Huff;

static unsigned inf_bits(Inflate *z, unsigned n) {
  unsigned v;
  while (z->bitcnt < n) {
    unsigned b = z->in_pos < z->in_len ? z->in[z->in_pos++] : (z->bad = 1, 0);
    z->bitbuf |= b << z->bitcnt;
    z->bitcnt += 8;
  }
  v = z->bitbuf & ((1u << n) - 1);
  z->bitbuf >>= n;
  z->bitcnt -= n;
  return v;
}

/* Canonical codes from the lengths; 0 when they form a complete or empty
 * set, else -1. */
static int huff_build(Huff *h, const unsigned char *len, int n) {
  int i, left;
  unsigned short offs[16];
  memset(h->count, 0, sizeof h->count);
  for (i = 0; i < n; i++)
    h->count[len[i]]++;
  if (h->count[0] == n)
    return 0;
  left = 1;
  for (i = 1; i < 16; i++) {
    left <<= 1;
    left -= h->count[i];
    if (left < 0)
      return -1;
  }
  offs[1] = 0;
  for (i = 1; i < 15; i++)
    offs[i + 1] = (unsigned short)(offs[i] + h->count[i]);
  for (i = 0; i < n; i++)
    if (len[i])
      h->symbol[offs[len[i]]++] = (unsigned short)i;
  return 0;
}

static int huff_decode(Inflate *z, const Huff *h) {
  int code = 0, first = 0, index = 0, len;
  for (len = 1; len < 16; len++) {
    code |= (int)inf_bits(z, 1);
    if (code - h->count[len] < first)
      return h->symbol[index + (code - first)];
    index += h->count[len];
    first += h->count[len];
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

static const unsigned short len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const unsigned char len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                            2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const unsigned short dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                             193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                             6145, 8193, 12289, 16385, 24577};
static const unsigned char dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                             6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int inf_codes(Inflate *z, const Huff *lit, const Huff *dist) {
  for (;;) {
    int sym = huff_decode(z, lit);
    unsigned len, d;
    if (sym < 0 || z->bad)
      return -1;
    if (sym < 256) {
      if (z->out_pos >= z->out_len)
        return -1;
      z->out[z->out_pos++] = (unsigned char)sym;
      continue;
    }
    if (sym == 256)
      return 0;
    sym -= 257;
    if (sym >= 29)
      return -1;
    len = len_base[sym] + inf_bits(z, len_extra[sym]);
    sym = huff_decode(z, dist);
    if (sym < 0 || sym >= 30)
      return -1;
    d = dist_base[sym] + inf_bits(z, dist_extra[sym]);
    if (d > z->out_pos || z->out_pos + len > z->out_len || z->bad)
      return -1;
    while (len--)
      z->out[z->out_pos] = z->out[z->out_pos - d], z->out_pos++;
  }
}

/* The fixed Huffman tables, built once in psx_pbp_open (one thread, before
 * any clone exists) and read-only from then on. */
static Huff s_fixed_lit, s_fixed_dist;
static int s_fixed_ready;

static void inf_fixed_tables(void) {
  unsigned char len[288];
  int i;
  if (s_fixed_ready)
    return;
  for (i = 0; i < 144; i++) len[i] = 8;
  for (; i < 256; i++) len[i] = 9;
  for (; i < 280; i++) len[i] = 7;
  for (; i < 288; i++) len[i] = 8;
  huff_build(&s_fixed_lit, len, 288);
  for (i = 0; i < 30; i++) len[i] = 5;
  huff_build(&s_fixed_dist, len, 30);
  s_fixed_ready = 1;
}

static int inf_fixed(Inflate *z) { return inf_codes(z, &s_fixed_lit, &s_fixed_dist); }

static int inf_dynamic(Inflate *z) {
  static const unsigned char order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  unsigned char len[320];
  Huff lit, dist;
  unsigned nlen = inf_bits(z, 5) + 257, ndist = inf_bits(z, 5) + 1, ncode = inf_bits(z, 4) + 4, i;
  if (nlen > 286 || ndist > 30)
    return -1;
  memset(len, 0, 19);
  for (i = 0; i < ncode; i++)
    len[order[i]] = (unsigned char)inf_bits(z, 3);
  if (huff_build(&lit, len, 19) != 0)
    return -1;
  for (i = 0; i < nlen + ndist;) {
    int sym = huff_decode(z, &lit);
    unsigned rep;
    unsigned char v = 0;
    if (sym < 0 || z->bad)
      return -1;
    if (sym < 16) {
      len[i++] = (unsigned char)sym;
      continue;
    }
    if (sym == 16) {
      if (i == 0)
        return -1;
      v = len[i - 1];
      rep = 3 + inf_bits(z, 2);
    } else if (sym == 17) {
      rep = 3 + inf_bits(z, 3);
    } else {
      rep = 11 + inf_bits(z, 7);
    }
    if (i + rep > nlen + ndist)
      return -1;
    while (rep--)
      len[i++] = v;
  }
  if (huff_build(&lit, len, (int)nlen) != 0 || huff_build(&dist, len + nlen, (int)ndist) != 0)
    return -1;
  return inf_codes(z, &lit, &dist);
}

static int inflate_raw(unsigned char *out, unsigned out_len, const unsigned char *in, unsigned in_len) {
  Inflate z;
  int last;
  memset(&z, 0, sizeof z);
  z.in = in;
  z.in_len = in_len;
  z.out = out;
  z.out_len = out_len;
  do {
    unsigned type;
    last = (int)inf_bits(&z, 1);
    type = inf_bits(&z, 2);
    if (type == 0) { /* stored */
      unsigned n, nn;
      z.bitbuf = 0;
      z.bitcnt = 0;
      if (z.in_pos + 4 > z.in_len)
        return -1;
      n = z.in[z.in_pos] | (unsigned)z.in[z.in_pos + 1] << 8;
      nn = z.in[z.in_pos + 2] | (unsigned)z.in[z.in_pos + 3] << 8;
      z.in_pos += 4;
      if ((n ^ 0xFFFF) != nn || z.in_pos + n > z.in_len || z.out_pos + n > z.out_len)
        return -1;
      memcpy(z.out + z.out_pos, z.in + z.in_pos, n);
      z.in_pos += n;
      z.out_pos += n;
    } else if (type == 1) {
      if (inf_fixed(&z) != 0)
        return -1;
    } else if (type == 2) {
      if (inf_dynamic(&z) != 0)
        return -1;
    } else {
      return -1;
    }
  } while (!last && !z.bad);
  return z.bad ? -1 : (int)z.out_pos;
}

/* ---- the container ---- */

/* PARAM.SFO's string key (a 0x0204 entry) into out; 0 or -1. */
static int sfo_string(const unsigned char *sfo, unsigned n, const char *key, char *out, size_t cap) {
  unsigned keys, data, count, i;
  if (cap == 0)
    return -1;
  if (n < 20 || memcmp(sfo, "\0PSF", 4) != 0)
    return -1;
  keys = le32(sfo + 8);
  data = le32(sfo + 12);
  count = le32(sfo + 16);
  if (keys >= n || data > n)
    return -1;
  for (i = 0; i < count && 20 + 16 * (i + 1) <= n; i++) {
    const unsigned char *e = sfo + 20 + 16 * i;
    unsigned ko = le16(e), fmt = le16(e + 2), len = le32(e + 4), off = le32(e + 12);
    const char *k = (const char *)sfo + keys + ko;
    /* every sum checked without wrapping */
    if (ko >= n - keys || fmt != 0x0204 || off > n - data || len > n - data - off)
      continue;
    if (strlen(key) < n - keys - ko && strncmp(k, key, n - keys - ko) == 0) {
      unsigned m = len < cap ? len : (unsigned)cap - 1, j;
      memcpy(out, sfo + data + off, m);
      out[m] = 0;
      /* for the log: control bytes and bytes that aren't UTF-8 (the European
       * copy's title has a Latin-1 (R)) become '?', so the log stays text */
      for (j = 0; j < m && out[j]; j++) {
        unsigned char c = (unsigned char)out[j];
        unsigned more = c >= 0xF0 && c < 0xF8 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : 0, t;
        if (c < 0x20 || c == 0x7F || (c >= 0x80 && more == 0)) {
          out[j] = '?';
          continue;
        }
        for (t = 1; t <= more; t++)
          if (j + t >= m || ((unsigned char)out[j + t] & 0xC0) != 0x80)
            break;
        if (t <= more)
          out[j] = '?';
        else
          j += more;
      }
      return 0;
    }
  }
  return -1;
}

/* The PBP's header: the SFO's place and the PSAR's, both inside the file
 * (size bytes). 0, or -1 if it isn't one. */
static int pbp_header(Fd fd, long size, long *sfo_off, unsigned *sfo_len, long *psar_off) {
  unsigned char h[40];
  unsigned sfo, sfo_end, psar;
  if (size < (long)sizeof h || read_at(fd, 0, h, sizeof h) != 0 || memcmp(h, "\0PBP", 4) != 0)
    return -1;
  sfo = le32(h + 8);
  sfo_end = le32(h + 12);
  psar = le32(h + 36);
  if (sfo < sizeof h || sfo_end < sfo || sfo_end - sfo > 1u << 16 || (long)sfo_end > size ||
      psar < sfo_end || psar > 0x7FFFFFFFu || (long)psar >= size)
    return -1;
  *sfo_off = (long)sfo;
  *sfo_len = sfo_end - sfo;
  *psar_off = (long)psar;
  return 0;
}

int psx_pbp_peek_disc_id(const char *path, char *disc_id, size_t cap) {
  Fd fd = fd_open(path);
  unsigned char *sfo;
  long sfo_off, psar;
  unsigned sfo_len;
  int r = -1;
  if (cap == 0)
    return -1;
  disc_id[0] = 0;
  if (fd < 0)
    return -1;
  if (pbp_header(fd, fd_size(fd), &sfo_off, &sfo_len, &psar) == 0 && (sfo = malloc(sfo_len))) {
    if (read_at(fd, sfo_off, sfo, sfo_len) == 0) {
      sfo_string(sfo, sfo_len, "DISC_ID", disc_id, cap);
      r = 0;
    }
    free(sfo);
  }
  fd_close(fd);
  return r;
}

int psx_pbp_open(PsxPbp **out, const char *path, PsxPbpError *err) {
  PsxPbp *p;
  unsigned char magic[16], *hdr = NULL, *sfo;
  long sfo_off;
  unsigned sfo_len, iso_off, n;
  const unsigned char *lay; /* the plain layout from 0x400 on */
  unsigned lay_len;
  int i;
  *out = NULL;
  if (err)
    err->code = PSX_PBP_OK;
  p = calloc(1, sizeof *p);
  if (!p)
    return fail(err, PSX_PBP_ERR_UNREADABLE, "out of memory");
  p->cached = -1;
  snprintf(p->path, sizeof p->path, "%s", path);
  p->fd = fd_open(path);
  if (p->fd < 0) {
    free(p);
    return fail(err, PSX_PBP_ERR_UNREADABLE, "%s can't be opened", path);
  }
  p->file_size = fd_size(p->fd);
  if (pbp_header(p->fd, p->file_size, &sfo_off, &sfo_len, &p->psar) != 0) {
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_NOT_PBP, "%s isn't a PSP EBOOT.PBP", path);
  }
  if ((sfo = malloc(sfo_len)) && read_at(p->fd, sfo_off, sfo, sfo_len) == 0) {
    sfo_string(sfo, sfo_len, "DISC_ID", p->disc_id, sizeof p->disc_id);
    sfo_string(sfo, sfo_len, "TITLE", p->title, sizeof p->title);
  }
  free(sfo);
  if (p->file_size - p->psar < 0x400 + (long)PGD_BYTES) {
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_UNREADABLE, "%s: DATA.PSAR is cut short (a damaged download?)", path);
  }
  if (read_at(p->fd, p->psar, magic, 16) != 0) {
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_UNREADABLE, "%s: DATA.PSAR can't be read", path);
  }
  if (memcmp(magic, "PSTITLEIMG000000", 16) == 0) {
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_MULTI_DISC, "%s holds a multi-disc title, which Spyro isn't", path);
  }
  if (memcmp(magic, "PSISOIMG0000", 12) != 0) {
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_NOT_PBP, "%s isn't a PS1 Classic (no PSISOIMG0000 image)", path);
  }
  hdr = malloc(PGD_BYTES);
  if (!hdr || read_at(p->fd, p->psar + 0x400, hdr, PGD_BYTES) != 0) {
    free(hdr);
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_UNREADABLE, "%s: the image header can't be read (cut short?)", path);
  }
  if (memcmp(hdr, "\0PGD", 4) == 0) {
    int size = pgd_decrypt(hdr, err);
    if (size < 0) {
      free(hdr);
      psx_pbp_close(p);
      return -1; /* err filled by pgd_decrypt */
    }
    p->encrypted = 1;
    lay = hdr + PGD_HEADER;
    lay_len = (unsigned)size; /* pgd_decrypt bounds it: TABLE_AT + 32 .. PGD_BYTES - PGD_HEADER */
  } else {
    lay = hdr;
    lay_len = PGD_BYTES;
  }
  /* plain-layout offset X is lay[X - 0x400] */
  iso_off = le32(lay + 0xBFC - 0x400);
  if (iso_off == 0)
    iso_off = 0x100000;
  if (iso_off > 0x7FFFFFFFu || (long)iso_off >= p->file_size - p->psar) {
    free(hdr);
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_BLOCK, "%s: the image's data base (0x%x) lies past the file", path, iso_off);
  }
  p->data_base = p->psar + (long)iso_off;
  n = (lay_len - TABLE_AT) / 32; /* the entries the decrypted or plain header holds */
  if (n > MAX_BLOCKS)
    n = MAX_BLOCKS;
  inf_fixed_tables();
  edc_table();
  p->block_off = malloc(n * sizeof *p->block_off);
  p->block_size = malloc(n * sizeof *p->block_size);
  p->block_marker = malloc(n * sizeof *p->block_marker);
  if (!p->block_off || !p->block_size || !p->block_marker) {
    free(hdr);
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_UNREADABLE, "out of memory");
  }
  for (i = 0; i < (int)n; i++) {
    const unsigned char *e = lay + TABLE_AT + 32 * i;
    unsigned size = le16(e + 4), off = le32(e);
    if (size == 0)
      break;
    if (size > PACKED_MAX || off > 0x7FFFFFFFu - size || (long)(off + size) > p->file_size - p->data_base) {
      free(hdr);
      psx_pbp_close(p);
      return fail(err, PSX_PBP_ERR_BLOCK, "%s: block %d (%u bytes at 0x%x) lies past the file or a block's size", path, i, size, off);
    }
    p->block_off[i] = off;
    p->block_size[i] = (unsigned short)size;
    p->block_marker[i] = (unsigned short)le16(e + 6);
  }
  p->nblocks = i;
  free(hdr);
  if (p->nblocks == 0) {
    psx_pbp_close(p);
    return fail(err, PSX_PBP_ERR_BLOCK, "%s: the image's block table is empty", path);
  }
  *out = p;
  return 0;
}

void psx_pbp_close(PsxPbp *p) {
  if (!p)
    return;
  if (p->fd >= 0)
    fd_close(p->fd);
  free(p->block_off);
  free(p->block_size);
  free(p->block_marker);
  free(p);
}

const char *psx_pbp_path(const PsxPbp *p) { return p->path; }

int psx_pbp_clone(const PsxPbp *p, PsxPbp **out) {
  PsxPbp *c = calloc(1, sizeof *c);
  size_t n = (size_t)p->nblocks;
  *out = NULL;
  if (!c)
    return -1;
  /* only what never changes after psx_pbp_open: the parent may be decoding */
  memcpy(c->path, p->path, sizeof c->path);
  memcpy(c->disc_id, p->disc_id, sizeof c->disc_id);
  memcpy(c->title, p->title, sizeof c->title);
  c->encrypted = p->encrypted;
  c->file_size = p->file_size;
  c->psar = p->psar;
  c->data_base = p->data_base;
  c->nblocks = p->nblocks;
  c->edc = p->edc;
  c->now = p->now;
  c->cached = -1;
  c->fd = fd_open(p->path);
  c->block_off = malloc(n * sizeof *c->block_off);
  c->block_size = malloc(n * sizeof *c->block_size);
  c->block_marker = malloc(n * sizeof *c->block_marker);
  if (c->fd < 0 || !c->block_off || !c->block_size || !c->block_marker) {
    psx_pbp_close(c);
    return -1;
  }
  memcpy(c->block_off, p->block_off, n * sizeof *c->block_off);
  memcpy(c->block_size, p->block_size, n * sizeof *c->block_size);
  memcpy(c->block_marker, p->block_marker, n * sizeof *c->block_marker);
  *out = c;
  return 0;
}

const char *psx_pbp_disc_id(const PsxPbp *p) { return p->disc_id; }
const char *psx_pbp_title(const PsxPbp *p) { return p->title; }
int psx_pbp_encrypted(const PsxPbp *p) { return p->encrypted; }
int psx_pbp_sectors(const PsxPbp *p) { return p->nblocks * PSX_PBP_BLOCK_SECTORS; }
void psx_pbp_set_edc(PsxPbp *p, int on) {
  p->edc = on;
  p->cached = -1; /* the cached block was made the other way */
}
void psx_pbp_set_clock(PsxPbp *p, unsigned long long (*now_us)(void)) { p->now = now_us; }
void psx_pbp_stats(const PsxPbp *p, PsxPbpStats *out) { *out = p->st; }

/* A Form 2 sector's EDC (ECMA-130's CRC over the subheader and data, the
 * polynomial 0x8001801B reflected), which the image leaves at zero and a
 * dump has, so the sectors come out as a dump's. Over the sector from its
 * subheader on (2336 bytes): bytes 0..2331, stored at 2332. The table is
 * built once in psx_pbp_open, before any clone exists. */
static unsigned s_edc_lut[256];
static int s_edc_ready;

static void edc_table(void) {
  unsigned i;
  if (s_edc_ready)
    return;
  for (i = 0; i < 256; i++) {
    unsigned v = i;
    int k;
    for (k = 0; k < 8; k++)
      v = (v >> 1) ^ ((v & 1) ? 0xD8018001u : 0);
    s_edc_lut[i] = v;
  }
  ecc_tables();
  s_edc_ready = 1;
}

static unsigned edc_of(const unsigned char *p, unsigned n) {
  unsigned crc = 0, i;
  for (i = 0; i < n; i++)
    crc = s_edc_lut[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}

static void put_edc(unsigned char *at, unsigned crc) {
  at[0] = (unsigned char)crc;
  at[1] = (unsigned char)(crc >> 8);
  at[2] = (unsigned char)(crc >> 16);
  at[3] = (unsigned char)(crc >> 24);
}

/* A Form 2 sector (from its subheader on, 2336 bytes): the EDC over bytes
 * 0..2331 at 2332. */
static void xa_edc(unsigned char *sec) {
  if (sec[2332] | sec[2333] | sec[2334] | sec[2335])
    return; /* the image kept it */
  put_edc(sec + 2332, edc_of(sec, 2332));
}

/* ECMA-130's Reed-Solomon product code for a Form 1 sector (the data
 * sectors between a stream's audio): GF(2^8) with the polynomial x^8 + x^4
 * + x^3 + x^2 + 1, the two parity bytes of each P column (86 columns of 24
 * words) and Q diagonal (52 of 43 words) over the 1032 words from the header
 * on, the header taken as zero in Form 1 of Mode 2. */
static unsigned char s_gf_x2[256], s_gf_x3_inv[256]; /* a * 2, and the inverse of a * 3 */

static void ecc_tables(void) {
  unsigned i;
  for (i = 0; i < 256; i++) {
    unsigned j = (i << 1) ^ ((i & 0x80) ? 0x11D : 0);
    s_gf_x2[i] = (unsigned char)j;
    s_gf_x3_inv[i ^ j] = (unsigned char)i;
  }
}

/* One parity set over the field (the sector from its header on, as bytes:
 * each byte plane is its own code): `major` code words of `minor` bytes,
 * byte j of word i at index (i/2 * major_mult + i%2 + j * minor_inc) mod
 * (major * minor); the two parity bytes of word i at dest[i] and
 * dest[i + major]. */
static void ecc_parity(const unsigned char *field, unsigned major, unsigned minor, unsigned major_mult,
                       unsigned minor_inc, unsigned char *dest) {
  unsigned i, j, n = major * minor;
  for (i = 0; i < major; i++) {
    unsigned idx = (i >> 1) * major_mult + (i & 1);
    unsigned char a = 0, b = 0;
    for (j = 0; j < minor; j++) {
      unsigned char d = field[idx];
      idx += minor_inc;
      if (idx >= n)
        idx -= n;
      a ^= d;
      b ^= d;
      a = s_gf_x2[a];
    }
    a = s_gf_x3_inv[s_gf_x2[a] ^ b];
    dest[i] = a;
    dest[i + major] = (unsigned char)(a ^ b);
  }
}

/* A Form 1 sector (from its subheader on, 2336 bytes): the EDC over bytes
 * 0..2055 (subheader and data) at 2056, then the P parity (over the 2064
 * bytes from the header, taken as zero, to the EDC) at 2060 and the Q
 * parity (over those and the P parity) at 2232. */
static void form1_codes(unsigned char *sec) {
  unsigned char field[4 + 2232]; /* the header as zeros, then subheader, data, EDC, P */
  if (sec[2056] | sec[2057] | sec[2058] | sec[2059])
    return; /* the image kept it */
  put_edc(sec + 2056, edc_of(sec, 2056));
  memset(field, 0, 4);
  memcpy(field + 4, sec, 2060);
  ecc_parity(field, 86, 24, 2, 86, field + 2064); /* P */
  ecc_parity(field, 52, 43, 86, 88, sec + 2232);  /* Q */
  memcpy(sec + 2060, field + 2064, 172);
}

static unsigned long long clock_now(const PsxPbp *p) { return p->now ? p->now() : 0; }

/* The compressed bytes of blocks b .. b + want - 1 into packed, in one read:
 * as many of them as fit (at most RUN_BLOCKS, and only while they lie one
 * after the other in the file). Sets run_first / run_n. */
static int load_run(PsxPbp *p, int b, int want) {
  unsigned start = p->block_off[b], end = start + p->block_size[b];
  int n = 1;
  unsigned long long t0;
  if (want > RUN_BLOCKS)
    want = RUN_BLOCKS;
  if (want == 1)
    want = RUN_BLOCKS / 2; /* a lone block (a probe, the hash): read ahead, the next are likely wanted */
  while (n < want && b + n < p->nblocks) {
    unsigned off = p->block_off[b + n];
    if (off < end || off - start + p->block_size[b + n] > PACKED_RUN - RUN_SLACK)
      break; /* out of order, a gap too wide, or more than the buffer holds */
    end = off + p->block_size[b + n];
    n++;
  }
  t0 = clock_now(p);
  p->run_n = 0;
  if (read_at(p->fd, p->data_base + (long)start, p->packed, end - start) != 0)
    return -1;
  memset(p->packed + (end - start), 0, RUN_SLACK);
  p->st.reads++;
  p->st.read_bytes += end - start;
  p->st.read_us += clock_now(p) - t0;
  p->run_first = b;
  p->run_n = n;
  return 0;
}

/* Block b into cache; want: the blocks this request still needs from b on,
 * read from the file together. */
static int load_block(PsxPbp *p, int b, int want) {
  unsigned size = p->block_size[b];
  const unsigned char *in;
  unsigned long long t0;
  int got;
  if (p->cached == b) {
    p->st.hits++;
    return 0;
  }
  p->cached = -1;
  if (!(p->run_n && b >= p->run_first && b < p->run_first + p->run_n) && load_run(p, b, want) != 0)
    return -1;
  in = p->packed + (p->block_off[b] - p->block_off[p->run_first]);
  t0 = clock_now(p);
  if (size == BLOCK_BYTES) {
    memcpy(p->cache, in, BLOCK_BYTES);
    got = BLOCK_BYTES;
  } else if (p->block_marker[b] == 1) {
    got = lzrc_decompress(p->cache, BLOCK_BYTES, in, size);
  } else {
    got = inflate_raw(p->cache, BLOCK_BYTES, in, size);
  }
  if (got < 0)
    return -1;
  if (got < BLOCK_BYTES) /* the image's last block may stop short */
    memset(p->cache + got, 0, BLOCK_BYTES - (unsigned)got);
  if (p->edc) { /* the error codes, zero in the image, computed so the sectors are a dump's */
    int k;
    for (k = 0; k < PSX_PBP_BLOCK_SECTORS; k++) {
      unsigned char *sec = p->cache + k * PSX_PBP_SECTOR;
      if (sec[15] != 2)
        continue; /* only Mode 2 sectors are a PlayStation disc's */
      if (sec[18] & 0x20)
        xa_edc(sec + 16);
      else
        form1_codes(sec + 16);
    }
  }
  p->st.blocks++;
  p->st.decode_us += clock_now(p) - t0;
  p->cached = b;
  return 0;
}

int psx_pbp_read_sectors(PsxPbp *p, int lba, int count, unsigned char *out) {
  while (count > 0) {
    int b = lba / PSX_PBP_BLOCK_SECTORS, k = lba % PSX_PBP_BLOCK_SECTORS;
    int n = PSX_PBP_BLOCK_SECTORS - k < count ? PSX_PBP_BLOCK_SECTORS - k : count;
    if (lba < 0 || b >= p->nblocks || load_block(p, b, (k + count + PSX_PBP_BLOCK_SECTORS - 1) / PSX_PBP_BLOCK_SECTORS) != 0)
      return -1;
    memcpy(out, p->cache + k * PSX_PBP_SECTOR, (size_t)n * PSX_PBP_SECTOR);
    out += (size_t)n * PSX_PBP_SECTOR;
    lba += n;
    count -= n;
  }
  return 0;
}

/* ---- extraction: the ISO9660 tree, as tools/extract_disc.py walks it ---- */

typedef struct {
  char path[128]; /* "S0/CRASH.EXE" */
  unsigned lba, size;
  int xa;
} Entry;

typedef struct {
  Entry v[MAX_FILES];
  int n;
} Files;

static int sector_data(PsxPbp *p, unsigned lba, unsigned char raw[PSX_PBP_SECTOR]) {
  return psx_pbp_read_sectors(p, (int)lba, 1, raw);
}

/* The tree is walked by the kit's reader over the copy's sectors (D158: one
 * walker for images and the copy, with its limits on depth and directory
 * size); this collects what it finds. */
typedef struct {
  PsxPbp *p;
  Files *files;
  PsxPbpError *err;
} Collect;

int psx_pbp_source(void *user, int lba, unsigned char raw[PSX_PBP_SECTOR]) {
  return psx_pbp_read_sectors(user, lba, 1, raw);
}

static int collect_entry(void *user, const VpkDiscEntry *e) {
  Collect *c = user;
  Entry *f;
  const char *s;
  /* a path that would leave the folder (the extractor writes these as paths): the
   * walker joins names with '/', so a part that is empty, "." or ".." or holds '\' is hostile */
  for (s = e->path; *s;) {
    const char *q = s;
    while (*q && *q != '/')
      q++;
    if (q == s || (q - s == 1 && s[0] == '.') || (q - s == 2 && s[0] == '.' && s[1] == '.') || memchr(s, '\\', (size_t)(q - s)))
      return fail(c->err, PSX_PBP_ERR_BLOCK, "a file name on the disc isn't a PlayStation disc's");
    s = *q ? q + 1 : q;
  }
  if (e->xa_attr & 0x4000)
    return fail(c->err, PSX_PBP_ERR_BLOCK, "%s is a CD-DA track, which isn't supported", e->path);
  if (c->files->n == MAX_FILES)
    return fail(c->err, PSX_PBP_ERR_BLOCK, "more than %d files on the disc", MAX_FILES);
  if (e->lba < 0 || e->size < 0 || (unsigned)e->size > (unsigned)psx_pbp_sectors(c->p) * 2048u ||
      (unsigned)e->lba + ((unsigned)e->size + 2047) / 2048 > (unsigned)psx_pbp_sectors(c->p))
    return fail(c->err, PSX_PBP_ERR_BLOCK, "%s lies past the image (%d sectors from %d)", e->path, e->size, e->lba);
  f = &c->files->v[c->files->n++];
  snprintf(f->path, sizeof f->path, "%s", e->path);
  f->lba = (unsigned)e->lba;
  f->size = (unsigned)e->size;
  f->xa = (e->xa_attr & 0x3000) != 0;
  if (!f->xa && f->size > 0) { /* the first sector's submode says Form 2 */
    unsigned char first[PSX_PBP_SECTOR];
    if (sector_data(c->p, f->lba, first) == 0 && (first[18] & 0x20))
      f->xa = 1;
  }
  return 0;
}

static int by_lba(const void *a, const void *b) {
  const Entry *x = a, *y = b;
  return (x->lba > y->lba) - (x->lba < y->lba);
}

static int make_dir(const char *path) {
#ifdef __vita__
  return sceIoMkdir(path, 0777) < 0 ? -1 : 0;
#else
  return mkdir(path, 0777);
#endif
}

/* out_dir/path's folders, made as needed */
static void make_parents(const char *out_dir, const char *path) {
  char full[512];
  size_t i, base = strlen(out_dir) + 1;
  if (base >= sizeof full || snprintf(full, sizeof full, "%s/%s", out_dir, path) >= (int)sizeof full)
    return; /* too long to open anyway: the fopen that follows says so */
  for (i = base; full[i]; i++)
    if (full[i] == '/') {
      full[i] = 0;
      make_dir(full);
      full[i] = '/';
    }
}

static int same(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
    if (x != y)
      return 0;
  }
  return !*a && !*b;
}

static const Entry *find_entry(const Files *files, const char *path) {
  int i;
  for (i = 0; i < files->n; i++)
    if (same(files->v[i].path, path))
      return &files->v[i];
  return NULL;
}

static int files_have(void *user, const char *name) { return find_entry(user, name) != NULL; }

/* The release the disc boots (SYSTEM.CNF), or the one whose executable it has. */
static const PsxRegion *disc_region(PsxPbp *p, Files *files) {
  const Entry *cnf = find_entry(files, "SYSTEM.CNF");
  unsigned char raw[PSX_PBP_SECTOR];
  char exe[64] = "";
  if (cnf && cnf->size > 0 && sector_data(p, cnf->lba, raw) == 0)
    psx_region_boot_exe((const char *)raw + 24, cnf->size < 2048 ? cnf->size : 2048, exe, sizeof exe);
  return psx_region_of_disc(exe, files_have, files);
}

static void remove_written(const char *out_dir, const Files *files, int upto) {
  char full[512];
  int i;
  for (i = 0; i < upto; i++) {
    snprintf(full, sizeof full, "%s/%s", out_dir, files->v[i].path);
    remove(full);
  }
}

/* The tree into files, sorted by LBA. */
static int list_files(PsxPbp *p, Files *files, PsxPbpError *err) {
  unsigned char raw[PSX_PBP_SECTOR];
  VpkDisc d;
  Collect c = {p, files, err};
  int r;
  files->n = 0;
  if (sector_data(p, 16, raw) != 0 || raw[24] != 1 || memcmp(raw + 25, "CD001", 5) != 0)
    return fail(err, PSX_PBP_ERR_BLOCK, "no CD file system at sector 16: not a PlayStation disc image");
  memset(&d, 0, sizeof d);
  if (vpk_disc_open_source(&d, psx_pbp_source, p, psx_pbp_sectors(p)) != 0)
    return fail(err, PSX_PBP_ERR_UNREADABLE, "out of memory");
  if (err)
    err->code = PSX_PBP_OK;
  r = vpk_disc_walk(&d, collect_entry, &c);
  vpk_disc_close(&d);
  if (r != 0) {
    if (!err || err->code == PSX_PBP_OK) /* the walker's own refusal: a directory it won't read */
      return fail(err, PSX_PBP_ERR_BLOCK, "the disc's file list can't be read (sector %d): too deep, too long or damaged", d.bad_lba);
    return -1;
  }
  qsort(files->v, (size_t)files->n, sizeof *files->v, by_lba);
  return 0;
}

int psx_pbp_extract(PsxPbp *p, const char *out_dir, int *region,
                    void (*progress)(void *user, int percent, const char *file), void *user,
                    PsxPbpError *err) {
  static Files files; /* 36 KB: not on the Vita's stack */
  unsigned char raw[PSX_PBP_SECTOR];
  const PsxRegion *r;
  unsigned long long total = 0, done = 0;
  char full[512], exe_hex[41], wad_hex[41];
  FILE *out;
  int i, last_pct = -1;
  if (err)
    err->code = PSX_PBP_OK;
  psx_pbp_set_edc(p, 1); /* the files as a dump's: the music's EDC computed */
  if (list_files(p, &files, err) != 0)
    return -1;
  r = disc_region(p, &files);
  if (!r) {
    char serial[32];
    const char *area;
    psx_region_serial(p->disc_id, serial, sizeof serial, &area);
    return fail(err, PSX_PBP_ERR_WRONG_DISC,
                "this is %s%s%s%s, not Spyro the Dragon. Spyro Vita needs Spyro the Dragon, USA "
                "(SCUS-94228) or Europe (SCES-01438).",
                serial[0] ? serial : "another game", *area ? " (" : "", area, *area ? ")" : "");
  }
  *region = r->id;
  if (!find_entry(&files, r->exe) || !find_entry(&files, "WAD.WAD"))
    return fail(err, PSX_PBP_ERR_WRONG_DISC, "%s or WAD.WAD is missing: this isn't Spyro the Dragon (%s)",
                r->exe, r->serial);
  for (i = 0; i < files.n; i++)
    total += (unsigned long long)(files.v[i].size + 2047) / 2048 * (files.v[i].xa ? PSX_PBP_SECTOR - 16 : 2048);
  exe_hex[0] = wad_hex[0] = 0;
  make_dir(out_dir);
  for (i = 0; i < files.n; i++) {
    const Entry *e = &files.v[i];
    unsigned nsect = (e->size + 2047) / 2048, k, left = e->size;
    PsxSha1 sha;
    int hashed = same(e->path, r->exe) || same(e->path, "WAD.WAD");
    psx_sha1_init(&sha);
    make_parents(out_dir, e->path);
    snprintf(full, sizeof full, "%s/%s", out_dir, e->path);
    out = fopen(full, "wb");
    if (!out) {
      remove_written(out_dir, &files, i);
      return fail(err, PSX_PBP_ERR_WRITE, "%s can't be written", full);
    }
    for (k = 0; k < nsect; k++) {
      const unsigned char *src;
      unsigned n;
      if (sector_data(p, e->lba + k, raw) != 0) {
        fclose(out);
        remove_written(out_dir, &files, i + 1);
        return fail(err, PSX_PBP_ERR_BLOCK, "%s: sector %u can't be read: the download is damaged?", e->path, e->lba + k);
      }
      if (e->xa) {
        src = raw + 16;
        n = PSX_PBP_SECTOR - 16;
      } else {
        src = raw + 24;
        n = left < 2048 ? left : 2048;
        left -= n;
      }
      if (fwrite(src, 1, n, out) != n) {
        fclose(out);
        remove_written(out_dir, &files, i + 1);
        return fail(err, PSX_PBP_ERR_WRITE, "%s: write failed (is the memory card full? the game needs 600 MB)", full);
      }
      if (hashed)
        psx_sha1_update(&sha, src, n);
      done += n;
      if (progress) {
        int pct = (int)(done * 100 / (total ? total : 1));
        if (pct != last_pct)
          progress(user, last_pct = pct, e->path);
      }
    }
    if (fclose(out) != 0) {
      remove_written(out_dir, &files, i + 1);
      return fail(err, PSX_PBP_ERR_WRITE, "%s: write failed (is the memory card full?)", full);
    }
    if (hashed)
      psx_sha1_hex(&sha, same(e->path, "WAD.WAD") ? wad_hex : exe_hex);
  }
  if (strcmp(exe_hex, r->exe_sha1) != 0 || strcmp(wad_hex, r->wad_sha1) != 0) {
    remove_written(out_dir, &files, files.n);
    return fail(err, PSX_PBP_ERR_BAD_DATA,
                "this is Spyro the Dragon (%s, %s), but not the release Spyro Vita knows: %s hashes %.8s "
                "(wants %.8s), WAD.WAD %.8s (wants %.8s).",
                r->area, r->serial, r->exe, exe_hex, r->exe_sha1, wad_hex, r->wad_sha1);
  }
  /* manifest.json last, as tools/extract_disc.py's json.dump(indent=2) writes it */
  snprintf(full, sizeof full, "%s/manifest.json", out_dir);
  out = fopen(full, "wb");
  if (!out) {
    remove_written(out_dir, &files, files.n);
    return fail(err, PSX_PBP_ERR_WRITE, "%s can't be written", full);
  }
  fputs("[", out);
  for (i = 0; i < files.n; i++) {
    const Entry *e = &files.v[i];
    fprintf(out, "%s\n  {\n    \"path\": \"%s\",\n    \"lba\": %u,\n    \"size\": %u,\n    \"type\": \"%s\"\n  }",
            i ? "," : "", e->path, e->lba, e->size, e->xa ? "xa" : "data");
  }
  fputs("\n]", out);
  if (fclose(out) != 0) {
    remove(full);
    remove_written(out_dir, &files, files.n);
    return fail(err, PSX_PBP_ERR_WRITE, "%s: write failed", full);
  }
  return 0;
}

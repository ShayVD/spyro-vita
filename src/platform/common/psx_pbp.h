/* The digital copy: a PS1 Classic's EBOOT.PBP as the PlayStation Store
 * installs it on the Vita (ux0:pspemu/PSP/GAME/<ID>/EBOOT.PBP) or a PSP
 * (decisions D157, github issue #132). Its DATA.PSAR is a PSISOIMG0000
 * image: the disc's raw 2352-byte sectors, 16 to a block, each block
 * compressed on its own (Sony's with the PSP's LZRC range coder; PSX2PSP's
 * with deflate), with a block table in a header that Sony's copies keep in a
 * PGD container (AMCTRL over KIRK's AES: pbp.c). The sectors' sync, MSF
 * header and error codes are zero in the image; the mode byte, the XA
 * subheader and the data are the disc's, so the game's files come out the
 * same as from a dump, and the XA music is kept. CD-DA tracks (none on
 * Spyro's discs) are stored elsewhere and not read.
 *
 * psx_pbp_open reads the table (a Sony copy: decrypts it; no licence or key
 * file is needed, the key is recovered from the header's own MAC),
 * psx_pbp_read_sectors serves sectors through a one-block cache, and
 * psx_pbp_extract writes the disc's files as tools/extract_disc.py does (a
 * manifest.json, data files, XA files as 2336-byte sectors), checking the
 * executable and WAD.WAD against the region table on the way. Plain C for
 * every host: the image is read by offset through the system's own calls
 * (pread, the Vita's sceIoPread; no stdio buffer), the extractor writes with
 * stdio. One PsxPbp is for one thread. */
#ifndef PSX_PBP_H
#define PSX_PBP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_PBP_SECTOR 2352
#define PSX_PBP_BLOCK_SECTORS 16

typedef struct PsxPbp PsxPbp;

/* Why psx_pbp_open or psx_pbp_extract failed (PsxPbpError.code). */
enum {
  PSX_PBP_OK,
  PSX_PBP_ERR_UNREADABLE, /* the file can't be opened or read */
  PSX_PBP_ERR_NOT_PBP,    /* not a PBP, or not a PS1 Classic (no PSISOIMG0000) */
  PSX_PBP_ERR_MULTI_DISC, /* PSTITLEIMG000000: a multi-disc title */
  PSX_PBP_ERR_PGD,        /* the header's MAC or key check failed: damaged, or a format we don't know */
  PSX_PBP_ERR_BLOCK,      /* a block won't decompress */
  PSX_PBP_ERR_WRITE,      /* the output can't be written (disk full?) */
  PSX_PBP_ERR_WRONG_DISC, /* not Spyro the Dragon, or not a release the region table has */
  PSX_PBP_ERR_BAD_DATA    /* the executable or WAD.WAD doesn't hash as a good dump */
};

typedef struct {
  int code;
  char text[512];
} PsxPbpError;

/* Opens path and reads its block table. On success *out is the handle
 * (psx_pbp_close frees it). */
int psx_pbp_open(PsxPbp **out, const char *path, PsxPbpError *err);
void psx_pbp_close(PsxPbp *p);

/* What the PBP says it is: PARAM.SFO's DISC_ID ("SCUS94228"; "" without
 * one), its TITLE, and whether the image header was a PGD (a Sony copy). */
const char *psx_pbp_disc_id(const PsxPbp *p);
const char *psx_pbp_title(const PsxPbp *p);
int psx_pbp_encrypted(const PsxPbp *p);
/* Sectors the image holds (the last block may pad past the disc's end). */
int psx_pbp_sectors(const PsxPbp *p);
/* count raw sectors from lba into out (count * PSX_PBP_SECTOR bytes). 0, or
 * -1 past the end or when a block is damaged. */
int psx_pbp_read_sectors(PsxPbp *p, int lba, int count, unsigned char *out);

/* Quick look without reading the image header: PARAM.SFO's DISC_ID of the
 * PBP at path ("" when its PARAM.SFO has none), or -1 if it isn't a PBP
 * (or is 2 GiB or more, which no single-disc PS1 Classic is). For choosing
 * among several copies before decrypting anything. */
int psx_pbp_peek_disc_id(const char *path, char *disc_id, size_t cap);

/* The file it was opened from. */
const char *psx_pbp_path(const PsxPbp *p);

/* Compute the sectors' error codes (zero in the image, present on a disc:
 * a Form 2 sector's EDC, a Form 1 sector's EDC and P/Q parity) when a block
 * is decoded: on for comparisons with a dump, off (the default) for the
 * game, which never reads them (D158). psx_pbp_extract computes them
 * whatever this says, and leaves the setting as it found it. */
void psx_pbp_set_edc(PsxPbp *p, int on);

/* Counts since the open, for the "cd:" lines (D158): blocks decoded, blocks
 * served from the cache, file reads (one per run of up to 8 blocks) with
 * their bytes, and the time in reads and in decoding (with a clock set). */
typedef struct {
  unsigned blocks, hits, reads;
  unsigned long long read_bytes, read_us, decode_us;
} PsxPbpStats;
void psx_pbp_set_clock(PsxPbp *p, unsigned long long (*now_us)(void));
void psx_pbp_stats(const PsxPbp *p, PsxPbpStats *out);
/* Another handle on the same image for another thread: its own file handle
 * and block cache, a copy of the table (nothing is decrypted again). */
int psx_pbp_clone(const PsxPbp *p, PsxPbp **out);

/* The disc's file table is the ISO9660 tree, walked by vita-port-kit's reader
 * over these sectors (vpk_disc_open_source, D158), as for an image: this is
 * the source to open it with (user: the PsxPbp). */
int psx_pbp_source(void *user, int lba, unsigned char raw[PSX_PBP_SECTOR]);

/* Writes the disc's files under out_dir as tools/extract_disc.py does, and
 * manifest.json last, after the executable and WAD.WAD were hashed and
 * matched the region table (psx_region.h); a mismatch leaves no manifest, so
 * the folder isn't taken for a disc. progress (may be NULL) gets 0..100 and
 * the file being written. The release's region index is returned in
 * *region (PSX_REGION_*). */
int psx_pbp_extract(PsxPbp *p, const char *out_dir, int *region,
                    void (*progress)(void *user, int percent, const char *file), void *user,
                    PsxPbpError *err);

#ifdef __cplusplus
}
#endif

#endif

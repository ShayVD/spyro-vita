/* The game's disc read straight from a raw disc image in the data folder
 * (src/platform/common/cdimage.c, decisions D60): a .cue and its .bin, or a
 * bare .bin/.img, 2352- or 2448-byte sectors, any pregap. Its ISO9660
 * directory gives libcd the same file table as tools/extract_disc.py's
 * manifest.json. Plain C and stdio, for every host. */
#ifndef PSX_CDIMAGE_H
#define PSX_CDIMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Why the disc couldn't be opened (psx_cd_error). */
enum {
  PSX_CD_OK,
  PSX_CD_ERR_NOT_FOUND,  /* no manifest.json and no disc image */
  PSX_CD_ERR_ISO,        /* a 2048-byte .iso: the XA music is gone */
  PSX_CD_ERR_CONVERT,    /* .chd, .pbp, .ecm: convert to bin/cue first */
  PSX_CD_ERR_WRONG_DISC, /* another game or release, or a bad dump */
  PSX_CD_ERR_BAD_IMAGE,  /* unreadable, damaged or cut short */
  PSX_CD_ERR_MANIFEST,   /* manifest.json there but unreadable */
};

typedef struct {
  int code;        /* PSX_CD_* */
  char title[40];  /* a few words, upper case fits the menu panel */
  char text[512];  /* plain sentences; '\n' starts a new line */
} PsxCdError;

/* After psx_cd_open (psx_hooks.h) returns non-zero: why, for the log and the
 * screen. code is PSX_CD_OK after a successful open. */
const PsxCdError *psx_cd_error(void);

/* What the last psx_cd_open opened: "files" (manifest.json) or the image's
 * path. */
const char *psx_cd_source(void);

/* WAD.WAD is hashed only when a marker path is set: the first open of an
 * image hashes it, and on a match writes the marker (the image's name, size
 * and modification time); later opens of the same image skip the hash.
 * SCUS_942.28 is hashed on every open. progress (may be NULL) is called
 * during the WAD.WAD hash with 0..100. Set before psx_cd_open. */
void psx_cd_set_verify(const char *marker_path, void (*progress)(int percent));

/* Disc reads so far (both backends): calls that went to the file system,
 * bytes they returned, and the time they took when a clock is set. */
typedef struct {
  unsigned reads, kbytes;
  unsigned long long us;
} PsxCdIo;
void psx_cd_set_clock(unsigned long long (*now_us)(void));
void psx_cd_io(PsxCdIo *out);

/* ---- cdimage.c, used by libcd.c ---- */

typedef struct {
  char path[128]; /* "WAD.WAD", "S0/CRASH.EXE" */
  int lba, size;  /* size: the directory's, 2048 bytes a sector */
  int xa;         /* Form 2 / interleaved: read as 2336-byte raw sectors */
} PsxCdImageFile;

/* Looks for an image in dir, opens it, walks its ISO9660 directory and
 * checks it is SCUS-94228. On success returns 0 and the file table (sorted by
 * LBA, malloc'd, the caller frees it); otherwise fills *err. */
int psx_cdimage_open(const char *dir, PsxCdImageFile **files, int *nfiles, PsxCdError *err);
void psx_cdimage_close(void);
const char *psx_cdimage_path(void);

/* n bytes from offset off of the 2352-byte raw sector at lba (off 16: the
 * subheader, 24: Form 1 data). Returns n, or -1 past the end of the image. */
int psx_cdimage_read(int lba, int off, void *buf, int n);

/* SHA-1 (cdimage.c), for the disc check and the tests. */
typedef struct {
  unsigned int h[5];
  unsigned long long len;
  unsigned char buf[64];
} PsxSha1;
void psx_sha1_init(PsxSha1 *s);
void psx_sha1_update(PsxSha1 *s, const void *data, unsigned n);
void psx_sha1_hex(PsxSha1 *s, char out[41]);

/* libcd.c's I/O accounting, for cdimage.c's reads */
void psx_cd_io_add(unsigned bytes, unsigned long long t0);
unsigned long long psx_cd_now(void);

#ifdef __cplusplus
}
#endif

#endif

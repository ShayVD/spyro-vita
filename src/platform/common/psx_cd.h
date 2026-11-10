/* CD drive state shared with the future XA decoder and mixer
 * (docs/porting-plan.md §6). Written only by libcd.c on the game thread. */
#ifndef PSX_CD_H
#define PSX_CD_H

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_CD_RAW_SECTOR 2336 /* subheader (2x4 bytes) + 2328 bytes */

typedef struct {
  int active;           /* ReadS/ReadN streaming */
  int lba;              /* sector under the head */
  unsigned char mode;   /* CdlMode* bits from the last Setmode */
  unsigned char file;   /* Setfilter */
  unsigned char chan;
  unsigned char muted;  /* Mute/Demute */
  unsigned char mix[4]; /* CdMix val0..3: L->L, L->R, R->R, R->L */
} PsxCdStream;

const PsxCdStream *psx_cd_stream(void);

/* One sector in XA raw form. Data-file sectors get a Form 1 data subheader
 * and their 2048 bytes at offset 8. Returns 0, or -1 for a sector that no
 * file covers (filled with zeros). */
int psx_cd_read_raw(int lba, unsigned char raw[PSX_CD_RAW_SECTOR]);

/* A data file of the open disc ("SCES_014.38", case ignored), whole: up to
 * cap bytes into buf; with buf NULL, its size. Returns the bytes, or -1 (no
 * such file, an XA file, a read error). For the region data loader (D133). */
int psx_cd_read_file(const char *path, void *buf, int cap);

/* Where the XA sector at lba lies in the files, for a reader of its own (the
 * Vita's read-ahead thread, D64): psx_cd_read_raw's bytes are `left`
 * consecutive sectors, the first at byte `offset` of `path`, one every
 * `stride` bytes, PSX_CD_RAW_SECTOR of them at `skip` into each. Only
 * reads tables that don't change once the disc is open, so any thread may
 * call it. 0, or -1 if lba isn't in an XA file. */
typedef struct {
  char path[512];
  long offset;
  int stride, skip, left;
} PsxCdXaLoc;
int psx_cd_xa_locate(int lba, PsxCdXaLoc *loc);

/* Where any sector at lba lies in the files, for the native reads (cd_native,
 * cdnative.c, D93): `left` consecutive sectors, the first at byte `offset` of
 * `path`, one every `stride` bytes. CdRead's 2048 bytes are at `data_skip`
 * into each, and psx_cd_read_raw's PSX_CD_RAW_SECTOR at `raw_skip` (xa
 * files; a data file's subheader is made up). `data_limit`: bytes of real
 * data from lba on (an image's data file: past them CdRead gets zeros), or
 * -1 (extracted files: the file's own length). Only reads tables that don't
 * change once the disc is open, so any thread may call it. 0, or -1 if no
 * file holds lba. */
typedef struct {
  char path[512];
  long offset, data_limit;
  int stride, data_skip, raw_skip, left, xa;
} PsxCdLoc;
int psx_cd_locate(int lba, PsxCdLoc *loc);

#ifdef __cplusplus
}
#endif

#endif

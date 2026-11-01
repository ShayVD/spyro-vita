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

#ifdef __cplusplus
}
#endif

#endif

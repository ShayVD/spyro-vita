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

#ifdef __cplusplus
}
#endif

#endif

/* XA ADPCM decoder for streamed CD audio (src/platform/common/xa.c). */
#ifndef PSX_XA_H
#define PSX_XA_H

#ifdef __cplusplus
extern "C" {
#endif

void psx_xa_reset(void);
void psx_xa_sector(int lba);      /* libcd: the head passed this sector */
/* psx_xa_sector is these two: 1 (and the sector in raw, PSX_CD_RAW_SECTOR
 * bytes) if the stream plays it, then the decode into the FIFO. */
int psx_xa_accept(int lba, unsigned char *raw);
/* psx_xa_accept's test of a sector already read (the stream must be in
 * real-time mode, CdlModeRT): 1 if it plays (the Vita's read-ahead, D64) */
int psx_xa_check(const unsigned char *raw);
void psx_xa_decode(const unsigned char *raw);
void psx_xa_next(int *l, int *r); /* SPU mixer: one 44.1 kHz frame */
void psx_xa_take(short *lr, int n); /* n frames of psx_xa_next, interleaved L R */
/* psx_xa_check with the stream's mode, file and channel given */
int psx_xa_check_filter(const unsigned char *raw, int mode, int file, int chan);

/* A sector decoded ahead of the stream (the Vita's XA read-ahead, D84):
 * from history key (per channel: older, old), leaving exit. */
#define PSX_XA_SECTOR_FRAMES (18 * 28 * 8) /* mono: 4032; stereo: half */
typedef struct PsxXaPcm {
  int key[2][2], exit[2][2];
  int rate, frames;
  short pcm[PSX_XA_SECTOR_FRAMES][2];
} PsxXaPcm;
void psx_xa_predecode(const unsigned char *raw, const int hist[2][2], PsxXaPcm *out);
int psx_xa_decoded(const PsxXaPcm *pcm); /* 1: pushed (key matched); 0: decode raw instead */

#ifdef __cplusplus
}
#endif

#endif

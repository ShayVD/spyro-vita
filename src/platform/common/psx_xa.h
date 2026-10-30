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
void psx_xa_decode(const unsigned char *raw);
void psx_xa_next(int *l, int *r); /* SPU mixer: one 44.1 kHz frame */

#ifdef __cplusplus
}
#endif

#endif

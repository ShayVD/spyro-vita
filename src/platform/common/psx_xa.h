/* XA ADPCM decoder for streamed CD audio (src/platform/common/xa.c). */
#ifndef PSX_XA_H
#define PSX_XA_H

#ifdef __cplusplus
extern "C" {
#endif

void psx_xa_reset(void);
void psx_xa_sector(int lba);      /* libcd: the head passed this sector */
void psx_xa_next(int *l, int *r); /* SPU mixer: one 44.1 kHz frame */

#ifdef __cplusplus
}
#endif

#endif

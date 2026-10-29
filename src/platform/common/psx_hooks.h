/* Calls between the platform layer (src/platform/<host>) and the shared PS1
 * hardware models in src/platform/common. */
#ifndef PSX_HOOKS_H
#define PSX_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

/* libcd over files (src/platform/common/libcd.c). */
int psx_cd_open(const char *disc_dir); /* reads <disc_dir>/manifest.json; 0 = ok */
void psx_cd_vblank(void);              /* once per virtual vblank, before the VSync callback */
/* CD ticks a CdRead waits before it completes (default 1). A tick is each
 * psx_cd_vblank() and each CdSync(1, ...) poll while a read is pending; 0
 * completes the read (and fires the callback) inside CdRead. */
void psx_cd_set_read_latency(int ticks);
void psx_cd_set_log(int on); /* one stderr line per CdRead; also SPYRO_CD_LOG=1 */

/* SPU (src/platform/common/libspu.c). */
void psx_spu_vblank(void); /* once per virtual vblank */

/* Memory cards (src/platform/common/libmcrd.c): <dir>/card0, card1. */
void psx_mcrd_set_root(const char *dir);

#ifdef __cplusplus
}
#endif

#endif

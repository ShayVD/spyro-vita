/* Calls between the platform layer (src/platform/<host>) and the shared PS1
 * hardware models in src/platform/common. */
#ifndef PSX_HOOKS_H
#define PSX_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

/* libcd over the disc (src/platform/common/libcd.c): <disc_dir>/manifest.json
 * and the extracted files, or without one a disc image in disc_dir (D60).
 * 0 = ok; else psx_cd_error() (psx_cdimage.h) says why. */
int psx_cd_open(const char *disc_dir);
void psx_cd_vblank(void);              /* once per virtual vblank, before the VSync callback */
/* CD ticks a CdRead waits before it completes (default 1). A tick is each
 * psx_cd_vblank() and each CdSync(1, ...) poll while a read is pending; 0
 * completes the read (and fires the callback) inside CdRead. */
void psx_cd_set_read_latency(int ticks);
void psx_cd_set_log(int on); /* one stderr line per CdRead; also SPYRO_CD_LOG=1 */

/* SPU (src/platform/common/libspu.c). */
void psx_spu_vblank(void); /* once per virtual vblank */

/* Per-vblank game-state trace (src/game/trace.c). */
int spyro_trace_open(const char *path); /* 0 = ok */
void spyro_trace_vblank(int vblank);
void spyro_trace_close(void);
void spyro_moby_dump(const char *path); /* level mobys as TSV (trace.c) */

/* Function-level oracle replay (src/game/oracle_replay.c). */
int spyro_replay(const char *dir, const char *only, const char *ignore, const char *fnmap);

/* Memory cards (src/platform/common/libmcrd.c): <dir>/card0, card1. */
void psx_mcrd_set_root(const char *dir);

#ifdef __cplusplus
}
#endif

#endif

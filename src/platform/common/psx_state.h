/* Save states (src/game/savestate.c, decisions D51): each PS1 model in
 * src/platform/common writes and reads its own state through one function,
 * and the platform provides the hooks at the end. */
#ifndef PSX_STATE_H
#define PSX_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PSX_STATE_SIZE, PSX_STATE_SAVE, PSX_STATE_LOAD };

typedef struct PsxStateIO {
  int mode;         /* PSX_STATE_*: count the bytes, write them to p, or read them from p */
  unsigned char *p; /* the chunk's bytes (SAVE, LOAD) */
  size_t n;         /* bytes so far */
} PsxStateIO;

/* One field of a model's state, in the direction io->mode says. */
static inline void psx_state_io(PsxStateIO *io, void *v, size_t n) {
  if (io->mode == PSX_STATE_SAVE)
    memcpy(io->p + io->n, v, n);
  else if (io->mode == PSX_STATE_LOAD)
    memcpy(v, io->p + io->n, n);
  io->n += n;
}

void psx_cd_state(PsxStateIO *io);   /* libcd.c: drive and XA stream */
void psx_xa_state(PsxStateIO *io);   /* xa.c: decoded FIFO and decoder */
void psx_mcrd_state(PsxStateIO *io); /* libmcrd.c: the pending card result */

/* Provided by the platform (src/platform/<host>/savestate_*.c). */
void savestate_plat_set_vblank(int vblank); /* VSync(-1) from now on */
/* Before and after the audio chunks: no mixing in flight, and every voice's
 * decoder state in g_psx_spu (save); the mixer follows g_psx_spu (load). */
void savestate_plat_audio(int mode);
void savestate_plat_audio_done(int mode);
/* VRAM, 1024x512, PS1-exact: the pixels to save, or where to load them;
 * then _done (load: the renderer picks them up). */
uint16_t *savestate_plat_vram(int mode);
void savestate_plat_vram_done(int mode);

#ifdef __cplusplus
}
#endif

#endif

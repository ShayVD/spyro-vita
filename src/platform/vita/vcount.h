/* The display's vblank counter, widened (decisions D89). On hardware
 * sceDisplayGetVcount counts in 16 bits: after 0xffff it is 0 again, 18 min
 * 13 s of display time. vblank.c compared it with a 32-bit target, so at the
 * wrap VSync's wait never ended (a freeze in the owner's play, crash dump
 * 2026-11-03). This counts its steps instead, modulo 0x10000, so the count
 * only grows; vblank.c reads it every frame, far more often than once a
 * wrap. Plain C (no SCE headers): tests/vcount runs it on the host. */
#ifndef SPYRO_VITA_VCOUNT_H
#define SPYRO_VITA_VCOUNT_H

typedef struct {
  unsigned raw;   /* the counter's last value */
  unsigned count; /* the widened count */
  int valid;
} VitaVcount;

/* The widened count for the counter's value raw (16 bits or wider: only the
 * low 16 bits of each step are used). */
static inline int vita_vcount_widen(VitaVcount *v, unsigned raw) {
  if (v->valid)
    v->count += (raw - v->raw) & 0xffffu;
  else
    v->count = raw & 0xffffu;
  v->raw = raw;
  v->valid = 1;
  return (int)v->count;
}

#endif

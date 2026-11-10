/* vpk_vcount: the display's vblank counter, widened. On hardware sceDisplayGetVcount counts in 16 bits: after 0xffff
 * it is 0 again, 65,536 vblanks (18 min 13 s at 59.94 Hz) after boot. A 32-bit compare against it froze every
 * session of a port at that minute. vpk_vcount_widen counts the counter's steps instead, modulo 0x10000, so the count
 * only grows; read it at least once per wrap (every frame is plenty). Plain C, no SCE headers: tested on the host.
 * From spyro-vita src/platform/vita/vcount.h @ bb62f38 (its decision D90). */
#ifndef VPK_VCOUNT_H
#define VPK_VCOUNT_H

typedef struct {
  unsigned raw;   /* the counter's last value */
  unsigned count; /* the widened count */
  int valid;      /* a flag, not a sentinel count: a save state may set any count */
} VpkVcount;

/* The widened count for the counter's value raw (16 bits or wider: only the low 16 bits of each step are used).
 * The first call starts the count at raw's low 16 bits. */
static inline unsigned vpk_vcount_widen(VpkVcount *v, unsigned raw) {
  if (v->valid)
    v->count += (raw - v->raw) & 0xffffu;
  else
    v->count = raw & 0xffffu;
  v->raw = raw;
  v->valid = 1;
  return v->count;
}

/* Start counting afresh from the next read (after a pause the caller does not want to catch up on). */
static inline void vpk_vcount_reset(VpkVcount *v) { v->valid = 0; }

#endif

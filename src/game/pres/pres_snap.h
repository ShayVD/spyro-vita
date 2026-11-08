/* The presenter's snapshot (stage 7e, decisions D124; pres_snap.c).
 *
 * What the presenter's draw reads of the game's changing state, as its own
 * copies, taken after the update at D30's pass's point:
 *  - the labels pres_snap_names.h names, each at its own offset in a
 *    mirror of the data region (g_pres_mirror, tools/pres_snap_syms.py);
 *  - the moby array and the particle space, copied into the presenter's
 *    area (render_ctx_area), and the pointers into them rebased onto the
 *    copies;
 *  - the primitive buffers and OTs: the presenter's own, a mirror of the
 *    game's layout in the area (the same sizes, so the same bounds trip).
 * Then the camera, Spyro, the mobys and the particles are blended in the
 * copies, as present.c's blend_state blends the live ones for D30's pass. */
#ifndef PRES_SNAP_H
#define PRES_SNAP_H

#include <stdint.h>

typedef struct {
  const char *name;
  unsigned off, len; /* in the mirror (from 0x8006BBE0), to the next label */
} PresSnapLabel;
extern const PresSnapLabel g_pres_snap_labels[];
extern const int g_pres_snap_nlabels;
extern const unsigned g_pres_mirror_len;
extern unsigned char g_pres_mirror[];

/* The game's labels as the presenter's copies, raw (no rebase, no blend):
 * --render-ctx-check gives the renderers' presenter builds the game's
 * state through these. */
void pres_snap_from_live(void);

/* The snapshot from the game's state now: labels, mobys, particles,
 * buffers, rebased; then blended unless blend is 0. Returns 0 if the
 * camera jumped (present.c: no in-between frame then) or the area isn't
 * there. */
int pres_snap_take(int blend);

/* Where the presenter's copies are, for the check and the audit: the
 * game's address range of a region and its copy's. */
typedef struct {
  uint32_t lo, len, to;
} PresSnapRegion;
enum { PRES_REGION_BUFFERS, PRES_REGION_MOBYS, PRES_REGION_PARTICLES, PRES_REGION_MIRROR, PRES_REGIONS };
extern PresSnapRegion g_pres_regions[PRES_REGIONS];
/* a game address moved onto its copy, if it is in one of the regions */
uint32_t pres_snap_rebase(uint32_t a);

/* n words, stride bytes apart from p: those pointing into the moby array
 * or the particle space moved onto the copies (the audit's fields) */
void pres_snap_fields(unsigned char *p, uint32_t n, uint32_t stride);

/* --pres-audit: count the words of the copies (labels, mobys, particles)
 * that point into the moby array or the particle space, by where they sit;
 * pres_snap_audit_report logs them. */
extern int g_pres_audit;
void pres_snap_audit_report(void (*log)(const char *fmt, ...));

#endif

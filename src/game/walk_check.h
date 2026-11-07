/* The host's walk checks against PsyCross's walk (stage 8d, D117).
 *
 * The reference build (SPYRO_PSYCROSS_REF, the arena build) links PsyCross's
 * PsyX_GPU.cpp beside our gpu_walk.c, every symbol of it renamed psyx_*
 * (cmake/psycross.cmake). walk_check.c drives it:
 *  - --walk-check: every packet the walk parses goes through both walks
 *    from the same state, and the vertices, the splits and the draw
 *    environment each leaves must be the same bytes;
 *  - walk_ref_*: PsyCross's walk for native_draw_check.c's checks.
 * In a build without the reference these say so and do nothing. */
#ifndef WALK_CHECK_H
#define WALK_CHECK_H

#include "psx_gpu.h"

/* 1 in a build with the reference walk */
extern const int g_walk_ref_built;

/* --walk-check: on from now (log: its lines). 0, or -1 without the
 * reference. WALK_MUTATE=1 or 2 in the environment turns on gpu_walk.c's
 * mutants, which the check must catch. */
int walk_check_init(void (*log)(const char *fmt, ...));
/* the counts ("walk check" lines) */
void walk_check_report(void);
/* --walk-check-synth N: after walk_check_init, n random packets of every
 * GP0 kind (the ones no run reaches too: rectangles, the fill, DR_MOVE,
 * DR_LOAD, E1-E6, polylines, PsyCross's 0xB1/0xB2), each also as DrawPrim
 * gives a primitive, through both walks under random draw state. Returns
 * the packets that differed. The VRAM calls are made: run it alone. */
unsigned long long walk_check_synth(unsigned n, unsigned seed);

/* PsyCross's walk, the reference library's */
/* its state from ours: the draw and display environments, the current
 * split and the vertex fill, the custom texture, the vertex cache's hook */
void walk_ref_sync(void);
/* ParsePrimitivesLinkedList from p (next: as psx_gpu_walk's) */
void walk_ref_parse(u_long *p, int single, PsxGpuNext next);
GrVertex *walk_ref_vertices(int *fill);
int walk_ref_split_count(void);
const PsxGpuSplit *walk_ref_split(int i);
void walk_ref_split_get(int i, int out[8]);
void walk_ref_clear(void);
DRAWENV *walk_ref_env(void);

#endif

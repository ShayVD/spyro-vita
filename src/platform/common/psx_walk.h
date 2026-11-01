/* The OT walk from a copy of the frame's primitives (walk.c, decisions D71):
 * PsyCross's parse of a DrawOTag list, done later or on another thread while
 * the game writes its next frame. */
#ifndef PSX_WALK_H
#define PSX_WALK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A span of PS1 RAM a list's packets live in, as offsets into the arena
 * (PS1 address - 0x80000000), lo inclusive, hi exclusive. */
typedef struct PsxWalkRange {
  uint32_t lo, hi;
} PsxWalkRange;

/* The ranges to copy for the list DrawOTag is given now (src/game/present.c
 * port_present_prim_ranges): returns how many it wrote, at most max. */
typedef int (*PsxWalkRangesFn)(PsxWalkRange *r, int max);

/* Mode 2's walker thread, from the platform. */
typedef struct {
  void (*post)(void);     /* the walker should call psx_walk_run() once */
  void (*wait)(void);     /* until that psx_walk_run() has returned */
  int (*on_walker)(void); /* 1 on the walker's thread */
  void (*submitted)(void); /* on the game thread, as a list is handed over */
} PsxWalkThread;

enum {
  PSX_WALK_OFF = 0,  /* DrawOTag walks the list in place, as PsyCross does */
  PSX_WALK_LAZY = 1, /* the game thread walks the copy at its next gate (host) */
  PSX_WALK_THREAD = 2 /* the platform's thread walks the copy at once */
};

/* 0 = on; else the walk stays in place (no 16 MB-aligned memory for the
 * copy). thr is for PSX_WALK_THREAD. Call once, before the game draws. */
int psx_walk_init(int mode, PsxWalkRangesFn ranges, const PsxWalkThread *thr);
int psx_walk_mode(void);

/* The walker: walk the list handed over last (PsxWalkThread.post). */
void psx_walk_run(void);

/* Game thread. Everything handed over has been walked and what was
 * deferred has run. PsyCross's own calls and VSync do this first
 * (g_psyxGpuGate); so must anything else that reads or writes the GPU state
 * a walk uses. A no-op on the walker. */
void psx_walk_join(void);
/* 1 while a list handed over hasn't been joined (game thread). */
int psx_walk_pending(void);
/* fn(copy of arg) once the walk handed over has finished, in the order
 * called, on the game thread (at the join); at once if none is pending.
 * For what the game thread does to the GPU state right after a DrawOTag
 * (present.c: the capture, the snapshot of the draw environment). */
void psx_walk_after(void (*fn)(void *), const void *arg, int size);

/* Counters since the last psx_walk_stats_take(). */
typedef struct {
  unsigned walks;      /* lists handed over */
  unsigned in_place;   /* lists DrawOTag walked in place (head outside the arena or the copy) */
  unsigned long long copied; /* bytes copied */
  unsigned long long copy_us; /* game thread: copying (the platform's clock) */
  unsigned foreign;    /* packets outside the copied ranges, read in place */
  uint32_t foreign_at; /* the last one's PS1 address */
  unsigned joins;      /* joins that waited for the walker */
  unsigned long long wait_us; /* game thread: waiting for the walker */
  unsigned long long walk_us; /* the walker: walking */
  unsigned deferred;   /* psx_walk_after calls that waited for a walk */
} PsxWalkStats;
void psx_walk_stats_take(PsxWalkStats *out);
/* The clock the stats use, microseconds (NULL: none, times stay 0). */
void psx_walk_set_clock(unsigned long long (*now_us)(void));

#ifdef __cplusplus
}
#endif

#endif

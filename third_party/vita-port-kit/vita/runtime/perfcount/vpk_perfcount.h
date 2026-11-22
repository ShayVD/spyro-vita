/* vpk_perfcount: per-frame work counters with names (draw calls, texture uploads, state changes, calls still
 * emulated, allocations), summed over a window of frames and logged as one line per window:
 *   PREFIX: w FIRST LAST FRAMES name=total name=total ...
 * vita/tools/perf_report.py reads the lines and compares two runs. The same lines come from Vita3K and from the
 * Vita, so a run's counts compare across both; the counts are work, not time (Vita3K's timing is not evidence).
 * vpk_pc_register must be called from one thread, before any other thread calls vpk_pc_add (the table is not
 * locked). vpk_pc_add may be called from any thread; vpk_pc_frame from one thread, once per game frame. Plain C, no SCE
 * headers: tested on the host. */
#ifndef VPK_PERFCOUNT_H
#define VPK_PERFCOUNT_H

#define VPK_PC_MAX 32     /* counters */
#define VPK_PC_NAME_MAX 23 /* characters in a name: letters, digits, '_' and '.' */

typedef struct VpkPerfcountConfig {
    unsigned window;                   /* frames per line (0: 300) */
    const char *prefix;                /* the lines' prefix (0: "vpk-perfcount") */
    void (*log)(const char *fmt, ...); /* where the lines go (vpk_log; 0: stdout) */
} VpkPerfcountConfig;

/* optional; before the first vpk_pc_frame. A null config keeps the defaults. */
void vpk_pc_init(const VpkPerfcountConfig *config);
/* the counter's id, or -1 for a bad name or a full table; a name registered twice gets its first id */
int vpk_pc_register(const char *name);
/* adds n to the counter; a bad id is ignored */
void vpk_pc_add(int id, unsigned n);
/* ends one game frame; logs a line and starts a new window when the window is full */
void vpk_pc_frame(unsigned frame);
/* logs the frames counted since the last line, if any (call at the run's stop, before its last line) */
void vpk_pc_flush(void);

#endif

/* What our libgpu (libgpu.c, stage 8b, D115) owns and calls besides PsyQ's
 * API, which libgpu.h declares. Before 8b these were PsyCross patches'
 * hooks in LIBGPU.C (0005, 0007, 0008, 0009); now they are direct calls. */
#ifndef PSX_LIBGPU_H
#define PSX_LIBGPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nonzero makes ClearImage, LoadImage, StoreImage and MoveImage do nothing.
 * A presentation pass re-runs the game's draw; VRAM bookkeeping there (the
 * pause screen saves the texture pages and overwrites them) must happen
 * once, in the real draw. Set and cleared by present.c (was PsyCross's
 * g_psyxVramFrozen, patch 0008). A walk on another thread reads the value
 * taken at its handover (psx_walk_vram_frozen, walk.c). */
extern int g_psx_vram_frozen;

/* DrawSync's callback (DrawSyncCallback); saved in a save state's GPU chunk
 * (savestate.c), so it keeps LIBGPU.C's name. */
extern void (*drawsync_callback)(void);

/* present.c: called first by DrawOTag; nonzero skips the list (the done
 * call still runs). The post call runs after the list is drawn or handed to
 * the walk. Both do nothing until a presentation tick has run. */
int port_present_ot_pre(unsigned long *p);
void port_present_ot_post(unsigned long *p);

#ifdef __cplusplus
}
#endif

#endif

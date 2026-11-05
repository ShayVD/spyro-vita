/* PsyQ's libetc for the port (stage 7b, D104), in place of PsyCross's
 * LIBETC.C (cmake/psycross.cmake leaves it out, as it does LIBCD.C, LIBSPU.C
 * and LIBMCRD.C): VSync, VSyncCallback and ResetCallback over the clock
 * (psx_clock.h), with LIBETC.C's semantics and our two PsyCross patches'
 * hooks (0005 g_psyxVSyncHook, 0009 g_psyxGpuGate). The game calls
 * VSyncCallback, StopCallback and VSync; LIBGPU.C calls GetVideoMode. */
#include "psx_clock.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdint.h>

#include <libetc.h>
#include <libgte.h> /* libgpu.h needs its SVECTOR */
#include <libgpu.h>

void (*vsync_callback)(void) = NULL;
int (*g_psyxVSyncHook)(int mode) = NULL;

static int s_vmode = MODE_NTSC;

int VSync(int mode) {
  if (g_psyxGpuGate) /* an OT walk still running elsewhere finishes first (D71) */
    g_psyxGpuGate();
  if (g_psyxVSyncHook)
    return g_psyxVSyncHook(mode);
  if (mode == 0)
    psx_platform_vsync();
  return psx_clock_count(); /* mode < 0, and mode > 0 without waiting, as LIBETC.C */
}

int VSyncCallback(void (*f)(void)) {
  int old = (int)(intptr_t)vsync_callback;
  vsync_callback = f;
  return old;
}

int ResetCallback(void) {
  int old = (int)(intptr_t)vsync_callback;
  vsync_callback = NULL;
  return old;
}

int StopCallback(void) { return 0; }

void psx_clock_callback(void) {
  if (vsync_callback)
    vsync_callback();
}

int SetVideoMode(int mode) {
  int old = s_vmode;
  s_vmode = mode;
  return old;
}

int GetVideoMode(void) { return s_vmode; }
